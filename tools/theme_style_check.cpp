// Off-game check that a theme switch leaves the same style a fresh load gives (include/ThemeStyle.h - the same function
// theme::Apply runs). The v3 test, 2026-10-12: after cycling themes, Norden - Black came back with tighter window padding
// until a relaunch.
//   tools\theme_style_check.bat
// Reads every shipped theme's fBorderThickness from dist\SKSE\Plugins\ApocryphaMenuFramework\themes, then at each display
// scale (1080p, 1440p, 1800p, 2160p): loads a theme fresh, switches through all of them and back, and compares the style -
// every size, every flag, not the colours, which the theme sets after - with a fresh load of the theme it ends on. The
// pre-v4 switch is run beside it as a control and must differ above 1080p (it is the reported fault).
// Prints one line per check and "theme_style_check: N of N passed"; exits 1 on any failure.
#include "../include/ThemeStyle.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
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

	struct Theme
	{
		std::string name;
		float thickness = 1.0f;
	};

	std::vector<Theme> ShippedThemes(const std::filesystem::path& a_dir)
	{
		std::vector<Theme> out;
		std::error_code ec;
		for (const auto& e : std::filesystem::directory_iterator(a_dir, ec))
		{
			if (e.path().extension() != ".ini") { continue; }
			Theme t;
			t.name = e.path().stem().string();
			std::ifstream in(e.path());
			std::string line;
			while (std::getline(in, line))
			{
				if (line.rfind("fBorderThickness=", 0) == 0) { t.thickness = std::stof(line.substr(17)); }
			}
			out.push_back(t);
		}
		return out;
	}

	constexpr float kFramePadding = 34.0f;   // Theme.cpp: knotwork::kCorner (26) + 8

	// ImGuiStyle with its colours and alpha blanked, so a byte compare covers every size and flag
	bool SameGeometry(ImGuiStyle a_a, ImGuiStyle a_b)
	{
		std::memset(a_a.Colors, 0, sizeof(a_a.Colors));
		std::memset(a_b.Colors, 0, sizeof(a_b.Colors));
		a_a.Alpha = a_b.Alpha = 1.0f;
		return std::memcmp(&a_a, &a_b, sizeof(ImGuiStyle)) == 0;
	}

	ImGuiStyle Fresh(float a_thickness, float a_scale)
	{
		ImGuiStyle s;
		themestyle::Build(s, a_thickness, kFramePadding, a_scale);
		return s;
	}

	// what a theme switch did before v4: these fields written over the live style, unscaled
	void OldSwitch(ImGuiStyle& a_s, float a_thickness)
	{
		a_s.WindowBorderSize = a_thickness;
		a_s.FrameBorderSize = std::max(1.0f, a_thickness);
		a_s.PopupBorderSize = a_thickness;
		a_s.ChildBorderSize = a_thickness;
		a_s.WindowPadding = ImVec2(kFramePadding, kFramePadding);
		a_s.TabBorderSize = std::max(1.0f, a_thickness);
		a_s.WindowRounding = 0.0f;
		a_s.FrameRounding = 0.0f;
	}
}

int main(int argc, char** argv)
{
	if (argc < 2) { std::cout << "usage: theme_style_check <themes folder>\n"; return 2; }
	const std::filesystem::path dir = argv[1];
	std::vector<Theme> themes = ShippedThemes(dir);
	Check(themes.size() == 9, "the 9 shipped themes are read from " + dir.string() + " (" + std::to_string(themes.size()) + ")");
	themes.insert(themes.begin(), Theme{ "skyrim (built in)", 1.0f });
	themes.push_back(Theme{ "a 2 px theme (made up)", 2.0f });   // a theme whose sizes differ, so a leak would show

	ImGui::CreateContext();
	for (const float scale : { 1.0f, 1440.0f / 1080.0f, 1800.0f / 1080.0f, 2.0f })
	{
		const std::string at = std::to_string(static_cast<int>(scale * 1080.0f + 0.5f)) + "p";
		// start-up: the style ImGui's context holds, built for the first theme
		ImGuiStyle& live = ImGui::GetStyle();
		live = ImGuiStyle();
		live.Alpha = 0.73f;   // the HUD opacity the frame loop set; Build keeps it
		themestyle::Build(live, themes[0].thickness, kFramePadding, scale);
		Check(live.Alpha == 0.73f, at + ": the game's opacity survives a rebuild");
		ImGuiStyle old = ImGuiStyle();
		OldSwitch(old, themes[0].thickness);
		old.ScaleAllSizes(scale);   // pre-v4 start-up: Apply, then ScaleAllSizes once

		bool allSame = true, oldDrifted = false;
		std::string firstDiff;
		for (std::size_t i = 1; i <= themes.size(); ++i)
		{
			const Theme& t = themes[i % themes.size()];   // all of them, then back to the first
			themestyle::Build(live, t.thickness, kFramePadding, scale);
			OldSwitch(old, t.thickness);
			if (!SameGeometry(live, Fresh(t.thickness, scale)))
			{
				allSame = false;
				if (firstDiff.empty()) { firstDiff = " (first differs at " + t.name + ")"; }
			}
			if (!SameGeometry(old, Fresh(t.thickness, scale))) { oldDrifted = true; }
		}
		Check(allSame, at + ": every switch, through all " + std::to_string(themes.size()) + " themes and back, equals a fresh load" + firstDiff);
		Check(live.WindowPadding.x == std::floor(kFramePadding * scale) && live.ItemSpacing.x == std::floor(8.0f * scale),
			  at + ": window padding " + std::to_string(static_cast<int>(live.WindowPadding.x)) + " and item spacing " +
				  std::to_string(static_cast<int>(live.ItemSpacing.x)) + " are the scaled figures");
		if (scale > 1.0f)
		{
			Check(oldDrifted && old.WindowPadding.x == kFramePadding,
				  at + ": control - the pre-v4 switch leaves padding " + std::to_string(static_cast<int>(old.WindowPadding.x)) + " beside scaled spacing (the reported fault)");
		}
	}
	ImGui::DestroyContext();

	std::cout << "theme_style_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
