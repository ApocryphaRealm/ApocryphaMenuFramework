#pragma once

// 2.2.2: a guard for reading a Papyrus variable slot that another thread, or the VM's own set-up, may not have finished writing.
//
// WHY (Nicallia's crash, 2026-10-17, a new game on Skyrim 1.7.104): the MCM script loader reads SkyUI configs' "_configManager"
// from an SKSE task (the main thread) while the Papyrus VM runs on its own threads. RE::BSScript::Variable::GetObject() copies the
// slot's smart pointer, which raises the object's reference count - a WRITE through whatever pointer the slot holds. The slot said
// "object" (its type word is read on its own) but held 0xB2CB0001, so the copy wrote through a made-up address and the game died.
// Nothing about a type check protects against a half-written slot: the type and the value are two separate words.
//
// So a slot's pointer is looked at as a number FIRST. A pointer the allocator could not have handed out (null, below the first
// 64 KB, not 8-byte aligned, above the user-mode ceiling) or one that points at memory that is not mapped is "unstable": the caller
// skips that object this pass and a later pass looks again. Nothing is dereferenced to decide that.
//
// No game headers here, so tools\slot_guard_check.cpp can prove the exact crash value is refused without a game or a VM.

#include <cstddef>
#include <cstdint>
#include <string.h>  // _strnicmp

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

// wingdi.h #defines GetObject to GetObjectW/A, which would rename RE::BSScript::Variable::GetObject() in every file that includes
// this after CommonLib has undone it (CommonLib's own Variable.h does the same #undef).
#ifdef GetObject
#undef GetObject
#endif

namespace slotguard
{
	enum class Slot
	{
		kNotPointer,  // the slot's type holds no pointer (an int, a bool, a float ...) - nothing to guard
		kNone,        // a pointer type holding null: "none"
		kPointer,     // a pointer type holding something that looks like a live allocation
		kUnstable,    // a pointer type holding something that cannot be a live allocation - skip it, look again later
	};

	// Could the allocator have handed this out? User-mode 64-bit address, past the first 64 KB, 8-byte aligned (every Papyrus
	// object, array and interned string is).
	[[nodiscard]] inline bool PlausiblePointer(std::uintptr_t a_value) noexcept
	{
		return a_value >= 0x10000 && a_value <= 0x00007FFFFFFFFFFFull && (a_value & 7) == 0;
	}

	// Is [a_p, a_p + a_size) mapped, committed and readable right now? Reads nothing from it. (A snapshot: the memory can still be
	// freed a moment later - this is a net for made-up values, not a lock.)
	[[nodiscard]] inline bool Readable(const void* a_p, std::size_t a_size) noexcept
	{
		auto at = reinterpret_cast<std::uintptr_t>(a_p);
		const auto end = at + a_size;
		while (at < end)
		{
			MEMORY_BASIC_INFORMATION info{};
			if (::VirtualQuery(reinterpret_cast<const void*>(at), &info, sizeof(info)) != sizeof(info)) { return false; }
			if (info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) { return false; }
			constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
			if (!(info.Protect & readable)) { return false; }
			at = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
		}
		return true;
	}

	// a_pointerType: the slot's type word says object / array / string. a_value: the slot's second word, read raw.
	// a_span: how many bytes the pointer's target must have readable (an object's reference count sits at +0x28).
	[[nodiscard]] inline Slot Classify(bool a_pointerType, std::uintptr_t a_value, std::size_t a_span = 0x30) noexcept
	{
		if (!a_pointerType) { return Slot::kNotPointer; }
		if (a_value == 0) { return Slot::kNone; }
		if (!PlausiblePointer(a_value)) { return Slot::kUnstable; }
		return Readable(reinterpret_cast<const void*>(a_value), a_span) ? Slot::kPointer : Slot::kUnstable;
	}

	// The slot's value word. A Papyrus Variable is { type word (8), value (8) } - sizeof 0x10, asserted by CommonLib.
	[[nodiscard]] inline std::uintptr_t RawValue(const void* a_variable) noexcept
	{
		if (!a_variable) { return 0; }   // no slot: reads as "none" (Classify's a_value == 0)
		return reinterpret_cast<const std::uintptr_t*>(a_variable)[1];
	}

