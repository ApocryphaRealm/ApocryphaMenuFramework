#pragma once

// ============================================================================================
// INI FILES AS AMF PAGES - a converter kind beside MCM, FLICK and Prisma.
//
// 2.1.9 (the owner, 2026-10-10): SSE Display Tweaks' INI as a page. Next version (the owner, 2026-10-12: "I want any
// surface level ini file to have a menu" ... "add the data folder to the sweep"): EVERY .ini file directly in
// Data\SKSE\Plugins and directly in Data (top level, not sub-folders) gets an entry "<name> (INI)" - each [Section] a tab,
// each active key a control chosen by its value (true/false, or 0/1 on a "bName" key -> a switch; a whole number or a
// decimal -> a number box stepping one unit of the digit shown; a word listed among the options in the comment above it ->
// a list; anything else -> a text box), the "##" / "#" / ";" lines above a key as its help.
//
// Names: a loaded <name>.dll gives its version resource's ProductName (else FileDescription); otherwise the file's name with
// its words split ("BackPocketForController" -> "Back Pocket For Controller"), so the Mods row's search finds it. A file in
// Data beside a plugin of the same name (<name>.esp/.esm/.esl - the game reads it as that plugin's game settings) is named
// after the plugin and marked "(INI, game settings)".
//
// Not shown, each listed with its reason on Settings > Converted menus > INI: AMF's own INIs; Skyrim.ini, SkyrimPrefs.ini and
// SkyrimCustom.ini (they live in Documents, never in Data - and are never touched if one is found there); a file over
// 256 KB; a file with no [section]; a file that does not read cleanly (a line that is neither a section, a setting nor a
// comment, a key before any section, a key or a section given twice, UTF-16 text); a <name>_custom.ini beside its
// <name>.ini (read as part of that page); a mod that already has a page here from its own AMF / SMF / FLICK / MCM / Prisma
// registration (matched by name - checked again each time the menu opens, as other plugins register after AMF).
//
// Writing: one key at a time. A file with a <name>_custom.ini beside it (and SSE Display Tweaks always - its reader takes the
// custom file over the main one) writes to the custom file: the key replaced in place, or added under its section. Any other
// file is changed in place: only that key's value on its line, the rest of the file - comments, order, byte-order mark,
// line endings - exactly as it was. Each file written keeps its first state once as <file>.amf-backup. A file is re-read
// and must still read cleanly, with the key once in its section, before anything is written. Under Mod Organizer the
// written file lands in Overwrite. These files are read by their mods (or the game) at start-up, so a change takes effect
// the next time the game starts, and the page says so.
// ============================================================================================

#include <string>

namespace inipages
{
	// kDataLoaded: find and read every INI file; the pages are registered by Sync.
	void Load();

	// Registers the pages found, and takes away any whose mod now has a page of its own (other plugins register after AMF's
	// kDataLoaded). Called each time the menu opens and before DevBench lists. Thread-safe.
	void Sync();

	// Settings > Converted menus > INI (the master switch) or a page's own switch: show or hide the entries.
	void ApplyVisibility();

	// The Converted menus > INI tab's list: each page found with its own switch, grouped by folder, then what was skipped.
	void DrawSettingsList();

	// DevBench amf.menu op=ini: do = list (default) | get {mod} | set {mod, section, name, value}. mod: the file's id
	// ("skse/plugins/ssedisplaytweaks.ini"), its name without .ini ("SSEDisplayTweaks") or its entry name. Thread-safe. set
	// writes exactly as the page's control does.
	std::string ToolJson(const std::string& a_argsJson);
}
