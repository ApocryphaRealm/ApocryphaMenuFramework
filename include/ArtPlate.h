#pragma once

// ============================================================================================
// HOW A CONTROL'S ART PART IS DRAWN (2.3.0, moved out of ArtHooks.cpp). Only ImGui and the part's own fields here, so the
// offline render (tools\keyboard_theme_render.cpp) draws a plate with the very function the art hook calls in game.
//
// THE KEY SHAPE (2.3.0, the owner, 2026-10-23: "the on-screen keyboard didn't seem to match the themes, though it tried,
// because the highlight from a focused key did have a different shape, but the keyboard keys themselves didn't have a
// matching shape"). Every shipped button part is a white shape - rounded, clipped, six- or eight-sided - tinted with the
// face colour, and a face sits in the panel colour at rest. So at rest the shape was invisible and the only thing seen
// was the outline, which was ImGui's SQUARE border line (the 2.1.9 outline fix); the keyboard's cursor key, whose face
// is the bright nav blue, was the only key whose real shape showed. A shaped outline draws the outline FROM the part:
// the shape in the outline colour, then the same shape a line's width inside it, in the panel colour and then the face
// colour - so the line, the face and every state of it (rest, hovered, held, the cursor's blue) share the theme's shape.
// ============================================================================================

#include "Skin.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>

namespace artplate
{
	// The resolution scale, read from the text size the framework set (16 px text at 1080p), as the highlight
	// frame reads it. Parts are drawn at twice their 1080p size, so they stay sharp at 4K.
	inline float Unit()
	{
		return std::max(0.5f, ImGui::GetFontSize() / 16.0f);
	}

	// A part's corner on screen: uDrawCorner (or uCorner) at 1080p, times the scale, never more than half the rect.
	inline float ScreenCorner(const skin::ArtPart& a_part, const ImRect& a_bb, float a_extra = 1.0f)
	{
		const float base = a_part.drawCorner > 0.0f ? a_part.drawCorner : a_part.corner * 0.5f;
		return std::min({ base * Unit() * a_extra, a_bb.GetWidth() * 0.5f, a_bb.GetHeight() * 0.5f });
	}

