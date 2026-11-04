// Off-game check of how a slider takes input and when a Left press inside a mod's page may send the highlight back to the
// mod list (AMF 2.4.0; the owner, 2026-11-03: the D-pad could not leave a slider). Runs AMF's own ImGui build (1.90.8 with the
// overlay patches, the SE line's vcpkg tree) headless, with keyboard navigation on, as the menu runs it.
//   tools\nav_left_check.bat   -> one line per check and "nav_left_check: N of N passed"; exits 1 on any failure.
// What it proves:
//   1. the decision (navside::LeftPressMayLeave): each guard on its own keeps the press on the page; with none it may leave;
//   2-3. on live frames, a HIGHLIGHTED slider or drag (AMF's precise slider, a consumer-shaped slider, a drag, a three-value
//      slider) is a row: Left may leave, its value never moves, nothing activates it, also while the key is held. On a widget
//      that steps its own value and cancels ImGui's move and on an open tree node, PageTookTheMove sees it; inside an open
//      combo, NavInPopup sees it. An activated slider is being edited: Left adjusts it and stays;
//   5. the same for widgets drawn as a mod with its OWN copy of Dear ImGui draws them (AMF Example). Text and number-entry
//      fields keep the press: AMF's and the exports' note themselves, a mod's own are known once typed in;
//      Enter on a slider activates it (no number box); Escape in an open dropdown closes only the dropdown; a band slider in
//      a table: not activated, Right moves to the next column; activated, Right / Left step it and the column stays;
//   8. activate, nudge, let go: one unit of the shown digit (precise and export sliders), a held key at the key-repeat rate at
//      60 and 144 fps, one apply a step; the controller's own sequence (A, D-pad, A or B; Down lets go and moves on); Escape
//      lets go without closing the menu; the mouse drags directly; and two mods' own helpers, which only those mods can drop.
// What it cannot: Renderer.cpp's own branch order and the game's input path (Input.cpp turns key and pad records into
// ImGui key events) - the in-game test covers those. tools\page_switch_check.bat covers B inside the menu's own panes.
#include "../include/PreciseSlider.h"
#include "../include/NavSide.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <utility>

namespace
{
	int g_pass = 0, g_fail = 0;
	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	enum Item { kRow, kPrecise, kConsumer, kDrag, kTriple, kCustom, kTree, kCombo, kPlain, kPlainDrag, kText, kNoInput, kBandA, kBandB,
				kExport, kText2, kDsA, kDsB, kDem, kNumber, kPlainNumber, kCount };
	ImGuiID g_ids[kCount] = {};
	ImVec2  g_mid[kCount] = {};     // each item's centre on screen (for a mouse click)
	int     g_focusRequest = -1;   // focus this item (SetFocusID) the next frame it is drawn

	float  g_speed = 0.5f;          // AMF Example's Speed: precise::SliderFloat 0..1 "%.2f"
	int    g_speedApplies = 0;      // how often precise::ApplyNow said "apply" for Speed (Window opacity's theme::Apply, S10)
	double g_scale = 0.5;           // a consumer's igSliderScalar (double) 0..1 "%.3f"
	float  g_drag = 1.0f;           // a consumer's igDragFloat
	float  g_triple[3] = { 1, 2, 3 };
	int    g_custom = 10;           // a page widget that steps its own value on Left and cancels ImGui's move (DEM's way)
	int    g_combo = 0;
	float  g_plain = 1.0f;          // AMF Example's Speed as it really runs: its OWN copy of ImGui, no NoteArrowItem (26d2 N1-N5)
	float  g_plainDrag = 5.0f;      // a plain drag from a mod's own ImGui
	char   g_text[32] = "Dovahkiin";// a plain text field from a mod's own ImGui
	float  g_noInput = 50.0f;       // a NoInput slider from a mod's own ImGui (no Inputable flag)
	float  g_bandA = 10.0f;         // Deep Snow's band table: "Starts at depth" ...
	float  g_bandB = 60.0f;         // ... and the "Speed" column beside it (04f6 step 8 / 26d2 notes)
	float  g_export = 50.0f;        // a mod's slider through igSliderFloat's generated body: 0..100 "%.1f" (ImGui alone: 1.0 a nudge)
	char   g_text2[32] = "Lydia";   // a text field nobody has typed in yet (no edit state of its own)
	float  g_dsA = 10.0f;           // Deep Snow 1.1.0's band table, as it really runs: its precise wrapper around the export ...
	float  g_dsB = 60.0f;           // ... plus precise::HighlightNudge (include\SliderNudge.h), in neighbouring columns
	float  g_dem = 0.50f;           // Dragon's Eye Minimap 1.7.5's NudgeableSlider (UI.cpp) around its precise wrapper, 0..1 "%.2f"
	const char* g_demSelected = nullptr;   // DEM's selectedSlider
	int    g_number = 5;            // a number box AMF or an export draws (+ NoteEntryField)
	float  g_plainNumber = 1.5f;    // a number box from a mod's own ImGui
	bool   g_enterTweaked = false;  // EnterActivatesArrowItem turned this frame's Enter into an activation
	bool   g_clickActivated = false;// ActivateAfterClick turned the first press after a mouse click into an activation
	bool   g_escBefore = false;     // EscapeClosesAPopup() read just before NewFrame (Renderer.cpp's escClosesPopup)
	bool   g_escHeld = false;       // SliderHeldByKeys() read just before NewFrame (Renderer.cpp's escLetsGo)
	int    g_popupsAfterNewFrame = 0;

