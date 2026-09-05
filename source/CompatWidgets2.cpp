// ============================================================================================
// WIDGET EXPORTS, FOURTH TRANCHE (1.5.9) - the wider checklist.
//
// 2026-09-05: the owner dropped a checklist of every SKSE Menu Framework consumer visible in a
// reporter's screenshots (58 mods). 54 were downloaded and their DLLs scanned for the ig*/Im*
// names they reference; 65 of those names were not exported here. Every one is below, with the
// signature the public consumer header declares for it. Same rules as the earlier tranches:
// ImVec2/ImVec4/ImRect cross by value, pOut names write through their first argument, *V names
// take a va_list, and any pointer that could be null is checked before use because the stock
// header calls whatever it got back.
//
// Two objects cross this boundary as REAL pointers into this framework's ImGui, and the layout
// of what the consumer reads through them was checked against both embedded versions:
//   * ImGuiListClipper - consumers read DisplayStart/DisplayEnd; both sit at the same offsets
//     in 1.90.2 and 1.92.8 (Ctx, DisplayStart, DisplayEnd, ItemsCount, ItemsHeight, StartPosY).
//   * ImGuiPayload - identical field order in both (Data, DataSize, SourceId, SourceParentId,
//     DataFrameCount, DataType[33], Preview, Delivery).
// ImGuiViewport is NOT stable (1.92 inserted FramebufferScale before WorkPos), so igGetMainViewport
// returns a copy in the consumer header's layout, the same way igGetStyle does.
// ============================================================================================

#include "utils/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>
#include <cstdarg>
#include <cstddef>
#include <cstdint>

#define AMF_EXPORT extern "C" __declspec(dllexport)

namespace compat_enums { int MapColour(int a_consumerIdx); }

namespace
{
	// The consumer header's ImGuiViewport (Dear ImGui 1.90 docking branch), field for field.
	struct SmfViewport
	{
		ImGuiID ID;
		int Flags;
		ImVec2 Pos;
		ImVec2 Size;
		ImVec2 WorkPos;
		ImVec2 WorkSize;
		float DpiScale;
		ImGuiID ParentViewportId;
		ImDrawData* DrawData;
		void* RendererUserData;
		void* PlatformUserData;
		void* PlatformHandle;
		void* PlatformHandleRaw;
		bool PlatformWindowCreated;
		bool PlatformRequestMove;
		bool PlatformRequestResize;
		bool PlatformRequestClose;
	};

	SmfViewport g_shadowViewport{};

	// ImTextureID is void* in 1.90.2 and an integer handle in 1.92.8; consumers hand back the
	// value LoadTexture gave them, which is our shader-resource view either way.
	ImTextureID ToTexture(void* a_id)
	{
		// C-style on purpose: ImTextureID is a pointer in 1.90.2 and an integer in 1.92.8, and
		// only a C-style cast is legal for both from the same source text.
		return (ImTextureID)(std::uintptr_t)a_id;
	}
}

// ---- list clipper (struct API) --------------------------------------------------------------
AMF_EXPORT ImGuiListClipper* ImGuiListClipper_ImGuiListClipper() { return new ImGuiListClipper(); }
AMF_EXPORT void ImGuiListClipper_destroy(ImGuiListClipper* a_self) { delete a_self; }
AMF_EXPORT void ImGuiListClipper_Begin(ImGuiListClipper* a_self, int a_count, float a_height) { if (a_self) { a_self->Begin(a_count, a_height); } }
AMF_EXPORT bool ImGuiListClipper_Step(ImGuiListClipper* a_self) { return a_self ? a_self->Step() : false; }
AMF_EXPORT void ImGuiListClipper_End(ImGuiListClipper* a_self) { if (a_self) { a_self->End(); } }

