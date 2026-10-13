// Off-game check of a slider dragged with real-mouse-shaped input (AMF 2.2.0, the final5 test: the Text size slider
// did not take real mouse presses while DevBench's op=click set it). Runs AMF's own ImGui build (1.90.8 with the overlay
// patches, the SE line's vcpkg tree) headless, with the framework's input rules: trickling off, the pointer published
// once per frame, and one transition per button per frame (include\InputFrames.h - the drain's own function).
//   tools\slider_drag_check.bat   -> one line per check and "slider_drag_check: N of N passed"; exits 1 on any failure.
// What it proves and what it cannot: a press on the slider, moves while held and a release on later frames drag it, a
// click shorter than a frame still sets it, the atlas rebuild AMF makes after the release does not lose the value, and
// ImGui's hovered item is the one under its own mouse position at text size 1.0 and 2.0 (the pointer art is drawn at
// that position minus its hot spot - ArtHooks.cpp DrawCursor). It cannot say where a real mouse put AMF's pointer in game.
#include "../include/InputFrames.h"
#include "../include/PreciseSlider.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	int g_pass = 0, g_fail = 0;
	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	struct Record
	{
		enum class Kind { kMove, kButton } kind;
		float x = 0, y = 0;
		bool down = false;
	};

	float g_value = 1.5f;
	ImRect g_sliderRect;
	bool g_hovered = false, g_edited = false, g_rebuildPending = false;
	ImVec2 g_pointer{ 0, 0 };
	float g_frameW = 0.0f;   // the slider's frame (its label sits after it)
	std::vector<Record> g_carry;
	float g_fontPx = 16.0f;

	void BuildFonts(float a_px)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->Clear();
		ImFontConfig cfg;
		cfg.SizePixels = a_px;
		io.Fonts->AddFontDefault(&cfg);
		unsigned char* pixels = nullptr;
		int w = 0, h = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
		io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(1)));
	}

	// One frame: drain the queued records the way Input.cpp does, then draw a window with the Text size slider the way
	// the Appearance page does - through precise::SliderFloat (include/PreciseSlider.h), the same call.
	void Frame(std::vector<Record> a_queue)
	{
		ImGuiIO& io = ImGui::GetIO();
		if (g_rebuildPending)   // Renderer: g_fontRebuildPending is served before NewFrame, outside a frame
		{
			g_rebuildPending = false;
			g_fontPx = 16.0f * 2.0f * g_value;
			BuildFonts(g_fontPx);
		}
		std::vector<Record> drained;
		drained.swap(g_carry);
		drained.insert(drained.end(), a_queue.begin(), a_queue.end());
		inputframes::OneTransitionPerFrame(drained, g_carry, [](const Record& r) -> std::uint64_t { return r.kind == Record::Kind::kButton ? 1 : 0; });
		for (const Record& r : drained)
		{
			if (r.kind == Record::Kind::kMove) { g_pointer = ImVec2(r.x, r.y); }
			else { io.AddMouseButtonEvent(0, r.down); }
		}
		io.AddMousePosEvent(g_pointer.x, g_pointer.y);   // once per frame, after the drain (Input.cpp)
		io.DeltaTime = 1.0f / 60.0f;
		ImGui::NewFrame();
		ImGui::SetNextWindowPos(ImVec2(100, 100));
		ImGui::SetNextWindowSize(ImVec2(1600, 900));
		ImGui::Begin("###amf-main", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		ImGui::TextUnformatted("Language");
		ImGui::TextWrapped("The framework's own text. Game language follows Skyrim's setting; pick one to force it.");
		g_frameW = ImGui::GetContentRegionAvail().x * 0.6f;
		ImGui::SetNextItemWidth(g_frameW);
		precise::SliderFloat("Text size", &g_value, 1.0f, 2.0f, "%.2f");
		g_sliderRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		g_hovered = ImGui::IsItemHovered();
		if (ImGui::IsItemDeactivatedAfterEdit())
		{
			g_edited = true;
			g_rebuildPending = true;   // re-rasterise at the new size, after the release (Renderer.cpp)
		}
		ImGui::End();
		ImGui::Render();
	}

	float TrackX(float a_fraction)
	{
		return g_sliderRect.Min.x + g_frameW * a_fraction;
	}
}

