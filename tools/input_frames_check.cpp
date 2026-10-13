// Off-game check of the input drain's one-transition-per-frame rule (include/InputFrames.h - the same function the drain
// runs). Feeds timed press/release sequences through a model of the drain (last frame's carry first, then what arrived)
// and checks what ImGui would see each frame: every press down for at least one frame, every release up for one, no two
// clicks merged, other buttons and moves never held.
//   tools\input_frames_check.bat
// Prints one line per check and "input_frames_check: N of N passed"; exits 1 on any failure.
#include "../include/InputFrames.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
	int g_pass = 0, g_fail = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	struct Rec
	{
		int kind;            // 0 move, 1 mouse button, 2 key
		std::uint32_t code;
		bool down;
	};
	Rec P(std::uint32_t a_b = 0) { return { 1, a_b, true }; }
	Rec R(std::uint32_t a_b = 0) { return { 1, a_b, false }; }
	Rec M() { return { 0, 0, false }; }
	Rec K(std::uint32_t a_k, bool a_d) { return { 2, a_k, a_d }; }

	std::uint64_t Key(const Rec& a_r) { return a_r.kind == 0 ? 0 : ((static_cast<std::uint64_t>(a_r.kind) + 1) << 32) | a_r.code; }

	// Runs frames: a_arrivals[f] is what reached the queue during frame f. Returns, per frame, ImGui's final state of
	// mouse button 0 after the drain (trickling off: only the last event of a frame counts), and counts the clicks ImGui
	// sees (a frame down after a frame up), and the records each frame actually delivered.
	struct Run
	{
		std::vector<int> state;    // per frame: 1 down, 0 up, for button a_b
		int clicks = 0;
		int deliveredMoves = 0;
		bool twoChangesInAFrame = false;
	};

	Run Drive(const std::vector<std::vector<Rec>>& a_arrivals, std::uint32_t a_b = 0, int a_extraFrames = 8)
	{
		Run out;
		std::vector<Rec> carry;
		bool imguiDown = false;
		const int frames = static_cast<int>(a_arrivals.size()) + a_extraFrames;
		for (int f = 0; f < frames; ++f)
		{
			std::vector<Rec> drain;
			drain.swap(carry);   // last frame's carry first
			if (f < static_cast<int>(a_arrivals.size())) { drain.insert(drain.end(), a_arrivals[f].begin(), a_arrivals[f].end()); }
			inputframes::OneTransitionPerFrame(drain, carry, Key);
			int changes = 0;
			bool before = imguiDown;
			for (const Rec& r : drain)
			{
				if (r.kind == 0) { ++out.deliveredMoves; continue; }
				if (r.kind == 1 && r.code == a_b) { ++changes; imguiDown = r.down; }
			}
			if (changes > 1) { out.twoChangesInAFrame = true; }
			if (!before && imguiDown) { ++out.clicks; }
			out.state.push_back(imguiDown ? 1 : 0);
		}
		return out;
	}

	std::string Str(const std::vector<int>& a_v)
	{
		std::string s;
		for (const int v : a_v) { s += v ? 'D' : '.'; }
		return s;
	}

	// ---- v4: the game's button events, as the hook sees them (InputFrames.h ButtonEdge) ------------------------------
	// A game ButtonEvent: value 1 = down, 0 = up; held = heldDownSecs.
	struct GameEv
	{
		float value;
		float held;
	};

	// Polls every a_frameMs from a_phaseMs; each poll is one batch the hook reads and one frame the drain runs. A click
	// pressed at `at` and released `hold` ms later: each edge lands in the batch of the first poll at or after it (a
	// buffered device), the press with held 0, the release with the time the button was SEEN held - the polls strictly
	// between the press's batch and the release's, times the frame - so a release in the press's batch or the next one has
	// held 0, the case CommonLib's IsUp() does not count. Returns the batches.
	std::vector<std::vector<GameEv>> GameBatches(const std::vector<std::pair<double, double>>& a_clicks, double a_frameMs, double a_phaseMs)
	{
		double last = 0.0;
		for (const auto& [at, hold] : a_clicks) { last = std::max(last, at + hold); }
		const int polls = static_cast<int>((last - a_phaseMs) / a_frameMs) + 3;
		std::vector<std::vector<GameEv>> out(static_cast<std::size_t>(polls));
		auto pollOf = [&](double a_t) { return std::max(0, static_cast<int>(std::ceil((a_t - a_phaseMs) / a_frameMs - 1e-9))); };
		for (const auto& [at, hold] : a_clicks)
		{
			const int pp = pollOf(at), pr = pollOf(at + hold);
			out[static_cast<std::size_t>(pp)].push_back({ 1.0f, 0.0f });
			for (int k = pp + 1; k < pr; ++k) { out[static_cast<std::size_t>(k)].push_back({ 1.0f, static_cast<float>((k - pp) * a_frameMs / 1000.0) }); }
			out[static_cast<std::size_t>(pr)].push_back({ 0.0f, pr - pp > 1 ? static_cast<float>((pr - pp - 1) * a_frameMs / 1000.0) : 0.0f });
		}
		return out;
	}

	// The hook's filter then the drain: a_old = the v3 filter (IsDown() / IsUp() - a release needs a held time), else v4's
	// ButtonEdge. Returns what ImGui saw: its clicks and whether the button is left down.
	struct Seen
	{
		int clicks = 0;
		bool leftDown = false;
		bool twoChangesInAFrame = false;
	};

	Seen ThroughHook(const std::vector<std::vector<GameEv>>& a_batches, bool a_old)
	{
		std::vector<std::vector<Rec>> arrivals;
		inputframes::FedButtons fed;
		for (const auto& batch : a_batches)
		{
			std::vector<Rec> recs;
			for (const GameEv& e : batch)
			{
				bool down = false;
				if (a_old)
				{
					const bool isDown = e.value > 0.0f && e.held == 0.0f, isUp = e.value == 0.0f && e.held > 0.0f;
					if (!isDown && !isUp) { continue; }
					down = isDown;
				}
				else
				{
					const auto edge = fed.Feed(0x100000000ull, e.value, e.held);
					if (edge == inputframes::Edge::kNone) { continue; }
					down = edge == inputframes::Edge::kPress;
				}
				recs.push_back(down ? P() : R());
			}
			arrivals.push_back(std::move(recs));
		}
		const Run r = Drive(arrivals, 0, 8);
		return { r.clicks, !r.state.empty() && r.state.back() == 1, r.twoChangesInAFrame };
	}
}

