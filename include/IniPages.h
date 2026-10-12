#pragma once

// ============================================================================================
// 2.1.9: INI-ONLY MODS AS AMF PAGES - a fourth converter kind beside MCM, FLICK and Prisma (the owner, 2026-10-10:
// "Because AMF can read all the config file types in your mod list order, could it theoretically generate a page for SSE
// display tweaks?"). Not a page written per mod: a mod in the table below is read from its own INI - each [Section] a tab,
// each active key a control chosen by its value (true/false -> a switch, a whole number or a decimal -> a number box that
// steps one unit of the digit shown, a word listed among the options in the comment above it -> a list, anything else ->
// a text box), the "##" line and the "#" lines above a key as its help. Commented-out keys ("#Key=value") are left out.
//
// Edits go to the mod's OVERRIDE file, never its main INI: only the changed keys are written, each under its section, the
// rest of the override file left as it was, through a temporary file swapped in, and the override file as it first was is
// kept once as <file>.amf-backup. Under Mod Organizer a new override file lands in Overwrite (or the mod that has one).
//
// The first (and so far only) mod: SSE Display Tweaks (SlavicPotato, MIT). Read from its source (SSETweaks/config.cpp):
// it loads SSEDisplayTweaks.ini and SSEDisplayTweaks_custom.ini ONCE, at game start, and a key present in the custom file
// wins over the main one, key by key; a custom file it cannot parse is ignored whole. So every change here takes effect
// the next time the game starts, and the page says so. Nothing of Display Tweaks is shipped; its own text (key names and
// help) is shown from the player's installed file, in English as written (rule 66).
// ============================================================================================

#include <string>

namespace inipages
{
	// kDataLoaded: read each table mod whose DLL is loaded and register its entry "<name> (INI)", one page per section.
	void Load();

	// Settings > Converted menus > INI: show or hide the entries (the caller has saved the INI).
	void ApplyVisibility();

	// The Converted menus > INI tab's list: one line per mod found.
	void DrawSettingsList();

	// DevBench amf.menu op=ini: do = list (default) | get {mod} | set {mod, section, name, value}. mod is the table id
	// ("SSEDisplayTweaks"). Thread-safe. set writes exactly as the page's control does.
	std::string ToolJson(const std::string& a_argsJson);
}
