#include "HelpBar.h"

#include "AmfIcons.h"
#include "Settings.h"
#include "Theme.h"
#include "ConsumerTooltip.h"
#include "utils/Logger.h"

#include <algorithm>
#include <imgui.h>

// See HelpBar.h. 2.1.5.

namespace helpbar
{
	namespace
	{
		int g_wantFrame = -10;   // the last frame a page asked for the bar
		std::string g_page;      // that page's name: a different page starts with an empty bar
		std::string g_text;      // the help on show (the last highlighted item's, as SkyUI's info line keeps it)
		float g_paneMinX = 0.0f, g_paneMinY = 0.0f, g_paneMaxX = 0.0f, g_paneMaxY = 0.0f;   // the right pane, last frame
		constexpr int kLines = 3;  // the bar's height in text lines; longer help scrolls inside it
		bool g_inModPage = false;  // a registered mod page is drawing (Renderer): its tooltips may go to the bar

		// consumer_tooltip's hook: a mod's plain-text tooltip. Taken (true) only while a mod page draws in the content pane
		// with the help bar on and the bar present; otherwise the caller draws its wrapped popup.
		bool TakeConsumerTooltip(const char* a_text)
		{
			if (!g_inModPage || !a_text || !*a_text) { return false; }
			if (!settings::Get().helpBar || g_wantFrame < ImGui::GetFrameCount() - 1) { return false; }
			if (g_text != a_text)
			{
				g_text = a_text;
				logger::debug("help bar: {} character(s) from a mod's tooltip", g_text.size());
			}
			return true;
		}

		[[maybe_unused]] const bool g_hookInstalled = (consumer_tooltip::Bar() = &TakeConsumerTooltip, true);
	}

	void EnterModPage()
	{
		g_inModPage = true;
	}

	bool InModPage()
	{
		return g_inModPage;
	}

	void LeaveModPage(const std::string& a_pageKey)
	{
		g_inModPage = false;
		// A page that asked for the bar itself keeps its own name; any other mod page asks here, under its own key.
		if (g_wantFrame != ImGui::GetFrameCount()) { Want(a_pageKey); }
	}

	void Want(const std::string& a_page)
	{
		if (a_page != g_page)
		{
			g_page = a_page;
			g_text.clear();
		}
		g_wantFrame = ImGui::GetFrameCount();
	}

	bool Active()
	{
		return settings::Get().helpBar && g_wantFrame >= ImGui::GetFrameCount() - 1;
	}

	float Height()
	{
		return ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(kLines) + ImGui::GetStyle().WindowPadding.y * 2.0f;
	}

	void NotePane(float a_minX, float a_minY, float a_maxX, float a_maxY)
	{
		g_paneMinX = a_minX;
		g_paneMinY = a_minY;
		g_paneMaxX = a_maxX;
		g_paneMaxY = a_maxY;
	}

	void OfferForLastItem(const std::string& a_help)
	{
		if (a_help.empty()) { return; }
		if (!ImGui::IsItemHovered() && !ImGui::IsItemFocused()) { return; }
		if (settings::Get().helpBar)
		{
			if (g_text != a_help)
			{
				g_text = a_help;
				logger::debug("help bar: {} character(s) for the highlighted option", g_text.size());
			}
			return;
		}
		// The popup form: wrapped to the right pane's width and placed inside it, under the item - never past the pane's
		// edges the way ImGui's mouse-following tooltip ran off the window (the owner's Atlas screenshot, 2026-10-07).
		const float paneW = g_paneMaxX - g_paneMinX;
		if (paneW <= 0.0f) { ImGui::SetTooltip("%s", a_help.c_str()); return; }
		const ImGuiStyle& style = ImGui::GetStyle();
		const float maxW = std::max(paneW - style.WindowPadding.x, ImGui::GetFontSize() * 8.0f);
		const float y = std::clamp(ImGui::GetItemRectMax().y + style.ItemSpacing.y, g_paneMinY, g_paneMaxY);
		ImGui::SetNextWindowPos(ImVec2(g_paneMinX + style.WindowPadding.x * 0.5f, y));
		ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(maxW, g_paneMaxY - g_paneMinY));
		if (ImGui::BeginTooltip())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			ImGui::PushTextWrapPos(maxW - style.WindowPadding.x * 2.0f);
			ImGui::TextUnformatted((std::string(icons::kInfo) + "  " + a_help).c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			ImGui::EndTooltip();
		}
	}

	void Draw(float a_width)
	{
		// NoNav: the D-pad never walks into the bar - it only reports what the highlight is on.
		ImGui::BeginChild("##helpbar", ImVec2(a_width, Height()), true, ImGuiWindowFlags_NoNav);
		if (!g_text.empty())
		{
			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			ImGui::PushTextWrapPos(0.0f);   // wrap at the bar's own width
			ImGui::TextUnformatted((std::string(icons::kInfo) + "  " + g_text).c_str());
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}
		ImGui::EndChild();
	}
}
