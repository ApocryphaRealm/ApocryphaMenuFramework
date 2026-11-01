#include "SmfImportModule.h"

#include "utils/Logger.h"

#include <cwchar>

// See SmfImportModule.h for the why. Everything before Report() runs inside DllMain, under the
// loader lock: fixed buffers, plain Win32 calls, and one LoadLibraryExW of a DLL that has no
// entry point and no imports, so the nested load runs no code.

namespace
{
	enum class Outcome
	{
		NotRun,          // DllMain never got here (should not happen)
		Loaded,          // loaded from AMF's folder - static importers bind to it
		RealSmfFile,     // Data\SKSE\Plugins\SKSEMenuFramework.dll exists - the real framework is installed, stand aside
		AlreadyLoaded,   // a module named SKSEMenuFramework.dll was already in the process - stand aside
		NoPath,          // could not build a path (GetModuleFileNameW failed or the path is too long)
		FileMissing,     // the import module is not in AMF's folder (a partial install)
		LoadFailed       // LoadLibraryExW refused it - g_error holds GetLastError()
	};

	Outcome g_outcome = Outcome::NotRun;
	DWORD g_error = 0;
	HMODULE g_module = nullptr;
	HMODULE g_self = nullptr;      // this DLL, as DllMain was handed it
	bool g_realSmf = false;        // the real SKSE Menu Framework is installed (RealSmfPresent)
	wchar_t g_modulePath[MAX_PATH * 2]{};
	wchar_t g_realPath[MAX_PATH * 2]{};

	constexpr wchar_t kModuleName[] = L"SKSEMenuFramework.dll";
	constexpr wchar_t kRelativeToSelf[] = L"ApocryphaMenuFramework\\SMF-Import\\SKSEMenuFramework.dll";
	constexpr wchar_t kRelativeToGame[] = L"Data\\SKSE\\Plugins\\SKSEMenuFramework.dll";

	// The folder of a_module's file with a_tail appended, into a_out. False when it does not fit.
	bool FolderOf(HMODULE a_module, const wchar_t* a_tail, wchar_t* a_out, std::size_t a_outCount) noexcept
	{
		if (!a_tail || !a_out || a_outCount == 0) {
			return false;
		}
		const DWORD len = ::GetModuleFileNameW(a_module, a_out, static_cast<DWORD>(a_outCount));
		if (len == 0 || len >= a_outCount) {
			a_out[0] = L'\0';
			return false;
		}
		wchar_t* slash = std::wcsrchr(a_out, L'\\');
		if (!slash) {
			slash = std::wcsrchr(a_out, L'/');
		}
		if (!slash) {
			a_out[0] = L'\0';
			return false;
		}
		slash[1] = L'\0';
		if (::wcscat_s(a_out, a_outCount, a_tail) != 0) {
			a_out[0] = L'\0';
			return false;
		}
		return true;
	}

	bool FileExists(const wchar_t* a_path) noexcept
	{
		if (!a_path || !*a_path) {
			return false;
		}
		const DWORD attributes = ::GetFileAttributesW(a_path);
		return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
	}

	std::string Narrow(const wchar_t* a_text)
	{
		if (!a_text || !*a_text) {
			return {};
		}
		const int len = ::WideCharToMultiByte(CP_UTF8, 0, a_text, -1, nullptr, 0, nullptr, nullptr);
		if (len <= 1) {
			return {};
		}
		std::string out(static_cast<std::size_t>(len), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, a_text, -1, out.data(), len, nullptr, nullptr);
		out.resize(static_cast<std::size_t>(len) - 1);
		return out;
	}
}

