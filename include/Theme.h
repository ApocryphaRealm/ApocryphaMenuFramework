#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ============================================================================================
// Theme REGISTRY (the author, 2026-08-27) - supersedes the original "no theme-customisation UI by
// design" stance recorded below, the same way a rule gets amended rather than left standing
// alongside its own contradiction (project convention: fold the update into the decision).
//
// Mirrors MO2's own theme mechanism exactly, confirmed by reading it directly
// (C:\Modlists\Apostasy\stylesheets\*.qss - one self-contained file per theme, additive,
// nothing overwrites anything else). AMF's equivalent: a Palette per theme, registered either
// at compile time (the two built-ins below) or scanned additively from
// Data/SKSE/Plugins/ApocryphaMenuFramework/themes/*.ini at load - never mutating another
// theme's entry.
//
// Original spec this supersedes, kept for provenance:
//   "the same build philosophy as the Untarnished UI minimap edit - solid black at full
//    opacity, affected by the game's opacity setting, and the white framing around all the
//    buttons and menus." That look now SHIPS AS the "Untarnished" theme (the author's instruction),
//    one entry in the registry rather than the only possible one.
// ============================================================================================

namespace theme
{
	struct Palette
	{
		std::string id;     // stable key, used in the INI (never renamed once shipped)
		std::string name;   // display name in the theme picker

		std::uint32_t background;    // ABGR
		std::uint32_t frame;         // ABGR - primary border/text/accent colour
		float borderThickness = 1.0f;
	};

	// Registers a theme additively. Re-registering an existing id REPLACES that entry only
	// (never touches any other) - the same semantics as dropping a new .qss into MO2's folder
	// alongside the others.
	void RegisterTheme(Palette a_palette);

	// Scans Data/SKSE/Plugins/ApocryphaMenuFramework/themes/*.ini and registers each as an
	// additional theme. Missing folder is not an error - the two built-ins still work.
	void ScanUserThemes();

	std::vector<Palette> ListThemes();

	// Empty/unknown id is a no-op (current theme stays active); logs either way.
	void SetActiveTheme(const std::string& a_id);
	const Palette& GetActiveTheme();

	// The game's own opacity, applied as ONE global alpha multiplier over the whole theme at
	// draw time - never threaded through individual colour constants. Orthogonal to which
	// palette is active.
	//
	// Resolution strategy is ported from Dragon's Eye Minimap (MiniMap.cpp GetHUDOpacitySetting,
	// proven in game 2026-08-27): try "fHUDOpacity", "fHUDOpacity:MAIN", "fHUDOpacity:Interface",
	// "fHUDOpacity:Display" across BOTH INI setting collections, log which name resolved, and
	// re-read every frame the menu is open so an options-menu change is followed without a
	// reload. FULL OPACITY IS THE FALLBACK - never fail toward translucent.
	float GetGameHUDOpacity();

	// Applies the active theme's palette + border rules to the live ImGui style.
	void Apply();
}
