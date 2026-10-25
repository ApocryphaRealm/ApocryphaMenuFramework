// Offline proof for include/SlotGuard.h (2.2.2). Builds slots the way a Papyrus Variable lays them out - { type word, value word } -
// and checks what the MCM script loader now does with each, including the exact value from the 2026-10-17 crash report
// (0xB2CB0001 under an "object" type). 2.3.0 adds the attached-script walk, with the 2026-10-25 crash's type word 0x72.
// Needs no game and no VM.
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

	// ---- 2.3.0: the attached-script walk (the Nolvus crash, 2026-10-25: a script object whose type word read 0x72) ----
	// Fake script objects and type infos laid out at the offsets SlotGuard.h names (Object +0x08 type; type +0x08 name, +0x10
	// parent). Each sits in its own zeroed, 8-aligned block.
	struct alignas(8) FakeType
	{
		std::uintptr_t refCount;  // 00
		std::uintptr_t name;      // 08
		std::uintptr_t parent;    // 10
		std::uintptr_t rest[4];   // 18..37
	};
	struct alignas(8) FakeObject
	{
		std::uintptr_t flags;     // 00
		std::uintptr_t type;      // 08
		std::uintptr_t rest[4];   // 10..2F
	};
	static_assert(sizeof(FakeType) == slotguard::kTypeSpan);
	static_assert(sizeof(FakeObject) == slotguard::kObjectSpan);
	static const char kBase[] = "SKI_ConfigBase";
	static const char kChild[] = "QuickLootIEMCM";
	static const char kLower[] = "ski_configbase";  // a .pex names its parent in lower case
	static const char kOther[] = "Quest";
	auto word = [](const void* a_p) { return reinterpret_cast<std::uintptr_t>(a_p); };
	auto judge = [](const FakeObject& a_o, std::uintptr_t a_pooled = 0) {
		return slotguard::JudgeScript(reinterpret_cast<std::uintptr_t>(&a_o), "SKI_ConfigBase", a_pooled);
	};
	auto ExpectScript = [](const char* a_what, slotguard::Script a_got, slotguard::Script a_want) {
		const bool ok = a_got == a_want;
		std::printf("%s  %s\n", ok ? "ok  " : "FAIL", a_what);
		if (!ok) { ++g_failed; }
	};

	auto base = std::make_unique<FakeType>();
	*base = FakeType{ 1, word(kBase), 0, {} };
	auto child = std::make_unique<FakeType>();
	*child = FakeType{ 1, word(kChild), word(base.get()), {} };
	auto other = std::make_unique<FakeType>();
	*other = FakeType{ 1, word(kOther), 0, {} };

	auto config = std::make_unique<FakeObject>();
	*config = FakeObject{ 7, word(child.get()), {} };
	ExpectScript("a config script derived from SKI_ConfigBase matches through its parent", judge(*config), slotguard::Script::kMatch);
	ExpectScript("the pooled-pointer fast path matches", judge(*config, word(kBase)), slotguard::Script::kMatch);

	auto lowerBase = std::make_unique<FakeType>();
	*lowerBase = FakeType{ 1, word(kLower), 0, {} };
	auto lowerObject = std::make_unique<FakeObject>();
	*lowerObject = FakeObject{ 7, word(lowerBase.get()), {} };
	ExpectScript("the class name compares without case, as the string pool does", judge(*lowerObject), slotguard::Script::kMatch);

	auto plain = std::make_unique<FakeObject>();
	*plain = FakeObject{ 7, word(other.get()), {} };
	ExpectScript("a script of another class does not match", judge(*plain), slotguard::Script::kNoMatch);

	auto crashObject = std::make_unique<FakeObject>();
	*crashObject = FakeObject{ 7, 0x72, {} };  // the report: the type word read 0x72, and the engine's copy wrote to 0x72
	ExpectScript("THE CRASH: a script object whose type word is 0x72 is unstable (nothing is written through it)", judge(*crashObject),
		slotguard::Script::kUnstable);

	auto noType = std::make_unique<FakeObject>();
	*noType = FakeObject{ 7, 0, {} };
	ExpectScript("a script object with no type yet is unstable", judge(*noType), slotguard::Script::kUnstable);

	auto badParent = std::make_unique<FakeType>();
	*badParent = FakeType{ 1, word(kChild), 0x72, {} };
	auto badParentObject = std::make_unique<FakeObject>();
	*badParentObject = FakeObject{ 7, word(badParent.get()), {} };
	ExpectScript("a parent-type word of 0x72 is unstable", judge(*badParentObject), slotguard::Script::kUnstable);

	auto badName = std::make_unique<FakeType>();
	*badName = FakeType{ 1, 0x72, 0, {} };
	auto badNameObject = std::make_unique<FakeObject>();
	*badNameObject = FakeObject{ 7, word(badName.get()), {} };
	ExpectScript("a type-name word of 0x72 is unstable", judge(*badNameObject), slotguard::Script::kUnstable);

	auto loop = std::make_unique<FakeType>();
	*loop = FakeType{ 1, word(kOther), 0, {} };
	loop->parent = word(loop.get());
	auto loopObject = std::make_unique<FakeObject>();
	*loopObject = FakeObject{ 7, word(loop.get()), {} };
	ExpectScript("a parent chain that loops is unstable, not an endless walk", judge(*loopObject), slotguard::Script::kUnstable);

	ExpectScript("an attached-script pointer of 0x72 is unstable", slotguard::JudgeScript(0x72, "SKI_ConfigBase", 0), slotguard::Script::kUnstable);
	ExpectScript("an attached-script pointer into unmapped memory is unstable",
		slotguard::JudgeScript(0x0000123400000000ull, "SKI_ConfigBase", 0), slotguard::Script::kUnstable);
	Expect("the attached-script flag bit is cleared", slotguard::AttachedPointer(word(config.get()) | 1) == word(config.get()) ? slotguard::Slot::kPointer : slotguard::Slot::kUnstable,
		slotguard::Slot::kPointer);

	std::printf("%s\n", g_failed ? "FAILED" : "all passed");
	return g_failed ? 1 : 0;
}
