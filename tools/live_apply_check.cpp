// Off-game check of live apply's decision (include/LiveApplyCore.h - the same gate McmLoader.cpp and PrismaRedux.cpp
// tick every frame). Plays timed sequences of changes, held controls, unfinished writes and page switches through the
// gate at 60 frames a second and checks when it fires: once per settled change, never during a drag, not before the
// write finished (unless it is stuck), and never once the page is left or the setting is off.
//   tools\live_apply_check.bat
// Prints one line per check and "live_apply_check: N of N passed"; exits 1 on any failure.
#include "../include/LiveApplyCore.h"

#include <functional>
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

	constexpr double kFrame = 1.0 / 60.0;

	// What the world looks like at time t.
	struct World
	{
		std::function<bool(double)> changeAt = [](double) { return false; };  // a change lands this frame
		std::function<bool(double)> held = [](double) { return false; };
		std::function<int(double)> inFlight = [](double) { return 0; };
		std::function<int(double)> open = [](double) { return 7; };
		std::function<bool(double)> enabled = [](double) { return true; };
		int key = 7;
	};

	struct Run
	{
		std::vector<double> fires;
		int dropsOff = 0;
		int dropsLeft = 0;
	};

	Run Play(const World& a_w, double a_seconds)
	{
		liveapply::Gate gate;
		Run r;
		for (int f = 0; f * kFrame < a_seconds; ++f)
		{
			const double t = f * kFrame;
			if (a_w.changeAt(t)) { gate.Change(a_w.key, t); }
			const auto d = gate.Tick(t, a_w.enabled(t), a_w.open(t), a_w.held(t), a_w.inFlight(t));
			if (d == liveapply::Decision::kFire) { r.fires.push_back(t); }
			if (d == liveapply::Decision::kDropOff) { ++r.dropsOff; }
			if (d == liveapply::Decision::kDropLeft) { ++r.dropsLeft; }
		}
		return r;
	}

	bool Near(double a_t, double a_want) { return a_t >= a_want - kFrame * 1.5 && a_t <= a_want + kFrame * 1.5; }
	bool Once(double a_t, double a_at) { return a_t >= a_at && a_t < a_at + kFrame; }
}

