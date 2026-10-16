// Off-game check of the mouse wheel on a slider (AMF 2.2.0; the owner, testing RaceMenu Atelier: the wheel did not move a
// slider after clicking it). Runs AMF's own ImGui build (1.90.8 with the overlay patches, the SE line's vcpkg tree) headless,
// with the framework's input rules (trickling off, the pointer published once per frame).
//   tools\slider_wheel_check.bat   -> one line per check and "slider_wheel_check: N of N passed"; exits 1 on any failure.
// What it proves: the step a consumer's format gives (precise::FormatStep - what amf_ConsumerSliderWheel uses for every
// igSlider* export), the step-and-clamp arithmetic (precise::WheelStepped), and on live ImGui frames that one notch over
// an AMF slider (precise::SliderFloat / SliderInt) and over a consumer-shaped slider (ImGui::SliderScalar, which SliderInt / SliderFloat reach,
// followed by precise::WheelNudge, exactly what the generated exports do) moves it one step, clamps at the ends, returns
// true and marks the item edited, and that the page does not scroll while the pointer is on a slider but still scrolls
// when it is not. Section 6 (the 2.2.0 fix): a slider that applies on IsItemDeactivatedAfterEdit - AMF's Text size, a
// consumer through igIsItemDeactivatedAfterEdit - applies a wheel change exactly once per settled run of notches.
// What it cannot: the game's own wheel records (Input.cpp turns them into io.AddMouseWheelEvent).
#include "../include/PreciseSlider.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
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
	bool Near(double a, double b) { return std::fabs(a - b) < 1e-6; }

	float  g_text = 1.5f;      // AMF's Text size: precise::SliderFloat 1..2 "%.2f"
	int    g_opacity = 50;     // AMF's Window opacity: precise::SliderInt 5..100 "%d%%"
	int    g_count = 3;        // a consumer's igSliderInt 0..10 "%d"
	double g_scale = 0.5;      // a consumer's igSliderScalar (double) 0..1 "%.1f"
	bool   g_retText = false, g_retCount = false, g_retScale = false, g_editedText = false;
	ImRect g_rectText, g_rectCount, g_rectScale;
	float  g_scrollY = 0.0f;
	ImVec2 g_pointer{ 5, 5 };
	bool   g_mouseDown = false;
	int    g_appliedText = 0, g_appliedCount = 0;   // release-applies: IsItemDeactivatedAfterEdit after the slider, as Renderer.cpp's Text size
	int    g_outlineVerts = 0, g_outlineEdgeVerts = 0, g_outlineTextVerts = 0;   // precise::OutlineValue on the Text size slider

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

	void Frame(float a_wheel = 0.0f)
	{
		ImGuiIO& io = ImGui::GetIO();
		if (a_wheel != 0.0f) { io.AddMouseWheelEvent(0.0f, a_wheel); }
		io.AddMousePosEvent(g_pointer.x, g_pointer.y);
		io.AddMouseButtonEvent(0, g_mouseDown);
		io.DeltaTime = 1.0f / 60.0f;
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(100, 100));
		ImGui::SetNextWindowSize(ImVec2(1600, 900));
		ImGui::Begin("###amf-main", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		ImGui::TextUnformatted("Appearance");
		ImGui::SetNextItemWidth(600);
		g_retText = precise::SliderFloat("Text size", &g_text, 1.0f, 2.0f, "%.2f");
		g_editedText = ImGui::IsItemEdited();
		if (ImGui::IsItemDeactivatedAfterEdit()) { ++g_appliedText; }   // Renderer.cpp: save + rebuild the atlas
		g_rectText = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		{
			// the value outline (2.2.0): drawn again here to count what it adds - glyphs in the window colour, then in the text colour
			ImDrawList* dl = ImGui::GetWindowDrawList();
			const int before = dl->VtxBuffer.Size;
			precise::OutlineValue(ImGuiDataType_Float, &g_text, "%.2f");
			ImVec4 edgeC = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
			edgeC.w = ImGui::GetStyle().Colors[ImGuiCol_Text].w;
			const ImU32 edge = ImGui::GetColorU32(edgeC), text = ImGui::GetColorU32(ImGuiCol_Text);
			g_outlineVerts = dl->VtxBuffer.Size - before;
			g_outlineEdgeVerts = g_outlineTextVerts = 0;
			for (int i = before; i < dl->VtxBuffer.Size; ++i)
			{
				if (dl->VtxBuffer[i].col == edge) { ++g_outlineEdgeVerts; }
				else if (dl->VtxBuffer[i].col == text) { ++g_outlineTextVerts; }
			}
		}
		ImGui::SetNextItemWidth(600);
		precise::SliderInt("Window opacity", &g_opacity, 5, 100, "%d%%");
		// the generated igSliderInt / igSliderScalar bodies: ImGui's slider, then the wheel with the format's step
		{
			int lo = 0, hi = 10;
			ImGui::SetNextItemWidth(600);
			const bool r = ImGui::SliderScalar("Count", ImGuiDataType_S32, &g_count, &lo, &hi, "%d");   // what igSliderInt reaches
			const bool w = precise::WheelNudge(ImGuiDataType_S32, &g_count, &lo, &hi, precise::FormatStep("%d", true));
			g_retCount = r || w;
			if (ImGui::IsItemDeactivatedAfterEdit()) { ++g_appliedCount; }   // a consumer's igIsItemDeactivatedAfterEdit
			g_rectCount = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		}
		{
			const double lo = 0.0, hi = 1.0;
			ImGui::SetNextItemWidth(600);
			const bool r = ImGui::SliderScalar("Scale", ImGuiDataType_Double, &g_scale, &lo, &hi, "%.1f");
			const bool w = precise::WheelNudge(ImGuiDataType_Double, &g_scale, &lo, &hi, precise::FormatStep("%.1f", false));
			g_retScale = r || w;
			g_rectScale = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		}
		ImGui::Dummy(ImVec2(10, 4000));   // a page long enough to scroll
		g_scrollY = ImGui::GetScrollY();
		ImGui::End();
		ImGui::Render();
	}

	ImVec2 Mid(const ImRect& r) { return ImVec2(r.Min.x + 100.0f, (r.Min.y + r.Max.y) * 0.5f); }
}

