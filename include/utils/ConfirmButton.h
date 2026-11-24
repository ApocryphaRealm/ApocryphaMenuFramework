#pragma once

// 2.5.0 (polish B8): a button that asks before it destroys.
//
// Of everything in this menu that throws the player's work away, only the settings preset's Delete asked first. Resetting
// every control, putting every colour back to the theme's, deleting a layout preset, resetting the list's order and
// forgetting a mod's remembered settings all happened on one press - one slip of the thumb on a pad.
//
// The first press ARMS the button: for four seconds it reads "Press again to confirm" in a warning tint. A second press
// in that time does the thing; anything else - waiting, pressing another button - leaves everything as it was. No popup
// and no second button, so it works the same with a mouse, the keyboard and a controller, and the highlight never has
// to move. Only one button is armed at a time.
//
// a_id names the button for ImGui (the label changes while armed, so the label cannot be the id).

#include "Strings.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <string>

namespace widgets
{
	// True on the confirming press only.
	inline bool ConfirmButton(const char* a_label, const char* a_id, bool a_small = false)
	{
		static ImGuiID s_armed = 0;
		static double s_armedAt = 0.0;
		const std::string idPart = std::string("###") + (a_id ? a_id : "confirm");
		const ImGuiID id = ImGui::GetID(idPart.c_str());
		if (s_armed == id && ImGui::GetTime() - s_armedAt >= 4.0) { s_armed = 0; }
		const bool armed = s_armed == id;

		const std::string label = std::string(armed ? strings::TR("AMF_ConfirmAgain", "Press again to confirm") : (a_label ? a_label : "")) + idPart;
		if (armed)
		{
			// part-way from the button's own colour to a warning red, so it reads as "careful" on every theme
			const ImVec4 warn(0.75f, 0.27f, 0.27f, 1.0f);
			ImGui::PushStyleColor(ImGuiCol_Button, ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_Button), warn, 0.55f));
			ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered), warn, 0.65f));
			ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive), warn, 0.75f));
		}
		const bool pressed = a_small ? ImGui::SmallButton(label.c_str()) : ImGui::Button(label.c_str());
		if (armed) { ImGui::PopStyleColor(3); }

		if (!pressed) { return false; }
		if (armed)
		{
			s_armed = 0;
			return true;
		}
		s_armed = id;
		s_armedAt = ImGui::GetTime();
		return false;
	}
}
