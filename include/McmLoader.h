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

#include "RememberedSettings.h"

#include <functional>
#include <string>
#include <utility>
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

	// The MCM auto-sort (the owner, 2026-10-05: "an auto sort function which sorted the imported menus into categories,
	// sort of like our mod manager plugin but built into AMF. And it can just sort by name, it doesn't have to be
	// perfect"). McmSort.cpp. Every imported MCM menu goes under a menu-list separator named for its kind, judged by its
	// name with MO2 Modlist Manager's name rules (McmCategoryRules.inc); no vote is "Other". It only rearranges: no menu
	// is hidden or removed. A separator of that name is reused, so a second run makes none. a_all false leaves a menu
	// already under ANY separator where it is (the player put it there, or an earlier sort did - so a re-run is a no-op);
	// true sorts those too. The order from before a sort that changed anything is kept as the layout preset
	// kMcmSortUndoPreset, which RestoreBeforeSort loads (and then deletes).
	inline constexpr const char* kMcmSortUndoPreset = "Before MCM sort";
	struct SortResult
	{
		int moved = 0;              // menus that changed place
		int kept = 0;               // menus left where they were (already under a separator, a_all false)
		int separatorsMade = 0;
		bool changed = false;       // the list differs from before (the undo preset was written)
		std::vector<std::pair<std::string, std::string>> placed;   // (entry, separator name) for each menu sorted
	};
	SortResult SortIntoCategories(bool a_all);
	// The sort learns from the player (the owner, 2026-10-05): an MCM menu moved by hand under a category's separator
	// is remembered there (McmSortLearned.txt) and the sort follows that before its name rules. Render thread, every
	// frame the menu is open; it only does work when the list has changed.
	void LearnFromLayoutIfChanged();
	// The category one entry would go to: its separator's shown name (translated). a_key is the import key.
	std::string CategoryFor(const std::string& a_key, const std::string& a_entry);
	bool CanRestoreBeforeSort();
	bool RestoreBeforeSort();
	// DevBench amf.mcm op=sort: action preview | run | all | undo, and the menu list's separators with their menus.
	std::string SortToolJson(const std::string& a_argsJson);

	// Every frame, right after ImGui::NewFrame: sends OnConfigClose to the mod whose entry stopped being drawn
	// (SkyUI's lifecycle - TrueHUD, True Directional Movement and Precision apply their settings in OnConfigClose).
	void Frame();

	// kPostLoadGame / kNewGame: script objects belong to the loaded game; drop the cached ones so
	// the next change resolves the config script afresh.
	void OnGameLoaded();

	// remembered MCM settings (RememberedSettings.cpp): the MCM Helper menus with values that live in the save (GlobalValue /
	// PropertyValue* controls - ModSetting* values are in MCM Helper's own INI and survive a new game by themselves).
	struct MemoryMenu
	{
		std::string key;    // "mcmhelper|<mod>"
		std::string entry;
	};
	std::vector<MemoryMenu> MemoryMenus();
	// Queued to the main thread: read every such control live; a_done gets (id, value) pairs (empty when the mod's
	// script or forms are not there yet).
	void MemorySnapshot(const std::string& a_key, std::function<void(std::vector<std::pair<std::string, std::string>>)> a_done);
	// Queued to the main thread: OnConfigOpen, each value that differs written through the page's own path (store,
	// OnSettingChange, action), then OnConfigClose. a_done(applied, missing).
	void MemoryRestore(const std::string& a_key, std::vector<std::pair<std::string, std::string>> a_values, std::function<void(int, int)> a_done);

	// Import from MCM Memory (2.1.5, RememberedSettings.cpp): the MCM Helper menu whose display name (MCM's ModName) is
	// a_modName, and the settings another mod saved for it turned into this menu's live control values. ModSetting* ones are
	// counted as kept by MCM Helper (its own INI already holds them); a row matching no control of a compatible kind is
	// counted as not found. Any thread. Empty key = no such MCM Helper menu here.
	struct HelperImport
	{
		std::string key;     // "mcmhelper|<mod>"
		std::string entry;
		std::vector<std::pair<std::string, std::string>> values;   // control key -> value as MCM Helper writes it
		int keptByHelper = 0;
		int notFound = 0;
	};
	HelperImport ImportHelperSettings(const std::string& a_modName, const std::vector<rememberedsettings::ForeignSetting>& a_settings);

	// DevBench amf.mcm (rules 31 and 64). ops: list (default), get {mod,id}, set {mod,id,value},
	// script {mod}. Thread-safe - runs on devbench's listener thread; a set is applied through the
	// same path as the page.
	std::string ToolJson(const std::string& a_argsJson);
}
