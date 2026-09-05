// ============================================================================================
// WIDGET EXPORTS, SECOND TRANCHE (1.5.4).
//
// Measured 2026-09-04: once the module-name alias arrived in time, every third-party SMF
// consumer registered - and the first page opened (Block Overhaul) crashed the game with
// "execute memory at 0x0". The stock consumer header's ImGui wrappers are NOT null-guarded:
// a widget export we do not have is a call to address zero the moment a page uses it. So the
// widget surface has to cover what shipping consumers actually call, not a hand-picked subset.
//
// These 61 names are the union of what the nine installed third-party consumers plus DevBench
// reference and AMF lacked. Signatures are the public consumer header's func_t typedefs,
// verbatim: ImVec2/ImVec4 cross by value, igGetContentRegionAvail is the one pOut case, the
// *V variants take a va_list. Same rules as Compat.cpp's first tranche.
// ============================================================================================

#include <imgui.h>

#include <cstdarg>
#include <cstddef>
#include <type_traits>

#define AMF_EXPORT extern "C" __declspec(dllexport)

// ---- windows / layout ------------------------------------------------------------------------
AMF_EXPORT bool igBegin(const char* a_name, bool* a_open, int a_flags) { return ImGui::Begin(a_name ? a_name : "", a_open, a_flags); }
AMF_EXPORT void igEnd() { ImGui::End(); }
AMF_EXPORT void igSetNextWindowPos(const ImVec2 a_pos, int a_cond, const ImVec2 a_pivot) { ImGui::SetNextWindowPos(a_pos, a_cond, a_pivot); }
AMF_EXPORT void igSetNextWindowSize(const ImVec2 a_size, int a_cond) { ImGui::SetNextWindowSize(a_size, a_cond); }
AMF_EXPORT void igSetNextWindowSizeConstraints(const ImVec2 a_min, const ImVec2 a_max, ImGuiSizeCallback a_cb, void* a_user) { ImGui::SetNextWindowSizeConstraints(a_min, a_max, a_cb, a_user); }
AMF_EXPORT void igSetNextWindowBgAlpha(float a_alpha) { ImGui::SetNextWindowBgAlpha(a_alpha); }
AMF_EXPORT bool igIsWindowAppearing() { return ImGui::IsWindowAppearing(); }
AMF_EXPORT void igSetWindowFontScale(float a_scale) { ImGui::SetWindowFontScale(a_scale); }
AMF_EXPORT void igGetContentRegionAvail(ImVec2* a_out) { if (a_out) { *a_out = ImGui::GetContentRegionAvail(); } }
AMF_EXPORT void igSetCursorScreenPos(const ImVec2 a_pos) { ImGui::SetCursorScreenPos(a_pos); }
AMF_EXPORT void igDummy(const ImVec2 a_size) { ImGui::Dummy(a_size); }
AMF_EXPORT void igSetNextItemWidth(float a_width) { ImGui::SetNextItemWidth(a_width); }
AMF_EXPORT void igSetItemDefaultFocus() { ImGui::SetItemDefaultFocus(); }
AMF_EXPORT void igSetKeyboardFocusHere(int a_offset) { ImGui::SetKeyboardFocusHere(a_offset); }
AMF_EXPORT void igSetScrollHereY(float a_ratio) { ImGui::SetScrollHereY(a_ratio); }
AMF_EXPORT void igBeginDisabled(bool a_disabled) { ImGui::BeginDisabled(a_disabled); }
AMF_EXPORT void igEndDisabled() { ImGui::EndDisabled(); }
AMF_EXPORT void igPushID_Int(int a_id) { ImGui::PushID(a_id); }

// ---- popups / combos / tabs / tooltips ---------------------------------------------------------
AMF_EXPORT bool igBeginPopup(const char* a_id, int a_flags) { return a_id ? ImGui::BeginPopup(a_id, a_flags) : false; }
AMF_EXPORT bool igBeginPopupModal(const char* a_name, bool* a_open, int a_flags) { return a_name ? ImGui::BeginPopupModal(a_name, a_open, a_flags) : false; }
AMF_EXPORT void igEndPopup() { ImGui::EndPopup(); }
AMF_EXPORT void igOpenPopup_Str(const char* a_id, int a_flags) { if (a_id) { ImGui::OpenPopup(a_id, a_flags); } }
AMF_EXPORT void igCloseCurrentPopup() { ImGui::CloseCurrentPopup(); }
AMF_EXPORT bool igBeginCombo(const char* a_label, const char* a_preview, int a_flags) { return ImGui::BeginCombo(a_label ? a_label : "", a_preview, a_flags); }
AMF_EXPORT void igEndCombo() { ImGui::EndCombo(); }
AMF_EXPORT bool igBeginTabBar(const char* a_id, int a_flags) { return ImGui::BeginTabBar(a_id ? a_id : "", a_flags); }
AMF_EXPORT void igEndTabBar() { ImGui::EndTabBar(); }
AMF_EXPORT bool igBeginTabItem(const char* a_label, bool* a_open, int a_flags) { return ImGui::BeginTabItem(a_label ? a_label : "", a_open, a_flags); }
AMF_EXPORT void igEndTabItem() { ImGui::EndTabItem(); }
AMF_EXPORT bool igBeginTooltip()
{
	// Dear ImGui made BeginTooltip return bool in 1.90; older releases return void. The
	// consumer expects a bool either way, and the contract is "true means draw the body".
	if constexpr (std::is_same_v<decltype(ImGui::BeginTooltip()), bool>) {
		return ImGui::BeginTooltip();
	} else {
		ImGui::BeginTooltip();
		return true;
	}
}
AMF_EXPORT void igEndTooltip() { ImGui::EndTooltip(); }

