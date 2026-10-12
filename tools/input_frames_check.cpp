// Off-game check of the input drain's one-transition-per-frame rule (include/InputFrames.h - the same function the drain
// runs). Feeds timed press/release sequences through a model of the drain (last frame's carry first, then what arrived)
// and checks what ImGui would see each frame: every press down for at least one frame, every release up for one, no two
// clicks merged, other buttons and moves never held.
//   tools\input_frames_check.bat
// Prints one line per check and "input_frames_check: N of N passed"; exits 1 on any failure.
#include "../include/InputFrames.h"

#include <cstdint>
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

	std::cout << "input_frames_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
