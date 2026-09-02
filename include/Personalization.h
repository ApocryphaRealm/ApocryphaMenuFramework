#pragma once

// ============================================================================================
// Menu-shell personalization (author verdict 2026-09-01, queued with the controller-nav work):
// the player may RENAME a mod's menu entry and REORDER the list. Deliberately NOT included:
// repositioning or floating per-mod windows ("no repositioning").
//
// This is a PRESENTATION layer over registry::Snapshot() - registered mods are untouched and
// know nothing about it. Two pieces of state, both stored in AMF's own INI:
//
//   ALIAS   a player-facing name per mod. Where an alias is set it replaces the mod's own name
//           in the list, in the content pane's heading, and in alphabetical sorting.
//
//   ORDER   the author's option B: the default is alphabetical, EVERY entry always shows its
//           position number, and typing a new number MOVES that entry there while every other
//           number re-flows (insert-and-shift, playlist-style). His reason for rejecting
//           "number only the ones you care about": it would force players to number the lot by
//           hand. A mod installed later inserts at its alphabetical position within the
//           existing sequence rather than landing at the end.
// ============================================================================================

#include "Registry.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace personalization
{
	struct DisplayEntry
	{
		int registryIndex = 0;      // index into the registry snapshot this row draws
		std::string modName;        // the mod's own registered name (identity - never shown when aliased)
		std::string displayName;    // alias when set, else modName (what the list shows and sorts by)
	};

	// The list the menu draws, in display order, one row per registered mod.
	std::vector<DisplayEntry> Order(const std::vector<registry::Entry>& a_entries);

	// Alias: empty string clears it (the entry falls back to the mod's own name).
	std::string GetAlias(const std::string& a_modName);
	void SetAlias(const std::string& a_modName, const std::string& a_alias);

	// Move a mod to 1-based position a_position within the current display order; every other
	// entry re-flows around it. Out-of-range positions clamp. Switches the list to custom order.
	void MoveTo(const std::vector<registry::Entry>& a_entries, const std::string& a_modName, int a_position);

	// Custom order on/off. Off = pure alphabetical by display name (the default).
	bool IsCustomOrder();
	void ResetToAlphabetical();

	// INI plumbing, called by settings::Load/Save so all of AMF's state lives in one file.
	void LoadFrom(const std::unordered_map<std::string, std::string>& a_iniEntries);
	std::string IniBlock();
}
