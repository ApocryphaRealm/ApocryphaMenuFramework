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
//   - the highlighted item is not a widget that Left/Right adjust - any slider, drag or text field, AMF's own, a converted
//     menu's, a mod's through the exports, or a mod's drawn by its own copy of Dear ImGui. AMF's own and the exports call
//     NoteArrowItem() right after drawing one; every other is known from ImGui's own item state (LearnNavItemKind);
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

	// ---------------------------------------------------------------------------------------------------------------------
	// Items known from ImGui's OWN state (2.3.0, retest batch B-20261025-053652-453d, 26d2 N1-N5: AMF Example's Speed slider
	// is drawn by AMF Example's own copy of Dear ImGui on the framework's context - precise::SliderFloat from the SDK, which
	// never calls NoteArrowItem - so Left on it still left the page). Any code drawing on the context leaves the item's kind in
	// shared state, whichever copy of ImGui drew it:
	//   - a nav move, tab or init result that landed on the item carries its item flags (ImGuiNavItemData::InFlags), and
	//     sliders, drags and text fields are added with ImGuiItemFlags_Inputable (a NoInput slider is not);
	//   - the last item a page drew, when it is the highlighted one (g.LastItemData.InFlags);
	//   - an item that is being held or edited and has claimed Left and Right for itself (g.ActiveIdUsingNavDirMask - set by
	//     every slider, drag and text field, NoInput sliders included, when it is clicked or activated).
	// An ImGui id names one item, so a kind once learnt holds until the context changes; a small ring of ids is plenty (only
	// the highlighted item is ever asked about).
	// ---------------------------------------------------------------------------------------------------------------------
	namespace detail
	{
		inline ImGuiContext* g_kindCtx = nullptr;
		inline ImGuiID       g_arrowIds[64] = {};
		inline int           g_arrowNext = 0;

		inline bool KnownArrow(ImGuiID a_id)
		{
			if (a_id == 0) return false;
			for (const ImGuiID id : g_arrowIds) { if (id == a_id) return true; }
			return false;
		}

		inline void LearnArrow(ImGuiID a_id)
		{
			if (a_id == 0 || KnownArrow(a_id)) return;
			g_arrowIds[g_arrowNext] = a_id;
			g_arrowNext = (g_arrowNext + 1) % static_cast<int>(sizeof(g_arrowIds) / sizeof(g_arrowIds[0]));
		}
	}

	// Every frame, after the pages are drawn (Renderer.cpp, before the Left decision; tools\nav_left_check.cpp per frame).
	inline void LearnNavItemKind()
	{
		ImGuiContext* const g = GImGui;
		if (!g) return;
		if (detail::g_kindCtx != g)
		{
			detail::g_kindCtx = g;
			for (ImGuiID& id : detail::g_arrowIds) { id = 0; }
			detail::g_arrowNext = 0;
		}
		const ImGuiID nav = g->NavId;
		if (nav == 0) return;
		const ImGuiNavItemData* const results[] = { &g->NavMoveResultLocal, &g->NavMoveResultLocalVisible, &g->NavMoveResultOther,
													&g->NavTabbingResultFirst, &g->NavInitResult };
		for (const ImGuiNavItemData* r : results)
		{
			if (r && r->ID == nav && (r->InFlags & ImGuiItemFlags_Inputable) != 0) { detail::LearnArrow(nav); }
		}
		if (g->LastItemData.ID == nav && (g->LastItemData.InFlags & ImGuiItemFlags_Inputable) != 0) { detail::LearnArrow(nav); }
		constexpr int kLeftRight = (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
		if (g->ActiveId == nav && (g->ActiveIdUsingNavDirMask & kLeftRight) == kLeftRight) { detail::LearnArrow(nav); }
	}

	// The highlighted item is a slider, drag or text field by ImGui's own state (LearnNavItemKind has seen it).
	inline bool NavOnKnownArrowItem()
	{
		const ImGuiContext* const g = GImGui;
		return g && detail::g_kindCtx == g && detail::KnownArrow(g->NavId);
	}

	// This frame, after the page is drawn: is the highlight on a slider or drag (noted by NoteArrowItem as it was drawn, or
	// known from ImGui's own state)?
	inline bool NavOnArrowItem()
	{
		const ImGuiContext* const g = GImGui;
		if (!g) return false;
		const bool noted = detail::g_ctx == g && detail::g_frame == g->FrameCount && detail::g_navId != 0 && detail::g_navId == g->NavId;
		return noted || NavOnKnownArrowItem();
	}

	// Right after ImGui::NewFrame, before any page draws: keyboard Enter on a slider or drag ACTIVATES it (Left / Right then
	// step it, Enter again lets go), the way Space and the pad's A do, instead of opening its number box (26d2 N3: "Enter on
	// Speed opened the slider's TEXT BOX instead of activating it"). ImGui 1.90.8 turns Enter into "prefer input"
	// (NavUpdate: ImGuiActivateFlags_PreferInput), which a slider answers with its text box; here that one press is turned
	// into the tweak Space gives. A number is still typed with Ctrl+click. A text field is unaffected: it starts editing on
	// a keyboard activation either way (InputTextEx: input_requested_by_nav). The pad's Input button keeps its meaning, and
	// Tab's activation (ImGuiActivateFlags_FromTabbing) is left alone. True when the press was changed.
	inline bool EnterActivatesArrowItem()
	{
		ImGuiContext* const g = GImGui;
		if (!g || g->NavActivateId == 0 || g->NavActivateId != g->NavId) return false;
		if ((g->NavActivateFlags & ImGuiActivateFlags_PreferInput) == 0 || (g->NavActivateFlags & ImGuiActivateFlags_FromTabbing) != 0) return false;
		const bool enter = ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false);
		if (!enter || ImGui::IsKeyPressed(ImGuiKey_NavGamepadInput, false)) return false;
		if (!NavOnKnownArrowItem() && !(detail::g_ctx == g && detail::g_navId == g->NavId && detail::g_frame == g->FrameCount - 1)) return false;
		g->NavActivateFlags = (g->NavActivateFlags & ~ImGuiActivateFlags_PreferInput) | ImGuiActivateFlags_PreferTweak;
		return true;
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
