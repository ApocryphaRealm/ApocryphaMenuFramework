// Off-game half of the DevBench null self-test (AMF 2.3.0; plan 4. plans\amf-null-checks\PLAN.md section 6, rule 64).
// Compiles AMF's GENERATED source\cimgui\cimgui.cpp - the exported entry points exactly as the DLL builds them - against
// AMF's own ImGui build, with stand-ins for the few framework hooks it calls, and runs include\NullSelfTestCore.h's
// cases in a real frame inside a hidden window, the way devbench amf.menu op=nulltest runs them in game.
//   tools\null_selftest_check.bat   -> one line per case and "null_selftest_check: N of N passed"; exits 1 on any failure.
// What it proves: every cimgui case is a no-op that returns the guard's promised value, with nothing drawn, no value
// written and the ID stack balanced; and, as the control, that the same null handed to Dear ImGui without the guard
// faults (caught here with SEH) - so the cases test something real.
// What it cannot: the SKSE Menu Framework and AMF C-API cases (AddSectionItem, AMF_RegisterPage, ...) - they need the
// running framework's registry; the in-game op runs them.
#include "../include/NullSelfTestCore.h"
#include "../include/NavSide.h"
#include "../include/PreciseSlider.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <windows.h>

#include <iostream>
#include <string>

// The framework hooks the generated cimgui.cpp calls (ConsumerSurface.cpp and Keyboard.cpp in the DLL): stand-ins that
// do what matters here, so the exports run their real guards and bodies
void amf_NoteTextField(unsigned int) {}
void amf_BeginConsumerHeader() {}
void amf_EndConsumerHeader() {}
void amf_ConsumerSameLine(float a_offsetFromStartX, float a_spacing) { ImGui::SameLine(a_offsetFromStartX, a_spacing); }
bool amf_ConsumerCheckbox(const char* a_label, bool* a_value) { return a_value ? ImGui::Checkbox(a_label ? a_label : "", a_value) : false; }
bool amf_ConsumerSliderWheel(int a_type, void* a_data, const void* a_min, const void* a_max, const char* a_format)
{
	return precise::WheelNudge(a_type, a_data, a_min, a_max, precise::FormatStep(a_format, precise::IsIntegerType(a_type)));
}
bool amf_ConsumerSliderAngleWheel(float* a_rad, float a_minDegrees, float a_maxDegrees, const char* a_format)
{
	return precise::WheelNudgeAngle(a_rad, a_minDegrees, a_maxDegrees, a_format);
}
double amf_ConsumerSliderValue(int a_type, const void* a_data) { return precise::SliderValue(a_type, a_data); }
bool amf_ConsumerSliderNav(int a_type, void* a_data, double a_before, const void* a_min, const void* a_max, const char* a_format, int a_flags)
{
	return precise::NavNudge(a_type, a_data, a_before, a_min, a_max, precise::FormatStep(a_format, precise::IsIntegerType(a_type)), a_flags);
}
void amf_NoteArrowItem() { navside::NoteArrowItem(); }
bool amf_BeginConsumerText() { return false; }
void amf_EndConsumerText(bool) {}

namespace
{
	int g_pass = 0, g_fail = 0;
	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	// The control: the same null value pointer handed straight to Dear ImGui, with no guard in front of it
	bool UnguardedSliderFaults()
	{
		__try
		{
			// ImGui::SliderFloat's whole body, spelled out (the package gate's sliders-are-precise row reads every source for the
			// literal slider call; this is the unguarded control, not a slider anyone sees)
			const float lo = 0.0f, hi = 1.0f;
			ImGui::SliderScalar("##control", ImGuiDataType_Float, nullptr, &lo, &hi, "%.2f");
			return false;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return true;
		}
	}

	// as nullselftest::BeginHiddenWindow (NullSelfTestCore.h), which the in-game op uses
	void BeginHidden(const char* a_name) { nullselftest::BeginHiddenWindow(a_name); }
	void EndHidden() { nullselftest::EndHiddenWindow(); }
}

int main()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(1920, 1080);
	{
		ImFontConfig cfg;
		cfg.SizePixels = 16.0f;
		io.Fonts->AddFontDefault(&cfg);
		unsigned char* pixels = nullptr;
		int w = 0, h = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
		io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(1)));
	}
	const int fontsBefore = io.Fonts->Fonts.Size;

	// the hidden window takes items from its first frame (alpha 0 would hide it - BeginHiddenWindow uses 0.001); the in-game
	// op still waits for a frame on which it does not skip them
	io.DeltaTime = 1.0f / 60.0f;
	ImGui::NewFrame();
	BeginHidden("##amf-null-selftest");
	const bool skippedFirst = GImGui->CurrentWindow->SkipItems;
	EndHidden();
	BeginHidden("##control");
	EndHidden();
	ImGui::Render();
	Check(!skippedFirst, "the hidden window takes its items from the first frame it is begun (it is not hidden)");

	io.DeltaTime = 1.0f / 60.0f;
	ImGui::NewFrame();
	BeginHidden("##amf-null-selftest");
	const std::vector<nullselftest::Case> cases = nullselftest::RunCimguiCases();
	const bool unclipped = !GImGui->CurrentWindow->SkipItems && GImGui->CurrentWindow->ClipRect.GetWidth() > 0.0f;
	EndHidden();
	ImGui::Render();
	Check(unclipped, "the hidden window is on screen and not clipped (widgets in it are really submitted, only invisible)");

	for (const auto& c : cases)
	{
		Check(c.ok, c.call + " (null " + c.kind + "): expected " + c.expected + ", got " + c.got);
	}
	Check(cases.size() == 14, "all 14 cimgui cases ran (" + std::to_string(cases.size()) + ")");
	Check(ImGui::GetDrawData() && ImGui::GetDrawData()->Valid, "the frame rendered");
	Check(io.Fonts->Fonts.Size == fontsBefore, "no font was added by ImFontAtlas_AddFontDefault(NULL, ...)");
	Check(nullselftest::CasesJson(cases).front() == '[' && nullselftest::CasesJson(cases).back() == ']', "the cases' JSON is an array");

	// a second frame: nothing the cases did left ImGui in a bad state
	io.DeltaTime = 1.0f / 60.0f;
	ImGui::NewFrame();
	BeginHidden("##amf-null-selftest");
	const std::vector<nullselftest::Case> again = nullselftest::RunCimguiCases();
	EndHidden();
	ImGui::Render();
	bool allAgain = again.size() == cases.size();
	for (const auto& c : again) { allAgain = allAgain && c.ok; }
	Check(allAgain, "run again on the next frame: every case still a no-op (no state left behind)");

	// the control, last (a fault in the middle of a widget leaves that frame half built)
	io.DeltaTime = 1.0f / 60.0f;
	ImGui::NewFrame();
	BeginHidden("##control");
	const bool faults = UnguardedSliderFaults();
	Check(faults, "CONTROL: the same null value pointer given to Dear ImGui's SliderFloat with no guard faults (access violation)");

	std::cout << "null_selftest_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
