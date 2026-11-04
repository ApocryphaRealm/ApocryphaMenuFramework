#pragma once

// ============================================================================================================
// When a sideways press inside a mod's page may send the highlight back to the mod list, and how a slider is taken hold of.
//
// The menu's rule since 1.8.8: Left at a row with nothing to its left goes back to the list. A Left press leaves the page
// only when ALL of these hold:
//   - the page has the highlight and nothing is being edited (a slider or drag that was activated is "being edited");
//   - the highlight is not inside a popup (an open combo, a context menu);
//   - the highlighted item is not a text or number-entry field (they kept the press in 2.3.0 and are unchanged);
//   - the page did not take the press itself - ImGui made a Left move request this frame and a widget cancelled it
//     (NavMoveRequestCancel), which is how a page that steps its own value on Left says "mine".
// A highlighted slider or drag is a row like any other (2.4.0).
//
// HOW A SLIDER TAKES INPUT (2.4.0; the owner, 2026-11-03: "you should have to click a slider for it to activate and then
// slide left to right" - "if you're using a mouse, you can just not have to click and you can just drag it directly. But
// if you're using a controller, you have to focus it first, click, and then you can use the D-pad to nudge it"). This is
// Dear ImGui's own behaviour, in every copy of ImGui drawing on the context, and AMF no longer overrides it:
//   - D-pad / arrows move the highlight, across a slider as across any row; A / Enter / Space activates it;
//   - while active, Left / Right nudge it (PreciseSlider.h and the slider exports hold that to one unit of the shown digit),
//     and a held key steps at the key-repeat rate (HoldTweakAtKeyRepeat below);
//   - A / Enter / Space, or B / Escape, lets go (B and Escape do only that on that press);
//   - Up / Down let go and move the highlight to the next row, the nudged value kept and reported as released;
//   - the mouse drags directly and the wheel notches, activated or not.
// The 2.3.0 mechanism that activated a highlighted slider on Left / Right (StepHighlightedArrowItem) was removed: it is why
// the D-pad could not leave a slider. The decision is LeftPressMayLeave() - a pure function, proven off-game by
// tools\nav_left_check.bat.
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
		bool onEntryField   = false;   // the highlighted item is a text or number-entry field (NavOnEntryField)
		bool pageTookTheKey = false;   // a widget cancelled ImGui's Left move this frame - it used the press itself
	};

	// True when this Left press may send the highlight back to the mod list (once ImGui has also found nothing to the left).
	inline bool LeftPressMayLeave(const LeftPress& a_p)
	{
		return a_p.contentHasNav && !a_p.editing && !a_p.inPopup && !a_p.onEntryField && !a_p.pageTookTheKey;
	}

	namespace detail
	{
		inline ImGuiContext* g_ctx = nullptr;   // the context the note below was taken in
		inline int     g_frame = -1;            // the frame an arrow-adjusted item was seen holding the highlight
		inline ImGuiID g_navId = 0;             // ImGui's NavId at that moment

		inline ImGuiContext* g_entryCtx = nullptr;   // the same for a number-entry field (NoteEntryField)
		inline int     g_entryFrame = -1;
		inline ImGuiID g_entryNavId = 0;

		// The item just drawn holds ImGui's nav highlight: on the item itself, or on one of its parts for a multi-part widget
		// (SliderFloat3's fields, a drag range, a number box with its - and + buttons), whose last item is the group around them.
		inline bool LastItemHoldsNav(ImGuiContext* a_g)
		{
			if (!a_g || a_g->NavId == 0 || !a_g->CurrentWindow || a_g->NavWindow != a_g->CurrentWindow) return false;
			if (a_g->LastItemData.ID != 0 && a_g->LastItemData.ID == a_g->NavId) return true;
			if (!a_g->NavIdIsAlive) return false;
			ImGuiWindow* const w = a_g->CurrentWindow;
			const ImRect nav = ImGui::WindowRectRelToAbs(w, w->NavRectRel[a_g->NavLayer]);
			return a_g->LastItemData.Rect.Contains(nav);
		}
	}

	// Call right after drawing a slider or drag: notes it when the highlight is on it, so Enter and the first press after a
	// mouse click activate it (EnterActivatesArrowItem, ActivateAfterClick) and a held key is paced (HoldTweakAtKeyRepeat).
	inline void NoteArrowItem()
	{
		ImGuiContext* const g = GImGui;
		if (!g || !detail::LastItemHoldsNav(g)) return;
		detail::g_ctx = g;
		detail::g_frame = g->FrameCount;
		detail::g_navId = g->NavId;
	}

	// Call right after drawing a number-entry field (InputInt / InputFloat / InputScalar...): Left on it keeps to the page, as
	// it did before sliders went back to navigating. AMF's own pages and the number-entry exports do; a mod's own copy of
	// ImGui cannot, and such a field is known only once it has been typed in (KnownText).
	inline void NoteEntryField()
	{
		ImGuiContext* const g = GImGui;
		if (!g || !detail::LastItemHoldsNav(g)) return;
		detail::g_entryCtx = g;
		detail::g_entryFrame = g->FrameCount;
		detail::g_entryNavId = g->NavId;
	}

	// ---------------------------------------------------------------------------------------------------------------------
	// Items known from ImGui's OWN state (2.3.0, retest batch B-20261025-053652-453d, 26d2: AMF Example's Speed slider is drawn
	// by AMF Example's own copy of Dear ImGui on the framework's context - precise::SliderFloat from the SDK, which never calls
	// NoteArrowItem). Enter's activation and the paced hold below need to know a slider from a button there too. Any code
	// drawing on the context leaves the item's kind in shared state, whichever copy of ImGui drew it:
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

	// Every frame, after the pages are drawn (Renderer.cpp; tools\nav_left_check.cpp per frame).
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

	// Right after ImGui::NewFrame, BEFORE EnterActivatesArrowItem (2.3.0; combined run 3266, reproduced twice: the first Enter
	// after a mouse click on a slider did nothing, the second activated it). A mouse click leaves the clicked slider as the
	// nav item but sets NavDisableHighlight (the highlight is hidden while the mouse is in use), and Dear ImGui 1.90.8's
	// NavUpdate turns Enter / Space / the pad's A into an activation only while the highlight is shown - so the first press
	// was spent on nothing (tools\nav_left_check.cpp 8h measured it: after the click NavDisableHighlight 1, NavActivateId 0).
	// For a slider or drag (by ImGui's own state, as above) that press now does what it does once the highlight shows: it
	// shows the highlight and activates the item as a tweak (Enter, Space and A alike - no number box; Ctrl+click still
	// types). Anything else keeps ImGui's behaviour. True when the press was turned into the activation.
	inline bool ActivateAfterClick()
	{
		ImGuiContext* const g = GImGui;
		if (!g || !g->NavDisableHighlight || g->NavId == 0 || !g->NavWindow || g->ActiveId != 0 || g->NavActivateId != 0) return false;
		if (g->IO.MouseDown[ImGuiMouseButton_Left]) return false;
		if (!NavOnKnownArrowItem() && !(detail::g_ctx == g && detail::g_navId == g->NavId && detail::g_frame == g->FrameCount - 1)) return false;
		const bool keyboardNav = (g->IO.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0;
		const bool padNav = (g->IO.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0;
		const bool keys = keyboardNav && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
		                                  ImGui::IsKeyPressed(ImGuiKey_Space, false));
		const bool pad = padNav && ImGui::IsKeyPressed(ImGuiKey_NavGamepadActivate, false);
		if (!keys && !pad) return false;
		g->NavDisableHighlight = false;
		g->NavActivateId = g->NavActivateDownId = g->NavActivatePressedId = g->NavId;
		g->NavActivateFlags = ImGuiActivateFlags_PreferTweak;
		g->NavInputSource = pad && !keys ? ImGuiInputSource_Gamepad : ImGuiInputSource_Keyboard;
		return true;
	}

	namespace detail
	{
		inline ImGuiID g_textIds[64] = {};    // items known to be text fields, learnt while they were being typed in
		inline int     g_textNext = 0;

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

		// The item holding ActiveId is a slider or drag the keyboard or pad has hold of: known from ImGui's state or noted as
		// drawn last frame, no text field, no number being typed into it, not held by the mouse.
		inline bool SliderHeldByKeys(const ImGuiContext* a_g)
		{
			if (!a_g || a_g->ActiveId == 0) return false;
			if (a_g->ActiveIdSource != ImGuiInputSource_Keyboard && a_g->ActiveIdSource != ImGuiInputSource_Gamepad) return false;
			const bool noted = g_ctx == a_g && g_navId == a_g->ActiveId && g_frame >= a_g->FrameCount - 1;
			return (noted || KnownArrow(a_g->ActiveId)) && !KnownText(a_g->ActiveId) && a_g->TempInputId != a_g->ActiveId;
		}
	}

	// This frame, after the page is drawn: is the highlight on a text or number-entry field? a_navIsTextField: AMF knows the
	// highlighted item as a text field (keyboard::IsTextField).
	inline bool NavOnEntryField(bool a_navIsTextField)
	{
		const ImGuiContext* const g = GImGui;
		if (!g) return false;
		const bool noted = detail::g_entryCtx == g && detail::g_entryFrame == g->FrameCount && detail::g_entryNavId != 0 && detail::g_entryNavId == g->NavId;
		return a_navIsTextField || noted || detail::KnownText(g->NavId);
	}

	// Read BEFORE ImGui::NewFrame: is a slider or drag held by the keyboard or pad? NewFrame's own cancel (NavUpdateCancelRequest)
	// lets it go on Escape or B, so by the time Renderer.cpp reads Escape as "close the menu" nothing shows it was held. That
	// press only lets go. A number being typed, a text field and a mouse drag are not this.
	inline bool SliderHeldByKeys() { return detail::SliderHeldByKeys(GImGui); }

	// ---------------------------------------------------------------------------------------------------------------------
	// A HELD key steps a slider at the key-repeat rate, never every frame (2.3.0; combined run 3266, 2026-10-25, step S8:
	// Skyrim crashed in Scaleform - SkyrimSE+0FFE143, the same place as DEM 1.7.5's typed-scale crash - while Right was held
	// on DEM's Offset X slider). Dear ImGui's keyboard / pad tweak of an ACTIVE slider repeats at its "nav tweak" rate:
	// 0.72 x io.KeyRepeatDelay, then 0.30 x io.KeyRepeatRate (0.198 s, then every 0.015 s - about 54 steps a second, one a
	// frame at 60 fps; tools\nav_left_check.cpp measured 49 at 60 fps and 55 at 144 before this). A mod that applies each
	// change (DEM re-applies its minimap display) then does that every frame.
	//
	// So while a slider or drag that the keyboard or pad has hold of is active and Left / Right (D-pad left / right) is
	// held, AMF paces the held key itself: it keeps its own clock of the hold and, right after NewFrame, writes the key's
	// DownDuration as 0 ("pressed") on the press and on each key-repeat tick - after io.KeyRepeatDelay (0.275 s), one every
	// io.KeyRepeatRate (0.050 s), DEM's own nudge timing - and as 0.1 s (below every repeat delay ImGui uses) on every other
	// frame. Every reader of that key on this context - ImGui's tweak in each copy of ImGui, DEM's NudgeableSlider
	// (IsKeyPressed with repeat), Deep Snow's helper - then sees one press on exactly those frames: about 15 steps in a
	// one-second hold at any frame rate, one step a tap, and never a navigation-move repeat (that waits 0.198 s). Changing
	// io.KeyRepeatDelay / Rate instead was tried first and dropped: it moves the default repeat too, so DEM's nudge then
	// fired on other frames and added steps (20 a second in the check). A text field is never touched.
	// Right after ImGui::NewFrame, every frame (Renderer.cpp; tools\nav_left_check.cpp section 8b and 8e).
	namespace detail
	{
		inline ImGuiContext* g_repeatCtx = nullptr;
		inline bool          g_repeatHeld = false;
		inline float         g_repeatClock = 0.0f;   // seconds the hold has lasted, by AMF's own count
		inline constexpr float kPacedIdle = 0.1f;    // a held key's DownDuration on a frame that is no tick

		inline void PaceKey(ImGuiContext* a_g, ImGuiKey a_key, bool a_tick)
		{
			ImGuiKeyData* const data = ImGui::GetKeyData(a_g, a_key);
			if (data && data->Down) { data->DownDuration = a_tick ? 0.0f : kPacedIdle; }
		}
	}

	// True while it paces a held key.
	inline bool HoldTweakAtKeyRepeat()
	{
		ImGuiContext* const g = GImGui;
		if (!g) return false;
		if (detail::g_repeatCtx != g)
		{
			detail::g_repeatCtx = g;
			detail::g_repeatHeld = false;
			for (ImGuiID& id : detail::g_textIds) { id = 0; }
			detail::g_textNext = 0;
		}
		// a text field being typed in (any copy of ImGui): while active it owns Home / End (InputTextEx's SetKeyOwner, which a
		// slider or drag never does), and it is not a slider's number box (Ctrl+click: TempInputId)
		if (g->ActiveId != 0 && g->TempInputId != g->ActiveId && ImGui::GetKeyOwner(ImGuiKey_Home) == g->ActiveId) { detail::LearnText(g->ActiveId); }
		if (!(detail::SliderHeldByKeys(g) && detail::HorizontalKeyDown()))
		{
			detail::g_repeatHeld = false;
			return false;
		}
		const ImGuiKey keys[] = { ImGuiKey_LeftArrow, ImGuiKey_RightArrow, ImGuiKey_GamepadDpadLeft, ImGuiKey_GamepadDpadRight };
		bool tick = false;
		if (!detail::g_repeatHeld)
		{
			// the hold starts: its clock starts at the longest real hold (0 on the press frame - a key held from before the
			// slider became active keeps its time); a press frame is a tick
			float longest = 0.0f;
			for (const ImGuiKey key : keys)
			{
				const ImGuiKeyData* const data = ImGui::GetKeyData(g, key);
				if (data && data->Down && data->DownDuration > longest) { longest = data->DownDuration; }
			}
			detail::g_repeatClock = longest;
			detail::g_repeatHeld = true;
			tick = longest == 0.0f;
		}
		else
		{
			const float before = detail::g_repeatClock;
			detail::g_repeatClock += g->IO.DeltaTime;
			tick = ImGui::CalcTypematicRepeatAmount(before, detail::g_repeatClock, g->IO.KeyRepeatDelay, g->IO.KeyRepeatRate) > 0;
		}
		for (const ImGuiKey key : keys) { detail::PaceKey(g, key, tick); }
		return true;
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

	// ---------------------------------------------------------------------------------------------------------------------
	// A NEW PAGE STARTS AT ITS FIRST ITEM (2.3.0, regression run c768 C2: opening another mod's page reused the highlight
	// position of the page before). Every mod's page is drawn in the same "##content" child window, and Dear ImGui keeps
	// that window's last nav item (NavLastIds) and its rectangle (NavRectRel) across frames; crossing into the pane
	// (FocusWindow) restores NavId from them. So mod B opened at mod A's old item - at the same item when B has one with
	// the same label (same id inside ##content), otherwise at a dead id whose stale rectangle the next move started from.
	// The kind memory above is keyed by id too, so B's "Speed" button could inherit A's "Speed" slider kind.
	// Call right after BeginChild of the pane, on the frame the selected page changes (Renderer.cpp; tools\
	// page_switch_check.bat): scrolls the pane and its child windows back to the top, forgets the pane's own nav memory
	// and its child windows', the item kinds learnt, a noted
	// arrow item, and any slider held by the keys. When the highlight is in the pane at that moment it restarts on the new page's
	// first item (NavInitWindow: the first item submitted this frame). A pane that does not hold the highlight is left
	// with no remembered item, so crossing into it later lands where crossing into a never-visited pane does.
	// True when the highlight was in the pane.
	inline bool ForgetPaneNav(ImGuiWindow* a_pane)
	{
		ImGuiContext* const g = GImGui;
		if (!g || !a_pane) return false;
		for (ImGuiWindow* w : g->Windows)
		{
			if (!w || (w != a_pane && !ImGui::IsWindowChildOf(w, a_pane, false, false))) continue;
			for (int layer = 0; layer < ImGuiNavLayer_COUNT; ++layer)
			{
				w->NavLastIds[layer] = 0;
				w->NavRectRel[layer] = ImRect();
			}
			w->NavLastChildNavWindow = nullptr;
			// and the new page opens at its top (Main Agent, after C2: A's scroll carried over, so B's first item could be
			// off screen when it took the highlight) - applied by ImGui at the window's next Begin
			ImGui::SetScrollY(w, 0.0f);
		}
		// a slider held by the keys on the old page lets go (the new page may reuse its id)
		if (detail::SliderHeldByKeys(g)) { ImGui::ClearActiveID(); }
		// the item kinds and the hold: ids from the old page mean nothing on the new one
		for (ImGuiID& id : detail::g_arrowIds) { id = 0; }
		detail::g_arrowNext = 0;
		for (ImGuiID& id : detail::g_textIds) { id = 0; }
		detail::g_textNext = 0;
		detail::g_navId = 0;
		detail::g_frame = -1;
		detail::g_entryNavId = 0;
		detail::g_entryFrame = -1;
		detail::g_repeatHeld = false;
		detail::g_repeatClock = 0.0f;

		const bool inPane = g->NavWindow && (g->NavWindow == a_pane || ImGui::IsWindowChildOf(g->NavWindow, a_pane, false, false));
		if (inPane)
		{
			if (g->NavWindow != a_pane) { ImGui::FocusWindow(a_pane); }
			ImGui::NavInitWindow(a_pane, true);
		}
		return inPane;
	}
}
