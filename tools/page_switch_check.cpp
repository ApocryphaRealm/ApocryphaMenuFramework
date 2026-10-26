// Off-game check that another mod's page starts at its first item (AMF 2.3.0; regression run c768, Part C, bug C2: "a stale
// navId carries over between mods - opening another mod's page reuses the highlight position of the page before").
// Runs AMF's own ImGui build (1.90.8 with the overlay patches, the SE line's vcpkg tree) headless, with keyboard navigation
// on, in the menu's own shape: a parent window, the "##side" list pane and the "##content" pane every mod's page draws in,
// the crossing between them done the way Renderer.cpp does it (SetNextWindowFocus; back to the list lands on the open
// entry through SetKeyboardFocusHere), and NavSide.h's per-frame calls in Renderer.cpp's order.
//   tools\page_switch_check.bat   -> one line per check and "page_switch_check: N of N passed"; exits 1 on any failure.
// Mod A's page: Enabled, Reset, Speed (a slider - item 3), Done. Mod B's page: Apply, Defaults, Speed (a BUTTON with the
// same label, so the same id inside ##content), Close. What it proves:
//   1. the CONTROL (the fix off, as 2.3.0 was before C2): highlight A's item 3 and step it, go back to the list, open B,
//      cross into B - the highlight comes back on B's item 3 (A's remembered id), and B's Speed button carries A's slider
//      kind (Left would stay on the page, Left/Right would activate the button); with Right held while the page changes,
//      AMF's one-way step activates B's button;
//   2. the fix (navside::ForgetPaneNav on the frame the page changes, as Renderer.cpp calls it): the same steps - B starts
//      at its first item, no stale id, no stale kind, no step or hold left over, and a held Right never activates B's button;
//   (tools\page_switch_check.bat --no-fix runs section 2 with the fix off: its checks fail - the control for the check.)
//   3. what must keep working: Left / back to the list lands on the open entry; going back into the SAME page keeps its
//      highlight (no reset without a change); clicking the open entry again is no change.
// nav_left_check (every Left case and DEM's N4) runs the same NavSide.h unchanged and is run beside this one.

#include "../include/NavSide.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <iostream>
#include <string>

namespace
{
	int g_pass = 0, g_fail = 0;
	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	enum Item { kRowA, kRowB, kA1, kA2, kA3, kA4, kB1, kB2, kB3, kB4, kCount };
	ImGuiID g_ids[kCount] = {};
	int     g_focusRequest = -1;     // focus this item (SetFocusID) the next frame it is drawn

	bool  g_fixOn = true;            // call navside::ForgetPaneNav when the page changes (Renderer.cpp, 2.3.0 C2)
	int   g_sel = 0;                 // the open mod: 0 = A, 1 = B (Renderer.cpp's sel / selMod)
	int   g_selExternal = -1;        // a selection made outside the list (DevBench op=select), read at the frame's start
	int   g_focusPane = 0;           // Renderer.cpp's g_focusPane: 1 = the list, 2 = the page
	bool  g_navToSelected = false;   // Renderer.cpp's g_navToSelected: back in the list, land on the open entry
	std::string g_shownPage;         // Renderer.cpp's s_shownPage
	int   g_resets = 0;
	float g_speed = 0.50f;
	bool  g_heldPacing = false;      // HoldTweakAtKeyRepeat's answer this frame
	navside::HighlightStep g_step = navside::HighlightStep::kNone;
	ImGuiID g_stepStartedOn = 0;     // the item AMF's one-way step activated this frame (0 = none)

	struct Keys { bool left = false, right = false, up = false, down = false, enter = false; };

