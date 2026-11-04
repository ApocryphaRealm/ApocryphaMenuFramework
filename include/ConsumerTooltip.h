#pragma once

// ============================================================================================
// A MOD'S OWN TOOLTIPS ARE WRAPPED (2.2.2). BanMarwein (Discord, 2026-10-14): on Skyrim Performance Solution's page the
// description of "Far shadow cache (BETA)" ran off the right edge of the screen on one line, wider than AMF's window. A mod
// built for SKSE Menu Framework draws its descriptions itself, through the ImGui calls it imports by name (igSetTooltip,
// igSetItemTooltip, igBeginTooltip / igBeginItemTooltip + text); Dear ImGui sizes a tooltip to its text and never wraps it,
// so a long description is one line as wide as the text. AMF's own help (the help bar and its popup form, HelpBar.cpp)
// already wraps; these exported calls now wrap the same way.
//
// THE WIDTH: the window the mod is drawing in (AMF's window - the root of the page's child - or a window of the mod's own),
// less its padding, never wider than the screen and never narrower than a dozen characters. Wrapping goes through Dear
// ImGui's own word wrap, which AMF's patch (cmake/ports/imgui/amf-cjk-wrap.patch, 2.1.8) also breaks between Chinese and
// Japanese characters, and Dear ImGui breaks inside a word that is wider than the whole line on its own.
// Header-only, so tools\tooltip_wrap_check.cpp runs this exact code against AMF's ImGui build.
// ============================================================================================

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace consumer_tooltip
{
	// 2.2.2: the help bar. AMF sets this once; a plain-text tooltip (SetTooltip / SetItemTooltip) is offered to it first, and
	// when it takes the text (the help bar is on and the mod is drawing inside AMF's page pane) no popup is drawn - the text
	// shows in the bar under the right pane, as a converted MCM page's help does. Null (the offline check) = always a popup.
	// Tooltips built from widgets (BeginTooltip ... EndTooltip) stay popups: a bar holds text only.
	using BarHook = bool (*)(const char* a_text);
	inline BarHook& Bar()
	{
		static BarHook s_hook = nullptr;
		return s_hook;
	}

	inline bool OfferToBar(const char* a_fmt, va_list a_args)
	{
		if (!Bar()) { return false; }
		va_list copy;
		va_copy(copy, a_args);
		const int n = std::vsnprintf(nullptr, 0, a_fmt, copy);
		va_end(copy);
		if (n <= 0) { return false; }
		std::string text(static_cast<std::size_t>(n) + 1, '\0');
		std::vsnprintf(text.data(), text.size(), a_fmt, a_args);
		text.resize(static_cast<std::size_t>(n));
		return Bar()(text.c_str());
	}

	// 2.4.0 THE ROW IS ONE THING (the owner, on Perfected Wheeler's page: the hint "doesn't show ... unless you hover over the
	// question mark, which you can't do with controller ... linking it to its parent"). The usual help marker is
	//     ImGui::SameLine(); ImGui::TextDisabled("(?)"); if (ImGui::IsItemHovered()) ImGui::SetTooltip(...);
	// Plain text has no id, so the D-pad can never land on it and Dear ImGui's IsItemHovered is false for it the moment the
	// controller is in use. A mod's control and its marker are one row: while the highlight is on a control earlier on the
	// same line, a text item answers as hovered, through the igIsItemHovered / tooltip exports, so the mod's own code runs
	// unchanged. Nothing in the mod has to know.
	//   - The highlight's rect is Dear ImGui's own (the nav window's NavRectRel), refreshed when the highlighted item is
	//     submitted; NavIdIsAlive says that has happened this frame, which is what "earlier on the line" means.
	//   - A marker drawn before its control is not linked. Two markers on a row: the first after the highlight has it, and
	//     keeps it for the second question about the same item (IsItemHovered, then SetItemTooltip). A row whose first
	//     control has no marker but whose second does would show the second's hint on the first: not told apart.
	//   - The mouse (a_mouseToo): the text answers while the pointer is on its line at or left of it, with the same one-owner
	//     rule. Never while a mouse button is down or just let go, so a mod's `IsItemHovered() && IsMouseClicked()` on a text
	//     item fires only for a click on the text itself.
	// Only ever adds answers for an item whose own answer was no.
	inline bool& LinkRows()    // AMF: a mod's page is drawing (HelpBar's EnterModPage / LeaveModPage). Off: Dear ImGui's answers only.
	{
		static bool s_on = false;
		return s_on;
	}

	inline bool& LinkMouse()   // AMF: the help bar is showing, so a hover ends in the bar and not in a popup over the row
	{
		static bool s_on = false;
		return s_on;
	}

	inline bool RowHighlighted(ImGuiHoveredFlags a_flags, bool a_mouseToo)
	{
		struct Owner
		{
			int frame = -1;
			ImVec2 at;
		};
		static Owner s_owner;

		ImGuiContext* g = ImGui::GetCurrentContext();
		if (!g) { return false; }
		ImGuiWindow* window = g->CurrentWindow;
		if (!window || window->SkipItems) { return false; }
		const ImGuiLastItemData& item = g->LastItemData;
		if ((item.InFlags & ImGuiItemFlags_Disabled) != 0 && (a_flags & ImGuiHoveredFlags_AllowWhenDisabled) == 0) { return false; }

		const bool taken = s_owner.frame == g->FrameCount;
		if (taken && s_owner.at.x == item.Rect.Min.x && s_owner.at.y == item.Rect.Min.y) { return true; }   // the same item, asked again

		// The line the item sits on: a marker's text is shorter than the switch beside it.
		const float lineTop = (std::min)(item.Rect.Min.y, window->DC.CursorPosPrevLine.y);
		const float lineBottom = (std::max)(item.Rect.Max.y, window->DC.CursorPosPrevLine.y + window->DC.PrevLineSize.y);

		const bool mouseHere = ImGui::IsWindowHovered();
		bool on = false;
		if (!g->NavDisableHighlight && (g->NavDisableMouseHover || !mouseHere) && g->NavId != 0 && g->NavIdIsAlive && g->NavWindow == window)
		{
			const ImRect nav = ImGui::WindowRectRelToAbs(window, window->NavRectRel[g->NavLayer]);
			if (item.ID != 0)
			{
				// A group whose last item has an id: the group holds the highlight when the highlighted item is inside it.
				ImRect around = item.Rect;
				around.Expand(1.0f);
				on = item.ID == g->NavId || around.Contains(nav);
			}
			else
			{
				on = !taken && nav.Max.y - 1.0f > lineTop && nav.Min.y + 1.0f < lineBottom;
			}
		}
		else if (a_mouseToo && !taken && item.ID == 0 && !g->NavDisableMouseHover && mouseHere && !ImGui::IsAnyMouseDown())
		{
			bool justLetGo = false;
			for (const bool released : g->IO.MouseReleased) { justLetGo = justLetGo || released; }
			const ImVec2 mouse = g->IO.MousePos;
			on = !justLetGo && mouse.y >= lineTop && mouse.y < lineBottom && mouse.x <= item.Rect.Max.x;
		}
		if (on)
		{
			s_owner.frame = g->FrameCount;
			s_owner.at = item.Rect.Min;
		}
		return on;
	}

	// What igIsItemHovered answers a mod: Dear ImGui's answer, or the row link while a mod's page draws.
	inline bool ItemHovered(ImGuiHoveredFlags a_flags)
	{
		if (ImGui::IsItemHovered(a_flags)) { return true; }
		if (!LinkRows() || (a_flags & ImGuiHoveredFlags_NoNavOverride) != 0) { return false; }
		return RowHighlighted(a_flags, LinkMouse());
	}

	// The wrap width for a tooltip opened now (before the tooltip window begins).
	inline float WrapWidth()
	{
		const ImGuiContext* g = ImGui::GetCurrentContext();
		if (!g) { return 0.0f; }
		const ImGuiStyle& style = g->Style;
		const float screenW = g->IO.DisplaySize.x;
		float hostW = screenW;
		if (const ImGuiWindow* w = g->CurrentWindow; w && w->RootWindow) { hostW = w->RootWindow->Size.x; }
		const float minW = g->FontSize * 12.0f;
		const float maxW = std::max(minW, screenW - style.WindowPadding.x * 4.0f);
		return std::clamp(hostW - style.WindowPadding.x * 2.0f, minW, maxW);
	}

	// One wrap pushed per tooltip opened through BeginTooltip / BeginItemTooltip, popped by its EndTooltip. (A tooltip
	// that failed to open pushes nothing; the mod then does not call EndTooltip for it, as ImGui requires.)
	// Kept by tooltip window, so an EndTooltip for a tooltip opened some other way (igBeginTooltipEx) pops nothing.
	inline std::vector<const ImGuiWindow*>& Stack()
	{
		static std::vector<const ImGuiWindow*> s_stack;
		return s_stack;
	}

	inline void PushWrapInsideTooltip(float a_wrap)
	{
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + a_wrap);
		Stack().push_back(ImGui::GetCurrentWindowRead());
	}

	inline bool BeginTooltip()
	{
		const float wrap = WrapWidth();
		if (!ImGui::BeginTooltip()) { return false; }
		PushWrapInsideTooltip(wrap);
		return true;
	}

	inline bool BeginItemTooltip()
	{
		const float wrap = WrapWidth();
		if (!ItemHovered(ImGuiHoveredFlags_ForTooltip) || !ImGui::BeginTooltip()) { return false; }
		PushWrapInsideTooltip(wrap);
		return true;
	}

	inline void EndTooltip()
	{
		if (!Stack().empty() && Stack().back() == ImGui::GetCurrentWindowRead())
		{
			Stack().pop_back();
			ImGui::PopTextWrapPos();
		}
		ImGui::EndTooltip();
	}

	// ImGui's SetTooltipV, with the text wrapped.
	inline void SetTooltipV(const char* a_fmt, va_list a_args)
	{
		if (OfferToBar(a_fmt, a_args)) { return; }
		const float wrap = WrapWidth();
		if (!ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePrevious, ImGuiWindowFlags_None)) { return; }
		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap);
		ImGui::TextV(a_fmt, a_args);
		ImGui::PopTextWrapPos();
		ImGui::EndTooltip();
	}

	// ImGui's SetItemTooltipV, with the text wrapped.
	inline void SetItemTooltipV(const char* a_fmt, va_list a_args)
	{
		if (ItemHovered(ImGuiHoveredFlags_ForTooltip)) { SetTooltipV(a_fmt, a_args); }
	}
}
