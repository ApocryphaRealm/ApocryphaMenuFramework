#pragma once

// ============================================================================================================
// Rows that must fit the pane's width at big text sizes (2.3.0, retest batch B-20261025-053652-453d, c0a3 U1: at text
// size 2.0 with the window at its narrowest, 1175 x 1080, the Controls page's "Controller" tab showed only "C" between
// tab arrows, and the pane grew a horizontal scroll bar).
//
// Why the scroll bar: an ImGui tab bar with FittingPolicyScroll keeps every tab at its full width and reports that full
// width to the window (imgui_widgets.cpp TabBarLayout: ItemSize(WidthAllTabs, ...)), so two tabs wider than the pane make
// the pane's content wider than the pane - the scroll bar - and the bar itself scrolls ("C" and the arrows). The
// "Save controls" / "Reset every control" pair on one line overflowed the same way.
//
// Now: the tab labels are drawn at a font scale that makes them fit (never below kMinScale; past that the tabs shrink,
// ImGui's FittingPolicyResizeDown, so nothing reports more width than the pane), and a button that would run past the
// edge starts its own line. Proven off-game by tools\controls_fit_check.bat.
// ============================================================================================================

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace tabfit
{
	inline constexpr float kMinScale = 0.6f;

	// The font scale at which tabs with these label widths (CalcTextSize at the current font) fit side by side in a_avail
	// pixels: 1 when they already fit, never below a_min. A tab is its label plus FramePadding.x on each side plus one pixel
	// (ImGui 1.90.8 TabItemCalcSize, no close button); neighbouring tabs sit ItemInnerSpacing.x apart (TabBarLayout).
	inline float LabelScale(const float* a_labelWidths, int a_count, float a_avail, float a_framePadX, float a_innerSpacingX,
		float a_min = kMinScale)
	{
		if (!a_labelWidths || a_count <= 0) return 1.0f;
		float labels = 0.0f;
		for (int i = 0; i < a_count; ++i) labels += (std::max)(0.0f, a_labelWidths[i]);
		const float fixed = static_cast<float>(a_count) * (a_framePadX * 2.0f + 1.0f) +
							static_cast<float>(a_count - 1) * a_innerSpacingX + 2.0f;   // + 2 px: rounding of the scaled glyphs
		if (labels <= 0.0f || labels + fixed <= a_avail) return 1.0f;
		return std::clamp((a_avail - fixed) / labels, a_min, 1.0f);
	}

	// Multiplies the current window's font scale by a_scale for its lifetime (the tab bar and each tab are drawn inside one;
	// a tab's contents are drawn after it, at the normal size). A scale of 1 changes nothing.
	class ScopedScale
	{
	public:
		explicit ScopedScale(float a_scale)
		{
			_window = ImGui::GetCurrentWindow();
			if (!_window || a_scale >= 1.0f) { _window = nullptr; return; }
			_previous = _window->FontWindowScale;
			ImGui::SetWindowFontScale(_previous * a_scale);
		}
		~ScopedScale()
		{
			if (_window && ImGui::GetCurrentWindow() == _window) { ImGui::SetWindowFontScale(_previous); }
		}
		ScopedScale(const ScopedScale&) = delete;
		ScopedScale& operator=(const ScopedScale&) = delete;

	private:
		ImGuiWindow* _window = nullptr;
		float        _previous = 1.0f;
	};

	// Puts the next item on the same line when an item a_width wide still fits there; otherwise it starts the next line.
	inline void SameLineIfFits(float a_width)
	{
		ImGui::SameLine();
		if (ImGui::GetContentRegionAvail().x < a_width) { ImGui::NewLine(); }
	}

	// A button's width for its label at the current font (ImGui's Button: the label plus FramePadding.x on each side).
	inline float ButtonWidth(const char* a_label)
	{
		if (!a_label) return 0.0f;
		return ImGui::CalcTextSize(a_label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
	}
}
