#include "ExportStubs.h"

#include "utils/Logger.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
	// Hashes a string_view, so a lookup by the caller's const char* builds no std::string (2.0.4).
	struct NameHash
	{
		using is_transparent = void;
		std::size_t operator()(std::string_view a_name) const noexcept { return std::hash<std::string_view>{}(a_name); }
	};

	std::mutex g_lock;
	std::unordered_map<std::string, void*, NameHash, std::equal_to<>> g_stubs;   // name -> generated thunk
	std::vector<std::string> g_names;                  // index -> name, for the handler
	std::atomic<std::size_t> g_known{ 0 };
	std::atomic<std::size_t> g_missing{ 0 };

	// One executable page holds every thunk; 29 bytes each, so a page is ~140 stubs, far more
	// than any consumer set will ever ask for. A second page is allocated if that is wrong.
	std::uint8_t* g_page = nullptr;
	std::size_t g_pageUsed = 0;
	constexpr std::size_t kPageSize = 4096;
	constexpr std::size_t kThunkSize = 29;

	// Called by every thunk with its name index. Logs once per name - the "listener" half -
	// and returns 0.0 so XMM0 is zero for a float-returning caller; the thunk zeroes RAX itself.
	extern "C" double __cdecl StubHandler(int a_index)
	{
		static std::mutex once;
		static std::vector<bool> logged;
		std::scoped_lock lock(once);
		if (a_index >= 0 && static_cast<std::size_t>(a_index) >= logged.size()) {
			logged.resize(a_index + 1, false);
		}
		if (a_index >= 0 && !logged[a_index]) {
			logged[a_index] = true;
			std::string name;
			{
				std::scoped_lock l2(g_lock);
				if (static_cast<std::size_t>(a_index) < g_names.size()) {
					name = g_names[a_index];
				}
			}
			logger::warn("export stub CALLED: a consumer invoked \"{}\", which this framework does not export - returned zero instead of crashing. Add this export.", name);
		}
		return 0.0;
	}

	void* BuildThunk(int a_index)
	{
		if (!g_page || g_pageUsed + kThunkSize > kPageSize) {
			g_page = static_cast<std::uint8_t*>(::VirtualAlloc(nullptr, kPageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
			g_pageUsed = 0;
			if (!g_page) {
				return nullptr;
			}
		}

		std::uint8_t* const p = g_page + g_pageUsed;
		if (!p) {
			return nullptr;   // never: g_page is non-null here (allocated above, or kept from an earlier call)
		}
		const auto handler = reinterpret_cast<std::uint64_t>(&StubHandler);

		// 2.3.0: the thunk is assembled in a local array, then copied to the page in one go - no write goes through a
		// moving pointer. The unused tail byte stays 0, as the freshly committed page's was.
		std::uint8_t code[kThunkSize]{};
		std::size_t n = 0;
		// sub rsp, 0x28          - shadow space + keep the call 16-byte aligned
		code[n++] = 0x48; code[n++] = 0x83; code[n++] = 0xEC; code[n++] = 0x28;
		// mov ecx, imm32          - the name index
		code[n++] = 0xB9; std::memcpy(code + n, &a_index, 4); n += 4;
		// mov rax, imm64          - StubHandler
		code[n++] = 0x48; code[n++] = 0xB8; std::memcpy(code + n, &handler, 8); n += 8;
		// call rax                - XMM0 = 0.0 on return
		code[n++] = 0xFF; code[n++] = 0xD0;
		// add rsp, 0x28
		code[n++] = 0x48; code[n++] = 0x83; code[n++] = 0xC4; code[n++] = 0x28;
		// xor eax, eax            - RAX = 0 (bool/int/pointer callers)
		code[n++] = 0x31; code[n++] = 0xC0;
		// ret
		code[n++] = 0xC3;
		std::memcpy(p, code, kThunkSize);

		g_pageUsed += kThunkSize;
		::FlushInstructionCache(::GetCurrentProcess(), p, kThunkSize);
		return p;
	}
}

namespace export_stubs
{
	void* StubFor(const char* a_name)
	{
		// A consumer whose wrapper resolves on every call asks for a missing name every frame too, so
		// the repeat is looked up by view - the std::string is built only for a name never seen before.
		const std::string_view view = a_name ? std::string_view(a_name) : std::string_view();
		std::scoped_lock lock(g_lock);
		if (const auto it = g_stubs.find(view); it != g_stubs.end()) {
			return it->second;
		}
		const std::string name(view);
		const auto index = static_cast<int>(g_names.size());
		g_names.push_back(name);
		void* thunk = BuildThunk(index);
		if (!thunk) {
			// Could not make executable memory - the one case a null goes back. Logged loudly
			// because the caller will crash on it exactly as before this file existed.
			logger::error("export stub: could not allocate a thunk for \"{}\" - the consumer will receive null", name);
			g_names.pop_back();
			return nullptr;
		}
		g_stubs[name] = thunk;
		g_missing.fetch_add(1, std::memory_order_relaxed);
		logger::warn("export listener: \"{}\" requested by a consumer and NOT exported - a safe stub was handed back", name);
		return thunk;
	}

	std::size_t KnownRequested() { return g_known.load(std::memory_order_relaxed); }
	std::size_t MissingRequested() { return g_missing.load(std::memory_order_relaxed); }
}

namespace export_stubs::detail
{
	namespace
	{
		std::shared_mutex g_seenLock;
		std::unordered_set<std::string, NameHash, std::equal_to<>> g_seen;
	}

	// Called by the GetProcAddress redirect for every name a consumer resolves from our module
	// that we DO export, so the inventory covers both halves.
	//
	// THE HOT PATH (2.0.4). The stock consumer header's wrappers call GetProcAddress on EVERY call -
	// another mod's do, around a thousand times a frame - and every one lands here. This used to
	// lock a mutex, build a std::string and walk a vector of every name seen so far on each of them.
	// A name already seen now costs a shared (reader) lock and one hash: no allocation, no scan, and
	// readers never wait on each other. Only a name's FIRST sighting takes the writer lock and allocates.
	void NoteKnown(const char* a_name)
	{
		const std::string_view name = a_name ? std::string_view(a_name) : std::string_view();
		{
			std::shared_lock read(g_seenLock);
			if (g_seen.find(name) != g_seen.end()) {
				return;
			}
		}
		{
			std::unique_lock write(g_seenLock);
			if (!g_seen.emplace(name).second) {
				return;   // another thread noted it between the two locks
			}
		}
		g_known.fetch_add(1, std::memory_order_relaxed);
		logger::debug("export listener: \"{}\" resolved by a consumer", name);
	}
}