namespace smf_import
{
	void LoadFromDllMain(HMODULE a_self) noexcept
	{
		g_self = a_self;
		// The real SKSE Menu Framework, or anything else already answering to the name, wins outright. Only a module that
		// is not AMF itself counts as the real one (an old MO2 alias presented AMF's own file under that name).
		if (const HMODULE existing = ::GetModuleHandleW(kModuleName)) {
			g_outcome = Outcome::AlreadyLoaded;
			g_realSmf = existing != a_self;
			return;
		}
		// The game's Data folder as the game sees it (under Mod Organizer, the virtual one): the exe's folder.
		if (FolderOf(nullptr, kRelativeToGame, g_realPath, std::size(g_realPath)) && FileExists(g_realPath)) {
			g_outcome = Outcome::RealSmfFile;
			g_realSmf = true;
			return;
		}
		// The import module beside our own file. Under Mod Organizer our module path is the real mod folder, where the
		// module physically sits beside us; on a plain install it is Data\SKSE\Plugins. Either way, our folder.
		if (!a_self || !FolderOf(a_self, kRelativeToSelf, g_modulePath, std::size(g_modulePath))) {
			g_outcome = Outcome::NoPath;
			return;
		}
		if (!FileExists(g_modulePath)) {
			g_outcome = Outcome::FileMissing;
			return;
		}
		g_module = ::LoadLibraryExW(g_modulePath, nullptr, 0);
		if (!g_module) {
			g_error = ::GetLastError();
			g_outcome = Outcome::LoadFailed;
			return;
		}
		g_outcome = Outcome::Loaded;
	}

	HMODULE Handle() noexcept { return g_module; }

	bool RealSmfPresent() noexcept { return g_realSmf; }

	void Report()
	{
		switch (g_outcome) {
		case Outcome::Loaded:
			{
				// Ask the loader, not our own bookkeeping: the module the NAME resolves to, and where one forwarded export
				// lands. GetModuleHandleW here is the real one - our own imports are never redirected by the alias.
				const HMODULE byName = ::GetModuleHandleW(kModuleName);
				const HMODULE self = g_self;
				const FARPROC viaModule = g_module ? ::GetProcAddress(g_module, "AddSectionItem") : nullptr;
				const FARPROC direct = self ? ::GetProcAddress(self, "AddSectionItem") : nullptr;
				const bool nameOk = byName == g_module;
				const bool forwardOk = viaModule && viaModule == direct;
				if (nameOk && forwardOk) {
					logger::info("SMF import module: loaded {} - a mod that links SKSEMenuFramework.dll at build time now binds to AMF (the name resolves to it; AddSectionItem forwards to AMF's own export)",
								 Narrow(g_modulePath));
				} else {
					logger::warn("SMF import module: loaded {} but the check failed - the name SKSEMenuFramework.dll resolves to {} (ours {}), AddSectionItem through it {} AMF's own ({} vs {})",
								 Narrow(g_modulePath), static_cast<const void*>(byName), static_cast<const void*>(g_module),
								 forwardOk ? "matches" : "does NOT match", reinterpret_cast<const void*>(viaModule), reinterpret_cast<const void*>(direct));
				}
				break;
			}
		case Outcome::RealSmfFile:
			logger::info("SMF import module: not loaded - the real SKSE Menu Framework is installed ({}), so mods that link it load against it and the SKSEMenuFramework name is left to it", Narrow(g_realPath));
			break;
		case Outcome::AlreadyLoaded:
			logger::info("SMF import module: not loaded - a module named SKSEMenuFramework.dll was already in the process ({})",
						 g_realSmf ? "the real SKSE Menu Framework: the SKSEMenuFramework name is left to it" : "AMF's own file under that name");
			break;
		case Outcome::NoPath:
			logger::warn("SMF import module: not loaded - could not build its path from this DLL's location");
			break;
		case Outcome::FileMissing:
			logger::warn("SMF import module: not loaded - {} is missing (reinstall Apocrypha Menu Framework); a mod that links SKSEMenuFramework.dll at build time will fail to load (skse64.log Error 126)",
						 Narrow(g_modulePath));
			break;
		case Outcome::LoadFailed:
			logger::error("SMF import module: LoadLibraryExW failed on {} (error {}); a mod that links SKSEMenuFramework.dll at build time will fail to load",
						  Narrow(g_modulePath), g_error);
			break;
		case Outcome::NotRun:
		default:
			logger::warn("SMF import module: the load-time check never ran (DllMain did not reach it)");
			break;
		}
	}
}

// AMF's DllMain. The import module has to be in the process before SKSE loads the first plugin that links it, and on
// AE SKSE loads every plugin (to read its version data) before it calls any SKSEPlugin_Load - so DllMain, which runs
// as SKSE loads this, the alphabetically first plugin, is the only point early enough on every runtime.
BOOL APIENTRY DllMain(HMODULE a_module, DWORD a_reason, LPVOID)
{
	if (a_reason == DLL_PROCESS_ATTACH) {
		smf_import::LoadFromDllMain(a_module);
	}
	return TRUE;
}
