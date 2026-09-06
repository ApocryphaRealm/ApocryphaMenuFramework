// ============================================================================================
// WIDGET EXPORTS, THIRD TRANCHE (1.5.9) - and the ENUM TRANSLATION every tranche needed.
//
// Measured 2026-09-05, the first hour after the file-name alias let every consumer reach its
// page: Simple Follower Framework's page called igGetStyle, which this framework did not
// export, took the safe stub's zero as an ImGuiStyle* and read from address 0. The static scan
// over the ten consumers on hand found eighteen names in that state; all are here.
//
// The part that is NOT just "more exports": the public consumer header was generated against
// the Dear ImGui 1.90 DOCKING branch. Its ImGuiCol_ enum has DockingPreview/DockingEmptyBg in
// the middle, its ImGuiStyleVar_ enum has TableAngledHeaders* and DockingSeparatorSize, its
// ImGuiItemFlags_ are the 1.90 internal bit layout, and its ImGuiStyle struct carries
// DockingSeparatorSize and a 55-entry Colors array. The SE/AE line embeds 1.90.2 WITHOUT
// docking (two colours fewer, three style vars fewer) and the 1.7.x line embeds 1.92.8 (a
// reordered colour enum, renamed tabs, a reshuffled style-var enum, a different item-flag
// layout). So a number the consumer passes for a colour or a style var is NOT the same number
// in this framework, and a pointer to our real ImGuiStyle is NOT laid out the way the consumer
// will read it. Every index crossing this boundary is translated by NAME, at compile time
// against whichever ImGui this line embeds; igGetStyle returns a copy in the CONSUMER'S layout.
//
// Unmapped values never unbalance a push/pop pair: an index this ImGui does not have pushes a
// harmless value of a var it does have, so the consumer's matching Pop still pops exactly one.
// ============================================================================================

#include "utils/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#define AMF_EXPORT extern "C" __declspec(dllexport)

namespace
{
	// ---- the consumer header's enums, as it numbers them -----------------------------------
	// (docking branch of Dear ImGui 1.90 - stated here so the translation is explicit and
	// checkable against the header rather than inferred from ours)

	enum SmfCol : int
	{
		SmfCol_Text, SmfCol_TextDisabled, SmfCol_WindowBg, SmfCol_ChildBg, SmfCol_PopupBg, SmfCol_Border,
		SmfCol_BorderShadow, SmfCol_FrameBg, SmfCol_FrameBgHovered, SmfCol_FrameBgActive, SmfCol_TitleBg,
		SmfCol_TitleBgActive, SmfCol_TitleBgCollapsed, SmfCol_MenuBarBg, SmfCol_ScrollbarBg, SmfCol_ScrollbarGrab,
		SmfCol_ScrollbarGrabHovered, SmfCol_ScrollbarGrabActive, SmfCol_CheckMark, SmfCol_SliderGrab,
		SmfCol_SliderGrabActive, SmfCol_Button, SmfCol_ButtonHovered, SmfCol_ButtonActive, SmfCol_Header,
		SmfCol_HeaderHovered, SmfCol_HeaderActive, SmfCol_Separator, SmfCol_SeparatorHovered, SmfCol_SeparatorActive,
		SmfCol_ResizeGrip, SmfCol_ResizeGripHovered, SmfCol_ResizeGripActive, SmfCol_Tab, SmfCol_TabHovered,
		SmfCol_TabActive, SmfCol_TabUnfocused, SmfCol_TabUnfocusedActive, SmfCol_DockingPreview, SmfCol_DockingEmptyBg,
		SmfCol_PlotLines, SmfCol_PlotLinesHovered, SmfCol_PlotHistogram, SmfCol_PlotHistogramHovered,
		SmfCol_TableHeaderBg, SmfCol_TableBorderStrong, SmfCol_TableBorderLight, SmfCol_TableRowBg, SmfCol_TableRowBgAlt,
		SmfCol_TextSelectedBg, SmfCol_DragDropTarget, SmfCol_NavHighlight, SmfCol_NavWindowingHighlight,
		SmfCol_NavWindowingDimBg, SmfCol_ModalWindowDimBg,
		SmfCol_COUNT
	};

