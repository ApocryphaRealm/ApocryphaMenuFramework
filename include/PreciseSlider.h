#pragma once

// ============================================================================================================
// A precise slider (the owner, 2026-09-29: "We need to make sure that all of our sliders are precise sliders and they
// don't jump more than one numerical unit per D-pad nudge"). ImGui's SliderBehaviorT moves a keyboard / gamepad nudge by
// 1% of the slider's range whenever the format shows decimals, and also for a "%.0f" slider whose range is wider than
// 100 - so a -150..150 slider stepped 3 per D-pad press. precise::SliderFloat draws ImGui's own slider and, when the
// change came from a keyboard or gamepad nudge (not a mouse drag, and not a number typed in), replaces it with exactly one
// step of the last digit shown: 1 for "%.0f", 0.1 for "%.1f", 0.01 for "%.2f". Held, the nudge repeats as ImGui repeats.
// ============================================================================================================

#include <imgui.h>
#include <imgui_internal.h>

#include "NavSide.h"   // every slider notes when it holds the highlight, so Left adjusts instead of leaving the page (2.3.0)

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <type_traits>

namespace precise
{
	// one unit of the last digit the format shows ("%.0f cm" -> 1, "%.2fx" -> 0.01); 1 when there is no precision
	inline double Step(const char* a_format)
	{
		const char* p = a_format ? std::strstr(a_format, "%.") : nullptr;
		if (p && p[2] >= '0' && p[2] <= '9') {
			return std::pow(10.0, -(p[2] - '0'));
		}
		return 1.0;
	}

	// ---------------------------------------------------------------------------------------------------------
	// The mouse wheel (2.2.0; the owner, testing another mod: "I tried using the scroller on the mouse to move the
	// sliders after clicking the slider, and it didn't scroll it" - and in a bunch of other menus drawn through AMF).
	// One notch over a slider - hovered, or held in a drag - moves it one step: up increases, down decreases, clamped to
	// its range, and the slider call returns true and the item reports itself edited, as a drag does. The slider claims
	// the wheel through ImGui's own key ownership (SetItemKeyOwner), so the window under it does not scroll while the
	// pointer is on it; anywhere else the wheel scrolls the page as before. While the page is mid-scroll (ImGui's wheeling
	// lock, which a pointer move releases) a slider that scrolls under a still pointer does not take the wheel.
	// ---------------------------------------------------------------------------------------------------------

	// The wheel step for a slider drawn from a format alone (a consumer's igSlider*): one unit of the last digit shown.
	// An integer type is always 1; "%.2f" -> 0.01, "%.0f" -> 1, "%d" -> 1; a float format with no precision ("%f", "%g")
	// or no format -> 0.001, the "%.3f" ImGui itself defaults a float slider to. "%%" is a literal percent sign.
	inline double FormatStep(const char* a_format, bool a_integer)
	{
		if (a_integer) return 1.0;
		if (!a_format) return 0.001;
		for (const char* p = a_format; *p; ++p) {
			if (*p != '%') continue;
			if (p[1] == '%') { ++p; continue; }
			++p;
			while (*p && std::strchr("-+ #0'", *p)) ++p;   // flags
			while (*p >= '0' && *p <= '9') ++p;             // width
			if (*p == '.') {
				++p;
				int prec = 0;
				while (*p >= '0' && *p <= '9') { prec = (std::min)(prec * 10 + (*p - '0'), 15); ++p; }
				return std::pow(10.0, -prec);   // "%.f" is precision 0
			}
			return (*p == 'd' || *p == 'i' || *p == 'u') ? 1.0 : 0.001;
		}
		return 0.001;
	}

	// One wheel step from a_v: onto the grid the step makes, one step in a_dir (+1 up, -1 down), clamped to the range
	// (either way round - ImGui allows a slider whose min is above its max).
	inline double WheelStepped(double a_v, double a_step, int a_dir, double a_min, double a_max)
	{
		if (!(a_step > 0.0)) a_step = 1.0;
		double v = (std::round(a_v / a_step) + a_dir) * a_step;
		v = std::round(v / a_step) * a_step;
		return std::clamp(v, (std::min)(a_min, a_max), (std::max)(a_min, a_max));
	}

