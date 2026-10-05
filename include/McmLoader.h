#pragma once

// ============================================================================================
// The MCM loader (the owner, 2026-10-04: built as an experiment, released the same day once its
// tests passed). Plan: 4. plans\amf-mcm-loader\PLAN.md.
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
#include <vector>

namespace mcmloader
{
	// kDataLoaded: scan, parse and register one AMF entry per MCM Helper mod (pages as tabs).
	// Does nothing when [MCM] bLoadMcmHelperConfigs=0.
	void Load();

	// The settings page's two switches (the owner, 2026-10-04: "a toggle in the settings page" and "an additional
	// toggle to turn off the menus in Sky UI for the mods that we're managing in AMF"). The caller saves the INI.
	// SetEnabled: loads the configs the first time it is switched on; off hides every MCM entry (and gives SkyUI its
	// menus back). SetHideInSkyUI: takes the fully drawable mods out of SkyUI's MCM list, or puts them back.
	void SetEnabled(bool a_on);
	void SetHideInSkyUI(bool a_on);
	// Phase 3's switch (script-only SkyUI menus, McmScripts.h); SkyUI's list follows it.
	void SetScriptsEnabled(bool a_on);
	// How many mods are hidden from SkyUI's list right now, and how many could be (for the settings page).
	int HiddenInSkyUI();
	int HideableInSkyUI();
	// True once AMF has seen SkyUI's config manager keep its list in no form AMF can read (not stock SkyUI, Barzing or
	// MCM Unlocked): the hide switch then takes nothing out, and the settings page says so.
	bool SkyUIListUnreadable();

	// The import choice (xLenax, 2026-10-04): which MCM menus come into AMF. ImportList is every menu found by a loader
	// that is on, sorted by name. SetMenuImported writes McmImport.txt, shows or hides the menu's pages and lets the
	// SkyUI-list pass give a left-out menu back. SetImportNew re-applies after [MCM] bImportNewMenus was changed and saved.
	struct ImportRow
	{
		std::string key;
		std::string entry;
		bool script;
		bool imported;
	};
	std::vector<ImportRow> ImportList();
	void SetMenuImported(const std::string& a_key, bool a_on);
	void SetImportNew(bool a_on);

	// Every frame, right after ImGui::NewFrame: sends OnConfigClose to the mod whose entry stopped being drawn
	// (SkyUI's lifecycle - TrueHUD, True Directional Movement and Precision apply their settings in OnConfigClose).
	void Frame();

	// kPostLoadGame / kNewGame: script objects belong to the loaded game; drop the cached ones so
	// the next change resolves the config script afresh.
	void OnGameLoaded();

	// DevBench amf.mcm (rules 31 and 64). ops: list (default), get {mod,id}, set {mod,id,value},
	// script {mod}. Thread-safe - runs on devbench's listener thread; a set is applied through the
	// same path as the page.
	std::string ToolJson(const std::string& a_argsJson);
}
