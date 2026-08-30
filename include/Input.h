#pragma once

// ============================================================================================
// M2: input capture - the user-validated top priority ("I can still move my perspective around
// my character instead of it halting that to use the menu", the author, first smoke test).
//
// Design per the prior-art survey (§2), adopted deliberately:
//   * ONE hook at BSInputDeviceManager::PollInputDevices (survey §2.1/§2.2) - the layer the
//     game actually reads input through, unlike WndProc games (§2.4).
//   * While the menu is open, events are SPLICED, not blunt-blocked: button RELEASES pass
//     through to the game so a key held across the open transition can never stick down (the
//     survey's central hazard - "swallow a key-down without its key-up and the game is left
//     with a key stuck down"). Everything else - presses, mouse movement, thumbsticks - is
//     consumed, which is precisely what halts the camera.
//   * Events are QUEUED in the hook and TRANSLATED on the render thread inside the present
//     hook, where the ImGui context is guaranteed (survey: "feeding io.AddKeyEvent from the
//     input thread while the render thread is inside NewFrame is a data race").
//   * Translation uses the modern 1.87+ io.Add*Event API exclusively - the pre-1.87
//     io.KeysDown[] pattern in older prior art must not be copied (survey §2.3).
// ============================================================================================

namespace input
{
	// Installs the PollInputDevices call-hook (pattern-guarded like every other hook in this
	// framework). Returns false, with the reason logged, if the site does not look like a call
	// instruction; the framework then runs render-only, exactly as M1 did.
	bool Install();

	// Drains the event queue into ImGui's input queue. Render thread only, called between the
	// backend NewFrame calls and ImGui::NewFrame().
	void ProcessQueuedEvents();

	// Render-thread notification that the menu just opened: centres the software cursor and
	// clears any stale queued events from a previous open.
	void OnMenuOpened();

	// Menu-toggle-key rebinding (the author, 2026-08-28 - "a key binding function on the framework
	// settings menu to change the key that opens and closes the menu"). BeginRebindToggleKey()
	// arms capture; the next keyboard key pressed (except Escape, which cancels) becomes the new
	// toggle key, is saved, and capture disarms. IsAwaitingRebind() drives the "press any key..."
	// prompt on the settings page. Thread-safe (an atomic flag); capture runs in the input hook.
	void BeginRebindToggleKey();
	bool IsAwaitingRebind();
}
