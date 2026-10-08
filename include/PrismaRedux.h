#pragma once

// ============================================================================================
// 2.1.6: Prisma MCM Redux menus drawn as AMF pages (plan: 4. plans\amf-prisma\PLAN.md; the owner, 2026-10-08:
// "Settings wrappers + coexistence", the suffix "(Prisma)", and the three-way choice every converted menu system gets).
//
// Prisma MCM Redux (Nexus 175023) is an MCM replacement drawn with Prisma UI. A mod ships ONE file:
//   Data\SKSE\Plugins\PrismaMCMRedux\Configs\<ModID>.json   the layout (pages, settings, about)
// and the player's values live in
//   Data\PrismaMCMRedux\Settings\<ModID>.ini                [Settings] key = value
// which the mod reads through Redux's Papyrus natives (Prisma.GetInt/GetFloat/GetBool/GetString). Redux reads that INI
// live, with no cache, so AMF writes it the way Redux's own window does - every value as text, as its page sends it -
// and then tells the mod with the same events: Prisma_OnSettingsApplied (strArg = modName) when the page is left, and
// Prisma_OnAction_<modName> (strArg = setting id) for an action button. Nothing of theirs is shipped or copied.
//
// The three-way choice ([Prisma] iControl): 0 both - the pages here and Redux's own window; 1 AMF only - Redux's key
// (PrismaCore.ini Hotkey) is switched off, the player's key remembered and put back when this changes; 2 Prisma only -
// AMF lists nothing. Per mod, the player can leave a menu out of AMF (the MCM import list, key "prisma|<ModID>").
// ============================================================================================

#include <string>
#include <vector>

namespace prisma
{
	// kDataLoaded: scan the configs and register one entry per config, "<modName> (Prisma)". Does nothing when Prisma
	// MCM Redux is not loaded.
	void Load();

	// Render thread, every frame right after NewFrame: Prisma_OnSettingsApplied for the page the player left, and the
	// one-entry rule (a mod with its own page or an MCM page here keeps that one).
	void Frame();

	// The settings page's three-way choice was changed (the caller has saved the INI): show or hide the entries and switch
	// Redux's own key off or back on.
	void ApplyControl();

	bool ReduxLoaded();
	// Redux's own key as PrismaCore.ini has it now (0 = none), and whether AMF switched it off.
	int ReduxHotkey();
	bool HotkeyHeldByAmf();

	struct ConfigRow
	{
		std::string key;      // "prisma|<ModID>"
		std::string modId;    // the config's file name, which Redux keys the INI on
		std::string entry;    // "<modName> (Prisma)"
		int settings = 0;
		bool imported = true;
		bool duplicate = false;   // the mod also has its own or an MCM page here - this copy is not listed
	};
	std::vector<ConfigRow> Configs();
	void SetImported(const std::string& a_key, bool a_on);

	// DevBench amf.menu op=prisma: do = list (default) | get {mod} | set {mod,id,value} | action {mod,id} | apply {mod}.
	// mod is the ModID. Thread-safe.
	std::string ToolJson(const std::string& a_argsJson);
}
