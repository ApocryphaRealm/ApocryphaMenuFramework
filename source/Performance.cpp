#include "Performance.h"

#include "HostCore.h"
#include "Settings.h"
#include "Strings.h"
#include "Theme.h"
#include "utils/Logger.h"
#include "utils/ToggleSwitch.h"

#include <imgui.h>

#include <Windows.h>

#include <algorithm>
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
		// v6: the corner (the v5 test had it over the minimap, top-right) - [Performance] iCorner
		{
			const char* corners[] = { TR("AMF_PerfCornerTL", "Top left"), TR("AMF_PerfCornerTR", "Top right"),
									  TR("AMF_PerfCornerBL", "Bottom left"), TR("AMF_PerfCornerBR", "Bottom right") };
			const int corner = std::clamp(values.perfCorner, 0, 3);
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
			if (theme::BeginComboTight(TR("AMF_PerfCorner", "Corner"), corners[corner]))
			{
				for (int i = 0; i < 4; ++i)
				{
					if (ImGui::Selectable(corners[i], i == corner) && i != corner)
					{
						values.perfCorner = i;
						logger::info("settings page: performance corner graph corner -> {}", i);
						settings::Save();
					}
				}
				ImGui::EndCombo();
			}
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
		// 2.2.0: drawn straight onto the FOREGROUND draw list, not as an ImGui window. As a window it sat under AMF's own
		// window, so with AMF open full-screen it showed in no corner (v5 and v6 runs). The foreground list is drawn after every
		// AMF window; the pointer (ArtHooks DrawCursor) and the curtain go on the same list LATER in the frame, so they stay
		// over it. A hosted menu (Open Animation Replacer through AMF-Host-OAR) is drawn by that mod's own ImGui after AMF's
		// frame - the v6 run saw OAR's window on top of AMF's - so the graph stays under a hosted menu. No input: nothing here
		// is an item.
		const hostcore::FrameTimes t = Copy();
		const auto s = t.Compute();
		float values[hostcore::FrameTimes::kFrames] = {};
		t.Ordered(values);
		const int n = t.Count();
		const ImGuiIO& io = ImGui::GetIO();
		const ImGuiStyle& st = ImGui::GetStyle();
		const float font = ImGui::GetFontSize();
		char overlay[64];
		std::snprintf(overlay, sizeof(overlay), "%.0f FPS  %.1f ms", s.fpsAvg, s.avgMs);
		char fpsLine[160];
		std::snprintf(fpsLine, sizeof(fpsLine), TR("AMF_PerfFpsLine", "FPS - min %.0f, avg %.0f, max %.0f"), s.fpsMin, s.fpsAvg, s.fpsMax);
		char msLine[160];
		std::snprintf(msLine, sizeof(msLine), TR("AMF_PerfMsLine", "Frame time - min %.1f ms, avg %.1f ms, max %.1f ms"), s.minMs, s.avgMs, s.maxMs);
		const ImVec2 fpsSize = ImGui::CalcTextSize(fpsLine);
		const ImVec2 msSize = ImGui::CalcTextSize(msLine);
		const float plotW = font * 16.0f;
		const float plotH = font * 3.0f;
		const float innerW = std::max({ plotW, fpsSize.x, msSize.x });
		const float innerH = plotH + st.ItemSpacing.y + fpsSize.y + st.ItemSpacing.y + msSize.y;
		const ImVec2 size(innerW + st.WindowPadding.x * 2.0f, innerH + st.WindowPadding.y * 2.0f);
		// 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right ([Performance] iCorner), kept inside the screen
		const auto at = hostcore::CornerPlace(settings::Get().perfCorner, io.DisplaySize.x, io.DisplaySize.y, size.x, size.y, font * 0.6f);
		const ImVec2 boxMin(at.x, at.y);
		const ImVec2 boxMax(at.x + size.x, at.y + size.y);

		ImDrawList* draw = ImGui::GetForegroundDrawList();
		ImVec4 bg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		bg.w = 0.75f;   // as the window's SetNextWindowBgAlpha(0.75f) was; GetColorU32 still applies the HUD opacity (style Alpha)
		draw->AddRectFilled(boxMin, boxMax, ImGui::GetColorU32(bg), st.WindowRounding);
		if (st.WindowBorderSize > 0.0f) { draw->AddRect(boxMin, boxMax, ImGui::GetColorU32(ImGuiCol_Border), st.WindowRounding, 0, st.WindowBorderSize); }
		// the plot: ImGui's PlotLines look - a frame, the line inside its padding, the FPS caption centred at its top
		const ImVec2 plotMin(boxMin.x + st.WindowPadding.x, boxMin.y + st.WindowPadding.y);
		const ImVec2 plotMax(plotMin.x + plotW, plotMin.y + plotH);
		draw->AddRectFilled(plotMin, plotMax, ImGui::GetColorU32(ImGuiCol_FrameBg), st.FrameRounding);
		const ImVec2 inMin(plotMin.x + st.FramePadding.x, plotMin.y + st.FramePadding.y);
		const ImVec2 inMax(plotMax.x - st.FramePadding.x, plotMax.y - st.FramePadding.y);
		if (n >= 2 && inMax.x > inMin.x && inMax.y > inMin.y)
		{
			// the scale holds a 60 FPS frame and the slowest frame shown, so a steady line sits mid-graph (as DrawGraph)
			const float top = std::max(s.maxMs * 1.15f, 20.0f);
			float xs[hostcore::FrameTimes::kFrames];
			float ys[hostcore::FrameTimes::kFrames];
			hostcore::GraphPoints(values, n, inMin.x, inMin.y, inMax.x - inMin.x, inMax.y - inMin.y, top, xs, ys);
			ImVec2 points[hostcore::FrameTimes::kFrames];
			for (int i = 0; i < n; ++i) { points[i] = ImVec2(xs[i], ys[i]); }
			draw->AddPolyline(points, n, ImGui::GetColorU32(ImGuiCol_PlotLines), ImDrawFlags_None, 1.0f);
		}
		const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
		const float overlayW = ImGui::CalcTextSize(overlay).x;
		draw->AddText(ImVec2(plotMin.x + (plotW - overlayW) * 0.5f, inMin.y), text, overlay);
		const float line1 = plotMax.y + st.ItemSpacing.y;
		draw->AddText(ImVec2(plotMin.x, line1), text, fpsLine);
		draw->AddText(ImVec2(plotMin.x, line1 + fpsSize.y + st.ItemSpacing.y), text, msLine);
	}

	std::string StateJson()
	{
		const auto s = Copy().Compute();
		char buf[384];
		std::snprintf(buf, sizeof(buf),
					  "{\"fps\":{\"min\":%.1f,\"avg\":%.1f,\"max\":%.1f},\"frameMs\":{\"min\":%.2f,\"avg\":%.2f,\"max\":%.2f},\"frames\":%d,\"overlay\":%s,\"corner\":%d}",
					  s.fpsMin, s.fpsAvg, s.fpsMax, s.minMs, s.avgMs, s.maxMs, s.count, settings::Get().perfOverlay ? "true" : "false",
					  settings::Get().perfCorner);
		return buf;
	}
}
