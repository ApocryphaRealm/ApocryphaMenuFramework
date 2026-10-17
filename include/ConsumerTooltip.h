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
		if (!ImGui::BeginItemTooltip()) { return false; }
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
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip)) { SetTooltipV(a_fmt, a_args); }
	}
}
