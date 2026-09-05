#include "SmfAlias.h"
#include "ExportStubs.h"

#include "utils/Logger.h"

#include <psapi.h>
#include <winternl.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <string_view>

namespace export_stubs::detail { void NoteKnown(const char* a_name); }

namespace
{
	using GetModuleHandleW_t = HMODULE(WINAPI*)(LPCWSTR);
	using GetModuleHandleA_t = HMODULE(WINAPI*)(LPCSTR);

	HMODULE g_self = nullptr;

	// Resolved from kernel32 DIRECTLY, never through our own import table - our own imports get
	// patched too, and calling through them would recurse forever.
	GetModuleHandleW_t g_realW = nullptr;
	GetModuleHandleA_t g_realA = nullptr;

	std::atomic<std::size_t> g_patched{ 0 };
	std::atomic<std::size_t> g_hits{ 0 };

	std::mutex g_logLock;
	std::set<std::wstring> g_loggedNames;   // one log line per distinct name, not per call

	constexpr std::wstring_view kSmf = L"sksemenuframework";

	// Matches "SKSEMenuFramework", "SKSEMenuFramework.dll", and either of those with a path in
	// front, case-insensitively. GetModuleHandle accepts all of those spellings, so a consumer
	// using any of them has to land here.
	bool IsSmfName(std::wstring_view a_name)
	{
		if (a_name.empty()) {
			return false;
		}

		if (const auto slash = a_name.find_last_of(L"\\/"); slash != std::wstring_view::npos) {
			a_name.remove_prefix(slash + 1);
		}

		std::wstring lowered;
		lowered.reserve(a_name.size());
		for (const auto c : a_name) {
			lowered.push_back(static_cast<wchar_t>(::towlower(c)));
		}

		if (lowered.ends_with(L".dll")) {
			lowered.resize(lowered.size() - 4);
		}

		return lowered == kSmf;
	}

	void NoteHit(const std::wstring& a_name)
	{
		g_hits.fetch_add(1, std::memory_order_relaxed);

		std::scoped_lock lock(g_logLock);
		if (g_loggedNames.insert(a_name).second) {
			// Once per distinct name. A consumer caches the handle, so this fires a handful of
			// times per session at most - but it is the line that proves the alias did its job,
			// so it is info, not debug.
			logger::info("SMF module-name alias: answered a lookup of \"{}\" with our own module",
						 std::string(a_name.begin(), a_name.end()));
		}
	}

	HMODULE WINAPI GetModuleHandleW_Alias(LPCWSTR a_name)
	{
		if (a_name && IsSmfName(a_name)) {
			NoteHit(a_name);
			return g_self;
		}
		return g_realW ? g_realW(a_name) : nullptr;
	}

	// The consumer API's name shapes: cimgui's ig* / Im*_* wrappers and the framework's own
	// verbs. Anything else asked of this module is some other plugin's probe.
	bool IsSmfApiName(const char* a_name)
	{
		static constexpr const char* kVerbs[] = {
			"AddSectionItem", "AddWindow", "AddWindowWithView", "GetMainWindow", "GetMenuFrameworkVersion",
			"IsAnyBlockingWindowOpened", "SetHotkeyEnabled", "IsHotkeyEnabled", "SetWindowsPauseGame",
			"RegisterEventPriority", "UnregisterEvent", "RegisterInpoutEvent", "RegisterInputEvent",
			"UnregisterInputEvent", "RegisterHudElement", "UnregisterHudElement", "LoadTexture",
			"DisposeTexture", "PushFont", "PushRegular", "PushSolid", "PushBrands", "Pop",
		};
		if (!a_name) {
			return false;
		}
		if (std::strncmp(a_name, "ig", 2) == 0 && a_name[2] >= 'A' && a_name[2] <= 'Z') {
			return true;
		}
		if (std::strncmp(a_name, "Im", 2) == 0 && std::strchr(a_name, '_') != nullptr) {
			return true;
		}
		for (const auto v : kVerbs) {
			if (std::strcmp(a_name, v) == 0) {
				return true;
			}
		}
		return false;
	}

	using GetProcAddress_t = FARPROC(WINAPI*)(HMODULE, LPCSTR);
	GetProcAddress_t g_realGetProc = nullptr;

