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
		return reinterpret_cast<const std::uintptr_t*>(a_variable)[1];
	}
}