	// Nine-slice: corners at a_dcs on screen, edges stretched (or repeated, a_tile), centre stretched - for a plate
	// the centre is the plate itself, so unlike the window frame it is drawn.
	inline void NineSlice(ImDrawList* a_dl, const skin::ArtImage& a_img, float a_cs, float a_dcs, const ImRect& a_bb, ImU32 a_col,
				   bool a_centre = true, bool a_tile = false)
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
		if (!a_tile)
		{
			slice(x1, y0, x2, y1, u1, 0.0f, u2, v1);
			slice(x1, y2, x2, y3, u1, v2, u2, 1.0f);
			slice(x0, y1, x1, y2, 0.0f, v1, u1, v2);
			slice(x2, y1, x3, y2, u2, v1, 1.0f, v2);
		}
		else
		{
			// 2.1.6: bTileEdges on a part - the sides repeat at the art's own scale instead of stretching (a rope's twist, a
			// stitch); the last repeat is cut to fit
			const float k = dcs / cs;
			const float stepX = std::max(1.0f, (W - 2.0f * cs) * k), stepY = std::max(1.0f, (H - 2.0f * cs) * k);
			for (float x = x1; x < x2 - 0.5f; x += stepX)
			{
				const float e = std::min(x + stepX, x2);
				const float ue = u1 + (u2 - u1) * ((e - x) / stepX);
				slice(x, y0, e, y1, u1, 0.0f, ue, v1);
				slice(x, y2, e, y3, u1, v2, ue, 1.0f);
			}
			for (float y = y1; y < y2 - 0.5f; y += stepY)
			{
				const float e = std::min(y + stepY, y2);
				const float ve = v1 + (v2 - v1) * ((e - y) / stepY);
				slice(x0, y, x1, e, 0.0f, v1, u1, ve);
				slice(x2, y, x3, e, u2, v1, 1.0f, ve);
			}
		}
		if (a_centre) { slice(x1, y1, x2, y2, u1, v1, u2, v2); }
	}

	// A plate (box, button, tick box, slider grab, scroll grab, tab): the shape tinted with the colour the control has
	// right now - so the theme's colours, the player's Colours picks and the hover / held states all still show - and
	// its edge layer, when it has one, in the theme's line colour.
	// 2.1.6 (the owner: the Oblivion slider and scroll grabs "should just be a single line of the frame art with its ending
	// circles art at either end of it"): a cap the art's width tall at each end of the long side, the middle between them.
	inline void ThreeSlice(ImDrawList* a_dl, const skin::ArtImage& a_img, const ImRect& a_bb, ImU32 a_col, bool a_tile)
	{
		if (!a_img.srv || a_img.w <= 0.0f || a_img.h <= a_img.w) { return; }
		const auto tex = reinterpret_cast<ImTextureID>(a_img.srv);
		const bool vertical = a_bb.GetHeight() >= a_bb.GetWidth();
		const float across = vertical ? a_bb.GetWidth() : a_bb.GetHeight();
		const float along = vertical ? a_bb.GetHeight() : a_bb.GetWidth();
		const float cap = std::min(across, along * 0.5f);
		const float v1 = a_img.w / a_img.h, v2 = 1.0f - v1;   // the caps are the art's width tall
		// the art is drawn standing up; a horizontal grab lies it on its side
		auto quad = [&](float s0, float s1, float t0, float t1) {
			if (s1 - s0 <= 0.0f) { return; }
			if (vertical)
			{
				a_dl->AddImage(tex, ImVec2(a_bb.Min.x, a_bb.Min.y + s0), ImVec2(a_bb.Max.x, a_bb.Min.y + s1), ImVec2(0.0f, t0), ImVec2(1.0f, t1), a_col);
			}
			else
			{
				const ImVec2 a(a_bb.Min.x + s0, a_bb.Min.y), b(a_bb.Min.x + s1, a_bb.Min.y), c(a_bb.Min.x + s1, a_bb.Max.y), d(a_bb.Min.x + s0, a_bb.Max.y);
				a_dl->AddImageQuad(tex, a, b, c, d, ImVec2(1.0f, t0), ImVec2(1.0f, t1), ImVec2(0.0f, t1), ImVec2(0.0f, t0), a_col);
			}
		};
		quad(0.0f, cap, 0.0f, v1);
		quad(along - cap, along, v2, 1.0f);
		const float midLen = along - 2.0f * cap;
		if (midLen <= 0.0f) { return; }
		if (!a_tile) { quad(cap, along - cap, v1, v2); return; }
		const float step = std::max(1.0f, (a_img.h - 2.0f * a_img.w) * (across / a_img.w));
		for (float s = cap; s < along - cap - 0.5f; s += step)
		{
			const float e = std::min(s + step, along - cap);
			quad(s, e, v1, v1 + (v2 - v1) * ((e - s) / step));
		}
	}
	// One plate (box, button, tick box, slider grab, scroll grab, tab), the part's main shape tinted with the colour the
	// control has right now, then its edge layer (a_edgeCol) when it has one.
	// a_outline: RenderFrame asked for a border. With no edge layer the outline is either ImGui's own line round the rect
	// (a_shaped false: the 2.1.9 basic outline every page uses) or the part's own shape in the outline colour (a_shaped
	// true: the on-screen keyboard's keys, 2.3.0). a_panel: the colour inside a shaped outline, under the face - the panel
	// colour, solid, so a see-through hover wash shows the panel and not the outline colour through it.
	inline bool DrawPlatePart(ImDrawList* a_dl, const skin::ArtPart& a_part, const ImRect& a_bb, ImU32 a_col, bool a_outline,
							  bool a_shaped, ImU32 a_lineCol, float a_lineWidth, float a_rounding, ImU32 a_panel, ImU32 a_edgeCol)
	{
		if (a_bb.GetWidth() < 2.0f || a_bb.GetHeight() < 2.0f) { return false; }
		const float dcs = ScreenCorner(a_part, a_bb);
		auto shape = [&](const ImRect& r, float c, ImU32 col) {
			if (!a_part.threeSlice) { NineSlice(a_dl, a_part.main, a_part.corner, c, r, col); }
			else { ThreeSlice(a_dl, a_part.main, r, col, false); }
		};
		const bool shapedOutline = a_outline && a_shaped && !a_part.edge.srv;
		if (shapedOutline)
		{
			// a whole number of pixels, so the line is as even as the square one it replaces
			const float t = std::max(1.0f, std::round(a_lineWidth));
			const ImRect inner(a_bb.Min.x + t, a_bb.Min.y + t, a_bb.Max.x - t, a_bb.Max.y - t);
			shape(a_bb, dcs, a_lineCol);
			if (inner.GetWidth() >= 2.0f && inner.GetHeight() >= 2.0f)
			{
				const float ci = std::max(1.0f, dcs - t * 0.5f);
				shape(inner, ci, a_panel);
				shape(inner, ci, a_col);
			}
		}
		else
		{
			shape(a_bb, dcs, a_col);
			if (a_outline && !a_part.edge.srv)
			{
				a_dl->AddRect(a_bb.Min, a_bb.Max, a_lineCol, a_rounding, 0, std::max(1.0f, a_lineWidth));
			}
		}
		if (a_part.edge.srv)
		{
			if (a_part.threeSlice) { ThreeSlice(a_dl, a_part.edge, a_bb, a_edgeCol, a_part.tile); }
			else { NineSlice(a_dl, a_part.edge, a_part.corner, dcs, a_bb, a_edgeCol, true, a_part.tile); }
		}
		return true;
	}
}
