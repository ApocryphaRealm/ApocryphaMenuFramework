#pragma once

// ============================================================================================
// EXPERIMENTAL (exp/mcm-loader, the owner 2026-10-04: "this wont go in the next update and is
// experimental for now"). Plan: D:\Claude output\4. plans\amf-mcm-loader\PLAN.md.
//
// MCM Helper menus drawn as AMF pages, read from the mods' own files at run time - nothing of
// theirs is shipped, the way Dynamic Interface Patcher works on other authors' interface files:
//   Data\MCM\Config\<mod>\config.json   the layout (pages, controls, groups)
//   Data\MCM\Config\<mod>\settings.ini  the defaults
//   Data\MCM\Settings\<mod>.ini         the player's values
// <mod> is the file-name stem of the plugin holding the mod's config quest (MCM Helper's own
// FormUtil::GetModName), so a config whose plugin is not loaded is skipped.
//
// Phase 1 edits ModSetting* values only. A change is written through MCM Helper's own global
// natives (MCM.SetModSetting*), which update its store and save the user INI, and then the mod is
// told with OnSettingChange(id) on its config script - exactly what MCM Helper's menu does.
// GlobalValue / PropertyValue* sources and CallFunction actions are shown read-only (phase 2).
// ============================================================================================

#include <string>

namespace mcmloader
{
	// kDataLoaded: scan, parse and register one AMF entry per MCM Helper mod (pages as tabs).
	// Does nothing when [MCM] bLoadMcmHelperConfigs=0.
	void Load();

	// kPostLoadGame / kNewGame: script objects belong to the loaded game; drop the cached ones so
	// the next change resolves the config script afresh.
	void OnGameLoaded();

	// DevBench amf.mcm (rules 31 and 64). ops: list (default), get {mod,id}, set {mod,id,value},
	// script {mod}. Thread-safe - runs on devbench's listener thread; a set is applied through the
	// same path as the page.
	std::string ToolJson(const std::string& a_argsJson);
}