int main()
{
	{
		// a click shorter than a frame: press and release in the same drain
		const Run r = Drive({ { M(), P(), R() } });
		Check(r.clicks == 1 && r.state[0] == 1 && r.state[1] == 0, "a press and release in ONE frame: down for frame 0, up on frame 1 (" + Str(r.state) + ")");
		Check(r.deliveredMoves == 1, "the move in that frame is delivered at once");
	}
	{
		// two clicks inside one frame (a 1 ms hold, 1 ms gap)
		const Run r = Drive({ { P(), R(), P(), R() } });
		Check(r.clicks == 2 && !r.twoChangesInAFrame, "two whole clicks in one frame: two clicks, one change per frame (" + Str(r.state) + ")");
	}
	{
		// 6b09667's miss: a same-frame click, then the next click's press one frame later and its release the frame after
		const Run r = Drive({ { P(), R() }, { P() }, { R() } });
		Check(r.clicks == 2, "click in frame 0, the next click's press in frame 1: two clicks, not merged (" + Str(r.state) + ")");
	}
	{
		// the next click arriving whole one frame later
		const Run r = Drive({ { P(), R() }, { P(), R() } });
		Check(r.clicks == 2 && !r.twoChangesInAFrame, "two same-frame clicks in consecutive frames: two clicks (" + Str(r.state) + ")");
	}
	{
		// 5 quick clicks, each press and release in its own frame pair arriving every frame
		std::vector<std::vector<Rec>> a;
		for (int i = 0; i < 5; ++i) { a.push_back({ P(), R() }); }
		const Run r = Drive(a, 0, 12);
		Check(r.clicks == 5 && !r.twoChangesInAFrame, "five same-frame clicks in five frames: five clicks (" + Str(r.state) + ")");
	}
	{
		// a click that spans frames is untouched
		const Run r = Drive({ { P() }, {}, { R() } }, 0, 2);
		Check(r.clicks == 1 && r.state[0] == 1 && r.state[1] == 1 && r.state[2] == 0, "a held click: down frames 0-1, up frame 2, nothing held (" + Str(r.state) + ")");
	}
	{
		// one button's carry does not hold another button or a key
		std::vector<Rec> drain = { P(0), R(0), P(1), K(30, true), K(30, false), R(1) };
		std::vector<Rec> carry;
		inputframes::OneTransitionPerFrame(drain, carry, Key);
		Check(drain.size() == 3 && carry.size() == 3, "left click, right click and a key in one frame: each first change now, each second next frame");
		Check(carry.size() == 3 && !carry[0].down && carry[0].code == 0 && !carry[1].down && carry[1].kind == 2 && !carry[2].down && carry[2].code == 1,
			  "the carry keeps arrival order");
	}
	{
		// the carry goes ahead of newer input of the same button
		std::vector<Rec> carry;
		std::vector<Rec> drain = { P(), R(), P() };
		inputframes::OneTransitionPerFrame(drain, carry, Key);
		std::vector<Rec> next;
		next.swap(carry);
		next.push_back(R());
		inputframes::OneTransitionPerFrame(next, carry, Key);
		Check(next.size() == 1 && !next[0].down && carry.size() == 2 && carry[0].down && !carry[1].down,
			  "frame 1 delivers the held release first; the second press and its release follow in order");
	}

	{
		// v4: THE GAME'S OWN EVENTS (the v3 test, 2026-10-12: with a 60 ms gap, 10 ms clicks 0 of 5, 20 ms 3 of 4, 30 ms 4 of 4 -
		// every lost click logged its press and never its release). The hook's filter is run too, not just the drain.
		std::cout << "== the hook's filter on the game's events (frame 11.6 ms, each case swept across 116 poll phases)\n";
		{
			// press and release in ONE batch, the release with no held time
			const Seen v4 = ThroughHook({ { { 1.0f, 0.0f }, { 0.0f, 0.0f } } }, false);
			const Seen v3 = ThroughHook({ { { 1.0f, 0.0f }, { 0.0f, 0.0f } } }, true);
			Check(v4.clicks == 1 && !v4.leftDown && !v4.twoChangesInAFrame, "press and release in the same batch (release held 0): one click, released");
			Check(v3.leftDown, "control: the v3 filter leaves that button down (the reported fault)");
			const Seen v4h = ThroughHook({ { { 1.0f, 0.0f }, { 0.0f, 0.004f } } }, false);
			Check(v4h.clicks == 1 && !v4h.leftDown, "press and release in the same batch (release with a held time): one click, released");
			// across batches, the release alone in the next batch with no held time
			const Seen v4n = ThroughHook({ { { 1.0f, 0.0f } }, { { 0.0f, 0.0f } } }, false);
			Check(v4n.clicks == 1 && !v4n.leftDown, "press in one batch, release in the next with no held time: one click, released");
			// a held repeat is not a press: one click
			const Seen v4r = ThroughHook({ { { 1.0f, 0.0f } }, { { 1.0f, 0.0116f } }, { { 1.0f, 0.0232f } }, { { 0.0f, 0.0232f } } }, false);
			Check(v4r.clicks == 1 && !v4r.leftDown, "a held press with repeats: one click, released");
			// a release whose press this framework never passed on (held across the menu opening) still goes through
			const Seen v4o = ThroughHook({ { { 0.0f, 0.5f } } }, false);
			Check(v4o.clicks == 0 && !v4o.leftDown, "a release with a held time and no press seen: no click, nothing held");
			// a value-0 event with no held time and no press passed on is nothing
			inputframes::FedButtons fb;
			Check(fb.Feed(7, 0.0f, 0.0f) == inputframes::Edge::kNone, "a value-0 event with no held time for a button not passed down: no edge");
		}
		for (const double gapMs : { 0.0, 60.0 })
		{
			for (const double holdMs : { 1.0, 5.0, 10.0, 20.0, 30.0 })
			{
				const int expect = gapMs > 0.0 ? 2 : 1;
				int ok = 0, total = 0, oldLost = 0, sameBatch = 0, nextBatchNoHeld = 0;
				std::string firstBad;
				for (int ph = 0; ph < 116; ++ph)
				{
					const double phase = ph * 0.1;
					std::vector<std::pair<double, double>> clicks = { { 20.0, holdMs } };
					if (gapMs > 0.0) { clicks.push_back({ 20.0 + holdMs + gapMs, holdMs }); }
					const auto batches = GameBatches(clicks, 11.6, phase);
					bool same = false, next = false;
					for (const auto& b : batches)
					{
						if (b.size() >= 2) { same = true; }
						else if (b.size() == 1 && b[0].value == 0.0f && b[0].held == 0.0f) { next = true; }
					}
					sameBatch += same ? 1 : 0;
					nextBatchNoHeld += next ? 1 : 0;
					const Seen v4 = ThroughHook(batches, false);
					const Seen v3 = ThroughHook(batches, true);
					++total;
					if (v4.clicks == expect && !v4.leftDown && !v4.twoChangesInAFrame) { ++ok; }
					else if (firstBad.empty()) { firstBad = " (first miss at phase " + std::to_string(phase) + " ms)"; }
					if (v3.clicks != expect || v3.leftDown) { ++oldLost; }
				}
				const std::string what = std::to_string(expect) + " click(s) of " + std::to_string(static_cast<int>(holdMs)) + " ms" +
										 (gapMs > 0.0 ? ", 60 ms apart" : "");
				Check(ok == total, what + ": every click whole in " + std::to_string(ok) + " of " + std::to_string(total) + " phases" + firstBad +
									   " (press and release in one batch in " + std::to_string(sameBatch) + ", release alone in the next batch with no held time in " +
									   std::to_string(nextBatchNoHeld) + "; the v3 filter lost a click in " + std::to_string(oldLost) + ")");
				if (holdMs <= 10.0) { Check(oldLost == total, "control: the v3 filter loses every " + std::to_string(static_cast<int>(holdMs)) + " ms click here, as in game (0 of 5)"); }
			}
		}
	}
	std::cout << "input_frames_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