// ---- tables ---------------------------------------------------------------------------------
AMF_EXPORT bool igBeginTable(const char* a_id, int a_columns, int a_flags, const ImVec2 a_outer, float a_innerWidth) { return ImGui::BeginTable(a_id ? a_id : "", a_columns, a_flags, a_outer, a_innerWidth); }
AMF_EXPORT void igEndTable() { ImGui::EndTable(); }
AMF_EXPORT void igTableSetupColumn(const char* a_label, int a_flags, float a_initWidth, ImGuiID a_userId) { ImGui::TableSetupColumn(a_label ? a_label : "", a_flags, a_initWidth, a_userId); }
AMF_EXPORT void igTableSetupScrollFreeze(int a_cols, int a_rows) { ImGui::TableSetupScrollFreeze(a_cols, a_rows); }
AMF_EXPORT void igTableHeadersRow() { ImGui::TableHeadersRow(); }
AMF_EXPORT void igTableNextRow(int a_flags, float a_minHeight) { ImGui::TableNextRow(a_flags, a_minHeight); }
AMF_EXPORT bool igTableNextColumn() { return ImGui::TableNextColumn(); }
AMF_EXPORT bool igTableSetColumnIndex(int a_column) { return ImGui::TableSetColumnIndex(a_column); }
AMF_EXPORT ImGuiTableSortSpecs* igTableGetSortSpecs() { return ImGui::TableGetSortSpecs(); }

// ---- text / widgets ----------------------------------------------------------------------------
AMF_EXPORT void igBulletTextV(const char* a_fmt, va_list a_args) { if (a_fmt) { ImGui::BulletTextV(a_fmt, a_args); } }
AMF_EXPORT void igTextColoredV(const ImVec4 a_col, const char* a_fmt, va_list a_args) { if (a_fmt) { ImGui::TextColoredV(a_col, a_fmt, a_args); } }
AMF_EXPORT void igTextUnformatted(const char* a_text, const char* a_end) { if (a_text) { ImGui::TextUnformatted(a_text, a_end); } }
AMF_EXPORT void igPushTextWrapPos(float a_pos) { ImGui::PushTextWrapPos(a_pos); }
AMF_EXPORT void igPopTextWrapPos() { ImGui::PopTextWrapPos(); }
AMF_EXPORT bool igDragFloat(const char* a_label, float* a_v, float a_speed, float a_min, float a_max, const char* a_fmt, int a_flags) { return a_v ? ImGui::DragFloat(a_label ? a_label : "", a_v, a_speed, a_min, a_max, a_fmt ? a_fmt : "%.3f", a_flags) : false; }
AMF_EXPORT bool igInputFloat(const char* a_label, float* a_v, float a_step, float a_stepFast, const char* a_fmt, int a_flags) { return a_v ? ImGui::InputFloat(a_label ? a_label : "", a_v, a_step, a_stepFast, a_fmt ? a_fmt : "%.3f", a_flags) : false; }
AMF_EXPORT bool igInputTextWithHint(const char* a_label, const char* a_hint, char* a_buf, size_t a_size, int a_flags, ImGuiInputTextCallback a_cb, void* a_user) { return (a_buf && a_size > 0) ? ImGui::InputTextWithHint(a_label ? a_label : "", a_hint ? a_hint : "", a_buf, a_size, a_flags, a_cb, a_user) : false; }
AMF_EXPORT bool igRadioButton_IntPtr(const char* a_label, int* a_v, int a_button) { return a_v ? ImGui::RadioButton(a_label ? a_label : "", a_v, a_button) : false; }
AMF_EXPORT bool igSmallButton(const char* a_label) { return ImGui::SmallButton(a_label ? a_label : ""); }
AMF_EXPORT void igProgressBar(float a_fraction, const ImVec2 a_size, const char* a_overlay) { ImGui::ProgressBar(a_fraction, a_size, a_overlay); }
AMF_EXPORT bool igTreeNodeEx_Str(const char* a_label, int a_flags) { return ImGui::TreeNodeEx(a_label ? a_label : "", a_flags); }
AMF_EXPORT void igTreePop() { ImGui::TreePop(); }

// ---- style / io / input -----------------------------------------------------------------------
// Colour indices arrive numbered by the consumer header's ImGuiCol_ enum (Dear ImGui 1.90
// docking), not ours - CompatEnums.cpp owns the translation table. (1.5.9)
namespace compat_enums { int MapColour(int a_consumerIdx); }
AMF_EXPORT void igPushStyleColor_Vec4(int a_idx, const ImVec4 a_col) { ImGui::PushStyleColor(compat_enums::MapColour(a_idx), a_col); }
AMF_EXPORT void igPopStyleColor(int a_count) { ImGui::PopStyleColor(a_count); }
AMF_EXPORT ImU32 igGetColorU32_Vec4(const ImVec4 a_col) { return ImGui::GetColorU32(a_col); }
AMF_EXPORT float igGetFontSize() { return ImGui::GetFontSize(); }
AMF_EXPORT ImGuiIO* igGetIO() { return &ImGui::GetIO(); }
AMF_EXPORT bool igIsKeyDown_Nil(int a_key) { return ImGui::IsKeyDown(static_cast<ImGuiKey>(a_key)); }
AMF_EXPORT bool igIsMouseClicked_Bool(int a_button, bool a_repeat) { return ImGui::IsMouseClicked(a_button, a_repeat); }

// ---- draw list --------------------------------------------------------------------------------
AMF_EXPORT void ImDrawList_AddLine(ImDrawList* a_list, const ImVec2 a_p1, const ImVec2 a_p2, ImU32 a_col, float a_thickness) { if (a_list) { a_list->AddLine(a_p1, a_p2, a_col, a_thickness); } }
