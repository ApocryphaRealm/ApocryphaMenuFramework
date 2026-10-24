#pragma once

// WHAT THE FRAMES AFTER AN OPENING COST (2.3.0) - pure logic, no ImGui, so tools\frame_profile_check.cpp tests it alone.
//
// The to-do entry of 2026-10-16: the FIRST open of AMF in a session draws at 0.8-1.8 fps for a second or two; later opens
// are normal. The guess (the font atlas built on first open) did not hold up against the logs: the atlas is built at data
// load (8 ms, 508 glyphs) and no atlas rebuild or texture load is logged at the first open (combined run 2026-10-23,
// batch 9). So the frames are measured before anything is changed: for the first kFrames frames after each opening,
// every frame whose interval (Present to Present - everything the game did) or whose AMF work is slow is logged with
// AMF's parts, and the opening ends with a summary line. If the interval is long while AMF's part is short, the time is
// spent outside AMF's frame (the game thread, a script, a driver) - the log says which.

#include <algorithm>
#include <array>
#include <cstddef>

namespace frameprofile
{
	inline constexpr int    kFrames = 120;          // frames measured after each opening
	inline constexpr int    kAlwaysLog = 5;         // the first frames are always logged
	inline constexpr double kSlowIntervalMs = 100.0;
	inline constexpr double kSlowAmfMs = 30.0;

	// AMF's parts of one frame, in milliseconds.
	//   atlas         font / icon-face / missed-glyph rebuilds before the frame
	//   backend       the DX11 and Win32 backends' NewFrame (device objects - shaders, font texture - made when missing)
	//   imgui-newframe ImGui::NewFrame
	//   mcm           the MCM loader's and Prisma's per-frame work
	//   mod-windows   other mods' HUD elements and windows (the SMF/AMF consumer surface)
	//   menu          AMF's own window, the keyboard, highlight frames, curtain, cursor, FLICK
	//   render        ImGui::Render and the DX11 draw
	enum Part : int { kAtlas, kBackend, kImGuiNewFrame, kMcm, kModWindows, kMenu, kRender, kPartCount };
	inline constexpr const char* kPartNames[kPartCount] = { "atlas", "backend", "imgui-newframe", "mcm", "mod-windows", "menu", "render" };

	struct Frame
	{
		double intervalMs = 0.0;                         // since the previous Present
		std::array<double, kPartCount> partMs{};
		double AmfMs() const { double s = 0.0; for (const double p : partMs) { s += p; } return s; }
	};

	struct Summary
	{
		int    opening = 0;          // 1 = the session's first
		int    frames = 0;
		double maxIntervalMs = 0.0, maxAmfMs = 0.0, sumIntervalMs = 0.0, sumAmfMs = 0.0;
		int    slowFrames = 0;       // frames over kSlowIntervalMs
		std::array<double, kPartCount> maxPartMs{};
	};

	class Tracker
	{
	public:
		// Per frame: whether the menu is shown now. Returns true while this frame belongs to an opening being measured.
		bool Begin(bool a_visible)
		{
			if (a_visible && !m_wasVisible)
			{
				++m_openings;
				m_index = 0;
				m_summary = Summary{};
				m_summary.opening = m_openings;
			}
			m_wasVisible = a_visible;
			m_measuring = a_visible && m_index < kFrames;
			return m_measuring;
		}

		// Record the frame just measured. Returns true when it should be logged (a slow frame, or one of the first).
		bool Record(const Frame& a_frame)
		{
			if (!m_measuring) { return false; }
			const int i = m_index++;
			auto& s = m_summary;
			++s.frames;
			s.maxIntervalMs = std::max(s.maxIntervalMs, a_frame.intervalMs);
			s.maxAmfMs = std::max(s.maxAmfMs, a_frame.AmfMs());
			s.sumIntervalMs += a_frame.intervalMs;
			s.sumAmfMs += a_frame.AmfMs();
			if (a_frame.intervalMs > kSlowIntervalMs) { ++s.slowFrames; }
			for (int p = 0; p < kPartCount; ++p) { s.maxPartMs[p] = std::max(s.maxPartMs[p], a_frame.partMs[p]); }
			return i < kAlwaysLog || a_frame.intervalMs > kSlowIntervalMs || a_frame.AmfMs() > kSlowAmfMs;
		}

		// True once, on the frame that completes an opening's measurement: time for the summary line.
		bool SummaryDue() const { return m_measuring && m_index == kFrames; }
		const Summary& GetSummary() const { return m_summary; }
		int FrameIndex() const { return m_index; }
		int Openings() const { return m_openings; }

		// Which part took the most of a frame's AMF time.
		static int Heaviest(const Frame& a_frame)
		{
			int best = 0;
			for (int p = 1; p < kPartCount; ++p) { if (a_frame.partMs[p] > a_frame.partMs[best]) { best = p; } }
			return best;
		}

	private:
		bool    m_wasVisible = false;
		bool    m_measuring = false;
		int     m_index = 0;
		int     m_openings = 0;
		Summary m_summary{};
	};
}