	enum SmfStyleVar : int
	{
		SmfVar_Alpha, SmfVar_DisabledAlpha, SmfVar_WindowPadding, SmfVar_WindowRounding, SmfVar_WindowBorderSize,
		SmfVar_WindowMinSize, SmfVar_WindowTitleAlign, SmfVar_ChildRounding, SmfVar_ChildBorderSize, SmfVar_PopupRounding,
		SmfVar_PopupBorderSize, SmfVar_FramePadding, SmfVar_FrameRounding, SmfVar_FrameBorderSize, SmfVar_ItemSpacing,
		SmfVar_ItemInnerSpacing, SmfVar_IndentSpacing, SmfVar_CellPadding, SmfVar_ScrollbarSize, SmfVar_ScrollbarRounding,
		SmfVar_GrabMinSize, SmfVar_GrabRounding, SmfVar_TabRounding, SmfVar_TabBorderSize, SmfVar_TabBarBorderSize,
		SmfVar_TableAngledHeadersAngle, SmfVar_TableAngledHeadersTextAlign, SmfVar_ButtonTextAlign,
		SmfVar_SelectableTextAlign, SmfVar_SeparatorTextBorderSize, SmfVar_SeparatorTextAlign, SmfVar_SeparatorTextPadding,
		SmfVar_DockingSeparatorSize,
		SmfVar_COUNT
	};

	// The header's ImGuiItemFlags_ bits (Dear ImGui 1.90 internal layout).
	constexpr int kSmfItem_NoTabStop = 1 << 0;
	constexpr int kSmfItem_ButtonRepeat = 1 << 1;
	constexpr int kSmfItem_Disabled = 1 << 2;
	constexpr int kSmfItem_NoNav = 1 << 3;
	constexpr int kSmfItem_NoNavDefaultFocus = 1 << 4;
	constexpr int kSmfItem_SelectableDontClosePopup = 1 << 5;
	constexpr int kSmfItem_AllowOverlap = 1 << 9;

	// Names that Dear ImGui renamed between the versions this framework builds against.
#if IMGUI_VERSION_NUM >= 19090
	constexpr ImGuiCol kOurTabActive = ImGuiCol_TabSelected;
	constexpr ImGuiCol kOurTabUnfocused = ImGuiCol_TabDimmed;
	constexpr ImGuiCol kOurTabUnfocusedActive = ImGuiCol_TabDimmedSelected;
#else
	constexpr ImGuiCol kOurTabActive = ImGuiCol_TabActive;
	constexpr ImGuiCol kOurTabUnfocused = ImGuiCol_TabUnfocused;
	constexpr ImGuiCol kOurTabUnfocusedActive = ImGuiCol_TabUnfocusedActive;
#endif
#if IMGUI_VERSION_NUM >= 19140
	constexpr ImGuiCol kOurNavHighlight = ImGuiCol_NavCursor;
#else
	constexpr ImGuiCol kOurNavHighlight = ImGuiCol_NavHighlight;
#endif

