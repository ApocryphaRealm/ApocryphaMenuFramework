#pragma once

// ============================================================================================
// THE GAME-KEY INJECTOR'S SEQUENCING (2.2.0 held-key fix) - the part with no game in it, so tools\imgui_host_check.cpp runs it
// offline. Open Animation Replacer, Community Shaders and IED read their menu keys from the game's own input events, and a
// Windows F13-F15 (SendInput) never reaches those (the final in-game test, 2026-10-12; HostCore.h InputPath). So AMF presses
// their keys as the game's keyboard would: a ButtonEvent per key in the event list PollInputDevices dispatches (Input.cpp),
// value 1 and no held time on the first dispatch, value 1 with the time held on each dispatch after, value 0 on the release.
//
// Down / Up requests are queued from the render thread (ImguiHost's press steps) and taken one dispatch at a time on the game
// thread. A key never goes down and up in the same dispatch: an edge for a key that already changed in this dispatch waits
// for the next one, so every press is seen down for at least one dispatch.
// ============================================================================================

#include <algorithm>
#include <cstdint>
#include <deque>
#include <vector>

namespace gamekeys
{
	struct KeyEvent
	{
		std::uint32_t dik = 0;   // the scan code, as the game's keyboard events carry it (an E0 key with 0x80 added)
		float value = 0.0f;      // 1 down, 0 released
		float heldSecs = 0.0f;   // 0 on the press; how long it has been down on a held repeat or the release
	};

	class VirtualKeys
	{
	public:
		void Down(std::uint32_t a_dik) { m_queue.push_back({ a_dik, true }); }
		void Up(std::uint32_t a_dik) { m_queue.push_back({ a_dik, false }); }

		// nothing queued and no key down: the injector passes the game's list on untouched
		bool Idle() const { return m_queue.empty() && m_down.empty(); }

		// Every key down, let go of in one go (the injector going away, a load): their releases come on the next Poll
		void ReleaseAll()
		{
			for (const auto& d : m_down) { m_queue.push_back({ d.dik, false }); }
		}

		// The events for one dispatch at a_now seconds, in order: held repeats of the keys already down, then this dispatch's
		// edges in the order they were asked for.
		std::vector<KeyEvent> Poll(double a_now)
		{
			std::vector<KeyEvent> out;
			std::vector<std::uint32_t> changed;
			std::vector<KeyEvent> edges;
			while (!m_queue.empty())
			{
				const Cmd c = m_queue.front();
				if (std::find(changed.begin(), changed.end(), c.dik) != changed.end()) { break; }   // its other edge: next dispatch
				m_queue.pop_front();
				changed.push_back(c.dik);
				auto it = std::find_if(m_down.begin(), m_down.end(), [&](const HeldKey& d) { return d.dik == c.dik; });
				if (c.down)
				{
					if (it != m_down.end()) { continue; }   // already down: nothing new
					m_down.push_back({ c.dik, a_now });
					edges.push_back({ c.dik, 1.0f, 0.0f });
				}
				else
				{
					if (it == m_down.end()) { continue; }   // not down: no release to send
					edges.push_back({ c.dik, 0.0f, static_cast<float>(a_now - it->since) });
					m_down.erase(it);
				}
			}
			for (const auto& d : m_down)
			{
				if (std::find(changed.begin(), changed.end(), d.dik) != changed.end()) { continue; }   // it went down this dispatch
				out.push_back({ d.dik, 1.0f, std::max(0.001f, static_cast<float>(a_now - d.since)) });
			}
			out.insert(out.end(), edges.begin(), edges.end());
			return out;
		}

	private:
		struct Cmd
		{
			std::uint32_t dik;
			bool down;
		};
		struct HeldKey
		{
			std::uint32_t dik;
			double since;
		};
		std::deque<Cmd> m_queue;
		std::vector<HeldKey> m_down;
	};
}