	// GetProcAddress aimed at OUR module: a name we export resolves normally (and is noted by
	// the listener); a name we lack gets a logging no-op stub instead of null, because the stock
	// consumer header calls whatever it got back without checking it. Everything aimed at any
	// other module passes straight through. Ordinal imports (name < 0x10000) are not ours to
	// interpret and pass through too.
	FARPROC WINAPI GetProcAddress_Alias(HMODULE a_module, LPCSTR a_name)
	{
		if (!g_realGetProc) {
			return nullptr;
		}
		const auto real = g_realGetProc(a_module, a_name);
		if (a_module != g_self || !a_name || reinterpret_cast<std::uintptr_t>(a_name) < 0x10000) {
			return real;
		}
		if (real) {
			export_stubs::detail::NoteKnown(a_name);
			return real;
		}
		// Only names shaped like the SKSE Menu Framework consumer API get a stub. The very first
		// live run showed why this must be narrow: a plugin probing EVERY loaded module for
		// "ReShadeRegisterAddon" was handed a stub, took the non-null as "this module is ReShade",
		// and called it. A probe for something we are not must get the honest null back.
		if (IsSmfApiName(a_name)) {
			return reinterpret_cast<FARPROC>(export_stubs::StubFor(a_name));
		}
		return nullptr;
	}

	HMODULE WINAPI GetModuleHandleA_Alias(LPCSTR a_name)
	{
		if (a_name) {
			const std::string narrow(a_name);
			const std::wstring wide(narrow.begin(), narrow.end());
			if (IsSmfName(wide)) {
				NoteHit(wide);
				return g_self;
			}
		}
		return g_realA ? g_realA(a_name) : nullptr;
	}

	// ONLY SKSE plugins get patched, and this restriction is load-bearing rather than tidiness.
	// The first build of this patched the import table of EVERY loaded module - the game itself,
	// ntdll, kernel32, and MO2's usvfs hook DLL among them - and the game died a few seconds
	// after load with no crash log at all. usvfs virtualises the file system by hooking exactly
	// this family of calls, so redirecting its own imports is not a compatibility fix, it is
	// sawing the branch off. The mods that need the alias are all SKSE plugins, so the search
	// space is exactly SKSE\Plugins - nothing outside it has any reason to ask for
	// SKSEMenuFramework, and nothing outside it is ours to touch.
	bool IsSksePlugin(HMODULE a_module)
	{
		wchar_t path[MAX_PATH]{};
		const auto len = ::GetModuleFileNameW(a_module, path, static_cast<DWORD>(std::size(path)));
		if (len == 0 || len >= std::size(path)) {
			return false;
		}

		std::wstring lowered(path, len);
		for (auto& c : lowered) {
			c = static_cast<wchar_t>(::towlower(c));
		}

		return lowered.find(L"\\skse\\plugins\\") != std::wstring::npos;
	}

	// ----------------------------------------------------------------------------------------
	// LOAD-TIME PATCHING - the half that makes the alias arrive in time.
	//
	// Measured 2026-09-04 (real-SMF reference run mirrored against AMF 1.5.3): two consumers
	// resolve the framework INSIDE their own SKSEPlugin_Load, about 100 ms after AMF loaded and
	// before any SKSE message exists - and the stock header caches whatever that first call
	// returns, forever. A pass over "every loaded module" at our own load sees two plugins; the
	// pass at kPostLoad sees them all but is already too late for anything that looked during
	// Load. So the patch has to land on each plugin BETWEEN its imports being resolved and its
	// entry point running. That window is exactly what the loader's DLL-load notification is.
	//
	// LdrRegisterDllNotification is ntdll, not kernel32, and is not in the SDK headers, so the
	// structures are stated here from the documented layout. The callback runs under the loader
	// lock: it must not load libraries or wait on anything that might, which is why it only
	// walks the new module's import table and touches a couple of atomics.
	// ----------------------------------------------------------------------------------------
	struct LDR_DLL_LOADED_NOTIFICATION_DATA
	{
		ULONG Flags;
		const UNICODE_STRING* FullDllName;
		const UNICODE_STRING* BaseDllName;
		PVOID DllBase;
		ULONG SizeOfImage;
	};

	union LDR_DLL_NOTIFICATION_DATA
	{
		LDR_DLL_LOADED_NOTIFICATION_DATA Loaded;
		LDR_DLL_LOADED_NOTIFICATION_DATA Unloaded;   // same layout for the unloaded case
	};

