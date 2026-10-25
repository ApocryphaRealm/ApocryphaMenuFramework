#pragma once

// How the pages AMF builds from MCM menus draw their own words (2.1.5, the owner, 2026-10-07): Font Awesome icons and
// the theme's text roles - labels in the text colour, values grey (TextDisabled), section headings in the heading colour,
// help and page notes in the help colour. Shared by McmLoader.cpp (MCM Helper menus) and McmScripts.cpp (script menus).

#include "AmfIcons.h"
#include "HelpBar.h"
#include "Settings.h"
#include "Theme.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>

#include <imgui.h>

namespace mcmstyle
{
	// A note about the page itself ("Drawn from ...", "Loading...", "Nothing on this page."): icon + text in the help colour,
	// wrapped to the pane.
	inline void PageNote(const char* a_icon, const std::string& a_text)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(((a_icon ? std::string(a_icon) + "  " : std::string()) + a_text).c_str());
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
	}

	// printf into a std::string, for a translated format string with one argument.
	inline std::string Fmt(const char* a_fmt, ...)
	{
		char buffer[1024]{};
		va_list args;
		va_start(args, a_fmt);
		std::vsnprintf(buffer, sizeof(buffer), a_fmt, args);
		va_end(args);
		return buffer;
	}

	// The player's spacing for a converted page's body (Settings > MCM menus > Look of converted pages): the column gap
	// and the extra row spacing. Colours are framework-wide (Appearance > Colours). Held for the body of one page.
	class PageScope
	{
	public:
		PageScope()
		{
			const ImGuiStyle& style = ImGui::GetStyle();
			const float extra = ImGui::GetFontSize() * static_cast<float>(settings::Get().mcmRowSpacing) / 100.0f;
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(style.ItemSpacing.x, style.ItemSpacing.y + extra));
			ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ColumnPadding(), style.CellPadding.y + extra * 0.5f));
		}
		~PageScope() { ImGui::PopStyleVar(2); }
		PageScope(const PageScope&) = delete;
		PageScope& operator=(const PageScope&) = delete;

		// Half the chosen column gap: a table cell pads each side, so two halves make the gap between the columns.
		static float ColumnPadding() { return ImGui::GetFontSize() * static_cast<float>(settings::Get().mcmColumnGap) / 200.0f; }
	};

	// ---- 2.2.4: responsive rows (the menu render matrix, 2026-10-22) -------------------------------------------------
	// From text size 2.00 converted pages cut their labels at the window's edge (MCM Helper: the control kept ImGui's 65%
	// of the row whatever the label needed; script menus: half the column) and drew a status value over its label. The
	// rule now: measure the label; the control gives up width for it, down to a minimum; below that the label goes on its
	// own line above the control. A row that fits draws exactly as before.

	// The visible width of a label ("##" onward is an ID, not text).
	inline float LabelWidth(const std::string& a_label)
	{
		return a_label.empty() ? 0.0f : ImGui::CalcTextSize(a_label.c_str(), nullptr, true).x;
	}

	struct RowFit
	{
		float width = 0.0f;     // the control's width
		bool stacked = false;   // true: the label goes on its own line above the control
	};

	// A control with its label to its right. a_minEm / a_maxEm: the control's narrowest and widest, in text heights
	// (a_maxEm 0 = no cap beyond ImGui's default width for the row).
	inline RowFit FitLabelRight(const std::string& a_label, float a_minEm, float a_maxEm = 0.0f)
	{
		const float avail = ImGui::GetContentRegionAvail().x;
		const float font = ImGui::GetFontSize();
		float natural = ImGui::CalcItemWidth();   // what ImGui would give it (65% of the row unless pushed)
		if (a_maxEm > 0.0f) { natural = std::min(natural, font * a_maxEm); }
		const float least = std::min(font * a_minEm, avail);
		natural = std::max(natural, least);
		const float label = LabelWidth(a_label);
		if (label <= 0.0f) { return { std::min(natural, avail), false }; }
		const float room = avail - label - ImGui::GetStyle().ItemInnerSpacing.x;
		if (room >= least) { return { std::min(natural, room), false }; }
		return { std::min(natural, avail), true };
	}

	// The label on its own line, wrapped to the row (the stacked form of a row).
	inline void LabelAbove(const std::string& a_label)
	{
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(a_label.c_str(), nullptr);
		ImGui::PopTextWrapPos();
	}

	// A label after a control on the same line, wrapped inside the row instead of running past its edge.
	inline void LabelAfter(const std::string& a_label)
	{
		ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(a_label.c_str(), nullptr);
		ImGui::PopTextWrapPos();
	}

	// The "##..." ID part of an ImGui label, to draw a control without its built-in label (the stacked form).
	inline std::string HiddenLabel(const std::string& a_imguiId)
	{
		const auto at = a_imguiId.find("##");
		return at == std::string::npos ? "##" + a_imguiId : a_imguiId.substr(at);
	}

	// SameLine only when an item of a_width still fits whole on this line; otherwise the next item starts a new line.
	inline void SameLineIfFits(float a_width)
	{
		const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;   // before SameLine: the row's end
		if (ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + a_width <= right) { ImGui::SameLine(); }
	}

	// A section heading in the theme's heading colour.
	inline void Heading(const std::string& a_label)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, theme::HeaderTextColor());
		ImGui::SeparatorText(a_label.empty() ? " " : a_label.c_str());
		ImGui::PopStyleColor();
	}
}