	// Consumer colour index -> ours. -1 = this ImGui has no such colour.
	constexpr ImGuiCol kColMap[SmfCol_COUNT] = {
		ImGuiCol_Text, ImGuiCol_TextDisabled, ImGuiCol_WindowBg, ImGuiCol_ChildBg, ImGuiCol_PopupBg, ImGuiCol_Border,
		ImGuiCol_BorderShadow, ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_TitleBg,
		ImGuiCol_TitleBgActive, ImGuiCol_TitleBgCollapsed, ImGuiCol_MenuBarBg, ImGuiCol_ScrollbarBg, ImGuiCol_ScrollbarGrab,
		ImGuiCol_ScrollbarGrabHovered, ImGuiCol_ScrollbarGrabActive, ImGuiCol_CheckMark, ImGuiCol_SliderGrab,
		ImGuiCol_SliderGrabActive, ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive, ImGuiCol_Header,
		ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive, ImGuiCol_Separator, ImGuiCol_SeparatorHovered, ImGuiCol_SeparatorActive,
		ImGuiCol_ResizeGrip, ImGuiCol_ResizeGripHovered, ImGuiCol_ResizeGripActive, ImGuiCol_Tab, ImGuiCol_TabHovered,
		kOurTabActive, kOurTabUnfocused, kOurTabUnfocusedActive,
#ifdef IMGUI_HAS_DOCK
		ImGuiCol_DockingPreview, ImGuiCol_DockingEmptyBg,   // exact: this ImGui is the docking branch (1.90.8, SMF's own)
#else
		kOurTabActive /* DockingPreview: no docking here; the nearest thing it tints */, ImGuiCol_WindowBg /* DockingEmptyBg */,
#endif
		ImGuiCol_PlotLines, ImGuiCol_PlotLinesHovered, ImGuiCol_PlotHistogram, ImGuiCol_PlotHistogramHovered,
		ImGuiCol_TableHeaderBg, ImGuiCol_TableBorderStrong, ImGuiCol_TableBorderLight, ImGuiCol_TableRowBg, ImGuiCol_TableRowBgAlt,
		ImGuiCol_TextSelectedBg, ImGuiCol_DragDropTarget, kOurNavHighlight, ImGuiCol_NavWindowingHighlight,
		ImGuiCol_NavWindowingDimBg, ImGuiCol_ModalWindowDimBg,
	};
	static_assert(sizeof(kColMap) / sizeof(kColMap[0]) == SmfCol_COUNT, "colour map must cover the consumer enum exactly");

	struct VarMap
	{
		ImGuiStyleVar ours;   // -1 = absent in this ImGui
		bool vec2;
	};

#if IMGUI_VERSION_NUM >= 19010
	constexpr ImGuiStyleVar kOurAngledAngle = ImGuiStyleVar_TableAngledHeadersAngle;      // both added in 1.90.1
	constexpr ImGuiStyleVar kOurAngledAlign = ImGuiStyleVar_TableAngledHeadersTextAlign;
#else
	constexpr ImGuiStyleVar kOurAngledAngle = -1;
	constexpr ImGuiStyleVar kOurAngledAlign = -1;
#endif
#if IMGUI_VERSION_NUM >= 19100
	constexpr ImGuiStyleVar kOurTabBorderSize = ImGuiStyleVar_TabBorderSize;              // added in 1.91.0
#else
	constexpr ImGuiStyleVar kOurTabBorderSize = -1;   // 1.90.x has the style field but no push-able var for it
#endif

	constexpr VarMap kVarMap[SmfVar_COUNT] = {
		{ ImGuiStyleVar_Alpha, false }, { ImGuiStyleVar_DisabledAlpha, false }, { ImGuiStyleVar_WindowPadding, true },
		{ ImGuiStyleVar_WindowRounding, false }, { ImGuiStyleVar_WindowBorderSize, false }, { ImGuiStyleVar_WindowMinSize, true },
		{ ImGuiStyleVar_WindowTitleAlign, true }, { ImGuiStyleVar_ChildRounding, false }, { ImGuiStyleVar_ChildBorderSize, false },
		{ ImGuiStyleVar_PopupRounding, false }, { ImGuiStyleVar_PopupBorderSize, false }, { ImGuiStyleVar_FramePadding, true },
		{ ImGuiStyleVar_FrameRounding, false }, { ImGuiStyleVar_FrameBorderSize, false }, { ImGuiStyleVar_ItemSpacing, true },
		{ ImGuiStyleVar_ItemInnerSpacing, true }, { ImGuiStyleVar_IndentSpacing, false }, { ImGuiStyleVar_CellPadding, true },
		{ ImGuiStyleVar_ScrollbarSize, false }, { ImGuiStyleVar_ScrollbarRounding, false }, { ImGuiStyleVar_GrabMinSize, false },
		{ ImGuiStyleVar_GrabRounding, false }, { ImGuiStyleVar_TabRounding, false }, { kOurTabBorderSize, false },
		{ ImGuiStyleVar_TabBarBorderSize, false }, { kOurAngledAngle, false }, { kOurAngledAlign, true },
		{ ImGuiStyleVar_ButtonTextAlign, true }, { ImGuiStyleVar_SelectableTextAlign, true },
		{ ImGuiStyleVar_SeparatorTextBorderSize, false }, { ImGuiStyleVar_SeparatorTextAlign, true },
		{ ImGuiStyleVar_SeparatorTextPadding, true },
#ifdef IMGUI_HAS_DOCK
		{ ImGuiStyleVar_DockingSeparatorSize, false },
#else
		{ -1 /* DockingSeparatorSize */, false },
#endif
	};
	static_assert(sizeof(kVarMap) / sizeof(kVarMap[0]) == SmfVar_COUNT, "style-var map must cover the consumer enum exactly");