// ---- drag and drop ----------------------------------------------------------------------------
AMF_EXPORT bool igBeginDragDropSource(int a_flags) { return ImGui::BeginDragDropSource(a_flags); }
AMF_EXPORT bool igSetDragDropPayload(const char* a_type, const void* a_data, size_t a_size, int a_cond) { return a_type ? ImGui::SetDragDropPayload(a_type, a_data, a_size, a_cond) : false; }
AMF_EXPORT void igEndDragDropSource() { ImGui::EndDragDropSource(); }
AMF_EXPORT bool igBeginDragDropTargetCustom(const ImRect a_bb, ImGuiID a_id) { return ImGui::BeginDragDropTargetCustom(a_bb, a_id); }
AMF_EXPORT const ImGuiPayload* igAcceptDragDropPayload(const char* a_type, int a_flags) { return ImGui::AcceptDragDropPayload(a_type, a_flags); }
AMF_EXPORT void igEndDragDropTarget() { ImGui::EndDragDropTarget(); }
AMF_EXPORT const ImGuiPayload* igGetDragDropPayload() { return ImGui::GetDragDropPayload(); }
AMF_EXPORT bool ImGuiPayload_IsDataType(ImGuiPayload* a_self, const char* a_type) { return (a_self && a_type) ? a_self->IsDataType(a_type) : false; }

// ---- windows / layout / cursor ----------------------------------------------------------------
AMF_EXPORT float igGetWindowWidth() { return ImGui::GetWindowWidth(); }
AMF_EXPORT float igCalcItemWidth() { return ImGui::CalcItemWidth(); }
AMF_EXPORT float igGetFrameHeightWithSpacing() { return ImGui::GetFrameHeightWithSpacing(); }
AMF_EXPORT float igGetTextLineHeightWithSpacing() { return ImGui::GetTextLineHeightWithSpacing(); }
AMF_EXPORT void igNewLine() { ImGui::NewLine(); }
AMF_EXPORT void igGetCursorPos(ImVec2* a_out) { if (a_out) { *a_out = ImGui::GetCursorPos(); } }
AMF_EXPORT float igGetCursorPosX() { return ImGui::GetCursorPosX(); }
AMF_EXPORT void igSetCursorPos(const ImVec2 a_pos) { ImGui::SetCursorPos(a_pos); }
AMF_EXPORT void igSetCursorPosX(float a_x) { ImGui::SetCursorPosX(a_x); }
AMF_EXPORT float igGetScrollY() { return ImGui::GetScrollY(); }
AMF_EXPORT float igGetScrollMaxY() { return ImGui::GetScrollMaxY(); }
AMF_EXPORT void igSetScrollY_Float(float a_y) { ImGui::SetScrollY(a_y); }
AMF_EXPORT void igGetItemRectMin(ImVec2* a_out) { if (a_out) { *a_out = ImGui::GetItemRectMin(); } }
AMF_EXPORT void igGetItemRectMax(ImVec2* a_out) { if (a_out) { *a_out = ImGui::GetItemRectMax(); } }
AMF_EXPORT void igGetItemRectSize(ImVec2* a_out) { if (a_out) { *a_out = ImGui::GetItemRectSize(); } }
AMF_EXPORT ImGuiID igGetID_Str(const char* a_id) { return ImGui::GetID(a_id ? a_id : ""); }
AMF_EXPORT void igColumns(int a_count, const char* a_id, bool a_border) { ImGui::Columns(a_count > 0 ? a_count : 1, a_id, a_border); }
AMF_EXPORT void igNextColumn() { ImGui::NextColumn(); }
AMF_EXPORT void igSeparatorEx(int a_flags, float a_thickness) { ImGui::SeparatorEx(a_flags, a_thickness); }
AMF_EXPORT ImGuiTable* igGetCurrentTable() { return ImGui::GetCurrentTable(); }

AMF_EXPORT void* igGetMainViewport()
{
	const ImGuiViewport* v = ImGui::GetMainViewport();
	SmfViewport& o = g_shadowViewport;
	o = SmfViewport{};
	if (v) {
		o.ID = v->ID;
		o.Flags = v->Flags;
		o.Pos = v->Pos;
		o.Size = v->Size;
		o.WorkPos = v->WorkPos;
		o.WorkSize = v->WorkSize;
		o.DpiScale = 1.0f;
		o.DrawData = nullptr;   // docking-branch field; neither embedded ImGui has it
		o.PlatformHandleRaw = v->PlatformHandleRaw;
	}
	return &o;
}

