// Off-game check of the FLICK table AMF answers (2.3.0, 4. plans\amf-flick-v6\PLAN.md): include/FlickSlots.inc is FLICK
// API version 7 with 303 slots; version 5's 259 slots (tools/flick_slots_v5.inc, generated from the v5 header) are its
// prefix slot for slot - same name, return type and parameter types - except the 255th slot (index 254), VSliderButton, whose parameters 7 and
// 8 went from bool to const char* in v6; source/FlickHost.cpp keeps VSliderButton the stub that reads no argument (never
// bound by FLICK_DO / FLICK_GUARD) and answers every slot v6 and v7 added; and include/FlickToolPath.h splits a nested group.
//   tools\flick_slots_check.bat
// Prints one line per check and "flick_slots_check: N of N passed"; exits 1 on any failure.
#include "../include/FlickToolPath.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
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

	struct Slot { std::string ret, name, params; };

	const std::vector<Slot> kNow = {
#define FLICK_SLOT(ret, name, params) { #ret, #name, #params },
#include "../include/FlickSlots.inc"
#undef FLICK_SLOT
	};
	const std::vector<Slot> kV5 = {
#define FLICK_V5_SLOT(ret, name, params) { #ret, #name, #params },
#include "flick_slots_v5.inc"
#undef FLICK_V5_SLOT
	};
}

int main(int argc, char** argv)
{
	// ---- the table ----
	Check(FLICK_API_VERSION_ANSWERED == 7, "the table answers version " + std::to_string(FLICK_API_VERSION_ANSWERED) + " (expected 7)");
	Check(FLICK_SLOT_COUNT == 303 && kNow.size() == 303, "the table has " + std::to_string(kNow.size()) + " slots (expected 303)");
	Check(4 + FLICK_SLOT_COUNT * 8 == 2428, "the table is 2428 bytes: a 4-byte version, then 303 pointers");
	Check(FLICK_V5_SLOT_COUNT == 259 && kV5.size() == 259, "version 5's list has " + std::to_string(kV5.size()) + " slots (expected 259)");
	{
		std::set<std::string> names;
		for (const auto& s : kNow) { names.insert(s.name); }
		Check(names.size() == kNow.size(), "every slot name is unique");
	}

	// ---- version 5 is a prefix, slot 255 aside ----
	int same = 0;
	std::string firstDiff;
	for (std::size_t i = 0; i < kV5.size() && i < kNow.size(); ++i)
	{
		const bool equal = kV5[i].ret == kNow[i].ret && kV5[i].name == kNow[i].name && kV5[i].params == kNow[i].params;
		if (equal) { ++same; continue; }
		if (i == 254) { continue; }
		if (firstDiff.empty()) { firstDiff = std::to_string(i) + " " + kV5[i].name + " / " + kNow[i].name; }
	}
	Check(same == 258 && firstDiff.empty(), "version 5's slots 0-258 are the same in version 7, all but the 255th, VSliderButton (" + std::to_string(same) +
												" of 259 identical" + (firstDiff.empty() ? "" : "; first difference at " + firstDiff) + ")");
	Check(kNow.size() > 254 && kNow[254].name == "VSliderButton" && kV5[254].name == "VSliderButton", "the 255th slot (index 254) is VSliderButton in both");
	Check(kV5[254].params == "(const char*, const ImVec2&, float*, float, float, const char*, bool, bool, bool*, bool*)",
		  "v5 VSliderButton: parameters 7 and 8 are bool");
	Check(kNow[254].params == "(const char*, const ImVec2&, float*, float, float, const char*, const char*, const char*, bool*, bool*)",
		  "v6+ VSliderButton: parameters 7 and 8 are const char* (labels)");
	Check(kNow[254].ret == kV5[254].ret, "VSliderButton's return type is unchanged");
	Check(kNow[259].name == "ColorButton" && kNow[272].name == "SetWindowAlphaOverride", "version 6's 14 slots sit at 259-272");
	Check(kNow[273].name == "ComboWithKeyBadges" && kNow[302].name == "GetInputCaptureState", "version 7's 30 slots sit at 273-302");

	// ---- what FlickHost.cpp binds ----
	const std::string hostPath = argc > 1 ? argv[1] : "source/FlickHost.cpp";
	std::ifstream in(hostPath, std::ios::binary);
	const std::string host((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	Check(!host.empty(), "read " + hostPath);
	Check(host.find("FLICK_DO(VSliderButton)") == std::string::npos && host.find("FLICK_GUARD(VSliderButton)") == std::string::npos &&
			  host.find("t.VSliderButton =") == std::string::npos,
		  "VSliderButton is not bound: it stays the generated stub, which reads no argument (right for the v5 bools and the v6 labels)");
	{
		std::string missing;
		for (std::size_t i = 259; i < kNow.size(); ++i)
		{
			if (host.find("FLICK_DO(" + kNow[i].name + ")") == std::string::npos) { missing += (missing.empty() ? "" : ", ") + kNow[i].name; }
		}
		Check(missing.empty(), "every slot of versions 6 and 7 is answered by FlickHost.cpp" + (missing.empty() ? std::string() : " - missing: " + missing));
	}
	for (const char* drawing : { "ColorButton", "ColorPicker3", "ColorPicker4", "BeginStickyList", "EndStickyList", "StickyHeader",
								 "ComboWithKeyBadges", "DrawCameraPresets", "DrawCameraSettings", "DrawCameraHelpMarker" })
	{
		Check(host.find(std::string("FLICK_GUARD(") + drawing + ")") != std::string::npos,
			  std::string(drawing) + " draws, so it is bound through the frame guard");
	}

	// ---- nested groups (FlickToolPath.h) ----
	using flick::toolpath::Of;
	{
		const auto p = Of("Camera::Presets", "Orbit");
		Check(p.entry == "Camera (FLICK)" && p.page == "Presets/Orbit" && p.base == "Camera", "\"Camera::Presets\" + Orbit -> entry Camera, page Presets/Orbit");
	}
	{
		const auto p = Of("A::B::C::D", "T");
		Check(p.entry == "A (FLICK)" && p.page == "B/C/D/T", "four deep -> entry A, page B/C/D/T");
	}
	{
		const auto p = Of("General", "Main");
		Check(p.entry == "General (FLICK)" && p.page == "Main", "a group without :: keeps the 2.2.x entry and page");
	}
	{
		const auto p = Of("", "Main");
		Check(p.entry == "Main (FLICK)" && p.page == "Main", "no group keeps the 2.2.x entry and page");
	}
	{
		const auto p = Of(" General ", "");
		Check(p.entry == " General  (FLICK)" && p.page == "Settings", "a group without :: is not trimmed (a saved name stays the same); no name -> page Settings");
	}
	{
		const auto p = Of("A:: ::B::", "T");
		Check(p.entry == "A (FLICK)" && p.page == "B/T", "empty and blank segments are dropped");
	}
	{
		const auto p = Of("::", "T");
		Check(p.entry == "T (FLICK)" && p.page == "T", "a group of nothing but :: is treated as no group");
	}
	{
		const auto p = Of("$DEMO_Group::Sub", "$DEMO_Tool");
		Check(p.entry == "$DEMO_Group (FLICK)" && p.page == "Sub/$DEMO_Tool", "translated keys are split the same way");
	}

	std::cout << "flick_slots_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail ? 1 : 0;
}
