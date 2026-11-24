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
#include <imgui_internal.h>

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

		bool changed = ImGui::InvisibleButton("##toggle", ImVec2(width, height));
		const bool hovered = ImGui::IsItemHovered();
		// 2.1.6: the theme's hover frame round the switch under the mouse, as round every other control (drawn by hand, so
		// not through the art hook)
		if (hovered) { renderer::NoteHoverRect(drawList, pos, ImVec2(pos.x + width, pos.y + height)); }

		if (changed && a_value)
		{
			*a_value = !*a_value;
		}

		const bool isOn = a_value && *a_value;

		// 2.5.0 (polish C7): the knob SLIDES to its side over about a tenth of a second, and the track's colour goes with
		// it, instead of both jumping. Where it is on its way is kept per switch in the window's own storage. A switch not
		// drawn last frame (its page was just opened, or another page's switch with the same name took its place) starts
		// at rest, so opening a page never shows every knob travelling.
		float slide = isOn ? 1.0f : 0.0f;
		if (ImGuiStorage* const store = ImGui::GetStateStorage())
		{
			const float target = slide;
			const int frame = ImGui::GetFrameCount();
			int* const seen = store->GetIntRef(ImGui::GetID("##knobframe"), -1);
			float* const at = store->GetFloatRef(ImGui::GetID("##knobat"), target);
			if (seen && at)
			{
				if (*seen < frame - 1) { *at = target; }
				else
				{
					const float step = ImGui::GetIO().DeltaTime * 9.0f;
					*at = *at < target ? std::min(target, *at + step) : std::max(target, *at - step);
				}
				*seen = frame;
				slide = *at;
			}
		}

		// 2.1.5: the track takes the player's Switch on / Switch off colours (Appearance > Colours; the theme's green and
		// red by default), a little lighter while hovered.
		const std::uint32_t onU32 = theme::RoleColor(theme::kRoleSwitchOn);
		const std::uint32_t offU32 = theme::RoleColor(theme::kRoleSwitchOff);
		const ImVec4 onColor = ImGui::ColorConvertU32ToFloat4(onU32 ? static_cast<ImU32>(onU32) : IM_COL32(76, 175, 80, 255));
		const ImVec4 offColor = ImGui::ColorConvertU32ToFloat4(offU32 ? static_cast<ImU32>(offU32) : IM_COL32(191, 68, 68, 255));
		ImVec4 track = ImLerp(offColor, onColor, slide);   // the colour travels with the knob
		if (hovered) { track = ImVec4(std::min(track.x + 0.06f, 1.0f), std::min(track.y + 0.06f, 1.0f), std::min(track.z + 0.06f, 1.0f), track.w); }
		// 2.5.0 (polish A1): a greyed-out switch kept its bright pill because these colours are made by hand and so never
		// took the style's alpha. GetColorU32 multiplies by it, and BeginDisabled lowers it - the switch now fades with
		// its label, as every other disabled control does.
		const ImU32 trackColor = ImGui::GetColorU32(track);

		const float knobX = pos.x + radius + (width - height) * slide;

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
								   ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImGui::GetColorU32(artTint ? static_cast<ImU32>(artTint) : IM_COL32_WHITE));
			}
			else
			{
				// next after 2.1.9: every control keeps a basic outline (the owner, 2026-10-12) - a track art with no outline layer
				drawList->AddRect(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(ImGuiCol_Border), 0.0f, 0,
								  std::max(1.0f, ImGui::GetStyle().FrameBorderSize));
			}
		}
		else
		{
			drawList->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), trackColor, radius);
			drawList->AddRect(pos, ImVec2(pos.x + width, pos.y + height), ImGui::GetColorU32(ImGuiCol_Border), radius, 0,
							  std::max(1.0f, ImGui::GetStyle().FrameBorderSize));
		}
		// 2.1.6 (the owner, 2026-10-08: the knob "should have a different shape as well ... a separate thing ... match the frame
		// art ... that piece that's at the corner of each frame"): a Switch knobs part draws in the frame art's colour.
		if (const skin::ArtPart* knob = skin::Part(skin::ArtKind::kKnob); knob && knob->main.srv)
		{
			const std::uint32_t knobTint = theme::RoleColor(theme::kRoleKnob);   // the Switch knob colour (white = as drawn)
			const float r = radius - 1.0f;
			drawList->AddImage(reinterpret_cast<ImTextureID>(knob->main.srv), ImVec2(knobX - r, pos.y + radius - r),
							   ImVec2(knobX + r, pos.y + radius + r), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
							   ImGui::GetColorU32(knobTint ? static_cast<ImU32>(knobTint) : IM_COL32_WHITE));
		}
		else
		{
			const bool picked = theme::RolePicked(theme::kRoleKnob);
			drawList->AddCircleFilled(ImVec2(knobX, pos.y + radius), radius - 2.0f,
									  ImGui::GetColorU32(picked ? static_cast<ImU32>(theme::RoleColor(theme::kRoleKnob)) : IM_COL32(240, 240, 240, 255)), 32);
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
				// 2.2.4 (the menu render matrix: converted MCM pages' switch labels were cut at the column's edge from text size
				// 2.00 - "Force camera to thirdpers"): a label wider than the room left on its line wraps inside the column
				// instead. One that fits draws exactly as before. A wrap position the caller set is kept.
				const ImGuiWindow* labelWindow = ImGui::GetCurrentWindow();
				const bool wrap = labelWindow && labelWindow->DC.TextWrapPos < 0.0f;
				if (wrap) { ImGui::PushTextWrapPos(0.0f); }
				ImGui::TextUnformatted(visible.data(), visible.data() + visible.size());
				if (wrap) { ImGui::PopTextWrapPos(); }
				// 2.5.0 (polish B9): a click on the label flips the switch, as a click on a tick box's label does - the
				// label is the bigger and nearer target. A greyed-out switch's label is not hovered, so it stays put.
				if (a_value && ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					*a_value = !*a_value;
					changed = true;
				}
			}
		}

		return changed;
	}
}
