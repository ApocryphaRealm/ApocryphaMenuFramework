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
//   4. a held Left (key repeat) on the slider keeps the highlight on it frame after frame;
//   5. (retest 26d2) widgets drawn as a mod with its OWN copy of Dear ImGui draws them (AMF Example: plain ImGui calls, no
//      NoteArrowItem) are known from ImGui's own state (navside::LearnNavItemKind) - a plain slider reached by a key or a
//      click, a drag, a text field, a NoInput slider once activated - so Left stays on them; Enter on a slider activates it
//      (no number box), Left then steps it, Enter lets it go; Enter on a text field still starts typing;
//   6. Deep Snow's band slider in a table: not activated, Right moves to the next column (ImGui's nav, as seen in 26d2);
//      activated with Enter, Right / Left step the value and the highlight stays;
//   7. Escape in an open dropdown: ImGui's NewFrame closes the popup before the menu reads Escape (the cause of "Esc closes
//      the whole menu"); navside::EscapeClosesAPopup read before NewFrame sees it.
//   8. ONE WAY for sliders (navside::StepHighlightedArrowItem, 4. plans\amf-null-checks\ONE-WAY-SLIDERS.md), with AMF's step
//      on as Renderer.cpp runs it while the menu is up (sections 1-7 run with it off, so they still show ImGui's own
//      behaviour): a HIGHLIGHTED plain slider in a table steps on Right and keeps its column; AMF's precise slider and an
//      export-shaped slider step one unit of the shown digit; Deep Snow 1.1.0's band slider (its precise wrapper + its own
//      HighlightNudge) and DEM 1.7.5's NudgeableSlider step ONCE a press and never twice in one frame while held (60 and
//      144 fps) - and without AMF's step Deep Snow's helper still steps it; the D-pad does the same from the pad; Up / Down
//      still move rows; Enter and Space still activate (DEM's N4 included); text fields are left alone (one typed in
//      before, and one never typed in, found on the first press); Left on a plain row still goes back to the list.
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

	enum Item { kRow, kPrecise, kConsumer, kDrag, kTriple, kCustom, kTree, kCombo, kPlain, kPlainDrag, kText, kNoInput, kBandA, kBandB,
				kExport, kText2, kDsA, kDsB, kDem, kCount };
	ImGuiID g_ids[kCount] = {};
	ImVec2  g_mid[kCount] = {};     // each item's centre on screen (for a mouse click)
	int     g_focusRequest = -1;   // focus this item (SetFocusID) the next frame it is drawn

	float  g_speed = 0.5f;          // AMF Example's Speed: precise::SliderFloat 0..1 "%.2f"
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
	// 8. ONE WAY for sliders (ONE-WAY-SLIDERS.md)
	float  g_export = 50.0f;        // a mod's slider through igSliderFloat's generated body: 0..100 "%.1f" (ImGui alone: 1.0 a nudge)
	char   g_text2[32] = "Lydia";   // a text field nobody has typed in yet (no edit state of its own)
	float  g_dsA = 10.0f;           // Deep Snow 1.1.0's band table, as it really runs: its precise wrapper around the export ...
	float  g_dsB = 60.0f;           // ... plus precise::HighlightNudge (include\SliderNudge.h), in neighbouring columns
	float  g_dem = 0.50f;           // Dragon's Eye Minimap 1.7.5's NudgeableSlider (UI.cpp) around its precise wrapper, 0..1 "%.2f"
	const char* g_demSelected = nullptr;   // DEM's selectedSlider
	bool   g_amfStep = false;       // Renderer.cpp's a_enabled: the menu is up and taking input (off for sections 1-7)
	navside::HighlightStep g_step = navside::HighlightStep::kNone;
	bool   g_enterTweaked = false;  // EnterActivatesArrowItem turned this frame's Enter into an activation
	bool   g_escBefore = false;     // EscapeClosesAPopup() read just before NewFrame (Renderer.cpp's escClosesPopup)
	int    g_popupsAfterNewFrame = 0;

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
		ImGui::NewFrame();
		g_popupsAfterNewFrame = GImGui->OpenPopupStack.Size;
		g_enterTweaked = navside::EnterActivatesArrowItem();   // Renderer.cpp: right after NewFrame
		g_step = navside::StepHighlightedArrowItem(g_amfStep, false);   // Renderer.cpp: next, every frame (8. ONE WAY)
		ImGui::SetNextWindowPos(ImVec2(20, 20));
		ImGui::SetNextWindowSize(ImVec2(1200, 1040));
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
		// 8. ONE WAY for sliders: a mod's slider through the export, a fresh text field, Deep Snow 1.1.0's band table and DEM's slider
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
		navside::LearnNavItemKind();   // Renderer.cpp: after the page, before the Left decision
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

	// 5. a mod drawing with its OWN copy of Dear ImGui (AMF Example's real Speed slider; retest 26d2 N1-N5 FAIL): no
	// NoteArrowItem is ever called, so only ImGui's own state can tell
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
		Check(s.onArrow && !MayLeave(s), "FIX 26d2 N1: a single Left on a plain slider from a mod's own ImGui stays on the page");
		Check(GImGui->NavId == g_ids[kPlain] && g_plain == before, "... the highlight is still on it and its value is unchanged (not activated)");
		bool allOn = true;
		for (int i = 0; i < 90; ++i) { Frame(true); allOn = allOn && g_seen.onArrow && !MayLeave(g_seen) && GImGui->NavId == g_ids[kPlain]; }
		Frame(false); Settle(2);
		Check(allOn, "FIX 26d2 N2: a held Left (1.5 s) on the plain slider stays on it every frame");
	}
	{
		// 26d2 N1 try 2: the highlight put there by a click on the knob (pressed and let go), not by a key
		FocusOn(kRow);
		const float before = g_plain;
		io.AddMousePosEvent(g_mid[kPlain].x, g_mid[kPlain].y); Frame();
		io.AddMouseButtonEvent(0, true);  Frame();
		io.AddMouseButtonEvent(0, false); Frame();
		g_plain = before;   // the click itself may move the value; only the highlight matters here
		io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); Settle(2);
		const bool onIt = GImGui->NavId == g_ids[kPlain];
		const Seen s = TapLeft();
		Check(onIt && s.onArrow && !MayLeave(s), "FIX 26d2 N1 (click focus): after a click on a plain slider, Left stays on the page");
	}
	{
		// a NoInput slider carries no Inputable flag: unknown until it is activated, when it claims Left / Right
		const bool there = reach(kNoInput);
		const Seen s0 = TapLeft();
		Check(there && !s0.onArrow, "a NoInput slider reached by keyboard is not known yet (no Inputable flag) - Left may leave, as before");
		press(ImGuiKey_Space);   // Space activates it
		const bool active = GImGui->ActiveId == g_ids[kNoInput];
		press(ImGuiKey_Space);   // Space again lets it go
		Settle(2);
		const Seen s1 = TapLeft();
		Check(active && s1.onArrow && !MayLeave(s1), "once activated (it claimed Left/Right), the NoInput slider is known: Left stays on the page");
	}
	{
		const bool there = reach(kPlainDrag);
		const Seen s = TapLeft();
		Check(there && s.onArrow && !MayLeave(s), "a plain drag from a mod's own ImGui stays on Left");
	}
	{
		const bool there = reach(kText);
		const Seen s = TapLeft();
		Check(there && s.onArrow && !MayLeave(s), "a plain text field from a mod's own ImGui (not being typed in) stays on Left");
	}
	{
		// 26d2 N3: Enter on the plain slider ACTIVATES it (no number box); Left steps it; Enter again lets it go
		const bool there = reach(kPlain);
		g_plain = 1.0f;
		Frame(false, true);
		const bool tweaked = g_enterTweaked;
		Frame(false, false);
		const bool active = GImGui->ActiveId == g_ids[kPlain];
		const bool box = ImGui::TempInputIsActive(g_ids[kPlain]);
		Check(there && tweaked && active && !box, "FIX 26d2 N3: Enter on a plain slider activates it - no number box opens");
		const float before = g_plain;
		Frame(true); Frame(false); Settle(1);
		Check(g_plain < before && GImGui->NavId == g_ids[kPlain], "... Left then steps its value down and the highlight stays on it");
		Frame(false, true); Frame(false, false); Settle(2);
		Check(GImGui->ActiveId == 0 && !ImGui::TempInputIsActive(g_ids[kPlain]), "... Enter again lets it go (no number box)");
	}
	{
		// 4. (c0a3 / 26d2 N7) Escape in an open dropdown: ImGui's NewFrame closes the popup, so after NewFrame - where the
		// menu read Escape as "close the menu" - nothing shows it was open; read before NewFrame, it is seen
		FocusOn(kCombo);
		Frame(false, true); Frame(false, false); Settle(2);   // Enter opens the combo
		const bool open = GImGui->OpenPopupStack.Size > 0;
		io.AddKeyEvent(ImGuiKey_Escape, true); Frame();
		const bool before = g_escBefore;
		const int after = g_popupsAfterNewFrame;
		const bool escPressed = true;
		io.AddKeyEvent(ImGuiKey_Escape, false); Settle(2);
		Check(open && after == 0, "CAUSE (Esc in a dropdown): ImGui's NewFrame already closed the dropdown before the menu reads Escape");
		Check(escPressed && before, "FIX: read before NewFrame, the open dropdown is seen - Escape closes it and the menu stays open");
		Frame(); const bool none = !g_escBefore;
		Check(none, "with no dropdown open, Escape still closes the menu (nothing seen before NewFrame)");
	}
	{
		// a text field still starts editing on Enter
		const bool there = reach(kText);
		Frame(false, true); Frame(false, false);
		Check(there && GImGui->ActiveId == g_ids[kText], "Enter on a text field still starts typing in it");
		press(ImGuiKey_Escape);
		Settle(2);
	}
	{
		// Deep Snow's band slider in a table (04f6 step 8 / 26d2 notes): NOT activated, Right moves to the next column -
		// ImGui's own navigation (there is a widget there); once ACTIVATED, Right / Left step the value and the highlight stays
		const bool there = reach(kBandA);
		Frame(false, false, 1.0f / 60.0f, true); Frame(); Settle(2);
		const bool movedOver = GImGui->NavId == g_ids[kBandB];
		Check(there && movedOver && g_bandA == 10.0f, "Deep Snow band slider, not activated: Right moves to the next column (ImGui nav), value unchanged - as seen in 26d2");
		FocusOn(kBandA);
		Frame(false, true); Frame(false, false);   // Enter activates it
		const bool active = GImGui->ActiveId == g_ids[kBandA] && !ImGui::TempInputIsActive(g_ids[kBandA]);
		Frame(false, false, 1.0f / 60.0f, true); Frame(); Settle(1);
		const float afterRight = g_bandA;
		const bool stayed = GImGui->NavId == g_ids[kBandA];
		Frame(true); Frame(); Settle(1);
		const float afterLeft = g_bandA;
		Check(active && afterRight > 10.0f && stayed, "Deep Snow band slider, activated with Enter: Right steps the value up and the highlight stays in the column");
		Check(afterLeft < afterRight && GImGui->NavId == g_ids[kBandA], "... Left steps it back down, still on the slider");
		Frame(false, true); Frame(false, false); Settle(2);
		Check(GImGui->ActiveId == 0, "... Enter lets it go");
	}

	// 8. ONE WAY for sliders (4. plans\amf-null-checks\ONE-WAY-SLIDERS.md): with AMF's step on (Renderer.cpp: the menu is up), a
	// HIGHLIGHTED slider or drag steps on Left / Right / D-pad without activation, keeps the highlight, and steps once a press
	// even when the mod runs its own helper (Deep Snow's HighlightNudge, DEM's NudgeableSlider)
	{
		using navside::HighlightStep;
		const float dt60 = 1.0f / 60.0f, dt144 = 1.0f / 144.0f;
		auto near = [](float a_a, float a_b) { return std::fabs(a_a - a_b) < 1e-4f; };
		struct Tap { HighlightStep press, release; bool activeOnPress; ImGuiID navAfter; Seen seen; };
		auto tapSide = [&](bool a_right, float a_dt = 1.0f / 60.0f) {
			Tap t{};
			Frame(!a_right, false, a_dt, a_right);
			t.press = g_step;
			t.activeOnPress = GImGui->ActiveId != 0;
			t.seen = g_seen;
			Frame(false, false, a_dt, false);
			t.release = g_step;
			Settle(1);
			t.navAfter = GImGui->NavId;
			return t;
		};
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
		g_amfStep = true;

		// a. a plain ImGui slider in a table (a mod's own copy of ImGui): Right steps it and keeps the column
		{
			const bool there = reach(kBandA);
			g_bandA = 10.0f;
			const float other = g_bandB;
			const Tap t = tapSide(true);
			Check(there && t.press == HighlightStep::kStarted && t.activeOnPress,
				  "8a: Right on a HIGHLIGHTED plain slider in a table makes it the active item while the key is down");
			Check(g_bandA > 10.0f && t.navAfter == g_ids[kBandA] && g_bandB == other,
				  "8a: ... it steps (ImGui's own step for a slider whose format AMF cannot see), the highlight keeps its column, the next column is untouched");
			Check(t.release == HighlightStep::kReleased && GImGui->ActiveId == 0, "8a: ... and it is let go on the frame Right is released");
			const float v = g_bandA;
			const Tap l = tapSide(false);
			Check(g_bandA < v && l.navAfter == g_ids[kBandA] && !MayLeave(l.seen), "8a: Left steps it back and never sends the highlight to the mod list");
		}
		// b. AMF's own precise slider (AMF Example's Speed shape, 0..1 "%.2f"): one unit of the shown digit
		{
			FocusOn(kPrecise);
			g_speed = 0.50f;
			const Tap r = tapSide(true);
			const float afterRight = g_speed;
			const Tap l = tapSide(false);
			Check(r.press == HighlightStep::kStarted && near(afterRight, 0.51f) && r.navAfter == g_ids[kPrecise],
				  "8b: Right on a highlighted AMF precise slider: 0.50 -> 0.51, highlight kept");
			Check(near(g_speed, 0.50f) && l.navAfter == g_ids[kPrecise], "8b: ... Left: back to 0.50");
		}
		// c. a mod's slider through the export (igSliderFloat's generated body), 0..100 "%.1f": ImGui alone moves 1.0 - the export holds it to 0.1
		{
			FocusOn(kExport);
			g_export = 50.0f;
			const Tap r = tapSide(true);
			Check(r.press == HighlightStep::kStarted && near(g_export, 50.1f) && r.navAfter == g_ids[kExport],
				  "8c: Right on a highlighted export slider: 50.0 -> 50.1 (one unit of the shown digit, not ImGui's 1% = 1.0)");
			const Hold h = holdRight(kExport, &g_export, 60, dt60);
			Check(h.stayed && h.total > 0.0f && h.maxStep < 0.1f + 1e-4f, "8c: ... held 1 s: it repeats, never more than 0.1 in a frame, highlight kept");
		}
		// d. Deep Snow 1.1.0's band slider (precise wrapper around the export + HighlightNudge) in its table: ONE step a press
		{
			FocusOn(kDsA);
			g_dsA = 10.0f;
			const float other = g_dsB;
			const Tap r = tapSide(true);
			Check(r.press == HighlightStep::kStarted && g_dsA == 11.0f && r.navAfter == g_ids[kDsA] && g_dsB == other,
				  "8d: Deep Snow band slider with its own HighlightNudge, under AMF: Right 10 -> 11 - one step, not two (HighlightNudge saw IsItemActive and stood down), column kept");
			const Tap r2 = tapSide(true);
			const Tap l = tapSide(false);
			Check(g_dsA == 11.0f && r2.navAfter == g_ids[kDsA] && l.navAfter == g_ids[kDsA], "8d: ... Right, Left: 11 -> 12 -> 11");
			const Hold h60 = holdRight(kDsA, &g_dsA, 60, dt60);
			const Hold h144 = holdRight(kDsA, &g_dsA, 144, dt144);
			Check(h60.stayed && h60.total > 0.0f && h60.maxStep <= 1.0f && g_dsB == other,
				  "8d: ... held 1 s at 60 fps: never two steps in one frame, never leaves the column");
			Check(h144.stayed && h144.total > 0.0f && h144.maxStep <= 1.0f, "8d: ... held 1 s at 144 fps: the same");
			g_amfStep = false;   // plain SKSE Menu Framework: AMF does nothing, the mod's own helper steps it
			g_dsA = 10.0f;
			const Tap smf = tapSide(true);
			g_amfStep = true;
			Check(smf.press == HighlightStep::kNone && g_dsA == 11.0f && smf.navAfter == g_ids[kDsA],
				  "8d: without AMF's step (plain SMF) Deep Snow's own HighlightNudge still steps it once and keeps the column");
		}
		// e. Dragon's Eye Minimap 1.7.5's NudgeableSlider (steps the slider last clicked or activated, unless the slider moved itself)
		{
			FocusOn(kRow);
			io.AddMousePosEvent(g_mid[kDem].x, g_mid[kDem].y); Frame();
			io.AddMouseButtonEvent(0, true);  Frame();
			io.AddMouseButtonEvent(0, false); Frame();
			io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); Settle(2);
			g_dem = 0.50f;   // the click itself moves the value; only the selection matters here
			Settle(1);
			const bool selected = g_demSelected != nullptr && GImGui->NavId == g_ids[kDem];
			const Tap r = tapSide(true);
			Check(selected && r.press == HighlightStep::kStarted && near(g_dem, 0.51f) && r.navAfter == g_ids[kDem],
				  "8e (DEM N4): clicked DEM slider, Right: 0.50 -> 0.51 - one step (NudgeableSlider adds none when the slider moved), highlight kept");
			const Hold h = holdRight(kDem, &g_dem, 144, dt144);
			Check(h.stayed && h.total > 0.0f && h.maxStep < 0.01f + 1e-4f, "8e: ... held 1 s at 144 fps: never two steps in one frame, highlight kept");
			g_dem = 0.50f;
			Frame(false, true); Frame(false, false);   // Enter activates it (EnterActivatesArrowItem), as before
			const bool active = GImGui->ActiveId == g_ids[kDem];
			const Tap ra = tapSide(true);
			Check(active && ra.press == HighlightStep::kNone && near(g_dem, 0.51f) && GImGui->ActiveId == g_ids[kDem],
				  "8e (DEM N4): Enter activates the DEM slider; Right then steps it once (AMF leaves an active slider alone)");
			Frame(false, true); Frame(false, false); Settle(2);
			Check(GImGui->ActiveId == 0, "8e: ... Enter lets it go");
			// DEM's selected slider cancels EVERY arrow move while it is drawn (its page is the only one on screen in game);
			// here every item shares one page, so the rest runs as if DEM's page were closed
			g_demSelected = nullptr;
		}
		// f. the D-pad (controller mode: gamepad navigation on, keyboard navigation off)
		{
			io.ConfigFlags = (io.ConfigFlags | ImGuiConfigFlags_NavEnableGamepad) & ~ImGuiConfigFlags_NavEnableKeyboard;
			io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
			auto dpad = [&](ImGuiKey a_key) {
				Tap t{};
				io.AddKeyEvent(a_key, true); Frame();
				t.press = g_step;
				t.activeOnPress = GImGui->ActiveId != 0 && GImGui->ActiveIdSource == ImGuiInputSource_Gamepad;
				io.AddKeyEvent(a_key, false); Frame();
				t.release = g_step;
				Settle(1);
				t.navAfter = GImGui->NavId;
				return t;
			};
			FocusOn(kPrecise);
			g_speed = 0.50f;
			const Tap r = dpad(ImGuiKey_GamepadDpadRight);
			Check(r.press == HighlightStep::kStarted && r.activeOnPress && near(g_speed, 0.51f) && r.navAfter == g_ids[kPrecise] &&
					  r.release == HighlightStep::kReleased,
				  "8f: D-pad right on a highlighted precise slider: active from the PAD while held, 0.50 -> 0.51, highlight kept, let go on release");
			FocusOn(kDsA);
			g_dsA = 10.0f;
			const Tap d = dpad(ImGuiKey_GamepadDpadRight);
			const Tap dl = dpad(ImGuiKey_GamepadDpadLeft);
			Check(d.press == HighlightStep::kStarted && dl.navAfter == g_ids[kDsA] && g_dsA == 10.0f,
				  "8f: D-pad right then left on Deep Snow's band slider: 10 -> 11 -> 10, one step each, column kept");
			io.ConfigFlags = (io.ConfigFlags | ImGuiConfigFlags_NavEnableKeyboard) & ~ImGuiConfigFlags_NavEnableGamepad;
			io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
			Settle(2);
		}
		// g. Up / Down still move between rows
		{
			FocusOn(kPrecise);
			g_speed = 0.50f;
			io.AddKeyEvent(ImGuiKey_DownArrow, true);  Frame();
			const HighlightStep st = g_step;
			io.AddKeyEvent(ImGuiKey_DownArrow, false); Settle(2);
			const bool down = GImGui->NavId == g_ids[kConsumer];
			io.AddKeyEvent(ImGuiKey_UpArrow, true);  Frame();
			io.AddKeyEvent(ImGuiKey_UpArrow, false); Settle(2);
			Check(st == HighlightStep::kNone && down && GImGui->NavId == g_ids[kPrecise] && near(g_speed, 0.50f),
				  "8g: Down / Up on a highlighted slider still move the highlight to the next / previous row, value unchanged");
		}
		// h. activation still works (Enter, Space), and AMF leaves an activated slider alone
		{
			FocusOn(kPrecise);
			g_speed = 0.50f;
			Frame(false, true); Frame(false, false);
			const bool enterActive = GImGui->ActiveId == g_ids[kPrecise] && !ImGui::TempInputIsActive(g_ids[kPrecise]);
			const Tap r = tapSide(true);
			const bool stillActive = GImGui->ActiveId == g_ids[kPrecise];
			Frame(false, true); Frame(false, false); Settle(2);
			Check(enterActive && r.press == HighlightStep::kNone && near(g_speed, 0.51f) && stillActive && GImGui->ActiveId == 0,
				  "8h: Enter activates the slider, Right steps it once, it stays active after Right is let go, Enter releases it");
			press(ImGuiKey_Space);
			const bool spaceActive = GImGui->ActiveId == g_ids[kPrecise];
			press(ImGuiKey_Space);
			Settle(2);
			Check(spaceActive && GImGui->ActiveId == 0, "8h: Space activates it and Space lets it go, as before");
		}
		// i. text fields are not stepped: one typed in before (learnt), and one nobody has typed in yet (found on the first press)
		{
			const bool there = reach(kText);
			const std::string before = g_text;
			const Tap t = tapSide(true);
			Check(there && t.press == HighlightStep::kNone && GImGui->ActiveId == 0 && before == g_text && t.navAfter == g_ids[kText],
				  "8i: Right on a highlighted text field that was typed in before: AMF leaves it alone (learnt while it was being typed in)");
			const bool there2 = reach(kText2);
			const Tap t1 = tapSide(true);
			const bool typing = io.WantTextInput;
			const Tap t2 = tapSide(true);
			Check(there2 && t1.press == HighlightStep::kStarted && t1.release == HighlightStep::kNotAStepper && !typing &&
					  std::string(g_text2) == "Lydia" && t1.navAfter == g_ids[kText2],
				  "8i: a text field nobody typed in yet: the first Right finds it is no slider (it drops the activation at once) - no typing, text unchanged, highlight kept");
			Check(t2.press == HighlightStep::kNone, "8i: ... and from then on AMF leaves it alone");
		}
		// j. a plain row: Left still goes back to the mod list
		{
			FocusOn(kRow);
			const Tap t = tapSide(false);
			Check(t.press == HighlightStep::kNone && MayLeave(t.seen), "8j: Left on a plain button row: AMF's step does nothing, Left still goes back to the list");
		}
		g_amfStep = false;
	}

	ImGui::DestroyContext();
	std::cout << "nav_left_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