	void BuildFonts()
	{
		ImGuiIO& io = ImGui::GetIO();
		ImFontConfig cfg;
		cfg.SizePixels = 16.0f;
		io.Fonts->AddFontDefault(&cfg);
		unsigned char* pixels = nullptr;
		int w = 0, h = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
		io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(1)));
	}

	void Mark(Item a_item)
	{
		g_ids[a_item] = ImGui::GetItemID();
		if (g_focusRequest == a_item)
		{
			ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
			GImGui->NavDisableHighlight = false;
			g_focusRequest = -1;
		}
	}

	void Frame(const Keys& a_k = {})
	{
		ImGuiIO& io = ImGui::GetIO();
		io.AddKeyEvent(ImGuiKey_LeftArrow, a_k.left);
		io.AddKeyEvent(ImGuiKey_RightArrow, a_k.right);
		io.AddKeyEvent(ImGuiKey_UpArrow, a_k.up);
		io.AddKeyEvent(ImGuiKey_DownArrow, a_k.down);
		io.AddKeyEvent(ImGuiKey_Enter, a_k.enter);
		io.DeltaTime = 1.0f / 60.0f;
		ImGui::NewFrame();
		// Renderer.cpp, right after NewFrame, in this order
		navside::ActivateAfterClick();
		navside::EnterActivatesArrowItem();
		g_step = navside::StepHighlightedArrowItem(true, false);
		g_heldPacing = navside::HoldTweakAtKeyRepeat();
		g_stepStartedOn = g_step == navside::HighlightStep::kStarted ? GImGui->ActiveId : 0;   // what AMF's step made active

		int sel = g_sel;
		if (g_selExternal >= 0) { sel = g_selExternal; g_selExternal = -1; }
		const bool navToSelected = g_navToSelected;
		g_navToSelected = false;

		ImGui::SetNextWindowPos(ImVec2(20, 20));
		ImGui::SetNextWindowSize(ImVec2(1000, 600));
		ImGui::Begin("###amf-menu", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		if (g_focusPane == 1) { ImGui::SetNextWindowFocus(); g_focusPane = 0; }
		ImGui::BeginChild("##side", ImVec2(300.0f, 0.0f), true);
		const char* names[] = { "Mod A", "Mod B" };
		for (int i = 0; i < 2; ++i)
		{
			if (sel == i && navToSelected) { ImGui::SetKeyboardFocusHere(); }
			if (ImGui::Selectable(names[i], sel == i)) { sel = i; }
			Mark(i == 0 ? kRowA : kRowB);
		}
		ImGui::EndChild();
		ImGui::SameLine();
		if (g_focusPane == 2) { ImGui::SetNextWindowFocus(); g_focusPane = 0; }
		ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border);
		{
			const std::string page = std::string("mod:") + names[sel];
			if (page != g_shownPage)
			{
				if (!g_shownPage.empty() && g_fixOn)
				{
					navside::ForgetPaneNav(ImGui::GetCurrentWindow());
					++g_resets;
				}
				g_shownPage = page;
			}
		}
		if (sel == 0)
		{
			ImGui::Button("Enabled");  Mark(kA1);
			ImGui::Button("Reset");    Mark(kA2);
			ImGui::SetNextItemWidth(400);
			ImGui::SliderFloat("Speed", &g_speed, 0.0f, 1.0f, "%.2f");   // item 3: a mod's plain slider (its own ImGui)
			Mark(kA3);
			ImGui::Button("Done");     Mark(kA4);
		}
		else
		{
			ImGui::Button("Apply");    Mark(kB1);
			ImGui::Button("Defaults"); Mark(kB2);
			ImGui::Button("Speed");    Mark(kB3);   // the same label inside ##content: the same id as A's slider
			ImGui::Button("Close");    Mark(kB4);
		}
		ImGui::EndChild();
		navside::LearnNavItemKind();   // Renderer.cpp: after the page, before the Left decision
		ImGui::End();
		ImGui::Render();
		g_sel = sel;
	}

	void Settle(int a_n = 3) { for (int i = 0; i < a_n; ++i) Frame(); }
	void Tap(Keys a_k) { Frame(a_k); Frame(); Settle(2); }
	void ToList() { g_focusPane = 1; g_navToSelected = true; Settle(3); }   // Renderer.cpp: "nav: options -> list (returning to the open entry)"
	void ToPage() { g_focusPane = 2; Settle(3); }                            // Renderer.cpp: "nav: list -> options"
	ImGuiID Nav() { return GImGui->NavId; }
	// the player's way onto A's item 3: Down presses (a nav move carries the item's flags, so its kind is learnt as in play)
	void ReachA3() { for (int i = 0; i < 6 && Nav() != g_ids[kA3]; ++i) Tap({ .down = true }); }

	struct Outcome
	{
		ImGuiID afterCross = 0;     // NavId right after crossing into B's page
		ImGuiID afterDown = 0;      // NavId after one Down press there (0 if the cross already placed it)
		bool    staleKind = false;  // B's Speed button known as a slider/drag once the highlight is on it
		bool    heldActivated = false;  // with Right held across the change, an item of B's page became active
		bool    heldPacing = false;     // HoldTweakAtKeyRepeat still pacing after the change
		ImGuiID heldNav = 0;        // NavId after the change made while the highlight was in the page
		int     resets = 0;
	};

	void Fresh()
	{
		// a new context at a NEW address before the old one goes, so NavSide.h's per-context memory starts fresh each run
		ImGuiContext* const old = ImGui::GetCurrentContext();
		ImGui::SetCurrentContext(ImGui::CreateContext());
		if (old) ImGui::DestroyContext(old);
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(1280, 720);
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		BuildFonts();
		g_sel = 0; g_selExternal = -1; g_focusPane = 0; g_navToSelected = false; g_shownPage.clear(); g_resets = 0; g_speed = 0.5f;
		Settle(3);
	}

	Outcome Run(bool a_fix, bool a_print)
	{
		g_fixOn = a_fix;
		Outcome o;
		const std::string tag = a_fix ? "[fix] " : "[control, fix off] ";

		// --- A: the highlight on item 3, stepped once (its kind learnt, as in play), then back to the list
		Fresh();
		ToPage();
		ReachA3();
		if (a_print) Check(Nav() == g_ids[kA3] && g_ids[kA3] != 0, tag + "A's page: the highlight on item 3 (the Speed slider)");
		const float before = g_speed;
		Tap({ .right = true });
		if (a_print) Check(g_speed > before && navside::NavOnKnownArrowItem(), tag + "A: Right steps the slider and its kind is learnt");
		ToList();
		if (a_print) Check(Nav() == g_ids[kRowA], tag + "back to the list lands on the open entry (Mod A)");

		// --- open B from the list: Down to its row, Enter
		Tap({ .down = true });
		Tap({ .enter = true });
		if (a_print) Check(g_sel == 1, tag + "Enter on Mod B's row opens B");
		ToPage();
		o.afterCross = Nav();
		o.staleKind = Nav() == g_ids[kB3] && navside::NavOnKnownArrowItem();
		if (o.afterCross == 0) { Tap({ .down = true }); o.afterDown = Nav(); }
		o.resets = g_resets;

		// --- returning to the open entry and coming back keeps B's highlight (no change, no reset)
		if (a_print && a_fix)
		{
			ToList();
			Check(Nav() == g_ids[kRowB], tag + "Left / back to the list from B lands on the open entry (Mod B)");
			Tap({ .enter = true });   // the open entry again: no change of page
			ToPage();
			const ImGuiID back = Nav();
			Check(back == o.afterDown || back == o.afterCross, tag + "back into the SAME page: its highlight is kept where it was");
			Check(g_resets == o.resets, tag + "clicking the open entry again and re-entering the same page reset nothing");
		}

		// --- a page change while Right is held on A's slider (DevBench op=select while the highlight is in the page)
		Fresh();
		ToPage();
		ReachA3();
		Tap({ .right = true });            // learn its kind as in play
		Frame({ .right = true });          // press and hold: AMF's one-way step makes the slider active
		if (a_print) Check(GImGui->ActiveId == g_ids[kA3], tag + "Right held on A's slider: it is the active item (one-way step)");
		g_selExternal = 1;                 // the page changes under the held key
		for (int i = 0; i < 40; ++i)
		{
			Frame({ .right = true });
			// a button lets go of an activation it did not get from A / Enter within the frame, so the step's own answer is read
			const ImGuiID on = g_stepStartedOn;
			if (on != 0 && (on == g_ids[kB1] || on == g_ids[kB2] || on == g_ids[kB3] || on == g_ids[kB4])) { o.heldActivated = true; }
			if (i > 2 && g_heldPacing) { o.heldPacing = true; }
		}
		Frame();
		Settle(3);
		o.heldNav = Nav();
		return o;
	}
}

