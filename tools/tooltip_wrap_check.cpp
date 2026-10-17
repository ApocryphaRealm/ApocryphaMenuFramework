// Off-game check of include/ConsumerTooltip.h (2.2.2): a mod's tooltip, drawn through AMF's tooltip exports, wraps to the
// window it is drawn in instead of running off the screen on one line (BanMarwein's Skyrim Performance Solution report).
// Runs AMF's own Dear ImGui build headless (1.90.8 with AMF's CJK line-break patch) and measures the tooltip windows.
//   tools\tooltip_wrap_check.bat
// Prints one line per check and "tooltip_wrap_check: N of N passed"; exits 1 on any failure.
#include "../include/ConsumerTooltip.h"

#include <cstdarg>
#include <cstdio>
#include <functional>
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

	void Tip(const char* a_fmt, ...)   // what igSetTooltip does (cimgui.cpp)
	{
		va_list args;
		va_start(args, a_fmt);
		consumer_tooltip::SetTooltipV(a_fmt, args);
		va_end(args);
	}

	constexpr float kHostW = 800.0f;

	// Three frames of a host window ("AMF's window", 800 px wide) whose page draws a tooltip; returns the tooltip window.
	const ImGuiWindow* Run(const std::function<void()>& a_draw)
	{
		const ImGuiWindow* tip = nullptr;
		for (int f = 0; f < 3; ++f)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.DeltaTime = 1.0f / 60.0f;
			io.AddMousePosEvent(300.0f, 300.0f);
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(100, 100));
			ImGui::SetNextWindowSize(ImVec2(kHostW, 600));
			ImGui::Begin("###amf-main", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
			ImGui::BeginChild("##content", ImVec2(0, 0), true);   // the page is drawn in AMF's content pane
			a_draw();
			ImGui::EndChild();
			ImGui::End();
			ImGui::Render();
			const ImGuiContext& g = *ImGui::GetCurrentContext();
			tip = nullptr;
			for (const ImGuiWindow* w : g.Windows)
			{
				if ((w->Flags & ImGuiWindowFlags_Tooltip) && w->LastFrameActive == g.FrameCount) { tip = w; }
			}
		}
		return tip;
	}

	float Limit()   // the widest a wrapped tooltip may be: the host's width less its padding, plus the tooltip's own padding
	{
		const ImGuiStyle& s = ImGui::GetStyle();
		return kHostW - s.WindowPadding.x * 2.0f + s.WindowPadding.x * 2.0f + 2.0f;
	}

	float Lines(const ImGuiWindow* a_tip) { return a_tip ? a_tip->ContentSize.y / ImGui::GetTextLineHeight() : 0.0f; }
}

int main()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(1920, 1080);
	io.IniFilename = nullptr;
	io.ConfigInputTrickleEventQueue = false;
	ImFontConfig cfg;
	cfg.SizePixels = 16.0f;
	io.Fonts->AddFontDefault(&cfg);
	unsigned char* pixels = nullptr;
	int tw = 0, th = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &tw, &th);
	io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(1)));

	const std::string english =
		"Caches far shadows between frames instead of drawing them every frame, which cuts the cost of the distant shadow "
		"cascade a great deal in open areas; it is a beta feature, may show stale shadows for a moment after a fast turn or "
		"a time-of-day jump, and helps most on systems limited by draw calls rather than by the graphics card itself.";
	std::string cjk;
	for (int i = 0; i < 120; ++i) { cjk += "\xE5\xBD\xB1"; }   // 影 (shadow) x 120, no spaces - breaks between characters
	const std::string word(400, 'x');                         // one word wider than the whole line

	// the control: Dear ImGui's own SetTooltip does not wrap - the test measures something real
	{
		const auto* tip = Run([&] { ImGui::SetTooltip("%s", english.c_str()); });
		Check(tip && tip->Size.x > Limit(), "control: ImGui's own SetTooltip runs one line wider than the window (" +
			  std::to_string(tip ? static_cast<int>(tip->Size.x) : 0) + " px)");
	}
	{
		const auto* tip = Run([&] { Tip("%s", english.c_str()); });
		Check(tip && tip->Size.x <= Limit(), "long English description: the tooltip fits the window's width (" +
			  std::to_string(tip ? static_cast<int>(tip->Size.x) : 0) + " px, limit " + std::to_string(static_cast<int>(Limit())) + ")");
		Check(Lines(tip) >= 3.0f, "long English description: wrapped onto " + std::to_string(static_cast<int>(Lines(tip) + 0.5f)) + " lines");
	}
	{
		const auto* tip = Run([&] { Tip("%s", cjk.c_str()); });
		Check(tip && tip->Size.x <= Limit(), "120 Chinese characters with no space: fits the window's width");
		Check(Lines(tip) >= 2.0f, "120 Chinese characters: broken between characters onto several lines");
	}
	{
		const auto* tip = Run([&] { Tip("%s", word.c_str()); });
		Check(tip && tip->Size.x <= Limit(), "a single 400-letter word: broken inside the word, within the window's width");
	}
	{
		// the BeginTooltip / text / EndTooltip form, and BeginItemTooltip over a hovered item
		const auto* tip = Run([&] {
			if (consumer_tooltip::BeginTooltip()) { ImGui::TextUnformatted(english.c_str()); consumer_tooltip::EndTooltip(); }
		});
		Check(tip && tip->Size.x <= Limit() && Lines(tip) >= 3.0f, "BeginTooltip + text + EndTooltip: wrapped to the window");
		Check(consumer_tooltip::Stack().empty(), "BeginTooltip / EndTooltip leave no wrap behind");
	}
	{
		const auto* tip = Run([&] { Tip("%s", "Short help."); });
		Check(tip && Lines(tip) < 1.5f && tip->Size.x < 200.0f, "a short help text stays one short line");
	}
	{
		// a page inside a narrow window of the mod's own: the tooltip follows that window, but never under 12 characters
		float wrap = 0.0f;
		for (int f = 0; f < 2; ++f)
		{
			io.DeltaTime = 1.0f / 60.0f;
			ImGui::NewFrame();
			ImGui::SetNextWindowSize(ImVec2(120, 200));
			ImGui::Begin("narrow", nullptr, ImGuiWindowFlags_NoSavedSettings);
			wrap = consumer_tooltip::WrapWidth();
			ImGui::End();
			ImGui::Render();
		}
		Check(wrap >= ImGui::GetFontSize() * 12.0f - 0.5f, "a very narrow host window still wraps at a dozen characters or more");
	}

	{
		// the help bar hook: a taker keeps the text out of the popup and receives it formatted; a decliner leaves the popup
		static std::string taken;
		static bool take = true;
		consumer_tooltip::Bar() = [](const char* a_text) { taken = a_text; return take; };
		taken.clear();
		take = true;
		const auto* tip = Run([&] { Tip("%s: %d", "Far shadow cache", 7); });
		Check(!tip && taken == "Far shadow cache: 7", "hook takes a plain tooltip: no popup, text formatted for the bar");
		take = false;
		tip = Run([&] { Tip("%s", "Declined."); });
		Check(tip != nullptr, "hook declines: the wrapped popup is drawn as before");
		tip = Run([&] {
			if (consumer_tooltip::BeginTooltip()) { ImGui::TextUnformatted("widgets"); consumer_tooltip::EndTooltip(); }
		});
		Check(tip != nullptr, "a BeginTooltip form is never offered to the bar");
		consumer_tooltip::Bar() = nullptr;
	}

	ImGui::DestroyContext();
	std::cout << "tooltip_wrap_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail ? 1 : 0;
}
