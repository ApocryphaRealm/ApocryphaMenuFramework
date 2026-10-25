// Off-game check of the Controls page at a big text size in a narrow window (AMF 2.3.0, retest batch B-20261025-053652-453d,
// c0a3 U1 FAIL: text size 2.0, window at its 1175 x 1080 floor - the "Controller" tab showed only "C" between tab arrows and
// the pane grew a horizontal scroll bar). Runs AMF's own ImGui build (1.90.8 with the overlay patches, the SE line's vcpkg
// tree) headless: a content pane as Renderer.cpp draws it (a child with a horizontal scroll bar allowed, its work rect held
// to the visible width), about 450 px wide, the font at 32 px (16 px x text size 2.0), the Controls page's intro, its two
// tabs and its Save / Reset button pair.
//   tools\controls_fit_check.bat   -> one line per check and "controls_fit_check: N of N passed"; exits 1 on any failure.
// What it proves:
//   1. the cause: drawn the old way (FittingPolicyScroll, full-size labels, the buttons on one line) the bar scrolls (its
//      tabs are wider than the bar) and the pane's content is wider than the pane - a horizontal scroll bar;
//   2. the fix (TabFit.h): the labels at tabfit::LabelScale fit - every tab at its full label width, all of them inside the
//      bar, no tab arrows, no horizontal scroll bar - and "Reset every control" starts its own line inside the pane;
//   3. at text size 1.0 in a wide pane nothing changes: scale 1, the buttons stay side by side.
// What it cannot: AMF's real fonts and icon glyphs, and the rest of the page (the bindings table is held inside the work
// rect by ImGui's table code) - the in-game retest covers those.
#include "../include/TabFit.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>
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

	// the labels as the page builds them: an icon glyph and two spaces, then the word (two letters stand in for the icon)
	const char* kKeyboardTab = "[] Keyboard and mouse";
	const char* kPadTab = "[] Controller";
	const char* kSave = "[] Save controls";
	const char* kReset = "[] Reset every control";

	struct Seen
	{
		bool  scrollbarX = false;
		float barWidth = 0.0f, allTabs = 0.0f, scale = 1.0f;
		bool  tabsWhole = false, arrows = false;
		float paneRight = 0.0f, resetRight = 0.0f, resetTop = 0.0f, saveTop = 0.0f;
	};

	void BuildFonts(float a_px)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();
		ImFontConfig cfg;
		cfg.SizePixels = a_px;
		io.Fonts->AddFontDefault(&cfg);
		unsigned char* pixels = nullptr;
		int w = 0, h = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
		io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(1)));
	}

	Seen Frame(bool a_fixed, float a_paneWidth)
	{
		Seen s;
		ImGuiIO& io = ImGui::GetIO();
		io.DeltaTime = 1.0f / 60.0f;
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(0, 0));
		ImGui::SetNextWindowSize(ImVec2(a_paneWidth + 40.0f, 1000.0f));
		ImGui::Begin("###amf-main", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration);
		ImGui::BeginChild("##content", ImVec2(a_paneWidth, 0.0f), ImGuiChildFlags_Border, ImGuiWindowFlags_HorizontalScrollbar);
		ImGuiWindow* pane = ImGui::GetCurrentWindow();
		pane->WorkRect.Max.x = (std::min)(pane->WorkRect.Max.x, pane->ContentRegionRect.Max.x);   // Renderer.cpp's hold
		pane->ParentWorkRect = pane->WorkRect;
		ImGui::TextUnformatted("Controls");
		ImGui::Separator();
		ImGui::TextWrapped("These are the framework's own controls - what moves through this menu and what opens and closes it.");
		const ImGuiStyle& st = ImGui::GetStyle();
		float scale = 1.0f;
		if (a_fixed)
		{
			const float widths[] = { ImGui::CalcTextSize(kKeyboardTab, nullptr, true).x, ImGui::CalcTextSize(kPadTab, nullptr, true).x };
			scale = tabfit::LabelScale(widths, 2, ImGui::GetContentRegionAvail().x, st.FramePadding.x, st.ItemInnerSpacing.x);
		}
		s.scale = scale;
		bool barOpen = false;
		{
			tabfit::ScopedScale scaled(scale);
			barOpen = ImGui::BeginTabBar("##controlstabs", a_fixed ? ImGuiTabBarFlags_FittingPolicyResizeDown : ImGuiTabBarFlags_FittingPolicyScroll);
		}
		if (barOpen)
		{
			ImGuiTabBar* bar = GImGui->CurrentTabBar;
			for (const char* label : { kKeyboardTab, kPadTab })
			{
				bool open = false;
				{
					tabfit::ScopedScale scaled(scale);
					open = ImGui::BeginTabItem(label);
				}
				if (open)
				{
					ImGui::TextWrapped("Open and close the menu - the one key that both opens and closes the menu.");
					ImGui::EndTabItem();
				}
			}
			if (bar)
			{
				s.barWidth = bar->BarRect.GetWidth();
				s.allTabs = bar->WidthAllTabs;
				s.tabsWhole = bar->Tabs.Size == 2;
				for (const ImGuiTabItem& t : bar->Tabs) { s.tabsWhole = s.tabsWhole && t.Width + 0.5f >= t.ContentWidth; }
				s.arrows = bar->WidthAllTabs > bar->BarRect.GetWidth() + 0.5f;   // FittingPolicyScroll draws its arrows then
			}
			ImGui::EndTabBar();
		}
		ImGui::Separator();
		ImGui::Button(kSave);
		s.saveTop = ImGui::GetItemRectMin().y;
		if (a_fixed) { tabfit::SameLineIfFits(tabfit::ButtonWidth(kReset)); }
		else { ImGui::SameLine(); }
		ImGui::Button(kReset);
		s.resetRight = ImGui::GetItemRectMax().x;
		s.resetTop = ImGui::GetItemRectMin().y;
		s.paneRight = pane->InnerClipRect.Max.x;
		s.scrollbarX = pane->ScrollbarX;
		ImGui::EndChild();
		ImGui::End();
		ImGui::Render();
		return s;
	}

	Seen Settle(bool a_fixed, float a_paneWidth)
	{
		Seen s;
		for (int i = 0; i < 8; ++i) { s = Frame(a_fixed, a_paneWidth); }   // the tab bar lays out a frame late, the scroll bar too
		return s;
	}
}

