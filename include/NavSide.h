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

	// ---------------------------------------------------------------------------------------------------------------------
	// ONE WAY for sliders (2.3.0; the owner, via Main Agent, 2026-10-25: a highlighted slider steps on Left/Right and the
	// D-pad without activation, in every mod - design and double-step proof in 4. plans\amf-null-checks\ONE-WAY-SLIDERS.md).
	// Dear ImGui changes a slider from the keyboard or pad only while it is the ACTIVE item; a slider that merely holds the
	// highlight hands Left / Right to navigation, which walks the highlight to the next table column. AMF cannot step a
	// mod's slider itself - a slider drawn by the mod's own copy of ImGui leaves no value, type, range or format in the
	// shared context - so instead, right after ImGui::NewFrame and before any page draws, a Left / Right (or D-pad left /
	// right) press on a highlighted slider or drag makes THAT item the active one, from the keyboard or the pad as ImGui
	// itself would, for as long as the key is held, and lets go on the first frame none of those keys is down. Every copy of
	// ImGui drawing on the context then sees an active slider and runs its own keyboard / pad tweak with its own format and
	// range (a precise slider holds that to one unit of the shown digit), exactly as A then Right does. A mod's own helper
	// that steps a highlighted, NOT active slider (Deep Snow's HighlightNudge) sees IsItemActive() and stands down; DEM's
	// NudgeableSlider adds nothing on a frame where the slider already moved. Text fields carry the same Inputable flag, so
	// they are excluded: one AMF knows (a_navIsTextField), one learnt while it was being typed in (it owned Home), and one
	// that dropped the activation within the frame it got it (InputTextEx does that for a field with no edit state) or
	// started typing on it (the field holding the context's edit state) - released at once and learnt.
	// ---------------------------------------------------------------------------------------------------------------------
	namespace detail
	{
		inline ImGuiContext* g_stepCtx = nullptr;
		inline ImGuiID       g_stepId = 0;          // the item AMF activated for a held Left / Right, 0 = none
		inline int           g_stepFrame = -1;      // the frame it was activated on
		inline ImGuiID       g_textIds[64] = {};    // items that are not steppers (text fields)
		inline int           g_textNext = 0;

		inline bool KnownText(ImGuiID a_id)
		{
			if (a_id == 0) return false;
			for (const ImGuiID id : g_textIds) { if (id == a_id) return true; }
			return false;
		}

		inline void LearnText(ImGuiID a_id)
		{
			if (a_id == 0 || KnownText(a_id)) return;
			g_textIds[g_textNext] = a_id;
			g_textNext = (g_textNext + 1) % static_cast<int>(sizeof(g_textIds) / sizeof(g_textIds[0]));
		}

		inline bool HorizontalKeyDown()
		{
			return ImGui::IsKeyDown(ImGuiKey_LeftArrow) || ImGui::IsKeyDown(ImGuiKey_RightArrow) ||
			       ImGui::IsKeyDown(ImGuiKey_GamepadDpadLeft) || ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight);
		}
	}

	enum class HighlightStep { kNone, kStarted, kReleased, kNotAStepper };

	// Right after ImGui::NewFrame, before any page draws, every frame (Renderer.cpp, after EnterActivatesArrowItem;
	// tools\nav_left_check.cpp section 8). a_enabled: the menu is up and taking input. a_navIsTextField: AMF knows the
	// highlighted item as a text field (keyboard::IsTextField). Returns what it did this frame (for the log).
	inline HighlightStep StepHighlightedArrowItem(bool a_enabled, bool a_navIsTextField)
	{
		ImGuiContext* const g = GImGui;
		if (!g) return HighlightStep::kNone;
		if (detail::g_stepCtx != g)
		{
			detail::g_stepCtx = g;
			detail::g_stepId = 0;
			detail::g_stepFrame = -1;
			for (ImGuiID& id : detail::g_textIds) { id = 0; }
			detail::g_textNext = 0;
		}
		// a text field being typed in (any copy of ImGui): while active it owns Home / End (InputTextEx's SetKeyOwner, which a
		// slider or drag never does), and it is not a slider's number box (Ctrl+click: TempInputId)
		if (g->ActiveId != 0 && g->TempInputId != g->ActiveId && ImGui::GetKeyOwner(ImGuiKey_Home) == g->ActiveId) { detail::LearnText(g->ActiveId); }

		if (detail::g_stepId != 0)
		{
			const ImGuiID id = detail::g_stepId;
			if (g->ActiveId != id)
			{
				detail::g_stepId = 0;
				// it let go within the frame it was given the activation: not a slider or drag (a text field with no state)
				if (detail::g_stepFrame == g->FrameCount - 1 && g->ActiveIdPreviousFrame != id)
				{
					detail::LearnText(id);
					return HighlightStep::kNotAStepper;
				}
				return HighlightStep::kNone;   // A / Enter / Escape / a click took it over, or it stopped being drawn
			}
			if (detail::KnownText(id))   // it started typing (a text field holding the context's edit state): let it go at once
			{
				detail::g_stepId = 0;
				ImGui::ClearActiveID();
				return HighlightStep::kNotAStepper;
			}
			if (!a_enabled || !detail::HorizontalKeyDown())
			{
				detail::g_stepId = 0;
				ImGui::ClearActiveID();   // a normal let-go: IsItemDeactivated(AfterEdit) report it to the mod this frame
				return HighlightStep::kReleased;
			}
			return HighlightStep::kNone;   // held: the slider steps itself on ImGui's tweak repeat
		}

		if (!a_enabled || g->ActiveId != 0 || g->NavId == 0 || !g->NavWindow) return HighlightStep::kNone;
		if (g->IO.MouseDown[ImGuiMouseButton_Left]) return HighlightStep::kNone;   // a drag is the mouse's
		const ImGuiID nav = g->NavId;
		const bool noted = detail::g_ctx == g && detail::g_navId == nav && detail::g_frame == g->FrameCount - 1;
		if (!noted && !NavOnKnownArrowItem()) return HighlightStep::kNone;
		if (a_navIsTextField || detail::KnownText(nav)) return HighlightStep::kNone;

		const bool keyboardNav = (g->IO.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
		const bool padNav = (g->IO.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0;
		const bool keys = keyboardNav && (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true) || ImGui::IsKeyPressed(ImGuiKey_RightArrow, true));
		const bool pad = padNav && (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true));
		if (!keys && !pad) return HighlightStep::kNone;

		ImGui::SetActiveID(nav, g->NavWindow);
		const ImGuiInputSource source = pad && !keys ? ImGuiInputSource_Gamepad : ImGuiInputSource_Keyboard;
		g->ActiveIdSource = source;   // what a precise slider tests to tell a nudge from a drag
		g->NavInputSource = source;   // which keys ImGui's tweak reads (GetNavTweakPressedAmount)
		g->ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);   // held: no navigation move either way
		ImGui::NavMoveRequestCancel();   // this press's move (made in NewFrame): the highlight stays
		g->NavDisableHighlight = false;  // and shows, as a keyboard / pad move would show it (so A / Enter work on it next)
		g->NavDisableMouseHover = true;
		detail::g_stepId = nav;
		detail::g_stepFrame = g->FrameCount;
		return HighlightStep::kStarted;
	}

	// This frame, after the page is drawn: is the highlight inside a popup (an open combo, a context menu, a modal)?
	inline bool NavInPopup()
	{
		const ImGuiContext* const g = GImGui;
		return g && g->NavWindow && (g->NavWindow->Flags & ImGuiWindowFlags_Popup) != 0;
	}

	// Read BEFORE ImGui::NewFrame: is a popup open that Escape will close (an open dropdown, a context menu - not a modal,
	// which Escape leaves open)? NewFrame's own cancel (NavUpdateCancelRequest) closes it, so by the time Renderer.cpp reads
	// Escape as "close the menu" the popup is already gone - this is the only moment it can still be seen (retest batch
	// B-20261025-053652-453d, 26d2 N7 / c0a3: Esc in an open dropdown closed the whole AMF menu).
	inline bool EscapeClosesAPopup()
	{
		const ImGuiContext* const g = GImGui;
		if (!g || g->OpenPopupStack.Size == 0) return false;
		const ImGuiWindow* const top = g->OpenPopupStack.back().Window;
		return top && (top->Flags & ImGuiWindowFlags_Modal) == 0;
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
