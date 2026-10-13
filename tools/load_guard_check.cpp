// Off-game check of include/LoadGuardCore.h - the same rules AMF's input hook, its open-menu path, its Other menus launcher
// and its SKSE task queuing ask (LoadGuard.h): the guard holds from plugin load until the Main Menu opens, and while the
// Loading Menu is open; an ignored input is logged once per stretch, not per key.
//   tools\load_guard_check.bat
// Prints one line per check and "load_guard_check: N of N passed"; exits 1 on any failure.
#include "../include/LoadGuardCore.h"

#include <iostream>
#include <string>

namespace
{
	int g_pass = 0, g_fail = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	// What AMF's input hook does with one press, as LoadGuard.h describes it: while blocked the list is passed on untouched
	// (nothing consumed, the menu key does not open the menu) and the first press of a stretch is logged.
	struct Hook
	{
		loadguard::core::State& guard;
		bool menuOpen = false;
		int logged = 0;
		int consumed = 0;
		void Press(bool a_menuKey)
		{
			if (guard.Blocked())
			{
				if (guard.TakeFirstIgnored()) { ++logged; }
				return;   // passed to the game as it came
			}
			if (a_menuKey) { menuOpen = !menuOpen; ++consumed; }
			else if (menuOpen) { ++consumed; }
		}
		// the render thread's first frame of a stretch: the menu closes
		void Frame()
		{
			if (menuOpen && guard.Blocked()) { menuOpen = false; }
		}
	};
}

int main()
{
	using loadguard::core::State;

	// ---- start-up: from plugin load to the Main Menu ----
	{
		State g;
		Hook h{ g };
		Check(g.Blocked(), "plugin load: the guard holds before anything has happened (not kDataLoaded, not a frame count)");
		h.Press(true);
		h.Press(true);
		h.Press(false);
		Check(!h.menuOpen && h.consumed == 0, "F1 three times during the start-up load: the menu stays closed and nothing is taken");
		Check(h.logged == 1, "the ignored input is logged once for the whole start-up, not once per key");
		g.OnLoadingMenu(true);
		g.OnLoadingMenu(false);
		Check(g.Blocked(), "a Loading Menu that comes and goes before the Main Menu does not lift the start-up hold");
		h.Press(true);
		Check(h.logged == 1, "still the same stretch: no second log line");
		g.OnMainMenuOpened();
		Check(!g.Blocked(), "the Main Menu opened: the guard lifts");
		h.Press(true);
		Check(h.menuOpen && h.consumed == 1, "F1 at the main menu opens the menu");
		h.Press(true);
		Check(!h.menuOpen, "and closes it again");
	}
	// ---- a save load, a door, again and again ----
	{
		State g;
		Hook h{ g };
		g.OnMainMenuOpened();
		for (int round = 1; round <= 3; ++round)
		{
			h.Press(true);
			Check(h.menuOpen, "round " + std::to_string(round) + ": the menu is open before the load");
			g.OnLoadingMenu(true);
			Check(g.Blocked(), "round " + std::to_string(round) + ": the Loading Menu opened - the guard holds");
			h.Frame();
			Check(!h.menuOpen, "round " + std::to_string(round) + ": the menu that was open when the load started is closed");
			const int before = h.logged;
			h.Press(true);
			h.Press(false);
			h.Press(true);
			Check(!h.menuOpen && h.logged == before + 1, "round " + std::to_string(round) + ": F1 and clicks during the load are ignored, logged once for this load");
			g.OnLoadingMenu(false);
			Check(!g.Blocked(), "round " + std::to_string(round) + ": the Loading Menu closed - the guard lifts");
			h.Press(true);
			Check(h.menuOpen, "round " + std::to_string(round) + ": F1 works once the load is over");
			h.Press(true);
		}
		Check(h.logged == 3, "three loads, three log lines - one per load");
	}
	// ---- SKSE's messages lift the start-up hold too (a game loaded or started: only the running game loop sends them) ----
	{
		State g;
		g.OnLoadingMenu(true);
		g.OnGameReady();
		Check(g.Blocked(), "kPostLoadGame while the Loading Menu is still up: still loading");
		g.OnLoadingMenu(false);
		Check(!g.Blocked(), "and the Loading Menu closing lifts it, with no Main Menu event seen");
	}
	{
		State g;
		g.OnMainMenuOpened();
		g.OnMainMenuOpened();
		Check(!g.Blocked() && g.period == 1, "the Main Menu opening again (back from a game) changes nothing");
		Check(!g.TakeFirstIgnored(), "nothing is logged while the guard is off");
	}

	std::cout << "load_guard_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
