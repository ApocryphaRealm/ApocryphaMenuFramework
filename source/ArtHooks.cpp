#include "PCH.h"

#include "ArtHooks.h"

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

		// The resolution scale, read from the text size the framework set (16 px text at 1080p), as the highlight
		// frame reads it. Parts are drawn at twice their 1080p size, so they stay sharp at 4K.
		float Unit()
		{
			return std::max(0.5f, ImGui::GetFontSize() / 16.0f);
		}

		// A part's corner on screen: uDrawCorner (or uCorner) at 1080p, times the scale, never more than half the rect.
		float ScreenCorner(const skin::ArtPart& a_part, const ImRect& a_bb, float a_extra = 1.0f)
		{
			const float base = a_part.drawCorner > 0.0f ? a_part.drawCorner : a_part.corner * 0.5f;
			return std::min({ base * Unit() * a_extra, a_bb.GetWidth() * 0.5f, a_bb.GetHeight() * 0.5f });
		}

		// Nine-slice: corners at a_dcs on screen, edges stretched (or repeated, a_tile), centre stretched - for a plate
		// the centre is the plate itself, so unlike the window frame it is drawn.
		void NineSlice(ImDrawList* a_dl, const skin::ArtImage& a_img, float a_cs, float a_dcs, const ImRect& a_bb, ImU32 a_col,
					   bool a_centre = true)
		{
			if (!a_img.srv || a_img.w <= 0.0f || a_img.h <= 0.0f || a_cs <= 0.0f || a_dcs <= 0.0f) { return; }
			const float W = a_img.w, H = a_img.h, cs = a_cs, dcs = a_dcs;
			const float u1 = cs / W, u2 = (W - cs) / W, v1 = cs / H, v2 = (H - cs) / H;
			const float x0 = a_bb.Min.x, x1 = x0 + dcs, x3 = a_bb.Max.x, x2 = x3 - dcs;
			const float y0 = a_bb.Min.y, y1 = y0 + dcs, y3 = a_bb.Max.y, y2 = y3 - dcs;
			if (x2 < x1 || y2 < y1) { return; }
			const auto tex = reinterpret_cast<ImTextureID>(a_img.srv);
			auto slice = [&](float ax, float ay, float bx, float by, float au, float av, float bu, float bv) {
				if (bx - ax <= 0.0f || by - ay <= 0.0f) { return; }
				a_dl->AddImage(tex, ImVec2(ax, ay), ImVec2(bx, by), ImVec2(au, av), ImVec2(bu, bv), a_col);
			};
			slice(x0, y0, x1, y1, 0.0f, 0.0f, u1, v1);
			slice(x2, y0, x3, y1, u2, 0.0f, 1.0f, v1);
			slice(x0, y2, x1, y3, 0.0f, v2, u1, 1.0f);
			slice(x2, y2, x3, y3, u2, v2, 1.0f, 1.0f);
			slice(x1, y0, x2, y1, u1, 0.0f, u2, v1);
			slice(x1, y2, x2, y3, u1, v2, u2, 1.0f);
			slice(x0, y1, x1, y2, 0.0f, v1, u1, v2);
			slice(x2, y1, x3, y2, u2, v1, 1.0f, v2);
			if (a_centre) { slice(x1, y1, x2, y2, u1, v1, u2, v2); }
		}

		// A plate (box, button, tick box, slider grab, scroll grab, tab): the shape tinted with the colour the control has
		// right now - so the theme's colours, the player's Colours picks and the hover / held states all still show - and
		// its edge layer, when it has one, in the theme's line colour.
		bool DrawPlate(ImDrawList* a_dl, ArtKind a_kind, const ImRect& a_bb, ImU32 a_col)
		{
			const skin::ArtPart* part = skin::Part(a_kind);
			if (!part || a_bb.GetWidth() < 2.0f || a_bb.GetHeight() < 2.0f) { return false; }
			const float dcs = ScreenCorner(*part, a_bb);
			NineSlice(a_dl, part->main, part->corner, dcs, a_bb, a_col);
			if (part->edge.srv) { NineSlice(a_dl, part->edge, part->corner, dcs, a_bb, ImGui::GetColorU32(ImGuiCol_Border)); }
			return true;
		}

		// RenderFrame is every widget's field and every button: told apart by the colour ImGui asked for.
		int FrameKind(ImU32 a_col)
		{
			for (const ImGuiCol c : { ImGuiCol_FrameBg, ImGuiCol_FrameBgHovered, ImGuiCol_FrameBgActive })
			{
				if (ImGui::GetColorU32(c) == a_col) { return static_cast<int>(ArtKind::kBox); }
			}
			for (const ImGuiCol c : { ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive })
			{
				if (ImGui::GetColorU32(c) == a_col) { return static_cast<int>(ArtKind::kButton); }
			}
			return -1;   // rows (Header*) and anything else keep ImGui's own look
		}

		// The section line: a strip drawn as end caps and a stretched middle, centred on the line ImGui would draw, its
		// height the part's own at 1080p. Tinted with the separator colour.
		bool DrawSection(ImDrawList* a_dl, const ImRect& a_bb, ImU32 a_col)
		{
			const skin::ArtPart* part = skin::Part(ArtKind::kSection);
			if (!part || a_bb.GetWidth() < 4.0f) { return false; }
			const float k = (part->drawCorner > 0.0f ? part->drawCorner / std::max(1.0f, part->corner) : 0.5f) * Unit();
			const float h = std::max(2.0f, std::round(part->main.h * k));
			float cap = part->corner * k;
			cap = std::min(cap, a_bb.GetWidth() * 0.5f);
			const float cy = std::round((a_bb.Min.y + a_bb.Max.y) * 0.5f);
			const float y0 = cy - h * 0.5f, y1 = y0 + h;
			const float x0 = a_bb.Min.x, x3 = a_bb.Max.x, x1 = x0 + cap, x2 = x3 - cap;
			const float u1 = part->corner / part->main.w, u2 = 1.0f - u1;
			const auto tex = reinterpret_cast<ImTextureID>(part->main.srv);
			a_dl->AddImage(tex, ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(0.0f, 0.0f), ImVec2(u1, 1.0f), a_col);
			if (x2 > x1) { a_dl->AddImage(tex, ImVec2(x1, y0), ImVec2(x2, y1), ImVec2(u1, 0.0f), ImVec2(u2, 1.0f), a_col); }
			a_dl->AddImage(tex, ImVec2(x2, y0), ImVec2(x3, y1), ImVec2(u2, 0.0f), ImVec2(1.0f, 1.0f), a_col);
			return true;
		}

		// An arrow: the part points right and is turned to the direction asked, filling the square ImGui would fill.
		bool DrawArrow(ImDrawList* a_dl, const ImRect& a_bb, ImU32 a_col, int a_dir)
		{
			const skin::ArtPart* part = skin::Part(ArtKind::kArrow);
			if (!part) { return false; }
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
			const std::uint32_t tint = theme::RoleColor(theme::kRoleArt);
			NineSlice(a_dl, part->main, part->corner, dcs, a_bb, tint ? static_cast<ImU32>(tint) : IM_COL32_WHITE, false);
		}

		bool Hook(ImDrawList* a_dl, int a_part, const ImRect& a_bb, ImU32 a_col, int a_arg)
		{
			if (!a_dl) { return false; }
			switch (a_part)
			{
			case ImGuiArtPart_Frame:
				{
					const int kind = FrameKind(a_col);
					return kind >= 0 && DrawPlate(a_dl, static_cast<ArtKind>(kind), a_bb, a_col);
				}
			case ImGuiArtPart_TickBox:    return DrawPlate(a_dl, ArtKind::kTickBox, a_bb, a_col);
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
			case ImGuiArtPart_ScrollTrack:
				{
					const skin::ArtPart* part = skin::Part(ArtKind::kScrollbar);
					if (a_arg != ImGuiAxis_Y || !part || !part->extra.srv) { return false; }
					const float cs = std::min(part->corner, std::min(part->extra.w, part->extra.h) * 0.5f - 1.0f);
					const float dcs = ScreenCorner(*part, a_bb);
					NineSlice(a_dl, part->extra, cs, dcs, a_bb, a_col);
					if (part->extraEdge.srv) { NineSlice(a_dl, part->extraEdge, cs, dcs, a_bb, ImGui::GetColorU32(ImGuiCol_Border)); }
					return true;
				}
			case ImGuiArtPart_Separator:  return DrawSection(a_dl, a_bb, a_col);
			case ImGuiArtPart_Tab:        return DrawPlate(a_dl, ArtKind::kTab, a_bb, a_col);
			case ImGuiArtPart_Arrow:      return DrawArrow(a_dl, a_bb, a_col, a_arg);
			case ImGuiArtPart_Popup:      DrawPopup(a_dl, a_bb); return false;
			default:                      return false;
			}
		}
	}

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
		ImGui::GetForegroundDrawList()->AddImage(reinterpret_cast<ImTextureID>(part->main.srv), at,
												 ImVec2(at.x + part->main.w * k, at.y + h));
		return true;
	}
}
