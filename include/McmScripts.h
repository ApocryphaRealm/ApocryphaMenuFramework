#pragma once

// ============================================================================================
// MCM loader, phase 3 - the owner, 2026-10-04: "do phase 3 for the SkyUI
// script menus". Plan: D:\Claude output\4. plans\amf-mcm-loader\PLAN.md, "Phase 3".
//
// SkyUI menus written only in Papyrus (a quest script extending SKI_ConfigBase, no MCM Helper
// config.json) drawn as AMF pages. AMF stands in for SkyUI's config manager: it makes the same
// calls on the mod's own script (OpenConfig, SetPage, SelectOption, RequestSliderDialogData /
// SetSliderValue, ... CloseConfig), one at a time, and reads the page from the option arrays
// the script fills (_optionFlagsBuf, _textBuf, _strValueBuf, _numValueBuf). Nothing of the
// mod's is patched. Menu lists and message boxes reach only the Journal's Flash panel
// (UI.InvokeStringA), so that one SKSE native is forwarded through a recorder while the Journal
// is closed - the original still runs every time.
// ============================================================================================

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace mcmloader::scripts
{
	// kPostLoadGame / kNewGame: the last game's script objects are dropped and every entry is hidden until discovery
	// finds it in this game (passes over the first 40 s).
	void OnGameLoaded();

	// Main thread: find this game's script-only SkyUI configs, register or show their entries. Idempotent.
	// Does nothing while [MCM] bLoadSkyUIScriptMenus=0.
	void Discover();

	// Any thread: queue one Discover on the main thread (at most every 2 s) - the AMF menu opening calls it, so a config
	// that set itself up after the last timed pass is in the list the moment the player looks.
	void RequestDiscovery();

	// Any thread: how long the call at the head of the queue has been running (0 = nothing running or queued). The menu's
	// pause lets go while this grows, so a menu's scripts can run - see Renderer.cpp SyncGamePause.
	std::chrono::milliseconds WaitingFor();

	// Every frame, right after ImGui::NewFrame: CloseConfig for the config whose entry stopped being drawn.
	void Frame();

	// The settings-page switch. The caller saves the INI. Off: entries hidden, the open config closed, the native
	// recorder taken out again.
	void SetEnabled(bool a_on);

	// For the SkyUI-list switch (McmLoader's SyncSkyUI, main thread): every config found in this game.
	struct HideTarget
	{
		RE::BSTSmartPointer<RE::BSScript::Object> config;
		std::string modName;  // the ModName property, as the config registers itself with
		std::size_t index;
		std::string key;      // "script|<plugin>|<ModName>" - its line in the hidden-by-AMF ledger
	};
	std::vector<HideTarget> HideTargets();

	// Every script menu found in this game, for the settings page's import list. Thread-safe.
	struct MenuInfo
	{
		std::string key;    // "script|<plugin>|<ModName>"
		std::string entry;  // its AMF entry name
		bool present;       // found in the game loaded now
	};
	std::vector<MenuInfo> Menus();

	// Shows exactly the tabs of the menus that are found, imported and switched on (after an import choice changes).
	void RefreshVisibility();
	void SetHidden(std::size_t a_index, bool a_hidden);
	int Hidden();
	int Count();

	// remembered MCM settings (RememberedSettings.cpp). Both drive the menu through the same one-call-at-a-time queue as the page, and
	// refuse (false, a_done not called) while a menu is open on AMF's page or another drive runs.
	//   MemorySnapshot: open the menu, build every page, read every toggle / slider / menu / colour / key / input, close.
	//   MemoryRestore:  open the menu, then for each record in order build its page, find the option (label + nth) and,
	//                   when its value differs, make the page's own Request/accept calls; close (OnConfigClose runs).
	struct MemoryOption
	{
		int type = 0;        // RememberedSettings' Type, as int
		std::string page;    // raw page name ("" for a menu with no pages)
		std::string text;    // raw option label
		int nth = 0;
		std::string value;
		int menuIndex = -1;
	};
	bool MemorySnapshot(const std::string& a_key, std::function<void(std::vector<MemoryOption>)> a_done);
	bool MemoryRestore(const std::string& a_key, std::vector<MemoryOption> a_records, std::function<void(int, int)> a_done);
	bool MemoryIdle();
	// Every script menu found in this game: (key, entry).
	std::vector<std::pair<std::string, std::string>> MemoryMenus();

	// DevBench amf.mcm op=skyui (rules 31 and 64): action list | open | page | options | select | slider | menu |
	// menuoptions | color | key | input | default | info | answer | close. Thread-safe.
	std::string ToolJson(const std::string& a_argsJson);
}
