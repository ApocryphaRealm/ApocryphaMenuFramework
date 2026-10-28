#pragma once

// ============================================================================================
// THE UNIVERSAL KEYBOARD'S GAME HALF (2.3.0, the owner, 2026-10-23: "I want the universal keyboard built into the
// current test build"; plan 4. plans\amf-universal-keyboard\PLAN.md).
//
// AMF's on-screen keyboard (Keyboard.h) typed only into AMF's own ImGui text boxes. This module gives it a second
// kind of target: a Scaleform TEXT FIELD in one of the GAME's menus - SkyUI's item and spell search (Inventory,
// Container, Barter, Gift, Magic), the map's location finder, the enchanting table's name box, SkyUI's MCM text
// dialog, and any other menu's text prompt (a mod's naming box, the character's name).
//
// The menus and SkyUI members were mapped from shazdeh2's Virtual Keyboard (Nexus 158716, whose permissions allow
// modification and asset use; credited in NOTICE.md and the README). The implementation is AMF's own, in C++; none of
// that mod's files are shipped or needed.
//
// DETECTION (plan 4c). A game menu that starts typing raises ControlMap's text-entry count (SkyUI's search, its dialogs
// and B612's text box all call skse.AllowTextInput). So: the count minus what AMF itself added (Input.cpp
// g_textInputAdded) is above zero while AMF's own window is not up = a game menu wants text. On that EDGE a UI task
// asks each open menu's movie, top first, for Selection.getFocus() and that path's .type; an "input" field names the
// target, and the menu picks the adapter. For the item lists, where a pad cannot reach the search box at all, AMF gives
// the box a CONTROLLER FOCUS (2.3.1, the owner, 2026-10-28: the keyboard "should only pop up when the player moves their
// controller navigation focus to the search box and presses into it"): Up on the first row (the visible row,
// getSelectedListEnumIndex, re-read every dispatch) moves the focus to the box, which AMF frames on screen; A on it calls
// SkyUI's searchWidget.startInput() and the keyboard opens; B or Down gives the focus back to the list; any other button
// is the menu's again. Up never opens the keyboard by itself, and Up with no row selected (Inventory Reforged's slots,
// Simple Loadout System's bar) stays with the menu. The open button (iKeyboardOpenPad) still starts the input straight
// away; the map's button calls showLocationFinder(). Every member path is a CANDIDATE probed when the menu opens (IsAvailable) and logged once
// (rule 30); a path that is not there turns that gesture off for the menu, never a crash.
//
// INJECTION (plan 4d), three routes, the first that is proven by read-back wins:
//   1. Scaleform events into the menu's movie: a GFxCharEvent per UTF-16 unit, GFxKeyEvent down/up for Backspace and
//      Enter (uiMovie->HandleEvent, in a UI task) - the field's own onChanged runs, so SkyUI filters as it does for a PC
//      keyboard.
//   2. The engine's own input path: real CharEvents and a Backspace / Enter key spliced ahead of AMF's input hook
//      (input::InjectText / InjectPress, the path DevBench's op=inject already uses).
//   3. SetVariable(<field>.text) and the field's onChanged handler called by hand.
// After every edit a UI task a couple of dispatches later reads <field>.text back: unchanged = that route did not reach
// the field (warned once per menu session), the next route takes over and the expected text is written through route 3,
// so the player never loses a character.
//
// INPUT (plan 4e). While the keyboard is open over a game menu its pad presses never reach the menu underneath: the input
// hook CLAIMS every gamepad button and both sticks (as AMF's own window's input hold does - a release passes only when
// the game saw its press) and hands them to the keyboard on the render thread. B once "parks" the keyboard (the panel goes,
// the field keeps typing, only A and B stay claimed: A brings the panel back, B again ends the input through the
// adapter). Start / Done sends Enter and ends it. The claim drops the moment the target's menu closes.
//
// CO-INSTALLED KEYBOARDS. Virtual Keyboard (158716, "Virtual Keyboard - SkyUI.esp") owns the item, map and enchanting
// boxes: AMF's adapters for those turn off with one log line and a note on the Settings page. A menu that carries
// 158716's injected clips is skipped by the generic adapter too. B612 ("b612.esp") shows its own keyboard on its own
// text box, so a menu whose movie is one of B612's is left to it.
//
// THREADS. The input hook (game thread) only reads atomics and queues; every Scaleform call is an SKSE UI task (plan
// risk 5); the render thread only reads the published snapshot. A queued lambda owns everything it touches (logic
// library 6013). The DevBench handlers run on DevBench's listener thread and only queue or read the snapshot.
// ============================================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace RE { class InputEvent; }

