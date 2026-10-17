// Offline proof for include/SlotGuard.h (2.2.2). Builds slots the way a Papyrus Variable lays them out - { type word, value word } -
// and checks what the MCM script loader now does with each, including the exact value from the 2026-10-17 crash report
// (0xB2CB0001 under an "object" type). Needs no game and no VM.
#include "../include/SlotGuard.h"

#include <cstdio>
#include <cstdlib>
#include <memory>

namespace
{
	int g_failed = 0;

	void Expect(const char* a_what, slotguard::Slot a_got, slotguard::Slot a_want)
	{
		const bool ok = a_got == a_want;
		std::printf("%s  %s\n", ok ? "ok  " : "FAIL", a_what);
		if (!ok) { ++g_failed; }
	}

	struct FakeVariable
	{
		std::uintptr_t type;
		std::uintptr_t value;
	};
}

int main()
{
	// a live-looking object: heap memory big enough for an Object header (+0x28 is its reference count)
	auto heap = std::make_unique<unsigned char[]>(0x100);
	const auto live = reinterpret_cast<std::uintptr_t>(heap.get());
	const auto liveAligned = (live + 7) & ~std::uintptr_t{ 7 };

	const FakeVariable crash{ 4, 0xB2CB0001 };  // the report: IsObject() true, value 0xB2CB0001
	Expect("the crash value 0xB2CB0001 under an object type is unstable",
		slotguard::Classify(true, slotguard::RawValue(&crash)), slotguard::Slot::kUnstable);
	Expect("an int slot holding the same bits is nothing to guard", slotguard::Classify(false, slotguard::RawValue(&crash)), slotguard::Slot::kNotPointer);
	Expect("null under an object type is none", slotguard::Classify(true, 0), slotguard::Slot::kNone);
	Expect("a live aligned heap pointer is a pointer", slotguard::Classify(true, liveAligned), slotguard::Slot::kPointer);
	Expect("a pointer into the first 64 KB is unstable", slotguard::Classify(true, 0x8000), slotguard::Slot::kUnstable);
	Expect("a misaligned pointer is unstable", slotguard::Classify(true, liveAligned + 1), slotguard::Slot::kUnstable);
	Expect("a kernel-range value is unstable", slotguard::Classify(true, 0xFFFF800000000000ull), slotguard::Slot::kUnstable);
	Expect("an aligned pointer into unmapped memory is unstable", slotguard::Classify(true, 0x0000123400000000ull), slotguard::Slot::kUnstable);

	// reserve two pages, commit only the first, point 8 bytes before its end: the reference count at +0x28 would be on the
	// uncommitted page
	slotguard::Slot edge = slotguard::Slot::kPointer;
	if (auto* base = static_cast<unsigned char*>(::VirtualAlloc(nullptr, 0x2000, MEM_RESERVE, PAGE_NOACCESS)))
	{
		::VirtualAlloc(base, 0x1000, MEM_COMMIT, PAGE_READWRITE);
		edge = slotguard::Classify(true, reinterpret_cast<std::uintptr_t>(base) + 0x1000 - 8);
		::VirtualFree(base, 0, MEM_RELEASE);
	}
	Expect("a pointer whose object header runs onto an uncommitted page is unstable", edge, slotguard::Slot::kUnstable);

	std::printf("%s\n", g_failed ? "FAILED" : "all passed");
	return g_failed ? 1 : 0;
}
