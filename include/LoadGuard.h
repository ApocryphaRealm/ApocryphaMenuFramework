#pragma once

// ============================================================================================
// THE LOADING GUARD (2.2.0; the owner, 2026-10-12: "Honestly amf shouldn't work during loading in as that can cause issues
// so build that in"). While the game is loading, or has not yet reached its main menu, AMF does nothing with input: the
// menu does not open (F1, DevBench, the System row, Other menus), no AMF key is handled, no input is taken from the game,
// and nothing queues an SKSE task. If the menu is open when a load starts, it closes. The rules are in LoadGuardCore.h.
// ============================================================================================

namespace loadguard
{
	// Registers the MenuOpenCloseEvent sink (kDataLoaded - before the Main Menu can open). Safe to call again.
	void Install();

	// SKSE kPostLoadGame / kNewGame: the game loop is running and a game is in (lifts the start-up hold even if the Main
	// Menu's event was missed).
	void OnGameReady();

	// True while the game is loading or before the main menu. Any thread; a lock and two bools.
	bool Blocked();

	// An input or an open request was ignored because of the guard: one debug line per stretch of loading, naming the first.
	void NoteIgnored(const char* a_what);
}