int main()
{
	// 1. the step from a consumer's format
	Check(Near(precise::FormatStep("%.2f", false), 0.01), "\"%.2f\" steps 0.01");
	Check(Near(precise::FormatStep("%d", false), 1.0), "\"%d\" steps 1");
	Check(Near(precise::FormatStep("%.0f", false), 1.0), "\"%.0f\" steps 1");
	Check(Near(precise::FormatStep("%.3f deg", false), 0.001), "\"%.3f deg\" steps 0.001");
	Check(Near(precise::FormatStep("%5.1f cm", false), 0.1), "\"%5.1f cm\" (a width) steps 0.1");
	Check(Near(precise::FormatStep("100%% at %.2f", false), 0.01), "a literal %% before the number is skipped");
	Check(Near(precise::FormatStep("%d%%", true), 1.0), "an integer slider steps 1 whatever its format");
	Check(Near(precise::FormatStep("%f", false), 0.001) && Near(precise::FormatStep(nullptr, false), 0.001), "no precision or no format: 0.001, ImGui's own %.3f");
	Check(Near(precise::Step("%.2f"), 0.01) && Near(precise::Step("%d%%"), 1.0), "AMF's own sliders keep the key-nudge step (precise::Step)");

	// 2. one step, on the shown grid, clamped
	Check(Near(precise::WheelStepped(1.50, 0.01, +1, 1.0, 2.0), 1.51), "1.50 up one step is 1.51");
	Check(Near(precise::WheelStepped(1.50, 0.01, -1, 1.0, 2.0), 1.49), "1.50 down one step is 1.49");
	Check(Near(precise::WheelStepped(0.126, 0.01, +1, 0.0, 1.0), 0.14), "0.126 (shown 0.13) up is 0.14 - onto the shown grid");
	Check(Near(precise::WheelStepped(2.0, 0.01, +1, 1.0, 2.0), 2.0), "up at the maximum stays at the maximum");
	Check(Near(precise::WheelStepped(1.0, 0.01, -1, 1.0, 2.0), 1.0), "down at the minimum stays at the minimum");
	Check(Near(precise::WheelStepped(5.0, 1.0, +1, 10.0, 0.0), 6.0) && Near(precise::WheelStepped(10.0, 1.0, +1, 10.0, 0.0), 10.0), "a range given max-first clamps the same");

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(3200, 1800);
	io.IniFilename = nullptr;
	io.ConfigInputTrickleEventQueue = false;   // Renderer.cpp
	BuildFonts();
	Frame();
	Frame();

	// 2b. the value outline over the grab (HadToRegister): eight copies in the window colour, then the value in the text colour
	Check(g_outlineVerts > 0 && g_outlineEdgeVerts == 8 * g_outlineTextVerts && g_outlineTextVerts > 0,
		  "the Text size value is drawn outlined: " + std::to_string(g_outlineEdgeVerts) + " outline vertices, " +
			  std::to_string(g_outlineTextVerts) + " text vertices");

	// 3. AMF's own slider: hover, then a notch each way
	g_pointer = Mid(g_rectText);
	Frame();
	Frame(+1.0f);
	Check(Near(g_text, 1.51f) || std::fabs(g_text - 1.51f) < 1e-5f, "wheel up over Text size: 1.50 -> " + std::to_string(g_text));
	Check(g_retText && g_editedText, "the slider call returns true and the item is edited, as a drag");
	Check(g_scrollY == 0.0f, "the page did not scroll under the slider");
	Frame(-1.0f);
	Frame(-1.0f);
	Check(std::fabs(g_text - 1.49f) < 1e-5f, "two notches down: 1.51 -> " + std::to_string(g_text));
	g_text = 2.0f;
	Frame(+1.0f);
	Check(g_text == 2.0f && !g_retText, "up at 2.00 stays 2.00 and reports no change");
	Frame();
	Check(!g_retText, "no notch, no change");

	// 4. the consumer shapes: igSliderInt "%d" and igSliderScalar double "%.1f"
	g_pointer = Mid(g_rectCount);
	Frame();
	Frame(+1.0f);
	Check(g_count == 4 && g_retCount, "consumer igSliderInt: 3 -> " + std::to_string(g_count) + ", returns true");
	g_count = 10;
	Frame(+1.0f);
	Check(g_count == 10 && !g_retCount, "consumer igSliderInt clamps at 10");
	g_pointer = Mid(g_rectScale);
	Frame();
	Frame(-1.0f);
	Check(std::fabs(g_scale - 0.4) < 1e-9 && g_retScale, "consumer igSliderScalar (double, \"%.1f\"): 0.5 -> " + std::to_string(g_scale));
	Check(g_scrollY == 0.0f, "still no page scroll over the sliders");

	// 5. off the sliders the wheel scrolls the page as before, and moves no slider
	const float textBefore = g_text;
	const int countBefore = g_count;
	g_pointer = ImVec2(1500, 900);   // inside the window, right of every slider
	Frame();
	Frame();
	Frame(-1.0f);
	Frame();
	Check(g_scrollY > 0.0f, "wheel over the page (not a slider) scrolls it: scrollY " + std::to_string(g_scrollY));
	Check(g_text == textBefore && g_count == countBefore, "and no slider moved");

	// 6. the 2.2.0 fix: a release-applying slider applies a wheel change once the wheel rests (precise::WheelSettle, 0.25 s)
	auto idle = [](int n) { for (int i = 0; i < n; ++i) Frame(); };
	idle(30);   // whatever the sections above left settling
	for (int i = 0; i < 3 && g_scrollY > 0.0f; ++i) { Frame(+1.0f); Frame(); }   // scroll section 5's page back to the top
	Check(g_scrollY == 0.0f, "page back at the top for section 6");
	idle(2);
	g_appliedText = g_appliedCount = 0;
	g_text = 1.5f;
	g_pointer = Mid(g_rectText);
	idle(2);
	Frame(+1.0f);
	Check(g_retText && g_appliedText == 0, "one notch on Text size: the call returns true; no release yet");
	idle(10);   // 0.17 s
	Check(g_appliedText == 0, "still settling at 0.17 s: not applied yet");
	idle(10);   // 0.33 s
	Check(g_appliedText == 1, "rested 0.25 s: IsItemDeactivatedAfterEdit fired, applied " + std::to_string(g_appliedText) + " time(s)");
	idle(60);
	Check(g_appliedText == 1, "and only once (" + std::to_string(g_appliedText) + " after another second)");

	g_appliedText = 0;
	for (int i = 0; i < 5; ++i) Frame(+1.0f);   // a fast spin: five notches on five frames
	Check(std::fabs(g_text - 1.56f) < 1e-5f && g_appliedText == 0, "five fast notches: 1.51 -> " + std::to_string(g_text) + ", nothing applied mid-spin");
	idle(20);
	Check(g_appliedText == 1, "the spin settles into ONE apply, not five: " + std::to_string(g_appliedText));

	g_appliedText = 0;
	for (int i = 0; i < 3; ++i) { Frame(-1.0f); idle(10); }   // three notches 0.18 s apart
	idle(20);
	Check(g_appliedText == 1 && std::fabs(g_text - 1.53f) < 1e-5f, "three slow notches (each under 0.25 s apart): one apply at " + std::to_string(g_text) + ", applied " + std::to_string(g_appliedText));

	g_appliedText = 0;
	g_text = 2.0f;
	Frame(+1.0f);
	idle(30);
	Check(g_appliedText == 0, "a notch that changes nothing (at the maximum) applies nothing");

	g_appliedText = 0;
	g_text = 1.5f;
	Frame(+1.0f);
	idle(3);
	g_pointer.x = g_rectText.Min.x + 596.0f;   // grabbed before it settles: press at the far end of the 600 px frame, then let go
	g_mouseDown = true;
	Frame();
	Frame();
	g_mouseDown = false;
	Frame();
	idle(30);
	Check(g_appliedText == 1 && g_text > 1.9f, "a notch then a grab before it settles: one apply, by the release (" + std::to_string(g_appliedText) + ", " + std::to_string(g_text) + ")");

	// the consumer shape: igSliderInt then igIsItemDeactivatedAfterEdit
	g_count = 3;
	g_pointer = Mid(g_rectCount);
	idle(2);
	Frame(+1.0f);
	Frame(+1.0f);
	Check(g_count == 5 && g_appliedCount == 0, "consumer igSliderInt: two notches, 3 -> " + std::to_string(g_count) + ", not applied mid-spin");
	idle(20);
	Check(g_appliedCount == 1, "consumer igIsItemDeactivatedAfterEdit fires once when the wheel rests: " + std::to_string(g_appliedCount));
	idle(30);
	Check(g_appliedCount == 1, "and not again");

	ImGui::DestroyContext();
	std::printf("slider_wheel_check: %d of %d passed\n", g_pass, g_pass + g_fail);
	return g_fail == 0 ? 0 : 1;
}
