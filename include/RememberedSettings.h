#pragma once

// ============================================================================================
// remembered MCM settings (the owner, 2026-10-06: "build it into AMF so that the settings you change
// for all these different MCMs are backed up and saved so that on a new game they still apply").
// Plan: D:\Claude output\4. plans\amf-mcm-memory\PLAN.md.
//
// What a new game forgets is what lives in the save: SkyUI script menus' values (phase 3) and MCM
// Helper's GlobalValue / PropertyValue* controls (phase 2). MCM Helper's ModSetting* values are in
// its own Data\MCM\Settings\<mod>.ini and need nothing. Each change AMF makes for those menus is
// recorded into the active profile; "Back up all now" reads every menu AMF can read; after a NEW
// game the profile is played back through the same calls the pages make.
//
// A profile is SKSE\Plugins\ApocryphaMenuFramework\RememberedSettings\<name>.json - outside the save and
// the download. One record per setting, in the order each was first changed: an "enable" switch
// usually reveals the rest, so it has to come back first.
// ============================================================================================

#include <functional>
#include <string>
#include <vector>

namespace rememberedsettings
{
	// SKI_ConfigBase option types this memory keeps (text rows are actions and are never recorded).
	enum class Type { kToggle, kSlider, kMenu, kColor, kKeymap, kInput, kHelper };

	// One remembered setting.
	//   script menus: page (raw, "" for a menu with no pages), text (the option's raw label), nth (which of the options
	//                 with that label and type on the page), value (bool/float/int/string as text), menu (a menu
	//                 option's shown value; index is the fallback)
	//   MCM Helper:   id (the control key AMF uses: its id, or "#<page>.<index>"), value as MCM Helper writes it
	struct Record
	{
		Type type = Type::kToggle;
		std::string page;
		std::string text;
		int nth = 0;
		std::string id;
		std::string value;
		int menuIndex = -1;
	};

	// kDataLoaded: read the INI's profile.
	void Init();

	// kNewGame: arm the automatic restore (it runs once the menus are found, from 40 s to 4 min after the start).
	// kPostLoadGame: disarm it - a loaded save already holds its settings.
	void OnNewGame();
	void OnSaveLoaded();

	// Any thread. A change AMF just made, recorded when [RememberedSettings] bAutoBackup=1 (and never while a restore runs).
	// a_key is the import key ("script|<plugin>|<ModName>" or "mcmhelper|<mod>"), a_entry the AMF entry name.
	void Remember(const std::string& a_key, const std::string& a_entry, const Record& a_record);

	// The settings page and DevBench.
	struct MenuRow
	{
		std::string key;
		std::string entry;
		int saved = 0;            // settings in the active profile
		bool autoRestore = true;  // takes part in the automatic new-game restore
		bool present = false;     // found in the game loaded now
	};
	std::vector<MenuRow> Menus();
	void SetAutoRestore(const std::string& a_key, bool a_on);
	void Forget(const std::string& a_key);

	std::vector<std::string> Profiles();
	std::string ActiveProfile();
	bool SwitchProfile(const std::string& a_name);                    // loads it (made empty when new); saves the INI
	bool CreateProfile(const std::string& a_name, bool a_copyActive);  // and switches to it
	bool DeleteProfile(const std::string& a_name);                    // never the active one

	// Back up every menu AMF can read now, or restore the profile now (a_keys empty: all; else those menus). False when
	// another backup or restore is running, or AMF's window has a script menu open.
	bool BackUpAll(const std::vector<std::string>& a_keys = {});
	bool RestoreNow(const std::vector<std::string>& a_keys = {});
	bool Busy();
	// The last backup / restore, one line, in the active language ("" before the first).
	std::string LastResult();

	// IMPORT FROM MCM MEMORY (2.1.5, the owner, 2026-10-07: "the whole point of having the import from MCM memory feature
	// is so that they can import their settings and then deactivate MCM memory"). Reads the other mod's saved profile
	// (Data\SKSE\Plugins\MCMMemory\Profiles\<name>.json - read only, never written) and merges every setting AMF can keep
	// into the ACTIVE profile (the owner's choice), so the next new game sets them without the other mod. What AMF has no
	// record for (button clicks, rows with no name, cycling text rows) and menus not in this game are listed by name.
	struct ForeignSetting   // one saved setting, as the other mod keeps it (raw text, value as text)
	{
		std::string page;
		std::string label;
		std::string kind;        // option (toggle) | slider | menu | color | keymap | input
		std::string value;
		std::string valueText;   // a menu's shown text
		int index = -1;          // a menu's index
		int optionIndex = -1;    // its slot on the page (orders same-named rows)
	};
	std::vector<std::string> McmMemoryProfiles();   // the profiles found, its active one first; empty = none saved
	bool McmMemoryAutoRestoreOn();                  // its DLL is loaded and its own automatic restore is on
	std::string ImportFromMcmMemory(const std::string& a_profile);   // the result, one line per part, in the active language

	// DevBench amf.mcm op=remembered: action status | backup {keys} | restore {keys} | records {key} | forget {key} |
	// auto {key,on} | profile {name} | create {name,copy} | delete {name}. Thread-safe.
	std::string ToolJson(const std::string& a_argsJson);
}