	ImGuiCol MapCol(int a_consumerIdx)
	{
		if (a_consumerIdx >= 0 && a_consumerIdx < SmfCol_COUNT) {
			return kColMap[a_consumerIdx];
		}
		static bool s_warned = false;
		if (!s_warned) {
			s_warned = true;
			logger::warn("consumer passed colour index {} which is outside the SKSE Menu Framework header's ImGuiCol_ range (0..{}); using Text", a_consumerIdx, SmfCol_COUNT - 1);
		}
		return ImGuiCol_Text;
	}

	// A push that this ImGui cannot honour still has to consume the consumer's later Pop, so
	// push the var's own current value: visually nothing, stack-wise exactly one entry.
	void PushBalancedNoOp()
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha);
	}

	void WarnVarOnce(int a_idx, const char* a_why)
	{
		static bool s_warned[SmfVar_COUNT + 1]{};
		const int slot = (a_idx >= 0 && a_idx < SmfVar_COUNT) ? a_idx : SmfVar_COUNT;
		if (!s_warned[slot]) {
			s_warned[slot] = true;
			logger::warn("consumer PushStyleVar index {}: {} - pushed a no-op so its Pop stays balanced", a_idx, a_why);
		}
	}

	int MapItemFlags(int a_consumer)
	{
		int ours = 0;
		if (a_consumer & kSmfItem_NoTabStop) { ours |= ImGuiItemFlags_NoTabStop; }
		if (a_consumer & kSmfItem_ButtonRepeat) { ours |= ImGuiItemFlags_ButtonRepeat; }
		if (a_consumer & kSmfItem_Disabled) { ours |= ImGuiItemFlags_Disabled; }
		if (a_consumer & kSmfItem_NoNav) { ours |= ImGuiItemFlags_NoNav; }
		if (a_consumer & kSmfItem_NoNavDefaultFocus) { ours |= ImGuiItemFlags_NoNavDefaultFocus; }
#if IMGUI_VERSION_NUM >= 19100
		if (a_consumer & kSmfItem_SelectableDontClosePopup) { ours |= ImGuiItemFlags_AutoClosePopups; }   // inverted meaning, handled below
		if (a_consumer & kSmfItem_AllowOverlap) { ours |= ImGuiItemFlags_AllowOverlap; }
#else
		if (a_consumer & kSmfItem_SelectableDontClosePopup) { ours |= ImGuiItemFlags_SelectableDontClosePopup; }
		if (a_consumer & kSmfItem_AllowOverlap) { ours |= ImGuiItemFlags_AllowOverlap; }
#endif
		return ours;
	}

	// ---- ImGuiStyle in the CONSUMER'S layout ---------------------------------------------------
	// The header's struct, field for field (Dear ImGui 1.90 docking branch), so a consumer that
	// reads style->FramePadding or style->Colors[ImGuiCol_Text] lands on the right bytes whatever
	// ImGui this line embeds. Filled from the live style on every igGetStyle call; a write into
	// it does not reach the real style (the stock header offers no way to hand it back either).
	struct SmfStyle
	{
		float Alpha;
		float DisabledAlpha;
		ImVec2 WindowPadding;
		float WindowRounding;
		float WindowBorderSize;
		ImVec2 WindowMinSize;
		ImVec2 WindowTitleAlign;
		int WindowMenuButtonPosition;
		float ChildRounding;
		float ChildBorderSize;
		float PopupRounding;
		float PopupBorderSize;
		ImVec2 FramePadding;
		float FrameRounding;
		float FrameBorderSize;
		ImVec2 ItemSpacing;
		ImVec2 ItemInnerSpacing;
		ImVec2 CellPadding;
		ImVec2 TouchExtraPadding;
		float IndentSpacing;
		float ColumnsMinSpacing;
		float ScrollbarSize;
		float ScrollbarRounding;
		float GrabMinSize;
		float GrabRounding;
		float LogSliderDeadzone;
		float TabRounding;
		float TabBorderSize;
		float TabMinWidthForCloseButton;
		float TabBarBorderSize;
		float TableAngledHeadersAngle;
		ImVec2 TableAngledHeadersTextAlign;
		int ColorButtonPosition;
		ImVec2 ButtonTextAlign;
		ImVec2 SelectableTextAlign;
		float SeparatorTextBorderSize;
		ImVec2 SeparatorTextAlign;
		ImVec2 SeparatorTextPadding;
		ImVec2 DisplayWindowPadding;
		ImVec2 DisplaySafeAreaPadding;
		float DockingSeparatorSize;
		float MouseCursorScale;
		bool AntiAliasedLines;
		bool AntiAliasedLinesUseTex;
		bool AntiAliasedFill;
		float CurveTessellationTol;
		float CircleTessellationMaxError;
		ImVec4 Colors[SmfCol_COUNT];
		float HoverStationaryDelay;
		float HoverDelayShort;
		float HoverDelayNormal;
		int HoverFlagsForTooltipMouse;
		int HoverFlagsForTooltipNav;
	};

	SmfStyle g_shadowStyle{};

	void FillShadowStyle()
	{
		const ImGuiStyle& s = ImGui::GetStyle();
		SmfStyle& o = g_shadowStyle;
		o.Alpha = s.Alpha;
		o.DisabledAlpha = s.DisabledAlpha;
		o.WindowPadding = s.WindowPadding;
		o.WindowRounding = s.WindowRounding;
		o.WindowBorderSize = s.WindowBorderSize;
		o.WindowMinSize = s.WindowMinSize;
		o.WindowTitleAlign = s.WindowTitleAlign;
		o.WindowMenuButtonPosition = static_cast<int>(s.WindowMenuButtonPosition);
		o.ChildRounding = s.ChildRounding;
		o.ChildBorderSize = s.ChildBorderSize;
		o.PopupRounding = s.PopupRounding;
		o.PopupBorderSize = s.PopupBorderSize;
		o.FramePadding = s.FramePadding;
		o.FrameRounding = s.FrameRounding;
		o.FrameBorderSize = s.FrameBorderSize;
		o.ItemSpacing = s.ItemSpacing;
		o.ItemInnerSpacing = s.ItemInnerSpacing;
		o.CellPadding = s.CellPadding;
		o.TouchExtraPadding = s.TouchExtraPadding;
		o.IndentSpacing = s.IndentSpacing;
		o.ColumnsMinSpacing = s.ColumnsMinSpacing;
		o.ScrollbarSize = s.ScrollbarSize;
		o.ScrollbarRounding = s.ScrollbarRounding;
		o.GrabMinSize = s.GrabMinSize;
		o.GrabRounding = s.GrabRounding;
		o.LogSliderDeadzone = s.LogSliderDeadzone;
		o.TabRounding = s.TabRounding;
		o.TabBorderSize = s.TabBorderSize;
#if IMGUI_VERSION_NUM >= 19100
		o.TabMinWidthForCloseButton = s.TabCloseButtonMinWidthSelected;
#else
		o.TabMinWidthForCloseButton = s.TabMinWidthForCloseButton;
#endif
#if IMGUI_VERSION_NUM >= 19010
		o.TableAngledHeadersAngle = s.TableAngledHeadersAngle;
		o.TableAngledHeadersTextAlign = s.TableAngledHeadersTextAlign;
#else
		o.TableAngledHeadersAngle = 0.0f;
		o.TableAngledHeadersTextAlign = ImVec2(0.5f, 0.0f);
#endif
		o.TabBarBorderSize = s.TabBarBorderSize;
		o.ColorButtonPosition = static_cast<int>(s.ColorButtonPosition);
		o.ButtonTextAlign = s.ButtonTextAlign;
		o.SelectableTextAlign = s.SelectableTextAlign;
		o.SeparatorTextBorderSize = s.SeparatorTextBorderSize;
		o.SeparatorTextAlign = s.SeparatorTextAlign;
		o.SeparatorTextPadding = s.SeparatorTextPadding;
		o.DisplayWindowPadding = s.DisplayWindowPadding;
		o.DisplaySafeAreaPadding = s.DisplaySafeAreaPadding;
#ifdef IMGUI_HAS_DOCK
		o.DockingSeparatorSize = s.DockingSeparatorSize;
#else
		o.DockingSeparatorSize = 0.0f;   // no docking on this line
#endif
		o.MouseCursorScale = s.MouseCursorScale;
		o.AntiAliasedLines = s.AntiAliasedLines;
		o.AntiAliasedLinesUseTex = s.AntiAliasedLinesUseTex;
		o.AntiAliasedFill = s.AntiAliasedFill;
		o.CurveTessellationTol = s.CurveTessellationTol;
		o.CircleTessellationMaxError = s.CircleTessellationMaxError;
		for (int i = 0; i < SmfCol_COUNT; ++i) {
			const ImGuiCol ours = kColMap[i];
			o.Colors[i] = (ours >= 0 && ours < ImGuiCol_COUNT) ? s.Colors[ours] : ImVec4(0, 0, 0, 0);
		}
		o.HoverStationaryDelay = s.HoverStationaryDelay;
		o.HoverDelayShort = s.HoverDelayShort;
		o.HoverDelayNormal = s.HoverDelayNormal;
		o.HoverFlagsForTooltipMouse = static_cast<int>(s.HoverFlagsForTooltipMouse);
		o.HoverFlagsForTooltipNav = static_cast<int>(s.HoverFlagsForTooltipNav);
	}
}

