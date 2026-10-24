// Off-game check of include/FrameProfileCore.h - which frames after an AMF opening are logged, and the summary (2.3.0, the
// slow first open of the 2026-10-16 to-do entry).
//   tools\frame_profile_check.bat
// Prints one line per check and "frame_profile_check: N of N passed"; exits 1 on any failure.
#include "../include/FrameProfileCore.h"

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

	frameprofile::Frame F(double a_interval, double a_menu = 1.0)
	{
		frameprofile::Frame f;
		f.intervalMs = a_interval;
		f.partMs[frameprofile::kMenu] = a_menu;
		return f;
	}
}

int main()
{
	using namespace frameprofile;
	Tracker t;

	// hidden: nothing is measured
	Check(!t.Begin(false) && !t.Record(F(16.0)), "a hidden menu is not measured");

	// the session's first opening: the first frames are always logged, then only slow ones
	int logged = 0;
	for (int i = 0; i < kFrames; ++i)
	{
		Check(t.Begin(true) || i >= kFrames, std::string("frame ") + std::to_string(i) + " of the opening is measured");
		const double interval = (i == 10 || i == 11) ? 900.0 : 16.0;   // two 0.9 s frames, as the to-do entry reports
		if (t.Record(F(interval))) { ++logged; }
		if (i == kFrames - 1) { Check(t.SummaryDue(), "the summary is due on the last measured frame"); }
	}
	Check(logged == kAlwaysLog + 2, "logged: the first " + std::to_string(kAlwaysLog) + " frames and the two slow ones (got " + std::to_string(logged) + ")");
	const Summary& s = t.GetSummary();
	Check(s.opening == 1 && s.frames == kFrames && s.slowFrames == 2 && s.maxIntervalMs == 900.0, "the summary: opening 1, 2 slow frames, max 900 ms");
	Check(!t.Begin(true), "past kFrames the open menu is no longer measured");

	// a second opening starts a new measurement
	t.Begin(false);
	Check(t.Begin(true) && t.Openings() == 2 && t.FrameIndex() == 0, "closing and opening again starts opening 2");
	t.Record(F(16.0));
	Check(t.GetSummary().opening == 2 && t.GetSummary().frames == 1, "opening 2 has its own summary");

	// AMF's own work over the threshold logs a frame even with a normal interval
	Tracker u;
	u.Begin(true);
	for (int i = 0; i < kAlwaysLog; ++i) { u.Begin(true); u.Record(F(16.0)); }
	u.Begin(true);
	Check(u.Record(F(16.0, kSlowAmfMs + 5.0)), "a frame whose AMF part is slow is logged");
	Frame heavy;
	heavy.partMs[kBackend] = 400.0;
	heavy.partMs[kMenu] = 3.0;
	Check(Tracker::Heaviest(heavy) == kBackend, "Heaviest names the part that took the time");

	std::cout << "frame_profile_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail ? 1 : 0;
}
