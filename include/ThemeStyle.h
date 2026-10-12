#pragma once

// ============================================================================================
// THE STYLE A THEME SETS, BUILT WHOLE (next after 2.1.9, v4; the v3 test: after cycling themes, Norden - Black came back
// with tighter window padding, and a relaunch fixed it). Only ImGui here, so the offline check
// (tools\theme_style_check.cpp) runs the same function theme::Apply does.
//
// Start-up ran theme::Apply (WindowPadding 34) and THEN ImGuiStyle::ScaleAllSizes(UI scale) once - 56 px at 1800p. Every
// later theme switch ran Apply again, which wrote the padding back as 34 unscaled (and the rounding as 0) over a style
// whose other sizes stayed scaled: the window padding shrank to the 1080p figure on any display taller than 1080, and
// stayed so until a relaunch. A switch now builds the WHOLE style from ImGui's own defaults - the base a fresh load starts
// from - sets the theme's sizes, scales once, and so equals a fresh load whichever theme came before.
// ============================================================================================

#include <imgui.h>

#include <algorithm>

namespace themestyle
{
	// Control outlines are never thinner than one pixel (theme::Apply's outline rule).
	inline constexpr float kMinOutlineWidth = 1.0f;

	// a_style: rebuilt in place (everything but Colors and Alpha is replaced; Colors are the theme's to set after this,
	// Alpha is the game's HUD opacity, set every frame). a_borderThickness: the theme's line width. a_framePadding: the
	// window padding at 1080p. a_uiScale: the display's scale (1 = 1080p), the one ScaleAllSizes takes.
	inline void Build(ImGuiStyle& a_style, float a_borderThickness, float a_framePadding, float a_uiScale)
	{
		ImVec4 colors[ImGuiCol_COUNT];
		std::copy(std::begin(a_style.Colors), std::end(a_style.Colors), std::begin(colors));
		const float alpha = a_style.Alpha;

		a_style = ImGuiStyle();   // ImGui's defaults: what CreateContext gave a fresh load

		a_style.WindowBorderSize = a_borderThickness;
		a_style.FrameBorderSize = std::max(kMinOutlineWidth, a_borderThickness);   // every control keeps an outline
		a_style.PopupBorderSize = a_borderThickness;
		a_style.ChildBorderSize = a_borderThickness;
		a_style.TabBorderSize = std::max(kMinOutlineWidth, a_borderThickness);
		a_style.WindowPadding = ImVec2(a_framePadding, a_framePadding);
		a_style.WindowRounding = 0.0f;
		a_style.FrameRounding = 0.0f;
		a_style.ScaleAllSizes(a_uiScale);

		std::copy(std::begin(colors), std::end(colors), std::begin(a_style.Colors));
		a_style.Alpha = alpha;
	}
}
