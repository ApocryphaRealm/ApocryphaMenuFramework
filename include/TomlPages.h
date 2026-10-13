#pragma once

// ============================================================================================
// TOML FILES AS AMF PAGES (2.2.1) - a converter kind beside INI, MCM, FLICK, Prisma and MFM. The owner, 2026-10-13: "Just make
// a tab called TOML, which would house all of the TOML format files similar to GSO." It grew out of the Game Settings
// Override page promised on Nexus for this update; GSO's files are now its special case.
//
// - Found: every .toml directly in Data\SKSE\Plugins and in its sub-folders one level down. A top-level file is a page
//   "<name> (TOML)" with a tab per table; a sub-folder's files are one page "<folder name> (TOML)" with a tab per file (its
//   tables as headings) - Game Settings Override's folder is the page "Game Settings Override". Names: a loaded DLL of the
//   same name gives its version resource's name, else the file's or folder's words.
// - Controls: bool -> switch, integer -> whole-number box, float -> number box stepping one unit of the last decimal the
//   file shows, string -> text box, date / time -> text box; the +/- buttons and the mouse wheel nudge a number. Arrays,
//   inline tables and multi-line strings are shown, never written. The "#" lines above a key are its help.
// - Writing: one value at a time, in place - only that value's text on its line (TomlCore.h); comments, order, quoting,
//   the byte-order mark and every line's ending are kept. The file is read again and must still read cleanly before
//   anything is written; it is kept once as <file>.amf-backup before the first change.
// - Taking effect: each page says how (PageRefresh.h) - reloaded in game through the mod's declared reload (its Mod Function
//   Menu Config entry, or GSO's ReloadConfig), called after a save and from the page's Reload button; otherwise the next
//   time the game starts.
// - Game Settings Override's files keep their extras: a control by the GAME setting's type, the game's live value beside
//   each, flags for a key the game does not know, a value written as another type and a key a later file wins; a change
//   also sets the game's value at once (when this file wins the key) before GSO reloads.
// - Not shown, each listed with its reason on Settings > Converted menus > TOML: Mod Function Menu's entry files
//   (ccld_ModFunctionMenu\ - they are the MFM menu's buttons), AMF's own, a name outside plain ASCII, a file over 256 KB, one
//   that does not read cleanly (with the line and why), one with no settings, and in GSO's folder a name GSO does not read
//   (".toml" not in small letters). A mod that has a page of its own here takes its TOML page away, as with INI pages.
// - Switches: [TomlMenus] bEnabled (on by default) for all of them, and each page's own, OFF by default (the owner's rule
//   for INI pages, 2026-10-13) - [TomlMenus] sOn lists the pages turned on.
// ============================================================================================

#include <string>

namespace tomlpages
{
	// kDataLoaded (after mfmpages::Load, whose entries say which mods reload): find and read every TOML file.
	void Load();

	// Registers the pages found and takes away any whose mod now has a page of its own. Each time the menu opens. Thread-safe.
	void Sync();

	// Render thread, every frame: Sync when the registry changed, and run a pending in-game reload once its page's controls
	// are let go.
	void Tick();

	// Settings > Converted menus > TOML (the master switch) or a page's own switch: show or hide the pages.
	void ApplyVisibility();

	// The Converted menus > TOML tab's list: each page with its own switch, its files and how it takes effect, then what was
	// not shown and why.
	void DrawSettingsList();

	// DevBench amf.menu op=toml: do = list (default) | get {file} | set {file, name, value} | reload {page}. file: its id
	// ("skse/plugins/enginefixes.toml"), its name, or its name without ".toml"; name: the key's id ("General.bVerboseLogging",
	// a top-level key by its name). set writes and reloads exactly as the page does, on the render thread. a_gso (op=gso, the
	// 2.2.1 GSO page's name kept as an alias): only Game Settings Override's files, named by file name.
	std::string ToolJson(const std::string& a_argsJson, bool a_gso);
}
