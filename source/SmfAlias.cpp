#include "SmfAlias.h"
#include "ExportStubs.h"

#include "utils/Logger.h"

#include <intrin.h>
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
	using GetModuleHandleExW_t = BOOL(WINAPI*)(DWORD, LPCWSTR, HMODULE*);
	using GetModuleHandleExA_t = BOOL(WINAPI*)(DWORD, LPCSTR, HMODULE*);
	using LoadLibraryW_t = HMODULE(WINAPI*)(LPCWSTR);
	using LoadLibraryA_t = HMODULE(WINAPI*)(LPCSTR);
	using LoadLibraryExW_t = HMODULE(WINAPI*)(LPCWSTR, HANDLE, DWORD);
	using LoadLibraryExA_t = HMODULE(WINAPI*)(LPCSTR, HANDLE, DWORD);
	using GetProcAddress_t = FARPROC(WINAPI*)(HMODULE, LPCSTR);

	using GetFileAttributesW_t = DWORD(WINAPI*)(LPCWSTR);
	using GetFileAttributesA_t = DWORD(WINAPI*)(LPCSTR);
	using GetFileAttributesExW_t = BOOL(WINAPI*)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
	using GetFileAttributesExA_t = BOOL(WINAPI*)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
	using FindFirstFileW_t = HANDLE(WINAPI*)(LPCWSTR, LPWIN32_FIND_DATAW);
	using FindFirstFileA_t = HANDLE(WINAPI*)(LPCSTR, LPWIN32_FIND_DATAA);
	using FindFirstFileExW_t = HANDLE(WINAPI*)(LPCWSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
	using FindFirstFileExA_t = HANDLE(WINAPI*)(LPCSTR, FINDEX_INFO_LEVELS, LPVOID, FINDEX_SEARCH_OPS, LPVOID, DWORD);
	using CreateFileW_t = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
	using CreateFileA_t = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);

	HMODULE g_self = nullptr;
	std::wstring g_selfPathW;   // full path of our own DLL - what a file query for SKSEMenuFramework.dll is answered with
	std::string g_selfPathA;

	// Resolved from kernel32 DIRECTLY, never through our own import table - our own imports get
	// patched too, and calling through them would recurse forever.
	GetModuleHandleW_t g_realW = nullptr;
	GetModuleHandleA_t g_realA = nullptr;
	GetModuleHandleExW_t g_realExW = nullptr;
	GetModuleHandleExA_t g_realExA = nullptr;
	LoadLibraryW_t g_realLoadW = nullptr;
	LoadLibraryA_t g_realLoadA = nullptr;
	LoadLibraryExW_t g_realLoadExW = nullptr;
	LoadLibraryExA_t g_realLoadExA = nullptr;
	GetProcAddress_t g_realGetProc = nullptr;

	GetFileAttributesW_t g_realAttrW = nullptr;
	GetFileAttributesA_t g_realAttrA = nullptr;
	GetFileAttributesExW_t g_realAttrExW = nullptr;
	GetFileAttributesExA_t g_realAttrExA = nullptr;
	FindFirstFileW_t g_realFindW = nullptr;
	FindFirstFileA_t g_realFindA = nullptr;
	FindFirstFileExW_t g_realFindExW = nullptr;
	FindFirstFileExA_t g_realFindExA = nullptr;
	CreateFileW_t g_realCreateW = nullptr;
	CreateFileA_t g_realCreateA = nullptr;

	std::atomic<std::size_t> g_patched{ 0 };
	std::atomic<std::size_t> g_hits{ 0 };
	std::atomic<std::size_t> g_fileHits{ 0 };

	std::mutex g_logLock;
	std::set<std::wstring> g_loggedNames;   // one log line per distinct name, not per call

	constexpr std::wstring_view kSmf = L"sksemenuframework";

	// The framework's own PREVIOUS filename (1.6.3 renamed the DLL to !ApocryphaMenuFramework.dll so

	// SKSE loads it first). Every consumer built before that - this project's own mods included - resolves

	// the framework with GetModuleHandle("ApocryphaMenuFramework"), and a stale old copy left beside the

	// new file must never be the module they get, so that name is answered with THIS module as well.

	constexpr std::wstring_view kAmfOld = L"apocryphamenuframework";
	constexpr std::wstring_view kSmfDll = L"sksemenuframework.dll";

	std::wstring Lowered(std::wstring_view a_text)
	{
		std::wstring lowered;
		lowered.reserve(a_text.size());
		for (const auto c : a_text) {
			lowered.push_back(static_cast<wchar_t>(::towlower(c)));
		}
		return lowered;
	}

	std::wstring Widen(const char* a_text)
	{
		if (!a_text || !*a_text) {
			return {};
		}
		const int len = ::MultiByteToWideChar(CP_ACP, 0, a_text, -1, nullptr, 0);
		if (len <= 0) {
			return {};
		}
		std::wstring wide(static_cast<std::size_t>(len), L'\0');
		::MultiByteToWideChar(CP_ACP, 0, a_text, -1, wide.data(), len);
		wide.resize(static_cast<std::size_t>(len) - 1);   // drop the terminator
		return wide;
	}

	std::string Narrow(const std::wstring& a_text)
	{
		if (a_text.empty()) {
			return {};
		}
		const int len = ::WideCharToMultiByte(CP_ACP, 0, a_text.c_str(), -1, nullptr, 0, nullptr, nullptr);
		if (len <= 0) {
			return {};
		}
		std::string narrow(static_cast<std::size_t>(len), '\0');
		::WideCharToMultiByte(CP_ACP, 0, a_text.c_str(), -1, narrow.data(), len, nullptr, nullptr);
		narrow.resize(static_cast<std::size_t>(len) - 1);
		return narrow;
	}

	// Matches "SKSEMenuFramework", "SKSEMenuFramework.dll", the framework's own old name
	// "ApocryphaMenuFramework(.dll)", and any of those with a path in front, case-insensitively. GetModuleHandle accepts all of those spellings, so a consumer
	// using any of them has to land here.
	bool IsSmfName(std::wstring_view a_name)
	{
		if (a_name.empty()) {
			return false;
		}

		if (const auto slash = a_name.find_last_of(L"\\/"); slash != std::wstring_view::npos) {
			a_name.remove_prefix(slash + 1);
		}

		auto lowered = Lowered(a_name);
		if (lowered.ends_with(L".dll")) {
			lowered.resize(lowered.size() - 4);
		}

		return lowered == kSmf || lowered == kAmfOld;
	}

	// The FILE the stock consumer header looks for: exactly "SKSEMenuFramework.dll" as the last
	// path component, any directory in front, case-insensitively. Deliberately narrower than
	// IsSmfName - a query for SKSEMenuFramework.ini or .pdb must get the honest answer, because
	// those files really are absent and a consumer reading them should find that out.
	bool IsSmfFileName(std::wstring_view a_path)
	{
		if (a_path.empty()) {
			return false;
		}
		if (const auto slash = a_path.find_last_of(L"\\/"); slash != std::wstring_view::npos) {
			a_path.remove_prefix(slash + 1);
		}
		if (a_path.size() != kSmfDll.size()) {
			return false;
		}
		return Lowered(a_path) == kSmfDll;
	}

	bool IsSmfFileNameA(const char* a_path)
	{
		return a_path && IsSmfFileName(Widen(a_path));
	}

	// WHO asked. The return address of the alias call sits in the consumer's own image, so the
	// module containing it names the consumer. (1.6.0 - added so a consumer that reaches the
	// alias but never registers can be told apart from one that never reached it; Ammo Patcher
	// logged "Registered" with no page on 2026-09-05 and nothing in the log said which.)
	std::string CallerName(void* a_returnAddress)
	{
		HMODULE mod = nullptr;
		if (!a_returnAddress ||
			!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
								  reinterpret_cast<LPCWSTR>(a_returnAddress), &mod) ||
			!mod) {
			return "?";
		}
		wchar_t path[MAX_PATH]{};
		const auto len = ::GetModuleFileNameW(mod, path, static_cast<DWORD>(std::size(path)));
		if (len == 0) {
			return "?";
		}
		std::wstring_view base(path, len);
		if (const auto slash = base.find_last_of(L"\\/"); slash != std::wstring_view::npos) {
			base.remove_prefix(slash + 1);
		}
		return std::string(base.begin(), base.end());
	}

	void NoteHit(const std::wstring& a_name, void* a_caller = nullptr)
	{
		g_hits.fetch_add(1, std::memory_order_relaxed);

		std::scoped_lock lock(g_logLock);
		const std::string who = CallerName(a_caller);
		if (g_loggedNames.insert(a_name + L"|" + std::wstring(who.begin(), who.end())).second) {
			// Once per distinct name and caller. A consumer caches the handle, so this fires a
			// handful of times per session at most - but it is the line that proves the alias did
			// its job, so it is info, not debug.
			logger::info("SMF module-name alias: answered a lookup of \"{}\" from {} with our own module",
						 std::string(a_name.begin(), a_name.end()), who);
		}
	}

	// The file-alias counterpart. Keyed by API + path + caller so each distinct question is
	// logged once; CreateFileW in particular can be hot, and this must never log per call.
	void NoteFileHit(const wchar_t* a_api, const std::wstring& a_path, void* a_caller = nullptr)
	{
		g_fileHits.fetch_add(1, std::memory_order_relaxed);

		std::scoped_lock lock(g_logLock);
		const std::string who = CallerName(a_caller);
		if (g_loggedNames.insert(std::wstring(a_api) + L"|" + a_path + L"|" + std::wstring(who.begin(), who.end())).second) {
			logger::info("SMF file alias: answered {}(\"{}\") from {} with our own file - the stock consumer header's IsInstalled() looks for that file, and this is what makes it pass",
						 std::string(a_api, a_api + ::wcslen(a_api)), std::string(a_path.begin(), a_path.end()), who);
		}
	}

	HMODULE WINAPI GetModuleHandleW_Alias(LPCWSTR a_name)
	{
		if (a_name && IsSmfName(a_name)) {
			NoteHit(a_name, _ReturnAddress());
			return g_self;
		}
		return g_realW ? g_realW(a_name) : nullptr;
	}

	HMODULE WINAPI GetModuleHandleA_Alias(LPCSTR a_name)
	{
		if (a_name) {
			const auto wide = Widen(a_name);
			if (IsSmfName(wide)) {
				NoteHit(wide, _ReturnAddress());
				return g_self;
			}
		}
		return g_realA ? g_realA(a_name) : nullptr;
	}

	// GetModuleHandleEx: only when the second argument really is a name (the FROM_ADDRESS flag
	// makes it an address instead). Answered by calling the real API on our own path, so the
	// reference-count semantics the caller asked for are exactly preserved.
	BOOL WINAPI GetModuleHandleExW_Alias(DWORD a_flags, LPCWSTR a_name, HMODULE* a_out)
	{
		if (!g_realExW) {
			return FALSE;
		}
		if (a_name && !(a_flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS) && IsSmfName(a_name)) {
			NoteHit(a_name, _ReturnAddress());
			return g_realExW(a_flags, g_selfPathW.c_str(), a_out);
		}
		return g_realExW(a_flags, a_name, a_out);
	}

	BOOL WINAPI GetModuleHandleExA_Alias(DWORD a_flags, LPCSTR a_name, HMODULE* a_out)
	{
		if (!g_realExA) {
			return FALSE;
		}
		if (a_name && !(a_flags & GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)) {
			const auto wide = Widen(a_name);
			if (IsSmfName(wide)) {
				NoteHit(wide, _ReturnAddress());
				return g_realExA(a_flags, g_selfPathA.c_str(), a_out);
			}
		}
		return g_realExA(a_flags, a_name, a_out);
	}

	// LoadLibrary of the framework's name loads US - by our real path, through the real API, so
	// the loader's reference count on this module goes up exactly as the caller expects and a
	// matching FreeLibrary later cannot unload a module it never loaded.
	HMODULE WINAPI LoadLibraryW_Alias(LPCWSTR a_name)
	{
		if (!g_realLoadW) {
			return nullptr;
		}
		if (a_name && IsSmfName(a_name)) {
			NoteHit(a_name, _ReturnAddress());
			return g_realLoadW(g_selfPathW.c_str());
		}
		return g_realLoadW(a_name);
	}

	HMODULE WINAPI LoadLibraryA_Alias(LPCSTR a_name)
	{
		if (!g_realLoadA) {
			return nullptr;
		}
		if (a_name) {
			const auto wide = Widen(a_name);
			if (IsSmfName(wide)) {
				NoteHit(wide, _ReturnAddress());
				return g_realLoadA(g_selfPathA.c_str());
			}
		}
		return g_realLoadA(a_name);
	}

	HMODULE WINAPI LoadLibraryExW_Alias(LPCWSTR a_name, HANDLE a_file, DWORD a_flags)
	{
		if (!g_realLoadExW) {
			return nullptr;
		}
		if (a_name && IsSmfName(a_name)) {
			NoteHit(a_name, _ReturnAddress());
			return g_realLoadExW(g_selfPathW.c_str(), a_file, a_flags);
		}
		return g_realLoadExW(a_name, a_file, a_flags);
	}

	HMODULE WINAPI LoadLibraryExA_Alias(LPCSTR a_name, HANDLE a_file, DWORD a_flags)
	{
		if (!g_realLoadExA) {
			return nullptr;
		}
		if (a_name) {
			const auto wide = Widen(a_name);
			if (IsSmfName(wide)) {
				NoteHit(wide, _ReturnAddress());
				return g_realLoadExA(g_selfPathA.c_str(), a_file, a_flags);
			}
		}
		return g_realLoadExA(a_name, a_file, a_flags);
	}

	// ----------------------------------------------------------------------------------------
	// FILE-NAME ALIAS - the half the module alias could not reach.
	//
	// Measured 2026-09-05, from three Nexus reports and one Discord list, then reproduced here
	// with the MO2 virtual alias switched off: the stock consumer header's IsInstalled() is
	//
	//     std::filesystem::exists("Data/SKSE/Plugins/SKSEMenuFramework.dll")
	//
	// and every third-party consumer surveyed (ten of ten DLLs) gates its registration on it -
	// "SKSEMenuFramework not installed, in-game menu disabled" - BEFORE ever asking for the
	// module. No file of that name exists on a player's install, because this framework ships
	// no second binary. So the file question is answered the same way the module question is:
	// a query naming exactly SKSEMenuFramework.dll is redirected to our own DLL's real path and
	// handed to the real API. exists() sees a file, a stat sees our real size and dates, an open
	// reads our own bytes. Nothing is written to disk, nothing is named SKSEMenuFramework.dll.
	//
	// Every function std::filesystem::exists can reach is covered (GetFileAttributesExW first,
	// FindFirstFileExW and CreateFileW as its fallbacks) plus the plain A/W spellings, because
	// which one a given STL build uses is not ours to know.
	// ----------------------------------------------------------------------------------------
	DWORD WINAPI GetFileAttributesW_Alias(LPCWSTR a_path)
	{
		if (!g_realAttrW) {
			return INVALID_FILE_ATTRIBUTES;
		}
		if (a_path && IsSmfFileName(a_path)) {
			NoteFileHit(L"GetFileAttributesW", a_path, _ReturnAddress());
			return g_realAttrW(g_selfPathW.c_str());
		}
		return g_realAttrW(a_path);
	}

	DWORD WINAPI GetFileAttributesA_Alias(LPCSTR a_path)
	{
		if (!g_realAttrA) {
			return INVALID_FILE_ATTRIBUTES;
		}
		if (IsSmfFileNameA(a_path)) {
			NoteFileHit(L"GetFileAttributesA", Widen(a_path), _ReturnAddress());
			return g_realAttrA(g_selfPathA.c_str());
		}
		return g_realAttrA(a_path);
	}

	BOOL WINAPI GetFileAttributesExW_Alias(LPCWSTR a_path, GET_FILEEX_INFO_LEVELS a_level, LPVOID a_info)
	{
		if (!g_realAttrExW) {
			return FALSE;
		}
		if (a_path && IsSmfFileName(a_path)) {
			NoteFileHit(L"GetFileAttributesExW", a_path, _ReturnAddress());
			return g_realAttrExW(g_selfPathW.c_str(), a_level, a_info);
		}
		return g_realAttrExW(a_path, a_level, a_info);
	}

	BOOL WINAPI GetFileAttributesExA_Alias(LPCSTR a_path, GET_FILEEX_INFO_LEVELS a_level, LPVOID a_info)
	{
		if (!g_realAttrExA) {
			return FALSE;
		}
		if (IsSmfFileNameA(a_path)) {
			NoteFileHit(L"GetFileAttributesExA", Widen(a_path), _ReturnAddress());
			return g_realAttrExA(g_selfPathA.c_str(), a_level, a_info);
		}
		return g_realAttrExA(a_path, a_level, a_info);
	}

	HANDLE WINAPI FindFirstFileW_Alias(LPCWSTR a_path, LPWIN32_FIND_DATAW a_data)
	{
		if (!g_realFindW) {
			return INVALID_HANDLE_VALUE;
		}
		if (a_path && IsSmfFileName(a_path)) {
			NoteFileHit(L"FindFirstFileW", a_path, _ReturnAddress());
			return g_realFindW(g_selfPathW.c_str(), a_data);
		}
		return g_realFindW(a_path, a_data);
	}

	HANDLE WINAPI FindFirstFileA_Alias(LPCSTR a_path, LPWIN32_FIND_DATAA a_data)
	{
		if (!g_realFindA) {
			return INVALID_HANDLE_VALUE;
		}
		if (IsSmfFileNameA(a_path)) {
			NoteFileHit(L"FindFirstFileA", Widen(a_path), _ReturnAddress());
			return g_realFindA(g_selfPathA.c_str(), a_data);
		}
		return g_realFindA(a_path, a_data);
	}

	HANDLE WINAPI FindFirstFileExW_Alias(LPCWSTR a_path, FINDEX_INFO_LEVELS a_level, LPVOID a_data, FINDEX_SEARCH_OPS a_op, LPVOID a_filter, DWORD a_flags)
	{
		if (!g_realFindExW) {
			return INVALID_HANDLE_VALUE;
		}
		if (a_path && IsSmfFileName(a_path)) {
			NoteFileHit(L"FindFirstFileExW", a_path, _ReturnAddress());
			return g_realFindExW(g_selfPathW.c_str(), a_level, a_data, a_op, a_filter, a_flags);
		}
		return g_realFindExW(a_path, a_level, a_data, a_op, a_filter, a_flags);
	}

	HANDLE WINAPI FindFirstFileExA_Alias(LPCSTR a_path, FINDEX_INFO_LEVELS a_level, LPVOID a_data, FINDEX_SEARCH_OPS a_op, LPVOID a_filter, DWORD a_flags)
	{
		if (!g_realFindExA) {
			return INVALID_HANDLE_VALUE;
		}
		if (IsSmfFileNameA(a_path)) {
			NoteFileHit(L"FindFirstFileExA", Widen(a_path), _ReturnAddress());
			return g_realFindExA(g_selfPathA.c_str(), a_level, a_data, a_op, a_filter, a_flags);
		}
		return g_realFindExA(a_path, a_level, a_data, a_op, a_filter, a_flags);
	}

	HANDLE WINAPI CreateFileW_Alias(LPCWSTR a_path, DWORD a_access, DWORD a_share, LPSECURITY_ATTRIBUTES a_sec, DWORD a_disp, DWORD a_flags, HANDLE a_template)
	{
		if (!g_realCreateW) {
			return INVALID_HANDLE_VALUE;
		}
		if (a_path && IsSmfFileName(a_path)) {
			NoteFileHit(L"CreateFileW", a_path, _ReturnAddress());
			return g_realCreateW(g_selfPathW.c_str(), a_access, a_share, a_sec, a_disp, a_flags, a_template);
		}
		return g_realCreateW(a_path, a_access, a_share, a_sec, a_disp, a_flags, a_template);
	}

	HANDLE WINAPI CreateFileA_Alias(LPCSTR a_path, DWORD a_access, DWORD a_share, LPSECURITY_ATTRIBUTES a_sec, DWORD a_disp, DWORD a_flags, HANDLE a_template)
	{
		if (!g_realCreateA) {
			return INVALID_HANDLE_VALUE;
		}
		if (IsSmfFileNameA(a_path)) {
			NoteFileHit(L"CreateFileA", Widen(a_path), _ReturnAddress());
			return g_realCreateA(g_selfPathA.c_str(), a_access, a_share, a_sec, a_disp, a_flags, a_template);
		}
		return g_realCreateA(a_path, a_access, a_share, a_sec, a_disp, a_flags, a_template);
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

	// The import names we redirect and what each becomes. Matched on the FUNCTION name, never
	// the DLL name: these APIs are imported from kernel32.dll by some binaries and from an
	// api-ms-win-core-* apiset by others, and a DLL-name filter silently misses the second kind.
	struct Redirect
	{
		const char* name;
		void* replacement;
	};

	const Redirect kRedirects[] = {
		{ "GetModuleHandleW", reinterpret_cast<void*>(&GetModuleHandleW_Alias) },
		{ "GetModuleHandleA", reinterpret_cast<void*>(&GetModuleHandleA_Alias) },
		{ "GetModuleHandleExW", reinterpret_cast<void*>(&GetModuleHandleExW_Alias) },
		{ "GetModuleHandleExA", reinterpret_cast<void*>(&GetModuleHandleExA_Alias) },
		{ "LoadLibraryW", reinterpret_cast<void*>(&LoadLibraryW_Alias) },
		{ "LoadLibraryA", reinterpret_cast<void*>(&LoadLibraryA_Alias) },
		{ "LoadLibraryExW", reinterpret_cast<void*>(&LoadLibraryExW_Alias) },
		{ "LoadLibraryExA", reinterpret_cast<void*>(&LoadLibraryExA_Alias) },
		{ "GetProcAddress", reinterpret_cast<void*>(&GetProcAddress_Alias) },
		{ "GetFileAttributesW", reinterpret_cast<void*>(&GetFileAttributesW_Alias) },
		{ "GetFileAttributesA", reinterpret_cast<void*>(&GetFileAttributesA_Alias) },
		{ "GetFileAttributesExW", reinterpret_cast<void*>(&GetFileAttributesExW_Alias) },
		{ "GetFileAttributesExA", reinterpret_cast<void*>(&GetFileAttributesExA_Alias) },
		{ "FindFirstFileW", reinterpret_cast<void*>(&FindFirstFileW_Alias) },
		{ "FindFirstFileA", reinterpret_cast<void*>(&FindFirstFileA_Alias) },
		{ "FindFirstFileExW", reinterpret_cast<void*>(&FindFirstFileExW_Alias) },
		{ "FindFirstFileExA", reinterpret_cast<void*>(&FindFirstFileExA_Alias) },
		{ "CreateFileW", reinterpret_cast<void*>(&CreateFileW_Alias) },
		{ "CreateFileA", reinterpret_cast<void*>(&CreateFileA_Alias) },
	};

	// ONLY SKSE plugins and the MSVC C++ runtime get patched, and this restriction is
	// load-bearing rather than tidiness. The first build of this patched the import table of
	// EVERY loaded module - the game itself, ntdll, kernel32, and MO2's usvfs hook DLL among
	// them - and the game died a few seconds after load with no crash log at all. usvfs
	// virtualises the file system by hooking exactly this family of calls, so redirecting its
	// own imports is not a compatibility fix, it is sawing the branch off.
	//
	// The mods that need the alias are all SKSE plugins, so the search space is SKSE\Plugins -
	// PLUS msvcp140.dll (1.5.9): a consumer built against the dynamic C++ runtime does not run
	// std::filesystem::exists in its own image at all. The call lands in msvcp140's
	// __std_fs_get_stats, and it is msvcp140's import of GetFileAttributesExW that has to be
	// answered. Every alias passes any other name straight through, so patching the runtime
	// changes nothing for its other users.
	bool IsPatchTarget(const wchar_t* a_path, std::size_t a_len)
	{
		if (!a_path || a_len == 0) {
			return false;
		}
		const auto lowered = Lowered(std::wstring_view(a_path, a_len));
		if (lowered.find(L"\\skse\\plugins\\") != std::wstring::npos) {
			return true;
		}
		std::wstring_view base(lowered);
		if (const auto slash = base.find_last_of(L"\\/"); slash != std::wstring_view::npos) {
			base.remove_prefix(slash + 1);
		}
		return base.starts_with(L"msvcp140");
	}

	bool IsPatchTarget(HMODULE a_module)
	{
		wchar_t path[MAX_PATH]{};
		const auto len = ::GetModuleFileNameW(a_module, path, static_cast<DWORD>(std::size(path)));
		if (len == 0 || len >= std::size(path)) {
			return false;
		}
		return IsPatchTarget(path, len);
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
	std::atomic<std::size_t> g_loadTimePatched{ 0 };   // modules patched from the callback
	std::atomic<std::size_t> g_loadTimeEntries{ 0 };

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
		if (!IsPatchTarget(name->Buffer, name->Length / sizeof(wchar_t))) {
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

					for (const auto& r : kRedirects) {
						if (std::strcmp(byName->Name, r.name) == 0) {
							PatchThunk(addrs, r.replacement);
							++patched;
							break;
						}
					}
				}
			}
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			return patched;
		}

		return patched;
	}

	template <class T>
	bool Resolve(HMODULE a_k32, const char* a_name, T& a_out)
	{
		a_out = reinterpret_cast<T>(::GetProcAddress(a_k32, a_name));
		if (!a_out) {
			logger::error("SMF alias: kernel32 has no \"{}\"; alias NOT installed", a_name);
			return false;
		}
		return true;
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

			// Our own full path - the answer to every file query for SKSEMenuFramework.dll.
			wchar_t path[MAX_PATH]{};
			const auto len = ::GetModuleFileNameW(g_self, path, static_cast<DWORD>(std::size(path)));
			if (len == 0 || len >= std::size(path)) {
				logger::error("SMF file alias: could not read our own module path ({}); alias NOT installed", ::GetLastError());
				g_self = nullptr;
				return 0;
			}
			g_selfPathW.assign(path, len);
			g_selfPathA = Narrow(g_selfPathW);
			logger::debug("SMF file alias: file queries for SKSEMenuFramework.dll will be answered with \"{}\"", g_selfPathA);
		}

		if (!g_realW || !g_realA) {
			// kernel32 is resolved by handle before anything is patched, and the originals are
			// called through these pointers forever after.
			const auto k32 = ::GetModuleHandleW(L"kernel32.dll");
			if (!k32) {
				logger::error("SMF module-name alias: kernel32 handle is null; alias NOT installed");
				return 0;
			}
			const bool ok =
				Resolve(k32, "GetModuleHandleW", g_realW) && Resolve(k32, "GetModuleHandleA", g_realA) &&
				Resolve(k32, "GetModuleHandleExW", g_realExW) && Resolve(k32, "GetModuleHandleExA", g_realExA) &&
				Resolve(k32, "LoadLibraryW", g_realLoadW) && Resolve(k32, "LoadLibraryA", g_realLoadA) &&
				Resolve(k32, "LoadLibraryExW", g_realLoadExW) && Resolve(k32, "LoadLibraryExA", g_realLoadExA) &&
				Resolve(k32, "GetProcAddress", g_realGetProc) &&
				Resolve(k32, "GetFileAttributesW", g_realAttrW) && Resolve(k32, "GetFileAttributesA", g_realAttrA) &&
				Resolve(k32, "GetFileAttributesExW", g_realAttrExW) && Resolve(k32, "GetFileAttributesExA", g_realAttrExA) &&
				Resolve(k32, "FindFirstFileW", g_realFindW) && Resolve(k32, "FindFirstFileA", g_realFindA) &&
				Resolve(k32, "FindFirstFileExW", g_realFindExW) && Resolve(k32, "FindFirstFileExA", g_realFindExA) &&
				Resolve(k32, "CreateFileW", g_realCreateW) && Resolve(k32, "CreateFileA", g_realCreateA);
			if (!ok) {
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
			if (!IsPatchTarget(modules[i])) {
				continue;
			}
			patchedNow += PatchModuleImports(modules[i]);
			++scanned;
		}

		logger::info("SMF alias: scanned {} module(s) (SKSE plugins + msvcp140), redirected {} import entr(ies) this pass ({} total; {} module(s) / {} entr(ies) patched at load time; {} module-name hit(s), {} file-name hit(s) so far)",
					  scanned, patchedNow, g_patched.load(std::memory_order_relaxed),
					  g_loadTimePatched.load(std::memory_order_relaxed), g_loadTimeEntries.load(std::memory_order_relaxed),
					  g_hits.load(std::memory_order_relaxed), g_fileHits.load(std::memory_order_relaxed));

		return patchedNow;
	}

	std::size_t PatchedEntries() { return g_patched.load(std::memory_order_relaxed); }
	std::size_t AliasHits() { return g_hits.load(std::memory_order_relaxed); }
	std::size_t FileAliasHits() { return g_fileHits.load(std::memory_order_relaxed); }
}
