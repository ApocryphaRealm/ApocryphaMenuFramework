// Offline render of the on-screen keyboard under every shipped theme, before and after the 2.3.0 key-shape fix (the
// owner, 2026-10-23: "the on-screen keyboard didn't seem to match the themes, though it tried, because the highlight from
// a focused key did have a different shape, but the keyboard keys themselves didn't have a matching shape").
//   tools\keyboard_theme_render.bat [out dir]   -> <out>\<theme>.png per shipped theme: before on top, after below.
// Runs AMF's own ImGui build (1.90.8 with the overlay patches, the SE line's vcpkg tree): the theme's style through
// include\ThemeStyle.h (theme::Apply's function), the keys through include\KeyboardKeys.h (Keyboard.cpp's loop), each
// button drawn by include\ArtPlate.h's DrawPlatePart (the art hook's function) with the theme's shipped button part,
// and the draw lists rasterised in software. "Before" is the 2.3.0 keyboard as committed at 8579b25: the same loop with
// only the cursor's rest colour swapped, and the square outline. Each image shows the keyboard twice: the mouse over Y
// (hovered) and the mouse held on K (pressed); the D-pad cursor is on G in both.
// What it cannot show: the theme's hover frame round the key under the mouse (Renderer.cpp's highlight frame; the
// in-game run checks it), the game's HDR / gamma, and the player's own Colours picks (the theme's own colours only).
#include "../include/ArtPlate.h"
#include "../include/KeyboardKeys.h"
#include "../include/ThemeStyle.h"

#include <imgui.h>
#include <imgui_internal.h>

#define NOMINMAX
#include <wincodec.h>
#include <windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace fs = std::filesystem;

namespace
{
	// ---- images -------------------------------------------------------------------------------
	struct Image
	{
		int w = 0, h = 0;
		std::vector<unsigned char> rgba;
	};
	std::vector<Image> g_textures;   // index = ImTextureID (1 = the font atlas)
	IWICImagingFactory* g_wic = nullptr;

