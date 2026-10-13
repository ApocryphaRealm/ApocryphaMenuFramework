#pragma once

// ============================================================================================
// GAME SETTINGS OVERRIDE AS AN AMF PAGE (2.2.1; promised on Nexus for the update after 2.2.0). Game Settings Override
// (ccld, Nexus 114911, GPL-3.0 - only its documented file format is read and written here, none of its code is used) sets
// the game's settings (GMSTs) from TOML files: Data/SKSE/Plugins/ccld_GameSettingsOverride/*.toml, flat "EditorID = value"
// lines, loaded A-Z with the later file winning. This is NOT GSO's Mod Function Menu entry (Config > Game Settings
// Override, which only calls its ReloadConfig - AMF's MFM menu already runs that): it is a page that reads and writes the
// files themselves.
//
// - One entry, "Game Settings Override", with a tab per file in load order. Each line is a control chosen by the GAME
//   setting's type (GameSettingCollection): bool -> switch, float -> a number box stepping one unit of the last decimal
//   the file shows, int / uint -> a whole-number box, colour -> a colour editor, string -> a text box. The game's current
//   value is shown beside each, and a key the game has no setting for, a value written as another type, and a key a later
//   file sets again (and so wins) are each flagged. A key the game does not know is drawn by the value the file holds.
// - Writing: one value at a time, in place - only that value's text on its line; comments, order, the byte-order mark and
//   every line's ending are kept (GsoCore.h). The file is read again from disk and must still read cleanly, with the key
//   in it, before anything is written; it is kept once as <file>.amf-backup before the first change.
// - Applying: after a write the game's value is set at once through the engine's Setting (only when this file is the one
//   that wins the key - otherwise the later file's value stays), then GSO's own ReloadConfig export is called
//   (GetModuleHandleW + GetProcAddress, never loaded; guarded as AMF's MFM calls are) and its message is shown. The page
//   says which of the two happened.
// - Files not shown, each listed with its reason on Settings > Converted menus > GSO: a name not ending in ".toml" in small
//   letters (GSO does not read it), a name outside plain ASCII, a file over 256 KB, UTF-16 text, a line this reader does
//   not take (a [table], a dotted or quoted key, an array, a date, a multi-line string), a key given twice, no settings.
// - Switches: [GsoMenus] bEnabled (on by default) for the whole page; and each file's own switch, OFF by default (the
//   owner's rule for INI pages, 2026-10-13: the feature on, every named file off, so a player turns on the one he wants) -
//   [GsoMenus] sOn lists the files turned on.
// ============================================================================================

#include <string>

namespace gsopages
{
	// kDataLoaded: find and read every GSO file and register the page's tabs (shown by the switches).
	void Load();

	// Settings > Converted menus > GSO (the master switch) or a file's own switch: show or hide the tabs.
	void ApplyVisibility();

	// The Converted menus > GSO tab's list: whether GSO is loaded, each file found (load order) with its own switch, then what
	// was not shown and why.
	void DrawSettingsList();

	// DevBench amf.menu op=gso: do = list (default) | get {file} | set {file, name, value}. file: the file's name, with or
	// without ".toml", any case. set writes and applies exactly as the page's control does, on the render thread. Called on
	// DevBench's listener thread.
	std::string ToolJson(const std::string& a_argsJson);
}
