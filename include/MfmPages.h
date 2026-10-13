#pragma once

// ============================================================================================
// MOD FUNCTION MENU PAGES (next version after 2.1.9; the owner, 2026-10-12: "I don't want to post 2.1.9 without the MFM
// compatibility" - then moved to the next version). Mod Function Menu (ccld, MIT) entries - TOML files under
// Data/SKSE/Plugins/ccld_ModFunctionMenu/{Mod,Config}/ that each name a function another SKSE plugin exports - shown as
// one converted menu here, "Mod Function Menu": the Mod and Config groups as headings, each folder a collapsible header,
// each file a button labelled with its file name. MfmCore.h reads, resolves and calls them; this draws them.
//
// - The folders are read once, at kDataLoaded (as MFM does); an entry's file is read again each time it is pressed.
// - A function is found with GetModuleHandleW + GetProcAddress only - nothing is ever loaded. One that is not there draws
//   its button disabled with a line naming the DLL or function that is missing, and the log says so. Unresolved entries
//   are looked up again while the page is open (rule 17: a DLL may be loaded later).
// - The call runs on the render thread (a press is handled where the page draws; DevBench's do=run is handed to the
//   render thread), guarded against a fault or a throw, and logged.
// - Message / MessageBox: the text the function returns is shown in a box with OK. preAction / postAction CloseMenu
//   close AMF's menu; CloseMenuAndResetPath also folds every folder back up (the page's root).
// - Settings > Converted menus > MFM: a switch ([MfmMenus] bEnabled, on by default) and the count found. The menu is
//   registered only when at least one entry exists. MFM itself is left alone: with both installed, both menus work.
// ============================================================================================

#include "RefreshCore.h"

#include <cstddef>
#include <string>
#include <vector>

namespace mfmpages
{
	// kDataLoaded: read the folders, resolve each entry and register the menu when there is anything in it.
	void Load();

	// Settings > Converted menus > MFM: show or hide the menu (the caller has saved the INI).
	void ApplyVisibility();

	// The Converted menus > MFM tab's list: what was found and how much of it can run.
	void DrawSettingsList();

	// DevBench amf.menu op=mfm: do = list (default) | run {path}. path is an entry's path from the menu root, as list
	// gives it ("Mod/Toggle UI/Toggle HUD - Compass.toml"; the ".toml" may be left off). run presses the entry as the page
	// does - on the render thread - and returns the outcome and any message text. Called on DevBench's listener thread.
	std::string ToolJson(const std::string& a_argsJson);

	// 2.2.1: every entry read (Mod and Config) as a declaration - its file, DLL, function and type - for the INI and TOML pages'
	// in-game reload (PageRefresh.h; only Config entries that reload are ever called). Read at kDataLoaded. Thread-safe.
	std::vector<refreshcore::Decl> Declarations();
}