	bool LoadPng(const fs::path& a_path, Image& a_out)
	{
		IWICBitmapDecoder* dec = nullptr;
		if (FAILED(g_wic->CreateDecoderFromFilename(a_path.wstring().c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec))) { return false; }
		IWICBitmapFrameDecode* frame = nullptr;
		IWICFormatConverter* conv = nullptr;
		bool ok = SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(g_wic->CreateFormatConverter(&conv)) &&
				  SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));
		UINT w = 0, h = 0;
		if (ok) { ok = SUCCEEDED(conv->GetSize(&w, &h)); }
		if (ok)
		{
			a_out.w = static_cast<int>(w);
			a_out.h = static_cast<int>(h);
			a_out.rgba.resize(static_cast<size_t>(w) * h * 4);
			ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(a_out.rgba.size()), a_out.rgba.data()));
		}
		if (conv) { conv->Release(); }
		if (frame) { frame->Release(); }
		dec->Release();
		return ok;
	}

	bool SavePng(const fs::path& a_path, const Image& a_img)
	{
		std::vector<unsigned char> bgra(a_img.rgba);
		for (size_t i = 0; i < bgra.size(); i += 4) { std::swap(bgra[i], bgra[i + 2]); }
		IWICStream* stream = nullptr;
		IWICBitmapEncoder* enc = nullptr;
		IWICBitmapFrameEncode* frame = nullptr;
		bool ok = SUCCEEDED(g_wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(a_path.wstring().c_str(), GENERIC_WRITE)) &&
				  SUCCEEDED(g_wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) && SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
				  SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
				  SUCCEEDED(frame->SetSize(static_cast<UINT>(a_img.w), static_cast<UINT>(a_img.h)));
		WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
		if (ok) { ok = SUCCEEDED(frame->SetPixelFormat(&fmt)) && fmt == GUID_WICPixelFormat32bppBGRA; }
		if (ok) { ok = SUCCEEDED(frame->WritePixels(static_cast<UINT>(a_img.h), static_cast<UINT>(a_img.w) * 4, static_cast<UINT>(bgra.size()), bgra.data())); }
		if (ok) { ok = SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit()); }
		if (frame) { frame->Release(); }
		if (enc) { enc->Release(); }
		if (stream) { stream->Release(); }
		return ok;
	}

	// ---- the software rasteriser ---------------------------------------------------------------
	void Sample(const Image& a_t, float a_u, float a_v, float a_out[4])
	{
		const float x = std::clamp(a_u * a_t.w - 0.5f, 0.0f, static_cast<float>(a_t.w - 1));
		const float y = std::clamp(a_v * a_t.h - 0.5f, 0.0f, static_cast<float>(a_t.h - 1));
		const int x0 = static_cast<int>(x), y0 = static_cast<int>(y);
		const int x1 = std::min(x0 + 1, a_t.w - 1), y1 = std::min(y0 + 1, a_t.h - 1);
		const float fx = x - x0, fy = y - y0;
		for (int c = 0; c < 4; ++c)
		{
			auto p = [&](int px, int py) { return a_t.rgba[(static_cast<size_t>(py) * a_t.w + px) * 4 + c] / 255.0f; };
			a_out[c] = (p(x0, y0) * (1 - fx) + p(x1, y0) * fx) * (1 - fy) + (p(x0, y1) * (1 - fx) + p(x1, y1) * fx) * fy;
		}
	}

	void Raster(Image& a_dst, const ImDrawData* a_dd)
	{
		for (int n = 0; n < a_dd->CmdListsCount; ++n)
		{
			const ImDrawList* dl = a_dd->CmdLists[n];
			for (const ImDrawCmd& cmd : dl->CmdBuffer)
			{
				if (cmd.UserCallback) { continue; }
				const size_t tex = static_cast<size_t>(reinterpret_cast<intptr_t>(cmd.GetTexID()));
				if (tex >= g_textures.size() || g_textures[tex].w == 0) { continue; }
				const Image& t = g_textures[tex];
				const int cx0 = std::max(0, static_cast<int>(cmd.ClipRect.x)), cy0 = std::max(0, static_cast<int>(cmd.ClipRect.y));
				const int cx1 = std::min(a_dst.w, static_cast<int>(std::ceil(cmd.ClipRect.z))), cy1 = std::min(a_dst.h, static_cast<int>(std::ceil(cmd.ClipRect.w)));
				for (unsigned int i = 0; i + 2 < cmd.ElemCount; i += 3)
				{
					const ImDrawVert& a = dl->VtxBuffer[cmd.VtxOffset + dl->IdxBuffer[cmd.IdxOffset + i]];
					const ImDrawVert& b = dl->VtxBuffer[cmd.VtxOffset + dl->IdxBuffer[cmd.IdxOffset + i + 1]];
					const ImDrawVert& c = dl->VtxBuffer[cmd.VtxOffset + dl->IdxBuffer[cmd.IdxOffset + i + 2]];
					const float area = (b.pos.x - a.pos.x) * (c.pos.y - a.pos.y) - (b.pos.y - a.pos.y) * (c.pos.x - a.pos.x);
					if (std::fabs(area) < 1e-6f) { continue; }
					const int x0 = std::max(cx0, static_cast<int>(std::floor(std::min({ a.pos.x, b.pos.x, c.pos.x }))));
					const int x1 = std::min(cx1, static_cast<int>(std::ceil(std::max({ a.pos.x, b.pos.x, c.pos.x }))));
					const int y0 = std::max(cy0, static_cast<int>(std::floor(std::min({ a.pos.y, b.pos.y, c.pos.y }))));
					const int y1 = std::min(cy1, static_cast<int>(std::ceil(std::max({ a.pos.y, b.pos.y, c.pos.y }))));
					auto col = [](ImU32 v, int k) { return ((v >> (8 * k)) & 0xFF) / 255.0f; };
					for (int y = y0; y < y1; ++y)
					{
						for (int x = x0; x < x1; ++x)
						{
							// a sample point a hair off the pixel centre, so a quad's shared diagonal belongs to one triangle, not both
							const float px = x + 0.5f + 1.3e-4f, py = y + 0.5f + 2.7e-4f;
							const float w0 = ((b.pos.x - px) * (c.pos.y - py) - (b.pos.y - py) * (c.pos.x - px)) / area;
							const float w1 = ((c.pos.x - px) * (a.pos.y - py) - (c.pos.y - py) * (a.pos.x - px)) / area;
							const float w2 = 1.0f - w0 - w1;
							if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) { continue; }
							float s[4];
							Sample(t, a.uv.x * w0 + b.uv.x * w1 + c.uv.x * w2, a.uv.y * w0 + b.uv.y * w1 + c.uv.y * w2, s);
							float src[4];
							for (int k = 0; k < 4; ++k) { src[k] = s[k] * (col(a.col, k) * w0 + col(b.col, k) * w1 + col(c.col, k) * w2); }
							unsigned char* d = &a_dst.rgba[(static_cast<size_t>(y) * a_dst.w + x) * 4];
							for (int k = 0; k < 3; ++k) { d[k] = static_cast<unsigned char>(std::clamp(src[k] * src[3] * 255.0f + d[k] * (1.0f - src[3]), 0.0f, 255.0f)); }
							d[3] = 255;
						}
					}
				}
			}
		}
	}

	// ---- the theme ---------------------------------------------------------------------------
	std::map<std::string, std::string> ReadIni(const fs::path& a_path)
	{
		std::map<std::string, std::string> kv;
		std::ifstream f(a_path);
		std::string line;
		while (std::getline(f, line))
		{
			if (line.empty() || line[0] == ';' || line[0] == '[') { continue; }
			const auto eq = line.find('=');
			if (eq == std::string::npos) { continue; }
			std::string v = line.substr(eq + 1);
			while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) { v.pop_back(); }
			kv[line.substr(0, eq)] = v;
		}
		return kv;
	}

	bool ParseColor(std::string a_hex, ImVec4& a_out)
	{
		if (!a_hex.empty() && a_hex[0] == '#') { a_hex.erase(0, 1); }
		if (a_hex.size() != 6 && a_hex.size() != 8) { return false; }
		unsigned int v = 0;
		if (std::from_chars(a_hex.data(), a_hex.data() + a_hex.size(), v, 16).ec != std::errc{}) { return false; }
		unsigned int a = 0xFF;
		if (a_hex.size() == 8) { a = v & 0xFF; v >>= 8; }
		a_out = ImVec4(((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, a / 255.0f);
		return true;
	}

	// theme::Apply's outline contrast (Theme.cpp), so the outline colour is the one the game uses
	float Lum(const ImVec4& c)
	{
		auto lin = [](float v) { return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
		return 0.2126f * lin(c.x) + 0.7152f * lin(c.y) + 0.0722f * lin(c.z);
	}
	ImVec4 Over(const ImVec4& t, const ImVec4& u)
	{
		const float a = std::clamp(t.w, 0.0f, 1.0f);
		return ImVec4(t.x * a + u.x * (1 - a), t.y * a + u.y * (1 - a), t.z * a + u.z * (1 - a), 1.0f);
	}
	float OutlineContrast(const ImVec4& line, const ImVec4& face, const ImVec4& panel)
	{
		const ImVec4 p = Over(panel, ImVec4(0, 0, 0, 1));
		const ImVec4 f = Over(face, p);
		const float l1 = Lum(Over(line, f)), l2 = Lum(f);
		return (std::max(l1, l2) + 0.05f) / (std::min(l1, l2) + 0.05f);
	}

	struct Theme
	{
		std::string id, name;
		skin::ArtPart button;
		bool hasButton = false;
	};

	// The colours theme::Apply sets for what the keyboard draws (Theme.cpp BuildLook, the theme's own colours, no picks).
	void ApplyTheme(const std::map<std::string, std::string>& a_kv, float a_uiScale)
	{
		auto get = [&](const char* k) { const auto it = a_kv.find(k); return it == a_kv.end() ? std::string() : it->second; };
		ImVec4 bg(0, 0, 0, 1), frame(1, 1, 1, 1);
		ParseColor(get("sBackground"), bg);
		ParseColor(get("sFrame"), frame);
		ImVec4 border = frame, text = frame, textDim = frame;
		ParseColor(get("sBorder"), border);
		ParseColor(get("sText"), text);
		ParseColor(get("sTextDim"), textDim);
		textDim.w = 1.0f;
		float thickness = 1.0f;
		if (!get("fBorderThickness").empty()) { thickness = std::stof(get("fBorderThickness")); }

		ImGuiStyle& st = ImGui::GetStyle();
		themestyle::Build(st, thickness, 26.0f + 8.0f, a_uiScale);
		auto tint = [](ImVec4 v, float a) { v.w = a; return v; };
		ImVec4* c = st.Colors;
		c[ImGuiCol_WindowBg] = bg;
		c[ImGuiCol_Text] = text;
		c[ImGuiCol_TextDisabled] = textDim;
		c[ImGuiCol_Border] = border;
		c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
		c[ImGuiCol_FrameBg] = bg;
		c[ImGuiCol_FrameBgHovered] = tint(border, 0.14f);
		c[ImGuiCol_FrameBgActive] = tint(border, 0.28f);
		c[ImGuiCol_Button] = bg;
		c[ImGuiCol_ButtonHovered] = tint(border, 0.14f);
		c[ImGuiCol_ButtonActive] = tint(border, 0.28f);
		c[ImGuiCol_NavHighlight] = ImVec4(0.24f, 0.62f, 1.00f, 1.00f);
		const float worst = std::min({ OutlineContrast(border, bg, bg), OutlineContrast(border, c[ImGuiCol_FrameBg], bg), OutlineContrast(border, c[ImGuiCol_Button], bg) });
		if (worst < 4.5f) { c[ImGuiCol_Border] = ImVec4(text.x, text.y, text.z, 1.0f); }
		st.Alpha = 1.0f;
	}

	// ---- the art hook, as ArtHooks.cpp's Frame branch -------------------------------------------
	const Theme* g_theme = nullptr;
	bool g_shaped = false;
	std::vector<ImRect> g_keyRects;   // every button frame drawn this frame, in order (the keys)

	bool Hook(ImDrawList* a_dl, int a_part, const ImRect& a_bb, ImU32 a_col, int a_arg)
	{
		if (a_part != ImGuiArtPart_Frame || !g_theme || !g_theme->hasButton) { return false; }
		bool button = false;
		for (const ImGuiCol c : { ImGuiCol_Button, ImGuiCol_ButtonHovered, ImGuiCol_ButtonActive })
		{
			if (ImGui::GetColorU32(c) == a_col) { button = true; }
		}
		if (!button) { return false; }
		g_keyRects.push_back(a_bb);
		const ImGuiStyle& st = ImGui::GetStyle();
		const ImVec4 panel = st.Colors[ImGuiCol_WindowBg];
		return artplate::DrawPlatePart(a_dl, g_theme->button, a_bb, a_col, a_arg != 0, g_shaped, ImGui::GetColorU32(ImGuiCol_Border),
									   st.FrameBorderSize, st.FrameRounding, ImGui::ColorConvertFloat4ToU32(ImVec4(panel.x, panel.y, panel.z, st.Alpha)),
									   ImGui::GetColorU32(ImGuiCol_Border));
	}

	// ---- the keyboard, before (8579b25) and after ----------------------------------------------
	void OldRows(int a_row, int a_col, float a_k)
	{
		// Keyboard.cpp's loop as committed at 8579b25: only the cursor's rest colour swapped
		const float gap = ImGui::GetStyle().ItemSpacing.x;
		const ImVec4 hi = ImGui::GetStyleColorVec4(ImGuiCol_NavHighlight);
		for (int r = 0; r < keyboardkeys::kRowCount; ++r)
		{
			const int len = keyboardkeys::RowLength(r);
			const float rowWidth = (r == keyboardkeys::kActionRow) ? (a_k * 1.6F + a_k * 4.0F + a_k * 1.8F + a_k * 1.8F + a_k * 1.6F + 4.0F * gap) : (len * a_k + (len - 1) * gap);
			ImGui::SetCursorPosX((ImGui::GetWindowSize().x - rowWidth) * 0.5F);
			for (int c = 0; c < len; ++c)
			{
				const bool cursor = (r == a_row && c == a_col);
				if (cursor) { ImGui::PushStyleColor(ImGuiCol_Button, hi); }
				ImGui::PushID(r * 32 + c);
				if (r != keyboardkeys::kActionRow)
				{
					char label[2] = { keyboardkeys::kRows[r][c], '\0' };
					ImGui::Button(label, ImVec2(a_k, a_k));
				}
				else
				{
					static const char* names[] = { "Shift", "Space", "Back", "Clear", "Done" };
					static const float widths[] = { 1.6F, 4.0F, 1.8F, 1.8F, 1.6F };
					ImGui::Button(names[c], ImVec2(a_k * widths[c], a_k));
				}
				ImGui::PopID();
				if (cursor) { ImGui::PopStyleColor(); }
				if (c + 1 < len) { ImGui::SameLine(); }
			}
		}
	}

	// One frame of the keyboard window (Keyboard.cpp Draw's window and hint, then the rows).
	ImRect DrawKeyboard(bool a_after)
	{
		const ImGuiIO& io = ImGui::GetIO();
		const float k = ImGui::GetFrameHeight() * 1.6F;
		ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5F, io.DisplaySize.y - k * 0.5F), ImGuiCond_Always, ImVec2(0.5F, 1.0F));
		ImGui::SetNextWindowSize(ImVec2(keyboardkeys::WindowWidth(k), 0.0F), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.92F);
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
									   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
									   ImGuiWindowFlags_NoSavedSettings;
		ImGui::Begin("###amf-keyboard", nullptr, flags);
		ImGui::TextDisabled("%s", "D-pad moves, A types, B goes back to the box, X shift, Y backspace");
		g_keyRects.clear();
		if (a_after)
		{
			g_shaped = true;
			int pr = -1, pc = -1;
			keyboardkeys::DrawRows({ "Shift", "Space", "Back", "Clear", "Done" }, 2, 4, false, k, pr, pc);
			g_shaped = false;
		}
		else { OldRows(2, 4, k); }
		const ImRect r = ImGui::GetCurrentWindow()->Rect();
		ImGui::End();
		return r;
	}

	// Renders the keyboard with the mouse at a key (by index in row order) held or not; returns the window's pixels.
	Image Shot(bool a_after, int a_key, bool a_held, const ImVec2 a_display)
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = a_display;
		io.DeltaTime = 1.0f / 60.0f;
		ImRect win;
		ImVec2 target(-1, -1);
		for (int f = 0; f < 8; ++f)
		{
			io.AddMousePosEvent(target.x, target.y);
			io.AddMouseButtonEvent(0, a_held && f >= 4);
			ImGui::NewFrame();
			win = DrawKeyboard(a_after);
			ImGui::Render();
			if (a_key >= 0 && a_key < static_cast<int>(g_keyRects.size())) { target = g_keyRects[a_key].GetCenter(); }
		}
		io.AddMouseButtonEvent(0, false);
		Image canvas;
		canvas.w = static_cast<int>(a_display.x);
		canvas.h = static_cast<int>(a_display.y);
		canvas.rgba.assign(static_cast<size_t>(canvas.w) * canvas.h * 4, 0);
		for (size_t i = 0; i < canvas.rgba.size(); i += 4)
		{
			// a neutral mid-tone backdrop, as a scene behind the 92% panel
			canvas.rgba[i] = 70; canvas.rgba[i + 1] = 78; canvas.rgba[i + 2] = 84; canvas.rgba[i + 3] = 255;
		}
		Raster(canvas, ImGui::GetDrawData());
		// crop to the window and a margin
		const int m = 12;
		const int x0 = std::max(0, static_cast<int>(win.Min.x) - m), y0 = std::max(0, static_cast<int>(win.Min.y) - m);
		const int x1 = std::min(canvas.w, static_cast<int>(win.Max.x) + m), y1 = std::min(canvas.h, static_cast<int>(win.Max.y) + m);
		Image out;
		out.w = x1 - x0;
		out.h = y1 - y0;
		out.rgba.resize(static_cast<size_t>(out.w) * out.h * 4);
		for (int y = 0; y < out.h; ++y)
		{
			std::copy_n(&canvas.rgba[(static_cast<size_t>(y + y0) * canvas.w + x0) * 4], static_cast<size_t>(out.w) * 4, &out.rgba[static_cast<size_t>(y) * out.w * 4]);
		}
		return out;
	}

	void Blit(Image& a_dst, const Image& a_src, int a_x, int a_y)
	{
		for (int y = 0; y < a_src.h && y + a_y < a_dst.h; ++y)
		{
			for (int x = 0; x < a_src.w && x + a_x < a_dst.w; ++x)
			{
				std::copy_n(&a_src.rgba[(static_cast<size_t>(y) * a_src.w + x) * 4], 4, &a_dst.rgba[(static_cast<size_t>(y + a_y) * a_dst.w + x + a_x) * 4]);
			}
		}
	}

	Image Blank(int a_w, int a_h)
	{
		Image i;
		i.w = a_w;
		i.h = a_h;
		i.rgba.assign(static_cast<size_t>(a_w) * a_h * 4, 255);
		for (size_t p = 0; p < i.rgba.size(); p += 4) { i.rgba[p] = 32; i.rgba[p + 1] = 32; i.rgba[p + 2] = 32; }
		return i;
	}
}

