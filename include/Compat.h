#pragma once

// ============================================================================================
// M3 second half: the SMF-compatible export surface, sized EXACTLY from the project's own
// export inventory (3. analyze mods\AMF export inventory\inventory.md - 39 names our 11 mods
// actually resolve, plus the optional unregister/version names their vendored header probes).
//
// ORIGINALITY RAIL: everything here is derived from OUR repos' consumer-side vendored header
// and the public cimgui naming conventions (MIT). SKSE Menu Framework's source and binary were
// never read. Signatures come from the inventory's master table verbatim.
// ============================================================================================

namespace RE
{
	class InputEvent;
}

namespace compat
{
	// Menu lifecycle events for the RegisterEventPriority consumers (Wait Your Turn Redux).
	// Values mirror the consumer-side header's own enum: 1 open, 2 close, 3/4 around render.
	enum class MenuEvent : int
	{
		kOpenMenu = 1,
		kCloseMenu = 2,
		kBeforeRender = 3,
		kAfterRender = 4,
	};

	// Render thread. Fires every registered event callback in priority order.
	void FireMenuEvent(MenuEvent a_event);

	// Input thread, called from the poll hook for each event while the menu is open. Returns
	// true if any registered input callback consumed the event (the caller then skips feeding
	// it to ImGui; the game does not see it either way).
	bool DispatchInputEvent(RE::InputEvent* a_event);
}
