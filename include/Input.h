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

	// What the player last actually used. Fed by the input hook, read by the settings page (so
	// the detection can be watched for accuracy) and by the auto-switch itself.
	enum class Device
	{
		kUnknown,
		kKeyboardMouse,
		kGamepad
	};
	Device LastDevice();
	// Seconds since the last device event, for the settings page's readout. -1 when nothing yet.
	float SecondsSinceLastDevice();

	// TRUE while the player is on a controller. This is DERIVED from what was last really used,
	// never stored and never configured (author, 2026-09-04: "I want the auto detection feature
	// built-in with no toggle and there doesn't need to be a controller toggle anymore").
	//
	// It used to be a saved setting that a second setting decided whether to overwrite. That is
	// two switches to describe one fact the game already knows, and it could be left contradicting
	// reality - a player who picked up a pad still driving keyboard navigation until they found the
	// toggle. Asking the detector directly cannot disagree with itself. Only DELIBERATE input moves
	// it: a button down, a mouse click, real mouse movement, or a stick past the nav deadzone, so a
	// resting stick or a nudged mouse never flips navigation mid-menu.
	bool UsingController();

	// Render-thread notification, sampled inside the frame: is an ImGui item currently being
	// edited (a slider taken hold of with A, a text field, an open drop-down)? While one is, the
	// controller scheme routes the RIGHT stick to it and holds the left stick off, so moving a
	// value can never also move the selection (author's spec, 2026-08-31).
	void SetItemActive(bool a_active);

	// Drains the event queue into ImGui's input queue. Render thread only, called between the
	// backend NewFrame calls and ImGui::NewFrame().
	void ProcessQueuedEvents();

	// DRIVING (1.5.6). The software cursor is the only mouse position ImGui ever sees while the
	// menu is open - the game recentres the OS cursor every frame - so an outside driver cannot
	// point at a widget through the OS. These give DevBench an authoritative way in: place the
	// cursor at an absolute display-space position, press/release a button, and read where the
	// cursor is. All three are queued and applied on the render thread like real input.
	void SetCursorAbsolute(float a_x, float a_y);
	void QueueMouseButton(std::uint32_t a_button, bool a_down);
	// A full click that SPANS FRAMES: press one frame after the call, release two frames after
	// that. ImGui event trickling is off in this framework (set at init, for the software
	// cursor), so a press and release queued in the same drain collapse to no click at all -
	// measured 2026-09-05 on a checkbox that the cursor was visibly sitting on.
	void QueueMouseClick(std::uint32_t a_button);
	void GetCursor(float& a_x, float& a_y);

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

	// Keybind-capture widget (queue L26, the author's green light 2026-08-31): OBSERVE-ONLY
	// capture of the next keyboard or gamepad button PRESS, for testing binds over DevBench
	// (`amf.keybind`) without going through a mod's own settings page. Unlike the toggle-key
	// rebind above it consumes nothing and changes no settings - the game and menu still see
	// the event; the hook just records it and disarms.
	void ArmKeyCapture();
	void CancelKeyCapture();
	bool IsKeyCaptureArmed();
	// Packed last capture: -1 = none yet, else (device << 32) | scan code
	// (device: RE::INPUT_DEVICE - 0 keyboard, 1 mouse, 2 gamepad).
	std::int64_t LastCapturedKey();
}
