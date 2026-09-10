// ============================================================================================
// WIDGET EXPORTS, FIFTH TRANCHE (1.7.1) - the gap a real bug report finally exposed.
//
// WHY THIS FILE EXISTS
//   A user reported on 2026-09-10 that Cinematic Idle Camera's settings page "show up as blank".
//   It is not blank because anything failed loudly. An SMF consumer does not link against the
//   framework: it resolves EVERY function it draws with by name through GetProcAddress, and that
//   includes the whole cimgui surface. So a name this framework does not export comes back null,
//   the page still registers (AddSectionItem exists), the callback still runs, and the part of it
//   that needed the missing function simply draws nothing. A registered page that draws nothing is
//   exactly what the reporter saw.
//
//   The owner's instruction, the same day: "just get all the exports from SMF and add them to AMF
//   as they're just exports they're not proprietary and this is just going to keep happening unless
//   we get all the exports." He is right about the pattern - this is the THIRD time names have been
//   added by scanning consumers (1.5.9 did 65 of them from a 58-mod checklist), and each pass only
//   ever covers the mods somebody happened to look at.
//
// HOW THIS TRANCHE WAS CHOSEN - measured, not guessed
//   Export tables were parsed from both DLLs: SKSE Menu Framework exports 1,420 names, this
//   framework exported 214. Every SKSE plugin on this machine that contains the string
//   "SKSEMenuFramework" - 139 of them across both MO2 instances - was then scanned for names that
//   exist in SMF's export table and not in ours, plus cinematicidlecamera.dll, downloaded for the
//   report. 135 of the 139 were already fully covered. Four were not, and the union of what they
//   need is the 38 functions below.
//
//   The raw 1,217-name gap badly overstates the real one: most of it is cimgui's internal struct
//   accessors (ImBitVector_*, ImGuiDockNode_*, ImGuiInputTextState_* and the like) that no ordinary
//   consumer touches. Closing the whole 1,420 is a different job, and the right mechanism for it is
//   vendoring cimgui rather than hand-writing wrappers - see the note at the bottom of this file.
//
// THE RULES, unchanged from the earlier tranches
//   * ImVec2/ImVec4 cross BY VALUE, except where cimgui returns one - those take a pOut pointer
//     as the first argument and write through it. Getting this backwards does not fail visibly,
//     it corrupts the call, so each pOut case below is marked.
//   * Any pointer that could be null is checked, because the stock consumer header calls whatever
//     it got back without looking.
//   * *V names take a va_list.
// ============================================================================================

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdarg>
#include <cstddef>

#define AMF_EXPORT extern "C" __declspec(dllexport)

// ---------------------------------------------------------------------------- layout and cursor
// The Y-axis and whole-size counterparts of names this framework already had. igGetCursorPosX,
// igSetCursorPosX, igGetWindowWidth and igGetTextLineHeightWithSpacing were all present; their
// partners were simply never written, which is what emptied the reporter's page.
AMF_EXPORT float igGetCursorPosY() { return ImGui::GetCursorPosY(); }
AMF_EXPORT void  igSetCursorPosY(float a_y) { ImGui::SetCursorPosY(a_y); }
AMF_EXPORT float igGetTextLineHeight() { return ImGui::GetTextLineHeight(); }
AMF_EXPORT float igGetColumnWidth(int a_column) { return ImGui::GetColumnWidth(a_column); }

// pOut cases: cimgui returns these as a written-through pointer, never by value.
AMF_EXPORT void igGetWindowPos(ImVec2* a_out) { if (a_out) { *a_out = ImGui::GetWindowPos(); } }
AMF_EXPORT void igGetWindowSize(ImVec2* a_out) { if (a_out) { *a_out = ImGui::GetWindowSize(); } }
AMF_EXPORT void igGetWindowContentRegionMax(ImVec2* a_out)
{
	if (a_out) { *a_out = ImGui::GetWindowContentRegionMax(); }
}

AMF_EXPORT void igSetWindowSize_Vec2(const ImVec2 a_size, int a_cond)
{
	ImGui::SetWindowSize(a_size, static_cast<ImGuiCond>(a_cond));
}
AMF_EXPORT void igSetNextWindowViewport(unsigned int a_viewportId)
{
	ImGui::SetNextWindowViewport(static_cast<ImGuiID>(a_viewportId));
}