namespace compat_enums
{
	// Used by the earlier tranches' exports that take a colour index, so one table decides.
	int MapColour(int a_consumerIdx) { return MapCol(a_consumerIdx); }
}

// ---- style ------------------------------------------------------------------------------------
AMF_EXPORT void* igGetStyle()
{
	FillShadowStyle();
	return &g_shadowStyle;
}

AMF_EXPORT void igPushStyleColor_U32(int a_idx, ImU32 a_col) { ImGui::PushStyleColor(MapCol(a_idx), a_col); }

AMF_EXPORT void igPushStyleVar_Float(int a_idx, float a_val)
{
	if (a_idx < 0 || a_idx >= SmfVar_COUNT) { WarnVarOnce(a_idx, "outside the header's ImGuiStyleVar_ range"); PushBalancedNoOp(); return; }
	const VarMap& m = kVarMap[a_idx];
	if (m.ours < 0) { WarnVarOnce(a_idx, "this ImGui has no such style var"); PushBalancedNoOp(); return; }
	if (m.vec2) { WarnVarOnce(a_idx, "is an ImVec2 var but was pushed as a float"); PushBalancedNoOp(); return; }
	ImGui::PushStyleVar(m.ours, a_val);
}

AMF_EXPORT void igPushStyleVar_Vec2(int a_idx, const ImVec2 a_val)
{
	if (a_idx < 0 || a_idx >= SmfVar_COUNT) { WarnVarOnce(a_idx, "outside the header's ImGuiStyleVar_ range"); PushBalancedNoOp(); return; }
	const VarMap& m = kVarMap[a_idx];
	if (m.ours < 0) { WarnVarOnce(a_idx, "this ImGui has no such style var"); PushBalancedNoOp(); return; }
	if (!m.vec2) { WarnVarOnce(a_idx, "is a float var but was pushed as an ImVec2"); PushBalancedNoOp(); return; }
	ImGui::PushStyleVar(m.ours, a_val);
}

