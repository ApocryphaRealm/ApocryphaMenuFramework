#pragma once

// WHEN THE SAVED WINDOW IS PLACED, AND WHEN IT IS SAVED (2.3.0) - pure logic, no ImGui, so tools\window_geometry_check.cpp
// tests it on its own.
//
// Combined run 2026-10-23 (batch 9): User.ini held a full-screen window (fHotkeyW 0.99875, fHotkeyH 1). The menu was opened
// while the game was in the background, and the game was drawing a 0x0, then an 8x8 image (log: "the game window is 1920x1080
// but draws a 8x8 image - the menu uses the image's size"). The opening frame applied the saved fractions to that 8x8 display,
// used up its one "apply" and never applied again; when the 1920x1080 image came back the size constraint lifted the 8 px
// window to its minimum, 20% of the screen (1495x216 after the names pane widened it), and that was saved over the player's
// window - the Search box was hidden until the window was dragged taller.
//
// So: a display smaller than kMinUsable is not a display - nothing is applied, constrained or saved against it; and the saved
// geometry is applied again whenever the display the window was placed for changes size.

#include <algorithm>
#include <cmath>

namespace windowgeometry
{
	inline constexpr float kMinUsableW = 320.0f;
	inline constexpr float kMinUsableH = 200.0f;

	// A real game image. 0x0 (minimised, driven in the background) and the 8x8 placeholder image are not.
	inline bool UsableDisplay(float a_w, float a_h) { return a_w >= kMinUsableW && a_h >= kMinUsableH; }

	// The window to open, in display fractions: its size and its centre. A saved window opens where the player left it
	// (its top-left plus half its size), the default centred; both kept whole on the screen.
	struct Placement
	{
		float w = 0.0f, h = 0.0f;     // fractions of the display
		float cx = 0.5f, cy = 0.5f;   // centre, fractions of the display
	};

	inline Placement OpenPlacement(bool a_saved, float a_x, float a_y, float a_w, float a_h, float a_defaultW, float a_defaultH)
	{
		Placement p;
		p.w = std::min(a_saved ? a_w : a_defaultW, 1.0f);
		p.h = std::min(a_saved ? a_h : a_defaultH, 1.0f);
		p.cx = a_saved ? a_x + p.w * 0.5f : 0.5f;
		p.cy = a_saved ? a_y + p.h * 0.5f : 0.5f;
		p.cx = std::clamp(p.cx, p.w * 0.5f, 1.0f - p.w * 0.5f);
		p.cy = std::clamp(p.cy, p.h * 0.5f, 1.0f - p.h * 0.5f);
		return p;
	}

	// Per frame, whether the saved geometry is applied now. a_pending is the "apply on open" flag: it stays set while the
	// display is not usable (so it is applied on the first usable frame, not lost on a placeholder one), and it is raised
	// again when the window is shown on a display of another size than the one it was placed for. a_placedFor holds that
	// display; it is updated when the geometry is applied.
	struct Gate
	{
		bool pending = false;
		float placedW = 0.0f, placedH = 0.0f;
	};

	inline bool ApplyNow(Gate& a_gate, float a_displayW, float a_displayH)
	{
		if (!UsableDisplay(a_displayW, a_displayH)) { return false; }
		const bool otherDisplay = a_gate.placedW > 0.0f &&
		                          (std::abs(a_gate.placedW - a_displayW) > 0.5f || std::abs(a_gate.placedH - a_displayH) > 0.5f);
		if (!a_gate.pending && !otherDisplay) { return false; }
		a_gate.pending = false;
		a_gate.placedW = a_displayW;
		a_gate.placedH = a_displayH;
		return true;
	}

	// Whether the window's present place is written back as the player's: never on the frame it was placed (that would
	// record the default as a choice), never mid-drag, and never against a display that is not a real image.
	inline bool SaveNow(bool a_appliedThisFrame, bool a_mouseDown, float a_displayW, float a_displayH)
	{
		return !a_appliedThisFrame && !a_mouseDown && UsableDisplay(a_displayW, a_displayH);
	}
}