// The one that actually emptied the page: a consumer whose whole body is inside a child region
// gets no body at all when this cannot be called. The _Str form was already exported; this is the
// same call keyed by id instead of by string.
AMF_EXPORT bool igBeginChild_ID(unsigned int a_id, const ImVec2 a_size, int a_childFlags, int a_windowFlags)
{
	return ImGui::BeginChild(static_cast<ImGuiID>(a_id), a_size,
							 static_cast<ImGuiChildFlags>(a_childFlags),
							 static_cast<ImGuiWindowFlags>(a_windowFlags));
}

// ---------------------------------------------------------------------------- state queries
AMF_EXPORT bool igIsItemFocused() { return ImGui::IsItemFocused(); }
AMF_EXPORT bool igIsAnyItemActive() { return ImGui::IsAnyItemActive(); }
AMF_EXPORT bool igIsWindowHovered(int a_flags)
{
	return ImGui::IsWindowHovered(static_cast<ImGuiHoveredFlags>(a_flags));
}
AMF_EXPORT bool igIsPopupOpen_Str(const char* a_id, int a_flags)
{
	return ImGui::IsPopupOpen(a_id ? a_id : "", static_cast<ImGuiPopupFlags>(a_flags));
}
AMF_EXPORT unsigned int igGetColorU32_Col(int a_idx, float a_alphaMul)
{
	return ImGui::GetColorU32(static_cast<ImGuiCol>(a_idx), a_alphaMul);
}
AMF_EXPORT void igSetMouseCursor(int a_cursor)
{
	ImGui::SetMouseCursor(static_cast<ImGuiMouseCursor>(a_cursor));
}
AMF_EXPORT void igSetNextFrameWantCaptureKeyboard(bool a_want) { ImGui::SetNextFrameWantCaptureKeyboard(a_want); }
AMF_EXPORT void igSetNextFrameWantCaptureMouse(bool a_want) { ImGui::SetNextFrameWantCaptureMouse(a_want); }

// ---------------------------------------------------------------------------- widgets
AMF_EXPORT bool igCombo_FnStrPtr(const char* a_label, int* a_current,
								 const char* (*a_getter)(void*, int), void* a_userData,
								 int a_count, int a_popupMax)
{
	if (!a_current || !a_getter) { return false; }
	return ImGui::Combo(a_label ? a_label : "", a_current, a_getter, a_userData, a_count, a_popupMax);
}
AMF_EXPORT bool igTreeNodeV_Str(const char* a_id, const char* a_fmt, va_list a_args)
{
	return ImGui::TreeNodeV(a_id ? a_id : "", a_fmt ? a_fmt : "", a_args);
}

// ---------------------------------------------------------------------------- draw list
// Every one takes the consumer's own ImDrawList*, which is a real pointer into this framework's
// ImGui handed out earlier by igGetWindowDrawList, so it is checked before use.
AMF_EXPORT void ImDrawList_AddCircle(ImDrawList* a_self, const ImVec2 a_centre, float a_radius,
									 unsigned int a_col, int a_segments, float a_thickness)
{
	if (a_self) { a_self->AddCircle(a_centre, a_radius, a_col, a_segments, a_thickness); }
}
AMF_EXPORT void ImDrawList_AddTriangle(ImDrawList* a_self, const ImVec2 a_p1, const ImVec2 a_p2,
									   const ImVec2 a_p3, unsigned int a_col, float a_thickness)
{
	if (a_self) { a_self->AddTriangle(a_p1, a_p2, a_p3, a_col, a_thickness); }
}
AMF_EXPORT void ImDrawList_AddRectFilledMultiColor(ImDrawList* a_self, const ImVec2 a_min, const ImVec2 a_max,
												   unsigned int a_upperLeft, unsigned int a_upperRight,
												   unsigned int a_lowerRight, unsigned int a_lowerLeft)
{
	if (a_self)
	{
		a_self->AddRectFilledMultiColor(a_min, a_max, a_upperLeft, a_upperRight, a_lowerRight, a_lowerLeft);
	}
}
AMF_EXPORT void ImDrawList_AddImageRounded(ImDrawList* a_self, ImTextureID a_texture,
										   const ImVec2 a_min, const ImVec2 a_max,
										   const ImVec2 a_uvMin, const ImVec2 a_uvMax,
										   unsigned int a_col, float a_rounding, int a_flags)
{
	if (a_self)
	{
		a_self->AddImageRounded(a_texture, a_min, a_max, a_uvMin, a_uvMax, a_col, a_rounding,
								static_cast<ImDrawFlags>(a_flags));
	}
}
AMF_EXPORT void ImDrawList_PushClipRect(ImDrawList* a_self, const ImVec2 a_min, const ImVec2 a_max,
										bool a_intersectWithCurrent)
{
	if (a_self) { a_self->PushClipRect(a_min, a_max, a_intersectWithCurrent); }
}
AMF_EXPORT void ImDrawList_PopClipRect(ImDrawList* a_self)
{
	if (a_self) { a_self->PopClipRect(); }
}