AMF_EXPORT void igPopStyleVar(int a_count) { ImGui::PopStyleVar(a_count); }

AMF_EXPORT void igPushItemFlag(int a_option, bool a_enabled)
{
	const int ours = MapItemFlags(a_option);
#if IMGUI_VERSION_NUM >= 19100
	// The header's SelectableDontClosePopup is the inverse of this ImGui's AutoClosePopups.
	if ((a_option & kSmfItem_SelectableDontClosePopup) && (ours & ImGuiItemFlags_AutoClosePopups)) {
		ImGui::PushItemFlag(ours & ~ImGuiItemFlags_AutoClosePopups, a_enabled);
		if (ours & ~ImGuiItemFlags_AutoClosePopups) { ImGui::PopItemFlag(); }   // keep exactly one push for the consumer's one pop
		ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, !a_enabled);
		return;
	}
#endif
	ImGui::PushItemFlag(ours, a_enabled);   // ours == 0 still pushes one entry, so the Pop stays balanced
}

AMF_EXPORT void igPopItemFlag() { ImGui::PopItemFlag(); }

// ---- layout / groups --------------------------------------------------------------------------
AMF_EXPORT void igBeginGroup() { ImGui::BeginGroup(); }
AMF_EXPORT void igEndGroup() { ImGui::EndGroup(); }
AMF_EXPORT void igAlignTextToFramePadding() { ImGui::AlignTextToFramePadding(); }
AMF_EXPORT void igSetNextItemOpen(bool a_open, int a_cond) { ImGui::SetNextItemOpen(a_open, a_cond); }
AMF_EXPORT bool igIsItemDeactivatedAfterEdit() { return ImGui::IsItemDeactivatedAfterEdit(); }

