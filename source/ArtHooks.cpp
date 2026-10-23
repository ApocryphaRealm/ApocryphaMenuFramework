#include "PCH.h"

#include "ArtHooks.h"
#include "ArtPlate.h"
#include "Renderer.h"

#include "Skin.h"
#include "Theme.h"

#include "utils/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace arthooks
{
	namespace
	{
		using skin::ArtKind;
		using artplate::NineSlice;
		using artplate::ThreeSlice;
		using artplate::Unit;

		// The colour role that recolours a kind's own-colour art when the player picks it.
		int OwnRole(ArtKind a_kind)
		{
			switch (a_kind)
			{
			case ArtKind::kBox:         return theme::kRoleBoxes;
			case ArtKind::kButton:      return theme::kRoleButtons;
			case ArtKind::kTickBox:     return theme::kRoleBoxes;
			case ArtKind::kSlider:      return theme::kRoleSlider;
			case ArtKind::kScrollbar:   return theme::kRoleScrollbar;
			case ArtKind::kTab:         return theme::kRoleTabs;
			case ArtKind::kSliderTrack: return theme::kRoleSliderTrack;
			case ArtKind::kScrollTrack: return theme::kRoleScrollTrack;
			default:                    return -1;
			}
		}

		// a_outline: the control's basic outline (next after 2.1.9, the owner 2026-10-12: "everything should have a basic minimal
		// outline so that it's at least visible. And then the detailed art styled frame ... when it's focused or hovered"). A
		// part with no edge layer of its own - every shipped box, button and tick box - covered the rect without one, so a face
		// in the panel's colour (the default) vanished. The outline is ImGui's own border line, in the Border colour (which
		// theme::Apply keeps visible), at least one pixel wide; the hover frame still draws over it.
		// 2.3.0: set by the on-screen keyboard around its keys - their outline is drawn from the button part's own shape
		// (artplate::DrawPlatePart). Render thread only.
		bool g_shapedButtons = false;

		bool DrawPlate(ImDrawList* a_dl, ArtKind a_kind, const ImRect& a_bb, ImU32 a_col, bool a_outline = false)
		{
			const skin::ArtPart* part = skin::Part(a_kind);
			if (!part) { return false; }
			ImU32 edgeCol = ImGui::GetColorU32(ImGuiCol_Border);
			if (part->edge.srv && part->ownColours)
			{
				// art in its own colours: as painted (the Frame art tint), or the kind's own colour once picked
				const int role = OwnRole(a_kind);
				const std::uint32_t c = role >= 0 && theme::RolePicked(role) ? theme::RoleColor(role) : theme::RoleColor(theme::kRoleArt);
				edgeCol = c ? static_cast<ImU32>(c) : IM_COL32_WHITE;
			}
			const ImGuiStyle& st = ImGui::GetStyle();
			const ImVec4 panel = st.Colors[ImGuiCol_WindowBg];
			const bool shaped = g_shapedButtons && a_kind == ArtKind::kButton;
			return artplate::DrawPlatePart(a_dl, *part, a_bb, a_col, a_outline, shaped, ImGui::GetColorU32(ImGuiCol_Border),
										   st.FrameBorderSize, st.FrameRounding,
										   ImGui::ColorConvertFloat4ToU32(ImVec4(panel.x, panel.y, panel.z, st.Alpha)), edgeCol);
		}

		// RenderFrame is every widget's field and every button: told apart by the colour ImGui asked for.
		// 2.1.6 (the owner: Buttons art "doesn't actually seem to apply to ... the filter or sort buttons, or where mods keep
		// their save, reload ... and restore defaults buttons"): told apart by WHAT the item is first, the colour second. A
		// theme whose buttons and fields share a colour sent every button to Box, and a mod's button in its own colours
		// matched neither. The item RenderFrame draws for was just added: text fields, sliders and drags are "inputable".
		int FrameKind(ImU32 a_col)
		{
			for (const ImGuiCol c : { ImGuiCol_Header, ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive })
			{
				if (ImGui::GetColorU32(c) == a_col) { return -1; }   // rows and headers keep ImGui's own look
			}
			const ImGuiContext& g = *GImGui;
			if (g.LastItemData.InFlags & ImGuiItemFlags_Inputable) { return static_cast<int>(ArtKind::kBox); }
			for (const ImGuiCol c : { ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive })
			{
				if (ImGui::GetColorU32(c) == a_col) { return static_cast<int>(ArtKind::kButton); }
			}
			for (const ImGuiCol c : { ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive })
			{
				if (ImGui::GetColorU32(c) == a_col) { return static_cast<int>(ArtKind::kBox); }
			}
			return static_cast<int>(ArtKind::kButton);   // a mod's button in colours of its own
		}

		// The section line: a strip drawn as end caps and a stretched middle, centred on the line ImGui would draw, its
		// height the part's own at 1080p. Tinted with the separator colour.
		bool DrawSection(ImDrawList* a_dl, const ImRect& a_bb, ImU32 a_col)
		{
			const skin::ArtPart* part = skin::Part(ArtKind::kSection);
			if (!part || a_bb.GetWidth() < 4.0f) { return false; }
			float k = (part->drawCorner > 0.0f ? part->drawCorner / std::max(1.0f, part->corner) : 0.5f) * Unit();
			// 2.1.6 (the owner's screenshot: the caps "end up poking into the things above and below them when the rows are too
			// close together"): never taller than the gap between rows allows
			const float maxH = std::max(4.0f, ImGui::GetStyle().ItemSpacing.y * 2.0f + a_bb.GetHeight());
			if (part->main.h * k > maxH) { k = maxH / part->main.h; }
			const float h = std::max(2.0f, std::round(part->main.h * k));
			const float cap = part->corner * k;
			const float cy = std::round((a_bb.Min.y + a_bb.Max.y) * 0.5f);
			const float y0 = cy - h * 0.5f, y1 = y0 + h;
			const float x0 = a_bb.Min.x, x3 = a_bb.Max.x;
			const float u1 = part->corner / part->main.w, u2 = 1.0f - u1;
			const auto tex = reinterpret_cast<ImTextureID>(part->main.srv);
			// a short piece (the bit before a heading's text) is the rail alone - two squeezed caps read as a smudge
			if (a_bb.GetWidth() < cap * 3.0f)
			{
				a_dl->AddImage(tex, ImVec2(x0, y0), ImVec2(x3, y1), ImVec2(u1, 0.0f), ImVec2(u2, 1.0f), a_col);
				return true;
			}
			const float x1 = x0 + cap, x2 = x3 - cap;
			a_dl->AddImage(tex, ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(0.0f, 0.0f), ImVec2(u1, 1.0f), a_col);
			if (x2 > x1) { a_dl->AddImage(tex, ImVec2(x1, y0), ImVec2(x2, y1), ImVec2(u1, 0.0f), ImVec2(u2, 1.0f), a_col); }
			a_dl->AddImage(tex, ImVec2(x2, y0), ImVec2(x3, y1), ImVec2(u2, 0.0f), ImVec2(1.0f, 1.0f), a_col);
			return true;
		}

		// An arrow: the part points right and is turned to the direction asked, filling the square ImGui would fill.
		bool DrawArrow(ImDrawList* a_dl, const ImRect& a_bb, ImU32 a_col, int a_dir)
		{
			const skin::ArtPart* part = skin::Part(ArtKind::kArrow);
			// 2.1.6: the Arrows colour (Text until picked). Only the text-coloured arrows take it - a disabled row's dim one stays.
			const bool picked = theme::RolePicked(theme::kRoleArrows) && a_col == ImGui::GetColorU32(ImGuiCol_Text);
			if (picked) { a_col = static_cast<ImU32>(theme::RoleColor(theme::kRoleArrows)); }
			if (!part)
			{
				if (!picked) { return false; }
				// the built-in triangle, in the picked colour (ImGui's own RenderArrow, scale 1)
				const ImVec2 c((a_bb.Min.x + a_bb.Max.x) * 0.5f, (a_bb.Min.y + a_bb.Max.y) * 0.5f);
				const float r = (a_bb.Max.x - a_bb.Min.x) * 0.5f * 0.40f;
				ImVec2 a, b, d;
				switch (a_dir)
				{
				case ImGuiDir_Up:    a = ImVec2(0, -0.75f); b = ImVec2(-0.866f, 0.75f); d = ImVec2(0.866f, 0.75f); break;
				case ImGuiDir_Down:  a = ImVec2(0, 0.75f); b = ImVec2(-0.866f, -0.75f); d = ImVec2(0.866f, -0.75f); break;
				case ImGuiDir_Left:  a = ImVec2(-0.75f, 0); b = ImVec2(0.75f, 0.866f); d = ImVec2(0.75f, -0.866f); break;
				default:             a = ImVec2(0.75f, 0); b = ImVec2(-0.75f, -0.866f); d = ImVec2(-0.75f, 0.866f); break;
				}
				a_dl->AddTriangleFilled(ImVec2(c.x + a.x * r, c.y + a.y * r), ImVec2(c.x + b.x * r, c.y + b.y * r), ImVec2(c.x + d.x * r, c.y + d.y * r), a_col);
				return true;
			}
			const ImVec2 a = a_bb.Min, b(a_bb.Max.x, a_bb.Min.y), c = a_bb.Max, d(a_bb.Min.x, a_bb.Max.y);
			// UVs at the four screen corners (top-left, top-right, bottom-right, bottom-left) for each turn
			ImVec2 uv[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };   // right: as drawn
			switch (a_dir)
			{
			case ImGuiDir_Down: uv[0] = { 0, 1 }; uv[1] = { 0, 0 }; uv[2] = { 1, 0 }; uv[3] = { 1, 1 }; break;
			case ImGuiDir_Left: uv[0] = { 1, 1 }; uv[1] = { 0, 1 }; uv[2] = { 0, 0 }; uv[3] = { 1, 0 }; break;
			case ImGuiDir_Up:   uv[0] = { 1, 0 }; uv[1] = { 1, 1 }; uv[2] = { 0, 1 }; uv[3] = { 0, 0 }; break;
			default: break;
			}
			a_dl->AddImageQuad(reinterpret_cast<ImTextureID>(part->main.srv), a, b, c, d, uv[0], uv[1], uv[2], uv[3], a_col);
			return true;
		}

		// The frame round a popup, a dropdown list or a tooltip: a frame part, smaller than round the window.
		void DrawPopup(ImDrawList* a_dl, const ImRect& a_bb)
		{
			const skin::ArtPart* part = skin::Part(ArtKind::kPopup);
			if (!part) { return; }
			const float base = part->drawCorner > 0.0f ? part->drawCorner : part->corner;
			const float dcs = std::min({ base * Unit() * 0.6f, a_bb.GetWidth() * 0.3f, a_bb.GetHeight() * 0.3f });
			const std::uint32_t tint = theme::RoleColor(theme::kRolePopups);   // 2.1.6: its own role (Frame art until picked)
			NineSlice(a_dl, part->main, part->corner, dcs, a_bb, tint ? static_cast<ImU32>(tint) : IM_COL32_WHITE, false);
		}

		// 2.1.6: a control drawn in its hovered (or held) colours is the one under the mouse - the theme's hover frame goes
		// round it, whatever page it is on (the owner: the mouse should bring up the frame the controller does). Scroll bars
		// and resize grips have colours of their own, so they never match.
		void NoteIfHovered(ImDrawList* a_dl, int a_part, const ImRect& a_bb, ImU32 a_col)
		{
			if (a_part != ImGuiArtPart_Frame && a_part != ImGuiArtPart_TickBox && a_part != ImGuiArtPart_SliderTrack &&
				a_part != ImGuiArtPart_Tab)
			{
				return;
			}
			for (const ImGuiCol c : { ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive,
									  ImGuiCol_HeaderHovered, ImGuiCol_HeaderActive, ImGuiCol_TabHovered })
			{
				if (ImGui::GetColorU32(c) == a_col)
				{
					renderer::NoteHoverRect(a_dl, a_bb.Min, a_bb.Max);
					return;
				}
			}
		}

		bool Hook(ImDrawList* a_dl, int a_part, const ImRect& a_bb, ImU32 a_col, int a_arg)
		{
			if (!a_dl) { return false; }
			NoteIfHovered(a_dl, a_part, a_bb, a_col);
			switch (a_part)
			{
			case ImGuiArtPart_Frame:
				{
					const int kind = FrameKind(a_col);
					// a_arg = RenderFrame's border flag: a box or button asks for one, a bare fill (a progress bar's) does not
					return kind >= 0 && DrawPlate(a_dl, static_cast<ArtKind>(kind), a_bb, a_col, a_arg != 0);
				}
			case ImGuiArtPart_TickBox:    return DrawPlate(a_dl, ArtKind::kTickBox, a_bb, a_col, true);
			case ImGuiArtPart_TickMark:
				{
					const skin::ArtPart* part = skin::Part(ArtKind::kTickBox);
					if (!part || !part->extra.srv) { return false; }
					const float pad = std::max(1.0f, std::floor(a_bb.GetWidth() / 8.0f));
					a_dl->AddImage(reinterpret_cast<ImTextureID>(part->extra.srv), ImVec2(a_bb.Min.x + pad, a_bb.Min.y + pad),
								   ImVec2(a_bb.Max.x - pad, a_bb.Max.y - pad), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), a_col);
					return true;
				}
			case ImGuiArtPart_SliderGrab: return a_arg == 0 && DrawPlate(a_dl, ArtKind::kSlider, a_bb, a_col);
			case ImGuiArtPart_ScrollGrab: return a_arg == ImGuiAxis_Y && DrawPlate(a_dl, ArtKind::kScrollbar, a_bb, a_col);
			case ImGuiArtPart_ScrollTrack:   // 2.1.6: its own kind (Scroll bar tracks), in the Scroll bar track colour
				return a_arg == ImGuiAxis_Y && DrawPlate(a_dl, ArtKind::kScrollTrack, a_bb, a_col);
			case ImGuiArtPart_SliderTrack:   // 2.1.6: a slider's track, its own kind and colour (the owner)
				{
					const bool picked = theme::RolePicked(theme::kRoleSliderTrack);
					if (picked)
					{
						// the track's own colour, with ImGui's hover / active shading kept as a lighter step
						const ImVec4 base = ImGui::ColorConvertU32ToFloat4(static_cast<ImU32>(theme::RoleColor(theme::kRoleSliderTrack)));
						const float lift = a_col == ImGui::GetColorU32(ImGuiCol_FrameBg) ? 0.0f : 0.08f;
						a_col = ImGui::ColorConvertFloat4ToU32(ImVec4(std::min(base.x + lift, 1.0f), std::min(base.y + lift, 1.0f), std::min(base.z + lift, 1.0f), base.w));
					}
					if (DrawPlate(a_dl, ArtKind::kSliderTrack, a_bb, a_col, true)) { return true; }
					if (!picked) { return false; }
					const ImGuiStyle& st = ImGui::GetStyle();
					a_dl->AddRectFilled(a_bb.Min, a_bb.Max, a_col, st.FrameRounding);
					a_dl->AddRect(a_bb.Min, a_bb.Max, ImGui::GetColorU32(ImGuiCol_Border), st.FrameRounding, 0, std::max(1.0f, st.FrameBorderSize));
					return true;
				}
			case ImGuiArtPart_Separator:  return DrawSection(a_dl, a_bb, a_col);
			case ImGuiArtPart_Tab:        return DrawPlate(a_dl, ArtKind::kTab, a_bb, a_col, true);
			case ImGuiArtPart_Arrow:      return DrawArrow(a_dl, a_bb, a_col, a_arg);
			case ImGuiArtPart_Popup:      DrawPopup(a_dl, a_bb); return false;
			default:                      return false;
			}
		}
	}

	void SetShapedButtons(bool a_on) { g_shapedButtons = a_on; }

	void Install()
	{
		GImGuiArtHook = &Hook;
		logger::info("art: ImGui art hook installed (boxes, buttons, tick boxes, sliders, scroll bars, section lines, tabs, "
					 "arrows, popups)");
	}

	bool DrawCursor(bool a_wanted)
	{
		const skin::ArtPart* part = skin::Part(ArtKind::kCursor);
		if (!a_wanted || !part || part->main.h <= 0.0f) { return false; }
		ImGuiIO& io = ImGui::GetIO();
		if (!ImGui::IsMousePosValid(&io.MousePos)) { return true; }
		// uDrawCorner is the pointer's height at 1080p; without it, half the art's own height (drawn at twice the size)
		const float h = (part->drawCorner > 0.0f ? part->drawCorner : part->main.h * 0.5f) * Unit();
		const float k = h / part->main.h;
		const ImVec2 at(io.MousePos.x - part->hotX * k, io.MousePos.y - part->hotY * k);
		const std::uint32_t tint = theme::RoleColor(theme::kRolePointer);   // 2.1.6: the Mouse pointer colour (white = as drawn)
		ImGui::GetForegroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(part->main.srv), at,
												 ImVec2(at.x + part->main.w * k, at.y + h), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
												 tint ? static_cast<ImU32>(tint) : IM_COL32_WHITE);
		return true;
	}
}