// ---------------------------------------------------------------------------- IO event injection
// A consumer that drives input itself (EnchantMenu does) pushes events into the IO it was handed.
// The pointer is this framework's own ImGuiIO, so it is checked rather than trusted.
AMF_EXPORT void ImGuiIO_AddInputCharacter(ImGuiIO* a_self, unsigned int a_c)
{
	if (a_self) { a_self->AddInputCharacter(a_c); }
}
AMF_EXPORT void ImGuiIO_AddKeyEvent(ImGuiIO* a_self, int a_key, bool a_down)
{
	if (a_self) { a_self->AddKeyEvent(static_cast<ImGuiKey>(a_key), a_down); }
}
AMF_EXPORT void ImGuiIO_AddMouseButtonEvent(ImGuiIO* a_self, int a_button, bool a_down)
{
	if (a_self) { a_self->AddMouseButtonEvent(a_button, a_down); }
}
AMF_EXPORT void ImGuiIO_AddMouseWheelEvent(ImGuiIO* a_self, float a_x, float a_y)
{
	if (a_self) { a_self->AddMouseWheelEvent(a_x, a_y); }
}
AMF_EXPORT void ImGuiIO_AddMouseSourceEvent(ImGuiIO* a_self, int a_source)
{
	if (a_self) { a_self->AddMouseSourceEvent(static_cast<ImGuiMouseSource>(a_source)); }
}
AMF_EXPORT void ImGuiIO_SetKeyEventNativeData(ImGuiIO* a_self, int a_key, int a_nativeKeycode,
											  int a_nativeScancode, int a_nativeLegacyIndex)
{
	if (a_self)
	{
		a_self->SetKeyEventNativeData(static_cast<ImGuiKey>(a_key), a_nativeKeycode, a_nativeScancode,
									  a_nativeLegacyIndex);
	}
}

// ---------------------------------------------------------------------------- imgui_internal
// These five are imgui_internal.h, not the public API. They are exported because SMF exports them
// and a consumer that resolved them from SMF will resolve them here - EnchantMenu does. They hand
// out real pointers into this framework's ImGui state, so they are the ones to look at first if a
// consumer ever misbehaves rather than merely fails to draw.
AMF_EXPORT ImGuiWindow* igGetCurrentWindow() { return ImGui::GetCurrentWindow(); }
AMF_EXPORT void         igClearActiveID() { ImGui::ClearActiveID(); }
AMF_EXPORT void         igClosePopupToLevel(int a_remaining, bool a_restoreFocus)
{
	ImGui::ClosePopupToLevel(a_remaining, a_restoreFocus);
}
AMF_EXPORT void igBringWindowToDisplayFront(ImGuiWindow* a_window)
{
	if (a_window) { ImGui::BringWindowToDisplayFront(a_window); }
}
AMF_EXPORT bool igIsPopupOpen_ID(unsigned int a_id, int a_flags)
{
	return ImGui::IsPopupOpen(static_cast<ImGuiID>(a_id), static_cast<ImGuiPopupFlags>(a_flags));
}
AMF_EXPORT unsigned int igImHashStr(const char* a_data, std::size_t a_size, unsigned int a_seed)
{
	return a_data ? ImHashStr(a_data, a_size, static_cast<ImGuiID>(a_seed)) : 0u;
}

// ============================================================================================
// ON CLOSING THE WHOLE GAP RATHER THAN THE MEASURED ONE
//
// This tranche covers every SMF consumer visible on this machine, and the previous two tranches
// were chosen the same way - which is why a fourth was needed. The durable answer is not another
// scan: it is to stop hand-writing wrappers and vendor CIMGUI, the generated C wrapper this whole
// ABI comes from. It is MIT, it is generated per Dear ImGui version, and building its cimgui.cpp
// against the 1.90.8-docking build already embedded here would produce all ~1,420 names at once,
// correct by construction.
//
// Two things have to be settled before that is done, which is why it is not done here:
//   * The ~190 wrappers already hand-written in Compat.cpp / CompatWidgets.cpp / CompatWidgets2.cpp
//     define the same symbols and would collide at link time. They would have to go.
//   * Those hand-written ones null-check every pointer and string, and cimgui's generated ones do
//     not. Swapping them in trades a consumer's blank page for a consumer's crash, so the guards
//     would need re-adding as a thin layer in front of cimgui rather than simply dropped.
// ============================================================================================