int main()
{
	// the pure function
	{
		const float w[] = { 300.0f, 150.0f };
		Check(tabfit::LabelScale(w, 2, 1000.0f, 8.0f, 8.0f) == 1.0f, "labels that fit are drawn at their own size");
		const float s = tabfit::LabelScale(w, 2, 450.0f, 8.0f, 8.0f);
		Check(s < 1.0f && s * 450.0f + 2 * 17.0f + 8.0f + 2.0f <= 450.0f + 0.01f, "labels that do not fit get the scale that fits them");
		Check(tabfit::LabelScale(w, 2, 100.0f, 8.0f, 8.0f) == tabfit::kMinScale, "never below the minimum scale");
		Check(tabfit::LabelScale(nullptr, 2, 100.0f, 8.0f, 8.0f) == 1.0f, "no labels: no scale");
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(1920, 1080);
	ImGui::GetStyle().ScaleAllSizes(2.0f);   // AMF scales the style with the text size
	BuildFonts(32.0f);                       // text size 2.0

	const float narrow = 470.0f;   // the content pane at the 1175 px window floor (frame 062851-B-U1-controls-2.0-floor-0637)
	{
		const Seen s = Settle(false, narrow);
		Check(s.arrows, "CAUSE: the old bar (FittingPolicyScroll, full-size labels) is wider than the pane - it scrolls (the 'C' with tab arrows)");
		Check(s.scrollbarX, "CAUSE: and the pane grows a horizontal scroll bar (the bar reports its full width; the buttons run past the edge)");
	}
	{
		const Seen s = Settle(true, narrow);
		char buf[200];
		std::snprintf(buf, sizeof(buf), " (scale %.2f, tabs %.0f of %.0f px)", s.scale, s.allTabs, s.barWidth);
		Check(s.scale < 1.0f && !s.arrows && s.allTabs <= s.barWidth + 0.5f, std::string("FIX: at text size 2.0 in the narrowest window both tabs fit the bar - no tab arrows") + buf);
		Check(s.tabsWhole, "FIX: each tab is as wide as its whole label - 'Keyboard and mouse' and 'Controller' wholly visible");
		Check(s.resetTop > s.saveTop && s.resetRight <= s.paneRight + 0.5f, "FIX: 'Reset every control' starts its own line, inside the pane");
		Check(!s.scrollbarX, "FIX: no horizontal scroll bar");
	}
	ImGui::GetStyle().ScaleAllSizes(0.5f);
	BuildFonts(16.0f);   // text size 1.0
	{
		const Seen s = Settle(true, 900.0f);
		Check(s.scale == 1.0f && !s.arrows && s.tabsWhole, "at text size 1.0 in a wide pane the tabs are drawn at their own size");
		Check(s.resetTop == s.saveTop && !s.scrollbarX, "and Save and Reset stay side by side");
	}

	ImGui::DestroyContext();
	std::printf("controls_fit_check: %d of %d passed\n", g_pass, g_pass + g_fail);
	return g_fail == 0 ? 0 : 1;
}
