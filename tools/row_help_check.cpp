// Off-game check of the row link in include/ConsumerTooltip.h (2.4.0): a mod's "(?)" marker answers as hovered while the
// control on its row holds the keyboard / controller highlight or the mouse is on the row, so its hint reaches the help bar.
// Runs AMF's own Dear ImGui build headless, with real nav key events and real mouse events.
//   tools\row_help_check.bat
// Prints one line per check and "row_help_check: N of N passed"; exits 1 on any failure.
#include "../include/ConsumerTooltip.h"

#include <cstdarg>
#include <cstdio>
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

	std::vector<std::string> g_bar;   // what the help bar was handed this frame
	bool BarHook(const char* a_text)
	{
		g_bar.emplace_back(a_text);
		return true;
	}

	void Item(const char* a_fmt, ...)   // what igSetItemTooltip does (cimgui.cpp)
	{
		va_list args;
		va_start(args, a_fmt);
		consumer_tooltip::SetItemTooltipV(a_fmt, args);
		va_end(args);
	}

	// A mod's own pattern, exactly as Perfected Wheeler's AmfPage.cpp HelpMarker draws it.
	void Marker(const char* a_hint)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (consumer_tooltip::ItemHovered(ImGuiHoveredFlags_None)) { Item("%s", a_hint); }
	}

	void Row(const char* a_label, const char* a_hint)
	{
		ImGui::PushID(a_label);
		// a switch: an invisible button and its label after it, as widgets::Toggle draws them
		ImGui::InvisibleButton("##toggle", ImVec2(ImGui::GetFrameHeight() * 2.0f, ImGui::GetFrameHeight()));
		ImGui::SameLine();
		ImGui::TextUnformatted(a_label);
		if (a_hint) { Marker(a_hint); }
		ImGui::PopID();
	}

	void Page()
	{
		Row("first", "first hint");
		Row("second", "second hint");
		Row("no hint", nullptr);
		Row("fourth", "fourth hint");
		ImGui::Button("two A"); Marker("A hint");   // two controls, each with its marker, on one line
		ImGui::SameLine();
		ImGui::Button("two B"); Marker("B hint");
		ImGui::TextUnformatted("lead"); Marker("text only");   // no control on this row
	}

	std::vector<std::pair<ImGuiKey, bool>> g_keys;

	void Frame(const std::function<void()>& a_draw)
	{
		g_bar.clear();
		ImGuiIO& io = ImGui::GetIO();
		io.DeltaTime = 1.0f / 60.0f;
		for (const auto& [key, down] : g_keys) { io.AddKeyEvent(key, down); }
		g_keys.clear();
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(100, 100));
		ImGui::SetNextWindowSize(ImVec2(800, 600));
		ImGui::Begin("###amf-main", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		static bool s_focused = false;
		if (!s_focused) { ImGui::SetNextWindowFocus(); s_focused = true; }   // AMF focuses its content pane the same way
		ImGui::BeginChild("##content", ImVec2(0, 0), true);
		consumer_tooltip::LinkRows() = true;
		consumer_tooltip::LinkMouse() = true;
		a_draw();
		consumer_tooltip::LinkRows() = false;
		consumer_tooltip::LinkMouse() = false;
		ImGui::EndChild();
		ImGui::End();
		ImGui::Render();
	}

	void Press(ImGuiKey a_key)
	{
		g_keys.push_back({ a_key, true });
		Frame(Page);
		g_keys.push_back({ a_key, false });
		Frame(Page);
		Frame(Page);
	}

	void MoveMouse(float a_x, float a_y)
	{
		ImGui::GetIO().AddMousePosEvent(a_x, a_y);
		Frame(Page);
		Frame(Page);
	}

	bool BarIs(const char* a_text) { return g_bar.size() == 1 && g_bar[0] == a_text; }
	std::string Shown()
	{
		std::string s = "[";
		for (const auto& t : g_bar) { s += t + ";"; }
		return s + "]";
	}
}

int main()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(1920, 1080);
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	unsigned char* pixels = nullptr;
	int w = 0, h = 0;
	io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
	io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(1)));
	consumer_tooltip::Bar() = &BarHook;
	io.AddMousePosEvent(1500.0f, 900.0f);   // parked off the page: the first position after none counts as no movement

	Frame(Page);
	Frame(Page);
	Press(ImGuiKey_DownArrow);   // a first press lands the highlight on the first row
	Check(BarIs("first hint"), "nav: the first row's switch highlighted, its marker's hint reaches the bar " + Shown());
	Frame(Page);
	Frame(Page);
	Check(BarIs("first hint"), "nav: the hint stays while the row stays highlighted " + Shown());

	Press(ImGuiKey_DownArrow);
	Check(BarIs("second hint"), "nav: the next row, only its own hint " + Shown());

	Press(ImGuiKey_DownArrow);
	Check(g_bar.empty(), "nav: a row with no marker offers nothing (the bar then empties) " + Shown());

	Press(ImGuiKey_DownArrow);
	Check(BarIs("fourth hint"), "nav: past the row with no hint " + Shown());

	Press(ImGuiKey_DownArrow);
	Check(BarIs("A hint"), "nav: two controls on a line, the first highlighted: the first's marker has it " + Shown());

	Press(ImGuiKey_RightArrow);
	Check(BarIs("B hint"), "nav: two controls on a line, the second highlighted: the second's marker has it " + Shown());

	// ---- the marker's own hover, and the row's, with the mouse
	const ImGuiWindow* content = nullptr;
	for (const ImGuiWindow* window : ImGui::GetCurrentContext()->Windows)
	{
		if (std::string(window->Name).find("##content") != std::string::npos) { content = window; }
	}
	if (!content)
	{
		Check(false, "the page window was not found");
		return 1;
	}
	const float x0 = content->Pos.x + ImGui::GetStyle().WindowPadding.x;
	const float y0 = content->Pos.y + ImGui::GetStyle().WindowPadding.y;
	const float rowH = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;

	MoveMouse(x0 + 5.0f, y0 + rowH + 5.0f);
	Check(BarIs("second hint"), "mouse: on the second row's switch, its hint " + Shown());

	MoveMouse(x0 + 5.0f, y0 + rowH * 2.0f + 5.0f);
	Check(g_bar.empty(), "mouse: on a row with no marker, nothing " + Shown());

	MoveMouse(x0 + 700.0f, y0 + rowH + 5.0f);
	Check(g_bar.empty(), "mouse: on the same line far right of the marker, nothing " + Shown());

	MoveMouse(x0 + 5.0f, y0 + 5.0f);
	io.AddMouseButtonEvent(0, true);
	Frame(Page);
	Check(g_bar.empty() || BarIs("first hint"), "mouse: button down on the switch, the marker's text is not linked " + Shown());
	io.AddMouseButtonEvent(0, false);
	Frame(Page);
	Frame(Page);

	// ---- link off: Dear ImGui's own answers only (a mod's overlay window, outside its page)
	bool linked = false;
	Press(ImGuiKey_DownArrow);
	Frame([&] {
		ImGui::InvisibleButton("##x", ImVec2(40, 20));
		ImGui::SameLine();
		ImGui::TextUnformatted("(?)");
		consumer_tooltip::LinkRows() = false;
		linked = consumer_tooltip::ItemHovered(ImGuiHoveredFlags_None);
		consumer_tooltip::LinkRows() = true;
	});
	Check(!linked, "link off: a text item stays unhovered");

	ImGui::DestroyContext();
	std::cout << "row_help_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail ? 1 : 0;
}
