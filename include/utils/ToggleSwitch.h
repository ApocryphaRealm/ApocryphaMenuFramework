#pragma once

// The on/off toggle switch every boolean in this project renders as instead of a tick-box
// (CLAUDE.md rule 32). Same visual design as the vendored SMF-page version in the mod repos
// (utils/Toggle.h there): red/green pill track, sliding circular knob, label to the right -
// but written against the real embedded Dear ImGui API rather than SMF's cimgui exports,
// because this framework owns its ImGui.

#include "Renderer.h"
#include "Skin.h"
#include "Theme.h"

#include <imgui.h>

#include <algorithm>
#include <string_view>

namespace widgets
{
	inline bool Toggle(const char* a_label, bool* a_value)
	{
		ImGui::PushID(a_label);

		const float height = ImGui::GetFrameHeight();
		const float width = height * 2.0f;
		const float radius = height * 0.5f;

		const ImVec2 pos = ImGui::GetCursorScreenPos();
		ImDrawList* drawList = ImGui::GetWindowDrawList();

		const bool changed = ImGui::InvisibleButton("##toggle", ImVec2(width, height));
		const bool hovered = ImGui::IsItemHovered();
		// 2.1.6: the theme's hover frame round the switch under the mouse, as round every other control (drawn by hand, so
		// not through the art hook)
		if (hovered) { renderer::NoteHoverRect(drawList, pos, ImVec2(pos.x + width, pos.y + height)); }

		if (changed && a_value)
		{
			*a_value = !*a_value;
		}

		const bool isOn = a_value && *a_value;

		// 2.1.5: the track takes the player's Switch on / Switch off colours (Appearance > Colours; the theme's green and
		// red by default), a little lighter while hovered.
		const std::uint32_t pickedU32 = theme::RoleColor(isOn ? theme::kRoleSwitchOn : theme::kRoleSwitchOff);
		const ImU32 baseColor = pickedU32 ? static_cast<ImU32>(pickedU32) : (isOn ? IM_COL32(76, 175, 80, 255) : IM_COL32(191, 68, 68, 255));
		ImVec4 track = ImGui::ColorConvertU32ToFloat4(baseColor);
		if (hovered) { track = ImVec4(std::min(track.x + 0.06f, 1.0f), std::min(track.y + 0.06f, 1.0f), std::min(track.z + 0.06f, 1.0f), track.w); }
		const ImU32 trackColor = ImGui::ColorConvertFloat4ToU32(track);

		const float knobX = pos.x + radius + (isOn ? (width - height) : 0.0f);

		// A UI author may supply toggle.png to restyle the track (see Skin.h). The knob still
		// draws over it, so the switch remains readable as a switch whatever the art does, and
		// the on/off tint is still applied - the plate carries the SHAPE, the state stays legible.
		if (skin::HasPlate(skin::Plate::kToggle))
		{
			drawList->AddImage(reinterpret_cast<ImTextureID>(skin::PlateTexture(skin::Plate::kToggle)),
							   pos, ImVec2(pos.x + width, pos.y + height),
							   ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), trackColor);
			// 2.1.6: the outline over it in its own colours (the Frame art tint), so the green and red stay inside the track
			if (void* edge = skin::ToggleEdgeTexture())
			{
				const std::uint32_t artTint = theme::RoleColor(theme::kRoleArt);
				drawList->AddImage(reinterpret_cast<ImTextureID>(edge), pos, ImVec2(pos.x + width, pos.y + height),
								   ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), artTint ? static_cast<ImU32>(artTint) : IM_COL32_WHITE);
			}
		}
		else
		{
			drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), trackColor, radius);
		}
		// 2.1.6 (the owner, 2026-10-08: the knob "should have a different shape as well ... a separate thing ... match the frame
		// art ... that piece that's at the corner of each frame"): a Switch knobs part draws in the frame art's colour.
		if (const skin::ArtPart* knob = skin::Part(skin::ArtKind::kKnob); knob && knob->main.srv)
		{
			const std::uint32_t knobTint = theme::RoleColor(theme::kRoleKnob);   // the Switch knob colour (white = as drawn)
			const float r = radius - 1.0f;
			drawList->AddImage(reinterpret_cast<ImTextureID>(knob->main.srv), ImVec2(knobX - r, pos.y + radius - r),
							   ImVec2(knobX + r, pos.y + radius + r), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
							   knobTint ? static_cast<ImU32>(knobTint) : IM_COL32_WHITE);
		}
		else
		{
			const bool picked = theme::RolePicked(theme::kRoleKnob);
			drawList->AddCircleFilled(ImVec2(knobX, pos.y + radius), radius - 2.0f,
									  picked ? static_cast<ImU32>(theme::RoleColor(theme::kRoleKnob)) : IM_COL32(240, 240, 240, 255), 32);
		}

		ImGui::PopID();

		if (a_label)
		{
			// "##" onward is an ID disambiguator, not part of the visible label.
			std::string_view label{ a_label };
			const size_t hashPos = label.find("##");
			const std::string_view visible = (hashPos == std::string_view::npos) ? label : label.substr(0, hashPos);

			if (!visible.empty())
			{
				ImGui::SameLine();
				ImGui::AlignTextToFramePadding();
				ImGui::Text("%.*s", static_cast<int>(visible.size()), visible.data());
			}
		}

		return changed;
	}
}
