// Off-game check of include/WindowGeometryCore.h - when the key-opened window's saved geometry is applied and saved (2.3.0).
// Combined run 2026-10-23, batch 9: a full-screen window (fHotkeyW 0.99875, fHotkeyH 1) was opened while the game drew a
// 0x0 and then an 8x8 image; the opening applied the fractions to the 8x8 display and was used up, and the window came back
// at its 20% minimum (1495x216) and was saved over the player's geometry.
//   tools\window_geometry_check.bat
// Prints one line per check and "window_geometry_check: N of N passed"; exits 1 on any failure.
#include "../include/WindowGeometryCore.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	int g_pass = 0, g_fail = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	bool Near(float a, float b) { return std::fabs(a - b) < 0.001f; }

	struct Frame { float w, h; };

	// Replays frames the way Renderer.cpp does: the opening raises `pending`; each frame ApplyNow decides; an applied frame
	// lowers it. Returns the display the geometry was last applied on (0x0 when never) and how many times it was applied.
	struct Replay { float appliedW = 0.0f, appliedH = 0.0f; int applies = 0; int saves = 0; };
	Replay Run(const std::vector<Frame>& a_frames)
	{
		windowgeometry::Gate gate;
		bool pending = true;   // the menu was just opened
		Replay r;
		for (const Frame& f : a_frames)
		{
			gate.pending = pending;
			const bool applied = windowgeometry::ApplyNow(gate, f.w, f.h);
			if (applied)
			{
				pending = false;
				r.appliedW = f.w;
				r.appliedH = f.h;
				++r.applies;
			}
			if (windowgeometry::SaveNow(applied, false, f.w, f.h)) { ++r.saves; }
		}
		return r;
	}
}

int main()
{
	using namespace windowgeometry;

	// what is a display
	Check(UsableDisplay(1920.0f, 1080.0f), "1920x1080 is a display");
	Check(UsableDisplay(1280.0f, 720.0f), "1280x720 is a display");
	Check(!UsableDisplay(8.0f, 8.0f), "the 8x8 placeholder image is not a display");
	Check(!UsableDisplay(0.0f, 0.0f), "0x0 (minimised / background) is not a display");
	Check(!UsableDisplay(1920.0f, 8.0f), "a display only one side of which is real is not one");

	// where a saved full-screen window opens: the player's saved values from batch 9
	{
		const Placement p = OpenPlacement(true, 0.000625f, 0.0f, 0.99875f, 1.0f, 0.55f, 0.70f);
		Check(Near(p.w, 0.99875f) && Near(p.h, 1.0f), "a saved full-screen window opens full-screen (w 0.99875, h 1)");
		Check(Near(p.cx, 0.5f) && Near(p.cy, 0.5f), "... centred on the screen");
		Check(p.h * 1080.0f > 1079.0f, "... 1080 px tall on a 1080 px display, not 216");
	}
	{
		const Placement p = OpenPlacement(false, -1.0f, -1.0f, -1.0f, -1.0f, 0.55f, 0.70f);
		Check(Near(p.w, 0.55f) && Near(p.h, 0.70f) && Near(p.cx, 0.5f) && Near(p.cy, 0.5f), "no saved window: the default, centred");
	}
	{
		const Placement p = OpenPlacement(true, 0.9f, 0.9f, 0.5f, 0.5f, 0.55f, 0.70f);
		Check(Near(p.cx, 0.75f) && Near(p.cy, 0.75f), "a saved window past the edge is kept whole on the screen");
	}
	{
		const Placement p = OpenPlacement(true, 0.0f, 0.0f, 1.4f, 1.2f, 0.55f, 0.70f);
		Check(Near(p.w, 1.0f) && Near(p.h, 1.0f), "a saved size above the screen is capped at the screen");
	}

	// the batch 9 sequence: opened while the game drew 0x0 then 8x8, then the real image came back
	{
		const Replay r = Run({ { 0.0f, 0.0f }, { 8.0f, 8.0f }, { 8.0f, 8.0f }, { 1920.0f, 1080.0f }, { 1920.0f, 1080.0f } });
		Check(r.applies == 1 && Near(r.appliedW, 1920.0f) && Near(r.appliedH, 1080.0f),
			"opened on 0x0 / 8x8: the geometry waits and is applied on the first 1920x1080 frame");
		Check(r.saves == 1, "... nothing is saved while the display is a placeholder (only the frame after the apply saves)");
	}
	// the window open and placed, then the image goes 8x8 and back: placed once, not again for the same display
	{
		const Replay r = Run({ { 1920.0f, 1080.0f }, { 8.0f, 8.0f }, { 1920.0f, 1080.0f }, { 1920.0f, 1080.0f } });
		Check(r.applies == 1, "a placeholder frame between two 1920x1080 frames does not place the window again");
		Check(r.saves == 2, "... and the placeholder frame saves nothing");
	}
	// a real resolution change under the open window places it again, from the saved fractions
	{
		const Replay r = Run({ { 1920.0f, 1080.0f }, { 2560.0f, 1440.0f }, { 2560.0f, 1440.0f } });
		Check(r.applies == 2 && Near(r.appliedW, 2560.0f), "a resolution change places the window again for the new display");
	}
	// an ordinary opening on a real display: placed on the first frame, as before
	{
		const Replay r = Run({ { 1920.0f, 1080.0f }, { 1920.0f, 1080.0f } });
		Check(r.applies == 1, "an ordinary opening is placed on its first frame");
	}

	// saving
	Check(!SaveNow(true, false, 1920.0f, 1080.0f), "not saved on the frame it was placed");
	Check(!SaveNow(false, true, 1920.0f, 1080.0f), "not saved mid-drag");
	Check(!SaveNow(false, false, 8.0f, 8.0f), "not saved against the 8x8 image");
	Check(SaveNow(false, false, 1920.0f, 1080.0f), "saved once the player lets go, on a real display");

	std::cout << "window_geometry_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail ? 1 : 0;
}