	constexpr ULONG LDR_DLL_NOTIFICATION_REASON_LOADED = 1;

	using LdrDllNotificationFn = VOID(CALLBACK*)(ULONG, const LDR_DLL_NOTIFICATION_DATA*, PVOID);
	using LdrRegisterDllNotification_t = NTSTATUS(NTAPI*)(ULONG, LdrDllNotificationFn, PVOID, PVOID*);

	PVOID g_ldrCookie = nullptr;
	std::atomic<std::size_t> g_loadTimePatched{ 0 };   // plugins patched from the callback
	std::atomic<std::size_t> g_loadTimeEntries{ 0 };

	bool IsSksePluginPath(const wchar_t* a_path, std::size_t a_len)
	{
		if (!a_path || a_len == 0) {
			return false;
		}
		std::wstring lowered(a_path, a_len);
		for (auto& c : lowered) {
			c = static_cast<wchar_t>(::towlower(c));
		}
		return lowered.find(L"\\skse\\plugins\\") != std::wstring::npos;
	}

	std::size_t PatchModuleImports(HMODULE a_module);   // defined below

	VOID CALLBACK OnDllNotification(ULONG a_reason, const LDR_DLL_NOTIFICATION_DATA* a_data, PVOID)
	{
		if (a_reason != LDR_DLL_NOTIFICATION_REASON_LOADED || !a_data) {
			return;
		}
		const auto* name = a_data->Loaded.FullDllName;
		if (!name || !name->Buffer) {
			return;
		}
		if (!IsSksePluginPath(name->Buffer, name->Length / sizeof(wchar_t))) {
			return;
		}

		const auto n = PatchModuleImports(static_cast<HMODULE>(a_data->Loaded.DllBase));
		if (n > 0) {
			g_loadTimePatched.fetch_add(1, std::memory_order_relaxed);
			g_loadTimeEntries.fetch_add(n, std::memory_order_relaxed);
		}
	}

	bool RegisterLoadNotification()
	{
		if (g_ldrCookie) {
			return true;
		}
		const auto ntdll = ::GetModuleHandleW(L"ntdll.dll");
		if (!ntdll) {
			return false;
		}
		const auto reg = reinterpret_cast<LdrRegisterDllNotification_t>(::GetProcAddress(ntdll, "LdrRegisterDllNotification"));
		if (!reg) {
			return false;
		}
		return reg(0, &OnDllNotification, nullptr, &g_ldrCookie) == 0 && g_ldrCookie != nullptr;
	}

	void PatchThunk(IMAGE_THUNK_DATA* a_thunk, void* a_replacement)
	{
		if (reinterpret_cast<void*>(a_thunk->u1.Function) == a_replacement) {
			return;   // already ours - Install() is meant to be re-run
		}

		DWORD previous = 0;
		if (::VirtualProtect(&a_thunk->u1.Function, sizeof(void*), PAGE_READWRITE, &previous)) {
			a_thunk->u1.Function = reinterpret_cast<ULONGLONG>(a_replacement);
			DWORD ignored = 0;
			::VirtualProtect(&a_thunk->u1.Function, sizeof(void*), previous, &ignored);
			g_patched.fetch_add(1, std::memory_order_relaxed);
		}
	}

	// Deliberately free of C++ objects so __try/__except is legal here. A malformed or unmapped
	// import table in someone else's module must not take the game down - it is skipped.
	std::size_t PatchModuleImports(HMODULE a_module)
	{
		std::size_t patched = 0;

		__try {
			auto* const base = reinterpret_cast<std::byte*>(a_module);
			auto* const dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
			if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
				return 0;
			}

			auto* const nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
			if (nt->Signature != IMAGE_NT_SIGNATURE) {
				return 0;
			}

			const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
			if (dir.VirtualAddress == 0 || dir.Size == 0) {
				return 0;
			}

			for (auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
				 desc->Name != 0;
				 ++desc) {
				// Match on the FUNCTION name, never the DLL name: these APIs are imported from
				// kernel32.dll by some binaries and from an api-ms-win-core-libraryloader apiset
				// by others, and a DLL-name filter silently misses the second kind.
				if (desc->OriginalFirstThunk == 0) {
					continue;
				}

				auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
				auto* addrs = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);

				for (; names->u1.AddressOfData != 0; ++names, ++addrs) {
					if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
						continue;
					}

					auto* const byName = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);