	// +1, -1 or 0: the wheel's notch for the item just drawn, when it is hovered or held. Claims the wheel for the item
	// either way, so the window under it does not scroll from the next frame on.
	inline int WheelDirection()
	{
		// 2.3.0 null checks: this header also builds off-game (tools\slider_*_check.bat) without AMF's logger, so plain
		// tests rather than NullGuard.h's macros. No ImGui context means no slider was drawn: nothing to do.
		if (!GImGui) return 0;
		ImGuiContext& g = *GImGui;
		const ImGuiID id = g.LastItemData.ID;
		if (id == 0) return 0;
		const bool active = g.ActiveId == id;
		if (!active && !ImGui::IsItemHovered()) return 0;
		if (!active && g.WheelingWindow != nullptr) return 0;   // the page is mid-scroll under a still pointer
		if (ImGui::TempInputIsActive(id)) return 0;             // a number is being typed in
		ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
		const float w = g.IO.MouseWheel;
		return w > 0.0f ? 1 : (w < 0.0f ? -1 : 0);
	}

	namespace detail
	{
		template <class T>
		inline bool Nudge(void* a_data, const void* a_min, const void* a_max, double a_step, int a_dir)
		{
			if (!a_data || !a_min || !a_max) return false;
			T& v = *static_cast<T*>(a_data);
			const double next = WheelStepped(static_cast<double>(v), a_step, a_dir,
				static_cast<double>(*static_cast<const T*>(a_min)), static_cast<double>(*static_cast<const T*>(a_max)));
			const T out = static_cast<T>(std::is_floating_point_v<T> ? next : std::round(next));
			if (out == v) return false;
			v = out;
			return true;
		}

		// A wheel change awaiting its "release" (2.2.0 fix; the owner: "Whenever using the scroller to scroll the slider on
		// the text size, it doesn't actually take effect unless you manually drag it"). A slider whose code applies on
		// IsItemDeactivatedAfterEdit - AMF's Text size rebuilding the atlas, every AMF slider that saves on release, and any
		// consumer doing the same through igIsItemDeactivatedAfterEdit - never saw one from a wheel notch on a hovered slider,
		// because the item was never active, so it was never deactivated. A few slots: one per slider still settling.
		struct WheelPending
		{
			ImGuiContext* ctx = nullptr;
			ImGuiID       id = 0;
			double        last = 0.0;   // ImGui time of the latest notch
		};
		inline WheelPending (&WheelPendings())[8]
		{
			static WheelPending s_pending[8]{};
			return s_pending;
		}
	}

	// How long the wheel must rest on a slider before its change counts as released: one "deactivated after edit" per
	// settled change, so a run of notches rebuilds the Text size atlas (or saves) once, not once per notch.
	inline constexpr double kWheelSettleSeconds = 0.25;

	// The slider item just drawn, every frame, after the wheel: a_ticked when a notch changed its value this frame.
	// - A notch on a hovered slider starts (or restarts) its settle timer; the slider call already returned true.
	// - A notch while the slider is held needs nothing: its release reports the edit, as a drag's does.
	// - Once the wheel has rested kWheelSettleSeconds, the frame the slider is next drawn reports it deactivated after an
	//   edit - IsItemDeactivatedAfterEdit() (and igIsItemDeactivatedAfterEdit, the FLICK slot) are true for it, once.
	// - Grabbed before it settles: the active item is marked edited, so the release reports the wheel's change too.
	// - Another item held meanwhile: the report waits for it to be let go (nothing else's deactivation is touched).
	inline void WheelSettle(bool a_ticked)
	{
		if (!GImGui) return;
		ImGuiContext& g = *GImGui;
		const ImGuiID id = g.LastItemData.ID;
		if (id == 0) return;
		auto& slots = detail::WheelPendings();
		detail::WheelPending* mine = nullptr;
		detail::WheelPending* open = nullptr;
		for (auto& s : slots) {
			if (s.id == id && s.ctx == &g) mine = &s;
			else if (s.id == 0 && !open) open = &s;
		}
		if (a_ticked) {
			if (g.ActiveId == id) { if (mine) mine->id = 0; return; }   // held: the release reports it
			if (!mine) {
				mine = open;
				if (!mine) {   // every slot settling: reuse the oldest
					std::size_t oldest = 0;
					for (std::size_t i = 1; i < std::size(slots); ++i) if (slots[i].last < slots[oldest].last) oldest = i;
					mine = &slots[oldest];
				}
			}
			*mine = { &g, id, g.Time };
			return;
		}
		if (!mine) return;
		if (g.ActiveId == id) { g.ActiveIdHasBeenEditedBefore = true; mine->id = 0; return; }   // grabbed: its release reports it
		if (g.ActiveId != 0) return;                                  // something else is held: wait for it
		if (g.Time - mine->last < kWheelSettleSeconds) return;        // still notching
		mine->id = 0;
		// ImGui's own test, with nothing active: the item reports deactivated, and an edit since the last activation
		g.LastItemData.StatusFlags |= ImGuiItemStatusFlags_HasDeactivated | ImGuiItemStatusFlags_Deactivated;
		g.ActiveIdHasBeenEditedBefore = true;
	}

