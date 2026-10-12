#include "Performance.h"

#include "HostCore.h"
#include "Settings.h"
#include "Strings.h"
#include "Theme.h"
#include "utils/Logger.h"
#include "utils/ToggleSwitch.h"

#include <imgui.h>

#include <Windows.h>

#include <cstdio>
#include <mutex>
#include <string>

namespace perf
{
	namespace
	{
		std::mutex g_lock;   // the render thread writes, DevBench reads
		hostcore::FrameTimes g_times;
		LARGE_INTEGER g_freq{};
		LARGE_INTEGER g_last{};
		bool g_started = false;

		using strings::TR;

		hostcore::FrameTimes Copy()
		{
			std::scoped_lock l(g_lock);
			return g_times;
		}

		// The graph and its three numbers - shared by the page and the corner widget.
		void DrawGraph(const hostcore::FrameTimes& a_t, float a_width, float a_height)
		{
			const auto s = a_t.Compute();
			float values[hostcore::FrameTimes::kFrames] = {};
			a_t.Ordered(values);
			// the scale holds a 60 FPS frame and the slowest frame shown, so a steady line sits mid-graph
			const float top = std::max(s.maxMs * 1.15f, 20.0f);
			char overlay[64];
			std::snprintf(overlay, sizeof(overlay), "%.0f FPS  %.1f ms", s.fpsAvg, s.avgMs);
			ImGui::PlotLines("##frametimes", values, a_t.Count(), 0, overlay, 0.0f, top, ImVec2(a_width, a_height));
			ImGui::Text(TR("AMF_PerfFpsLine", "FPS - min %.0f, avg %.0f, max %.0f"), s.fpsMin, s.fpsAvg, s.fpsMax);
			ImGui::Text(TR("AMF_PerfMsLine", "Frame time - min %.1f ms, avg %.1f ms, max %.1f ms"), s.minMs, s.avgMs, s.maxMs);
		}
	}

	void NoteFrame()
	{
		LARGE_INTEGER now{};
		::QueryPerformanceCounter(&now);
		if (!g_started)
		{
			::QueryPerformanceFrequency(&g_freq);
			g_last = now;
			g_started = true;
			logger::debug("perf: frame timing started (QPC {} Hz)", g_freq.QuadPart);
			return;
		}
		const double ms = g_freq.QuadPart > 0 ? static_cast<double>(now.QuadPart - g_last.QuadPart) * 1000.0 / static_cast<double>(g_freq.QuadPart) : 0.0;
		g_last = now;
		// a frame over 2 s is a load screen or a pause of the whole process, not a frame time worth graphing
		if (ms > 0.0 && ms < 2000.0)
		{
			std::scoped_lock l(g_lock);
			g_times.Push(static_cast<float>(ms));
		}
	}

	void DrawPage()
	{
		auto& values = settings::Get();
		ImGui::TextUnformatted(TR("AMF_Performance", "Performance"));
		ImGui::Separator();
		ImGui::Spacing();
		const hostcore::FrameTimes t = Copy();
		DrawGraph(t, ImGui::GetContentRegionAvail().x, ImGui::GetFontSize() * 7.0f);
		ImGui::Spacing();
		if (widgets::Toggle(TR("AMF_PerfOverlay", "Show the graph in the corner of the screen"), &values.perfOverlay))
		{
			logger::info("settings page: performance corner graph -> {}", values.perfOverlay);
			settings::Save();
		}
		ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(TR("AMF_PerfHelp", "Measured by this menu itself: the time between the frames the game shows, over the "
			"last 240 frames. It is this menu's own measurement, not another tool's. The corner graph is drawn whether or not "
			"the menu is open, and never takes the mouse or the keys."));
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
	}

	void DrawOverlay()
	{
		if (!settings::Get().perfOverlay) { return; }
		const hostcore::FrameTimes t = Copy();
		const ImGuiIO& io = ImGui::GetIO();
		const float w = ImGui::GetFontSize() * 16.0f;
		const float pad = ImGui::GetFontSize() * 0.6f;
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - pad, pad), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
		ImGui::SetNextWindowBgAlpha(0.75f);
		constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
											ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus |
											ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking;
		if (ImGui::Begin("##amf_perf_overlay", nullptr, kFlags))
		{
			DrawGraph(t, w, ImGui::GetFontSize() * 3.0f);
		}
		ImGui::End();
	}

	std::string StateJson()
	{
		const auto s = Copy().Compute();
		char buf[384];
		std::snprintf(buf, sizeof(buf),
					  "{\"fps\":{\"min\":%.1f,\"avg\":%.1f,\"max\":%.1f},\"frameMs\":{\"min\":%.2f,\"avg\":%.2f,\"max\":%.2f},\"frames\":%d,\"overlay\":%s}",
					  s.fpsMin, s.fpsAvg, s.fpsMax, s.minMs, s.avgMs, s.maxMs, s.count, settings::Get().perfOverlay ? "true" : "false");
		return buf;
	}
}