int main(int argc, char** argv)
{
	const fs::path plugin = argc > 1 ? fs::path(argv[1]) : fs::path("dist/SKSE/Plugins/ApocryphaMenuFramework");
	const fs::path out = argc > 2 ? fs::path(argv[2]) : fs::path("keyboard-renders");
	fs::create_directories(out);
	if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) ||
		FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_wic))))
	{
		std::cerr << "WIC unavailable\n";
		return 1;
	}

	const float uiScale = 1.25f;   // a 1350p display: keys big enough to see the shape at 100%
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	{
		ImFontConfig cfg;
		const char* face = "C:/Windows/Fonts/segoeui.ttf";   // AMF's first default face (Renderer.cpp)
		if (fs::exists(face)) { io.Fonts->AddFontFromFileTTF(face, 16.0f * uiScale, &cfg); }
		else { cfg.SizePixels = 16.0f * uiScale; io.Fonts->AddFontDefault(&cfg); }
		unsigned char* px = nullptr;
		int w = 0, h = 0;
		io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
		g_textures.resize(2);
		g_textures[1].w = w;
		g_textures[1].h = h;
		g_textures[1].rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
		io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(1)));
	}
	GImGuiArtHook = &Hook;

	std::vector<fs::path> themes;
	for (const auto& e : fs::directory_iterator(plugin / "themes"))
	{
		if (e.path().extension() == ".ini") { themes.push_back(e.path()); }
	}
	std::sort(themes.begin(), themes.end());
	std::vector<std::pair<std::string, Image>> sheets;
	const ImVec2 display(1920.0f * uiScale, 1080.0f * uiScale);
	const int kHoverKey = 15, kPressKey = 27;   // Y (row 2, 6th) and K (row 3, 8th); the cursor sits on G
	for (const fs::path& tp : themes)
	{
		const auto kv = ReadIni(tp);
		Theme th;
		th.id = tp.stem().string();
		th.name = kv.count("sName") ? kv.at("sName") : th.id;
		const std::string art = kv.count("sButtonArt") ? kv.at("sButtonArt") : std::string();
		if (!art.empty())
		{
			Image img;
			const fs::path png = plugin / "assets" / "buttons" / (art + ".png");
			if (LoadPng(png, img))
			{
				const auto pk = ReadIni(plugin / "assets" / "buttons" / (art + ".ini"));
				g_textures.push_back(std::move(img));
				const size_t id = g_textures.size() - 1;
				th.button.main.srv = reinterpret_cast<void*>(static_cast<intptr_t>(id));
				th.button.main.w = static_cast<float>(g_textures[id].w);
				th.button.main.h = static_cast<float>(g_textures[id].h);
				th.button.corner = pk.count("uCorner") ? std::stof(pk.at("uCorner")) : std::min(th.button.main.w, th.button.main.h) * 0.25f;
				th.button.drawCorner = pk.count("uDrawCorner") ? std::stof(pk.at("uDrawCorner")) : 0.0f;
				th.button.threeSlice = pk.count("bThreeSlice") && pk.at("bThreeSlice") == "1";
				Image edge;
				if (LoadPng(plugin / "assets" / "buttons" / (art + "-edge.png"), edge))
				{
					g_textures.push_back(std::move(edge));
					const size_t eid = g_textures.size() - 1;
					th.button.edge.srv = reinterpret_cast<void*>(static_cast<intptr_t>(eid));
					th.button.edge.w = static_cast<float>(g_textures[eid].w);
					th.button.edge.h = static_cast<float>(g_textures[eid].h);
				}
				th.hasButton = true;
			}
		}
		g_theme = &th;
		ApplyTheme(kv, uiScale);
		const Image bh = Shot(false, kHoverKey, false, display), bp = Shot(false, kPressKey, true, display);
		const Image ah = Shot(true, kHoverKey, false, display), ap = Shot(true, kPressKey, true, display);
		const int gap = 16;
		Image sheet = Blank(bh.w * 2 + gap * 3, bh.h * 2 + gap * 3);
		Blit(sheet, bh, gap, gap);
		Blit(sheet, bp, gap * 2 + bh.w, gap);
		Blit(sheet, ah, gap, gap * 2 + bh.h);
		Blit(sheet, ap, gap * 2 + bh.w, gap * 2 + bh.h);
		const fs::path file = out / (th.id + ".png");
		const bool saved = SavePng(file, sheet);
		std::cout << (saved ? "wrote " : "FAILED ") << file.string() << "  (" << th.name << ", button art \"" << art << "\")\n";
		sheets.emplace_back(th.id, std::move(sheet));
		g_theme = nullptr;
	}
	ImGui::DestroyContext();
	g_wic->Release();
	return 0;
}
