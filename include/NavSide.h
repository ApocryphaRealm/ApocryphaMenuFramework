#pragma once

// ============================================================================================================
// When a sideways press inside a mod's page may send the highlight back to the mod list (2.3.0).
//
// The menu's rule since 1.8.8: Left at a row with nothing to its left goes back to the list. Until 2.3.0 the only guard
// was "no item is active" (ImGui::IsAnyItemActive). A slider with the highlight on it is NOT active - ImGui activates a
// slider only on A / Enter / a held click - so a Left on a slider found nothing to its left and left the page, while a
// page that steps the slider itself on Left (Dragon's Eye Minimap's held arrows) kept stepping it too (combined run
// 2026-10-25 1515: any Left on DEM's sliders and on AMF Example's plain Speed slider moved focus to the list).
//
// Now a Left press leaves the page only when ALL of these hold:
//   - the page has the highlight and nothing is being edited (as before);
//   - the highlight is not inside a popup (an open combo, a context menu);
//   - the highlighted item is not a widget that Left/Right adjust - any slider or drag, AMF's own, a converted menu's
//     or a mod's through the exports. Each of those calls NoteArrowItem() right after it is drawn;
//   - the page did not take the press itself - ImGui made a Left move request this frame and a widget cancelled it
//     (NavMoveRequestCancel), which is how a page that steps its own value on Left says "mine".
// Left on a plain row (a switch, a button, a text line, a key binding) still goes back to the list.
// The decision itself is LeftPressMayLeave() - a pure function, proven off-game by tools\nav_left_check.bat.
// ============================================================================================================

#include <imgui.h>
#include <imgui_internal.h>

namespace navside
{
	struct LeftPress
	{
		bool contentHasNav  = false;   // the mod page (content pane) holds ImGui's nav highlight
		bool editing        = false;   // ImGui::IsAnyItemActive(): a slider held, a text field typing
		bool inPopup        = false;   // the highlight is in a popup window (an open combo, a context menu)
		bool onArrowItem    = false;   // the highlighted item is a slider or drag (NoteArrowItem saw it this frame)
		bool pageTookTheKey = false;   // a widget cancelled ImGui's Left move this frame - it used the press itself
	};

	// True when this Left press may send the highlight back to the mod list (once ImGui has also found nothing to the left).
	inline bool LeftPressMayLeave(const LeftPress& a_p)
	{
		return a_p.contentHasNav && !a_p.editing && !a_p.inPopup && !a_p.onArrowItem && !a_p.pageTookTheKey;
	}

	namespace detail
	{
		inline ImGuiContext* g_ctx = nullptr;   // the context the note below was taken in
		inline int     g_frame = -1;            // the frame an arrow-adjusted item was seen holding the highlight
		inline ImGuiID g_navId = 0;             // ImGui's NavId at that moment
	}

	// Call right after drawing a widget that Left/Right adjust (a slider, a drag). Notes it when ImGui's nav highlight is
	// on it - on the item itself, or on one of its parts for a multi-value widget (SliderFloat3's fields, a drag range),
	// whose last item is the group around them.
	inline void NoteArrowItem()
	{
		ImGuiContext* const g = GImGui;
		if (!g || g->NavId == 0 || !g->CurrentWindow || g->NavWindow != g->CurrentWindow) return;
		bool on = g->LastItemData.ID != 0 && g->LastItemData.ID == g->NavId;
		if (!on && g->NavIdIsAlive)
		{
			ImGuiWindow* const w = g->CurrentWindow;
			const ImRect nav = ImGui::WindowRectRelToAbs(w, w->NavRectRel[g->NavLayer]);
			on = g->LastItemData.Rect.Contains(nav);
		}
		if (on)
		{
			detail::g_ctx = g;
			detail::g_frame = g->FrameCount;
			detail::g_navId = g->NavId;
		}
	}

	// This frame, after the page is drawn: is the highlight on a slider or drag?
	inline bool NavOnArrowItem()
	{
		const ImGuiContext* const g = GImGui;
		return g && detail::g_ctx == g && detail::g_frame == g->FrameCount && detail::g_navId != 0 && detail::g_navId == g->NavId;
	}

	// This frame, after the page is drawn: is the highlight inside a popup (an open combo, a context menu, a modal)?
	inline bool NavInPopup()
	{
		const ImGuiContext* const g = GImGui;
		return g && g->NavWindow && (g->NavWindow->Flags & ImGuiWindowFlags_Popup) != 0;
	}

	// This frame, after the page is drawn: ImGui made a move request in a_dir and a widget cancelled it (it used the press) -
	// a page stepping its own value, or ImGui's own tree node closing on Left. A request FORWARDED to the next frame (a
	// wrap-around) also passes through a cancel, but it is still moving, so it does not count.
	inline bool PageTookTheMove(ImGuiDir a_dir)
	{
		const ImGuiContext* const g = GImGui;
		return g && g->NavMoveDir == a_dir && !g->NavMoveSubmitted && !g->NavMoveForwardToNextFrame;
	}
}