	// The wheel for the slider item just drawn: one step of a_step, clamped. True when the value changed (the item is
	// then marked edited, as a drag marks it).
	inline bool WheelNudge(ImGuiDataType a_type, void* a_data, const void* a_min, const void* a_max, double a_step)
	{
		// 2.3.0: every slider passes through here right after it is drawn (AMF's precise sliders, converted menus, the
		// consumer slider exports), so this is where a slider tells the side-navigation it holds the highlight (NavSide.h)
		navside::NoteArrowItem();
		if (!GImGui) return false;
		const int dir = WheelDirection();
		if (dir == 0 || !a_data || !a_min || !a_max) { WheelSettle(false); return false; }
		bool changed = false;
		switch (a_type) {
		case ImGuiDataType_S8:     changed = detail::Nudge<ImS8>(a_data, a_min, a_max, a_step, dir); break;
		case ImGuiDataType_U8:     changed = detail::Nudge<ImU8>(a_data, a_min, a_max, a_step, dir); break;
		case ImGuiDataType_S16:    changed = detail::Nudge<ImS16>(a_data, a_min, a_max, a_step, dir); break;
		case ImGuiDataType_U16:    changed = detail::Nudge<ImU16>(a_data, a_min, a_max, a_step, dir); break;
		case ImGuiDataType_S32:    changed = detail::Nudge<ImS32>(a_data, a_min, a_max, a_step, dir); break;
		case ImGuiDataType_U32:    changed = detail::Nudge<ImU32>(a_data, a_min, a_max, a_step, dir); break;
		case ImGuiDataType_S64:    changed = detail::Nudge<ImS64>(a_data, a_min, a_max, a_step, dir); break;
		case ImGuiDataType_U64:    changed = detail::Nudge<ImU64>(a_data, a_min, a_max, a_step, dir); break;
		case ImGuiDataType_Float:  changed = detail::Nudge<float>(a_data, a_min, a_max, a_step, dir); break;
		case ImGuiDataType_Double: changed = detail::Nudge<double>(a_data, a_min, a_max, a_step, dir); break;
		default: WheelSettle(false); return false;
		}
		if (changed) ImGui::MarkItemEdited(GImGui->LastItemData.ID);
		WheelSettle(changed);   // the release a release-applying slider waits for, once the wheel rests
		return changed;
	}

	inline bool IsIntegerType(ImGuiDataType a_type) { return a_type != ImGuiDataType_Float && a_type != ImGuiDataType_Double; }

	// ---------------------------------------------------------------------------------------------------------
	// 2.3.0 ONE WAY for sliders (ONE-WAY-SLIDERS.md): a slider a mod draws through the exports (igSliderFloat / Int / Scalar)
	// steps one unit of its shown digit on a keyboard / D-pad nudge, as AMF's own precise sliders do - Dear ImGui alone moves
	// it 1% of its range for a decimal format. The export knows the format and range; the generated wrapper reads the value
	// before ImGui's slider (SliderValue) and hands it here after (NavNudge). The new value is ASSIGNED from the one before
	// the call - before + one unit, on the format's grid, clamped - never added to what ImGui wrote, so a mod's own precise
	// wrapper around the export, which does the same from the same value, still lands on the same one step.
	// ---------------------------------------------------------------------------------------------------------
	namespace detail
	{
		template <class T>
		inline double Read(const void* a_data) { return a_data ? static_cast<double>(*static_cast<const T*>(a_data)) : 0.0; }