int main(int argc, char** argv)
{
	// --no-fix: section 2's assertions run with the fix OFF (the proof they fail without it; exits 1)
	const bool noFix = argc > 1 && std::string(argv[1]) == "--no-fix";
	// 1. the control: the fix off reproduces C2
	{
		const Outcome c = Run(false, true);
		Check(c.afterCross == g_ids[kB3], "[control, fix off] crossing into B restores A's remembered id: the highlight is on B's ITEM 3 (the bug, C2)");
		Check(c.staleKind, "[control, fix off] B's Speed BUTTON carries A's slider kind (stale kind)");
		Check(c.heldActivated, "[control, fix off] Right held across the change: AMF's step activates an item of B's page");
		Check(c.heldNav != g_ids[kB1], "[control, fix off] a change made with the highlight in the page leaves it off B's first item");
	}
	// 2. the fix
	{
		const Outcome f = Run(!noFix, true);
		const ImGuiID landed = f.afterCross != 0 ? f.afterCross : f.afterDown;
		Check(f.afterCross != g_ids[kB3] && f.afterCross != g_ids[kA3], "[fix] crossing into B restores no remembered id from A");
		Check(landed == g_ids[kB1], "[fix] B's page starts at its first item (Apply)");
		Check(!f.staleKind, "[fix] no stale kind on B's Speed button");
		Check(!f.heldActivated, "[fix] Right held across the change: nothing on B's page is activated");
		Check(!f.heldPacing, "[fix] no hold left pacing a key after the change");
		Check(f.heldNav == g_ids[kB1], "[fix] a change made with the highlight in the page restarts it on B's first item");
		Check(f.resets >= 1, "[fix] the reset ran on the page change");
		// the stale kind, asked directly with the highlight on B's Speed button
		Settle(1);
		g_focusRequest = kB3;
		Settle(3);
		Check(Nav() == g_ids[kB3] && !navside::NavOnKnownArrowItem(), "[fix] the highlight moved onto B's Speed button: not taken for a slider");
		Tap({ .right = true });
		Check(Nav() == g_ids[kB3] && GImGui->ActiveId == 0, "[fix] Right on B's Speed button activates nothing");
	}
	ImGui::DestroyContext();
	std::cout << "page_switch_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