	// ------------------------------------------------------------------------------------------------ attached scripts
	//
	// 2.3.0 - WHY (a Nolvus user's crash on 2.2.3, 1.5.97, 2026-10-25): the script loader asked the VM
	// FindBoundObject(<quest handle>, "SKI_ConfigBase") for every quest, from an SKSE task. Inside that call the ENGINE walked the
	// scripts attached to QuickLootIE's MCM quest, found a script object whose type word read 0x72, and copied it as a smart
	// pointer - "lock inc [rax]" with rax = 0x72, a write to address 0x72, and the game died. The engine checks nothing before
	// that write, and a null check in our code cannot reach inside it.
	//
	// So the loader now walks the attached-script list itself (under the VM's own attachedScriptsLock) and judges every word
	// from its number and VirtualQuery BEFORE any reference count is touched: the script object, its type, each parent type and
	// each type's name. Anything that cannot be a live allocation is "unstable": the quest is left for a later pass (rule 17).
	// Like the slot guard above, this is a net for made-up values, not a lock on the object's memory.
	//
	// The layout (CommonLibSSE-NG's own offset comments, the same in both build lines; McmScripts.cpp static_asserts the sizes):
	//   AttachedScript   = one word: BSTSmartPointer<Object> with flag bit 0
	//   Object           +0x08 type (BSTSmartPointer<ObjectTypeInfo>), reference count at +0x28, variables from +0x30
	//   ObjectTypeInfo   +0x08 name (BSFixedString = pointer to the pooled characters), +0x10 parentTypeInfo; sizeof 0x38
	inline constexpr std::size_t kObjectTypeOffset = 0x08;
	inline constexpr std::size_t kObjectSpan = 0x30;
	inline constexpr std::size_t kTypeNameOffset = 0x08;
	inline constexpr std::size_t kTypeParentOffset = 0x10;
	inline constexpr std::size_t kTypeSpan = 0x38;
	inline constexpr int kMaxTypeDepth = 64;  // a Papyrus inheritance chain is a handful deep; more is a loop in garbage

	enum class Script
	{
		kNoMatch,   // a live-looking script of another class
		kMatch,     // a live-looking script of the class asked for, or derived from it
		kUnstable,  // some word on the way cannot be live - leave the whole quest for a later pass
	};

	[[nodiscard]] inline std::uintptr_t WordAt(std::uintptr_t a_address) noexcept
	{
		return *reinterpret_cast<const volatile std::uintptr_t*>(a_address);
	}

	// An attached-script word as a pointer: flag bit 0 cleared.
	[[nodiscard]] inline std::uintptr_t AttachedPointer(std::uintptr_t a_word) noexcept { return a_word & ~static_cast<std::uintptr_t>(1); }

	// A pooled string's characters: user-mode, past 64 KB (no alignment rule for character data), readable for the length compared.
	[[nodiscard]] inline bool NameIs(std::uintptr_t a_name, const char* a_want) noexcept
	{
		if (!a_want || a_name < 0x10000 || a_name > 0x00007FFFFFFFFFFFull) { return false; }
		std::size_t length = 0;
		while (a_want[length]) { ++length; }
		if (!Readable(reinterpret_cast<const void*>(a_name), length + 1)) { return false; }
		return ::_strnicmp(reinterpret_cast<const char*>(a_name), a_want, length + 1) == 0;
	}

	// a_object: one attached-script pointer (flag cleared). a_class: the class asked for; a_pooled: the same name's pooled string
	// pointer (two interned names are the same pointer; the character compare covers anything else). Reads only words it has
	// judged readable; writes nothing.
	[[nodiscard]] inline Script JudgeScript(std::uintptr_t a_object, const char* a_class, std::uintptr_t a_pooled) noexcept
	{
		if (!PlausiblePointer(a_object) || !Readable(reinterpret_cast<const void*>(a_object), kObjectSpan)) { return Script::kUnstable; }
		std::uintptr_t type = WordAt(a_object + kObjectTypeOffset);
		if (type == 0) { return Script::kUnstable; }  // a bound script object without a type is not finished
		for (int depth = 0; depth < kMaxTypeDepth; ++depth)
		{
			if (!PlausiblePointer(type) || !Readable(reinterpret_cast<const void*>(type), kTypeSpan)) { return Script::kUnstable; }
			const std::uintptr_t name = WordAt(type + kTypeNameOffset);
			if (name != 0 && (name < 0x10000 || name > 0x00007FFFFFFFFFFFull)) { return Script::kUnstable; }
			if (name != 0 && ((a_pooled != 0 && name == a_pooled) || NameIs(name, a_class))) { return Script::kMatch; }
			type = WordAt(type + kTypeParentOffset);
			if (type == 0) { return Script::kNoMatch; }  // the root type: no parent
		}
		return Script::kUnstable;  // deeper than any real chain
	}
}