		template <class T>
		inline bool WriteStepped(void* a_data, double a_before, const void* a_min, const void* a_max, double a_step, int a_dir)
		{
			if (!a_data || !a_min || !a_max) return false;
			T& v = *static_cast<T*>(a_data);
			const double next = WheelStepped(a_before, a_step, a_dir, static_cast<double>(*static_cast<const T*>(a_min)),
				static_cast<double>(*static_cast<const T*>(a_max)));
			const T out = static_cast<T>(std::is_floating_point_v<T> ? next : std::round(next));
			if (out == v) return false;
			v = out;
			return true;
		}
	}

	// The slider's value as a double (0 for no value or an unknown type).
	inline double SliderValue(ImGuiDataType a_type, const void* a_data)
	{
		if (!a_data) return 0.0;
		switch (a_type) {
		case ImGuiDataType_S8:     return detail::Read<ImS8>(a_data);
		case ImGuiDataType_U8:     return detail::Read<ImU8>(a_data);
		case ImGuiDataType_S16:    return detail::Read<ImS16>(a_data);
		case ImGuiDataType_U16:    return detail::Read<ImU16>(a_data);
		case ImGuiDataType_S32:    return detail::Read<ImS32>(a_data);
		case ImGuiDataType_U32:    return detail::Read<ImU32>(a_data);
		case ImGuiDataType_S64:    return detail::Read<ImS64>(a_data);
		case ImGuiDataType_U64:    return detail::Read<ImU64>(a_data);
		case ImGuiDataType_Float:  return detail::Read<float>(a_data);
		case ImGuiDataType_Double: return detail::Read<double>(a_data);
		default:                   return 0.0;
		}
	}

	// For the (horizontal) slider item just drawn: when it is active from the keyboard or pad (not dragged, no number being
	// typed, not read-only) and a Left / Right tweak ticked this frame, its value becomes a_before one step that way. True
	// when the value changed here.
	inline bool NavNudge(ImGuiDataType a_type, void* a_data, double a_before, const void* a_min, const void* a_max, double a_step,
		ImGuiSliderFlags a_flags)
	{
		if (!GImGui || !a_data || !a_min || !a_max) return false;
		ImGuiContext& g = *GImGui;
		const ImGuiID id = g.LastItemData.ID;
		if (id == 0 || g.ActiveId != id) return false;
		if (g.ActiveIdSource != ImGuiInputSource_Keyboard && g.ActiveIdSource != ImGuiInputSource_Gamepad) return false;
		if (ImGui::TempInputIsActive(id)) return false;
		if ((a_flags & ImGuiSliderFlags_ReadOnly) != 0 || (g.LastItemData.InFlags & ImGuiItemFlags_ReadOnly) != 0) return false;
		const float amount = ImGui::GetNavTweakPressedAmount(ImGuiAxis_X);
		if (amount == 0.0f) return false;
		const int dir = amount > 0.0f ? 1 : -1;
		switch (a_type) {
		case ImGuiDataType_S8:     return detail::WriteStepped<ImS8>(a_data, a_before, a_min, a_max, a_step, dir);
		case ImGuiDataType_U8:     return detail::WriteStepped<ImU8>(a_data, a_before, a_min, a_max, a_step, dir);
		case ImGuiDataType_S16:    return detail::WriteStepped<ImS16>(a_data, a_before, a_min, a_max, a_step, dir);
		case ImGuiDataType_U16:    return detail::WriteStepped<ImU16>(a_data, a_before, a_min, a_max, a_step, dir);
		case ImGuiDataType_S32:    return detail::WriteStepped<ImS32>(a_data, a_before, a_min, a_max, a_step, dir);
		case ImGuiDataType_U32:    return detail::WriteStepped<ImU32>(a_data, a_before, a_min, a_max, a_step, dir);
		case ImGuiDataType_S64:    return detail::WriteStepped<ImS64>(a_data, a_before, a_min, a_max, a_step, dir);
		case ImGuiDataType_U64:    return detail::WriteStepped<ImU64>(a_data, a_before, a_min, a_max, a_step, dir);
		case ImGuiDataType_Float:  return detail::WriteStepped<float>(a_data, a_before, a_min, a_max, a_step, dir);
		case ImGuiDataType_Double: return detail::WriteStepped<double>(a_data, a_before, a_min, a_max, a_step, dir);
		default:                   return false;
		}
	}