// pOut convention: the result is written through the first argument.
AMF_EXPORT void igCalcTextSize(ImVec2* a_out, const char* a_text, const char* a_end, bool a_hideAfterHash, float a_wrap)
{
	if (!a_out) {
		return;
	}
	*a_out = a_text ? ImGui::CalcTextSize(a_text, a_end, a_hideAfterHash, a_wrap) : ImVec2(0, 0);
}

// ---- widgets ------------------------------------------------------------------------------------
AMF_EXPORT bool igRadioButton_Bool(const char* a_label, bool a_active) { return ImGui::RadioButton(a_label ? a_label : "", a_active); }
AMF_EXPORT bool igTreeNode_Str(const char* a_label) { return ImGui::TreeNode(a_label ? a_label : ""); }
AMF_EXPORT bool igInputFloat3(const char* a_label, float a_v[3], const char* a_fmt, int a_flags) { return a_v ? ImGui::InputFloat3(a_label ? a_label : "", a_v, a_fmt ? a_fmt : "%.3f", a_flags) : false; }
AMF_EXPORT bool igSliderFloat3(const char* a_label, float a_v[3], float a_min, float a_max, const char* a_fmt, int a_flags) { return a_v ? ImGui::SliderFloat3(a_label ? a_label : "", a_v, a_min, a_max, a_fmt ? a_fmt : "%.3f", a_flags) : false; }
AMF_EXPORT bool igSliderFloat4(const char* a_label, float a_v[4], float a_min, float a_max, const char* a_fmt, int a_flags) { return a_v ? ImGui::SliderFloat4(a_label ? a_label : "", a_v, a_min, a_max, a_fmt ? a_fmt : "%.3f", a_flags) : false; }