// ---- popups / tooltips / menus / list boxes ---------------------------------------------------
AMF_EXPORT bool igBeginPopupContextItem(const char* a_id, int a_flags) { return ImGui::BeginPopupContextItem(a_id, a_flags); }
AMF_EXPORT bool igMenuItem_Bool(const char* a_label, const char* a_shortcut, bool a_selected, bool a_enabled) { return ImGui::MenuItem(a_label ? a_label : "", a_shortcut, a_selected, a_enabled); }
AMF_EXPORT bool igBeginListBox(const char* a_label, const ImVec2 a_size) { return ImGui::BeginListBox(a_label ? a_label : "", a_size); }
AMF_EXPORT void igEndListBox() { ImGui::EndListBox(); }
AMF_EXPORT bool igBeginItemTooltip() { return ImGui::BeginItemTooltip(); }
AMF_EXPORT void igSetItemTooltipV(const char* a_fmt, va_list a_args) { if (a_fmt) { ImGui::SetItemTooltipV(a_fmt, a_args); } }
// Variadic spelling: one consumer resolves the non-V name, which cimgui declares as (fmt, ...).
AMF_EXPORT void igSetTooltip(const char* a_fmt, ...)
{
	if (!a_fmt) {
		return;
	}
	va_list args;
	va_start(args, a_fmt);
	ImGui::SetTooltipV(a_fmt, args);
	va_end(args);
}

// ---- widgets ------------------------------------------------------------------------------------
AMF_EXPORT bool igArrowButton(const char* a_id, int a_dir) { return ImGui::ArrowButton(a_id ? a_id : "", static_cast<ImGuiDir>(a_dir)); }
AMF_EXPORT bool igColorEdit3(const char* a_label, float a_col[3], int a_flags) { return a_col ? ImGui::ColorEdit3(a_label ? a_label : "", a_col, a_flags) : false; }
AMF_EXPORT bool igColorEdit4(const char* a_label, float a_col[4], int a_flags) { return a_col ? ImGui::ColorEdit4(a_label ? a_label : "", a_col, a_flags) : false; }
AMF_EXPORT bool igColorPicker4(const char* a_label, float a_col[4], int a_flags, const float* a_ref) { return a_col ? ImGui::ColorPicker4(a_label ? a_label : "", a_col, a_flags, a_ref) : false; }
AMF_EXPORT bool igColorButton(const char* a_id, const ImVec4 a_col, int a_flags, const ImVec2 a_size) { return ImGui::ColorButton(a_id ? a_id : "", a_col, a_flags, a_size); }
AMF_EXPORT bool igInputDouble(const char* a_label, double* a_v, double a_step, double a_stepFast, const char* a_fmt, int a_flags) { return a_v ? ImGui::InputDouble(a_label ? a_label : "", a_v, a_step, a_stepFast, a_fmt ? a_fmt : "%.6f", a_flags) : false; }
AMF_EXPORT bool igInputInt3(const char* a_label, int a_v[3], int a_flags) { return a_v ? ImGui::InputInt3(a_label ? a_label : "", a_v, a_flags) : false; }
AMF_EXPORT bool igInputScalar(const char* a_label, int a_type, void* a_data, const void* a_step, const void* a_stepFast, const char* a_fmt, int a_flags) { return a_data ? ImGui::InputScalar(a_label ? a_label : "", a_type, a_data, a_step, a_stepFast, a_fmt, a_flags) : false; }
AMF_EXPORT bool igSliderScalar(const char* a_label, int a_type, void* a_data, const void* a_min, const void* a_max, const char* a_fmt, int a_flags) { return (a_data && a_min && a_max) ? ImGui::SliderScalar(a_label ? a_label : "", a_type, a_data, a_min, a_max, a_fmt, a_flags) : false; }
AMF_EXPORT void igPlotLines_FloatPtr(const char* a_label, const float* a_values, int a_count, int a_offset, const char* a_overlay, float a_min, float a_max, ImVec2 a_size, int a_stride)
{
	if (!a_values || a_count <= 0) {
		return;
	}
	ImGui::PlotLines(a_label ? a_label : "", a_values, a_count, a_offset, a_overlay, a_min, a_max, a_size, a_stride > 0 ? a_stride : static_cast<int>(sizeof(float)));
}
AMF_EXPORT bool igIsItemActivated() { return ImGui::IsItemActivated(); }