	// SliderAngle: the value is radians, the range and format are degrees ("%.0f deg" when none is given).
	inline bool WheelNudgeAngle(float* a_rad, float a_minDeg, float a_maxDeg, const char* a_format)
	{
		if (!a_rad) return false;
		float deg = *a_rad * 360.0f / (2.0f * IM_PI);
		if (!WheelNudge(ImGuiDataType_Float, &deg, &a_minDeg, &a_maxDeg, FormatStep(a_format ? a_format : "%.0f deg", false))) return false;
		*a_rad = deg * (2.0f * IM_PI) / 360.0f;
		return true;
	}

	// 2.2.0 (HadToRegister, 2026-10-12: the value is hard to read where the handle sits under it): the slider's value drawn
	// again over itself with a thin outline in the window's own colour, so the number reads over the grab in every theme.
	// The same text, place and clipping as ImGui's own (SliderScalar: DataTypeFormatString, then RenderTextClipped centred in
	// the frame - the frame is the item's nav rect). Not while a number is being typed into the slider.
	inline void OutlineValue(ImGuiDataType a_type, const void* a_data, const char* a_format)
	{
		if (!GImGui) return;
		ImGuiContext& g = *GImGui;
		ImGuiWindow* const window = g.CurrentWindow;
		if (!window || window->SkipItems || !a_data || !ImGui::IsItemVisible()) return;
		const ImGuiID id = g.LastItemData.ID;
		if (id == 0 || ImGui::TempInputIsActive(id)) return;
		ImRect frame = g.LastItemData.NavRect;
		if (frame.GetWidth() <= 0.0f || frame.GetHeight() <= 0.0f) return;
#ifdef AMF_IMGUI_SLIDER_VALUE_RECT
		// 2.3.0: AMF's ImGui moves a value the grab would cover to the side of the grab - redraw it where it was drawn
		if (GImGuiSliderValueId == id && GImGuiSliderValueRect.GetWidth() > 0.0f) { frame = GImGuiSliderValueRect; }
#endif
		char buf[64];
		const ImGuiDataTypeInfo* const info = a_format ? nullptr : ImGui::DataTypeGetInfo(a_type);
		const char* const fmt = a_format ? a_format : (info ? info->PrintFmt : nullptr);
		if (!fmt) return;
		const char* const end = buf + ImGui::DataTypeFormatString(buf, IM_ARRAYSIZE(buf), a_type, a_data, fmt);
		ImVec4 edge = g.Style.Colors[ImGuiCol_WindowBg];
		edge.w = g.Style.Colors[ImGuiCol_Text].w;
		const float t = (std::max)(1.0f, std::floor(g.FontSize / 18.0f));
		const ImVec2 offsets[] = { ImVec2(-t, 0.0f), ImVec2(t, 0.0f), ImVec2(0.0f, -t), ImVec2(0.0f, t),
								   ImVec2(-t, -t), ImVec2(t, -t), ImVec2(-t, t), ImVec2(t, t) };
		ImGui::PushStyleColor(ImGuiCol_Text, edge);
		for (const ImVec2& o : offsets) {
			ImGui::RenderTextClipped(ImVec2(frame.Min.x + o.x, frame.Min.y + o.y), ImVec2(frame.Max.x + o.x, frame.Max.y + o.y), buf, end,
									 nullptr, ImVec2(0.5f, 0.5f), &frame);
		}
		ImGui::PopStyleColor();
		ImGui::RenderTextClipped(frame.Min, frame.Max, buf, end, nullptr, ImVec2(0.5f, 0.5f), &frame);
	}

	// 2.3.0 (retest batch B-20261025-053652-453d, c0a3 U3: typing into Window opacity re-applied the theme once per KEYSTROKE
	// before Enter). For a slider whose change is heavy to apply (the theme), call right after it with what it returned: true
	// for a change to apply now - a drag, a nudge, a wheel notch, and a typed number once it settles (Enter, or the field let
	// go) - and false for each keystroke of a number still being typed (Ctrl+click), whose value is in the variable already.
	// 2.3.0 (combined run 3266, S10: each TAP on Window opacity applied the theme twice - on the press, which stepped it, and
	// again on the release, when the one-way step let it go and ImGui reported "deactivated after edit"): the let-go applies
	// only a change that was held back - a typed number - never one already applied. One apply a tap, one a typed entry, one a
	// step while held, live while dragging or wheeling (tools\nav_left_check.cpp 8b S10, tools\slider_wheel_check).
	namespace detail
	{
		inline ImGuiID g_applyHeldBack = 0;   // the item whose typed number has not been applied yet
	}

