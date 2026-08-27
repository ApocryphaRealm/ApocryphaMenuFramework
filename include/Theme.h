#pragma once

#include <cstdint>

// ============================================================================================
// The theme IS the identity. Decided by the author 2026-08-27, before any code (PLANNED-MODS.md):
//
//   "the same build philosophy as the Untarnished UI minimap edit - solid black at full
//    opacity, affected by the game's opacity setting, and the white framing around all the
//    buttons and menus."
//
// Settled, not a starting suggestion. There is no theme-customisation UI by design.
// ============================================================================================

namespace theme
{
	// True black backgrounds at FULL opacity. Stock ImGui's translucent grey over a bright game
	// scene is a large part of why menu pages are hard to read - low contrast against an
	// unpredictable background. Solid black removes the variable: contrast becomes a property of
	// the theme, not of whatever the player is looking at.
	inline constexpr std::uint32_t kBackground = 0xFF000000;  // ABGR packed, alpha pre-multiplier

	// Warm off-white #F5F2E9 for framing, borders and text. DELIBERATELY NOT pure #FFFFFF - this
	// is the project's established value, shared with the Untarnished UI reskins of Compass
	// Navigation Overhaul and Dragon's Eye Minimap. Hardcoding 0xFFFFFF here would make the
	// framework visibly mismatch every mod around it.
	inline constexpr std::uint32_t kFrame = 0xFFE9F2F5;  // ABGR: A=FF B=E9 G=F2 R=F5

	// Every interactive element gets a visible border - buttons, sliders, combo boxes, menus,
	// not just window edges. A defining feature of the look, never decoration to trim.
	inline constexpr float kBorderThickness = 1.0f;

	// -----------------------------------------------------------------------------------------
	// The game's own opacity, applied as ONE global alpha multiplier over the whole theme at
	// draw time - never threaded through individual colour constants.
	//
	// Resolution strategy is ported from Dragon's Eye Minimap (MiniMap.cpp GetHUDOpacitySetting,
	// proven in game 2026-08-27: "fHUDOpacity 0.5 -> alpha 50" tracked the options slider live):
	// try "fHUDOpacity", "fHUDOpacity:MAIN", "fHUDOpacity:Interface", "fHUDOpacity:Display"
	// across BOTH INI setting collections, log which name resolved, and re-read every frame the
	// menu is open so an options-menu change is followed without a reload.
	//
	// FULL OPACITY IS THE FALLBACK. If the setting cannot be resolved, render solid - never fail
	// toward translucent. (the author's spec names this explicitly.)
	// -----------------------------------------------------------------------------------------
	float GetGameHUDOpacity();  // returns 0..1, 1.0 when unresolved

	// Applies the palette + border rules to the live ImGui style. No-op stub until M1 lands the
	// ImGui dependency; kept in the contract now so main.cpp's wiring never changes.
	void Apply();
}
