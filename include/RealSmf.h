#pragma once

// ============================================================================================
// IS THE REAL SKSE MENU FRAMEWORK INSTALLED? (2.4.0)
//
// With the real SKSE Menu Framework installed beside AMF, AMF's module-name alias (SmfAlias.h)
// used to answer "SKSEMenuFramework" with AMF anyway. A mod whose static imports bound to the
// real framework and whose runtime GetModuleHandle("SKSEMenuFramework.dll") came to AMF had its
// HUD element drawn by AMF, which called the real framework's ImGui with no context - a crash at
// data load (Scavenger Sense, combined run 2026-11-01, phase B). When the real framework is
// installed, the name SKSEMenuFramework is its own: the alias leaves module-name and file
// lookups of it alone, so every consumer binds wholly to one framework.
//
// "Installed" = Data\SKSE\Plugins\SKSEMenuFramework.dll exists as the game sees it (under Mod
// Organizer, the virtual Data folder). Decided once, on first ask, from the exe's folder.
// ============================================================================================

namespace realsmf
{
	bool Present() noexcept;

	// One log line saying what was decided (call once the log is up).
	void Report();
}