	inline bool ApplyNow(bool a_changed)
	{
		if (!GImGui) return a_changed;
		const ImGuiID id = GImGui->LastItemData.ID;
		const bool typing = id != 0 && ImGui::TempInputIsActive(id);
		if (a_changed && typing)
		{
			detail::g_applyHeldBack = id;   // a keystroke of a number still being typed: held back until it settles
			return false;
		}
		if (a_changed)
		{
			if (detail::g_applyHeldBack == id) { detail::g_applyHeldBack = 0; }
			return true;
		}
		if (id != 0 && detail::g_applyHeldBack == id && ImGui::IsItemDeactivatedAfterEdit())
		{
			detail::g_applyHeldBack = 0;   // the typed number settled (Enter, or the field let go): apply it once
			return true;
		}
		return false;
	}

	inline bool SliderFloat(const char* a_label, float* a_v, float a_min, float a_max, const char* a_format)
	{
		if (!a_v || !GImGui) return false;   // no value to edit (ImGui's own slider would dereference it), or no context
		const float before = *a_v;
		// 2.3.0 (gate row precise-sliders-always-clamp; Dragon's Eye Minimap 7c3ce18, test d844): AlwaysClamp, so a number typed
		// in (Ctrl+click) is clamped INSIDE ImGui - 1.90.8 otherwise leaves it unclamped and, while the text field is open,
		// copies the typed text back over the value every frame the two differ ('changed' every frame); clamped again below
		bool changed = ImGui::SliderFloat(a_label, a_v, a_min, a_max, a_format, ImGuiSliderFlags_AlwaysClamp);
		if (changed && a_min <= a_max) { *a_v = std::clamp(*a_v, a_min, a_max); }
		if (changed) {
			ImGuiContext& g = *GImGui;
			const ImGuiID id = ImGui::GetItemID();
			const bool nav = g.ActiveId == id && (g.ActiveIdSource == ImGuiInputSource_Keyboard || g.ActiveIdSource == ImGuiInputSource_Gamepad);
			const bool typing = g.InputTextState.ID == id && ImGui::TempInputIsActive(id);
			if (nav && !typing && *a_v != before) {
				const double step = Step(a_format);
				double v = static_cast<double>(before) + (*a_v > before ? step : -step);
				v = std::round(v / step) * step;   // on the grid the format shows
				*a_v = static_cast<float>(std::clamp(v, static_cast<double>(a_min), static_cast<double>(a_max)));
			}
		}
		// the wheel: one step of the shown digit, the same step as a key nudge
		if (WheelNudge(ImGuiDataType_Float, a_v, &a_min, &a_max, Step(a_format))) changed = true;
		OutlineValue(ImGuiDataType_Float, a_v, a_format);   // readable over the grab
		return changed;
	}

	inline bool SliderInt(const char* a_label, int* a_v, int a_min, int a_max, const char* a_format = "%d")
	{
		if (!a_v || !GImGui) return false;   // as SliderFloat
		const int  before = *a_v;
		bool changed = ImGui::SliderInt(a_label, a_v, a_min, a_max, a_format, ImGuiSliderFlags_AlwaysClamp);   // as SliderFloat
		if (changed && a_min <= a_max) { *a_v = std::clamp(*a_v, a_min, a_max); }
		if (changed) {
			ImGuiContext& g = *GImGui;
			const ImGuiID id = ImGui::GetItemID();
			const bool nav = g.ActiveId == id && (g.ActiveIdSource == ImGuiInputSource_Keyboard || g.ActiveIdSource == ImGuiInputSource_Gamepad);
			const bool typing = g.InputTextState.ID == id && ImGui::TempInputIsActive(id);
			if (nav && !typing && *a_v != before) {
				*a_v = std::clamp(before + (*a_v > before ? 1 : -1), a_min, a_max);
			}
		}
		if (WheelNudge(ImGuiDataType_S32, a_v, &a_min, &a_max, 1.0)) changed = true;   // the wheel: one step
		OutlineValue(ImGuiDataType_S32, a_v, a_format);   // readable over the grab
		return changed;
	}
}
