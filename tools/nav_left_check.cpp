// Off-game check of when a Left press inside a mod's page may send the highlight back to the mod list (AMF 2.3.0; combined
// run 2026-10-25 1515: any keyboard Left on Dragon's Eye Minimap's sliders, and on AMF Example's plain Speed slider, moved
// the highlight to the list's row while the value kept stepping). Runs AMF's own ImGui build (1.90.8 with the overlay
// patches, the SE line's vcpkg tree) headless, with keyboard navigation on, as the menu runs it.
//   tools\nav_left_check.bat   -> one line per check and "nav_left_check: N of N passed"; exits 1 on any failure.
// What it proves:
//   1. the decision (navside::LeftPressMayLeave): each guard on its own keeps the press on the page; with none it may leave;
//   2. on live frames, the cause: with the highlight on a slider that is NOT active (ActiveId 0, IsAnyItemActive false - the
//      state the old guard read), ImGui finds nothing to the left - so before 2.3.0 the press left the page;
//   3. on live frames, the fix: the highlight on AMF's precise slider, on a consumer-shaped slider (ImGui's slider + the
//      wheel helper, what igSliderFloat runs), on a drag and on a three-value slider (+ amf_NoteArrowItem, what the drag and
//      multi-value exports run) is seen by NavOnArrowItem, so the press stays; on a widget that steps its own value and
//      cancels ImGui's move (Dragon's Eye Minimap's way) and on an open tree node, PageTookTheMove sees it; inside an open
//      combo, NavInPopup sees it; on a plain button row none of them fire, so Left still goes back to the list;
//   4. a held Left (key repeat) on the slider keeps the highlight on it frame after frame.
// What it cannot: Renderer.cpp's own branch order and the game's input path (Input.cpp turns key and pad records into
// ImGui key events) - the in-game retest covers those.
#include "../include/PreciseSlider.h"
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

	enum Item { kRow, kPrecise, kConsumer, kDrag, kTriple, kCustom, kTree, kCombo, kCount };
	ImGuiID g_ids[kCount] = {};
	int     g_focusRequest = -1;   // focus this item (SetFocusID) the next frame it is drawn

	float  g_speed = 0.5f;          // AMF Example's Speed: precise::SliderFloat 0..1 "%.2f"
	double g_scale = 0.5;           // a consumer's igSliderScalar (double) 0..1 "%.3f"
	float  g_drag = 1.0f;           // a consumer's igDragFloat
	float  g_triple[3] = { 1, 2, 3 };
	int    g_custom = 10;           // a page widget that steps its own value on Left and cancels ImGui's move (DEM's way)
	int    g_combo = 0;

	struct Seen { bool onArrow, inPopup, tookLeft, anyActive; ImGuiID navId, activeId; };
	Seen g_seen{};

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

	void Frame(bool a_left = false, bool a_enter = false, float a_dt = 1.0f / 60.0f)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.AddKeyEvent(ImGuiKey_LeftArrow, a_left);
		io.AddKeyEvent(ImGuiKey_Enter, a_enter);
		io.DeltaTime = a_dt;
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(100, 100));
		ImGui::SetNextWindowSize(ImVec2(1200, 900));
		ImGui::Begin("###amf-page", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		ImGui::Button("A plain row");
		Mark(kRow);
		ImGui::SetNextItemWidth(500);
		precise::SliderFloat("Speed", &g_speed, 0.0f, 1.0f, "%.2f");   // AMF's own / a converted menu's slider
		Mark(kPrecise);
		{
			// the generated igSliderScalar body: ImGui's slider, then the wheel helper (which notes the highlight)
			const double lo = 0.0, hi = 1.0;
			ImGui::SetNextItemWidth(500);
			ImGui::SliderScalar("Scale", ImGuiDataType_Double, &g_scale, &lo, &hi, "%.3f");
			precise::WheelNudge(ImGuiDataType_Double, &g_scale, &lo, &hi, precise::FormatStep("%.3f", false));
			Mark(kConsumer);
		}
		// the generated igDragFloat / igSliderFloat3 bodies: ImGui's widget, then amf_NoteArrowItem -> navside::NoteArrowItem
		ImGui::SetNextItemWidth(500);
		ImGui::DragFloat("Drag", &g_drag, 0.1f, 0.0f, 10.0f, "%.1f");
		navside::NoteArrowItem();
		Mark(kDrag);
		ImGui::SetNextItemWidth(500);
		ImGui::SliderFloat3("Triple", g_triple, 0.0f, 10.0f, "%.1f");
		navside::NoteArrowItem();
		g_ids[kTriple] = ImGui::GetItemID();   // the group around the three fields
		ImGui::Button("Custom stepper");
		if (ImGui::GetItemID() == GImGui->NavId && GImGui->NavMoveDir == ImGuiDir_Left && GImGui->NavMoveSubmitted)
		{
			--g_custom;                      // the page steps its own value...
			ImGui::NavMoveRequestCancel();   // ...and tells ImGui the press was its own
		}
		Mark(kCustom);
		ImGui::SetNextItemOpen(true, ImGuiCond_Always);
		const bool treeOpen = ImGui::TreeNodeEx("A tree node", ImGuiTreeNodeFlags_None);
		Mark(kTree);
		if (treeOpen) { ImGui::TextUnformatted("child"); ImGui::TreePop(); }
		static const char* kItems[] = { "One", "Two", "Three" };
		ImGui::SetNextItemWidth(300);
		ImGui::Combo("Combo", &g_combo, kItems, 3);
		Mark(kCombo);
		g_seen = { navside::NavOnArrowItem(), navside::NavInPopup(), navside::PageTookTheMove(ImGuiDir_Left), ImGui::IsAnyItemActive(),
				   GImGui->NavId, GImGui->ActiveId };
		ImGui::End();
		ImGui::Render();
	}

	void Settle(int a_n = 3) { for (int i = 0; i < a_n; ++i) Frame(); }

	void FocusOn(Item a_item)
	{
		g_focusRequest = a_item;
		Settle(3);
	}

	// One tap of Left: the press frame (what Renderer.cpp reads), then the release.
	Seen TapLeft()
	{
		Frame(true);
		const Seen s = g_seen;
		Frame(false);
		Settle(2);
		return s;
	}

	bool MayLeave(const Seen& a_s)
	{
		return navside::LeftPressMayLeave({ true, a_s.anyActive, a_s.inPopup, a_s.onArrow, a_s.tookLeft });
	}
}

