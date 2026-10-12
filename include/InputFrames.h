#pragma once

// ============================================================================================
// ONE TRANSITION PER BUTTON PER FRAME - the input drain's rule, with no game or ImGui code so the offline check
// (tools\input_frames_check.cpp) runs the same function the drain does.
//
// ImGui runs here with input trickling OFF (Renderer.cpp - the cursor's one-position-per-frame rule), and with trickling
// off it flattens a frame's events: "successive down/up/down/up will be lost" (imgui.cpp, UpdateInputEvents). So a frame
// may carry at most ONE press or release of each mouse button, key or pad button. Anything more of that same button waits
// for the next frame, in order, ahead of what arrives later - every press is seen down for at least one frame, and every
// release up for one, however short the click and however close the next.
//
// next after 2.1.9 (the 2026-10-12 test, after 6b09667): 6b09667 held a same-frame release a frame, but promoted it BEHIND
// the next frame's newer records, so a second click arriving one frame later ran into it and the two merged into one.
// The carry now goes first.
// ============================================================================================

#include <algorithm>
#include <cstdint>
#include <vector>

namespace inputframes
{
	// a_drain: this frame's records, oldest first, last frame's a_carry already at the front. Keeps in a_drain the first
	// record of each button (a_key(r) != 0 names the button; 0 = not a button - a move, a wheel step, a character) and
	// everything that is not a button; moves every later record of a button already seen into a_carry, in order.
	template <class R, class KeyFn>
	void OneTransitionPerFrame(std::vector<R>& a_drain, std::vector<R>& a_carry, KeyFn a_key)
	{
		a_carry.clear();
		std::vector<std::uint64_t> seen;
		std::vector<R> kept;
		kept.reserve(a_drain.size());
		for (R& r : a_drain)
		{
			const std::uint64_t k = a_key(r);
			if (k != 0)
			{
				if (std::find(seen.begin(), seen.end(), k) != seen.end())
				{
					a_carry.push_back(std::move(r));
					continue;
				}
				seen.push_back(k);
			}
			kept.push_back(std::move(r));
		}
		a_drain.swap(kept);
	}
}
