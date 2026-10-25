#pragma once

// ============================================================================================
// SETTINGS PRESETS (2.3.0). The owner, 2026-10-25 01:00: save all of AMF's current settings across mods, "like MCM normally
// does", so players can keep presets (easy / hard, profiles) and a modlist author can ship one; 01:05: picking a preset,
// Save current as..., Load (with the diff) and Delete live in Settings > General, and nowhere else.
// Plan: D:\Claude output\4. plans\amf-settings-presets\PLAN.md. The format, selection and comparison: PresetsCore.h.
//
// One feature with Remembered settings, not a second system: a user preset IS a Remembered settings profile
// (SKSE\Plugins\ApocryphaMenuFramework\RememberedSettings\<name>.json) - its "menus" block (script menus, MCM Helper values
// kept in the save) plus a "files" block (INI pages, TOML pages, Prisma MCM Redux, MCM Helper ModSetting values). The preset
// in use is the profile in use: the last one loaded or saved. Shipped presets (a modlist author's) are read only, in
// Data\SKSE\Plugins\ApocryphaMenuFramework\Presets\<name>.json.
//
// Every value is read through its page's own get and written through its page's own set (INI / TOML / Prisma / MCM Helper),
// and script menus through Remembered settings' read and restore - nothing here writes a mod's file itself.
// FLICK pages are drawn by their mods' own code, which keeps their values; AMF has no path to them.
// ============================================================================================

#include <string>

namespace presets
{
	// Settings > General: the Presets section (render thread).
	void DrawGeneralSection();

	// The preset in use (the Remembered settings profile in use).
	std::string InUse();

	// DevBench amf.menu op=presets: do = list | save {name, skip?:[group keys]} | diff {name, shipped?} |
	// load {keys?:[group keys]} (after diff) | delete {name} | status. save / diff / load start the work and return at once;
	// status reports the phase, the comparison and the last result. Thread-safe.
	std::string ToolJson(const std::string& a_argsJson);
}
