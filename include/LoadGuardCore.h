#pragma once

// ============================================================================================
// THE LOADING GUARD - the part with no game in it, so tools\load_guard_check.cpp runs the same code offline (2.2.0; the
// owner, 2026-10-12: "Honestly amf shouldn't work during loading in as that can cause issues so build that in").
//
// One question, "is the game loading or not yet at the main menu?", and every AMF input path and its open-menu path ask
// it (LoadGuard.h). It HOLDS:
//   * from the moment the plugin loads until the Main Menu has really opened (its MenuOpenCloseEvent) - or until SKSE says
//     a game was loaded or started (kPostLoadGame / kNewGame), which only the running game loop sends. Not a frame count
//     and not kDataLoaded: the 2.2.0 hang investigation (4. plans\amf-2.2.0-investigation\NOTES.txt) saw F1 open AMF after
//     kDataLoaded while other plugins were still registering, and the SKSE task AMF queued on that open never ran;
//   * while the Loading Menu is open - every load after that: a save load, a new game, a door or a fast travel.
// It is events in, one answer out: nothing is polled and no game object is read to decide it.
// ============================================================================================

#include <cstdint>

namespace loadguard::core
{
	struct State
	{
		bool started = false;          // the Main Menu has opened, or SKSE reported a game loaded / started
		bool loading = false;          // the Loading Menu is open
		std::uint32_t period = 1;      // counts each stretch the guard holds, so "ignored" is logged once per stretch
		std::uint32_t loggedPeriod = 0;

		bool Blocked() const { return !started || loading; }

		void OnMainMenuOpened() { Set(true, loading); }
		void OnGameReady() { Set(true, loading); }
		void OnLoadingMenu(bool a_opening) { Set(started, a_opening); }

		// True the first time an input is ignored in this stretch of the guard (the caller logs it once), false after.
		bool TakeFirstIgnored()
		{
			if (!Blocked() || loggedPeriod == period) { return false; }
			loggedPeriod = period;
			return true;
		}

	private:
		void Set(bool a_started, bool a_loading)
		{
			const bool was = Blocked();
			started = a_started;
			loading = a_loading;
			if (!was && Blocked()) { ++period; }   // a new stretch begins: the next ignored input is logged again
		}
	};
}