int main()
{
	// 1. the decision
	using navside::LeftPress;
	Check(navside::LeftPressMayLeave({ true, false, false, false, false }), "a plain row with nothing to the left: Left may go back to the list");
	Check(!navside::LeftPressMayLeave({ false, false, false, false, false }), "the page does not hold the highlight: no");
	Check(!navside::LeftPressMayLeave({ true, true, false, false, false }), "an item is being edited (held slider, typing): no");
	Check(!navside::LeftPressMayLeave({ true, false, true, false, false }), "the highlight is in a popup (an open combo): no");
	Check(!navside::LeftPressMayLeave({ true, false, false, true, false }), "the highlight is on a slider or drag: no");
	Check(!navside::LeftPressMayLeave({ true, false, false, false, true }), "a widget took the press itself: no");

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(1920, 1080);
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	BuildFonts();
	Settle(3);

	// 2 + 3. the slider: the cause, and the fix
	FocusOn(kPrecise);
	Check(GImGui->NavId == g_ids[kPrecise] && g_ids[kPrecise] != 0, "the highlight sits on AMF Example's Speed slider");
	{
		const float before = g_speed;
		const Seen s = TapLeft();
		Check(s.activeId == 0 && !s.anyActive, "CAUSE: during the Left press the slider is NOT active - IsAnyItemActive() is false, the only guard before 2.3.0");
		Check(navside::LeftPressMayLeave({ true, s.anyActive, s.inPopup, false, s.tookLeft }),
			  "CAUSE: without the slider note the decision lets the press leave the page (the 2.2.x behaviour)");
		Check(s.onArrow, "FIX: NavOnArrowItem sees the highlight on the precise slider");
		Check(!MayLeave(s), "FIX: a single Left on the precise slider stays on the page");
		Check(GImGui->NavId == g_ids[kPrecise], "after the tap ImGui's highlight is still on the slider (nothing to its left)");
		Check(g_speed == before, "an unactivated slider's value is not changed by the tap (ImGui moves nothing)");
	}
	{
		// held: key repeat - every frame of the hold keeps reading "on a slider", and the highlight never moves
		bool allOn = true, stayed = true;
		for (int i = 0; i < 60; ++i)   // one second at 60 fps, past ImGui's 0.275 s repeat delay
		{
			Frame(true);
			allOn = allOn && g_seen.onArrow && !MayLeave(g_seen);
			stayed = stayed && GImGui->NavId == g_ids[kPrecise];
		}
		Frame(false);
		Settle(2);
		Check(allOn, "a held Left on the precise slider reads 'stays' on every frame of a one-second hold");
		Check(stayed, "a held Left keeps ImGui's highlight on the slider throughout");
	}
	FocusOn(kConsumer);
	{
		const Seen s = TapLeft();
		Check(GImGui->NavId == g_ids[kConsumer] && s.onArrow && !MayLeave(s), "a mod's slider (igSliderScalar's body: slider + wheel helper) stays on Left");
	}
	FocusOn(kDrag);
	{
		const Seen s = TapLeft();
		Check(GImGui->NavId == g_ids[kDrag] && s.onArrow && !MayLeave(s), "a mod's drag (igDragFloat's body + amf_NoteArrowItem) stays on Left");
	}
	{
		// the three-value slider: put the highlight on its middle field by moving right from the first via nav
		// (the fields are separate items inside a group; the group is what NoteArrowItem sees last)
		ImGuiID tripleField = 0;
		FocusOn(kDrag);
		// down once from the drag lands on the triple's first field
		io.AddKeyEvent(ImGuiKey_DownArrow, true);  Frame();
		io.AddKeyEvent(ImGuiKey_DownArrow, false); Settle(2);
		tripleField = GImGui->NavId;
		const bool onTriple = tripleField != 0 && tripleField != g_ids[kDrag] && tripleField != g_ids[kCustom];
		io.AddKeyEvent(ImGuiKey_RightArrow, true);  Frame();
		io.AddKeyEvent(ImGuiKey_RightArrow, false); Settle(2);
		const ImGuiID middle = GImGui->NavId;
		const Seen s = TapLeft();
		Check(onTriple && middle != tripleField, "the highlight reached the three-value slider's second field");
		Check(s.onArrow && !MayLeave(s), "a mod's three-value slider (igSliderFloat3's body + amf_NoteArrowItem) reads 'stays' on Left, field by field");
	}
	FocusOn(kCustom);
	{
		const int before = g_custom;
		const Seen s = TapLeft();
		Check(g_custom == before - 1, "the self-stepping widget stepped its own value on Left");
		Check(!s.onArrow && s.tookLeft && !MayLeave(s), "a widget that cancels ImGui's Left move (DEM's way) keeps the press on the page");
	}
	FocusOn(kTree);
	{
		const Seen s = TapLeft();
		Check(s.tookLeft && !MayLeave(s), "Left on an open tree node (ImGui closes it and cancels the move) stays on the page");
	}
	FocusOn(kCombo);
	{
		Frame(false, true);   // Enter opens the combo
		Frame(false, false);
		Settle(3);
		const Seen s = TapLeft();
		Check(s.inPopup && !MayLeave(s), "Left inside an open combo stays on the page");
		ImGui::ClosePopupToLevel(0, true);   // 1.90.8 internal: close every popup
		Settle(3);
	}
	FocusOn(kRow);
	{
		const Seen s = TapLeft();
		Check(!s.onArrow && !s.inPopup && !s.tookLeft && !s.anyActive, "a plain button row: no guard fires");
		Check(MayLeave(s) && GImGui->NavId == g_ids[kRow], "a plain row with nothing to its left: Left still goes back to the list (ImGui moved nothing)");
	}
	FocusOn(kPrecise);
	{
		// activated with Enter: the slider is being edited - the old guard, still in place
		Frame(false, true);
		Frame(false, false);
		Settle(1);
		const Seen s = TapLeft();
		Check(s.anyActive && !MayLeave(s), "a slider activated with Enter (being edited) stays on the page, as before");
		Frame(false, true); Frame(false, false); Settle(2);   // Enter again lets it go
	}

	ImGui::DestroyContext();
	std::cout << "nav_left_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