					if (std::strcmp(byName->Name, "GetModuleHandleW") == 0) {
						PatchThunk(addrs, reinterpret_cast<void*>(&GetModuleHandleW_Alias));
						++patched;
					} else if (std::strcmp(byName->Name, "GetModuleHandleA") == 0) {
						PatchThunk(addrs, reinterpret_cast<void*>(&GetModuleHandleA_Alias));
						++patched;
					} else if (std::strcmp(byName->Name, "GetProcAddress") == 0) {
						PatchThunk(addrs, reinterpret_cast<void*>(&GetProcAddress_Alias));
						++patched;
					}
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return patched;
		}

		return patched;
	}
}

namespace smf_alias
{
	std::size_t Install()
	{
		if (!g_self) {
			// Our own handle, from an address inside this module - no name lookup, so it cannot
			// be confused by the very aliasing this file installs.
			if (!::GetModuleHandleExW(
					GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
					reinterpret_cast<LPCWSTR>(&GetModuleHandleW_Alias),
					&g_self) ||
				!g_self) {
				logger::error("SMF module-name alias: could not resolve our own module handle; alias NOT installed");
				return 0;
			}
		}

		if (!g_realW || !g_realA) {
			// kernel32 is resolved by handle before anything is patched, and the originals are
			// called through these pointers forever after.
			const auto k32 = ::GetModuleHandleW(L"kernel32.dll");
			if (!k32) {
				logger::error("SMF module-name alias: kernel32 handle is null; alias NOT installed");
				return 0;
			}
			g_realW = reinterpret_cast<GetModuleHandleW_t>(::GetProcAddress(k32, "GetModuleHandleW"));
			g_realA = reinterpret_cast<GetModuleHandleA_t>(::GetProcAddress(k32, "GetModuleHandleA"));
			g_realGetProc = reinterpret_cast<GetProcAddress_t>(::GetProcAddress(k32, "GetProcAddress"));
			if (!g_realW || !g_realA || !g_realGetProc) {
				logger::error("SMF module-name alias: could not resolve the real GetModuleHandleW/A; alias NOT installed");
				g_realW = nullptr;
				g_realA = nullptr;
				return 0;
			}
		}

		// Registered once, at the first Install() - which is AMF's own SKSEPlugin_Load, so every
		// plugin that loads after us is patched before its own Load can look the framework up.
		static bool s_notified = false;
		if (!s_notified) {
			s_notified = true;
			if (RegisterLoadNotification()) {
				logger::info("SMF module-name alias: load-time patching armed (LdrRegisterDllNotification) - plugins loading after this point are patched before their entry point runs");
			} else {
				logger::warn("SMF module-name alias: LdrRegisterDllNotification unavailable - falling back to the message-phase sweeps only, which arrive too late for consumers that resolve during their own load");
			}
		}

		HMODULE modules[1024]{};
		DWORD needed = 0;
		if (!::EnumProcessModules(::GetCurrentProcess(), modules, sizeof(modules), &needed)) {
			logger::error("SMF module-name alias: EnumProcessModules failed ({})", ::GetLastError());
			return 0;
		}

		const auto count = std::min<std::size_t>(needed / sizeof(HMODULE), std::size(modules));
		std::size_t patchedNow = 0;
		std::size_t scanned = 0;

		for (std::size_t i = 0; i < count; ++i) {
			// Skip ourselves: our own calls to the real API must never route through the alias.
			if (modules[i] == g_self) {
				continue;
			}
			if (!IsSksePlugin(modules[i])) {
				continue;
			}
			patchedNow += PatchModuleImports(modules[i]);
			++scanned;
		}

		logger::info("SMF module-name alias: scanned {} SKSE plugin(s), redirected {} import entr(ies) this pass ({} total; {} plugin(s) / {} entr(ies) patched at load time)",
					  scanned, patchedNow, g_patched.load(std::memory_order_relaxed),
					  g_loadTimePatched.load(std::memory_order_relaxed), g_loadTimeEntries.load(std::memory_order_relaxed));

		return patchedNow;
	}

	std::size_t PatchedEntries() { return g_patched.load(std::memory_order_relaxed); }
	std::size_t AliasHits() { return g_hits.load(std::memory_order_relaxed); }
}