int main()
{
	// 1. one toggle click: one apply, `quiet` after it
	{
		World w;
		w.changeAt = [](double t) { return Once(t, 1.0); };
		const Run r = Play(w, 3.0);
		Check(r.fires.size() == 1 && Near(r.fires[0], 1.35), "one change applies once, 0.35 s after it");
	}
	// 2. nothing changed: nothing applied
	{
		World w;
		const Run r = Play(w, 3.0);
		Check(r.fires.empty() && r.dropsLeft == 0 && r.dropsOff == 0, "no change: nothing applied, nothing dropped");
	}
	// 3. a slider dragged for 2 s, changing every frame: no apply during the drag, one after the release
	{
		World w;
		w.changeAt = [](double t) { return t >= 1.0 && t < 3.0; };
		w.held = [](double t) { return t >= 1.0 && t < 3.2; };   // let go 0.2 s after the last value change
		const Run r = Play(w, 5.0);
		Check(r.fires.size() == 1, "a 2 s slider drag that changes every frame applies exactly once");
		Check(!r.fires.empty() && r.fires[0] >= 3.2, "...and only after the slider is let go");
		Check(!r.fires.empty() && Near(r.fires[0], 3.35), "...0.35 s after its last change (released 0.2 s after it)");
	}
	// 4. a slider held still for a long time after its last change: still waits for the release
	{
		World w;
		w.changeAt = [](double t) { return Once(t, 1.0); };
		w.held = [](double t) { return t >= 0.9 && t < 4.0; };
		const Run r = Play(w, 6.0);
		Check(r.fires.size() == 1 && Near(r.fires[0], 4.0), "a held control never applies while held (released at 4 s, applied then)");
	}
	// 5. D-pad nudges 0.2 s apart (closer than quiet): one apply after the last
	{
		World w;
		w.changeAt = [](double t) { return Once(t, 1.0) || Once(t, 1.2) || Once(t, 1.4) || Once(t, 1.6); };
		const Run r = Play(w, 4.0);
		Check(r.fires.size() == 1 && Near(r.fires[0], 1.95), "four nudges 0.2 s apart apply once, after the last");
	}
	// 6. nudges 1 s apart: each applies
	{
		World w;
		w.changeAt = [](double t) { return Once(t, 1.0) || Once(t, 2.0) || Once(t, 3.0); };
		const Run r = Play(w, 5.0);
		Check(r.fires.size() == 3, "three changes 1 s apart apply three times");
	}
	// 7. the write is still in flight: wait for it
	{
		World w;
		w.changeAt = [](double t) { return Once(t, 1.0); };
		w.inFlight = [](double t) { return t < 2.0 ? 1 : 0; };
		const Run r = Play(w, 4.0);
		Check(r.fires.size() == 1 && Near(r.fires[0], 2.0), "an unfinished write holds the apply until it finishes");
	}
	// 8. a write that never reports back: the apply goes after `stuck`
	{
		World w;
		w.changeAt = [](double t) { return Once(t, 1.0); };
		w.inFlight = [](double) { return 1; };
		const Run r = Play(w, 9.0);
		Check(r.fires.size() == 1 && Near(r.fires[0], 6.0), "a write that never finishes holds the apply 5 s at most");
	}
	// 9. the page is left before the change settles: dropped (leaving sends the close), never applied
	{
		World w;
		w.changeAt = [](double t) { return Once(t, 1.0); };
		w.open = [](double t) { return t < 1.1 ? 7 : 3; };
		const Run r = Play(w, 3.0);
		Check(r.fires.empty() && r.dropsLeft == 1, "the page left before the change settled: dropped once, not applied");
	}
	// 10. the menu closed (no page drawn)
	{
		World w;
		w.changeAt = [](double t) { return Once(t, 1.0); };
		w.open = [](double t) { return t < 1.2 ? 7 : -1; };
		const Run r = Play(w, 3.0);
		Check(r.fires.empty() && r.dropsLeft == 1, "the menu closed before the change settled: dropped, not applied");
	}
	// 11. setting off: nothing applied, the pending change dropped
	{
		World w;
		w.changeAt = [](double t) { return Once(t, 1.0) || Once(t, 2.0); };
		w.enabled = [](double) { return false; };
		const Run r = Play(w, 3.0);
		Check(r.fires.empty() && r.dropsOff == 2, "setting off: no apply, each change dropped (the old close-only behaviour)");
	}
	// 12. switched off while a change is pending
	{
		World w;
		w.changeAt = [](double t) { return Once(t, 1.0); };
		w.enabled = [](double t) { return t < 1.1; };
		const Run r = Play(w, 3.0);
		Check(r.fires.empty() && r.dropsOff == 1, "switched off while pending: dropped");
	}
	// 13. a change on another page replaces the pending one (the first page was left)
	{
		liveapply::Gate gate;
		gate.Change(7, 1.0);
		gate.Change(3, 1.1);
		const auto d1 = gate.Tick(1.2, true, 3, false, 0);
		const auto d2 = gate.Tick(1.5, true, 3, false, 0);
		const auto d3 = gate.Tick(2.0, true, 3, false, 0);
		Check(d1 == liveapply::Decision::kWait && d2 == liveapply::Decision::kFire && d3 == liveapply::Decision::kIdle && gate.Key() == 3,
			"a change on another page replaces the pending one, which then fires once for the new page");
	}
	// 14. pending/quiet reporting (for the DevBench tool)
	{
		liveapply::Gate gate;
		const bool idle = !gate.Pending() && gate.QuietFor(5.0) == 0.0;
		gate.Change(2, 1.0);
		Check(idle && gate.Pending() && gate.QuietFor(1.25) == 0.25 && gate.Key() == 2, "Pending, Key and QuietFor report the pending change");
		gate.Reset();
		Check(!gate.Pending() && gate.Tick(9.0, true, 2, false, 0) == liveapply::Decision::kIdle, "Reset drops the pending change");
	}
	// 15. names are stable for logs and the tool
	Check(std::string(liveapply::Name(liveapply::Decision::kFire)) == "fire" && std::string(liveapply::Name(liveapply::Decision::kWait)) == "wait",
		"decision names");

	std::cout << "live_apply_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