int main()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2(3200, 1800);
	io.IniFilename = nullptr;
	io.ConfigInputTrickleEventQueue = false;   // Renderer.cpp
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.MouseDrawCursor = true;
	BuildFonts(g_fontPx);
	Frame({});
	Frame({});

	const float midY = (g_sliderRect.Min.y + g_sliderRect.Max.y) * 0.5f;

	// 1. a drag: press on the track, moves on later frames while held, release
	g_value = 1.5f;
	g_edited = false;
	Frame({ { Record::Kind::kMove, TrackX(0.3f), midY } });
	Check(g_hovered, "the slider is hovered with the pointer on its track");
	Frame({ { Record::Kind::kButton, 0, 0, true } });
	const ImGuiID active = GImGui->ActiveId;
	Check(active != 0 && GImGui->ActiveIdSource == ImGuiInputSource_Mouse, "a real press makes the slider active, source mouse");
	for (int i = 1; i <= 6; ++i) { Frame({ { Record::Kind::kMove, TrackX(0.3f + 0.1f * i), midY } }); }
	Check(GImGui->ActiveId == active, "the slider stays active through the drag");
	const float dragged = g_value;
	Frame({ { Record::Kind::kButton, 0, 0, false } });
	Check(g_edited, "the release reports the edit (IsItemDeactivatedAfterEdit - AMF saves and rebuilds the atlas then)");
	Check(dragged > 1.75f, "the drag moved the value from 1.50 to " + std::to_string(dragged));
	Frame({});
	Check(std::fabs(g_value - dragged) < 0.0001f && g_fontPx > 16.0f * 2.0f * 1.7f, "the rebuild after the release keeps the value and builds at the new size");

	// 2. a click shorter than a frame: press and release in one drain - the release waits a frame, the press still lands
	g_edited = false;
	Frame({ { Record::Kind::kMove, TrackX(0.05f), (g_sliderRect.Min.y + g_sliderRect.Max.y) * 0.5f } });
	Frame({ { Record::Kind::kButton, 0, 0, true }, { Record::Kind::kButton, 0, 0, false } });
	Frame({});
	Frame({});
	Check(g_edited && g_value < 1.2f, "a click shorter than a frame on the track's start sets it to " + std::to_string(g_value));

	// 3. the hovered item is the one under ImGui's mouse position at text size 1.0 and 2.0 (pointer and hover share it)
	for (float size : { 1.0f, 2.0f })
	{
		g_value = size;
		g_rebuildPending = true;
		Frame({});
		Frame({});
		const float y = (g_sliderRect.Min.y + g_sliderRect.Max.y) * 0.5f;
		Frame({ { Record::Kind::kMove, TrackX(0.5f), y } });
		char what[160];
		std::snprintf(what, sizeof(what), "text size %.1f: font %.0f px, slider rows %.0f-%.0f, hovered under the pointer", size, g_fontPx,
			g_sliderRect.Min.y, g_sliderRect.Max.y);
		Check(g_hovered, what);
		Frame({ { Record::Kind::kMove, TrackX(0.5f), g_sliderRect.Min.y - 40.0f } });
		Check(!g_hovered, "  and not hovered 40 px above it");
	}

	// 4. control - the hypothesis the test raised: an atlas rebuild IN THE MIDDLE of a drag. AMF does not do this (it
	// rebuilds on the release), but if it did, ImGui keeps the active item as long as the item is drawn each frame.
	g_value = 1.5f;
	g_rebuildPending = true;
	Frame({});
	Frame({});
	const float y4 = (g_sliderRect.Min.y + g_sliderRect.Max.y) * 0.5f;
	Frame({ { Record::Kind::kMove, TrackX(0.5f), y4 } });
	Frame({ { Record::Kind::kButton, 0, 0, true } });
	const ImGuiID active4 = GImGui->ActiveId;
	BuildFonts(40.0f);
	Frame({ { Record::Kind::kMove, TrackX(0.6f), y4 } });
	Check(active4 != 0 && GImGui->ActiveId == active4, "control: a rebuild mid-drag does not drop the active slider either");
	Frame({ { Record::Kind::kButton, 0, 0, false } });

	ImGui::DestroyContext();
	std::cout << "slider_drag_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail ? 1 : 0;
}