	struct Seen { bool onField, inPopup, tookLeft, anyActive; ImGuiID navId, activeId; };
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
		g_mid[a_item] = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
		if (g_focusRequest == a_item)
		{
			ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
			GImGui->NavDisableHighlight = false;
			g_focusRequest = -1;
		}
	}

	// igSliderFloat's generated body (tools\harden-cimgui.py): the value before, ImGui's slider, the one-unit nudge, the wheel
	bool ExportSliderFloat(const char* a_label, float* a_v, float a_min, float a_max, const char* a_format, ImGuiSliderFlags a_flags = 0)
	{
		const double before = precise::SliderValue(ImGuiDataType_Float, a_v);
		const bool r = ImGui::SliderFloat(a_label, a_v, a_min, a_max, a_format, a_flags);
		const bool n = precise::NavNudge(ImGuiDataType_Float, a_v, before, &a_min, &a_max, precise::FormatStep(a_format, false), a_flags);
		const bool w = precise::WheelNudge(ImGuiDataType_Float, a_v, &a_min, &a_max, precise::FormatStep(a_format, false));
		return r || n || w;
	}

	bool ArrowOrPadSideDown()
	{
		return ImGui::IsKeyDown(ImGuiKey_LeftArrow) || ImGui::IsKeyDown(ImGuiKey_RightArrow) || ImGui::IsKeyDown(ImGuiKey_GamepadDpadLeft) ||
			   ImGui::IsKeyDown(ImGuiKey_GamepadDpadRight);
	}

	// Deep Snow 1.1.0 / DEM 1.7.5's precise::SliderFloat (their include\PreciseSlider.h), written against ImGui instead of
	// ImGuiMCP: the export, then a nudge (a side key down, the mouse up) held to one unit from the value before the call
	bool ModPrecise(const char* a_label, float* a_v, float a_min, float a_max, const char* a_format)
	{
		const float before = *a_v;
		if (!ExportSliderFloat(a_label, a_v, a_min, a_max, a_format, ImGuiSliderFlags_AlwaysClamp)) return false;
		*a_v = std::clamp(*a_v, a_min, a_max);
		const bool nudging = !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
							 (ArrowOrPadSideDown() || ImGui::IsKeyDown(ImGuiKey_GamepadLStickLeft) || ImGui::IsKeyDown(ImGuiKey_GamepadLStickRight));
		if (*a_v != before && nudging)
		{
			const double step = precise::Step(a_format);
			double v = static_cast<double>(before) + (*a_v > before ? step : -step);
			v = std::round(v / step) * step;
			*a_v = static_cast<float>(std::clamp(v, static_cast<double>(a_min), static_cast<double>(a_max)));
		}
		return true;
	}

	// Deep Snow 1.1.0's precise::HighlightNudge (include\SliderNudge.h), against ImGui instead of ImGuiMCP
	bool HighlightNudge(float* a_v, float a_min, float a_max, const char* a_format)
	{
		if (!a_v || !ImGui::IsItemFocused() || ImGui::IsItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left)) return false;
		const bool left = ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true);
		const bool right = ImGui::IsKeyPressed(ImGuiKey_RightArrow, true) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true);
		if (left || right || ArrowOrPadSideDown()) ImGui::NavMoveRequestCancel();
		if (left == right) return false;
		const float before = *a_v;
		const double step = precise::Step(a_format);
		double v = static_cast<double>(before) + (right ? step : -step);
		v = std::round(v / step) * step;
		*a_v = static_cast<float>(std::clamp(v, static_cast<double>((std::min)(a_min, a_max)), static_cast<double>((std::max)(a_min, a_max))));
		return *a_v != before;
	}

	// Dragon's Eye Minimap 1.7.5's NudgeableSlider (source\UI.cpp), against ImGui instead of ImGuiMCP
	bool NudgeableSlider(const char* a_label, float* a_value, float a_min, float a_max, const char* a_format)
	{
		bool changed = ModPrecise(a_label, a_value, a_min, a_max, a_format);
		g_ids[kDem] = ImGui::GetItemID();
		g_mid[kDem] = ImVec2((ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5f, (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f);
		if (g_focusRequest == kDem) { ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow()); GImGui->NavDisableHighlight = false; g_focusRequest = -1; }
		if (ImGui::IsItemClicked() || ImGui::IsItemActive()) g_demSelected = a_label;
		if (g_demSelected == a_label)
		{
			const float step = static_cast<float>(precise::Step(a_format));
			float nudge = 0.0f;
			if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_DownArrow)) nudge -= step;
			if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)) nudge += step;
			if (nudge != 0.0f && !changed) { *a_value = std::clamp(*a_value + nudge, a_min, a_max); changed = true; }
			const bool arrowHeld = ImGui::IsKeyDown(ImGuiKey_LeftArrow) || ImGui::IsKeyDown(ImGuiKey_RightArrow) ||
								   ImGui::IsKeyDown(ImGuiKey_UpArrow) || ImGui::IsKeyDown(ImGuiKey_DownArrow);
			if (nudge != 0.0f || arrowHeld) ImGui::NavMoveRequestCancel();
			ImGui::SameLine();
			ImGui::TextDisabled("<-->");
		}
		return changed;
	}

	void Frame(bool a_left = false, bool a_enter = false, float a_dt = 1.0f / 60.0f, bool a_right = false)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.AddKeyEvent(ImGuiKey_LeftArrow, a_left);
		io.AddKeyEvent(ImGuiKey_RightArrow, a_right);
		io.AddKeyEvent(ImGuiKey_Enter, a_enter);
		io.DeltaTime = a_dt;
		g_escBefore = navside::EscapeClosesAPopup();
		g_escHeld = navside::SliderHeldByKeys();
		ImGui::NewFrame();
		g_popupsAfterNewFrame = GImGui->OpenPopupStack.Size;
		g_clickActivated = navside::ActivateAfterClick();      // Renderer.cpp: right after NewFrame, first
		g_enterTweaked = navside::EnterActivatesArrowItem();   // Renderer.cpp: then this
		navside::HoldTweakAtKeyRepeat();   // Renderer.cpp: then this (S8: a held key steps at the key-repeat rate)
		ImGui::SetNextWindowPos(ImVec2(20, 20));
		ImGui::SetNextWindowSize(ImVec2(1200, 1040));
		ImGui::Begin("###amf-page", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		ImGui::Button("A plain row");
		Mark(kRow);
		ImGui::SetNextItemWidth(500);
		// AMF's own / a converted menu's slider; applied the way Renderer.cpp applies Window opacity (S10: precise::ApplyNow)
		if (precise::ApplyNow(precise::SliderFloat("Speed", &g_speed, 0.0f, 1.0f, "%.2f"))) { ++g_speedApplies; }
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
		// widgets drawn the way a mod with its OWN copy of Dear ImGui draws them (AMF Example): plain ImGui calls, nothing of
		// AMF's after them - only ImGui's own state says what they are
		ImGui::SetNextItemWidth(500);
		ImGui::SliderFloat("Plain speed", &g_plain, 0.5f, 3.0f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		Mark(kPlain);
		ImGui::SetNextItemWidth(500);
		ImGui::DragFloat("Plain drag", &g_plainDrag, 0.1f, 0.0f, 10.0f, "%.1f");
		Mark(kPlainDrag);
		ImGui::SetNextItemWidth(500);
		ImGui::InputText("Plain name", g_text, sizeof(g_text));
		Mark(kText);
		ImGui::SetNextItemWidth(500);
		ImGui::SliderFloat("No-input slider", &g_noInput, 0.0f, 100.0f, "%.0f", ImGuiSliderFlags_NoInput);
		Mark(kNoInput);
		// Deep Snow's band table: plain sliders in neighbouring columns
		if (ImGui::BeginTable("##bands", 2, ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::SliderFloat("##depth0", &g_bandA, 0.0f, 150.0f, "%.0f %%", ImGuiSliderFlags_AlwaysClamp);
			Mark(kBandA);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::SliderFloat("##speed0", &g_bandB, 10.0f, 100.0f, "%.0f %%", ImGuiSliderFlags_AlwaysClamp);
			Mark(kBandB);
			ImGui::EndTable();
		}
		// a mod's slider through the export, a fresh text field, Deep Snow 1.1.0's band table and DEM's slider
		ImGui::SetNextItemWidth(500);
		ExportSliderFloat("Export slider", &g_export, 0.0f, 100.0f, "%.1f");
		Mark(kExport);
		ImGui::SetNextItemWidth(500);
		ImGui::InputText("Fresh name", g_text2, sizeof(g_text2));
		Mark(kText2);
		if (ImGui::BeginTable("##dsbands", 2, ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			ModPrecise("##dsdepth0", &g_dsA, 0.0f, 150.0f, "%.0f %%");
			HighlightNudge(&g_dsA, 0.0f, 150.0f, "%.0f %%");   // Deep Snow UI.cpp: right after the slider
			Mark(kDsA);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0f);
			ModPrecise("##dsspeed0", &g_dsB, 10.0f, 100.0f, "%.0f %%");
			HighlightNudge(&g_dsB, 10.0f, 100.0f, "%.0f %%");
			Mark(kDsB);
			ImGui::EndTable();
		}
		ImGui::SetNextItemWidth(500);
		NudgeableSlider("DEM Offset", &g_dem, 0.0f, 1.0f, "%.2f");
		ImGui::SetNextItemWidth(300);
		ImGui::InputInt("Number", &g_number, 1, 10);
		navside::NoteEntryField();
		Mark(kNumber);
		ImGui::SetNextItemWidth(300);
		ImGui::InputFloat("Plain number", &g_plainNumber, 0.0f, 0.0f, "%.2f");
		Mark(kPlainNumber);
		navside::LearnNavItemKind();   // Renderer.cpp: after the page, before the Left decision
		g_seen = { navside::NavOnEntryField(false), navside::NavInPopup(), navside::PageTookTheMove(ImGuiDir_Left), ImGui::IsAnyItemActive(),
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
		return navside::LeftPressMayLeave({ true, a_s.anyActive, a_s.inPopup, a_s.onField, a_s.tookLeft });
	}
}

int main()
{
	// 1. the decision
	using navside::LeftPress;
	Check(navside::LeftPressMayLeave({ true, false, false, false, false }), "a plain row or a highlighted slider with nothing to the left: Left may go back to the list");
	Check(!navside::LeftPressMayLeave({ false, false, false, false, false }), "the page does not hold the highlight: no");
	Check(!navside::LeftPressMayLeave({ true, true, false, false, false }), "an item is being edited (an activated slider, typing): no");
	Check(!navside::LeftPressMayLeave({ true, false, true, false, false }), "the highlight is in a popup (an open combo): no");
	Check(!navside::LeftPressMayLeave({ true, false, false, true, false }), "the highlight is on a text or number-entry field: no");
	Check(!navside::LeftPressMayLeave({ true, false, false, false, true }), "a widget took the press itself: no");

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(1920, 1080);
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	BuildFonts();
	Settle(3);

	// 2 + 3. a highlighted slider is a row like any other (the owner, 2026-11-03: D-pad left stayed on the slider and moved it)
	FocusOn(kPrecise);
	Check(GImGui->NavId == g_ids[kPrecise] && g_ids[kPrecise] != 0, "the highlight sits on AMF Example's Speed slider");
	{
		const float before = g_speed;
		const Seen s = TapLeft();
		Check(s.activeId == 0 && !s.anyActive, "during the Left press the highlighted slider is NOT active");
		Check(MayLeave(s), "FIX: a Left on a highlighted precise slider with nothing to its left goes back to the list");
		Check(g_speed == before, "FIX: ... and its value does not move");
	}
	{
		// held: ImGui's move repeats, nothing steps the value
		const float before = g_speed;
		bool leaves = true;
		for (int i = 0; i < 60; ++i)
		{
			Frame(true);
			leaves = leaves && MayLeave(g_seen) && GImGui->ActiveId == 0;
		}
		Frame(false);
		Settle(2);
		Check(leaves && g_speed == before, "a held Left on a highlighted slider never activates it and never moves it");
	}
	FocusOn(kConsumer);
	{
		const double before = g_scale;
		const Seen s = TapLeft();
		Check(MayLeave(s) && g_scale == before, "a mod's slider (igSliderScalar's body: slider + wheel helper): Left may leave, value kept");
	}
	FocusOn(kDrag);
	{
		const float before = g_drag;
		const Seen s = TapLeft();
		Check(MayLeave(s) && g_drag == before, "a mod's drag (igDragFloat's body + amf_NoteArrowItem): Left may leave, value kept");
	}
	{
		// the three-value slider: put the highlight on its middle field by moving right from the first via nav. Left moves back
		// to the first field (Renderer.cpp sees ImGui moved and stays); Left on the first field may leave.
		ImGuiID tripleField = 0;
		FocusOn(kDrag);
		io.AddKeyEvent(ImGuiKey_DownArrow, true);  Frame();
		io.AddKeyEvent(ImGuiKey_DownArrow, false); Settle(2);
		tripleField = GImGui->NavId;
		const bool onTriple = tripleField != 0 && tripleField != g_ids[kDrag] && tripleField != g_ids[kCustom];
		io.AddKeyEvent(ImGuiKey_RightArrow, true);  Frame();
		io.AddKeyEvent(ImGuiKey_RightArrow, false); Settle(2);
		const ImGuiID middle = GImGui->NavId;
		TapLeft();
		Check(onTriple && middle != tripleField && GImGui->NavId == tripleField, "a three-value slider, not activated: Left walks back field by field");
		const float before = g_triple[0];
		const Seen s = TapLeft();
		Check(MayLeave(s) && g_triple[0] == before, "... and from the first field it may leave, value kept");
	}
	FocusOn(kCustom);
	{
		const int before = g_custom;
		const Seen s = TapLeft();
		Check(g_custom == before - 1, "the self-stepping widget stepped its own value on Left");
		Check(s.tookLeft && !MayLeave(s), "a widget that cancels ImGui's Left move keeps the press on the page");
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
		Check(!s.onField && !s.inPopup && !s.tookLeft && !s.anyActive, "a plain button row: no guard fires");
		Check(MayLeave(s) && GImGui->NavId == g_ids[kRow], "a plain row with nothing to its left: Left still goes back to the list (ImGui moved nothing)");
	}
	FocusOn(kPrecise);
	{
		// activated with Enter: the slider is being edited, so Left adjusts it and stays
		Frame(false, true);
		Frame(false, false);
		Settle(1);
		const Seen s = TapLeft();
		Check(s.anyActive && !MayLeave(s), "a slider activated with Enter (being edited) stays on the page");
		Frame(false, true); Frame(false, false); Settle(2);   // Enter again lets it go
	}

	// 5. a mod drawing with its OWN copy of Dear ImGui (AMF Example's real Speed slider): no note is ever called
	auto reach = [&](Item a_item) {   // the highlight arrives as a player brings it: keyboard Down from the item above
		FocusOn(static_cast<Item>(a_item - 1));
		io.AddKeyEvent(ImGuiKey_DownArrow, true);  Frame();
		io.AddKeyEvent(ImGuiKey_DownArrow, false); Settle(2);
		return GImGui->NavId == g_ids[a_item];
	};
	auto press = [&](ImGuiKey a_key) {
		io.AddKeyEvent(a_key, true);  Frame();
		io.AddKeyEvent(a_key, false); Frame();
	};
	{
		const bool there = reach(kPlain);
		const float before = g_plain;
		const Seen s = TapLeft();
		Check(there, "keyboard Down brought the highlight onto the plain slider (a mod's own ImGui)");
		Check(MayLeave(s) && g_plain == before, "a Left on a plain slider from a mod's own ImGui may leave, value unchanged (no activation by AMF)");
		bool quiet = true;
		for (int i = 0; i < 90; ++i) { Frame(true); quiet = quiet && GImGui->ActiveId == 0; }
		Frame(false); Settle(2);
		Check(quiet && g_plain == before, "a held Left (1.5 s) on it activates nothing and changes nothing");
	}
	{
		// the highlight put there by a click on the knob (pressed and let go), not by a key
		FocusOn(kRow);
		const float before = g_plain;
		io.AddMousePosEvent(g_mid[kPlain].x, g_mid[kPlain].y); Frame();
		io.AddMouseButtonEvent(0, true);  Frame();
		io.AddMouseButtonEvent(0, false); Frame();
		g_plain = before;   // the click itself may move the value; only the highlight matters here
		io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); Settle(2);
		const bool onIt = GImGui->NavId == g_ids[kPlain];
		const Seen s = TapLeft();
		Check(onIt && MayLeave(s) && g_plain == before, "after a click on a plain slider, Left does not step it and may leave");
	}
	{
		const bool there = reach(kNoInput);
		const Seen s0 = TapLeft();
		Check(there && MayLeave(s0), "a NoInput slider: Left may leave");
		press(ImGuiKey_Space);   // Space activates it
		const bool active = GImGui->ActiveId == g_ids[kNoInput];
		press(ImGuiKey_Space);   // Space again lets it go
		Settle(2);
		const Seen s1 = TapLeft();
		Check(active && MayLeave(s1), "... activated and let go, it is a row again");
	}
	{
		const bool there = reach(kPlainDrag);
		const Seen s = TapLeft();
		Check(there && MayLeave(s), "a plain drag from a mod's own ImGui may leave");
	}
	// text and number-entry fields keep the press, as they did
	{
		reach(kNumber);   // a number box with - and + buttons: the highlight lands on its text part
		const int before = g_number;
		const Seen s = TapLeft();
		Check(s.onField && !MayLeave(s) && g_number == before, "a number box AMF or the exports drew (NoteEntryField): Left stays on the page, unchanged");
	}
	{
		const Seen n = [&] { FocusOn(kPlainNumber); return TapLeft(); }();
		Check(MayLeave(n), "LIMIT: a number box from a mod's own ImGui, never typed in, cannot be told from its slider: Left may leave");
		press(ImGuiKey_Enter);   // Enter starts typing in it
		const bool typing = GImGui->ActiveId != 0;
		press(ImGuiKey_Escape);
		Settle(2);
		const Seen s = TapLeft();
		Check(typing && s.onField && !MayLeave(s), "... typed in once, it is known and keeps the press");
	}
	{
		const bool there = reach(kText);
		const Seen s0 = TapLeft();
		Frame(false, true); Frame(false, false); Frame();
		const bool typing = GImGui->ActiveId == g_ids[kText];
		Check(there && typing, "Enter on a text field still starts typing in it");
		press(ImGuiKey_Escape);
		Settle(2);
		const Seen s = TapLeft();
		Check(MayLeave(s0) && s.onField && !MayLeave(s), "a text field from a mod's own ImGui: known once typed in, then Left stays on the page");
	}
	Check(navside::LeftPressMayLeave({ true, false, false, navside::NavOnEntryField(false), false }) == !navside::NavOnEntryField(false), "the decision follows the field check");
	{
		// Enter on the plain slider ACTIVATES it (no number box); Left steps it; Enter again lets it go
		const bool there = reach(kPlain);
		g_plain = 1.0f;
		Frame(false, true);
		const bool tweaked = g_enterTweaked;
		Frame(false, false);
		const bool active = GImGui->ActiveId == g_ids[kPlain];
		const bool box = ImGui::TempInputIsActive(g_ids[kPlain]);
		Check(there && tweaked && active && !box, "Enter on a plain slider activates it - no number box opens");
		const float before = g_plain;
		Frame(true); Frame(false); Settle(1);
		Check(g_plain < before && GImGui->NavId == g_ids[kPlain], "... Left then steps its value down and the highlight stays on it");
		Frame(false, true); Frame(false, false); Settle(2);
		Check(GImGui->ActiveId == 0 && !ImGui::TempInputIsActive(g_ids[kPlain]), "... Enter again lets it go (no number box)");
	}
	{
		// Escape in an open dropdown: ImGui's NewFrame closes the popup, so after NewFrame - where the menu read Escape as
		// "close the menu" - nothing shows it was open; read before NewFrame, it is seen
		FocusOn(kCombo);
		Frame(false, true); Frame(false, false); Settle(2);   // Enter opens the combo
		const bool open = GImGui->OpenPopupStack.Size > 0;
		io.AddKeyEvent(ImGuiKey_Escape, true); Frame();
		const bool before = g_escBefore;
		const int after = g_popupsAfterNewFrame;
		io.AddKeyEvent(ImGuiKey_Escape, false); Settle(2);
		Check(open && after == 0, "CAUSE (Esc in a dropdown): ImGui's NewFrame already closed the dropdown before the menu reads Escape");
		Check(before, "FIX: read before NewFrame, the open dropdown is seen - Escape closes it and the menu stays open");
		Frame(); const bool none = !g_escBefore;
		Check(none, "with no dropdown open, Escape still closes the menu (nothing seen before NewFrame)");
	}
	{
		// Deep Snow's band slider in a table: NOT activated, Right moves to the next column; ACTIVATED, Right / Left step it
		const bool there = reach(kBandA);
		Frame(false, false, 1.0f / 60.0f, true); Frame(); Settle(2);
		const bool movedOver = GImGui->NavId == g_ids[kBandB];
		Check(there && movedOver && g_bandA == 10.0f, "a band slider in a table, not activated: Right moves to the next column, value unchanged");
		FocusOn(kBandA);
		Frame(false, true); Frame(false, false);   // Enter activates it
		const bool active = GImGui->ActiveId == g_ids[kBandA] && !ImGui::TempInputIsActive(g_ids[kBandA]);
		Frame(false, false, 1.0f / 60.0f, true); Frame(); Settle(1);
		const float afterRight = g_bandA;
		const bool stayed = GImGui->NavId == g_ids[kBandA];
		Frame(true); Frame(); Settle(1);
		const float afterLeft = g_bandA;
		Check(active && afterRight > 10.0f && stayed, "... activated with Enter: Right steps the value up and the highlight stays in the column");
		Check(afterLeft < afterRight && GImGui->NavId == g_ids[kBandA], "... Left steps it back down, still on the slider");
		Frame(false, true); Frame(false, false); Settle(2);
		Check(GImGui->ActiveId == 0, "... Enter lets it go");
	}

	// 8. Activate, nudge, let go (2.4.0): one unit of the shown digit a press, a held key at the key-repeat rate, one apply a step
	{
		const float dt60 = 1.0f / 60.0f, dt144 = 1.0f / 144.0f;
		auto near = [](float a_a, float a_b) { return std::fabs(a_a - a_b) < 1e-4f; };
		auto tapSide = [&](bool a_right, float a_dt = 1.0f / 60.0f) {
			Frame(!a_right, false, a_dt, a_right);
			Frame(false, false, a_dt, false);
			Settle(1);
		};
		auto enter = [&] { Frame(false, true); Frame(false, false); };
		struct Hold { float maxStep = 0.0f; float total = 0.0f; bool stayed = true; };
		auto holdRight = [&](Item a_item, const float* a_v, int a_frames, float a_dt) {
			Hold h{};
			float last = *a_v;
			const float first = *a_v;
			for (int i = 0; i < a_frames; ++i)
			{
				Frame(false, false, a_dt, true);
				h.maxStep = (std::max)(h.maxStep, std::fabs(*a_v - last));
				last = *a_v;
				h.stayed = h.stayed && GImGui->NavId == g_ids[a_item];
			}
			Frame(false, false, a_dt, false);
			Settle(1);
			h.total = *a_v - first;
			return h;
		};

		// b. AMF's own precise slider (AMF Example's Speed shape, 0..1 "%.2f"): one unit of the shown digit
		{
			FocusOn(kPrecise);
			g_speed = 0.50f;
			tapSide(true);
			Check(near(g_speed, 0.50f) && GImGui->ActiveId == 0, "8b: Right on a highlighted precise slider moves nothing and activates nothing");
			enter();
			const bool held = GImGui->ActiveId == g_ids[kPrecise];
			tapSide(true);
			const float afterRight = g_speed;
			tapSide(false);
			Check(held && near(afterRight, 0.51f) && near(g_speed, 0.50f) && GImGui->NavId == g_ids[kPrecise] && GImGui->ActiveId == g_ids[kPrecise],
				  "8b: activated: Right 0.50 -> 0.51, Left back to 0.50, highlight kept, it stays active after the key is let go");
			enter();
			Settle(1);
			Check(GImGui->ActiveId == 0, "8b: ... Enter lets it go");
			// S8 (combined run 3266, 2026-10-25: Skyrim crashed holding Right on DEM's Offset X): a HELD key steps at the
			// key-repeat rate - the first press, then after io.KeyRepeatDelay (0.275 s) one step every io.KeyRepeatRate (0.050 s):
			// about 1 + (1 - 0.275) / 0.05 = 15 in a one-second hold, at any frame rate. ImGui's own nav tweak repeats at
			// 0.72 x delay and 0.30 x rate (0.198 s / 0.015 s, about 54 a second) - one a frame at 60 fps.
			auto steps = [&](int a_frames, float a_dt) {
				FocusOn(kPrecise);
				g_speed = 0.0f;
				enter();
				Settle(1);
				const Hold h = holdRight(kPrecise, &g_speed, a_frames, a_dt);
				enter();
				Settle(1);
				return std::pair<int, Hold>{ static_cast<int>(std::lround(h.total / 0.01f)), h };
			};
			const auto [n60, h60] = steps(60, dt60);
			const auto [n144, h144] = steps(144, dt144);
			std::printf("    (S8 hold: %d steps in 1 s at 60 fps, %d at 144 fps; io repeat delay %.3f, rate %.3f after the hold)\n", n60, n144,
				io.KeyRepeatDelay, io.KeyRepeatRate);
			Check(n60 >= 13 && n60 <= 17 && h60.stayed && h60.maxStep < 0.01f + 1e-4f,
				  "8b S8: Right HELD 1 s at 60 fps on an activated precise slider: about 15 steps (the key-repeat rate), not one a frame; highlight kept");
			Check(n144 >= 13 && n144 <= 17 && h144.stayed && h144.maxStep < 0.01f + 1e-4f,
				  "8b S8: ... held 1 s at 144 fps: the same about 15, not 54 or 144");
			Check(near(io.KeyRepeatDelay, 0.275f) && near(io.KeyRepeatRate, 0.050f), "8b S8: ... and ImGui's repeat settings are untouched");
			// S10: a slider applied through precise::ApplyNow applies once a step, and nothing again when it is let go
			FocusOn(kPrecise);
			g_speed = 0.50f;
			enter();
			g_speedApplies = 0;
			tapSide(true);
			const int tapApplies = g_speedApplies;
			tapSide(true);
			const int twoApplies = g_speedApplies;
			enter();
			Settle(1);
			std::printf("    (S10: %d apply(ies) for one tap, %d for two, %d after the let-go)\n", tapApplies, twoApplies, g_speedApplies);
			Check(near(g_speed, 0.52f) && tapApplies == 1 && twoApplies == 2 && g_speedApplies == 2,
				  "8b S10: activated, each tap through ApplyNow is ONE apply, and letting go adds none");
			g_speed = 0.50f;
		}
		// c. a mod's slider through the export (igSliderFloat's generated body), 0..100 "%.1f": ImGui alone moves 1.0 - the export holds it to 0.1
		{
			FocusOn(kExport);
			g_export = 50.0f;
			tapSide(true);
			const bool quiet = near(g_export, 50.0f) && GImGui->ActiveId == 0;
			enter();
			tapSide(true);
			Check(quiet && near(g_export, 50.1f) && GImGui->NavId == g_ids[kExport],
				  "8c: an export slider: highlighted, Right moves nothing; activated, 50.0 -> 50.1 (one unit of the shown digit, not ImGui's 1%)");
			const Hold h = holdRight(kExport, &g_export, 60, dt60);
			Check(h.stayed && h.total > 0.0f && h.maxStep < 0.1f + 1e-4f, "8c: ... held 1 s: it repeats, never more than 0.1 in a frame, highlight kept");
			enter();
			Settle(1);
		}
		// d. a drag: the same activation; its step stays ImGui's (the larger of its speed and the shown digit)
		{
			FocusOn(kDrag);
			g_drag = 1.0f;
			tapSide(true);
			const bool quiet = g_drag == 1.0f && GImGui->ActiveId == 0;
			enter();
			tapSide(true);
			const float afterRight = g_drag;
			enter();
			Settle(1);
			Check(quiet && near(afterRight, 1.1f) && GImGui->ActiveId == 0, "8d: a drag: highlighted, Right moves nothing; activated, Right steps 1.0 -> 1.1; Enter lets go");
		}
		// e. Deep Snow 1.1.0's own HighlightNudge and DEM's NudgeableSlider are in those mods, not in AMF
		{
			FocusOn(kDsA);
			g_dsA = 10.0f;
			tapSide(true);
			Check(g_dsA == 11.0f && GImGui->NavId == g_ids[kDsA] && GImGui->ActiveId == 0,
				  "8e LIMIT: Deep Snow 1.1.0's HighlightNudge steps its own band slider while only highlighted - until Deep Snow drops it");
			enter();
			tapSide(true);
			Check(g_dsA == 12.0f, "8e: ... activated it is one step a press, not two");
			enter();
			Settle(1);
			FocusOn(kRow);
			io.AddMousePosEvent(g_mid[kDem].x, g_mid[kDem].y); Frame();
			io.AddMouseButtonEvent(0, true);  Frame();
			io.AddMouseButtonEvent(0, false); Frame();
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); Settle(2);
			g_dem = 0.50f;   // the click itself moves the value; only the selection matters here
			Settle(1);
			enter();
			const bool active = GImGui->ActiveId == g_ids[kDem];
			tapSide(true);
			Check(active && near(g_dem, 0.51f), "8e: DEM's NudgeableSlider, activated: Right 0.50 -> 0.51, one step");
			const Hold h = holdRight(kDem, &g_dem, 144, dt144);
			const int demSteps = static_cast<int>(std::lround(h.total / 0.01f));
			std::printf("    (S8 DEM hold: %d steps in 1 s at 144 fps)\n", demSteps);
			Check(h.stayed && h.maxStep < 0.01f + 1e-4f && demSteps >= 13 && demSteps <= 17,
				  "8e S8: ... held 1 s at 144 fps: about 15 steps, never two in a frame - the hold that crashed DEM in combined run 3266");
			enter();
			Settle(2);
			g_demSelected = nullptr;   // DEM's selected slider cancels every arrow move while drawn; the rest runs as if its page were closed
		}
		// f. the controller (gamepad navigation on, keyboard navigation off): the owner's own sequence
		{
			io.ConfigFlags = (io.ConfigFlags | ImGuiConfigFlags_NavEnableGamepad) & ~ImGuiConfigFlags_NavEnableKeyboard;
			io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
			auto pad = [&](ImGuiKey a_key) {
				io.AddKeyEvent(a_key, true); Frame();
				const Seen s = g_seen;
				io.AddKeyEvent(a_key, false); Frame();
				Settle(1);
				return s;
			};
			FocusOn(kPrecise);
			g_speed = 0.50f;
			const Seen l = pad(ImGuiKey_GamepadDpadLeft);
			Check(MayLeave(l) && near(g_speed, 0.50f) && GImGui->ActiveId == 0, "8f: D-pad left on a highlighted slider: may go back to the list, value unmoved");
			pad(ImGuiKey_GamepadDpadRight);
			Check(near(g_speed, 0.50f) && GImGui->ActiveId == 0, "8f: D-pad right: value unmoved, not activated");
			pad(ImGuiKey_GamepadFaceDown);
			const bool active = GImGui->ActiveId == g_ids[kPrecise] && GImGui->ActiveIdSource == ImGuiInputSource_Gamepad;
			pad(ImGuiKey_GamepadDpadRight);
			const float r1 = g_speed;
			const Seen l2 = pad(ImGuiKey_GamepadDpadLeft);
			pad(ImGuiKey_GamepadDpadLeft);
			Check(active && near(r1, 0.51f) && near(g_speed, 0.49f) && !MayLeave(l2), "8f: A takes hold; D-pad right 0.50 -> 0.51, left 0.50, left 0.49; Left no longer leaves");
			pad(ImGuiKey_GamepadFaceDown);
			Check(GImGui->ActiveId == 0 && GImGui->NavId == g_ids[kPrecise], "8f: A lets go, highlight kept");
			pad(ImGuiKey_GamepadFaceDown);
			const bool again = GImGui->ActiveId == g_ids[kPrecise];
			pad(ImGuiKey_GamepadFaceRight);
			Check(again && GImGui->ActiveId == 0 && GImGui->NavId == g_ids[kPrecise], "8f: A takes hold again; B lets go, highlight kept (page_switch_check: B does not leave the page)");
			pad(ImGuiKey_GamepadFaceDown);
			pad(ImGuiKey_GamepadDpadDown);
			Check(GImGui->ActiveId == 0 && GImGui->NavId == g_ids[kConsumer], "8f: held, D-pad down lets go and moves to the next row");
			FocusOn(kExport);
			g_export = 50.0f;
			pad(ImGuiKey_GamepadFaceDown);
			pad(ImGuiKey_GamepadDpadRight);
			Check(near(g_export, 50.1f), "8f: an export slider from the pad: A, then D-pad right: 50.0 -> 50.1");
			pad(ImGuiKey_GamepadFaceRight);
			FocusOn(kPlain);
			g_plain = 1.0f;
			pad(ImGuiKey_GamepadDpadRight);
			const bool plainQuiet = g_plain == 1.0f;
			pad(ImGuiKey_GamepadFaceDown);
			pad(ImGuiKey_GamepadDpadRight);
			Check(plainQuiet && g_plain > 1.0f, "8f: a plain slider from a mod's own ImGui: the D-pad moves it only after A");
			pad(ImGuiKey_GamepadFaceRight);
			io.ConfigFlags = (io.ConfigFlags | ImGuiConfigFlags_NavEnableKeyboard) & ~ImGuiConfigFlags_NavEnableGamepad;
			io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
			Settle(2);
		}
		// g. Up / Down
		{
			FocusOn(kPrecise);
			g_speed = 0.50f;
			io.AddKeyEvent(ImGuiKey_DownArrow, true);  Frame();
			io.AddKeyEvent(ImGuiKey_DownArrow, false); Settle(2);
			const bool down = GImGui->NavId == g_ids[kConsumer];
			io.AddKeyEvent(ImGuiKey_UpArrow, true);  Frame();
			io.AddKeyEvent(ImGuiKey_UpArrow, false); Settle(2);
			Check(down && GImGui->NavId == g_ids[kPrecise] && near(g_speed, 0.50f), "8g: Down / Up on a highlighted slider move the highlight to the next / previous row, value unchanged");
			enter();
			tapSide(true);
			g_speedApplies = 0;
			io.AddKeyEvent(ImGuiKey_DownArrow, true);  Frame();
			io.AddKeyEvent(ImGuiKey_DownArrow, false); Settle(2);
			Check(GImGui->NavId == g_ids[kConsumer] && GImGui->ActiveId == 0 && near(g_speed, 0.51f) && g_speedApplies == 0,
				  "8g: on an ACTIVATED slider, Down lets it go and moves to the next row; the nudged value stays");
		}
		// h. Enter / Space, and a mouse click then Enter
		{
			FocusOn(kPrecise);
			g_speed = 0.50f;
			io.AddMousePosEvent(g_mid[kPrecise].x, g_mid[kPrecise].y); Frame();
			io.AddMouseButtonEvent(0, true);  Frame();
			io.AddMouseButtonEvent(0, false); Frame();
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); Settle(2);
			const bool clicked = GImGui->NavId == g_ids[kPrecise] && GImGui->ActiveId == 0;
			Frame(false, true);
			Frame(false, false);
			const bool activeFirst = GImGui->ActiveId == g_ids[kPrecise] && !ImGui::TempInputIsActive(g_ids[kPrecise]);
			Check(clicked && activeFirst, "8h: after a mouse click and release on a slider, the FIRST Enter activates it (no number box)");
			if (GImGui->ActiveId != 0) { Frame(false, true); Frame(false, false); }
			Settle(2);
			press(ImGuiKey_Space);
			const bool spaceActive = GImGui->ActiveId == g_ids[kPrecise];
			press(ImGuiKey_Space);
			Settle(2);
			Check(spaceActive && GImGui->ActiveId == 0, "8h: Space activates it and Space lets it go");
		}
		// i. Escape lets go of a slider the keys hold, and does not close the menu; a mouse drag and a typed number are not this
		{
			FocusOn(kPrecise);
			g_speed = 0.50f;
			enter();
			Frame();
			io.AddKeyEvent(ImGuiKey_Escape, true); Frame();
			const bool seen = g_escHeld;
			const ImGuiID activeAfter = GImGui->ActiveId;
			io.AddKeyEvent(ImGuiKey_Escape, false); Settle(2);
			Check(seen && activeAfter == 0 && GImGui->NavId == g_ids[kPrecise], "8i: Escape on an activated slider: seen before NewFrame (the menu stays open), let go, highlight kept");
			io.AddKeyEvent(ImGuiKey_Escape, true); Frame();
			const bool notHeld = !g_escHeld;
			io.AddKeyEvent(ImGuiKey_Escape, false); Settle(2);
			Check(notHeld, "8i: ... the next Escape is the menu's again");
			io.AddMousePosEvent(g_mid[kPrecise].x, g_mid[kPrecise].y); Frame();
			io.AddMouseButtonEvent(0, true);  Frame(); Frame();
			const bool mouseActive = GImGui->ActiveId == g_ids[kPrecise];
			io.AddKeyEvent(ImGuiKey_Escape, true); Frame();
			const bool mouseNotHeld = !g_escHeld;
			io.AddKeyEvent(ImGuiKey_Escape, false);
			io.AddMouseButtonEvent(0, false); Frame();
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); Settle(2);
			Check(mouseActive && mouseNotHeld, "8i: a slider held by the mouse is not this");
			FocusOn(kText);
			Frame(false, true); Frame(false, false); Frame();
			const bool typing = GImGui->ActiveId == g_ids[kText];
			io.AddKeyEvent(ImGuiKey_Escape, true); Frame();
			const bool textNotHeld = !g_escHeld;
			io.AddKeyEvent(ImGuiKey_Escape, false); Settle(2);
			Check(typing && textNotHeld, "8i: a text field being typed in is not this");
		}
		// j. the mouse: a drag needs no click first, and moves the value at once
		{
			FocusOn(kRow);
			g_speed = 0.20f;
			const ImVec2 at = g_mid[kPrecise];
			io.AddMousePosEvent(at.x, at.y); Frame();
			io.AddMouseButtonEvent(0, true);  Frame(); Frame();
			const float onPress = g_speed;
			const bool held = GImGui->ActiveId == g_ids[kPrecise] && GImGui->ActiveIdSource == ImGuiInputSource_Mouse;
			io.AddMousePosEvent(at.x + 100.0f, at.y); Frame(); Frame();
			const float dragged = g_speed;
			io.AddMouseButtonEvent(0, false); Frame();
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); Settle(2);
			Check(held && !near(onPress, 0.20f) && dragged > onPress && GImGui->ActiveId == 0, "8j: the mouse drags a slider directly from a plain row, and lets go on release");
			g_speed = 0.50f;
		}
		// k. text fields are not stepped, a plain row still leaves
		{
			FocusOn(kText2);
			const std::string before = g_text2;
			const ImGuiID nav = GImGui->NavId;
			tapSide(false);
			Check(GImGui->ActiveId == 0 && before == g_text2 && !io.WantTextInput && GImGui->NavId != 0 && nav != 0, "8k: Left on a highlighted text field starts no typing and changes nothing");
			FocusOn(kRow);
			Frame(true);
			const Seen s = g_seen;
			Frame(); Settle(1);
			Check(MayLeave(s), "8k: Left on a plain button row still goes back to the list");
		}
	}

	ImGui::DestroyContext();
	std::cout << "nav_left_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