namespace gamefield
{
	// kDataLoaded: the menu sink, and the co-installed keyboards (158716, B612) detected once.
	void Install();
	bool VirtualKeyboardInstalled();   // Virtual Keyboard (158716) is loaded - its boxes are left to it
	bool B612Installed();              // B612 is loaded - its own text box keeps its own keyboard

	// ---- game thread: the input hook ------------------------------------------------------------
	enum class Claim : int { kNone = 0, kFull = 1, kParked = 2 };
	Claim CurrentClaim();

	// Once per dispatch, before the event loop: notes the device in use, follows the game's text-entry edge (a_amfAdded =
	// how much of ControlMap's count AMF itself added), runs the timed checks. a_amfWindow: AMF's window (or a mod's
	// blocking window) has the input this dispatch - game fields are left alone then.
	void OnDispatch(const RE::InputEvent* a_head, int a_amfAdded, bool a_amfWindow);

	// True = this event is the keyboard's (or a gesture's) and must not reach the game; it has been queued.
	bool TakeEvent(const RE::InputEvent* a_event, Claim a_claim);

	// ---- render thread: the search box's controller focus (2.3.1) ------------------------------------
	// True while an item or spell list's search box holds the controller focus (Up on the first row put it there), with the
	// box as fractions of the screen (0..1), for AMF's highlight frame round it.
	bool SearchFocusRect(float& a_x0, float& a_y0, float& a_x1, float& a_y1);

	// ---- render thread: the keyboard -----------------------------------------------------------
	struct Target
	{
		std::string menu;      // the menu's name ("InventoryMenu"), empty for an unnamed menu
		std::string url;       // the movie's file
		std::string path;      // the field's path in the movie
		std::string adapter;   // "skyui-item-search", "skyui-magic-search", "map-location-finder", "enchant-name",
		                       // "skyui-mcm-dialog", "generic"
		std::string text;      // the field's text when it was found
		bool explicitOpen = false;   // a gesture or DevBench asked (opens even with bKeyboardAutoOpen off)
	};
	bool TakeOpenRequest(Target& a_out);
	bool TakeCloseRequest(std::string& a_why);

	struct PadEvent
	{
		int kind = 0;               // 0 a pad button, 1 a stick, 2 a mouse button over the keyboard
		std::uint32_t code = 0;     // XInput mask (button) / 0 left 1 right (stick) / 0 (mouse left)
		bool down = false;
		float x = 0.0f, y = 0.0f;   // stick axes, or the pointer's position
	};
	void DrainPad(std::vector<PadEvent>& a_out);

	// The keyboard says what it is doing, so the hook claims the right inputs (kNone closes the target).
	void SetClaim(Claim a_claim);
	// The keyboard's rectangle this frame, for a mouse press with bKeyboardWithMouse (screen pixels).
	void PublishRect(bool a_valid, float a_x0, float a_y0, float a_x1, float a_y1);

	// The edits, from the keyboard (render thread) - each becomes a UI task on the current target.
	void ApplyEdit(int a_backspaces, const std::string& a_insertUtf8);
	void SendEnter();
	void EndInput(const char* a_why);   // the adapter's End (item search: endInput()), then the target is let go
	// Parked: one step of the list under the box (-1 up, +1 down) - SkyUI's item, spell and map-finder lists, which hold
	// their selection while their search is active. Nothing for a menu with no list.
	void MoveList(int a_dir);

	// The field's text as last read back (the keyboard's echo line), and whether a target is held.
	std::string FieldText();
	bool HasTarget();

	// ---- DevBench (listener thread) -----------------------------------------------------------
	// {"op":"gamefield","do":"probe"[,"menu":..]} - READ-ONLY: per open menu, which adapter, which member paths answer,
	// the focused field and its text, the co-installed keyboards, and per route how many edits were sent / proven /
	// unchanged. {"do":"state"} the snapshot. {"do":"start","menu":..} runs the adapter's start gesture (startInput /
	// showLocationFinder), {"do":"route","n":1|2|3} forces a route, {"do":"detect"} looks for the focused field now.
	std::string ToolJson(const std::string& a_args);
	std::string StateJson();   // the snapshot, for the keyboard's state and amf.menu state
}
