#pragma once

// ============================================================================================
// THE SMF IMPORT MODULE (2.4.0)
//
// Some mods link SKSE Menu Framework at BUILD time: their DLL's import table names
// SKSEMenuFramework.dll, and the Windows loader resolves that itself while SKSE loads the
// plugin. Scavenger Sense 0.10.1beta is one (75 functions, 2026-11-01): under AMF alone it
// never loaded - skse64.log "Error 126", the module was not found. The import-table alias
// (SmfAlias.h) cannot help there: it answers calls the mod MAKES, and this one fails before
// the mod's code runs.
//
// So AMF ships SKSE\Plugins\ApocryphaMenuFramework\SMF-Import\SKSEMenuFramework.dll - a
// code-free DLL whose every export forwards to the same export of AMF (stub/SMFImportModule.cpp)
// - and loads it by full path from DllMain. !ApocryphaMenuFramework.dll sorts before every other
// plugin, so the module is in the process under the name SKSEMenuFramework.dll before any
// static importer is loaded, and the loader binds the importer to it by name.
//
// It is loaded ONLY when the real SKSE Menu Framework is absent (no SKSEMenuFramework.dll in
// Data\SKSE\Plugins and no module of that name loaded) - with the real one installed, nothing
// here runs and the two never meet.
// ============================================================================================

#include <Windows.h>

namespace smf_import
{
	// From DllMain (DLL_PROCESS_ATTACH) only: decide and load. No logging (the log is not up),
	// no CRT allocation - the outcome is kept for Report().
	void LoadFromDllMain(HMODULE a_self) noexcept;

	// Once the log is up (SKSEPluginLoad): says what LoadFromDllMain decided and checks the
	// loaded module against the loader itself - the module the name resolves to, and that an
	// export resolved through it lands in AMF (rule 30: ask the object).
	void Report();

	// The module's handle when AMF loaded it, else null.
	HMODULE Handle() noexcept;

	// True when the REAL SKSE Menu Framework is installed: its DLL in Data\SKSE\Plugins, or a module of that name
	// already in the process that is not AMF itself (decided once, in DllMain). The module-name and file aliases
	// (SmfAlias.cpp) then leave the name SKSEMenuFramework to it, so every consumer binds to ONE framework - a mod
	// whose static imports went to the real one and whose runtime lookup came to AMF crashed (2026-11-01, phase B).
	bool RealSmfPresent() noexcept;
}