AMF_EXPORT void igImage(void* a_tex, const ImVec2 a_size, const ImVec2 a_uv0, const ImVec2 a_uv1, const ImVec4 a_tint, const ImVec4 a_border)
{
	if (!a_tex) {
		return;
	}
#if IMGUI_VERSION_NUM >= 19200
	ImGui::ImageWithBg(ToTexture(a_tex), a_size, a_uv0, a_uv1, a_border, a_tint);
#else
	ImGui::Image(ToTexture(a_tex), a_size, a_uv0, a_uv1, a_tint, a_border);
#endif
}

// ---- colours / fonts / style --------------------------------------------------------------------
AMF_EXPORT ImU32 igColorConvertFloat4ToU32(const ImVec4 a_in) { return ImGui::ColorConvertFloat4ToU32(a_in); }
AMF_EXPORT void igColorConvertU32ToFloat4(ImVec4* a_out, ImU32 a_in) { if (a_out) { *a_out = ImGui::ColorConvertU32ToFloat4(a_in); } }
AMF_EXPORT const ImVec4* igGetStyleColorVec4(int a_idx) { return &ImGui::GetStyleColorVec4(compat_enums::MapColour(a_idx)); }
AMF_EXPORT ImFont* igGetFont() { return ImGui::GetFont(); }
AMF_EXPORT const char* igGetKeyName(int a_key) { return ImGui::GetKeyName(static_cast<ImGuiKey>(a_key)); }

// ---- mouse ----------------------------------------------------------------------------------------
AMF_EXPORT void igGetMousePos(ImVec2* a_out) { if (a_out) { *a_out = ImGui::GetMousePos(); } }
AMF_EXPORT bool igIsMouseDown_Nil(int a_button) { return ImGui::IsMouseDown(a_button); }
AMF_EXPORT bool igIsMouseReleased_Nil(int a_button) { return ImGui::IsMouseReleased(a_button); }
AMF_EXPORT bool igIsMouseHoveringRect(const ImVec2 a_min, const ImVec2 a_max, bool a_clip) { return ImGui::IsMouseHoveringRect(a_min, a_max, a_clip); }

// ---- clipboard ------------------------------------------------------------------------------------
AMF_EXPORT void igSetClipboardText(const char* a_text) { if (a_text) { ImGui::SetClipboardText(a_text); } }

// ---- draw list ------------------------------------------------------------------------------------
AMF_EXPORT void ImDrawList_AddImage(ImDrawList* a_list, void* a_tex, const ImVec2 a_min, const ImVec2 a_max, const ImVec2 a_uvMin, const ImVec2 a_uvMax, ImU32 a_col)
{
	if (a_list && a_tex) {
		a_list->AddImage(ToTexture(a_tex), a_min, a_max, a_uvMin, a_uvMax, a_col);
	}
}
AMF_EXPORT void ImDrawList_AddText_FontPtr(ImDrawList* a_list, ImFont* a_font, float a_size, const ImVec2 a_pos, ImU32 a_col, const char* a_begin, const char* a_end, float a_wrap, const ImVec4* a_clip)
{
	if (a_list && a_begin) {
		a_list->AddText(a_font, a_size, a_pos, a_col, a_begin, a_end, a_wrap, a_clip);
	}
}
