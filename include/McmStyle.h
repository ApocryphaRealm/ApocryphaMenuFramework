#pragma once

// How the pages AMF builds from MCM menus draw their own words (2.1.5, the owner, 2026-10-07): Font Awesome icons and
// the theme's text roles - labels in the text colour, values grey (TextDisabled), section headings in the heading colour,
// help and page notes in the help colour. Shared by McmLoader.cpp (MCM Helper menus) and McmScripts.cpp (script menus).

#include "AmfIcons.h"
#include "HelpBar.h"
#include "Settings.h"
#include "Theme.h"

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
		ImGui::TextUnformatted((std::string(a_icon) + "  " + a_text).c_str());
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

	// A section heading in the theme's heading colour.
	inline void Heading(const std::string& a_label)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, theme::HeaderTextColor());
		ImGui::SeparatorText(a_label.empty() ? " " : a_label.c_str());
		ImGui::PopStyleColor();
	}
}
