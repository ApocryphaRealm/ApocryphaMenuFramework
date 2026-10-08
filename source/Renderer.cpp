#include "Renderer.h"
#include "Keyboard.h"
#include "McmLoader.h"
#include "RememberedSettings.h"
#include "McmScripts.h"

#include "ConsumerSurface.h"

#include "Compat.h"
#include "Curtain.h"
#include "AmfIcons.h"
#include "ArtHooks.h"
#include "FlickHost.h"
#include "PrismaRedux.h"
#include "HelpBar.h"
#include "McmStyle.h"
#include "Input.h"
#include "Offsets.h"
#include "Persistence.h"
#include "KnotworkBorder.h"
#include "Skin.h"
#include "Bindings.h"
#include "Personalization.h"
#include "Registry.h"
#include "Settings.h"
#include "Strings.h"
#include "SystemRow.h"
#include "Theme.h"
#include "Watchdog.h"
#include "utils/ToggleSwitch.h"
#include "utils/Logger.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <mutex>
#include <unordered_map>

#include <imgui.h>
#include <imgui_internal.h>
#include "PreciseSlider.h"
#include <vector>
// The vcpkg imgui port installs the binding headers FLAT at the include root, not under
// backends/ as in the upstream repo layout.
#include <d3d11.h>
#include <directxtk/ScreenGrab.h>
#include <wincodec.h>
#include <condition_variable>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <atomic>
#include <algorithm>
#include <cctype>
#include <string>

namespace renderer
{
	// Search-box mirrors (driving tool). Written on the render thread, read on the listener thread.
	std::atomic<float> g_searchRect[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	std::atomic<bool> g_searchActive{ false };
	std::atomic<int> g_searchLen{ 0 };
	std::mutex g_searchTextLock;
	std::string g_searchText;
	std::atomic<bool> g_wantTextInput{ false };
	std::atomic<bool> g_backspaceDown{ false };
	std::atomic<unsigned int> g_activeIdMirror{ 0 };
	std::atomic<bool> g_modCtrl{ false }, g_modShift{ false }, g_modAlt{ false };

	using strings::TR;

	namespace
	{
		std::atomic<bool> g_d3dReady{ false };

		// In-process capture state. The swapchain/context are owned by the game; never released here.
		IDXGISwapChain* g_swapChain = nullptr;
		ID3D11DeviceContext* g_captureContext = nullptr;
		std::mutex g_captureLock;
		std::condition_variable g_captureCv;
		std::wstring g_capturePath;      // non-empty = a capture is pending
		bool g_captureDone = false;
		std::string g_captureError;
		std::atomic<bool> g_windowVisible{ false };

		// A MOD'S OWN WINDOW HAS THE INPUT (2.0.4). True while any window a mod registered through AddWindow is
		// open, asked to block the player's input (BlockUserInput) AND drew a window last frame that takes the
		// mouse (consumer::AnyWindowOwnsInput says why the flags alone are not enough) - sampled once per frame on the render thread
		// and published here for the input thread, which then feeds ImGui and holds the game's input exactly as it
		// does for our own menu. Until 2.0.4 only our own menu did that, so such a window drew every frame with no
		// cursor and no input at all unless the framework menu happened to be open too (mmmizuhara, 2026-10-03:
		// RaceMenu Atelier "isn't working ... It works fine when I switch back to SKSE Menu Framework"). Published by
		// the render thread rather than asked of the registry by the input thread, so the two sides agree: the input
		// thread only queues events for ImGui once the renderer has already taken the rising edge and cleared the queue.
		std::atomic<bool> g_consumerInput{ false };

		// PAUSE WHILE OPEN (1.9.7, [Menu] bPauseGame). The window is an overlay, not a game menu, so it pauses the
		// game the way a pausing menu does: by holding one count on UI::numPausesGame. The render thread notices the
		// wanted state change; the count itself is only ever touched on the main thread, once up and once down, and
		// g_pauseHeld says whether this framework is holding one - so a close, a toggle flip or a save/load between
		// them can never leave the game paused, or take a count some other menu holds.
		bool g_pauseHeld = false;   // main thread only
		// NEVER TAKE ANOTHER MENU'S PAUSE COUNT (2.1.5, tested 2026-10-08): setting numPausesGame to 0 under the open journal, so
		// a converted page's scripts could run, froze the game on the spot - no frame after it, the owner's cursor gone, the
		// process killed. Only our own count is ever let go (a_lift).

		// a_wantOwn: our own pause (the setting, our window open on its own - never on top of the journal's, which already
		// pauses). a_lift: a page's scripts are waiting, so our own pause is let go for now.
		void SyncGamePause(bool a_wantOwn, bool a_lift)
		{
			static bool lastOwn = false, lastLift = false;   // render thread only
			if (a_wantOwn == lastOwn && a_lift == lastLift)
			{
				return;
			}
			lastOwn = a_wantOwn;
			lastLift = a_lift;
			auto* tasks = SKSE::GetTaskInterface();
			if (!tasks)
			{
				logger::warn("pause: no SKSE task interface - the game is not {} with the menu", a_wantOwn ? "paused" : "unpaused");
				return;
			}
			tasks->AddTask([a_wantOwn, a_lift]() {
				auto* ui = RE::UI::GetSingleton();
				if (!ui)
				{
					logger::warn("pause: UI singleton not ready - pause {} skipped", a_wantOwn ? "on" : "off");
					return;
				}
				const bool own = a_wantOwn && !a_lift;
				if (own && !g_pauseHeld)
				{
					++ui->numPausesGame;
					g_pauseHeld = true;
					logger::info("pause: game paused while the menu is open (pause count now {})", ui->numPausesGame);
				}
				else if (!own && g_pauseHeld)
				{
					if (ui->numPausesGame > 0)
					{
						--ui->numPausesGame;
					}
					g_pauseHeld = false;
					logger::info("pause: released (menu closed, setting off, or a menu's scripts running) - game resumed (pause count now {})", ui->numPausesGame);
				}
			});
		}
		std::atomic<void*> g_gameWindow{ nullptr };   // the game's HWND, set at D3DInit; read by the watchdog
		std::atomic<bool> g_justOpened{ false };  // set on the input thread, consumed on the render thread

		// Opened from the row in the game's own System menu, rather than from the hotkey or a menu
		// launcher. Geometry only - see SetMenuVisible.
		std::atomic<bool> g_nested{ false };

		// Set whenever the window is opened; consumed by the draw once it has placed the window at
		// its profile's geometry. Separate from g_justOpened, which is consumed elsewhere for the
		// focus grab - two consumers of one exchange() flag would race to see it.
		std::atomic<bool> g_applyGeometry{ false };
		// 2.1.6: a DevBench art pick (listener thread) asks the render thread to reload the art at its next frame - the
		// textures and their cache belong to the render thread.
		std::atomic<bool> g_artReloadPending{ false };

		// Knotwork frame texture (the embedded MO2-Skyrim border-image.png). Created once at
		// D3DInit on the game's own device; used by DrawKnotworkFrame as an ImGui texture id.
		ID3D11ShaderResourceView* g_knotSRV = nullptr;

		// Menu navigation state, promoted from static locals so the DevBench tool can drive and read
		// it from the listener thread (see DevBenchTool.cpp). Guarded by g_selLock; the render loop
		// copies in at frame start and out at frame end.
		std::mutex g_selLock;
		// The framework window's rect as last drawn (2.1.1), for the DevBench state: a resize or move test reads the real
		// size instead of judging a screenshot (the owner, 2026-10-05: "I couldn't resize the vertical length ... you'll
		// have to check"). Guarded by g_selLock.
		float g_mainX = 0.0f, g_mainY = 0.0f, g_mainW = 0.0f, g_mainH = 0.0f;
		std::string g_selTab  = "mods";            // kept for the DevBench state JSON; the SMF shape has one list
		std::string g_selNode = "settings";        // side-list entry: settings|controls|help|mod
		int g_selMod = 0;
		// The open mod's tab bar, mirrored under this same lock for the DevBench state JSON. The
		// render loop owns the live values below; these are the copy the listener thread may read.
		std::string g_selTabName;
		int g_selTabIndex = 0;
		int g_selTabCount = 0;
		// Set when the selection is changed from OUTSIDE the UI (the amf.menu DevBench tool).
		// Without this the render loop copied its own state back every frame and ImGui's tab bar,
		// which owns its selected tab internally, stomped the external change immediately - the
		// automated pane sweep on 2026-08-28 showed every select() snapping back to "quests".
		bool g_selExternal = false;

		// Draws the 78x78 knotwork PNG as a 9-slice frame around the given screen rect: the four
		// ornate corners at fixed size, the four edges stretched between them, the centre left
		// transparent so the window shows through. Faithful reproduction, so the art is drawn at
		// its own colour (white tint = no recolour). No-op if the texture failed to create.
		// The nine-slice itself, for ANY texture. Split out on 2026-09-09 so a UI author's own
		// frame art (skin::FrameTexture) goes through exactly the same geometry the built-in
		// knotwork always did - one implementation, so a supplied frame cannot draw differently
		// from the one this was proven on.
		// a_tile / a_dcs (2.1.6, from the Oblivion port with its map-edge frame): a_tile REPEATS the edges' middle strip at
		// its own size instead of stretching it - for art with a pattern along the edge (the map edge's stitches), which
		// stretching would smear; the last repeat on each side is cut short, UVs and all, so nothing overhangs the far
		// corner. a_dcs is the corner's size ON SCREEN when it differs from its size in the texture (0 = the same).
		void DrawNineSlice(ImDrawList* dl, void* a_srv, float W, float H, float cs,
						   const ImVec2& p0, const ImVec2& p1, bool a_tile = false, float a_dcs = 0.0f)
		{
			if (!a_srv || !dl || W <= 0.0f || H <= 0.0f || cs <= 0.0f)
			{
				return;
			}
			const float dcs = a_dcs > 0.0f ? a_dcs : cs;
			const float k = dcs / cs;   // screen pixels per texture pixel

			// UV split points (source), and screen split points (dest, corners at dcs px).
			const float u0 = 0.0f, u1 = cs / W, u2 = (W - cs) / W, u3 = 1.0f;
			const float v0 = 0.0f, v1 = cs / H, v2 = (H - cs) / H, v3 = 1.0f;
			const float x0 = p0.x, x1 = p0.x + dcs, x2 = p1.x - dcs, x3 = p1.x;
			const float y0 = p0.y, y1 = p0.y + dcs, y2 = p1.y - dcs, y3 = p1.y;

			// Degenerate guard: a window smaller than two corners would flip the middle slices.
			if (x2 <= x1 || y2 <= y1)
			{
				return;
			}

			const auto tex = reinterpret_cast<ImTextureID>(a_srv);
			// 2.1.5: the player's Frame art tint (Appearance > Colours); white, the art as drawn, by default.
			const std::uint32_t artTint = theme::RoleColor(theme::kRoleArt);
			const ImU32 white = artTint ? static_cast<ImU32>(artTint) : IM_COL32_WHITE;
			auto slice = [&](float ax, float ay, float bx, float by, float au, float av, float bu, float bv) {
				dl->AddImage(tex, ImVec2(ax, ay), ImVec2(bx, by), ImVec2(au, av), ImVec2(bu, bv), white);
			};

			// corners
			slice(x0, y0, x1, y1, u0, v0, u1, v1);  // top-left
			slice(x2, y0, x3, y1, u2, v0, u3, v1);  // top-right
			slice(x0, y2, x1, y3, u0, v2, u1, v3);  // bottom-left
			slice(x2, y2, x3, y3, u2, v2, u3, v3);  // bottom-right
			if (a_tile && W > 2.0f * cs && H > 2.0f * cs)
			{
				const float runU = (W - 2.0f * cs) * k;   // the strip's own length, on screen
				const float runV = (H - 2.0f * cs) * k;
				constexpr int kMaxRepeats = 512;
				int n = 0;
				for (float x = x1; x < x2 && n < kMaxRepeats; x += runU, ++n)
				{
					const float xe = std::min(x + runU, x2);
					const float ue = u1 + (u2 - u1) * ((xe - x) / runU);
					slice(x, y0, xe, y1, u1, v0, ue, v1);   // top
					slice(x, y2, xe, y3, u1, v2, ue, v3);   // bottom
				}
				n = 0;
				for (float y = y1; y < y2 && n < kMaxRepeats; y += runV, ++n)
				{
					const float ye = std::min(y + runV, y2);
					const float ve = v1 + (v2 - v1) * ((ye - y) / runV);
					slice(x0, y, x1, ye, u0, v1, u1, ve);   // left
					slice(x2, y, x3, ye, u2, v1, u3, ve);   // right
				}
				return;
			}
			// edges (stretched along their run)
			slice(x1, y0, x2, y1, u1, v0, u2, v1);  // top
			slice(x1, y2, x2, y3, u1, v2, u2, v3);  // bottom
			slice(x0, y1, x1, y2, u0, v1, u1, v2);  // left
			slice(x2, y1, x3, y2, u2, v1, u3, v2);  // right
		}

		// Draws the window frame: the UI author's own art when one is configured and loaded,
		// otherwise the embedded 78x78 knotwork. A supplied frame REPLACES the knotwork rather
		// than drawing over it - a theme in this project means replacement art, not a second
		// ornament on top of the first.
		void DrawKnotworkFrame(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1)
		{
			if (skin::HasFrame())
			{
				const ImVec2 sz = skin::FrameSize();
				// 2.1.6: a frame drawn at its own on-screen corner (the map edge: art at twice its size) scales with the
				// layout, as on the Oblivion port - the window padding is the knotwork corner + 8 px at the 1080p baseline
				// times the UI scale, so the band grows with the text and always fits the padding it sits in.
				float dcs = skin::FrameDrawCorner();
				if (dcs > 0.0f)
				{
					const float base = static_cast<float>(knotwork::kCorner) + 8.0f;
					dcs *= std::max(1.0f, ImGui::GetStyle().WindowPadding.x / base);
				}
				DrawNineSlice(dl, skin::FrameTexture(), sz.x, sz.y, skin::FrameCorner(), p0, p1, skin::FrameTiles(), dcs);
				return;
			}
			if (!skin::DrawsFrame()) { return; }   // 2.1.6: the player picked no frame - not even the knotwork
			DrawNineSlice(dl, g_knotSRV, static_cast<float>(knotwork::kWidth),
						  static_cast<float>(knotwork::kHeight), static_cast<float>(knotwork::kCorner), p0, p1);
		}

		// The author's background, drawn INSIDE the given rect and clipped to it, behind whatever
		// the window then draws. A small image tiles at its own pixel size; a large one is
		// stretched to fill. Which one it is is decided by the image, not by a fifth INI key.
		void DrawSkinBackground(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1)
		{
			if (!dl || !skin::HasBackground() || p1.x <= p0.x || p1.y <= p0.y)
			{
				return;
			}
			const auto  tex = reinterpret_cast<ImTextureID>(skin::BackgroundTexture());
			const ImVec2 sz = skin::BackgroundSize();
			// A UI author's background fades with the window when See-through window is on (2.1.1).
			const auto& sv = settings::Get();
			const int fade = sv.seeThrough ? std::clamp(sv.windowOpacity, 5, 100) : 100;
			// 2.1.5: tinted by the player's Frame art colour, as the frame is; white = as drawn.
			const std::uint32_t artTint = theme::RoleColor(theme::kRoleArt);
			const ImU32 white = artTint ? ((static_cast<ImU32>(artTint) & 0x00FFFFFFu) | (static_cast<ImU32>(fade * 255 / 100) << 24))
			                            : IM_COL32(255, 255, 255, fade * 255 / 100);

			if (!skin::BackgroundTiles())
			{
				dl->AddImage(tex, p0, p1, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), white);
				return;
			}

			// Tiled. Clip so the last row and column are cut rather than overhanging the window,
			// and cap the count so a 1px image cannot spend the frame budget on draw calls.
			dl->PushClipRect(p0, p1, true);
			const float tw = sz.x > 0.0f ? sz.x : 1.0f;
			const float th = sz.y > 0.0f ? sz.y : 1.0f;
			constexpr int kMaxTiles = 4096;
			int drawn = 0;
			for (float y = p0.y; y < p1.y && drawn < kMaxTiles; y += th)
			{
				for (float x = p0.x; x < p1.x && drawn < kMaxTiles; x += tw, ++drawn)
				{
					dl->AddImage(tex, ImVec2(x, y), ImVec2(x + tw, y + th),
								 ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), white);
				}
			}
			dl->PopClipRect();
		}

		// The knotwork frames a rect from just OUTSIDE it (author, 2026-09-01): drawn exactly on a
		// pane's rect, the art's own hairlines land on the pane's 1px border and the two read as one
		// smudged double line. Pushed out by a few pixels it reads as a frame AROUND the box, and the
		// box's own border and corners stay legible - which is what the Untarnished theme gets for
		// free by having no art at all.
		constexpr float kKnotOutset = 4.0f;

		void DrawKnotworkAround(ImDrawList* dl, ImVec2 p0, ImVec2 p1)
		{
			DrawKnotworkFrame(dl, ImVec2(p0.x - kKnotOutset, p0.y - kKnotOutset),
							  ImVec2(p1.x + kKnotOutset, p1.y + kKnotOutset));
		}

		// THE THEME'S FRAME ROUND A HIGHLIGHTED ENTRY (2.1.5, the owner, 2026-10-07: "I want Skyrim for AMF to do the same thing
		// that Skyrim for Witcher 3 does by having frame art on the selected box. So while your mouse hovers over different menu
		// names, it has a frame going around it" - "and the frame should match the theme frame"). The theme's own frame art -
		// the knotwork, or a theme's frame.png - nine-sliced around the item with its corners scaled to the row's height, drawn
		// over everything just before Render so no item's fill covers it. Full strength round the controller / keyboard
		// highlight anywhere in the menu, fainter round the menu name under the mouse. A theme with no art (Untarnished) gets
		// its own plain line. The bright-blue nav box stays (the owner, 2026-09-15); the frame sits just outside it.
		struct HighlightMark
		{
			ImDrawList* drawList = nullptr;
			ImVec2 min, max, clipMin, clipMax;
			bool nav = false;
		};
		std::vector<HighlightMark> g_highlights;   // render thread: this frame's framed items

		void DrawHighlightFrame(ImDrawList* dl, ImVec2 p0, ImVec2 p1, float a_alpha)
		{
			const float unit = ImGui::GetFontSize() / 16.0f;   // the resolution scale, read from the text size it set
			// No frame round a rect with no height or width (the owner, 2026-10-07: D-pad up from the top of the list drew
			// "a single straight horizontal line" under the title - ImGui's nav rect with nothing in it, which the plain-line
			// form drew as a flat box). A real row is at least half a line tall.
			if (p1.y - p0.y < ImGui::GetFontSize() * 0.5f || p1.x - p0.x < ImGui::GetFontSize() * 0.5f) { return; }
			const float pad = std::max(2.0f, std::round(2.0f * unit));
			p0 = ImVec2(p0.x - pad, p0.y - pad);
			p1 = ImVec2(p1.x + pad, p1.y + pad);
			// 2.1.6: a frame picked for the Highlight kind (Appearance > Art) draws here instead of the window's frame.
			const skin::ArtPart* hl = skin::Part(skin::ArtKind::kHighlight);
			const bool art = hl || skin::DrawsFrame();
			if (!art)
			{
				const ImVec4 b = ImGui::GetStyleColorVec4(ImGuiCol_Border);
				dl->AddRect(p0, p1, ImGui::GetColorU32(ImVec4(b.x, b.y, b.z, b.w * a_alpha)), 0.0f, 0, std::max(1.0f, std::round(unit)));
				return;
			}
			void* srv = hl ? hl->main.srv : skin::HasFrame() ? skin::FrameTexture() : g_knotSRV;
			const ImVec2 size = hl ? ImVec2(hl->main.w, hl->main.h) : skin::HasFrame() ? skin::FrameSize() : ImVec2(static_cast<float>(knotwork::kWidth), static_cast<float>(knotwork::kHeight));
			const float csSrc = hl ? hl->corner : skin::HasFrame() ? skin::FrameCorner() : static_cast<float>(knotwork::kCorner);
			if (!srv || size.x <= 0.0f || size.y <= 0.0f || csSrc <= 0.0f) { return; }
			// NOTHING OVER THE TEXT (the owner's screenshot, 2026-10-07: the full nine-slice on one row "blocks out part of the
			// text and text box, so you can't see the inside" - the art's edge bands are solid). So: a thin line round the
			// item, just outside it, and only the art's four CORNER ornaments, small and centred on the line's corners - a
			// quarter of each reaches inside, clear of the text, which starts a frame-padding in from the edge.
			// A theme's OWN frame art (Oathvein, Vel'dun, Norden: thin lines with open bands) reads well as the whole nine-slice
			// round a row (the owner, same evening: "Oathvein looks fine"), so it keeps that; the knotwork's solid bands get
			// the line-and-corners form.
			const float h = p1.y - p0.y, w = p1.x - p0.x;
			// 2.1.6: which form is now the frame's own say (sHighlight in its .ini): the knotwork and the map edge are
			// "corners", the themes' thin-line frames "whole". A frame from a path, with no .ini, keeps the whole frame.
			const bool wholeFrame = hl ? !hl->cornersOnly : skin::HasFrame() && !skin::FrameHighlightCorners();
			const float cs = std::clamp(std::min(h, w) * (wholeFrame ? 0.42f : 0.4f), std::min(4.0f * unit, csSrc), csSrc);
			const float u1 = csSrc / size.x, u2 = (size.x - csSrc) / size.x, v1 = csSrc / size.y, v2 = (size.y - csSrc) / size.y;
			if (w <= cs || h <= cs * 0.5f) { return; }
			// The art's tint: the player's Hover highlight colour round a hovered name when they set one, else the Frame art
			// tint (white = the art as drawn).
			const bool hoverPicked = a_alpha < 1.0f && theme::RolePicked(theme::kRoleHover);
			const std::uint32_t artTint = theme::RoleColor(hoverPicked ? theme::kRoleHover : theme::kRoleArt);
			ImVec4 tint = ImGui::ColorConvertU32ToFloat4(artTint ? static_cast<ImU32>(artTint) : IM_COL32_WHITE);
			tint.w *= a_alpha * ImGui::GetStyle().Alpha;
			const ImU32 col = ImGui::ColorConvertFloat4ToU32(tint);
			const auto tex = reinterpret_cast<ImTextureID>(srv);
			auto slice = [&](float ax, float ay, float bx, float by, float au, float av, float bu, float bv) {
				dl->AddImage(tex, ImVec2(ax, ay), ImVec2(bx, by), ImVec2(au, av), ImVec2(bu, bv), col);
			};
			if (wholeFrame)
			{
				const float x1 = p0.x + cs, x2 = p1.x - cs, y1 = p0.y + cs, y2 = p1.y - cs;
				if (x2 <= x1 || y2 <= y1) { return; }
				slice(p0.x, p0.y, x1, y1, 0.0f, 0.0f, u1, v1);
				slice(x2, p0.y, p1.x, y1, u2, 0.0f, 1.0f, v1);
				slice(p0.x, y2, x1, p1.y, 0.0f, v2, u1, 1.0f);
				slice(x2, y2, p1.x, p1.y, u2, v2, 1.0f, 1.0f);
				slice(x1, p0.y, x2, y1, u1, 0.0f, u2, v1);
				slice(x1, y2, x2, p1.y, u1, v2, u2, 1.0f);
				slice(p0.x, y1, x1, y2, 0.0f, v1, u1, v2);
				slice(x2, y1, p1.x, y2, u2, v1, 1.0f, v2);
				return;
			}
			// the line, in the frame-line colour with the same strength as the art
			{
				const ImVec4 b = ImGui::GetStyleColorVec4(ImGuiCol_Border);
				dl->AddRect(p0, p1, ImGui::GetColorU32(ImVec4(b.x, b.y, b.z, b.w * a_alpha)), 0.0f, 0, std::max(1.0f, std::round(unit)));
			}
			const float half = cs * 0.5f;
			slice(p0.x - half, p0.y - half, p0.x + half, p0.y + half, 0.0f, 0.0f, u1, v1);   // top-left
			slice(p1.x - half, p0.y - half, p1.x + half, p0.y + half, u2, 0.0f, 1.0f, v1);   // top-right
			slice(p0.x - half, p1.y - half, p0.x + half, p1.y + half, 0.0f, v2, u1, 1.0f);   // bottom-left
			slice(p1.x - half, p1.y - half, p1.x + half, p1.y + half, u2, v2, 1.0f, 1.0f);   // bottom-right
		}

		// Right after an item the mouse may be over: a hover frame round it when it is (the mod list's names).
		void NoteHoverFrame()
		{
			if (!ImGui::IsItemHovered()) { return; }
			ImDrawList* dl = ImGui::GetWindowDrawList();
			g_highlights.push_back({ dl, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), dl->GetClipRectMin(), dl->GetClipRectMax(), false });
		}

		// Before Render: the nav item's frame (ImGui's own nav rect, wherever the highlight is), then every frame noted.
		void FlushHighlightFrames()
		{
			ImGuiContext& g = *GImGui;
			// NavIdIsAlive: the highlighted item was drawn THIS frame. Without it the frame went round the last place a vanished
			// item stood - a box round nothing beside the General tab after a tab change (the owner's screenshot, 2026-10-08).
			// ImGui's own highlight is drawn by the item itself, so it never had this.
			if (g.NavWindow && g.NavId != 0 && g.NavIdIsAlive && !g.NavDisableHighlight && g.NavWindow->DrawList)
			{
				const ImRect r = ImGui::WindowRectRelToAbs(g.NavWindow, g.NavWindow->NavRectRel[g.NavLayer]);
				const ImRect clip = g.NavWindow->InnerClipRect;
				g_highlights.push_back({ g.NavWindow->DrawList, r.Min, r.Max, clip.Min, clip.Max, true });
			}
			for (std::size_t i = 0; i < g_highlights.size(); ++i)
			{
				const HighlightMark& m = g_highlights[i];
				if (!m.drawList) { continue; }
				if (!m.nav)
				{
					bool underNav = false;   // the hover frame stays off the row the nav frame is already round
					for (const HighlightMark& o : g_highlights)
					{
						underNav = underNav || (o.nav && o.drawList == m.drawList && o.min.x == m.min.x && o.min.y == m.min.y);
					}
					if (underNav) { continue; }
				}
				m.drawList->PushClipRect(m.clipMin, m.clipMax, false);
				DrawHighlightFrame(m.drawList, m.min, m.max, m.nav ? 1.0f : 0.6f);
				m.drawList->PopClipRect();
			}
			g_highlights.clear();
		}

		// M1.1 (the author's smoke-test feedback): at 3200x1800 the stock ImGui font and a fixed
		// 520x340 window are "far too small". One scale factor, derived from the real display
		// height against 1080p as the baseline, applied to the font, the style metrics and the
		// default window size together so everything stays proportioned.
		float g_uiScale = 1.0f;

		// FONT (1.4.2). The default ImGui font is ProggyClean, a 13px BITMAP face; the old code
		// magnified it with FontGlobalScale = uiScale * textScale (~2.17x at 3200x1800), which is
		// exactly why the text looked pixelated. Instead we rasterise a real TrueType face at the
		// NATIVE pixel size for the display, and keep FontGlobalScale at 1.0 so nothing is
		// magnified. Changing the text-size slider rebuilds the atlas rather than stretching it.
		constexpr float kBaseFontPx = 16.0f;   // at the 1080p baseline, before uiScale/textScale
		std::atomic<bool> g_fontRebuildPending{ false };
		// A Font Awesome face a mod asked for that the atlas lacks (2.1.3): ADDED to the built atlas, which is then built
		// again - never Clear()ed. Clear() frees every ImFont, and a mod drawing through this framework may keep the
		// ImFont* it was given (GetFont(), io.Fonts->Fonts[0], a pushed face): KnightQueen1, 2026-10-07, AE 1.6.1170 -
		// the first click on Cinematic Conversation Camera or MCM Memory (both push "solid") froze the game right after
		// "atlas 3 built" (2.1.1: a crash). ImGui 1.90.8 rebuilds the existing ImFont objects in place when the atlas is
		// built again, so every pointer anyone holds stays valid. Full rebuilds stay for the player's own language,
		// face and text-size changes.
		std::atomic<bool> g_iconFaceAddPending{ false };
		std::string g_lastTextFacePath;   // the text face the last full build used - every icon face is built on it
		float g_lastFontPx = 0.0f;
		bool g_iconFaceBuilt[consumer::kIconFaceCount] = {};

		// When Save was last pressed, so "saved" can appear beside the button for a few seconds
		// rather than the press doing nothing visible. settings::Save() returns nothing, so there
		// is no honest success/failure to report here - only that the write was asked for.
		double g_menuListSavedAt = 0.0;
		// Layout presets (2.0.3): the name being typed, and the last action's result for a few seconds.
		char g_presetName[64] = {};
		char g_mcmImportFilter[64] = {};  // the settings page's filter over the MCM import list
		std::string g_mcmSortStatus;      // the last MCM sort's one-line result under its buttons
		char g_memProfileName[64] = {};   // Remembered MCM settings: the name typed for a new profile
		std::string g_memStatus;          // Remembered MCM settings: why a button did nothing ("" when it ran)

		std::string FormatSortStatus(const mcmloader::SortResult& a_r)
		{
			char line[256];
			if (!a_r.changed)
			{
				snprintf(line, sizeof(line), "%s", TR("AMF_McmSortNothing", "Nothing changed - the MCM menus are already sorted."));
			}
			else
			{
				snprintf(line, sizeof(line), TR("AMF_McmSortDone", "Sorted: %d menus moved, %d separators made, %d left where you put them."),
					a_r.moved, a_r.separatorsMade, a_r.kept);
			}
			return line;
		}
		std::string g_presetStatus;
		double g_presetStatusAt = 0.0;

		// Keyboard-loss diagnostic (1.9.5). Stamped WHEN THEY HAPPEN, read afterwards.
		int g_frameFocusHere = -1;      // SetKeyboardFocusHere() was called on this frame
		int g_frameWindowFocus = -1;    // SetNextWindowFocus() was called on this frame
		int g_frameNavConsumed = -1;    // the nav-to-selected flag was consumed TRUE on this frame
		int g_frameSearchDrawn = -1;    // the mod-search box was actually submitted on this frame

		// The rename asked for from a mod row's right-click menu. The modal itself is drawn once,
		// outside the list, because a popup opened from inside the loop would otherwise be
		// submitted once per row and fight itself for the id.
		std::string g_renameTarget;
		char g_renameBuffer[64] = {};
		bool g_renameOpenPending = false;
	}

	// Strings::SetLanguage and kDataLoaded ask for a new atlas holding the language's glyphs.
	void RequestFontRebuild() { g_fontRebuildPending = true; }
	// A consumer pushed a Font Awesome face the atlas lacks: added at the next frame's start, nothing freed (2.1.3).
	void RequestIconFaces() { g_iconFaceAddPending = true; }

	namespace
	{
		std::mutex g_fontProbeLock;
		FontProbe g_fontProbe;
	}

	FontProbe GetFontProbe()
	{
		std::scoped_lock l(g_fontProbeLock);
		return g_fontProbe;
	}

	namespace
	{

		// Ordered candidates: a clean sans that matches Skyrim's own menu lettering, then fallbacks.
		// A user-supplied path (sFontPath in the INI) wins when set, so any .ttf can be dropped in.
		const char* const kFontCandidates[] = {
			"C:/Windows/Fonts/segoeui.ttf",
			"C:/Windows/Fonts/calibri.ttf",
			"C:/Windows/Fonts/trebuc.ttf",
		};

		// Selectable faces for the FONT PICKER (design decision, 2026-08-28: "a separate selector from the
		// theme that lets you choose a font"). Deliberately its own control, not a theme property -
		// a theme sets colours; the face is an independent choice, so any font works with any theme.
		// Scanned once from the Windows font directory plus AMF's own optional fonts folder, so a
		// .ttf dropped in beside the plugin shows up in the list.
		struct FontChoice
		{
			std::string label;  // shown in the combo
			std::string path;   // empty = "Default (auto)"
		};
		std::vector<FontChoice> g_fontChoices;

		void ScanFonts()
		{
			g_fontChoices.clear();
			g_fontChoices.push_back({ "Default (auto)", "" });

			// Curated, widely-present Windows faces - a full enumeration of C:/Windows/Fonts would
			// be hundreds of entries, most of them useless for a game menu.
			const std::pair<const char*, const char*> known[] = {
				{ "Segoe UI",        "C:/Windows/Fonts/segoeui.ttf" },
				{ "Segoe UI Semibold","C:/Windows/Fonts/seguisb.ttf" },
				{ "Calibri",         "C:/Windows/Fonts/calibri.ttf" },
				{ "Trebuchet MS",    "C:/Windows/Fonts/trebuc.ttf" },
				{ "Georgia",         "C:/Windows/Fonts/georgia.ttf" },
				{ "Constantia",      "C:/Windows/Fonts/constan.ttf" },
				{ "Palatino Linotype","C:/Windows/Fonts/pala.ttf" },
				{ "Times New Roman", "C:/Windows/Fonts/times.ttf" },
				{ "Cambria",         "C:/Windows/Fonts/cambria.ttc" },
			};
			for (const auto& k : known)
			{
				std::error_code ec;
				if (std::filesystem::exists(k.second, ec)) { g_fontChoices.push_back({ k.first, k.second }); }
			}

			// Anything the user drops into Data/SKSE/Plugins/ApocryphaMenuFramework/fonts/.
			const std::filesystem::path dir{ "Data/SKSE/Plugins/ApocryphaMenuFramework/fonts" };
			std::error_code ec;
			if (std::filesystem::is_directory(dir, ec))
			{
				for (const auto& e : std::filesystem::directory_iterator(dir, ec))
				{
					if (!e.is_regular_file(ec)) { continue; }
					const auto ext = e.path().extension().string();
					if (_stricmp(ext.c_str(), ".ttf") == 0 || _stricmp(ext.c_str(), ".otf") == 0)
					{
						g_fontChoices.push_back({ e.path().stem().string(), e.path().string() });
					}
				}
			}
			logger::info("font picker: {} face(s) available", g_fontChoices.size());
		}

		// One Font Awesome face (2.0.4): the text face (Latin, Latin Extended-A, Cyrillic) with that style's icons merged
		// in, added to io.Fonts. nullptr when the icon file is missing or the text face cannot be read. Shared by the full
		// build and by the add-only path (AddIconFaces, 2.1.3).
		ImFont* AddIconFace(int a_face, const std::string& a_textFace, float a_px)
		{
			static constexpr const char* kIconFiles[consumer::kIconFaceCount] = { "fa-solid-900.ttf", "fa-regular-400.ttf", "fa-brands-400.ttf" };
			static const ImWchar kIconTextRanges[] = { 0x0020, 0x00FF, 0x0100, 0x017F, 0x0400, 0x04FF, 0 };
			static const ImWchar kIconRanges[] = { 0xE000, 0xF8FF, 0 };
			static bool s_missingLogged[consumer::kIconFaceCount] = {};
			if (a_face < 0 || a_face >= consumer::kIconFaceCount || a_textFace.empty())
			{
				return nullptr;
			}
			ImGuiIO& io = ImGui::GetIO();
			const float iconPx = std::round(a_px * 0.8f);
			const std::string path = std::string("Data/SKSE/Plugins/ApocryphaMenuFramework/icons/") + kIconFiles[a_face];
			std::error_code ec;
			if (!std::filesystem::exists(path, ec))
			{
				if (!s_missingLogged[a_face])
				{
					s_missingLogged[a_face] = true;
					logger::warn("font: \"{}\" is missing - a mod asked for that Font Awesome face, so its icons "
								 "draw as \"?\" (reinstall Apocrypha Menu Framework)", path);
				}
				return nullptr;
			}
			ImFont* const iconFont = io.Fonts->AddFontFromFileTTF(a_textFace.c_str(), a_px, nullptr, kIconTextRanges);
			if (!iconFont)
			{
				return nullptr;
			}
			ImFontConfig icons;
			icons.MergeMode = true;
			icons.PixelSnapH = true;
			icons.OversampleH = 1;
			icons.GlyphMinAdvanceX = iconPx;
			if (!io.Fonts->AddFontFromFileTTF(path.c_str(), iconPx, &icons, kIconRanges))
			{
				logger::warn("font: \"{}\" could not be read as a font - that icon face stays text only", path);
			}
			return iconFont;
		}

		void BuildFonts();

		// THE ADD-ONLY PATH (2.1.3, KnightQueen1's freeze - see g_iconFaceAddPending). The faces a mod asked for that the
		// atlas lacks are ADDED to it and the atlas is built again: no Clear(), so no ImFont anyone holds is freed - ImGui
		// rebuilds the existing ones in place. Call OUTSIDE a frame, with the backend's device objects invalidated (the
		// font texture is made again at the next NewFrame). Falls back to a full build when there was none to add to.
		void AddIconFaces()
		{
			ImGuiIO& io = ImGui::GetIO();
			if (g_lastTextFacePath.empty() || g_lastFontPx <= 0.0f || io.Fonts->Fonts.Size == 0)
			{
				BuildFonts();
				return;
			}
			bool added = false;
			for (int f = 0; f < consumer::kIconFaceCount; ++f)
			{
				if (!consumer::IconFaceWanted(f) || g_iconFaceBuilt[f])
				{
					continue;
				}
				g_iconFaceBuilt[f] = true;   // tried once per atlas: a missing file is not retried every frame
				if (ImFont* face = AddIconFace(f, g_lastTextFacePath, g_lastFontPx))
				{
					consumer::SetIconFont(f, face);   // handed over AFTER the build below, before the next frame uses it
					added = true;
				}
			}
			if (!added)
			{
				return;
			}
			const int fontsBefore = io.Fonts->Fonts.Size;
			ImFont* const textFace = io.Fonts->Fonts[0];   // what a mod that kept GetFont() holds
			io.Fonts->ClearTexData();
			io.Fonts->Build();
			// the proof the fix rests on: the face a mod may hold is the same object, filled again, after the build
			logger::info("font: the text face a mod may hold is {} after the build ({} glyphs)",
						 io.Fonts->Fonts[0] == textFace && textFace->Glyphs.Size > 0 ? "the same object, rebuilt in place" : "NOT the same object",
						 textFace->Glyphs.Size);
			unsigned char* pixels = nullptr;
			int w = 0, h = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
			std::string faces;
			static constexpr const char* kFaceNames[consumer::kIconFaceCount] = { "solid", "regular", "brands" };
			for (int f = 0; f < consumer::kIconFaceCount; ++f)
			{
				if (g_iconFaceBuilt[f] && consumer::IconFaceWanted(f)) { faces += (faces.empty() ? "" : ", ") + std::string(kFaceNames[f]); }
			}
			logger::info("font: Font Awesome face(s) added to the atlas without a rebuild - {} ({} fonts, {}x{}); every font "
						 "a mod already holds stays valid", faces, fontsBefore, w, h);
		}

		// Rebuilds the font atlas at the current scale. Call OUTSIDE a frame (before NewFrame).
		void BuildFonts()
		{
			ImGuiIO& io = ImGui::GetIO();
			const float px = kBaseFontPx * g_uiScale * settings::Get().textScale;

			io.Fonts->Clear();
			ImFont* loaded = nullptr;
			std::string loadedPath;   // the text face's file, which every icon face is built on (2.0.4)
			// Clear() freed every ImFont - a consumer must not be handed a dead icon face.
			for (int f = 0; f < consumer::kIconFaceCount; ++f) { consumer::SetIconFont(f, nullptr); }
			ImFont* iconFonts[consumer::kIconFaceCount] = {};

			// GLYPH RANGES (1.6.4, language support): the atlas holds the default Latin set plus
			// every character that appears in the loaded translation - Cyrillic, Polish and Czech
			// letters, kana, hanzi - built from the strings themselves, so no per-language table
			// can be wrong or incomplete. Static so the ranges outlive Build().
			// 1.8.9 (littlefot's Wheeler report, 2026-09-17, the same class checked here): the atlas
			// also holds the BUILT-IN ranges of the scripts in play - the framework's language AND the
			// game's own sLanguage - because not every character a page draws comes from a translation
			// file: a Japanese game lists Japanese item names in Item Explorer whatever language the
			// framework's pages are set to, and those kanji were in no file the builder had read.
			static ImVector<ImWchar> s_ranges;
			const std::string lang = strings::Language();
			const std::string gameLang = strings::GameLanguageSetting();
			{
				ImFontGlyphRangesBuilder builder;
				builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
				for (const std::string& l : { lang, gameLang })
				{
					const ImWchar* r = nullptr;
					if (l == "japanese") { r = io.Fonts->GetGlyphRangesJapanese(); }
					else if (l == "korean") { r = io.Fonts->GetGlyphRangesKorean(); }
					else if (l == "chinese" || l == "schinese" || l == "tchinese") { r = io.Fonts->GetGlyphRangesChineseSimplifiedCommon(); }
					else if (l == "russian" || l == "ukrainian" || l == "bulgarian") { r = io.Fonts->GetGlyphRangesCyrillic(); }
					else if (l == "thai") { r = io.Fonts->GetGlyphRangesThai(); }
					else if (l == "vietnamese") { r = io.Fonts->GetGlyphRangesVietnamese(); }
					if (r) { builder.AddRanges(r); }
				}
				builder.AddText(strings::AllText().c_str());
				s_ranges.clear();
				builder.BuildRanges(&s_ranges);
			}

			const std::string& custom = settings::Get().fontPath;
			if (!custom.empty())
			{
				loaded = io.Fonts->AddFontFromFileTTF(custom.c_str(), px, nullptr, s_ranges.Data);
				if (!loaded) { logger::warn("font: sFontPath \"{}\" could not be loaded; falling back", custom); }
				else { loadedPath = custom; }
			}
			for (const char* cand : kFontCandidates)
			{
				if (loaded) { break; }
				loaded = io.Fonts->AddFontFromFileTTF(cand, px, nullptr, s_ranges.Data);
				if (loaded) { loadedPath = cand; logger::info("font: rasterised \"{}\" at {:.1f}px", cand, px); }
			}
			// A FALLBACK FACE merged in for the glyphs the chosen face lacks (MergeMode adds only what
			// is missing): the Latin faces above carry Cyrillic and Latin Extended but no kana or
			// hanzi, so Japanese and Chinese draw from a system CJK face. Harmless for English.
			if (loaded)
			{
				// The script that picks the preferred face: the framework's language when it is CJK,
				// otherwise the game's (1.8.9 - a Japanese game with English pages still needs kana).
				auto isCjk = [](const std::string& l) { return l == "japanese" || l == "korean" || l == "chinese" || l == "schinese" || l == "tchinese"; };
				const std::string cjkLang = isCjk(lang) ? lang : isCjk(gameLang) ? gameLang : lang;
				// Per language first (the owner's priority order: Japanese, Korean, Chinese, Russian), then
				// every CJK/Hangul face Windows ships, so a missing preferred face still finds glyphs.
				const char* const cjk[] = {
					cjkLang == "japanese" ? "C:/Windows/Fonts/meiryo.ttc" : cjkLang == "korean" ? "C:/Windows/Fonts/malgun.ttf" : "C:/Windows/Fonts/msyh.ttc",
					cjkLang == "japanese" ? "C:/Windows/Fonts/YuGothM.ttc" : cjkLang == "korean" ? "C:/Windows/Fonts/malgunbd.ttf" : "C:/Windows/Fonts/simsun.ttc",
					"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/meiryo.ttc", "C:/Windows/Fonts/malgun.ttf", "C:/Windows/Fonts/YuGothM.ttc", "C:/Windows/Fonts/msgothic.ttc", "C:/Windows/Fonts/simsun.ttc" };
				if (lang != "english" || gameLang != "english")
				{
					ImFontConfig merge;
					merge.MergeMode = true;
					merge.PixelSnapH = true;
					for (const char* face : cjk)
					{
						std::error_code ec;
						if (!std::filesystem::exists(face, ec)) { continue; }
						if (io.Fonts->AddFontFromFileTTF(face, px, &merge, s_ranges.Data))
						{
							logger::info("font: merged \"{}\" for the glyphs \"{}\" (game \"{}\") needs", face, lang, gameLang);
							break;
						}
					}
				}

				// AMF'S OWN ICONS (2.1.5, the owner: "include Font Awesome for the generated menus"). The ten solid glyphs in
				// AmfIcons.h, merged into the TEXT face itself (MergeMode adds to the last font added, which is still the
				// text face - the CJK merge above adds no font), so a label carries an icon inline with no font push. Only
				// those codepoints are rasterised, so the atlas grows by ten glyphs. Same size and cell rules as the icon
				// faces below. A missing file is logged once; the icons then draw as "?" and the text still reads.
				{
					static const ImWchar kOwnIcons[] = {   // AmfIcons.h's codepoints, in pairs
						0xF00D, 0xF00D, 0xF023, 0xF023, 0xF054, 0xF054, 0xF05A, 0xF05A, 0xF071, 0xF071,
						0xF11C, 0xF11C, 0xF252, 0xF252, 0xF2EA, 0xF2EA, 0xF304, 0xF304, 0xF53F, 0xF53F, 0 };
					static bool s_ownIconsMissingLogged = false;
					const char* const solid = "Data/SKSE/Plugins/ApocryphaMenuFramework/icons/fa-solid-900.ttf";
					std::error_code ec;
					if (std::filesystem::exists(solid, ec))
					{
						const float iconPx = std::round(px * 0.8f);
						ImFontConfig own;
						own.MergeMode = true;
						own.PixelSnapH = true;
						own.OversampleH = 1;
						own.GlyphMinAdvanceX = iconPx;
						if (io.Fonts->AddFontFromFileTTF(solid, iconPx, &own, kOwnIcons))
						{
							logger::debug("font: AMF's own icons merged into the text face at {:.1f}px", iconPx);
						}
						else { logger::warn("font: \"{}\" could not be read - AMF's own icons draw as \"?\"", solid); }
					}
					else if (!s_ownIconsMissingLogged)
					{
						s_ownIconsMissingLogged = true;
						logger::warn("font: \"{}\" is missing - AMF's own icons draw as \"?\" (reinstall Apocrypha Menu Framework)", solid);
					}
				}

				// FONT AWESOME ICON FACES (2.0.4). SKSE Menu Framework consumers push a Font Awesome face by
				// name and draw its icons (U+E000-U+F8FF); RaceMenu Atelier's buttons drew as "?" because only
				// the text face existed (mmmizuhara, 2026-10-03). Each face is a font of its own - solid and
				// regular share codepoints, so they cannot share one - made of the text face (Latin, Latin
				// Extended-A and Cyrillic only, so "<icon> Label" works without copying a CJK set three times)
				// with that style's icons merged in. Only the faces a mod has actually pushed are built
				// (consumer::IconFaceWanted), so a load order with no icon-using mod keeps the atlas it had.
				//
				// SIZE AND BASELINE. A merged glyph sits on the text face's baseline (ImGui offsets every
				// glyph by the first font's ascent), so no GlyphOffset is needed. ImGui sizes a face by
				// ascent - descent: Segoe UI's is 1.33 em, Font Awesome's 534 of 512 units. At 0.8 of the text
				// size an icon is one text em tall - the size Font Awesome draws beside text of the same size
				// on a web page. Measured offline with this ImGui build at the owner's 34.7 px: "H" spans rows
				// 10-28 of the 35 px line, the user icon 4-32, so the icon is centred on the line and on the
				// capitals within a pixel. GlyphMinAdvanceX gives every icon at least a square cell, centred,
				// so a column of icons lines up. OversampleH 1: icons are pixel-snapped shapes that gain
				// nothing from horizontal oversampling, and it halves the atlas space they take.
				//
				// Files: Data/SKSE/Plugins/ApocryphaMenuFramework/icons/, shipped in the package with the
				// SIL OFL 1.1 text beside them. A missing file is logged once and that face keeps the old
				// behaviour (the current font is pushed).
				for (int f = 0; f < consumer::kIconFaceCount; ++f)
				{
					iconFonts[f] = consumer::IconFaceWanted(f) ? AddIconFace(f, loadedPath, px) : nullptr;
				}
			}
			g_lastTextFacePath = loaded ? loadedPath : std::string();
			g_lastFontPx = px;
			for (int f = 0; f < consumer::kIconFaceCount; ++f) { g_iconFaceBuilt[f] = iconFonts[f] != nullptr; }
			if (!loaded)
			{
				// Never fail to render: fall back to the built-in face, magnified as before.
				io.Fonts->AddFontDefault();
				io.FontGlobalScale = g_uiScale * settings::Get().textScale;
				logger::warn("font: no TrueType face could be loaded; using the built-in bitmap font");
				return;
			}

			io.FontGlobalScale = 1.0f;  // native size - no magnification, so no pixelation
			// THE ATLAS HEIGHT IS NOT ROUNDED UP TO A POWER OF TWO (2.0.4). ImGui does that by default,
			// and with the icon faces it nearly doubled the texture for nothing (measured offline at the
			// owner's 34.7 px: English with all three faces 2048x2048 -> 2048x1397, Japanese without any
			// 2048x4096 -> 2048x2857). D3D11 takes any texture height, so this also trims the CJK atlas
			// every Japanese and Chinese player already had.
			io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;
			io.Fonts->Build();

			// Hand the icon faces to the consumer surface and say once per atlas which are in it. Atlas
			// builds are rare (start-up, a language, face or size change, a face first asked for), so
			// this is not a per-frame line.
			{
				std::string faces;
				static constexpr const char* kFaceNames[consumer::kIconFaceCount] = { "solid", "regular", "brands" };
				for (int f = 0; f < consumer::kIconFaceCount; ++f)
				{
					consumer::SetIconFont(f, iconFonts[f]);
					if (!iconFonts[f]) { continue; }
					const int icons = iconFonts[f]->FindGlyphNoFallback(f == consumer::kIconBrands ? 0xF09B : 0xF007) ? 1 : 0;
					faces += (faces.empty() ? "" : ", ") + std::string(kFaceNames[f]) + " (" +
							 std::to_string(iconFonts[f]->Glyphs.Size) + " glyphs" + (icons ? "" : ", NO icon glyphs") + ")";
				}
				if (!faces.empty()) { logger::info("font: Font Awesome icon faces in the atlas: {}", faces); }
			}

			// What the atlas can draw, one probe glyph per script (1.8.9): hiragana A, hangul HAN, the
			// hanzi for water, Cyrillic ZHE. Read back by the driving tool so a language switch is
			// proved by the atlas rather than a capture.
			{
				FontProbe probe;
				probe.language = lang;
				probe.gameLanguage = gameLang;
				probe.glyphs = loaded->Glyphs.Size;
				probe.hasKana = loaded->FindGlyphNoFallback(0x3042) != nullptr;
				probe.hasHangul = loaded->FindGlyphNoFallback(0xD55C) != nullptr;
				probe.hasHanzi = loaded->FindGlyphNoFallback(0x6C34) != nullptr;
				probe.hasCyrillic = loaded->FindGlyphNoFallback(0x0416) != nullptr;
				// GetTexDataAsRGBA32 writes through its pixel pointer unconditionally - a null there is a
				// crash at the first atlas build (boot, 2026-09-17 14:24), not a "skip".
				unsigned char* pixels = nullptr;
				io.Fonts->GetTexDataAsRGBA32(&pixels, &probe.atlasWidth, &probe.atlasHeight);
				std::scoped_lock l(g_fontProbeLock);
				probe.builds = g_fontProbe.builds + 1;
				g_fontProbe = probe;
				logger::info("font: atlas {} built for \"{}\" (game \"{}\"): {} glyphs, {}x{}, kana {} hangul {} hanzi {} cyrillic {}",
					probe.builds, lang, gameLang, probe.glyphs, probe.atlasWidth, probe.atlasHeight, probe.hasKana, probe.hasHangul, probe.hasHanzi, probe.hasCyrillic);
			}
		}

		// -----------------------------------------------------------------------------------
		// Pattern guard (survey non-negotiable): never write a hook over bytes that are not the
		// call instruction we expect. A refused guard means "unsupported runtime, and here is
		// why" in the log, instead of a hard crash in the renderer.
		// -----------------------------------------------------------------------------------
		bool LooksLikeCallSite(std::uintptr_t a_address)
		{
			return REL::make_pattern<"E8">().match(a_address);
		}

		// -----------------------------------------------------------------------------------
		// D3D init - one shot. Original first, then us (survey non-negotiable).
		// -----------------------------------------------------------------------------------
		struct D3DInitHook
		{
			static inline REL::Relocation<void()> func;

			static void thunk()
			{
				func();

				if (g_d3dReady.exchange(true))
				{
					return;  // init ran twice; everything below is once-only
				}

				// This CommonLibSSE-NG's renderer type: RE::BSGraphics::Renderer (RE/R/Renderer.h).
				// BSRenderManager.h exists but is a ZERO-BYTE tombstone - the survey's Wheeler
				// citation predates the rename. Device and context live on RendererData; the
				// swapchain and HWND live on renderWindows[0]. REX::W32 wrapper types are
				// layout-compatible with the native D3D interfaces, hence the reinterpret_casts.
				auto* rendererSingleton = RE::BSGraphics::Renderer::GetSingleton();
				if (!rendererSingleton)
				{
					logger::error("D3DInit fired but BSGraphics::Renderer is null; framework stays inert");
					g_d3dReady = false;
					return;
				}

				auto& data = rendererSingleton->GetRuntimeData();
				auto& window = data.renderWindows[0];

				auto* device = reinterpret_cast<ID3D11Device*>(data.forwarder);
				auto* context = reinterpret_cast<ID3D11DeviceContext*>(data.context);

				// The consumer surface needs the device to decode textures for LoadTexture.
				// Handed over here, at the one moment it is known to be valid.
				consumer::SetDevice(device);
				// The author's own menu art, if any is configured. After SetDevice because the skin
				// decodes through the same cached loader consumer mods use, and that needs the device.
				skin::Reload();
				const HWND hwnd = reinterpret_cast<HWND>(window.hWnd);
				g_gameWindow.store(reinterpret_cast<void*>(hwnd), std::memory_order_release);   // for the watchdog's foreground check

				if (!device || !context || !hwnd || !window.swapChain)
				{
					logger::error("D3DInit: renderer runtime data incomplete (device={}, context={}, hwnd={}, swapChain={}); framework stays inert",
								  static_cast<const void*>(device), static_cast<const void*>(context),
								  static_cast<const void*>(hwnd), static_cast<const void*>(window.swapChain));
					g_d3dReady = false;
					return;
				}

				ImGui::CreateContext();

				// Trickle-off: apply every queued input event in the same NewFrame. With
				// trickling, the Win32 backend's OS-cursor poll and our software cursor could
				// land in DIFFERENT frames and the cursor visibly alternated between the two
				// (the 1.1.0 flicker). All sources resolve within one frame, last-writer wins,
				// and the last writer is always our integrated position.
				ImGui::GetIO().ConfigInputTrickleEventQueue = false;

				// THE WINDOW MOVES BY ITS TITLE BAR AND NOTHING ELSE (the owner, 2026-09-19: "We need to
				// make it so you can't drag AMF by anything but the top bar of the entire menu interface
				// because I can be pointing my cursor at the item in the preview pane and instead move the
				// AMF menu around or move the menu around while moving the item and rotating it").
				//
				// Dear ImGui moves a window when its BODY is dragged, and a mod's page is all body - so a
				// drag meant for a slider, a 3D preview or a row of items moved the framework's window
				// instead. Worse, the move takes the active id on the very frame the button goes down, and
				// while something is active every IsItemHovered() in the frame answers false: a plain
				// left-click on a page's row therefore did nothing at all, which is the other half of the
				// same report. One flag fixes both, for this window and for every consumer's.
				ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;
				// Written to the log so the next test can settle it by reading rather than by feel: this
				// only restricts a window that HAS a title bar, and a page that still drags from its body
				// would mean something else is moving it.
				logger::info("window move: title bar only = {}", ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly);
				// RESIZE FROM ANY EDGE (2.1.1 - Barzing on Nexus, 2026-10-05: "the possibility to resize window also in height
				// size"). Without this only the bottom-right grip resized, and a corner drag keeps the window's shape, so the
				// height could never change on its own. ImGui honours edge resizing only when the backend says it handles
				// mouse cursors; this framework draws its own cursor, so it says so itself.
				ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
				ImGui::GetIO().ConfigWindowsResizeFromEdges = true;

				ImGui_ImplWin32_Init(hwnd);
				ImGui_ImplDX11_Init(device, context);
				arthooks::Install();   // 2.1.6: Appearance > Art's parts drawn in place of ImGui's own shapes
				g_swapChain = reinterpret_cast<IDXGISwapChain*>(window.swapChain);
				g_captureContext = context;

				// Upload the embedded knotwork frame to a texture on the game's device, once.
				// Failure is non-fatal: DrawKnotworkFrame no-ops and the theme still applies its
				// colours, just without the ornament.
				{
					D3D11_TEXTURE2D_DESC td{};
					td.Width = static_cast<UINT>(knotwork::kWidth);
					td.Height = static_cast<UINT>(knotwork::kHeight);
					td.MipLevels = 1;
					td.ArraySize = 1;
					td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
					td.SampleDesc.Count = 1;
					td.Usage = D3D11_USAGE_DEFAULT;
					td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

					D3D11_SUBRESOURCE_DATA sd{};
					sd.pSysMem = knotwork::kRGBA;
					sd.SysMemPitch = static_cast<UINT>(knotwork::kWidth) * 4u;

					ID3D11Texture2D* tex = nullptr;
					if (SUCCEEDED(device->CreateTexture2D(&td, &sd, &tex)) && tex)
					{
						if (FAILED(device->CreateShaderResourceView(tex, nullptr, &g_knotSRV)))
						{
							g_knotSRV = nullptr;
							logger::warn("knotwork: CreateShaderResourceView failed; frame ornament disabled");
						}
						tex->Release();
					}
					else
					{
						logger::warn("knotwork: CreateTexture2D failed; frame ornament disabled");
					}
				}

				theme::Apply();
				// Again now the theme registry exists: the first Reload above ran before theme::Apply
				// had registered anything, so an active theme's own art is only found on this one.
				skin::Reload();

				g_uiScale =window.windowHeight > 0 ? static_cast<float>(window.windowHeight) / 1080.0f : 1.0f;
				if (g_uiScale < 1.0f)
				{
					g_uiScale = 1.0f;  // never shrink below the 1080p baseline
				}

				// Text is rasterised at native size for this display (see BuildFonts) rather than
				// magnifying the built-in bitmap font, which is what made it look pixelated.
				ScanFonts();
				BuildFonts();
				watchdog::Init();
				{ std::string why; watchdog::InstallFastExit(why); }
				ImGui::GetStyle().ScaleAllSizes(g_uiScale);

				ImGui::GetIO().IniFilename = "Data/SKSE/Plugins/ApocryphaMenuFramework_layout.ini";

				logger::info("UI scale set to {:.2f} for a {}px-tall display (1080p baseline)", g_uiScale, window.windowHeight);

				logger::info("ImGui initialized on the game's device (window {}, {}x{}); theme applied",
							 static_cast<const void*>(hwnd), window.windowWidth, window.windowHeight);
			}
		};

		// -----------------------------------------------------------------------------------
		// The framework window: SMF's two-pane structure (design decision, 2026-08-27 - left pane lists
		// the mods' menus, right pane shows the selected menu's settings). M3's registry fills
		// the left pane; until then the framework's own settings page is the only entry.
		// -----------------------------------------------------------------------------------
		// Which pane nav should be moved into on the NEXT frame (0 = leave it alone, 1 = the mod
		// list, 2 = the options). Set when the player pushes across the border; applied by
		// SetNextWindowFocus before that child begins, which also makes ImGui pick a sensible item
		// inside it (the first one, or the one it was last on).
		// Atomic because a driving tool sets it from devbench's listener thread (renderer::FocusPane).
		std::atomic<int> g_focusPane{ 0 };

		// When nav returns to the mod list, put the cursor back on the entry whose page is open -
		// not wherever the list's cursor happened to be left (author, 2026-09-01: "if I select
		// settings and go right and I scroll to the bottom and then I go back left then it should
		// take me back to the settings menu selector not to the bottom of the left pane").
		std::atomic<bool> g_navToSelected{ false };

		// Tab navigation inside the options pane (the author, 2026-09-08: "using the left d pad
		// doesnt move out of the menu until you get to the begining of the tabs otherwise you cant
		// use the lft d pad in the menu except to exit the menu"). A mod with several pages draws a
		// tab bar, and the D-pad now WALKS that bar: left steps back one tab, and only a left press
		// already at the FIRST tab hands nav back to the mod list. Before this the very first left
		// press left the pane, so the twelve sections of a mod like Character Progression Control
		// could not be reached with the D-pad at all - left's only use inside a menu was to leave it.
		//
		// Right steps FORWARD a tab only while the cursor is on the bar itself, and that asymmetry
		// is deliberate. Left was already spent on leaving the pane, so taking it costs nothing;
		// right is still ImGui's own move-between-widgets key down in the page and stays that way.
		// On the bar nothing is lost either: ImGui moves the nav highlight along the tabs but does
		// NOT select the one it lands on - selecting needs an activate press - so all that changes
		// is that the highlight and the selection now move together.
		int  g_tabCount = 0;          // tabs the open mod drew this frame; 0 or 1 = no bar to walk
		int  g_tabIndex = 0;          // which of them is selected - re-read from the bar every frame,
		                              // so a mouse click or the tab-list popup keeps it honest
		int  g_tabRequest = -1;       // tab to force-select on the next frame; -1 = none
		bool g_tabBarHasNav = false;  // the cursor is on the bar itself, not down in the page
		// A page's OWN tab bar, declared by the page itself (AMF_DeclareInnerTabs).
		//
		// The framework can only measure the bar IT submits, so a consumer that draws its own BeginTabBar
		// inside a page is invisible to the nav decision and the D-pad does nothing there (the owner,
		// 2026-09-16: "the nav box behaves properly on the main tabs ... but when going to the other tabs
		// within those tabs, it does not"). It cannot be fixed by guessing from outside, so the page says
		// what it has and reads back which one to open. A mod that never calls this is unaffected - which
		// is the point: no other author has to patch anything.
		int g_innerCount = 0;    // tabs the open page declared THIS frame; 0 = it has none
		int g_innerIndex = 0;    // which of them the page says is open
		int g_innerRequest = -1; // the tab the page should open next frame; -1 = no request
		// 1.8.8: a sideways press inside the content pane is FIRST offered to ImGui's own item navigation,
		// and steps a tab only when ImGui found nothing to move to. The press is noted on the frame it
		// happens (+1 right, -1 left) and decided on the next one, when GImGui->NavJustMovedToId says
		// whether the cursor landed on another widget. The owner, 2026-09-16, in Item Explorer: "dpad
		// right sends you to the favorites tab instead of the add item box" - the page's own widgets sit
		// side by side (SameLine), and the tab step used to win before ImGui had a chance to move.
		int g_pendingTabStep = 0;
		// A right press in the LIST pane, decided one frame late like the content pane's sideways press (the
		// owner, 2026-10-07: on the Mods row, D-pad right "skips past the toggle and sort button and goes to the
		// right pane"): if ImGui moved the highlight to a widget beside it in the list pane, it stays there; a press
		// that moved nothing goes across to the options.
		bool g_pendingSideRight = false;
		bool g_innerFresh = false;  // the declaration was renewed this frame
		// 2.1.5 - Y IN A PAGE GOES UP TO THE MAIN TABS (the owner, 2026-10-07: "when pressing Y, instead of zooming all the way
		// out to the main [left] pane ... it should just send you to the main tabs"). The page had the highlight last frame
		// (the list then leaves Y alone); the focus request is answered by the main tab bar's open tab as it is submitted.
		// 2.1.5 - THE LIST FILTER (the owner, 2026-10-07: "a filter button that when you press it next to sort, it will open
		// up a small context menu where you can type in a word ... whether you want to filter for that item or filter out
		// that item", then "persistent and saved within AMF ... the same way that Mod Organizer 2 does it with a little
		// colored button with a plus minus ... right next to the word"). A saved list of words (settings listFilters), each
		// with a tri-state button as MO2's filter rows cycle (FilterList::cycleItem at v2.5.2: left click Inactive -> Active
		// -> Inverted, right click back): off, green + (show only rows with it), red - (hide rows with it). Matched against
		// the names the player reads. Any + word makes the list flat, as the search does; - alone keeps the separators.
		char g_listFilterEdit[64] = {};   // the popup's "add a word" box

		int NextFilterState(int a_state, int a_dir)   // MO2's cycle: 0 -> +1 -> -1 -> 0 (a_dir 1), the reverse for -1
		{
			static constexpr int kOrder[3] = { 0, 1, -1 };
			int i = 0;
			while (i < 3 && kOrder[i] != a_state) { ++i; }
			return kOrder[((i % 3) + a_dir + 3) % 3];
		}
		bool g_contentNavLastFrame = false;
		bool g_focusMainTabs = false;
		// 2.1.5 - THE HIGHLIGHT FOLLOWS A BUMPER (the owner, 2026-10-07: switching tabs with the shoulder buttons "you can see a
		// slight change in color, but the focus doesn't actually change like it should, where the frame should follow the current
		// selected tab"). The tab a bumper opens takes the highlight as it is submitted - the main bar's (FocusMainTabIfAsked) or
		// a settings sub-tab bar's (SubTabs::Tab). A mod's own tab bar is drawn by the mod, so its tab cannot be focused from here.
		bool g_bumperFocusMain = false;
		bool g_bumperFocusInner = false;
		int g_prevTabIndex = 0;     // the main bar's open tab last frame (g_tabIndex is re-measured from 0 every frame)

		// Right before the main bar's tab number a_index is submitted: the highlight goes onto it when Y asked for that.
		void FocusMainTabIfAsked(int a_index)
		{
			if (g_focusMainTabs && a_index == g_prevTabIndex)
			{
				ImGui::SetKeyboardFocusHere();
				g_focusMainTabs = false;
				logger::debug("nav: Y -> main tab {}", a_index);
			}
			else if (g_bumperFocusMain && a_index == g_tabRequest)
			{
				ImGui::SetKeyboardFocusHere();
				g_bumperFocusMain = false;
			}
		}

		// Where a driving tool's synthetic press lands (amf.menu op=nav). It is read in exactly the
		// place a real D-pad press is read, so the tool exercises this logic rather than a shortcut
		// past it (rule 64). 0 = nothing pending, 1 = left, 2 = right.
		std::atomic<int> g_navRequest{ 0 };

		void DrawMenuListSection();  // defined below, next to the other leaf panes

		// SUB-TABS of a settings tab (2.1.5, the owner: Appearance and MCM menus get "sub tabs ... rather than a long page
		// with collapsible sections"). A tab bar inside the tab, declared to the nav exactly like a mod page's own tabs
		// (DeclareInnerTabs), so the bumpers walk it and a page change keeps the D-pad working. The bar is ended when the
		// object goes, so it must go out of scope before the outer tab's EndTabItem. a_current is kept by the caller.
		class SubTabs
		{
		public:
			SubTabs(const char* a_id, int a_count, int& a_current) : _current(a_current)
			{
				_request = DeclareInnerTabs(a_count, a_current);
				_open = ImGui::BeginTabBar(a_id, ImGuiTabBarFlags_FittingPolicyScroll);
			}
			~SubTabs()
			{
				if (_open) { ImGui::EndTabBar(); }
			}
			SubTabs(const SubTabs&) = delete;
			SubTabs& operator=(const SubTabs&) = delete;

			bool Tab(const char* a_label)
			{
				if (!_open) { return false; }
				const ImGuiTabItemFlags flags = _index == _request ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				if (_index == _request && g_bumperFocusInner)   // the highlight goes with the bumper (see g_bumperFocusMain)
				{
					ImGui::SetKeyboardFocusHere();
					g_bumperFocusInner = false;
				}
				const bool open = ImGui::BeginTabItem(a_label, nullptr, flags);
				if (open) { _current = _index; }
				++_index;
				return open;
			}

		private:
			int& _current;
			int _request = -1;
			int _index = 0;
			bool _open = false;
		};

		// ART (2.1.6, the owner, 2026-10-08: an asset library where a player picks the frame, background and switch from any
		// theme's art "and build their own theme", recoloured by the colour roles). One dropdown per kind of part, kept per
		// theme like the colours: the theme's own, none, or any part in assets/<kind>/. A picture of the part beside each.
		std::string ArtLabel(const std::string& a_name)
		{
			if (a_name == "veldun") { return "Vel'dun"; }   // the theme's own spelling; a file name cannot carry it
			std::string out = a_name;
			for (char& c : out) { if (c == '-' || c == '_') { c = ' '; } }
			if (!out.empty()) { out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0]))); }
			return out;
		}

		void DrawArtPicks()
		{
			using skin::ArtKind;
			auto& values = settings::Get();
			const theme::Palette& active = theme::GetActiveTheme();
			// The part lists are read from disk when the page opens and every few seconds after, not every frame.
			static std::array<std::vector<std::string>, skin::kArtKindCount> s_parts;
			static double s_scannedAt = -1.0;
			const double now = ImGui::GetTime();
			if (s_scannedAt < 0.0 || now - s_scannedAt > 5.0)
			{
				for (std::size_t k = 0; k < s_parts.size(); ++k) { s_parts[k] = skin::ListArt(static_cast<ArtKind>(k)); }
				s_scannedAt = now;
			}
			if (values.skinEnabled && !(values.skinFrame.empty() && values.skinBackground.empty() && values.skinPlates.empty()))
			{
				ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
				ImGui::TextWrapped("%s", TR("AMF_ArtSkinOverride", "Custom menu art from a UI author is on (Theme and text), so its art draws instead of the picks below."));
				ImGui::PopStyleColor();
				ImGui::Spacing();
			}
			// EVERY KIND (ArtKinds.h; the owner, 2026-10-08: the scroll bar, boxes, sliders, switches, the section lines, the
			// on-screen keyboard "and anything else that you can think of"). Five shapes of each, the default one of them: the
			// Skyrim look's own. For the frame that is a part (the knotwork); for the rest it is the built-in shape - no
			// picture - which is what each kind's "no art" entry draws, so that entry is named for it.
			const char* kindLabel[skin::kArtKindCount] = { TR("AMF_ArtFrame", "Frame"), TR("AMF_ArtBackground", "Background"),
				TR("AMF_ArtSwitch", "Switch"), TR("AMF_ArtBox", "Boxes"), TR("AMF_ArtButton", "Buttons"), TR("AMF_ArtTickBox", "Tick boxes"),
				TR("AMF_ArtSlider", "Slider grabs"), TR("AMF_ArtScrollbar", "Scroll bars"), TR("AMF_ArtSection", "Section lines"),
				TR("AMF_ArtTab", "Tabs"), TR("AMF_ArtArrow", "Arrows"), TR("AMF_ArtPopup", "Popups and lists"),
				TR("AMF_ArtHighlight", "Highlight frame"), TR("AMF_ArtCursor", "Mouse pointer") };
			const char* builtIn = TR("AMF_ArtBuiltInDefault", "Built-in (default)");
			const char* noArtLabel[skin::kArtKindCount] = { TR("AMF_ArtNone", "None"), TR("AMF_ArtPlainDefault", "Plain (default)"),
				TR("AMF_ArtRoundedDefault", "Rounded (default)"), builtIn, builtIn, builtIn, builtIn, builtIn, builtIn, builtIn, builtIn,
				TR("AMF_ArtNoneDefault", "None (default)"), TR("AMF_ArtWindowFrameDefault", "Window frame (default)"), builtIn };
			const char* builtInWord = TR("AMF_ArtBuiltInWord", "built-in");
			const char* noArtWord[skin::kArtKindCount] = { TR("AMF_ArtNoneWord", "none"), TR("AMF_ArtPlainWord", "plain"),
				TR("AMF_ArtRoundedWord", "rounded"), builtInWord, builtInWord, builtInWord, builtInWord, builtInWord, builtInWord,
				builtInWord, builtInWord, TR("AMF_ArtNoneWord", "none"), TR("AMF_ArtWindowFrameWord", "window frame"), builtInWord };
			constexpr const char* kDefaultFrame = "skyrim-knotwork";
			// the page's groups, in the order a player thinks of them
			struct Group { const char* title; std::vector<ArtKind> kinds; };
			const Group groups[] = {
				{ TR("AMF_ArtGroupWindow", "Window"), { ArtKind::kFrame, ArtKind::kBackground, ArtKind::kPopup, ArtKind::kHighlight } },
				{ TR("AMF_ArtGroupControls", "Controls"), { ArtKind::kBox, ArtKind::kButton, ArtKind::kTickBox, ArtKind::kToggle,
					ArtKind::kSlider, ArtKind::kScrollbar, ArtKind::kTab, ArtKind::kArrow } },
				{ TR("AMF_ArtGroupLines", "Lines and pointer"), { ArtKind::kSection, ArtKind::kCursor } },
			};
			const float thumb = ImGui::GetFrameHeight() * 1.6f;
			bool changed = false;
			for (const Group& group : groups)
			{
				ImGui::SeparatorText(group.title);
				if (!ImGui::BeginTable("##artkinds", 3, ImGuiTableFlags_SizingFixedFit))
				{
					continue;
				}
				ImGui::TableSetupColumn("##kind", ImGuiTableColumnFlags_WidthFixed);
				ImGui::TableSetupColumn("##pick", ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableSetupColumn("##look", ImGuiTableColumnFlags_WidthFixed, thumb * 2.2f);
				for (const ArtKind kind : group.kinds)
				{
					const std::size_t k = static_cast<std::size_t>(kind);
					const auto& parts = s_parts[k];
					const std::string own = skin::ThemeArt(kind);
					auto found = values.themeArt.find(active.id);
					const std::string pick = found != values.themeArt.end() ? found->second[k] : std::string();
					// entry 0: the theme's own (named), 1: the kind's default / no art, then every part
					std::vector<std::string> labels;
					labels.push_back(std::format("{} ({})", TR("AMF_ArtThemeOwn", "Theme's own"), own.empty() ? std::string(noArtWord[k]) : ArtLabel(own)));
					labels.push_back(noArtLabel[k]);
					int current = 0;
					for (std::size_t i = 0; i < parts.size(); ++i)
					{
						labels.push_back(kind == ArtKind::kFrame && parts[i] == kDefaultFrame
						                     ? std::format("{} ({})", ArtLabel(parts[i]), TR("AMF_ArtDefaultWord", "default"))
						                     : ArtLabel(parts[i]));
						if (pick == parts[i]) { current = static_cast<int>(i) + 2; }
					}
					if (pick == skin::kArtNone) { current = 1; }
					std::vector<const char*> cLabels;
					for (const auto& l : labels) { cLabels.push_back(l.c_str()); }

					ImGui::PushID(static_cast<int>(k));
					ImGui::TableNextRow();
					ImGui::TableNextColumn();
					ImGui::AlignTextToFramePadding();
					ImGui::TextUnformatted(kindLabel[k]);
					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(-FLT_MIN);
					if (theme::ComboTight("##pick", &current, cLabels.data(), static_cast<int>(cLabels.size())))
					{
						auto& slot = values.themeArt[active.id][k];
						slot = current == 0 ? std::string() : current == 1 ? std::string(skin::kArtNone) : parts[static_cast<std::size_t>(current - 2)];
						logger::info("settings page: art {} for theme {} -> \"{}\"", skin::kArtKeys[k] + 1, active.id, slot.empty() ? "theme's own" : slot);
						changed = true;
					}
					// the part drawing now, small, in the colours it takes in the menu
					ImGui::TableNextColumn();
					const std::string shown = skin::ActiveArt(kind);
					ImVec2 size{ 0.0f, 0.0f };
					void* tex = shown.empty() ? nullptr : skin::ArtThumb(kind, shown, &size);
					if (tex && size.x > 0.0f && size.y > 0.0f)
					{
						const float sc = std::min(thumb * 2.0f / size.x, thumb / size.y);
						const ImVec2 at = ImGui::GetCursorScreenPos();
						const ImVec2 to(at.x + size.x * sc, at.y + size.y * sc);
						ImGui::Dummy(ImVec2(thumb * 2.0f, thumb));
						ImDrawList* dl = ImGui::GetWindowDrawList();
						const bool plate = kind == ArtKind::kBox || kind == ArtKind::kButton || kind == ArtKind::kTickBox ||
						                   kind == ArtKind::kSlider || kind == ArtKind::kScrollbar || kind == ArtKind::kTab;
						const bool ink = kind == ArtKind::kSection || kind == ArtKind::kArrow;
						const std::uint32_t artTint = theme::RoleColor(theme::kRoleArt);
						const ImU32 tint = plate ? ImGui::GetColorU32(ImGuiCol_Button) : ink ? ImGui::GetColorU32(ImGuiCol_Text)
						                 : (artTint ? static_cast<ImU32>(artTint) : IM_COL32_WHITE);
						dl->AddImage(reinterpret_cast<ImTextureID>(tex), at, to, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
						if (plate)
						{
							ImVec2 esz{ 0.0f, 0.0f };
							if (void* edge = skin::ArtThumb(kind, shown + "-edge", &esz))
							{
								dl->AddImage(reinterpret_cast<ImTextureID>(edge), at, to, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), ImGui::GetColorU32(ImGuiCol_Border));
							}
						}
					}
					else
					{
						ImGui::AlignTextToFramePadding();
						ImGui::TextDisabled("%s", noArtWord[k]);
					}
					ImGui::PopID();
				}
				ImGui::EndTable();
				ImGui::Spacing();
			}
			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			ImGui::TextWrapped("%s", TR("AMF_ArtOskNote", "The on-screen keyboard follows Buttons for its keys and Frame and Background for its panel."));
			ImGui::PopStyleColor();
			ImGui::Spacing();
			if (ImGui::Button(TR("AMF_ArtReset", "Theme's own art")))
			{
				values.themeArt.erase(active.id);
				logger::info("settings page: art for theme {} -> theme's own", active.id);
				changed = true;
			}
			if (changed)
			{
				settings::Save();
				skin::Reload();
			}
		}

		// COLOURS (2.1.5, the owner, 2026-10-07): the player's own colour for every part of the framework's look - "its frame,
		// box, sliders, and other things", the background ("like how Oathvein is gray, but Norden and Skyrim themes are black"),
		// switches, and the converted MCM pages' headings and help ("blue text or yellow text"). A picker per role for the
		// mouse, a row of preset swatches the D-pad walks, and Theme to go back to the active theme's own colour.
		// THE COLOURS PREVIEW (2.1.5, the owner, 2026-10-07: "the right side of the menu to have a column that displays an example
		// for when you change a color. It has like a toggle and some frame around it and a section heading with some help text
		// below"). Every part a colour role paints, drawn with the colours in use this frame, so a pick shows at once. Its
		// widgets are samples: off the D-pad's path (NoNav) and their state is the preview's own.
		void DrawColorPreview()
		{
			static bool on = true, off = false, tick = true;
			static float slider = 0.6f;
			static int tab = 0;
			ImGui::TextDisabled("%s", TR("AMF_ColorPreview", "Preview"));
			ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_WindowBg));   // the Background colour
			const float pad = ImGui::GetFontSize() * 0.9f;
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
			const bool shown = ImGui::BeginChild("##colourpreview", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
			ImGui::PopStyleVar();
			if (shown)
			{
				ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
				// tabs (Selection and tabs, Boxes)
				if (ImGui::BeginTabBar("##previewtabs"))
				{
					if (ImGui::BeginTabItem(TR("AMF_PreviewTab", "Tab"))) { tab = 0; ImGui::EndTabItem(); }
					if (ImGui::BeginTabItem(TR("AMF_PreviewOtherTab", "Other tab"))) { tab = 1; ImGui::EndTabItem(); }
					ImGui::EndTabBar();
				}
				mcmstyle::Heading(TR("AMF_PreviewHeading", "Section heading"));
				// an option with its value (Text, Secondary text), then its help (Help text)
				ImGui::TextUnformatted(TR("AMF_PreviewOption", "An option"));
				ImGui::SameLine();
				ImGui::TextDisabled("%s", TR("AMF_PreviewValue", "its value"));
				ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextUnformatted(TR("AMF_PreviewHelp", "Help text tells you what the highlighted option does."));
				ImGui::PopTextWrapPos();
				ImGui::PopStyleColor();
				ImGui::Spacing();
				// a switch with the highlight frame round it, as the D-pad's highlight looks (Switch on, Frame art)
				{
					const ImVec2 start = ImGui::GetCursorScreenPos();
					widgets::Toggle(TR("AMF_PreviewOn", "Switched on"), &on);
					// round the switch, as the D-pad's highlight is
					const float h = ImGui::GetFrameHeight();
					DrawHighlightFrame(ImGui::GetWindowDrawList(), start, ImVec2(start.x + h * 2.0f, start.y + h), 1.0f);
				}
				widgets::Toggle(TR("AMF_PreviewOff", "Switched off"), &off);
				ImGui::Checkbox(TR("AMF_PreviewTick", "Tick box"), &tick);
				ImGui::SetNextItemWidth(-FLT_MIN);
				precise::SliderFloat("##previewslider", &slider, 0.0f, 1.0f, "%.2f");   // 2.1.6: one step of the shown digit per nudge, like every slider
				ImGui::Button(TR("AMF_PreviewButton", "Button"));
				ImGui::Spacing();
				// a selected row, and a row as it looks under the mouse (Selection, Hover highlight)
				ImGui::Selectable(TR("AMF_PreviewSelected", "Selected"), true);
				{
					const ImVec2 at = ImGui::GetCursorScreenPos();
					const ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight());
					ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y), ImGui::GetColorU32(ImGuiCol_HeaderHovered));
					ImGui::TextUnformatted(TR("AMF_PreviewHover", "Under the mouse"));
					DrawHighlightFrame(ImGui::GetWindowDrawList(), at, ImVec2(at.x + size.x, at.y + size.y), 0.6f);
				}
				ImGui::PopItemFlag();
			}
			ImGui::EndChild();
			ImGui::PopStyleColor();
			(void)tab;
		}

		void DrawColorRoles()
		{
			// the ACTIVE theme's own picks: each theme keeps its changes, like a preset (the owner, 2026-10-07)
			auto& colors = theme::PlayerColors();
			auto& values = settings::Get();
			// SHOW CHANGES EVERYWHERE STRAIGHT AWAY (the owner, 2026-10-08: "a little toggle ... whether you want your color
			// changes to apply only to the preview or to the whole framework instantaneously ... then you can press a button to
			// apply it to your current theme"). Off, picks go into a draft only the preview draws in, until Apply. The draft
			// follows the applied picks while nothing is pending, and starts again from them on a theme change.
			static std::array<std::string, theme::kRoleCount> s_draft;
			static std::string s_draftTheme;
			static bool s_pending = false;
			const std::string themeId = theme::GetActiveTheme().id;
			if (s_draftTheme != themeId) { s_pending = false; s_draftTheme = themeId; }
			if (!s_pending) { s_draft = colors; }
			if (widgets::Toggle(TR("AMF_ColorApplyNow", "Show changes everywhere straight away"), &values.colorsApplyNow))
			{
				// switched on with changes waiting: they go on, as the switch now says
				if (values.colorsApplyNow && s_pending) { colors = s_draft; s_pending = false; theme::Apply(); }
				logger::info("settings page: colours apply straight away -> {}", values.colorsApplyNow);
				settings::Save();
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("%s", TR("AMF_ColorApplyNowTip", "On: each colour you pick changes the whole menu at once. Off: picks show only in the preview until you press Apply."));
			}
			// APPLY, RIGHT BESIDE THE SWITCH (the owner, 2026-10-08: "default to ... the preview only and then just have a button
			// right next to it that says apply which will apply it to your theme"). Live once anything is waiting.
			{
				ImGui::SameLine();
				ImGui::BeginDisabled(values.colorsApplyNow || !s_pending);
				if (ImGui::Button(TR("AMF_ColorApply", "Apply")))
				{
					colors = s_draft;
					s_pending = false;
					logger::info("settings page: preview colours applied to {}", themeId);
					settings::Save();
					theme::Apply();
				}
				ImGui::SameLine();
				if (ImGui::Button(TR("AMF_ColorDiscard", "Discard")))
				{
					s_pending = false;
					s_draft = colors;
					logger::info("settings page: preview colours discarded");
				}
				ImGui::EndDisabled();
			}
			const bool live = values.colorsApplyNow;
			auto& picks = live ? colors : s_draft;
			const auto commit = [&]() {
				if (live) { settings::Save(); theme::Apply(); }
				else { s_pending = picks != colors; }
			};

			struct Role { const char* key; const char* fallback; int role; };
			static constexpr Role kRoles[] = {
				{ "AMF_ColorBackground", "Background", theme::kRoleBackground },
				{ "AMF_ColorBorder", "Frame lines and borders", theme::kRoleBorder },
				{ "AMF_ColorArt", "Frame art", theme::kRoleArt },
				{ "AMF_ColorBoxes", "Boxes and buttons", theme::kRoleBoxes },
				{ "AMF_ColorText", "Text", theme::kRoleText },
				{ "AMF_ColorTextDim", "Secondary text", theme::kRoleTextDim },
				{ "AMF_ColorAccent", "Selection and tabs", theme::kRoleAccent },
				{ "AMF_ColorSlider", "Sliders and tick marks", theme::kRoleSlider },
				{ "AMF_ColorSwitchOn", "Switch on", theme::kRoleSwitchOn },
				{ "AMF_ColorSwitchOff", "Switch off", theme::kRoleSwitchOff },
				{ "AMF_ColorHeading", "Section headings", theme::kRoleHeading },
				{ "AMF_ColorHelp", "Help text", theme::kRoleHelp },
				{ "AMF_ColorHover", "Hover highlight", theme::kRoleHover } };
			// Presets: black, dark grey, grey, off-white, gold, yellow, orange, red, green, light blue, blue, purple.
			static constexpr std::uint32_t kSwatches[] = { 0x000000, 0x333333, 0x9A9A9A, 0xF5F2E9, 0xD8C27A, 0xF0E070,
														   0xE8A050, 0xD86A6A, 0x8CC88C, 0x9FC8E8, 0x6A9FE0, 0xB89AE0 };
			auto setColor = [&](int a_role, std::uint32_t a_rgb, const char* a_name) {
				char hex[8];
				snprintf(hex, sizeof(hex), "#%06X", a_rgb & 0xFFFFFF);
				picks[a_role] = hex;
				logger::info("settings page: {} colour ({}{}) -> {}", a_name, themeId, live ? "" : ", preview only", picks[a_role]);
				commit();
			};
			const float swatch = ImGui::GetFrameHeight() * 0.8f;
			// the role list on the left, a live sample of every part on the right (DrawColorPreview)
			const bool table = ImGui::BeginTable("##colourpage", 2, ImGuiTableFlags_None);
			if (table)
			{
				ImGui::TableSetupColumn("##roles", ImGuiTableColumnFlags_WidthStretch, 1.35f);
				ImGui::TableSetupColumn("##preview", ImGuiTableColumnFlags_WidthStretch, 1.0f);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
			}
			for (const Role& r : kRoles)
			{
				ImGui::PushID(r.role);
				// what draws now: the player's, else the theme's - or, previewing, what the draft would draw
				const std::uint32_t abgr = live ? theme::RoleColor(r.role) : theme::PicksRoleColor(picks, r.role);
				float col[3] = { (abgr & 0xFF) / 255.0f, ((abgr >> 8) & 0xFF) / 255.0f, ((abgr >> 16) & 0xFF) / 255.0f };
				if (ImGui::ColorEdit3(TR(r.key, r.fallback), col, ImGuiColorEditFlags_NoInputs))
				{
					const auto c8 = [](float f) { return static_cast<std::uint32_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
					setColor(r.role, (c8(col[0]) << 16) | (c8(col[1]) << 8) | c8(col[2]), r.fallback);
				}
				ImGui::SameLine();
				if (!picks[r.role].empty())
				{
					if (ImGui::SmallButton(TR("AMF_ColorTheme", "Theme")))
					{
						picks[r.role].clear();
						logger::info("settings page: {} colour -> the theme's{}", r.fallback, live ? "" : " (preview only)");
						commit();
					}
				}
				else { ImGui::TextDisabled("%s", TR("AMF_ColorFromTheme", "(the theme's)")); }
				for (int i = 0; i < static_cast<int>(std::size(kSwatches)); ++i)
				{
					if (i > 0) { ImGui::SameLine(); }
					ImGui::PushID(100 + i);
					const std::uint32_t s = kSwatches[i];
					const ImVec4 sv{ ((s >> 16) & 0xFF) / 255.0f, ((s >> 8) & 0xFF) / 255.0f, (s & 0xFF) / 255.0f, 1.0f };
					if (ImGui::ColorButton("##swatch", sv, ImGuiColorEditFlags_NoTooltip, ImVec2(swatch, swatch))) { setColor(r.role, s, r.fallback); }
					ImGui::PopID();
				}
				ImGui::PopID();
			}
			bool any = false;
			for (const auto& c : picks) { any = any || !c.empty(); }
			if (!any) { ImGui::BeginDisabled(); }
			if (ImGui::Button(TR("AMF_ColorAllTheme", "All back to the theme")))
			{
				for (auto& c : picks) { c.clear(); }
				logger::info("settings page: every colour -> the theme's{}", live ? "" : " (preview only)");
				commit();
			}
			if (!any) { ImGui::EndDisabled(); }
			// previewing: the picks wait here until Apply puts them on this theme, or Discard drops them
			if (!live)
			{
				if (s_pending)
				{
					ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
					ImGui::PushTextWrapPos(0.0f);
					ImGui::TextUnformatted(TR("AMF_ColorPending", "Not applied yet - only the preview shows these colours."));
					ImGui::PopTextWrapPos();
					ImGui::PopStyleColor();
				}
			}
			if (table)
			{
				ImGui::TableSetColumnIndex(1);
				if (!live) { theme::BeginPreviewColors(s_draft); }
				DrawColorPreview();
				if (!live) { theme::EndPreviewColors(); }
				ImGui::EndTable();
			}
		}

		void DrawFrameworkSettingsPane()
		{
			auto& values = settings::Get();

			ImGui::TextUnformatted(TR("AMF_FrameworkSettings", "Framework Settings"));
			ImGui::Separator();
			ImGui::Spacing();

			// TABS BY AREA (the owner, 2026-10-05: "divide the AMF settings page into several tabs that are divided by their
			// area that they affect"). Same tab mechanics as Controls and Help, so the pad walks them the same way.
			if (!ImGui::BeginTabBar("##settingstabs", ImGuiTabBarFlags_FittingPolicyScroll)) { return; }
			int index = 0;
			const auto tab = [&](const char* a_label) {
				const ImGuiTabItemFlags flags = (index == g_tabRequest) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				FocusMainTabIfAsked(index);   // 2.1.5: Y in a page lands here
				const bool open = ImGui::BeginTabItem(a_label, nullptr, flags);
				if (ImGui::IsItemFocused()) { g_tabBarHasNav = true; }
				if (open) { g_tabIndex = index; }
				++index;
				return open;
			};

			// GENERAL: how the menu starts, opens, pauses and exits, and how it is driven
			if (tab(TR("AMF_TabGeneral", "General")))
			{
				// THE SYSTEM ROW IS A SETTING, NOT AN INSTALL-TIME CHOICE (author, 2026-09-04: "we can
				// just have one version and not a fomod"). It shipped briefly as a FOMOD fork, which
				// made a reversible preference into something you had to reinstall to change - and
				// forked the documentation, the INI and the support answers along with it. One build,
				// one INI, and the choice lives here where it can be changed and changed back.
				if (widgets::Toggle(TR("AMF_BlackCurtain", "Black screen until the main menu is ready"), &values.startupCurtain))
				{
					logger::info("settings page: startup curtain -> {}", values.startupCurtain);
					settings::Save();
					if (!values.startupCurtain)
					{
						// Turning it off while the curtain is still up must give the screen back NOW, not at
						// the next launch - otherwise the one control that fixes a stuck curtain is behind it.
						curtain::Lift("turned off from the settings page");
					}
				}
				ImGui::TextWrapped("%s", TR("AMF_BlackCurtainHelp", "On: the screen is held black from the first frame the game "
								   "draws until its main menu is up, so the logo frames and the half-drawn menu behind it are never "
								   "shown. It lifts the moment play begins, or after a couple of minutes if the main menu never "
								   "appears, so a slow start can never leave you looking at nothing."));
				ImGui::Spacing();

				if (widgets::Toggle(TR("AMF_PauseGame", "Pause the game while this menu is open"), &values.pauseGameWhileOpen))
				{
					logger::info("settings page: pause the game while open -> {}", values.pauseGameWhileOpen);
					settings::Save();
				}
				ImGui::TextWrapped("%s", TR("AMF_PauseGameHelp", "On: time stops while this menu is open, the way it does in the game's own "
								   "menus - nothing moves, fights or ticks down behind it. Off: the game keeps running while you change settings."));
				ImGui::Spacing();

				if (widgets::Toggle(TR("AMF_FastExit", "Fast exit - end the process the moment the game exits"), &values.fastExit))
				{
					logger::info("settings page: fast exit -> {}", values.fastExit);
					settings::Save();
				}
				if (widgets::Toggle(TR("AMF_SystemRow", "Mod settings in the game's System menu"), &values.systemMenuRow))
				{
					logger::info("settings page: system menu row -> {}", values.systemMenuRow);
					settings::Save();
				}
				ImGui::TextWrapped("%s", TR("AMF_SystemRowHelp2", "On: a SKSE MENUS row is added to the journal's System tab, beside SAVE, "
								   "LOAD and SETTINGS, and opens this menu in the middle of the screen. "
								   "The row is added to the menu as it opens rather than by replacing any "
								   "game file, so it works with whatever menu artwork you have installed. "
								   "Off: the game's menu is left completely untouched and this menu is "
								   "reached by its key alone."));
				ImGui::TextDisabled("%s", TR("AMF_TakesEffectJournal", "Takes effect the next time the journal is opened."));
				ImGui::Spacing();

				// THE ON-SCREEN KEYBOARD (1.8.9, the owner, 2026-09-18): a framework feature, so every mod's
				// search box gets it; a toggle here, drawn at the bottom of the screen, never over the page.
				if (widgets::Toggle(TR("AMF_OnScreenKeyboard", "On-screen keyboard for controllers"), &values.onScreenKeyboard))
				{
					logger::info("settings page: on-screen keyboard -> {}", values.onScreenKeyboard);
					settings::Save();
				}
				ImGui::TextWrapped("%s", TR("AMF_OnScreenKeyboardHelp", "On: highlight any text box on a mod's page with the D-pad and press A, "
								   "and a key grid appears across the bottom of the screen. The D-pad walks the keys, A types one, B puts "
								   "the highlight back on the box, X is shift and Y is backspace. It works in every mod's page. Off: text "
								   "boxes take a real keyboard only."));
				ImGui::Spacing();

				// INPUT MODE IS DETECTED, AND IS NOT A SETTING (author, 2026-09-04: "I want the auto
				// detection feature built-in with no toggle and there doesn't need to be a controller
				// toggle anymore"). There were two switches here - one holding the mode, one deciding
				// whether the detector was allowed to write it - which is two controls describing one
				// fact the game already knows, and they could be left disagreeing with reality. The
				// detector's reading is now simply used, and shown, so it can still be judged while
				// playing rather than taken on trust.
				ImGui::TextUnformatted(TR("AMF_Navigation", "Navigation"));
				ImGui::TextWrapped("%s", TR("AMF_NavigationHelp", "Follows whatever you last used: press a key or move the mouse for "
								   "keyboard navigation (arrow keys, Enter, Escape), touch the pad for "
								   "controller navigation (D-pad moves, A activates, B cancels)."));
				{
					const input::Device device = input::LastDevice();
					const float since = input::SecondsSinceLastDevice();
					const char* name = device == input::Device::kGamepad ? TR("AMF_DevController", "controller")
									 : device == input::Device::kKeyboardMouse ? TR("AMF_DevKeyboard", "keyboard and mouse")
									 : TR("AMF_DevNone", "nothing yet");
					if (since < 0.0f) { ImGui::TextDisabled(TR("AMF_Detected", "Detected: %s"), name); }
					else { ImGui::TextDisabled(TR("AMF_DetectedAgo", "Detected: %s (%.1fs ago)"), name, since); }
				}
				ImGui::Spacing();
				ImGui::Spacing();

				// The menu key is set in ONE place, Controls > Open and close the menu (the owner, 2026-10-05: "there's duplicate
				// entries for the menus toggle key ... There should just be one"). The Rebind that sat here went in 2.1.1, with
				// the "Window position: Centre" line beside it, a stub that offered nothing to set.

				// WINDOW SIZE (2026-10-02: one centred window for both ways of opening - see the window code). The size is all
				// that is remembered now; this is the way back to the default.
				{
					auto& v = settings::Get();
					const bool anySet = v.nestedWindow.IsSet() || v.hotkeyWindow.IsSet();
					ImGui::TextUnformatted(TR("AMF_WindowSize", "Window size"));
					ImGui::TextWrapped("%s", TR("AMF_WindowSizeHelp", "The menu sits in the middle of the screen, from its key and from the "
									   "System menu row alike, and opens wide enough to show the menu names and the page in full. Drag "
									   "its edges to resize it; the size you choose is kept."));
					ImGui::BeginDisabled(!anySet);
					if (ImGui::Button(TR("AMF_ResetSize", "Reset to the default size")))
					{
						v.nestedWindow.Clear();
						v.hotkeyWindow.Clear();
						settings::Save();
						g_applyGeometry.store(true, std::memory_order_release);
						logger::info("settings page: window size reset to the default");
					}
					ImGui::EndDisabled();
					if (!anySet)
					{
						ImGui::SameLine();
						ImGui::TextDisabled("%s", TR("AMF_SizeDefault", "(at the default size)"));
					}
				}
				ImGui::Spacing();
				ImGui::Spacing();

				// The persistence-channel test harness (decisions doc S10) that sat here at trace level is gone (the owner,
				// 2026-10-07: "get rid of the persistence test at the bottom of the settings page"); the channel itself
				// (Persistence.h) is unchanged.

				ImGui::EndTabItem();
			}

			// APPEARANCE: theme, font, language, text size, custom art
			if (tab(TR("AMF_TabAppearance", "Appearance")))
			{
				// APPEARANCE ALWAYS APPLIES, in both installs (corrected 2026-09-04). These settings
				// used to be hidden whenever the System row was on, under the belief that a nested
				// surface would wear the game's own menu artwork and so have nothing to theme. It does
				// not: nesting changes GEOMETRY only - the window is fitted to the journal panel around
				// it - and every pixel inside that rectangle is still drawn by this framework, in this
				// theme. Hiding the controls left the one install that most needs them unable to reach
				// them, and told the player something untrue about their own menu while doing it.

				// SUB-TABS (2.1.5, the owner: "sub tabs for it rather than a long page with collapsible sections").
				{
					static int s_appearanceSub = 0;
					SubTabs sub("##appearancesub", 4, s_appearanceSub);
					if (sub.Tab(TR("AMF_SubThemeText", "Theme and text")))
					{
					// Theme picker (design decision, 2026-08-27) - supersedes the original "no theme UI by design"
					// stance; the registry is additive (theme::Theme.h), never overwriting an entry.
					const std::vector<theme::Palette> themes = theme::ListThemes();
					const theme::Palette& active = theme::GetActiveTheme();

					int currentIndex = 0;
					std::vector<const char*> names;
					names.reserve(themes.size());
					for (std::size_t i = 0; i < themes.size(); ++i)
					{
						names.push_back(themes[i].name.c_str());
						if (themes[i].id == active.id)
						{
							currentIndex = static_cast<int>(i);
						}
					}

					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
					if (theme::ComboTight(TR("AMF_Theme", "Theme"), &currentIndex, names.data(), static_cast<int>(names.size())))
					{
						theme::SetActiveTheme(themes[currentIndex].id);
						theme::Apply();
						skin::Reload();   // the new theme's own frame and background, if it has any
						values.themeId = themes[currentIndex].id;
						settings::Save();
					}
					ImGui::TextWrapped("%s", TR("AMF_ThemeHelp", "\"Skyrim\" is the knotwork look - the Nordic frame with silver and gold "
									   "lines. \"Untarnished\" is the framework's original identity: the same "
									   "layout with clean lines and no frame art."));
		
					ImGui::Spacing();

					// FONT picker - separate from the theme on purpose (the author): the theme decides colours,
					// this decides the letterforms, and the two combine freely.
					{
						int current = 0;
						std::vector<const char*> labels;
						labels.reserve(g_fontChoices.size());
						for (std::size_t i = 0; i < g_fontChoices.size(); ++i)
						{
							labels.push_back(g_fontChoices[i].label.c_str());
							if (g_fontChoices[i].path == values.fontPath) { current = static_cast<int>(i); }
						}
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
						if (!labels.empty() && theme::ComboTight(TR("AMF_Font", "Font"), &current, labels.data(), static_cast<int>(labels.size())))
						{
							values.fontPath = g_fontChoices[current].path;
							settings::Save();
							g_fontRebuildPending = true;  // re-rasterise in the new face
							logger::info("settings page: font -> \"{}\" ({})",
										 g_fontChoices[current].label,
										 values.fontPath.empty() ? "auto" : values.fontPath.c_str());
						}
						ImGui::TextWrapped("%s", TR("AMF_FontHelp", "Drop a .ttf into Data/SKSE/Plugins/ApocryphaMenuFramework/fonts "
										   "to add it to this list."));
					}
					ImGui::Spacing();
					ImGui::Spacing();

					// LANGUAGE (1.6.4): which translation file the framework's own text comes from. "Game
					// language" follows the game's sLanguage; a named entry forces that file (the INI's
					// sLanguage). Changing it reloads the strings and rebuilds the atlas for the new glyphs.
					{
						static std::vector<std::string> s_langs;
						static double s_scannedAt = -1.0;
						const double now = ImGui::GetTime();
						if (s_scannedAt < 0.0 || now - s_scannedAt > 5.0) { s_langs = strings::Available(); s_scannedAt = now; }
						std::vector<std::string> labels;
						labels.push_back(std::string(TR("AMF_LanguageAuto", "Game language")) + " (" + strings::Language() + ")");
						for (const auto& l : s_langs) { std::string t = l; if (!t.empty()) { t[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(t[0]))); } labels.push_back(t); }
						std::vector<const char*> cLabels;
						for (const auto& l : labels) { cLabels.push_back(l.c_str()); }
						int current = 0;
						const std::string& forced = settings::Get().language;
						for (std::size_t i = 0; i < s_langs.size(); ++i) { if (!forced.empty() && s_langs[i] == forced) { current = static_cast<int>(i) + 1; } }
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
						if (theme::ComboTight(TR("AMF_Language", "Language"), &current, cLabels.data(), static_cast<int>(cLabels.size())))
						{
							strings::SetLanguage(current == 0 ? "" : s_langs[static_cast<std::size_t>(current - 1)]);
						}
						ImGui::TextWrapped("%s", TR("AMF_LanguageHelp", "The framework's own text. Game language follows Skyrim's setting; pick one to force it. "
										   "Each mod's own page is translated by that mod. Translation files: Data/Interface/Translations/ApocryphaMenuFramework_<language>.txt."));
					}
					ImGui::Spacing();
					ImGui::Spacing();

					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
					if (precise::SliderFloat(TR("AMF_TextSize", "Text size"), &values.textScale, 1.0f, 2.0f, "%.2f"))
					{
						// applied live via FontGlobalScale each frame
					}
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						logger::info("settings page: text scale -> {:.2f}", values.textScale);
						settings::Save();
						g_fontRebuildPending = true;  // re-rasterise at the new size rather than stretch
					}
					ImGui::TextWrapped("%s", TR("AMF_TextSizeHelp", "Extra text scaling on top of the automatic resolution scale."));
					ImGui::Spacing();
					ImGui::Spacing();
					// CUSTOM MENU ART IS OFF UNLESS ASKED FOR (the owner, 2026-09-10: "i dont want the custom
					// menu art to be visible ... there needs to be a way to not have it on at all times").
					// The switch is here as well as in the INI so a player can turn it off without editing a
					// file, and turning it off reloads at once rather than at the next launch.
					if (widgets::Toggle(TR("AMF_SkinEnabled", "Custom menu art from a UI author"), &values.skinEnabled))
					{
						logger::info("settings page: custom menu art -> {}", values.skinEnabled);
						settings::Save();
						skin::Reload();
					}
					ImGui::TextWrapped("%s", TR("AMF_SkinEnabledHelp", "Off: the menu keeps its built-in look, whatever is set under [Skin] in the "
									   "INI. On: the frame, background and toggle switch are replaced by the PNGs a UI "
									   "author has pointed the framework at. Leave this off unless you have installed "
									   "artwork made for it."));
						ImGui::EndTabItem();
					}
					if (sub.Tab(TR("AMF_SubWindow", "Window")))
					{
					// THE WINDOW, THREE SWITCHES (2.1.1 - Barzing on Nexus, 2026-10-05: resize "also in height", "move the window",
					// "the semi transparence of the window"; the owner: "seperate toggles" ... "in apperance teb"). Each ON by default
					// (the owner: "have it default to on, along with the other settings we just added"); See-through starts at
					// 100% opacity, so it looks solid until the slider is lowered.
					if (widgets::Toggle(TR("AMF_MovableWindow", "Move the window"), &values.movableWindow))
					{
						logger::info("settings page: move the window -> {}", values.movableWindow);
						settings::Save();
						if (!values.movableWindow) { g_applyGeometry.store(true, std::memory_order_release); }   // back to the centre
					}
					ImGui::TextWrapped("%s", TR("AMF_MovableWindowHelp", "On: drag the top row - the name and version - to move the "
						"menu, and it opens where you left it. Off: it sits in the middle of the screen."));
					if (widgets::Toggle(TR("AMF_FreeResize", "Resize the window"), &values.freeResize))
					{
						logger::info("settings page: resize the window -> {}", values.freeResize);
						settings::Save();
					}
					ImGui::TextWrapped("%s", TR("AMF_FreeResizeHelp", "On: drag any edge or corner to resize the menu - height "
						"and width alike - and the size is kept. Off: the size is fixed."));
					if (widgets::Toggle(TR("AMF_SeeThrough", "See-through window"), &values.seeThrough))
					{
						logger::info("settings page: see-through window -> {}", values.seeThrough);
						settings::Save();
						theme::Apply();
					}
					ImGui::TextWrapped("%s", TR("AMF_SeeThroughHelp", "On: the opacity below fades the menu's background so the game "
						"shows through. Off: the background is solid."));
					ImGui::BeginDisabled(!values.seeThrough);
					ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
					if (precise::SliderInt(TR("AMF_WindowOpacity", "Window opacity"), &values.windowOpacity, 5, 100, "%d%%"))
					{
						theme::Apply();   // live while dragging
					}
					if (ImGui::IsItemDeactivatedAfterEdit())
					{
						logger::info("settings page: window opacity -> {}%", values.windowOpacity);
						settings::Save();
					}
					ImGui::TextWrapped("%s", TR("AMF_WindowOpacityHelp", "How solid the menu is: 100% is solid, lower lets the game show through. The black background fades the most, boxes and borders less, text least; right-click menus stay solid."));
					ImGui::EndDisabled();
					// 2.1.5 (the owner, 2026-10-07): the help bar, with "a toggle just in case anybody doesn't like" it.
					if (widgets::Toggle(TR("AMF_HelpBar", "Help bar"), &values.helpBar))
					{
						logger::info("settings page: help bar -> {}", values.helpBar);
						settings::Save();
					}
					ImGui::TextWrapped("%s", TR("AMF_HelpBarHelp", "On: the help for the highlighted option on a converted MCM page "
						"shows in a bar under the right pane, inside the menu, like SkyUI's info line. Off: it shows as a popup beside "
						"the option."));
						ImGui::EndTabItem();
					}
					// 2.1.5: the player's own colours over the theme's, kept per theme (DrawColorRoles).
					if (sub.Tab(TR("AMF_Colors", "Colours")))
					{
						ImGui::TextWrapped("%s", TR("AMF_ColorsHelp", "Your own colour for each part of the menu, over the theme's. Each starts as "
							"the theme's own; Theme puts one back. Frame art tints the theme's frame and background pictures."));
						DrawColorRoles();
						ImGui::EndTabItem();
					}
					// 2.1.6: the theme's art parts, swapped for any theme's, kept per theme (DrawArtPicks).
					if (sub.Tab(TR("AMF_SubArt", "Art")))
					{
						ImGui::TextWrapped("%s", TR("AMF_ArtHelp", "Build your own look from every theme's art: pick each part of the menu for "
							"this theme - five shapes of each, the default among them. Each theme keeps its own picks, and they take your Colours."));
						ImGui::Spacing();
						DrawArtPicks();
						ImGui::EndTabItem();
					}
				}
				ImGui::Spacing();
				ImGui::EndTabItem();
			}

			// MCM MENUS: which MCM menus come in, and SkyUI's own list
			// 2.1.6: "Converted menus" (the owner, 2026-10-08, the FLICK plan's Q1) - the pages AMF brings in from other menu
			// systems: MCM menus, and FLICK mods' pages. A new key, so no language keeps the old "MCM menus".
			if (tab(TR("AMF_TabConverted", "Converted menus")))
			{
				const bool anyMcm = values.loadMcmHelperConfigs || values.loadSkyUIScriptMenus;
				// SUB-TABS (2.1.5, the owner: "I want the MCM menus tab to have sub tabs ... rather than a long page with collapsible
				// sections"): where the menus come from, which ones, what is remembered, and how converted pages are spaced.
				{
					static int s_mcmSub = 0;
					SubTabs sub("##mcmsub", 5, s_mcmSub);
					if (sub.Tab(TR("AMF_SubMcmSources", "Menus")))
					{
					// The MCM loader's switches (the owner, 2026-10-04): MCM Helper menus, SkyUI script menus, SkyUI's list.
					if (widgets::Toggle(TR("AMF_McmLoad", "MCM Helper menus here"), &values.loadMcmHelperConfigs))
					{
						logger::info("settings page: MCM Helper menus -> {}", values.loadMcmHelperConfigs);
						settings::Save();
						mcmloader::SetEnabled(values.loadMcmHelperConfigs);
					}
					ImGui::TextWrapped("%s", TR("AMF_McmLoadHelp", "On: every mod that uses MCM Helper also gets its menu here, read from "
									   "the mod's own files. A change goes through MCM Helper exactly as it does in SkyUI's menu."));
					// phase 3: SkyUI menus written only in Papyrus (the owner, 2026-10-04: "do phase 3 for the SkyUI script menus")
					if (widgets::Toggle(TR("AMF_McmScripts", "SkyUI script menus here"), &values.loadSkyUIScriptMenus))
					{
						logger::info("settings page: SkyUI script menus -> {}", values.loadSkyUIScriptMenus);
						settings::Save();
						mcmloader::SetScriptsEnabled(values.loadSkyUIScriptMenus);
					}
					ImGui::TextWrapped("%s", TR("AMF_McmScriptsHelp", "On: a mod whose SkyUI menu is written only in its own script also gets "
									   "its menu here. This menu makes the same calls on that script as SkyUI's menu does."));
					if (!anyMcm) { ImGui::BeginDisabled(); }
					if (widgets::Toggle(TR("AMF_McmHideSkyUI", "Take those mods out of SkyUI's MCM list"), &values.hideMcmInSkyUI))
					{
						logger::info("settings page: hide MCM mods in SkyUI -> {}", values.hideMcmInSkyUI);
						settings::Save();
						mcmloader::SetHideInSkyUI(values.hideMcmInSkyUI);
					}
					ImGui::TextWrapped("%s", TR("AMF_McmHideSkyUIHelp", "On: a mod whose whole menu is drawn here is set in one place - "
									   "here. A mod with anything this menu cannot draw stays in SkyUI's list. Off puts them all back."));
					ImGui::TextDisabled(TR("AMF_McmHideSkyUICount", "%d of %d hidden from SkyUI's list now"),
						mcmloader::HiddenInSkyUI(), mcmloader::HideableInSkyUI());
					if (mcmloader::SkyUIListUnreadable())
					{
						ImGui::TextWrapped("%s", TR("AMF_McmHideUnreadable", "SkyUI's MCM list is run by another mod in a way this menu cannot read, "
										   "so this switch takes nothing out of it here."));
					}
					if (!anyMcm) { ImGui::EndDisabled(); }
						ImGui::EndTabItem();
					}
					if (sub.Tab(TR("AMF_SubMcmChoose", "Choose menus")))
					{
						if (!anyMcm) { ImGui::TextDisabled("%s", TR("AMF_McmNoneOn", "Switch on MCM Helper menus or SkyUI script menus on the Menus tab first.")); }
					// Which MCM menus come into this menu (xLenax, 2026-10-04: "an option to choose which MCMs I'd like to import
					// instead of importing all of them or None" - with about 200 of them). Folded by default; a filter for long lists.
					if (anyMcm)
					{
						const std::vector<mcmloader::ImportRow> rows = mcmloader::ImportList();
						const int imported = static_cast<int>(std::count_if(rows.begin(), rows.end(), [](const mcmloader::ImportRow& r) { return r.imported; }));
						char header[192];
						snprintf(header, sizeof(header), TR("AMF_McmImportHeader", "Choose which MCM menus appear here (%d of %d)"), imported, static_cast<int>(rows.size()));
						// One menu at a time from SkyUI (the owner, 2026-10-05: "a drop down box next to import from Sky UI that shows any
						// currently Sky UI owned menus that AMF doesn't already own"): the menus left to SkyUI only; picking one brings it
						// in, the same as switching it on in the list below.
						{
							const bool anyLeft = imported < static_cast<int>(rows.size());
							ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
							if (!anyLeft) { ImGui::BeginDisabled(); }
							if (theme::BeginComboTight(TR("AMF_McmBringIn", "Bring in from SkyUI"),
									anyLeft ? TR("AMF_McmBringInPick", "Pick a menu...") : TR("AMF_McmBringInNone", "Every MCM menu is already here")))
							{
								for (const auto& r : rows)
								{
									if (r.imported) { continue; }
									ImGui::PushID(r.key.c_str());
									if (ImGui::Selectable(personalization::ShownEntryName(r.entry).c_str()))
									{
										logger::info("settings page: '{}' brought in from SkyUI", r.entry);
										mcmloader::SetMenuImported(r.key, true);
									}
									ImGui::PopID();
								}
								ImGui::EndCombo();
							}
							if (!anyLeft) { ImGui::EndDisabled(); }
						}
						ImGui::TextUnformatted(header);
						{
							ImGui::TextWrapped("%s", TR("AMF_McmImportHelp", "Switch off a menu to keep it in SkyUI's menu only: it leaves this menu, "
											   "and taking menus out of SkyUI's list leaves it alone."));
							if (widgets::Toggle(TR("AMF_McmImportNew", "Bring in MCM menus not switched below"), &values.importNewMcmMenus))
							{
								logger::info("settings page: bring in MCM menus not chosen by hand -> {}", values.importNewMcmMenus);
								settings::Save();
								mcmloader::SetImportNew(values.importNewMcmMenus);
							}
							ImGui::TextWrapped("%s", TR("AMF_McmImportNewHelp", "Off: only the menus switched on below come in - with a long list, "
											   "start from none and pick the few you use."));
							if (ImGui::Button(TR("AMF_McmImportAllOn", "All on")))
							{
								for (const auto& r : rows) { if (!r.imported) { mcmloader::SetMenuImported(r.key, true); } }
							}
							ImGui::SameLine();
							if (ImGui::Button(TR("AMF_McmImportAllOff", "All off")))
							{
								for (const auto& r : rows) { if (r.imported) { mcmloader::SetMenuImported(r.key, false); } }
							}
							ImGui::SameLine();
							ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
							ImGui::InputTextWithHint("##mcmimportfilter", TR("AMF_McmImportFilter", "Filter by name"), g_mcmImportFilter, sizeof(g_mcmImportFilter));
							keyboard::NoteTextField(ImGui::GetItemID());
							if (rows.empty())
							{
								ImGui::TextDisabled("%s", TR("AMF_McmImportNone", "No MCM menus found yet - menus written only in a script appear once a game is loaded."));
							}
							std::string needle = g_mcmImportFilter;
							std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
							for (const auto& r : rows)
							{
								std::string name = r.entry;
								std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
								if (!needle.empty() && name.find(needle) == std::string::npos) { continue; }
								ImGui::PushID(r.key.c_str());
								bool on = r.imported;
								if (widgets::Toggle(personalization::ShownEntryName(r.entry).c_str(), &on)) { mcmloader::SetMenuImported(r.key, on); }
								ImGui::SameLine();
								ImGui::TextDisabled("%s", r.script ? TR("AMF_McmKindScript", "(script menu)") : TR("AMF_McmKindHelper", "(MCM Helper)"));
								ImGui::PopID();
							}
						}
					}
						ImGui::EndTabItem();
					}
					if (sub.Tab(TR("AMF_McmMemTitle", "Remembered settings")))
					{
						if (!anyMcm) { ImGui::TextDisabled("%s", TR("AMF_McmNoneOn", "Switch on MCM Helper menus or SkyUI script menus on the Menus tab first.")); }
					if (anyMcm)
					{
						// REMEMBERED SETTINGS (the owner, 2026-10-06: "build it into AMF so that the settings you change for all these
						// different MCMs are backed up and saved so that on a new game they still apply"). RememberedSettings.h.
						ImGui::TextWrapped("%s", TR("AMF_McmMemHelp", "A new game forgets what menus written in a mod's script, and MCM Helper "
										   "settings kept in your save, were set to. Those settings are remembered here and set again after a "
										   "new game. MCM Helper's other settings are kept by MCM Helper itself."));
						if (widgets::Toggle(TR("AMF_McmMemAuto", "Remember each change made here"), &values.mcmAutoBackup))
						{
							logger::info("settings page: remembered settings: automatic backup -> {}", values.mcmAutoBackup);
							settings::Save();
						}
						if (widgets::Toggle(TR("AMF_McmMemOnNewGame", "Set them again after a new game"), &values.mcmRestoreOnNewGame))
						{
							logger::info("settings page: remembered settings: restore on a new game -> {}", values.mcmRestoreOnNewGame);
							settings::Save();
						}

						const bool memBusy = rememberedsettings::Busy();
						if (memBusy) { ImGui::BeginDisabled(); }
						// the profile in use, a new one (empty, or a copy of this one), and deleting another
						const std::string memActive = rememberedsettings::ActiveProfile();
						const auto memProfiles = rememberedsettings::Profiles();
						ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
						if (theme::BeginComboTight(TR("AMF_McmMemProfile", "Profile"), memActive.c_str()))
						{
							for (const auto& name : memProfiles)
							{
								if (ImGui::Selectable(name.c_str(), name == memActive) && name != memActive) { rememberedsettings::SwitchProfile(name); }
							}
							ImGui::EndCombo();
						}
						ImGui::SameLine();
						ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
						ImGui::InputTextWithHint("##memprofile", TR("AMF_McmMemNewName", "New profile name"), g_memProfileName, sizeof(g_memProfileName));
						keyboard::NoteTextField(ImGui::GetItemID());
						const bool memNamed = g_memProfileName[0] != '\0';
						if (!memNamed) { ImGui::BeginDisabled(); }
						ImGui::SameLine();
						if (ImGui::Button(TR("AMF_McmMemNew", "New")))
						{
							g_memStatus = rememberedsettings::CreateProfile(g_memProfileName, false) ? "" : TR("AMF_McmMemExists", "A profile with that name exists already.");
							g_memProfileName[0] = '\0';
						}
						ImGui::SameLine();
						if (ImGui::Button(TR("AMF_McmMemCopy", "Copy this one")))
						{
							g_memStatus = rememberedsettings::CreateProfile(g_memProfileName, true) ? "" : TR("AMF_McmMemExists", "A profile with that name exists already.");
							g_memProfileName[0] = '\0';
						}
						if (!memNamed) { ImGui::EndDisabled(); }
						{
							const bool others = memProfiles.size() > 1;
							ImGui::SameLine();
							ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
							if (!others) { ImGui::BeginDisabled(); }
							if (theme::BeginComboTight("##memdelete", TR("AMF_McmMemDelete", "Delete a profile...")))
							{
								for (const auto& name : memProfiles)
								{
									if (name != memActive && ImGui::Selectable(name.c_str())) { rememberedsettings::DeleteProfile(name); }
								}
								ImGui::EndCombo();
							}
							if (!others) { ImGui::EndDisabled(); }
						}

						if (ImGui::Button(TR("AMF_McmMemBackUp", "Back up all now")))
						{
							g_memStatus = rememberedsettings::BackUpAll() ? "" : TR("AMF_McmMemWait", "Close the MCM page that is open, or wait for the backup or restore that is running.");
						}
						ImGui::SameLine();
						if (ImGui::Button(TR("AMF_McmMemRestore", "Restore now")))
						{
							g_memStatus = rememberedsettings::RestoreNow() ? "" : TR("AMF_McmMemWait", "Close the MCM page that is open, or wait for the backup or restore that is running.");
						}
						if (memBusy) { ImGui::EndDisabled(); }
						ImGui::TextWrapped("%s", TR("AMF_McmMemButtonsHelp", "Back up all now reads every menu this menu can read - also what you "
										   "set in SkyUI's own menu. Restore now sets this game's menus to the profile."));

						// IMPORT FROM MCM MEMORY (2.1.5, the owner: "the whole point ... is so that they can import their settings and
						// then deactivate MCM memory"). Shown when that mod has a saved profile; merged into the active profile (his
						// choice). Named on this button by his choice; the feature itself stays "Remembered settings".
						{
							static std::vector<std::string> s_mmProfiles;
							static double s_mmScannedAt = -10.0;
							if (ImGui::GetTime() - s_mmScannedAt > 5.0) { s_mmProfiles = rememberedsettings::McmMemoryProfiles(); s_mmScannedAt = ImGui::GetTime(); }
							static int s_mmPick = 0;
							if (!s_mmProfiles.empty())
							{
								ImGui::Spacing();
								s_mmPick = std::clamp(s_mmPick, 0, static_cast<int>(s_mmProfiles.size()) - 1);
								if (memBusy) { ImGui::BeginDisabled(); }
								ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
								if (theme::BeginComboTight(TR("AMF_McmImportProfile", "MCM Memory profile"), s_mmProfiles[static_cast<std::size_t>(s_mmPick)].c_str()))
								{
									for (int i = 0; i < static_cast<int>(s_mmProfiles.size()); ++i)
									{
										if (ImGui::Selectable(s_mmProfiles[static_cast<std::size_t>(i)].c_str(), i == s_mmPick)) { s_mmPick = i; }
									}
									ImGui::EndCombo();
								}
								ImGui::SameLine();
								if (ImGui::Button(TR("AMF_McmImport", "Import from MCM Memory")))
								{
									g_memStatus.clear();
									rememberedsettings::ImportFromMcmMemory(s_mmProfiles[static_cast<std::size_t>(s_mmPick)]);
								}
								if (memBusy) { ImGui::EndDisabled(); }
								ImGui::TextWrapped("%s", TR("AMF_McmMemImportHelp", "Brings every setting MCM Memory saved in that profile into this "
									"profile, so you can switch MCM Memory off afterwards: AMF sets them again after each new game. Its files "
									"are only read, never changed."));
							}
							if (rememberedsettings::McmMemoryAutoRestoreOn())
							{
								ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
								ImGui::TextWrapped("%s %s", icons::kWarning, TR("AMF_McmMemBothOn", "MCM Memory's automatic restore is on as well, so after a new "
									"game both set the same menus. Once your settings are imported, switch MCM Memory off."));
								ImGui::PopStyleColor();
							}
						}

						const std::string memLast = rememberedsettings::LastResult();
						if (!g_memStatus.empty()) { ImGui::TextWrapped("%s", g_memStatus.c_str()); }
						else if (!memLast.empty())
						{
							ImGui::PushTextWrapPos(0.0f);
							ImGui::TextDisabled("%s", memLast.c_str());
							ImGui::PopTextWrapPos();
						}

						const auto memRows = rememberedsettings::Menus();
						char memHeader[160];
						snprintf(memHeader, sizeof(memHeader), TR("AMF_McmMemMenus", "Menus in this profile (%d)"), static_cast<int>(memRows.size()));
						ImGui::Spacing();
						ImGui::TextUnformatted(memHeader);
						{
							ImGui::TextWrapped("%s", TR("AMF_McmMemMenusHelp", "Switch off a menu to leave it out of the automatic restore after a "
											   "new game; Forget drops what this profile remembers of it."));
							for (const auto& row : memRows)
							{
								ImGui::PushID(row.key.c_str());
								bool on = row.autoRestore;
								if (widgets::Toggle(personalization::ShownEntryName(row.entry).c_str(), &on)) { rememberedsettings::SetAutoRestore(row.key, on); }
								ImGui::SameLine();
								if (row.present) { ImGui::TextDisabled(TR("AMF_McmMemSaved", "%d saved"), row.saved); }
								else { ImGui::TextDisabled(TR("AMF_McmMemAbsent", "%d saved - not in this game"), row.saved); }
								if (row.saved > 0)
								{
									ImGui::SameLine();
									if (ImGui::SmallButton(TR("AMF_McmMemForget", "Forget"))) { rememberedsettings::Forget(row.key); }
								}
								ImGui::PopID();
							}
						}
					}
						ImGui::EndTabItem();
					}
					if (sub.Tab(TR("AMF_SubMcmSpacing", "Spacing")))
					{
					// SPACING OF CONVERTED PAGES (2.1.5, the owner, 2026-10-07: a player "didn't like the spacing of the generated menus
					// for some of them like Atlas map markers which are very close together"). Their colours are framework-wide, on
					// Appearance > Colours (the owner: "some of these features may be redundant ... if we just move them over").
					{
						ImGui::TextWrapped("%s", TR("AMF_McmLookHelp", "The room between the columns and rows of the pages built from MCM "
										   "menus. Their colours are on Appearance > Colours."));
						ImGui::Spacing();
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
						precise::SliderInt(TR("AMF_McmColumnGap", "Gap between columns"), &values.mcmColumnGap, 0, 200, "%d%%");
						if (ImGui::IsItemDeactivatedAfterEdit())
						{
							logger::info("settings page: converted pages' column gap -> {}%", values.mcmColumnGap);
							settings::Save();
						}
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
						precise::SliderInt(TR("AMF_McmRowSpacing", "Space between rows"), &values.mcmRowSpacing, 0, 100, "%d%%");
						if (ImGui::IsItemDeactivatedAfterEdit())
						{
							logger::info("settings page: converted pages' row spacing -> {}%", values.mcmRowSpacing);
							settings::Save();
						}
					}
						ImGui::EndTabItem();
					}
					// 2.1.6: FLICK - the mods written for FLICK whose pages AMF draws (FlickHost.h)
					if (sub.Tab(TR("AMF_SubFlick", "FLICK")))
					{
						if (widgets::Toggle(TR("AMF_FlickHost", "FLICK mods' pages here"), &values.flickHost))
						{
							logger::info("settings page: FLICK host -> {} (applies after a restart)", values.flickHost);
							settings::Save();
						}
						ImGui::TextWrapped("%s", TR("AMF_FlickHostHelp", "On: mods written for FLICK show their settings page in this menu, "
							"with (FLICK) after the name, and are reached only through it - there is no separate FLICK window, key or "
							"pause-menu row. Off: FLICK mods are left alone. A change applies after you restart the game."));
						if (values.flickHost != flick::Enabled())
						{
							ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
							ImGui::TextWrapped("%s", TR("AMF_FlickRestart", "Restart the game for this to take effect."));
							ImGui::PopStyleColor();
						}
						ImGui::Spacing();
						if (flick::RealFlickInstalled())
						{
							ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
							ImGui::TextWrapped("%s", TR("AMF_FlickRealInstalled", "FLICK itself (FUCK.dll) is installed beside this menu. This menu "
								"holds the FLICK mods, so FLICK's own menu stays empty; disable FLICK in your mod manager to remove its row from the pause menu."));
							ImGui::PopStyleColor();
							ImGui::Spacing();
						}
						// one line per FLICK mod (its plugin), its pages after it; a mod whose FLICK copy is hidden because it has its
						// own page here says so
						std::vector<std::pair<std::string, std::vector<flick::ToolInfo>>> mods;
						for (std::size_t i = 0, n = flick::ToolCount(); i < n; ++i)
						{
							flick::ToolInfo t = flick::ToolAt(i);
							auto it = std::find_if(mods.begin(), mods.end(), [&t](const auto& m) { return m.first == t.plugin; });
							if (it == mods.end()) { mods.emplace_back(t.plugin, std::vector<flick::ToolInfo>{}); it = std::prev(mods.end()); }
							it->second.push_back(std::move(t));
						}
						if (mods.empty())
						{
							ImGui::TextDisabled("%s", TR("AMF_FlickNone", "No FLICK mods have connected."));
						}
						else
						{
							ImGui::SeparatorText(std::format("{} ({})", TR("AMF_FlickConnected", "FLICK mods here"), mods.size()).c_str());
							for (const auto& [plugin, pages] : mods)
							{
								const std::string& shown = pages.front().group.empty() ? pages.front().name : pages.front().group;
								ImGui::BulletText("%s", shown.c_str());
								ImGui::SameLine();
								ImGui::TextDisabled("(%s)", plugin.c_str());
								if (pages.size() > 1 || pages.front().name != shown)
								{
									ImGui::Indent();
									for (const auto& p : pages) { ImGui::TextDisabled("%s", p.name.c_str()); }
									ImGui::Unindent();
								}
								if (!pages.front().listed)
								{
									ImGui::Indent();
									ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
									ImGui::TextWrapped("%s", TR("AMF_FlickDuplicate", "It also has its own page here, so its FLICK copy is not listed."));
									ImGui::PopStyleColor();
									ImGui::Unindent();
								}
							}
						}
						ImGui::EndTabItem();
					}
					// 2.1.6: Prisma - the Prisma MCM Redux menus AMF draws (PrismaRedux.h), with the three-way choice every converted
					// menu system gets (the owner, 2026-10-08: "Same as sky ui, optional to host them only or make them held only by
					// amf or move control back out of amf and into prisma")
					if (sub.Tab(TR("AMF_SubPrisma", "Prisma")))
					{
						if (!prisma::ReduxLoaded())
						{
							ImGui::TextWrapped("%s", TR("AMF_PrismaNotLoaded", "Prisma MCM Redux is not installed. When it is, the menus of the mods "
								"that use it show here, with (Prisma) after the name."));
						}
						else
						{
							const char* modes[] = { TR("AMF_PrismaBoth", "Here and in Prisma's own window"), TR("AMF_PrismaAmfOnly", "Here only"),
								TR("AMF_PrismaOnly", "Prisma's own window only") };
							int mode = std::clamp(values.prismaControl, 0, 2);
							ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
							if (theme::BeginComboTight(TR("AMF_PrismaControl", "Prisma MCM Redux menus"), modes[mode]))
							{
								for (int i = 0; i < 3; ++i)
								{
									if (ImGui::Selectable(modes[i], i == mode) && i != mode)
									{
										values.prismaControl = i;
										settings::Save();
										prisma::ApplyControl();
										logger::info("settings page: Prisma MCM Redux menus -> {}", i);
									}
								}
								ImGui::EndCombo();
							}
							ImGui::TextWrapped("%s", TR("AMF_PrismaControlHelp", "Here and in Prisma's own window: the menus are in this menu, and "
								"Prisma MCM Redux's own window still opens with its key. Here only: its key is switched off from the next game start, "
								"and put back when you choose another option. Prisma's own window only: nothing of Prisma's is listed here."));
							if (prisma::HotkeyHeldByAmf())
							{
								ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
								ImGui::TextWrapped("%s", TR("AMF_PrismaKeyOff", "Prisma MCM Redux's own key is switched off."));
								ImGui::PopStyleColor();
							}
							ImGui::Spacing();
							const auto rows = prisma::Configs();
							if (rows.empty())
							{
								ImGui::TextDisabled("%s", TR("AMF_PrismaNone", "No mod has a Prisma MCM Redux menu."));
							}
							else
							{
								ImGui::SeparatorText(std::format("{} ({})", TR("AMF_PrismaMenus", "Prisma menus"), rows.size()).c_str());
								if (values.prismaControl == 2) { ImGui::BeginDisabled(); }
								for (const auto& r : rows)
								{
									bool on = r.imported;
									ImGui::PushID(r.key.c_str());
									if (widgets::Toggle(personalization::ShownEntryName(r.entry).c_str(), &on)) { prisma::SetImported(r.key, on); }
									if (r.duplicate)
									{
										ImGui::Indent();
										ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
										ImGui::TextWrapped("%s", TR("AMF_PrismaDuplicate", "It also has its own or an MCM page here, so its Prisma copy is not listed."));
										ImGui::PopStyleColor();
										ImGui::Unindent();
									}
									ImGui::PopID();
								}
								if (values.prismaControl == 2) { ImGui::EndDisabled(); }
							}
						}
						ImGui::Spacing();
						ImGui::TextDisabled("%s", TR("AMF_PrismaPmcm", "PMCM menus (the other Prisma settings system) are web pages of their own and "
							"stay in PMCM."));
						ImGui::EndTabItem();
					}
				}
				ImGui::Spacing();
				ImGui::EndTabItem();
			}

			// MENU LIST: sorting into categories, the list's order, names and separators, layout presets
			if (tab(TR("AMF_MenuList", "Menu list")))
			{
				// The sort sits at the top of the page (the owner, 2026-10-05: "I think this sort button should be at the top").
				if (values.loadMcmHelperConfigs || values.loadSkyUIScriptMenus)
				{
					// Sort them into categories (the owner, 2026-10-05: "an auto sort function which sorted the imported menus
					// into categories, sort of like our mod manager plugin but built into AMF"). Only rearranges; undoable.
					// One button only ("I just want it to add a button that does it"): "re-sort all" stays a DevBench op.
					if (ImGui::Button(TR("AMF_McmSort", "Sort MCM menus into categories")))
					{
						const auto r = mcmloader::SortIntoCategories(false);
						g_mcmSortStatus = FormatSortStatus(r);
					}
					if (mcmloader::CanRestoreBeforeSort())
					{
						ImGui::SameLine();
						if (ImGui::Button(TR("AMF_McmSortUndo", "Undo the sort")))
						{
							g_mcmSortStatus = mcmloader::RestoreBeforeSort() ? TR("AMF_McmSortUndone", "The menu list is back to its order from before the sort.")
																			 : TR("AMF_McmSortUndoFailed", "The order from before the sort could not be read.");
						}
					}
					ImGui::TextWrapped("%s", TR("AMF_McmSortHelp", "Puts each MCM menu in this menu under a separator for its kind - Interface, "
									   "Combat, Camera and so on - judged by its name; a menu with no match goes under Other. Nothing is hidden "
									   "or removed. A menu you already put under a separator stays there. Undo puts "
									   "the list back as it was before the last sort."));
					ImGui::TextWrapped("%s", TR("AMF_McmSortLearnHelp", "Move a menu under another category's separator yourself and "
										   "the sort remembers it: the next sort puts it there too."));
					if (!g_mcmSortStatus.empty()) { ImGui::TextDisabled("%s", g_mcmSortStatus.c_str()); }
					ImGui::Spacing();
				}

				DrawMenuListSection();
				ImGui::EndTabItem();
			}
			g_tabCount = index;     // the bumpers walk these tabs, as on Controls and Help
			g_tabRequest = -1; g_bumperFocusMain = false;   // a requested tab (and its highlight) is taken once, not every frame
			ImGui::EndTabBar();
		}

		// ---- Separators in the side list (the owner, 2026-10-02 - MO2's separators) --------------------------------------
		// Creating one names it at once: the rename modal opens on it with the default name filled in.
		void BeginNewSeparator(const std::vector<registry::Entry>& a_entries, const std::string& a_beforeName)
		{
			const char* name = TR("AMF_SeparatorDefaultName", "New separator");
			g_renameTarget = personalization::AddSeparator(a_entries, a_beforeName, name);
			std::snprintf(g_renameBuffer, sizeof(g_renameBuffer), "%s", name);
			g_renameOpenPending = true;
			settings::Save();
		}

		// A separator row: a fold arrow, the name, how many menus it holds when folded, a white box when pinned. A (or a
		// click) folds and unfolds it; Y (or a right-click) opens its menu - the same binding a mod row uses.
		// GRAB AND MOVE (the owner, 2026-10-02). The mod picked up with the grab action, empty when none is.
		std::string g_grabbedMod;

		// The move actions bound to a stick direction are polled rather than raised: one step as the stick is pushed,
		// then a step every 0.12 s while it is held past a 0.35 s pause. A move bound to a button or key is raised
		// like any command instead and steps once per press. Called every frame so a held stick is not mistaken for
		// a fresh push the moment a mod is picked up.
		int GrabStickStep()
		{
			static int s_lastDir = 0;
			static double s_nextAt = 0.0;
			const auto held = [](bindings::Action a_action) {
				const bindings::Binding b = bindings::Get(a_action);
				if (b.padKind != bindings::PadKind::kStickDir) { return false; }
				float x = 0.0f, y = 0.0f;
				bool clicked = false, live = false;
				input::GetStick((b.padCode >> 4) & 0xF, x, y, clicked, live);
				constexpr float kPush = 0.5f;   // y > 0 is up, as for the left stick's navigation
				switch (b.padCode & 0xF)
				{
				case 0: return y > kPush;
				case 1: return y < -kPush;
				case 2: return x < -kPush;
				case 3: return x > kPush;
				default: return false;
				}
			};
			const int dir = held(bindings::Action::kGrabUp) ? -1 : (held(bindings::Action::kGrabDown) ? 1 : 0);
			const double now = ImGui::GetTime();
			int step = 0;
			if (dir != 0 && dir != s_lastDir) { step = dir; s_nextAt = now + 0.35; }
			else if (dir != 0 && now >= s_nextAt) { step = dir; s_nextAt = now + 0.12; }
			s_lastDir = dir;
			return step;
		}

		void DrawSeparatorRow(const std::vector<registry::Entry>& a_entries, const personalization::DisplayEntry& a_row,
							  bool a_contextMenu, bool a_favouriteKey)
		{
			ImGui::PushID(a_row.modName.c_str());
			const bool favourite = personalization::IsFavourite(a_row.modName);
			const float boxSide = ImGui::GetFontSize() * 0.55f;
			const float gutter = boxSide + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
			const ImVec2 rowTopLeft = ImGui::GetCursorScreenPos();

			// the name (and, folded, how many menus it holds) is the row's one label - one nav stop; the fold arrow is drawn
			// in front of it, ImGui's own tree arrow (down when open, right when folded)
			char label[160];
			if (a_row.collapsed) { std::snprintf(label, sizeof(label), "%s  (%d)", a_row.displayName.c_str(), a_row.children); }
			else { std::snprintf(label, sizeof(label), "%s", a_row.displayName.c_str()); }
			const float arrowRoom = ImGui::GetFontSize() * 1.1f;
			const ImVec2 arrowAt(rowTopLeft.x + gutter, rowTopLeft.y);
			ImGui::Indent(gutter + arrowRoom);
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			// "###sep": one ID whether folded or open. The "(n)" in the label made it a different item on every fold, so the
			// highlight held an ID that no longer existed and the next A did nothing until the player moved off and back (the
			// owner, 2026-10-07: after Fold, a separator "will not let me unfold it until I select a different separator").
			std::strncat(label, "###sep", sizeof(label) - std::strlen(label) - 1);
			const bool picked = ImGui::Selectable(label, false);
			ImGui::PopStyleColor();
			ImGui::Unindent(gutter + arrowRoom);
			ImGui::RenderArrow(ImGui::GetWindowDrawList(), arrowAt, ImGui::GetColorU32(ImGuiCol_Text),
							   a_row.collapsed ? ImGuiDir_Right : ImGuiDir_Down, 0.8f);
			// a hairline under the separator, from the end of its name to the pane's edge
			{
				const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
				const float y = (mn.y + mx.y) * 0.5f;
				const float x0 = mn.x + ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().ItemSpacing.x;
				if (x0 < mx.x) { ImGui::GetWindowDrawList()->AddLine(ImVec2(x0, y), ImVec2(mx.x, y), ImGui::GetColorU32(ImGuiCol_Separator), 1.0f); }
			}
			if (favourite)
			{
				const float top = rowTopLeft.y + (ImGui::GetTextLineHeight() - boxSide) * 0.5f;
				const float left = rowTopLeft.x + ImGui::GetStyle().ItemInnerSpacing.x * 0.5f;
				ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(left, top), ImVec2(left + boxSide, top + boxSide), IM_COL32(255, 255, 255, 255));
			}
			if (picked)
			{
				personalization::ToggleCollapsed(a_row.modName);
				settings::Save();
			}
			if (ImGui::IsItemFocused())
			{
				if (a_contextMenu) { ImGui::OpenPopup("##sepctx"); }
				if (a_favouriteKey)
				{
					personalization::ToggleFavourite(a_row.modName);
					settings::Save();
				}
			}
			const float ctxPad = ImGui::GetFontSize() * 0.35f;
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ctxPad, ctxPad));
			const bool ctxOpen = ImGui::BeginPopupContextItem("##sepctx");
			ImGui::PopStyleVar();
			if (ctxOpen)
			{
				if (ImGui::MenuItem(a_row.collapsed ? TR("AMF_SeparatorExpand", "Expand") : TR("AMF_SeparatorCollapse", "Collapse")))
				{
					personalization::ToggleCollapsed(a_row.modName);
					settings::Save();
				}
				if (ImGui::MenuItem(favourite ? TR("AMF_Unfavourite", "Remove from favourites") : TR("AMF_Favourite", "Add to favourites")))
				{
					personalization::ToggleFavourite(a_row.modName);
					settings::Save();
				}
				if (ImGui::MenuItem(TR("AMF_Rename", "Rename...")))
				{
					g_renameTarget = a_row.modName;
					std::snprintf(g_renameBuffer, sizeof(g_renameBuffer), "%s", a_row.displayName.c_str());
					g_renameOpenPending = true;
				}
				ImGui::Separator();
				if (ImGui::MenuItem(TR("AMF_NewSeparatorAbove", "New separator above")))
				{
					BeginNewSeparator(a_entries, a_row.modName);
				}
				if (ImGui::MenuItem(TR("AMF_MoveToTop", "Move to the top")))
				{
					personalization::MoveTo(a_entries, a_row.modName, 1);
					settings::Save();
				}
				ImGui::Separator();
				if (ImGui::MenuItem(TR("AMF_SeparatorDelete", "Delete separator")))
				{
					personalization::RemoveSeparator(a_row.modName);   // its menus join the separator above
					settings::Save();
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}

		// ---- Menu list: rename and reorder (author verdict 2026-09-01) ----------------------
		// Presentation only - the registry and the mods themselves are untouched. Numbering is
		// insert-and-shift: type a position and every other entry re-flows around it, so nobody
		// has to number the whole list by hand.
		void DrawMenuListSection()
		{
			const std::vector<registry::Entry> entries = registry::Snapshot();
			ImGui::SeparatorText(TR("AMF_MenuList", "Menu list"));
			ImGui::TextWrapped("%s", TR("AMF_MenuListHelp", "Rename any mod's entry, and set the order of the list. Type a position "
							   "number to move an entry there - every other entry re-flows around it."));

			ImGui::Text(TR("AMF_Order", "Order: %s"), personalization::IsCustomOrder() ? TR("AMF_OrderCustom", "custom") : TR("AMF_OrderAlphabetical", "alphabetical"));
			ImGui::SameLine();
			if (ImGui::Button(TR("AMF_ResetAlphabetical", "Reset to alphabetical")))
			{
				personalization::ResetToAlphabetical();
				settings::Save();
			}
			ImGui::SameLine();
			// Asked for directly (the owner, 2026-09-10). Every edit above already writes the file
			// on commit, so this is the reassurance that the list on screen is the list on disk -
			// and the way out if a field was left mid-edit.
			if (ImGui::Button(TR("AMF_SaveMenuList", "Save menu list")))
			{
				settings::Save();
				g_menuListSavedAt = ImGui::GetTime();
			}
			if (g_menuListSavedAt > 0.0 && ImGui::GetTime() - g_menuListSavedAt < 3.0)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", TR("AMF_MenuListSaved", "saved"));
			}

			// Layout presets (2.0.3, xLenax via the owner, 2026-10-02): the list's order, separators, favourites and
			// renames saved under a name, to load back later or keep a second arrangement. Every preset can be deleted
			// (the owner's standing rule for presets).
			ImGui::Spacing();
			ImGui::TextUnformatted(TR("AMF_LayoutPresets", "Layout presets"));
			ImGui::TextWrapped("%s", TR("AMF_LayoutPresetsHelp", "Save this list - its order, separators, favourites and names - "
							   "under a name, and load it back any time. Your settings are kept in a file the download never "
							   "contains, so an update does not reset them."));
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
			ImGui::InputTextWithHint("##presetName", TR("AMF_PresetNameHint", "Preset name"), g_presetName, sizeof(g_presetName));
			keyboard::NoteTextField(ImGui::GetItemID());
			ImGui::SameLine();
			if (ImGui::Button(TR("AMF_SavePreset", "Save as preset")))
			{
				const std::string name = g_presetName;
				g_presetStatus = settings::SaveLayoutPreset(name) ? TR("AMF_PresetSaved", "Saved.") : TR("AMF_PresetNotSaved", "Not saved - type a name first.");
				g_presetStatusAt = ImGui::GetTime();
			}
			const std::vector<std::string> presets = settings::ListLayoutPresets();
			if (presets.empty())
			{
				ImGui::TextDisabled("%s", TR("AMF_NoPresets", "No presets saved yet."));
			}
			for (const std::string& preset : presets)
			{
				ImGui::PushID(preset.c_str());
				ImGui::BulletText("%s", preset.c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton(TR("AMF_LoadPreset", "Load")))
				{
					g_presetStatus = settings::LoadLayoutPreset(preset) ? TR("AMF_PresetLoaded", "Loaded.") : TR("AMF_PresetNotLoaded", "Could not be read - see the log.");
					g_presetStatusAt = ImGui::GetTime();
				}
				ImGui::SameLine();
				if (ImGui::SmallButton(TR("AMF_DeletePreset", "Delete")))
				{
					g_presetStatus = settings::DeleteLayoutPreset(preset) ? TR("AMF_PresetDeleted", "Deleted.") : TR("AMF_PresetNotDeleted", "Could not be deleted - see the log.");
					g_presetStatusAt = ImGui::GetTime();
				}
				ImGui::PopID();
			}
			if (!g_presetStatus.empty() && ImGui::GetTime() - g_presetStatusAt < 4.0)
			{
				ImGui::TextDisabled("%s", g_presetStatus.c_str());
			}
			ImGui::Spacing();

			if (entries.empty())
			{
				ImGui::TextDisabled("%s", TR("AMF_NoMods", "No mods have registered a page yet."));
				return;
			}

			const std::vector<personalization::DisplayEntry> rows = personalization::Order(entries);
			static std::unordered_map<std::string, std::array<char, 64>> aliasBuffers;

			// Rows with something to show: an entry whose every page is hidden (an MCM loader switched off, or a menu left out
			// of AMF) has no row here either (xLenax, 2026-10-04: they "still appear in the settings page, just not in the
			// actual Menu"). It keeps its place in the saved order; the number shown is its place among the rows shown, and a
			// number typed is mapped back to the whole order before the move.
			auto allPagesHidden = [&](const personalization::DisplayEntry& r) {
				if (r.separator || r.registryIndex < 0 || r.registryIndex >= static_cast<int>(entries.size())) { return false; }
				const auto& pages = entries[r.registryIndex].pages;
				return !pages.empty() && std::all_of(pages.begin(), pages.end(), [](const registry::Page& p) { return p.hidden; });
			};
			std::vector<int> shownRows;  // indices into rows
			for (int i = 0; i < static_cast<int>(rows.size()); ++i)
			{
				if (!allPagesHidden(rows[i])) { shownRows.push_back(i); }
			}

			// A reorder requested this frame, applied AFTER the table closes.
			//
			// It used to call personalization::MoveTo() inline, in the middle of the loop that is
			// still walking `rows`. MoveTo rewrites the global order immediately, so every row drawn
			// after the commit was laid out against a sequence that no longer matched - the widget
			// ids are pushed from the mod NAME while the committed value is compared against the row
			// INDEX (`position != i + 1`), and after a move those two refer to different entries.
			// A player could move two or three and then found the fields stopped responding
			// (xLenax, 2026-09-11). Deferring keeps the frame's layout consistent with the sequence
			// it was built from, and applies exactly one move per frame.
			std::string pendingMoveMod;
			int pendingMovePosition = 0;

			if (ImGui::BeginTable("##menulist", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.2f);
				ImGui::TableSetupColumn(TR("AMF_ColMod", "Mod"));
				ImGui::TableSetupColumn(TR("AMF_ColShowsAs", "Shows as"));
				ImGui::TableHeadersRow();

				for (int shownIndex = 0; shownIndex < static_cast<int>(shownRows.size()); ++shownIndex)
				{
					const int i = shownRows[shownIndex];
					const personalization::DisplayEntry& row = rows[i];
					ImGui::TableNextRow();
					ImGui::PushID(row.modName.c_str());

					ImGui::TableSetColumnIndex(0);
					int position = shownIndex + 1;
					ImGui::SetNextItemWidth(-FLT_MIN);
					// Commit on Enter OR on losing focus. EnterReturnsTrue alone meant that typing a
					// position and then clicking away threw the number away without a word, which
					// is why reordering appeared not to save at all (the owner, 2026-09-10). The
					// alias field beside this one already committed both ways; now they match.
					const bool posEntered = ImGui::InputInt("##pos", &position, 0, 0,
															ImGuiInputTextFlags_EnterReturnsTrue);
					keyboard::NoteTextField(ImGui::GetItemID());
					if ((posEntered || ImGui::IsItemDeactivatedAfterEdit()) && position != shownIndex + 1)
					{
						// RECORDED, not applied - see the note above the declaration. Applying here
						// rewrote the order while this same loop was still walking `rows`.
						// The number typed is a place among the rows shown: the move goes to that row's place in the whole order.
						const int target = std::clamp(position, 1, static_cast<int>(shownRows.size()));
						pendingMoveMod = row.modName;
						pendingMovePosition = shownRows[target - 1] + 1;
					}

					ImGui::TableSetColumnIndex(1);
					if (row.separator) { ImGui::TextDisabled("%s", TR("AMF_SeparatorRow", "(separator)")); }
					else
					{
						if (row.depth > 0) { ImGui::Indent(ImGui::GetFontSize() * 0.9f); }
						ImGui::TextUnformatted(row.modName.c_str());
						if (row.depth > 0) { ImGui::Unindent(ImGui::GetFontSize() * 0.9f); }
					}

					ImGui::TableSetColumnIndex(2);
					auto buffer = aliasBuffers.find(row.modName);
					if (buffer == aliasBuffers.end())
					{
						std::array<char, 64> fresh{};
						// A separator starts from the name it shows (2.1.1: a category's name in the language picked).
						const std::string alias = row.separator ? row.displayName : personalization::GetAlias(row.modName);
						std::snprintf(fresh.data(), fresh.size(), "%s", alias.c_str());
						buffer = aliasBuffers.emplace(row.modName, fresh).first;
					}
					ImGui::SetNextItemWidth(-FLT_MIN);
					const bool committed =
						ImGui::InputTextWithHint("##alias", row.separator ? row.displayName.c_str() : row.modName.c_str(), buffer->second.data(),
												 buffer->second.size(), ImGuiInputTextFlags_EnterReturnsTrue);
						keyboard::NoteTextField(ImGui::GetItemID());
					if ((committed || ImGui::IsItemDeactivatedAfterEdit()) && !(row.separator && buffer->second[0] == '\0'))
					{
						personalization::SetAlias(row.modName, buffer->second.data());
						settings::Save();
					}

					ImGui::PopID();
				}
				ImGui::EndTable();
			}

			// Applied once, after the table has closed, so the sequence only changes between frames
			// and never underneath the rows being drawn from it.
			if (!pendingMoveMod.empty())
			{
				personalization::MoveTo(entries, pendingMoveMod, pendingMovePosition);
				settings::Save();
			}
		}

		// ---- nested game-menu leaf panes (the author's game-menu-replacement model, 2026-08-28) ----

		// ---- Controls: two tabs, keyboard and controller, every function reboundable -------
		// The owner, 2026-09-19: "add a tab to the controls row to divide controller and keyboard
		// and let them rebind the different functions to different buttons/stick/mouse". Each tab
		// is one row per function: its name, what it is bound to now, and a button that captures
		// the next press. The two halves are deliberately the SAME list of functions, so a player
		// on a pad is never offered fewer controls than a player on a keyboard.
		void DrawControlsPane()
		{
			auto& values = settings::Get();
			ImGui::TextUnformatted(TR("AMF_Controls", "Controls"));
			ImGui::Separator();
			ImGui::TextWrapped("%s", TR("AMF_ControlsHelp2", "These are the framework's own controls - what moves through this menu and what opens "
							   "and closes it. A mod's own keys belong on that mod's page. Press Rebind and then the "
							   "key, mouse button, pad button or stick direction you want."));
			ImGui::Spacing();

			if (!ImGui::BeginTabBar("##controlstabs", ImGuiTabBarFlags_FittingPolicyScroll)) { return; }

			int index = 0;
			const auto tab = [&](const char* a_label) {
				const ImGuiTabItemFlags flags =
					(index == g_tabRequest) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				FocusMainTabIfAsked(index);   // 2.1.5: Y in a page lands here
				const bool open = ImGui::BeginTabItem(a_label, nullptr, flags);
				if (ImGui::IsItemFocused()) { g_tabBarHasNav = true; }
				if (open) { g_tabIndex = index; }
				++index;
				return open;
			};

			// One tab's worth of rows. gamepadSide picks which half of each binding is shown and
			// which device the capture listens to; everything else is identical, on purpose.
			const auto drawRows = [&](bool a_gamepadSide) {
				if (!ImGui::BeginTable(a_gamepadSide ? "##padbinds" : "##keybinds", 3,
									   ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
				{
					return;
				}
				// The key column is as wide as its widest entry (2.1.1): stretched by proportion, it clipped to "unbour",
				// "Backsp" and "Left stick lef" in the narrower window the journal's SKSE MENUS row opens.
				float boundWidth = ImGui::CalcTextSize(TR("AMF_ColBoundTo", "Bound to")).x;
				for (int i = 0; i < static_cast<int>(bindings::Action::kCount); ++i)
				{
					const auto action = static_cast<bindings::Action>(i);
					const std::string bound = a_gamepadSide ? bindings::PadText(action) : bindings::KeyText(action);
					boundWidth = (std::max)(boundWidth, ImGui::CalcTextSize(bound.c_str()).x);
				}
				ImGui::TableSetupColumn(TR("AMF_ColFunction", "Function"));
				ImGui::TableSetupColumn(TR("AMF_ColBoundTo", "Bound to"), ImGuiTableColumnFlags_WidthFixed,
										boundWidth + ImGui::GetStyle().CellPadding.x * 2.0f);
				// As wide as Rebind and Unbind side by side (2.1.1), not a flat 13 em: at the default window size the flat width
				// left a gap after the buttons while the Function column clipped "Open and close the menu".
				const ImGuiStyle& cs = ImGui::GetStyle();
				const float buttonsWidth = ImGui::CalcTextSize(TR("AMF_BindRebind", "Rebind")).x + ImGui::CalcTextSize(TR("AMF_BindUnbindBtn", "Unbind")).x +
										   cs.FramePadding.x * 4.0f + cs.ItemSpacing.x + cs.CellPadding.x * 2.0f;
				ImGui::TableSetupColumn("##rebind", ImGuiTableColumnFlags_WidthFixed, buttonsWidth);
				ImGui::TableHeadersRow();

				for (int i = 0; i < static_cast<int>(bindings::Action::kCount); ++i)
				{
					const auto action = static_cast<bindings::Action>(i);
					ImGui::TableNextRow();
					ImGui::PushID(i + (a_gamepadSide ? 1000 : 0));

					ImGui::TableSetColumnIndex(0);
					ImGui::TextWrapped("%s", bindings::Label(action));   // wraps rather than clipping in a narrow window (2.1.1)
					if (const char* help = bindings::Description(action); help && help[0])
					{
						// Wrapped inside the column (2.1.1) - TextDisabled ran on past the cell and was cut mid-sentence.
						ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
						ImGui::TextWrapped("%s", help);
						ImGui::PopStyleColor();
					}

					ImGui::TableSetColumnIndex(1);
					const std::string bound = a_gamepadSide ? bindings::PadText(action) : bindings::KeyText(action);
					ImGui::TextUnformatted(bound.c_str());

					ImGui::TableSetColumnIndex(2);
					const bool capturingThis = bindings::IsCapturing() &&
											   bindings::CapturingAction() == action &&
											   bindings::CapturingGamepadSide() == a_gamepadSide;
					if (capturingThis)
					{
						ImGui::TextUnformatted(TR("AMF_BindPress", "press..."));
					}
					else
					{
						if (ImGui::Button(TR("AMF_BindRebind", "Rebind")))
						{
							bindings::BeginCapture(action, a_gamepadSide);
						}
						// Unbind sits beside Rebind and is shown only when there is something to
						// clear, so a row that is already unbound offers one button, not two.
						const bool bound = a_gamepadSide
							? bindings::Get(action).padKind != bindings::PadKind::kNone
							: bindings::Get(action).keyKind != bindings::KeyKind::kNone;
						if (bound)
						{
							ImGui::SameLine();
							if (ImGui::Button(TR("AMF_BindUnbindBtn", "Unbind")))
							{
								bindings::Unbind(action, a_gamepadSide);
								settings::Save();
							}
						}
					}

					ImGui::PopID();
				}
				ImGui::EndTable();

				if (bindings::IsCapturing() && bindings::CapturingGamepadSide() == a_gamepadSide)
				{
					ImGui::Spacing();
					ImGui::TextWrapped("%s", a_gamepadSide
						? TR("AMF_BindPadPrompt", "Press a pad button, click a stick, or push a stick in the direction you want. B cancels.")
						: TR("AMF_BindKeyPrompt", "Press a key or a mouse button. Escape cancels."));
				}
				if (const char* refused = bindings::LastRefusal(); refused && refused[0])
				{
					ImGui::Spacing();
					ImGui::TextWrapped("%s", refused);
				}
			};

			if (tab(TR("AMF_TabKeyboard", "Keyboard and mouse")))
			{
				ImGui::Spacing();
				drawRows(false);
				ImGui::EndTabItem();
			}
			if (tab(TR("AMF_TabController", "Controller")))
			{
				ImGui::Spacing();
				drawRows(true);
				ImGui::EndTabItem();
			}

			g_tabCount = index;
			g_tabRequest = -1; g_bumperFocusMain = false;
			ImGui::EndTabBar();

			ImGui::Spacing();
			ImGui::Separator();
			if (ImGui::Button(TR("AMF_BindSave", "Save controls")))
			{
				settings::Save();
				g_menuListSavedAt = ImGui::GetTime();
			}
			if (g_menuListSavedAt > 0.0 && ImGui::GetTime() - g_menuListSavedAt < 3.0)
			{
				ImGui::SameLine();
				ImGui::TextDisabled("%s", TR("AMF_MenuListSaved", "saved"));
			}
			ImGui::SameLine();
			if (ImGui::Button(TR("AMF_BindReset", "Reset every control")))
			{
				bindings::ResetToDefaults();
				settings::Save();
			}
			// Its own wrapped line (2.1.1): beside the buttons it ran off the pane's edge.
			ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
			ImGui::TextWrapped("%s", TR("AMF_BindNote", "Reserved keys are refused, and two functions that can be used at the same time cannot share a control."));
			ImGui::PopStyleColor();
			(void)values;
		}

		// ---- Help: the instruction manual, in tabs (the owner, 2026-09-19: "the help row should
		// have tabs: controls, features, readme, and others as you see fit") -----------------
		// A page a player can actually learn the menu from, rather than two paragraphs saying
		// where things live. The bar is submitted exactly like a mod's own page bar, and reports
		// the same tab count and index, so the D-pad walks these tabs the way it walks any other -
		// onto the tab itself, never by stepping sideways off a control.
		void DrawHelpPane()
		{
			ImGui::TextUnformatted(TR("AMF_Help", "Help"));
			ImGui::Separator();

			const auto para = [](const char* a_text) { ImGui::TextWrapped("%s", a_text); ImGui::Spacing(); };
			const auto bullet = [](const char* a_text) { ImGui::Bullet(); ImGui::SameLine(); ImGui::TextWrapped("%s", a_text); };

			if (!ImGui::BeginTabBar("##helptabs", ImGuiTabBarFlags_FittingPolicyScroll)) { return; }

			int index = 0;
			const auto tab = [&](const char* a_label) {
				const ImGuiTabItemFlags flags =
					(index == g_tabRequest) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				FocusMainTabIfAsked(index);   // 2.1.5: Y in a page lands here
				const bool open = ImGui::BeginTabItem(a_label, nullptr, flags);
				if (ImGui::IsItemFocused()) { g_tabBarHasNav = true; }
				if (open) { g_tabIndex = index; }
				++index;
				return open;
			};

			if (tab(TR("AMF_HelpTabControls", "Controls")))
			{
				ImGui::Spacing();
				ImGui::SeparatorText(TR("AMF_ManOpening", "Opening and closing the menu"));
				para(TR("AMF_ManOpening1", "Press F1 to open the menu and F1 again to close it. Escape closes it too. The key is yours to change: Controls -> Open and close the menu -> Rebind, then press the key you want."));
				para(TR("AMF_ManOpening2", "On a controller, Start closes the menu. There is no controller button that opens it - "
						"open it from the journal instead: press Start, go to the System tab, and choose the "
						"SKSE MENUS row. That row can be turned off under Settings if you would rather not have it."));
				para(TR("AMF_ManOpening3", "While the menu is up the game does not see your keys or your mouse, so the camera and "
						"your character stay still. Mods' own hotkeys are held off as well, so a key that opens "
						"something else cannot fire while you are reading a page."));

				ImGui::SeparatorText(TR("AMF_ManMoving", "Moving around"));
				bullet(TR("AMF_ManMoving1", "Mouse: point and click, as anywhere else. The cursor is drawn by the menu itself."));
				bullet(TR("AMF_ManMoving2", "Keyboard: the arrow keys move the highlight, Enter activates, Escape closes."));
				bullet(TR("AMF_ManMoving3", "Controller: the D-pad and the left stick move the highlight, A activates, B goes back. "
						  "Take hold of a slider with A and the RIGHT stick moves it, so adjusting a value never "
						  "also moves the highlight."));
				bullet(TR("AMF_ManMoving4", "Left and right cross between the list and the page beside it. The bumpers - Page Up and Page Down on a keyboard - step through the tabs at the top of a page; moving sideways never changes the tab under you."));
				bullet(TR("AMF_ManMoving6", "Right-click a mod in the list, or press Y on a controller, for its options."));
				ImGui::Spacing();
				para(TR("AMF_ManMoving5", "The menu follows whatever you last used: touch the pad and it switches to controller "
						"navigation, touch the mouse or a key and it switches back. There is nothing to set."));

				ImGui::SeparatorText(TR("AMF_ManTyping", "Typing"));
				para(TR("AMF_ManTyping1", "Click a text box and type. Ctrl+A selects everything in it, and Ctrl+C, Ctrl+X, "
						"Ctrl+V and Ctrl+Z work as they do anywhere."));
				para(TR("AMF_ManTyping2", "On a controller, put the highlight on a text box and press A: a key grid appears "
						"across the bottom of the screen. The D-pad walks it, A types, B puts the highlight back "
						"on the box, X is shift and Y is backspace. It works on every mod's page, and it can be "
						"turned off under Settings."));
				ImGui::EndTabItem();
			}

			if (tab(TR("AMF_HelpTabFeatures", "Features")))
			{
				ImGui::Spacing();
				ImGui::SeparatorText(TR("AMF_ManList", "The mod list"));
				para(TR("AMF_ManList1", "Every mod that registers a page appears under Mods, with the framework's own Settings, "
						"Controls and this Help page above it. Type in the Search box to narrow the list - two or "
						"three letters is usually enough - and it matches whatever name the entry is showing."));
				bullet(TR("AMF_ManList2", "A-Z and Z-A beside Mods sort the list. Turn both off and the list goes back to the order "
						  "you arranged it in."));
				bullet(TR("AMF_ManList3", "Right-click a mod (or press Y on a controller) for its options: add it to your "
						  "favourites, rename it, or move it to the top."));
				bullet(TR("AMF_ManList4", "A favourite sits at the top of the list with a filled white box beside its name. "
						  "Favourites keep the order you added them in, so a new one lands after the last."));
				bullet(TR("AMF_ManList5", "Renaming changes only what this menu shows. The mod itself never sees it, and the "
						  "search box finds the entry by the name you gave it."));
				ImGui::Spacing();
				para(TR("AMF_ManList6", "Settings -> Menu list has the same controls as a table, with a position number you can "
						"type into: put 3 in a row's number and it moves there, and everything else re-flows around it."));

				ImGui::SeparatorText(TR("AMF_ManLook", "How it looks"));
				// 2.1.6 (the owner, 2026-10-08: "make sure the help pages are up to date too"): Appearance has four sub-tabs now -
				// Theme and text, Window, Colours (2.1.5) and Art (2.1.6) - and each has its line here.
				bullet(TR("AMF_ManLook1", "Theme: Skyrim is the Nordic knotwork frame; Vel'dun, Oathvein, Norden, Norden - Black and "
						  "Oblivion each bring their own art, and Untarnished is plain lines. Settings -> Appearance -> Theme and text."));
				bullet(TR("AMF_ManLook2", "Font: drop a .ttf into Data/SKSE/Plugins/ApocryphaMenuFramework/fonts and pick it under Settings -> Appearance -> Theme and text."));
				bullet(TR("AMF_ManLook3", "Text size scales on top of the automatic resolution scale, so the menu reads the same on "
						  "a 1080p screen and a 4K one."));
				bullet(TR("AMF_ManLook4", "Language: the framework's own text follows the game's language unless you force one."));
				bullet(TR("AMF_ManLook5", "Window: move it by its title and resize it from any edge or corner, each with its own switch, "
						  "and it remembers where you leave it, separately for each way of opening it. See-through lets the game show "
						  "behind it. Settings -> Appearance -> Window."));
				bullet(TR("AMF_ManLook6", "Colours: your own colour for each part of the menu - background, borders, text, selection, "
						  "sliders, switches, headings and help - kept for each theme. Settings -> Appearance -> Colours."));
				bullet(TR("AMF_ManLook7", "Art: build your own look from every theme's art - five shapes of each part of the menu: "
						  "frame, background, popups, highlight frame, boxes, buttons, tick boxes, switches, slider grabs, scroll bars, tabs, "
						  "arrows, section lines and the mouse pointer, the default among them. Each theme keeps its own picks, and they "
						  "take your Colours. Settings -> Appearance -> Art."));
				ImGui::EndTabItem();
			}

			if (tab(TR("AMF_HelpTabReadme", "Readme")))
			{
				ImGui::Spacing();
				ImGui::TextWrapped("%s", TR("AMF_Help1", "ApocryphaRealm Menu Framework presents mod settings in one menu, laid out like "
								   "the game's own: tabs across the top, a list down the side, and the "
								   "selected entry's options here."));
				ImGui::Spacing();
				ImGui::TextWrapped("%s", TR("AMF_Help2", "Open this menu from the SKSE MENUS row in the journal's System tab, or with its key. The framework's own options are under Settings, and the keys that drive it under Controls."));
				ImGui::Spacing();
				para(TR("AMF_ReadmeWhat", "It is one menu for every mod that asks for one. A mod does not have to know anything "
						"about this framework's look, its themes or its controller support - it hands over its "
						"settings and gets all of it."));
				para(TR("AMF_ReadmeCompat", "Pages written for SKSE Menu Framework work here unchanged: the same API is answered, so "
						"a mod built against either one is at home. Install only one of the two."));
				para(TR("AMF_ReadmeAuthors", "For mod authors: the framework exports a C API and a single header. Register a section, "
						"add pages to it, draw them with the ImGui calls the header wraps, and the menu does the "
						"rest - layout, theme, font, translation, keyboard, controller and the on-screen keyboard."));
				para(TR("AMF_ReadmeFiles", "Your settings are kept in Data/SKSE/Plugins/ApocryphaMenuFramework/User.ini, a file the download never contains, so an update keeps them; ApocryphaMenuFramework.ini beside the plugin holds the defaults. The log is in Documents/My Games/Skyrim Special Edition/SKSE/."));
				ImGui::EndTabItem();
			}

			if (tab(TR("AMF_HelpTabTrouble", "Troubleshooting")))
			{
				ImGui::Spacing();
				bullet(TR("AMF_ManTrouble1", "A mod's page is missing: the mod has not registered one, or it needs a newer framework "
						  "than the one installed. Its own log will say."));
				bullet(TR("AMF_ManTrouble2", "The menu will not open: something else may have taken F1. Rebind it under Controls, or open the menu from the journal's System tab instead."));
				bullet(TR("AMF_ManTrouble3", "A key does nothing inside the menu: another mod may be claiming it. The framework's log "
						  "names the device and key whenever that happens."));
				bullet(TR("AMF_ManTrouble5", "Text boxes take no typing: update the framework. Before 1.9.5 the engine was never asked "
						  "to turn key presses into characters while the menu was up."));
				ImGui::Spacing();
				para(TR("AMF_ManTrouble4", "The log is at Documents/My Games/Skyrim Special Edition/SKSE/ApocryphaMenuFramework.log. uLogLevel under [Log] in the INI decides how much it writes: 0 writes the most."));
				ImGui::EndTabItem();
			}

			g_tabCount = index;
			g_tabRequest = -1; g_bumperFocusMain = false;
			ImGui::EndTabBar();
		}

		void DrawFrameworkWindow()
		{
			const ImVec2 display = ImGui::GetIO().DisplaySize;

			// TWO PROFILES, NOT TWO PRESETS (author, 2026-09-04: "lets have it treat them as
			// profiles to save the users settings to so that we set the default to vanilla
			// positioning and size and if their ui mod does different then they can change it and
			// it will remember").
			//
			// Each way of opening the framework has its own saved geometry. Until the player moves
			// or resizes that window, the profile is unset and takes its DEFAULT - the measured
			// journal panel when nested, the centred proportions otherwise. The first drag or
			// resize fills the profile in, and from then on that is what the window opens at.
			//
			// This supersedes the 2026-08-27 "preset positions, never free placement" decision for
			// this window. That rule existed so the window could not be lost off-screen or left
			// somewhere useless; a REMEMBERED position with a reset button gives the same safety
			// while letting someone whose menu replacer puts its panel elsewhere fix it once. The
			// preset below still decides where an unset profile centres itself.
			//
			// Geometry is kept as fractions of the display, so the numbers stay correct if the
			// resolution changes between sessions.
			ImVec2 anchor{ 0.5f, 0.5f };
			switch (settings::Get().windowPreset)
			{
			default:
				break;  // 0 (and any unknown value) = centre
			}

			// Each mode Begin()s a DIFFERENT ImGui id, so ImGui also keeps their layout entries
			// apart. Sharing one id is what let the nested mode's forced size overwrite a size the
			// apart. The text after ### is the identity and is not displayed. ### rather than ##
			// on purpose: with ##, ImGui hashes the WHOLE label, so changing the visible name
			// would silently orphan that saved entry - exactly what renaming this to
			// "ApocryphaRealm" would otherwise have done. With ###, the id is the id and the
			// shown text is free to change.
			const bool nested = g_nested.load(std::memory_order_acquire);
			const char* windowId = nested ? "ApocryphaRealm Menu Framework###amf-nested"
										  : "ApocryphaRealm Menu Framework###amf-main";
			// ONE CENTRED WINDOW (the owner, 2026-10-02: "AMF is still locked to the center of the screen and ... automatically
			// increases its ... width to fit both the left and right panes ... I don't want it to dynamically change per mod
			// selected", and "make the system row version of AMF also not specific to the journal bounds, because otherwise
			// it's going to be too small to see"). Both ways of opening share the key-opened window's geometry: centred on the
			// screen, not movable, one remembered size; the journal-panel fit of the System-row window is retired.
			settings::WindowGeometry& profile = settings::Get().hotkeyWindow;

			// ---- this profile's default, in screen fractions ----
			float dx = 0.0f, dy = 0.0f, dw = 0.55f, dh = 0.70f;
			bool haveDefault = false;
			if (false)   // the journal-panel measurement (System-row window) - retired 2026-10-02, kept for reference
			{
				// The panel is measured off the LIVE movie rather than assumed: its size differs
				// under every art replacer, which is the same reason the row is injected rather
				// than shipped. The last good measurement is kept, because the journal fades its
				// panel in and a frame where the read fails must not move the window.
				static float px = 0.0f, py = 0.0f, pw = 0.0f, ph = 0.0f;
				float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
				if (systemrow::GetPanelRect(x, y, w, h))
				{
					px = x; py = y; pw = w; ph = h;
				}
				if (pw > 0.0f && ph > 0.0f)
				{
					// Inset by the window padding: ImGui strokes its border ON the rect it is given
					// and the journal strokes its panel border on the same line, so filling the rect
					// exactly puts two borders flush together and reads as one thick misaligned
					// rule. Padding comes from the active style, so it tracks the theme and the text
					// size rather than being right at one of them only.
					const ImVec2 pad = ImGui::GetStyle().WindowPadding;
					dx = px + pad.x / display.x;
					dy = py + pad.y / display.y;
					dw = pw - (pad.x * 2.0f) / display.x;
					dh = ph - (pad.y * 2.0f) / display.y;
					haveDefault = dw > 0.0f && dh > 0.0f;
				}
			}
			else
			{
				dx = anchor.x - dw * 0.5f;
				dy = anchor.y - dh * 0.5f;
				haveDefault = true;
			}

			// ---- apply, ONCE per opening ----
			// Only on the frame the window is opened, so a drag afterwards is not undone the next
			// frame. If the nested measurement is not ready yet the flag is left set and the next
			// frame tries again, rather than falling back to the centre and jumping later.
			bool appliedThisFrame = false;
			// NO TITLE BAR (the owner, 2026-10-02: "get rid of that top bar with the arrow pointing down ... Let's just fill it
			// in with the Nordic knotwork, and we don't need a way to hide the menu aside from the hotkey or pressing the start
			// button"). The knotwork frame now runs round the window's own top edge, and the collapse arrow is gone. The window
			// still MOVES (the owner's 2026-09-21 requirement): ImGui's ConfigWindowsMoveFromTitleBarOnly does not apply to a
			// window without a title bar, so a drag on its frame or any empty part of it moves it.
			// NoMove stays (2.1.1): ImGui's own move would let the body drag the window. The top row is the handle instead -
			// see "THE TOP ROW MOVES THE WINDOW" below.
			ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
			// Resize the window OFF really is off (the owner, 2026-10-05: "make sure the toggle actually toggles off the
			// resizing"): no edge or corner resizes it. On (the default), every edge and corner does, freely.
			if (!settings::Get().freeResize) { windowFlags |= ImGuiWindowFlags_NoResize; }
			// 1.7.7/1.7.8 (the owner, 2026-09-13): the KEY-OPENED window is fixed to the screen centre; an
			// edge drag grows both sides (the centre never moves); a corner drag keeps the window's SHAPE
			// and grows it - the text does not scale ("it should just increase the size of the window
			// length and width proportionally"); the window can never be larger than the game screen.
			// Done through ImGui's own size constraint callback, so ImGui resizes once per frame with
			// the rule already applied and nothing is forced afterwards (no jumping). None of it
			// applies to the nested window, which keeps the journal-panel placement below.
			struct HotkeyConstraint { ImVec2 display{}; float aspect = 0.0f; bool free = false; };
			static HotkeyConstraint s_hotkeyConstraint{};
			// 1.9.6 (the owner, 2026-09-21: "the F1 called AMF does not [move], as it is fixed in position, which should
			// still be movable if they grab it by the top"). The key-opened window now MOVES by its top bar like the
			// System-row one (ConfigWindowsMoveFromTitleBarOnly keeps the body from dragging it). What stays from
			// 2026-09-13 is the resize: an edge drag still grows both sides about the window's centre and a corner
			// drag keeps its shape - the centre is simply wherever the player has put the window, not the screen's.
			static ImVec2 s_hotCentre{ -1.0f, -1.0f };
			ImGuiWindow* hotWindow = ImGui::FindWindowByName(windowId);
			const bool movingHot = hotWindow && GImGui->MovingWindow && GImGui->MovingWindow->RootWindow == hotWindow;
			constexpr bool kCentred = true;   // both modes (see above)
			if (kCentred)
			{
				if (g_applyGeometry.load(std::memory_order_acquire))
				{
					const float gw = std::min(profile.IsSet() ? profile.w : dw, 1.0f);
					const float gh = std::min(profile.IsSet() ? profile.h : dh, 1.0f);
					ImGui::SetNextWindowSize(ImVec2(display.x * gw, display.y * gh), ImGuiCond_Always);
					// WHERE THE PLAYER LEFT IT (2.1.1 - Barzing on Nexus, 2026-10-05: "the possibility to move the window"; the
					// owner: "ill add ... move the window"). This reverses 2026-10-02's lock to the screen centre: the window opens
					// centred the first time and after Reset, and otherwise where it was last put - its saved top-left plus half
					// its size is the centre it is held at.
					// Only with Move the window on; off, it is held at the screen centre as before.
					const bool free = settings::Get().movableWindow && profile.IsSet();
					float cx = free ? profile.x + gw * 0.5f : 0.5f;
					float cy = free ? profile.y + gh * 0.5f : 0.5f;
					cx = std::clamp(cx, gw * 0.5f, 1.0f - gw * 0.5f);
					cy = std::clamp(cy, gh * 0.5f, 1.0f - gh * 0.5f);
					s_hotCentre = ImVec2(display.x * cx, display.y * cy);
					g_applyGeometry.store(false, std::memory_order_release);
					appliedThisFrame = true;
					s_hotkeyConstraint.aspect = 0.0f;
				}
				if (s_hotCentre.x < 0.0f) { s_hotCentre = ImVec2(display.x * 0.5f, display.y * 0.5f); }
				s_hotkeyConstraint.display = display;
				s_hotkeyConstraint.free = settings::Get().freeResize;
				ImGui::SetNextWindowSizeConstraints(ImVec2(display.x * 0.2f, display.y * 0.2f), display,
					+[](ImGuiSizeCallbackData* a_data) {
						auto* c = static_cast<HotkeyConstraint*>(a_data->UserData);
						const bool wChanged = std::fabs(a_data->DesiredSize.x - a_data->CurrentSize.x) > 0.5f;
						const bool hChanged = std::fabs(a_data->DesiredSize.y - a_data->CurrentSize.y) > 0.5f;
						// With Resize the window on (2.1.1) a corner drag is free - width and height each follow the mouse.
						const bool corner = wChanged && hChanged && c->aspect > 0.0f && !c->free;
						if (corner)
						{
							a_data->DesiredSize.y = a_data->DesiredSize.x / c->aspect;   // keep the shape
						}
						a_data->DesiredSize.x = std::min(a_data->DesiredSize.x, c->display.x);
						a_data->DesiredSize.y = std::min(a_data->DesiredSize.y, c->display.y);
						if (corner && a_data->DesiredSize.y * c->aspect < a_data->DesiredSize.x)
						{
							a_data->DesiredSize.x = a_data->DesiredSize.y * c->aspect;   // the clamp held one side: keep the shape
						}
					}, &s_hotkeyConstraint);
				// Held at its centre every frame EXCEPT while the title bar is being dragged, when ImGui's own move
				// runs and the centre follows it (below). That is what keeps a resize symmetric.
				if (!movingHot) { ImGui::SetNextWindowPos(s_hotCentre, ImGuiCond_Always, ImVec2(0.5f, 0.5f)); }
			}
			else if (g_applyGeometry.load(std::memory_order_acquire))
			{
				bool useProfile = profile.IsSet();
				// 1.8.1: the nested profile is remembered PER JOURNAL ART (the owner, 2026-09-13: "the
				// position should only change to match the redesign when the redesign is active"). The
				// art on screen is what GetPanelRect just measured; a position dragged under other art
				// is ignored, the journal is measured afresh, and the next drag saves for this art.
				const std::string artNow = systemrow::ArtKey();
				if (useProfile && haveDefault && profile.art != artNow)
				{
					useProfile = false;
					static std::string s_saidFor;
					if (s_saidFor != artNow)
					{
						s_saidFor = artNow;
						logger::info("window profile (nested) was saved under journal art '{}'; the journal on screen is '{}', so the measured panel is used instead",
							profile.art.empty() ? "unknown" : profile.art, artNow);
					}
				}
				if (useProfile || haveDefault)
				{
					const float gx = useProfile ? profile.x : dx;
					const float gy = useProfile ? profile.y : dy;
					const float gw = useProfile ? profile.w : dw;
					const float gh = useProfile ? profile.h : dh;
					ImGui::SetNextWindowPos(ImVec2(display.x * gx, display.y * gy), ImGuiCond_Always);
					ImGui::SetNextWindowSize(ImVec2(display.x * gw, display.y * gh), ImGuiCond_Always);
					g_applyGeometry.store(false, std::memory_order_release);
					appliedThisFrame = true;
					// Evidence for the owner's standing requirement (2026-09-21): the System-row window reopens where
					// the player left it, across game reloads. One line per opening says which geometry was used.
					logger::info("window profile (nested) applied on open: {} x={:.3f} y={:.3f} w={:.3f} h={:.3f} (art '{}')",
						useProfile ? "the player's saved position" : "the measured journal panel (no saved position for this art)",
						gx, gy, gw, gh, artNow);
				}
			}

			if (ImGui::Begin(windowId, nullptr, windowFlags))
			{
				{
					const ImVec2 rp = ImGui::GetWindowPos();
					const ImVec2 rs = ImGui::GetWindowSize();
					std::scoped_lock l(g_selLock);
					g_mainX = rp.x; g_mainY = rp.y; g_mainW = rs.x; g_mainH = rs.y;
				}
				if (kCentred)
				{
					// The shape a corner drag keeps is the shape the window had when the drag began:
					// refreshed every frame the mouse is up, frozen while it is down.
					const ImVec2 cur = ImGui::GetWindowSize();
					if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && cur.x > 1.0f && cur.y > 1.0f)
					{
						s_hotkeyConstraint.aspect = cur.x / cur.y;
					}
					if (movingHot)
					{
						const ImVec2 wp = ImGui::GetWindowPos();
						s_hotCentre = ImVec2(wp.x + cur.x * 0.5f, wp.y + cur.y * 0.5f);
					}
				}
				// The author's background, before any content: ImGui has already painted the window's
				// own colour, and everything drawn after this lands on top of the art.
				if (skin::HasBackground())
				{
					const ImVec2 bp = ImGui::GetWindowPos();
					const ImVec2 bs = ImGui::GetWindowSize();
					DrawSkinBackground(ImGui::GetWindowDrawList(), bp, ImVec2(bp.x + bs.x, bp.y + bs.y));
				}
				// REMEMBER WHERE THE PLAYER LEAVES IT. Written back as fractions of the display, so
				// the profile stays correct if the resolution changes between sessions.
				//
				// Not on the frame we just placed the window - that would record our own default as
				// though the player had chosen it, and the profile would never be "unset" again, so
				// Reset could not restore the journal fit. And not mid-drag either: the settings
				// file is rewritten on each save, and a drag would rewrite it every frame. Waiting
				// for the mouse to come up saves once, when the player has finished.
				if (!appliedThisFrame && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
				{
					const ImVec2 wpos = ImGui::GetWindowPos();
					const ImVec2 wsize = ImGui::GetWindowSize();
					const float nx = wpos.x / display.x;
					const float ny = wpos.y / display.y;
					const float nw = wsize.x / display.x;
					const float nh = wsize.y / display.y;
					const auto moved = [](float a, float b) { return std::fabs(a - b) > 0.001f; };
					if (moved(nx, profile.x) || moved(ny, profile.y) ||
						moved(nw, profile.w) || moved(nh, profile.h))
					{
						profile.x = nx; profile.y = ny; profile.w = nw; profile.h = nh;
						if (nested) { profile.art = systemrow::ArtKey(); }   // 1.8.1: remembered for THIS journal art
						settings::Save();
						logger::debug("window profile ({}) saved: x={:.3f} y={:.3f} w={:.3f} h={:.3f}",
							nested ? "nested" : "hotkey", nx, ny, nw, nh);
					}
				}

				// Version always on show - a version-less status line reads as a stale build
				// (the author, third smoke test).
				static const std::string version =
					SKSE::PluginDeclaration::GetSingleton()->GetVersion().string(".");
				ImGui::Text("ApocryphaRealm Menu Framework  v%s", version.c_str());
				// THE TOP ROW MOVES THE WINDOW (2.1.1). The window has no title bar, and its body must never drag it: a page's
				// slider drag or row click would move the window instead (ConfigWindowsMoveFromTitleBarOnly, above, only
				// restrains windows WITH a title bar). So the band from the window's top edge to the bottom of this line is an
				// invisible handle: drag it and the window follows; let go and the place is saved with the size. ImGui's own
				// edge-resize zones are tested before any item, so the very edge still resizes.
				if (kCentred && settings::Get().movableWindow)
				{
					const ImVec2 afterRow = ImGui::GetCursorScreenPos();
					const ImVec2 wp = ImGui::GetWindowPos();
					const float border = ImGui::GetStyle().WindowBorderSize + 2.0f;
					const float bandH = ImGui::GetItemRectMax().y - wp.y - border;
					if (bandH > 1.0f)
					{
						ImGui::SetCursorScreenPos(ImVec2(wp.x + border, wp.y + border));
						// a mouse handle only: off the D-pad's path (the owner, 2026-10-07: D-pad up from the panes put the 2.1.5
						// highlight frame round the top bar - the band was a nav stop)
						ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
						ImGui::InvisibleButton("##amf-move", ImVec2(std::max(1.0f, ImGui::GetWindowWidth() - border * 2.0f), bandH));
						ImGui::PopItemFlag();
						if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f))
						{
							const ImVec2 d = ImGui::GetIO().MouseDelta;
							const ImVec2 sz = ImGui::GetWindowSize();
							// Kept whole on the screen: the centre stays half a window from every edge.
							s_hotCentre.x = std::clamp(s_hotCentre.x + d.x, sz.x * 0.5f, std::max(sz.x * 0.5f, display.x - sz.x * 0.5f));
							s_hotCentre.y = std::clamp(s_hotCentre.y + d.y, sz.y * 0.5f, std::max(sz.y * 0.5f, display.y - sz.y * 0.5f));
						}
						ImGui::SetCursorScreenPos(afterRow);
					}
				}
				ImGui::Separator();

				// Whether this theme wants the knotwork frame - captured once, applied to every
				// panel below and the outer window for a consistent framed look.
				// A supplied frame is drawn whatever the theme says: an author who ships frame art
				// has asked for a frame, and it replaces the knotwork rather than adding to it.
				const bool knot = skin::DrawsFrame();   // 2.1.6: also off when the player picked no frame

				const std::vector<registry::Entry> entries = registry::Snapshot();
				// THE SIDE PANE FITS ITS NAMES (the owner, 2026-10-02: "make it so that the names are always fully visible
				// ... by making the left pane auto adjust its width to fit the names of the menus"). It used to be a flat
				// 30% of the window, which clipped "ApocryphaRealm Lock Interaction Overhaul" to "ApocryphaR". Now it is the
				// widest name actually shown - a mod under a separator with its indent, a separator with its fold arrow and
				// count - plus the pinned-box gutter, the window padding and a scrollbar; never under the old 30%.
				float leftWidth = 0.0f;
				{
					const float avail = ImGui::GetContentRegionAvail().x;
					const ImGuiStyle& st = ImGui::GetStyle();
					const float gutter = ImGui::GetFontSize() * 0.55f + st.ItemInnerSpacing.x * 2.0f;
					float widest = ImGui::CalcTextSize(TR("AMF_Framework", "Framework")).x;
					for (const personalization::DisplayEntry& row : personalization::Order(entries))
					{
						float w = ImGui::CalcTextSize(row.displayName.c_str()).x;
						if (row.separator) { w += ImGui::GetFontSize() * 1.1f + ImGui::CalcTextSize("  (000)").x; }
						else if (row.depth > 0) { w += ImGui::GetFontSize() * 0.9f; }
						widest = std::max(widest, w);
					}
					const float needed = widest + gutter + st.WindowPadding.x * 2.0f + st.ScrollbarSize + st.ItemSpacing.x * 2.0f;
					// THE WINDOW GROWS RATHER THAN SQUEEZING THE PAGE (the owner, 2026-10-02, after the names pane took the
					// room: "we're gonna have to have the right pane automatically fit its mod menus by size as well ... now
					// you can't see hardly anything on the right side"). The page pane keeps at least 28 characters' width;
					// when the names and that minimum do not both fit, the window widens itself once (up to 98% of the screen;
					// it is centred, so it grows both ways) - from the names alone, so it does not change with the mod picked.
					const float rightMin = ImGui::GetFontSize() * 28.0f;
					const float between = ImGui::GetStyle().ItemSpacing.x + kKnotOutset * 4.0f;
					if (avail < needed + rightMin + between)
					{
						ImGuiWindow* self = ImGui::GetCurrentWindow();
						const float grown = std::min(self->Size.x + (needed + rightMin + between - avail), display.x * 0.98f);
						if (grown > self->Size.x + 0.5f) { ImGui::SetWindowSize(ImVec2(grown, self->Size.y)); }
					}
					const float most = std::max(avail * 0.30f, avail - rightMin - between);
					leftWidth = std::clamp(needed, avail * 0.30f, most);
				}

				// SMF SHAPE (design decision, 2026-08-30): a one-for-one replacement of SKSE Menu Framework's
				// window - a SIDE LIST of the registered mods (plus the framework's own entries) and a
				// CONTENT pane for the selected mod's pages (tabs when it has several). No game-menu
				// tabs, no Save/Load/Quit: the game's own menus are not this framework's job.
				std::string sel;
				int selMod = 0;
				{
					std::scoped_lock l(g_selLock);
					sel = g_selNode; selMod = g_selMod;
					g_selExternal = false;
				}
				bool changed = false;  // set only by a real UI interaction this frame
				// CONSUMED HERE, ONCE, whatever happens below. It used to be cleared only inside the
				// branch that acts on it - which needs the selected entry to be DRAWN - so with any
				// text in the search box the selected mod was filtered out, the flag was never
				// cleared, and it sat armed until that row reappeared and stole the keyboard from
				// the search box (the owner, 2026-09-19: "the typing indicator just disappears").
				const bool navToSelected = g_navToSelected.exchange(false);
				if (navToSelected) { g_frameNavConsumed = ImGui::GetFrameCount(); }
				// (the registry snapshot is taken above, before the side pane's width is measured from it)
				if (selMod >= static_cast<int>(entries.size())) { selMod = 0; }

				// ---- SIDE LIST -------------------------------------------------------------------
				// PANE CROSSING (author playtest 2026-09-01: "neither the d-pad the left or the right stick
				// will let me go from the left to the right pane"). ImGuiWindowFlags_NavFlattened is
				// documented for children with NO scrolling; the content pane scrolls, and flattening it
				// gave asymmetric crossing - content->list worked, list->content never did. So nav stays
				// contained in each pane and the crossing is done explicitly below, which is also exactly
				// what the controller spec asks for.
				if (g_focusPane == 1) { ImGui::SetNextWindowFocus(); g_focusPane = 0; g_frameWindowFocus = ImGui::GetFrameCount(); }
				ImGui::BeginChild("##side", ImVec2(leftWidth, 0.0f), true);
				auto sideItem = [&](const char* label, const char* id) {
					const bool isOpen = (sel == id);
					if (isOpen && navToSelected)
					{
						ImGui::SetKeyboardFocusHere();  // the next item submitted takes the nav cursor
						g_frameFocusHere = ImGui::GetFrameCount();
					}
					if (ImGui::Selectable(label, isOpen)) { sel = id; changed = true; }
					NoteHoverFrame();   // 2.1.5: the theme's frame round the name under the mouse
				};
				ImGui::TextDisabled("%s", TR("AMF_Framework", "Framework"));
				sideItem(TR("AMF_Settings", "Settings"), "settings");
				sideItem(TR("AMF_Controls", "Controls"), "controls");
				sideItem(TR("AMF_Help", "Help"),     "help");
				ImGui::Separator();
				ImGui::TextDisabled("%s", TR("AMF_Mods", "Mods"));

				// THE MODS ROW (2.1.1 - the owner, 2026-10-05: "we only need one toggle because switched off, it would be Z to A,
				// and switch on, it would be A to Z ... just have a tick box next to the sorting toggle. Whether they want
				// alphabetical sorting on. So a tick box, a toggle for A to Z and Z to A, and then a sort button for sorting and
				// adding the separators"). It replaces 2026-09-19's two switches (A-Z, Z-A; both off = the list's own order):
				//   tick box  - alphabetical order on or off (off: the order the player arranged by hand);
				//   switch    - on A-Z, off Z-A; greyed while the tick box is off, and remembered for the next tick;
				//   Sort      - the MCM category sort, separators and all, as on Settings > Menu list (where Undo is).
				// A tick box rather than a switch for the first, against rule 32, because the owner asked for one by name.
				// Favourites stay pinned at the top under any order.
				{
					const auto mode = personalization::GetSortMode();
					static bool s_ascending = mode != personalization::SortMode::kAlphaDesc;   // the direction while unticked
					bool alphabetical = mode != personalization::SortMode::kListOrder;
					if (mode == personalization::SortMode::kAlphaAsc) { s_ascending = true; }
					if (mode == personalization::SortMode::kAlphaDesc) { s_ascending = false; }
					const auto apply = [&]() {
						personalization::SetSortMode(!alphabetical ? personalization::SortMode::kListOrder
													 : (s_ascending ? personalization::SortMode::kAlphaAsc : personalization::SortMode::kAlphaDesc));
						settings::Save();
					};
					ImGui::SameLine();
					if (ImGui::Checkbox("##alphabetical", &alphabetical)) { apply(); }
					if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", TR("AMF_SortAlphaTip", "Sort the list alphabetically. Off: the order you arranged by hand.")); }
					ImGui::SameLine();
					ImGui::BeginDisabled(!alphabetical);
					// "###sortdir": the label flips A-Z / Z-A, and a label-made ID would make every press a new item - the next A did
					// nothing until the highlight left and came back (the owner, 2026-10-07, on the Filter +/- buttons; same cause)
					const std::string sortDirLabel = std::string(s_ascending ? TR("AMF_SortAsc", "A-Z") : TR("AMF_SortDesc", "Z-A")) + "###sortdir";
					if (widgets::Toggle(sortDirLabel.c_str(), &s_ascending)) { apply(); }
					ImGui::EndDisabled();
					if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) { ImGui::SetTooltip("%s", TR("AMF_SortDirTip", "On: A to Z. Off: Z to A.")); }
					// FOLD ALL, next to A-Z with Sort kept at the far right (2.1.5, the owner, 2026-10-07: "next to the sort button ... a collapse and uncollapse toggle. When
					// it's on, it collapses all [separators], and when it's off, it uncollapses them. And if the user goes and
					// uncollapses one individually, then the toggle doesn't auto-reassert itself until it's toggled again").
					// The switch acts once, when it is switched; a separator folded or opened by hand afterwards stays as it is.
					{
						auto& foldValues = settings::Get();
						const auto separators = personalization::Separators();
						ImGui::SameLine();
						ImGui::BeginDisabled(separators.empty());
						if (widgets::Toggle(TR("AMF_FoldAll", "Fold"), &foldValues.foldAllSeparators))
						{
							int changed = 0;
							for (const auto& s : separators)
							{
								if (personalization::IsCollapsed(s.id) != foldValues.foldAllSeparators)
								{
									personalization::ToggleCollapsed(s.id);
									++changed;
								}
							}
							settings::Save();
							// the frame and the input that pressed it (the owner, 2026-10-07: one press logged fold-all then open-all 0.3 s
							// apart, and "when it unfolds, it leaves some separators folded") - so a double fire shows its source
							const ImGuiInputSource src = GImGui ? GImGui->ActiveIdSource : ImGuiInputSource_None;
							logger::info("side list: fold all -> {} ({} of {} separator(s) changed) [frame {}, {}, nav {}]", foldValues.foldAllSeparators, changed,
								separators.size(), ImGui::GetFrameCount(), src == ImGuiInputSource_Mouse ? "mouse" : src == ImGuiInputSource_Gamepad ? "gamepad" :
								src == ImGuiInputSource_Keyboard ? "keyboard" : "other", GImGui ? GImGui->NavId : 0u);
						}
						ImGui::EndDisabled();
						if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
						{
							ImGui::SetTooltip("%s", TR("AMF_FoldAllTip", "On: every separator folded. Off: every one open. Folding or opening one by hand afterwards leaves this switch as it is."));
						}
					// FILTER, just before Sort (Sort stays at the far right): the saved words, each with its +/- button.
					{
						auto& filterValues = settings::Get();
						const bool filtering = std::any_of(filterValues.listFilters.begin(), filterValues.listFilters.end(),
														   [](const settings::Values::FilterWord& f) { return f.state != 0; });
						ImGui::SameLine();
						if (filtering) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab)); }
						// "Filter (n)" while words are in use (the owner, 2026-10-07: a saved "-mcm" hid every converted page and the
						// groups looked empty - the tinted text alone did not show in Norden - Black, whose tint is near white)
						const auto activeWords = std::count_if(filterValues.listFilters.begin(), filterValues.listFilters.end(),
															   [](const settings::Values::FilterWord& f) { return f.state != 0; });
						const std::string filterLabel = std::string(TR("AMF_FilterButton", "Filter")) +
														(activeWords > 0 ? " (" + std::to_string(activeWords) + ")" : std::string()) + "###listfilterbtn";
						if (ImGui::SmallButton(filterLabel.c_str())) { ImGui::OpenPopup("##listfilter"); }
						if (filtering) { ImGui::PopStyleColor(); }
						if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", TR("AMF_FilterTip", "Words to show only, or to hide, in the list - kept between games.")); }
						if (ImGui::BeginPopup("##listfilter"))
						{
							bool dirty = false;
							int remove = -1;
							const float side = ImGui::GetFrameHeight();
							for (int i = 0; i < static_cast<int>(filterValues.listFilters.size()); ++i)
							{
								auto& f = filterValues.listFilters[static_cast<std::size_t>(i)];
								ImGui::PushID(i);
								// the coloured state button: green +, red -, plain when off (left click forward, right click back)
								const ImVec4 green{ 0.30f, 0.69f, 0.31f, 1.0f }, red{ 0.75f, 0.27f, 0.27f, 1.0f };
								int colours = 0;
								if (f.state != 0)
								{
									const ImVec4 c = f.state > 0 ? green : red;
									ImGui::PushStyleColor(ImGuiCol_Button, c);
									ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.x + 0.08f, c.y + 0.08f, c.z + 0.08f, 1.0f));
									ImGui::PushStyleColor(ImGuiCol_ButtonActive, c);
									ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
									colours = 4;
								}
								if (ImGui::Button(f.state > 0 ? "+###state" : f.state < 0 ? "-###state" : " ###state", ImVec2(side, side)))
								{
									f.state = NextFilterState(f.state, 1);
									dirty = true;
								}
								if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
								{
									f.state = NextFilterState(f.state, -1);
									dirty = true;
								}
								if (colours) { ImGui::PopStyleColor(colours); }
								if (ImGui::IsItemHovered())
								{
									ImGui::SetTooltip("%s", f.state > 0 ? TR("AMF_FilterStateOnly", "+ : only menus with this word")
															: f.state < 0 ? TR("AMF_FilterStateOut", "- : menus with this word are hidden")
																		  : TR("AMF_FilterStateOff", "Off: kept, not used"));
								}
								ImGui::SameLine();
								ImGui::TextUnformatted(f.word.c_str());
								ImGui::SameLine();
								if (ImGui::SmallButton((std::string(icons::kClear) + "##remove").c_str())) { remove = i; }
								if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", TR("AMF_FilterRemove", "Remove this word")); }
								ImGui::PopID();
							}
							if (remove >= 0)
							{
								filterValues.listFilters.erase(filterValues.listFilters.begin() + remove);
								dirty = true;
							}
							if (filterValues.listFilters.empty()) { ImGui::TextDisabled("%s", TR("AMF_FilterNone", "No words yet.")); }
							ImGui::Separator();
							ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
							const bool entered = ImGui::InputTextWithHint("##filteradd", TR("AMF_FilterHint", "A word to filter by"),
																		  g_listFilterEdit, sizeof(g_listFilterEdit), ImGuiInputTextFlags_EnterReturnsTrue);
							keyboard::NoteTextField(ImGui::GetItemID());
							ImGui::SameLine();
							if ((ImGui::Button(TR("AMF_FilterAdd", "Add")) || entered) && g_listFilterEdit[0] != '\0')
							{
								std::string word = g_listFilterEdit;
								word.erase(std::remove(word.begin(), word.end(), ';'), word.end());   // ';' splits the saved list
								const auto lowerOf = [](std::string s) { for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); } return s; };
								const bool known = std::any_of(filterValues.listFilters.begin(), filterValues.listFilters.end(),
															   [&](const settings::Values::FilterWord& f) { return lowerOf(f.word) == lowerOf(word); });
								if (!word.empty() && !known) { filterValues.listFilters.push_back({ word, 1 }); dirty = true; }
								g_listFilterEdit[0] = '\0';
							}
							if (widgets::Toggle(TR("AMF_FilterMatchAll", "Match every + word"), &filterValues.listFilterMatchAll)) { dirty = true; }
							if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", TR("AMF_FilterMatchAllTip", "On: a menu needs every + word. Off: any one of them.")); }
							if (dirty)
							{
								settings::Save();
								logger::info("side list: filter words now {} ({} in use, match {})", filterValues.listFilters.size(),
											 std::count_if(filterValues.listFilters.begin(), filterValues.listFilters.end(),
														   [](const settings::Values::FilterWord& f) { return f.state != 0; }),
											 filterValues.listFilterMatchAll ? "all" : "any");
							}
							ImGui::EndPopup();
						}
					}
					const auto& sortValues = settings::Get();
					if (sortValues.loadMcmHelperConfigs || sortValues.loadSkyUIScriptMenus)
					{
						ImGui::SameLine();
						if (ImGui::SmallButton(TR("AMF_SortButton", "Sort")))
						{
							const auto r = mcmloader::SortIntoCategories(false);
							g_mcmSortStatus = FormatSortStatus(r);
							logger::info("side list: Sort pressed - {}", g_mcmSortStatus);
						}
						if (ImGui::IsItemHovered())
						{
							ImGui::SetTooltip("%s%s%s", TR("AMF_SortButtonTip", "Sort the MCM menus into categories, each under a separator for its kind. Undo is on Settings > Menu list."),
											  g_mcmSortStatus.empty() ? "" : "\n\n", g_mcmSortStatus.c_str());
						}
					}
					}
				}

				// Search the list by name. Once a load order registers thirty or more pages the
				// list is longer than the pane and finding one means scrolling; typing two or
				// three letters is faster than any amount of ordering.
				static char s_modFilter[64] = {};
				ImGui::SetNextItemWidth(-FLT_MIN);
				ImGui::InputTextWithHint("##modsearch", TR("AMF_SearchMods", "Search"),
										 s_modFilter, sizeof(s_modFilter));
				// The framework's OWN text boxes are drawn with ImGui directly, so the generator's
				// amf_NoteTextField hook (which only the C-API wrappers carry) never sees them; each one
				// notes itself, or the on-screen keyboard works on every mod's box except ours (the owner,
				// 2026-09-18: "the keyboard appears while in item explorer but not when using amfs own search bar").
				keyboard::NoteTextField(ImGui::GetItemID());
				g_frameSearchDrawn = ImGui::GetFrameCount();
				// Mirrored for the driving tool (report 2026-09-12: the box stops taking input after the
				// text is erased). Rect so the REAL box can be clicked; active/text/key state so the
				// failure is measured at the widget rather than guessed from a symptom.
				{
					const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
					g_searchRect[0].store(mn.x); g_searchRect[1].store(mn.y); g_searchRect[2].store(mx.x); g_searchRect[3].store(mx.y);
					g_searchActive.store(ImGui::IsItemActive());
					g_searchLen.store(static_cast<int>(std::strlen(s_modFilter)));
					{ std::scoped_lock l(g_searchTextLock); g_searchText = s_modFilter; }
					const ImGuiIO& sio = ImGui::GetIO();
					g_wantTextInput.store(sio.WantTextInput);
					g_backspaceDown.store(ImGui::IsKeyDown(ImGuiKey_Backspace));
					g_modCtrl.store(sio.KeyCtrl); g_modShift.store(sio.KeyShift); g_modAlt.store(sio.KeyAlt);
					g_activeIdMirror.store(GImGui ? GImGui->ActiveId : 0u);   // imgui_internal: which item holds keyboard input
				}

				const auto lower = [](std::string a_in) {
					std::transform(a_in.begin(), a_in.end(), a_in.begin(),
								   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					return a_in;
				};
				const std::string needle = lower(s_modFilter);
				// 2.1.5: the Filter button's saved words - + words show only matching rows (all of them or any one), - words hide
				std::vector<std::string> filterIn, filterOutWords;
				for (const auto& f : settings::Get().listFilters)
				{
					if (f.state > 0) { filterIn.push_back(lower(f.word)); }
					else if (f.state < 0) { filterOutWords.push_back(lower(f.word)); }
				}
				const bool filterMatchAll = settings::Get().listFilterMatchAll;
				const bool filterOnly = !filterIn.empty();
				const bool filterOut = !filterOutWords.empty();
				const auto passesFilter = [&](const std::string& a_name) {
					const std::string name = lower(a_name);
					const auto has = [&](const std::string& w) { return name.find(w) != std::string::npos; };
					if (std::any_of(filterOutWords.begin(), filterOutWords.end(), has)) { return false; }
					if (filterIn.empty()) { return true; }
					return filterMatchAll ? std::all_of(filterIn.begin(), filterIn.end(), has) : std::any_of(filterIn.begin(), filterIn.end(), has);
				};
				const bool flatList = !needle.empty() || filterOnly;

				// Player-facing order and names (menu-shell personalization). The rows carry the
				// REGISTRY index, so selection, the C API and DevBench addressing are unaffected.
				// Consumed ONCE for the frame, then applied to whichever row has the highlight. Taken
				// outside the loop so a single press cannot fire on several rows.
				// 2.1.5: only while the list has the highlight. Taken every frame before, Y in a page opened the options of
				// the list's row and pulled the highlight out to the list (the owner: "instead of zooming all the way out to
				// the main left pane"); in a page Y now goes up to the main tabs (see the nav block).
				const bool rowContextMenu = !g_contentNavLastFrame && bindings::TakeTriggered(bindings::Action::kContextMenu);
				const bool rowFavourite = bindings::TakeTriggered(bindings::Action::kFavourite);
				// GRAB AND MOVE (the owner, 2026-10-02: "pressing right stick will select the mod and then going and
				// moving the stick up or down will move its position up or down. And this should be rebindable"). The
				// grab picks the highlighted mod up; the two moves walk it one place at a time (Nudge - the same step as
				// the Reorder arrows); the grab again, B, or the highlight leaving it puts it down.
				const bool rowGrab = bindings::TakeTriggered(bindings::Action::kGrabMod);
				int grabStep = 0;
				if (bindings::TakeTriggered(bindings::Action::kGrabUp)) { grabStep = -1; }
				if (bindings::TakeTriggered(bindings::Action::kGrabDown)) { grabStep = 1; }
				if (const int stickStep = GrabStickStep(); grabStep == 0) { grabStep = stickStep; }
				bool grabbedFocused = false;

				int shown = 0;
				std::vector<personalization::DisplayEntry> displayRows = personalization::Order(entries);

				// MCM loader: an entry whose every page is hidden draws no row (below). It KEEPS its
				// place in the saved order and under its separator - only the drawing skips it, so it returns to the same
				// spot when a page is shown again - and a separator's "(n)" counts only the rows it actually shows; a
				// separator whose mods are all hidden still draws, as an empty one does (Main Agent's two conditions,
				// 2026-10-04).
				auto allPagesHidden = [&](const personalization::DisplayEntry& r) {
					if (r.separator || r.registryIndex < 0 || r.registryIndex >= static_cast<int>(entries.size())) { return false; }
					const auto& pages = entries[r.registryIndex].pages;
					return !pages.empty() && std::all_of(pages.begin(), pages.end(), [](const registry::Page& p) { return p.hidden; });
				};
				{
					personalization::DisplayEntry* separator = nullptr;
					for (auto& r : displayRows)
					{
						if (r.separator) { separator = &r; separator->children = 0; continue; }
						// a row the Filter hides is not counted either, so a folded "(n)" promises only rows that will show
						if (separator && r.depth > 0 && !allPagesHidden(r) && (!filterOut || passesFilter(r.displayName))) { ++separator->children; }
					}
				}
				for (const personalization::DisplayEntry& row : displayRows)
				{
					// The name the player actually reads is what they will type at, so the filter
					// matches the DISPLAY name - an aliased entry is findable by its alias. While searching, the list is
					// flat: separator rows step aside and a match inside a folded group is shown all the same.
					if (!needle.empty() && (row.separator || lower(row.displayName).find(needle) == std::string::npos))
					{
						continue;
					}
					// 2.1.5: the Filter button - only the matches (flat), or everything but them (separators kept)
					if (filterOnly && row.separator) { continue; }
					if ((filterOnly || filterOut) && !row.separator && !passesFilter(row.displayName)) { continue; }
					// a folded separator hides its mods (MO2's collapse - the owner, 2026-10-02)
					if (!flatList && row.hidden) { continue; }

					// MCM loader: an entry whose every page is hidden has nothing to show, so it has no
					// row - the MCM loader's off switch hides all of its entries' pages. (Before this an all-hidden entry
					// kept an empty row; worth confirming with the main AMF line before this merges.)
					if (allPagesHidden(row)) { continue; }

					if (row.separator)
					{
						DrawSeparatorRow(entries, row, rowContextMenu, rowFavourite);
						continue;
					}
					++shown;

					const bool isOpen = (sel == "mod" && selMod == row.registryIndex);
					if (isOpen && navToSelected)
					{
						ImGui::SetKeyboardFocusHere();
						g_frameFocusHere = ImGui::GetFrameCount();
					}

					ImGui::PushID(row.modName.c_str());

					// A FILLED WHITE BOX to the left of a favourited menu's name (the owner,
					// 2026-09-19), in a gutter every row reserves so the names stay in one column
					// whether or not they are pinned.
					const bool favourite = personalization::IsFavourite(row.modName);
					const float boxSide = ImGui::GetFontSize() * 0.55f;
					const float gutter = boxSide + ImGui::GetStyle().ItemInnerSpacing.x * 2.0f;
					const ImVec2 rowTopLeft = ImGui::GetCursorScreenPos();

					// a mod under a separator sits one step in, so the group reads as a group
					const float rowIndent = gutter + (!flatList && row.depth > 0 ? ImGui::GetFontSize() * 0.9f : 0.0f);
					ImGui::Indent(rowIndent);
					const bool picked = ImGui::Selectable(row.displayName.c_str(), isOpen);
					NoteHoverFrame();   // 2.1.5: the theme's frame round the menu name under the mouse
					ImGui::Unindent(rowIndent);

					// the picked-up mod is boxed, so it reads as held rather than merely highlighted
					if (!g_grabbedMod.empty() && g_grabbedMod == row.modName)
					{
						const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
						ImDrawList* dl = ImGui::GetWindowDrawList();
						dl->AddRectFilled(mn, mx, IM_COL32(255, 255, 255, 40));
						dl->AddRect(mn, mx, ImGui::GetColorU32(ImGuiCol_Text), 0.0f, 0, 2.0f);
					}

					if (favourite)
					{
						const float top = rowTopLeft.y + (ImGui::GetTextLineHeight() - boxSide) * 0.5f;
						const float left = rowTopLeft.x + ImGui::GetStyle().ItemInnerSpacing.x * 0.5f;
						ImGui::GetWindowDrawList()->AddRectFilled(
							ImVec2(left, top), ImVec2(left + boxSide, top + boxSide),
							IM_COL32(255, 255, 255, 255));
					}

					if (picked)
					{
						sel = "mod";
						selMod = row.registryIndex;
						changed = true;
					}

					// Y IS THE RIGHT-CLICK (the owner, 2026-09-19: "y should do the same as the right
					// click"), and from 1.9.6 it is an ordinary bindable action rather than a
					// hard-wired key, so it can be moved from the Controls page like everything else.
					// Favouriting the highlighted mod is a second action, for players who would
					// rather not go through the menu at all.
					if (ImGui::IsItemFocused())
					{
						if (rowContextMenu) { ImGui::OpenPopup("##modctx"); }
						if (rowFavourite)
						{
							personalization::ToggleFavourite(row.modName);
							settings::Save();
						}
						if (rowGrab)
						{
							if (g_grabbedMod == row.modName)
							{
								g_grabbedMod.clear();
								logger::info("menu order: put \"{}\" down", row.modName);
							}
							else
							{
								g_grabbedMod = row.modName;
								logger::info("menu order: picked \"{}\" up", row.modName);
							}
						}
						if (g_grabbedMod == row.modName) { grabbedFocused = true; }
					}

					// RIGHT-CLICK: favourite/unfavourite, rename, and the two moves that a pinned
					// list makes obvious. Rename hands off to the modal below, so the text field is
					// drawn once rather than once per row.
					// A TIGHT BOX (the owner, 2026-09-21: "fix the empty space ... make the outer bounds of the box smaller so
					// it fits around the 3 options"). The theme pads every window by the knotwork corner + 8 px so the
					// frame art has room; a context menu carries no frame art, so that padding was an empty band around
					// three short items. The popup reads WindowPadding at Begin, so it is pushed just for that call.
					const float ctxPad = ImGui::GetFontSize() * 0.35f;
					ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ctxPad, ctxPad));
					const bool ctxOpen = ImGui::BeginPopupContextItem("##modctx");
					ImGui::PopStyleVar();
					if (ctxOpen)
					{
						if (ImGui::MenuItem(favourite ? TR("AMF_Unfavourite", "Remove from favourites")
													  : TR("AMF_Favourite", "Add to favourites")))
						{
							personalization::ToggleFavourite(row.modName);
							settings::Save();
						}
						if (ImGui::MenuItem(TR("AMF_Rename", "Rename...")))
						{
							g_renameTarget = row.modName;
							const std::string alias = personalization::GetAlias(row.modName);
							std::snprintf(g_renameBuffer, sizeof(g_renameBuffer), "%s", alias.c_str());
							g_renameOpenPending = true;
						}
						// MCM menus (the owner, 2026-10-05: "they can just be hidden in AMF through the context menu"): leave this
						// one to SkyUI's own MCM only - the same as its switch under Framework Settings' "Choose which MCM menus
						// appear here", which brings it back. Its import key is found by its entry name (the import list's entries
						// are the Menu-list names, all 83 checked in Njordlinger Test).
						{
							std::string mcmKey;
							for (const auto& r : mcmloader::ImportList())
							{
								if (r.imported && r.entry == row.modName) { mcmKey = r.key; break; }
							}
							if (!mcmKey.empty() && ImGui::MenuItem(TR("AMF_KeepInSkyUIOnly", "Keep in SkyUI only")))
							{
								logger::info("context menu: '{}' kept in SkyUI only (left out of this menu)", row.modName);
								mcmloader::SetMenuImported(mcmKey, false);
							}
						}
						ImGui::Separator();
						// the top of ITS OWN group, not of the list - pinning is what puts a mod at the very top (the owner,
						// 2026-10-02: "That way it's distinct from favoriting")
						if (ImGui::MenuItem(TR("AMF_MoveToTop", "Move to the top")))
						{
							personalization::MoveToGroupTop(entries, row.modName);
							settings::Save();
						}
						// REORDER (the owner, 2026-10-02): a little window to the right with an up and a down arrow, one place per
						// press. Arrow BUTTONS, not menu items, so the window stays open and the mod can be walked several
						// places in a row; the Menu list page's numbers follow (they read the same order).
						// the submenus get the context menu's tight padding too - the theme's frame padding left an empty band
						// round the two arrows and the separator names (seen in the 2.0.0 release captures)
						ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ctxPad, ctxPad));
						const bool reorderOpen = ImGui::BeginMenu(TR("AMF_Reorder", "Reorder"));
						ImGui::PopStyleVar();
						if (reorderOpen)
						{
							if (ImGui::ArrowButton("##nudgeup", ImGuiDir_Up))
							{
								if (personalization::Nudge(entries, row.modName, -1)) { settings::Save(); }
							}
							if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", TR("AMF_MoveUp", "Move up one place")); }
							ImGui::SameLine();
							if (ImGui::ArrowButton("##nudgedown", ImGuiDir_Down))
							{
								if (personalization::Nudge(entries, row.modName, 1)) { settings::Save(); }
							}
							if (ImGui::IsItemHovered()) { ImGui::SetTooltip("%s", TR("AMF_MoveDown", "Move down one place")); }
							ImGui::EndMenu();
						}
						// SEPARATORS (the owner, 2026-10-02): make one above this mod, or send this mod into one.
						if (ImGui::MenuItem(TR("AMF_NewSeparatorAbove", "New separator above")))
						{
							BeginNewSeparator(entries, row.modName);
						}
						const auto separators = personalization::Separators();
						ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ctxPad, ctxPad));
						const bool sendToOpen = ImGui::BeginMenu(TR("AMF_SendTo", "Send to"), !separators.empty() || row.depth > 0);
						ImGui::PopStyleVar();
						if (sendToOpen)
						{
							for (const auto& sep : separators)
							{
								ImGui::PushID(sep.id.c_str());
								if (ImGui::MenuItem(sep.name.c_str()))
								{
									personalization::SendTo(entries, row.modName, sep.id);
									settings::Save();
								}
								ImGui::PopID();
							}
							if (row.depth > 0)
							{
								ImGui::Separator();
								if (ImGui::MenuItem(TR("AMF_SendToNone", "No separator")))
								{
									personalization::SendTo(entries, row.modName, std::string());
									settings::Save();
								}
							}
							ImGui::EndMenu();
						}
						ImGui::EndPopup();
					}

					ImGui::PopID();
				}
				if (!g_grabbedMod.empty())
				{
					if (!grabbedFocused)
					{
						logger::info("menu order: put \"{}\" down (the highlight left it)", g_grabbedMod);
						g_grabbedMod.clear();
					}
					else if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false))
					{
						logger::info("menu order: put \"{}\" down (B)", g_grabbedMod);
						g_grabbedMod.clear();
					}
					else if (grabStep != 0 && personalization::Nudge(entries, g_grabbedMod, grabStep))
					{
						settings::Save();
						logger::info("menu order: \"{}\" stepped {}", g_grabbedMod, grabStep < 0 ? "up" : "down");
					}
				}
				if (entries.empty()) { ImGui::TextDisabled("%s", TR("AMF_NoneRegistered", "none registered")); }
				else if (shown == 0) { ImGui::TextDisabled("%s", TR("AMF_NoMatch", "no mod matches that")); }
				const bool sideHasNav = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
				ImGui::EndChild();
				// Captured BEFORE the rename popup below: the knotwork is drawn around the side
				// PANE, and a popup submitted in between would leave GetItemRect* describing the
				// popup instead (the frame would jump to wherever the modal sat).
				const ImVec2 sidePaneMin = ImGui::GetItemRectMin();
				const ImVec2 sidePaneMax = ImGui::GetItemRectMax();

				// The rename modal the right-click menu asks for. Opened and drawn OUT HERE, at the
				// window's own id level, so it is one popup rather than one per row.
				if (g_renameOpenPending)
				{
					ImGui::OpenPopup("##amf_rename");
					g_renameOpenPending = false;
				}
				// No title bar (2.1.1): the id has no visible title, so the bar was an empty strip above the box.
				if (ImGui::BeginPopupModal("##amf_rename", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
				{
					const bool renamingSeparator = personalization::IsSeparator(g_renameTarget);
					ImGui::TextUnformatted(renamingSeparator ? TR("AMF_SeparatorNameTitle", "Name this separator")
															 : TR("AMF_RenameTitle", "Show this menu as"));
					if (!renamingSeparator) { ImGui::TextDisabled("%s", g_renameTarget.c_str()); }
					ImGui::Spacing();
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
					if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); }
					const bool entered = ImGui::InputTextWithHint("##renamefield",
																  renamingSeparator ? TR("AMF_SeparatorDefaultName", "New separator") : g_renameTarget.c_str(),
																  g_renameBuffer, sizeof(g_renameBuffer),
																  ImGuiInputTextFlags_EnterReturnsTrue);
					keyboard::NoteTextField(ImGui::GetItemID());
					ImGui::TextDisabled("%s", renamingSeparator ? TR("AMF_SeparatorNameHint", "A separator groups the menus below it, up to the next one.")
																: TR("AMF_RenameHint", "Leave it empty to go back to the mod's own name."));
					ImGui::Spacing();
					const bool ok = ImGui::Button(TR("AMF_RenameOk", "Rename")) || entered;
					ImGui::SameLine();
					const bool cancel = ImGui::Button(TR("AMF_RenameCancel", "Cancel"));
					if (ok && !(renamingSeparator && g_renameBuffer[0] == '\0'))   // a separator keeps a name
					{
						personalization::SetAlias(g_renameTarget, g_renameBuffer);
						settings::Save();
					}
					if (ok || cancel)
					{
						g_renameTarget.clear();
						ImGui::CloseCurrentPopup();
					}
					ImGui::EndPopup();
				}

				if (knot)
				{
					DrawKnotworkAround(ImGui::GetWindowDrawList(), sidePaneMin, sidePaneMax);
				}

				// Room between the panes for both knotwork frames plus a breath of air.
				ImGui::SameLine(0.0f, knot ? kKnotOutset * 4.0f : -1.0f);

				// ---- CONTENT PANE -------------------------------------------------------------
				if (g_focusPane == 2) { ImGui::SetNextWindowFocus(); g_focusPane = 0; g_frameWindowFocus = ImGui::GetFrameCount(); }
				// 2.1.5: the help bar sits under this pane while a converted MCM page asks for it - the pane gives up its
				// height plus the same gap the knotwork frames keep between the panes.
				const bool helpBarOn = helpbar::Active();
				const float helpBarGap = knot ? kKnotOutset * 4.0f : ImGui::GetStyle().ItemSpacing.y;
				ImGui::BeginChild("##content", ImVec2(0.0f, helpBarOn ? -(helpbar::Height() + helpBarGap) : 0.0f), true);
				// Re-measured every frame. A pane with no tab bar leaves these at zero, so left
				// falls straight back to the mod list exactly as it always did.
				g_prevTabIndex = g_tabIndex;   // 2.1.5: where Y sends the highlight
				g_tabCount = 0;
				g_tabIndex = 0;
				g_tabBarHasNav = false;
				// A page re-declares its inner tabs every frame it draws; stale numbers must not steer nav.
				g_innerFresh = false;
				std::string curTabName;
				if (sel == "settings")      { DrawFrameworkSettingsPane(); }
				else if (sel == "controls") { DrawControlsPane(); }
				else if (sel == "help")     { DrawHelpPane(); }
				else if (sel == "mod" && !entries.empty())
				{
					const registry::Entry& entry = entries[selMod];
					{
						const std::string alias = personalization::GetAlias(entry.modName);
						ImGui::TextUnformatted(alias.empty() ? entry.modName.c_str() : alias.c_str());
					}
					ImGui::Separator();
					// Pages a mod hid with AMF_SetPageVisible (1.8.3) are left out; the rest keep their order.
					std::vector<const registry::Page*> visiblePages;
					for (const registry::Page& page : entry.pages)
					{
						if (!page.hidden) { visiblePages.push_back(&page); }
					}
					if (visiblePages.size() == 1)
					{
						visiblePages[0]->render();
					}
					else if (visiblePages.size() > 1 && ImGui::BeginTabBar("##pages",ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton))   // a mod with many sections keeps whole labels: the bar scrolls, and the list button on the left opens every section by name (Character Progression Control reached twelve tabs and the default policy squeezed them to "Level... Expe... Skills")
					{
						int index = 0;
						for (const registry::Page* pagePtr : visiblePages)
						{
							const registry::Page& page = *pagePtr;
							// A D-pad step asks for its tab for exactly ONE frame. Every other
							// frame the bar owns its own selection, so the D-pad, a mouse click
							// and the tab-list popup never fight over which tab is open.
							const ImGuiTabItemFlags flags =
								(index == g_tabRequest) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
							FocusMainTabIfAsked(index);   // 2.1.5: Y in a page lands here
							const bool open = ImGui::BeginTabItem(page.pageName.c_str(), nullptr, flags);
							// Asked of the tab itself rather than worked out from where nav "should"
							// be (rule 30): the item just submitted is the tab button, selected or not.
							if (ImGui::IsItemFocused()) { g_tabBarHasNav = true; }
							if (open)
							{
								g_tabIndex = index;
								curTabName = page.pageName;
								page.render();
								ImGui::EndTabItem();
							}
							++index;
						}
						g_tabCount = index;
						g_tabRequest = -1; g_bumperFocusMain = false;
						ImGui::EndTabBar();
					}
				}
				else { DrawFrameworkSettingsPane(); }
				if (!g_innerFresh) { g_innerCount = 0; g_innerIndex = 0; }
				const bool contentHasNav = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
				g_contentNavLastFrame = contentHasNav;
				ImGui::EndChild();
				const ImVec2 contentMin = ImGui::GetItemRectMin();
				const ImVec2 contentMax = ImGui::GetItemRectMax();
				helpbar::NotePane(contentMin.x, contentMin.y, contentMax.x, contentMax.y);
				if (knot)
				{
					DrawKnotworkAround(ImGui::GetWindowDrawList(), contentMin, contentMax);
				}
				if (helpBarOn)
				{
					ImGui::SetCursorScreenPos(ImVec2(contentMin.x, contentMax.y + helpBarGap));
					helpbar::Draw(contentMax.x - contentMin.x);
					if (knot)
					{
						DrawKnotworkAround(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
					}
				}

				// Draw the OUTER window's knotwork frame LAST, on the window's own draw list so it
				// sits on top of the content and exactly over ImGui's border, at the window rect.
				// The transparent centre keeps the panes fully visible.
				if (knot)
				{
					const ImVec2 wp = ImGui::GetWindowPos();
					const ImVec2 ws = ImGui::GetWindowSize();
					DrawKnotworkFrame(ImGui::GetWindowDrawList(), wp, ImVec2(wp.x + ws.x, wp.y + ws.y));
				}
				if (changed)
				{
					std::scoped_lock l(g_selLock);
					g_selNode = sel; g_selMod = selMod;
				}
				// Right out of the list, left back into it. Only when nothing is being edited, so
				// pushing left inside a slider adjusts the value instead of leaving the pane. Both
				// sticks and the D-pad and the arrow keys all count as the same "move across".
				{
					const bool wantsRight = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, false) ||
											ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight, false) ||
											ImGui::IsKeyPressed(ImGuiKey_RightArrow, false);
					const bool wantsLeft = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, false) ||
										   ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft, false) ||
										   ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false);
					const bool editing = ImGui::IsAnyItemActive();
					// A driving tool's press is read here, alongside the real ones, so amf.menu
					// op=nav proves THIS decision rather than a private path around it.
					const int  driven   = g_navRequest.exchange(0);
					const bool navLeft  = wantsLeft  || driven == 1;
					const bool navRight = wantsRight || driven == 2;
					if (g_pendingSideRight)
					{
						g_pendingSideRight = false;
						if (sideHasNav && GImGui && GImGui->NavJustMovedToId != 0)
						{
							logger::debug("nav: ImGui moved to a widget in the list pane; staying");
						}
						else if (sideHasNav && !editing)
						{
							g_focusPane = 2;
							logger::debug("nav: list -> options");
						}
					}
					else if (sideHasNav && navRight && !editing)
					{
						// a driven op=nav press moves no ImGui cursor, so it still crosses next frame
						g_pendingSideRight = true;
						logger::debug("nav: right press in the list noted, deciding next frame");
					}
					// Inside the content pane a sideways press is ImGui's first: if there is a widget to that
					// side, the cursor moves there and nothing else happens. A press that moved nothing never
					// steps a tab (1.9.0 - tabs change only by selecting and activating them); a left press
					// that moved nothing leaves for the mod list. The
					// decision is taken one frame late, when ImGui has reported the move (NavJustMovedToId),
					// so the two never race. A driven op=nav press moves no ImGui cursor and therefore always
					// steps, which keeps the driving tool's proof of this path intact.
					else if (contentHasNav && (navRight || navLeft) && !editing && g_pendingTabStep == 0)
					{
						g_pendingTabStep = navRight ? 1 : -1;
						logger::debug("nav: sideways press noted ({}), deciding next frame", navRight ? "right" : "left");
					}
					else if (g_pendingTabStep != 0)
					{
						const int step = g_pendingTabStep;
						g_pendingTabStep = 0;
						const bool imguiMoved = GImGui && GImGui->NavJustMovedToId != 0;
						if (imguiMoved)
						{
							logger::debug("nav: ImGui moved to a widget; no tab step");
						}
						else if (!contentHasNav || editing)
						{
							logger::debug("nav: content lost focus before the step was decided; dropped");
						}
						// 1.9.0: NO tab stepping here any more (the owner, 2026-09-18: "i want the only way for the
						// dpad to switch tabs in our mods is to select said tab and activate it, im tired of switching
						// tabs by accident"). A tab - the framework's page tabs and a page's own inner bar alike - changes
						// only when the highlight is moved ONTO the tab and it is activated, which ImGui's own nav does.
						// A right press that moved nothing now does nothing; a left press that moved nothing leaves for
						// the mod list, whatever tab is open.
						else if (step < 0)
						{
							// Nothing to move to on the left: back to the list (from any tab).
							g_focusPane = 1;
							g_navToSelected = true;  // land on the open entry, not the last cursor position
							logger::debug("nav: options -> list (returning to the open entry)");
						}
					}
					// Stepping tabs must NOT depend on where ImGui's nav focus happens to be.
					//
					// This branch used to require g_tabBarHasNav - the cursor sitting literally on the tab bar - and in
					// practice it almost never is: opening a mod leaves focus in the page below the bar, so right never
					// advanced the tab and the index stayed at 0. Left then had nothing to step back through and fell
					// straight to its last branch, dropping the player out to the mod list. That is the whole of the
					// reported fault (the owner, 2026-09-16: "d-pad left, making it go all the way back to the left pane
					// instead of just scrolling the tabs ... it should only go back to the far left pane when you're
					// already done scrolling left and there's nothing left to scroll") - left was never the broken half.
					//
					// Measured, not guessed: amf.menu op=state reports page/pageIndex/pageCount, and two op=nav dir=right
					// presses on Wheeler left it at idx=0 of 3.
					//
					// !editing still guards it, so pushing right inside a slider adjusts the value rather than changing tab.

				}

				// Publish the tab bar for the DevBench state JSON, so a driving tool can assert
				// which section is open without reading pixels.
				{
					std::scoped_lock l(g_selLock);
					g_selTabName = curTabName; g_selTabIndex = g_tabIndex; g_selTabCount = g_tabCount;
				}

				// Y IN A PAGE GOES UP TO THE MAIN TABS (2.1.5, the owner, 2026-10-07): the open tab of the page's main bar takes
				// the highlight next frame (FocusMainTabIfAsked). Not while a field is being edited or the on-screen keyboard
				// is up (its backspace is Y); a page with no main bar leaves Y alone.
				if (contentHasNav && g_tabCount > 1 && !ImGui::IsAnyItemActive() && !keyboard::Capturing() &&
					bindings::TakeTriggered(bindings::Action::kContextMenu))
				{
					g_focusMainTabs = true;
					logger::debug("nav: Y in the page -> the main tabs (tab {})", g_tabIndex);
				}

				// THE BUMPERS WALK THE TABS (the owner, 2026-09-19: "bumpers navigate tabs, dpad
				// doesnt"). The D-pad deliberately never steps a tab - moving the highlight onto one
				// and activating it is the other way, and the standing rule forbids stepping - so tab
				// navigation gets controls of its own. The INNERMOST bar that exists takes the press:
				// a mod's own tab bar if it drew one this frame, otherwise the framework's page bar.
				{
					const int step = (bindings::TakeTriggered(bindings::Action::kTabNext) ? 1 : 0) -
									 (bindings::TakeTriggered(bindings::Action::kTabPrev) ? 1 : 0);
					if (step != 0)
					{
						// Inside a page with its own tabs (a mod's, or Appearance / MCM menus' sub-tabs) the bumpers walk ONLY
						// those, wrapping round (the owner, 2026-10-07: "I actually want them to wrap around ... using the
						// shoulder buttons in sub tabs only moves the sub tabs"); Y takes the highlight up to the main tabs.
						if (g_innerFresh && g_innerCount > 1)
						{
							g_innerRequest = (g_innerIndex + step + g_innerCount) % g_innerCount;
							g_bumperFocusInner = true;
							logger::debug("nav: bumper -> inner tab {}", g_innerRequest);
						}
						else if (g_tabCount > 1)
						{
							g_tabRequest = (g_tabIndex + step + g_tabCount) % g_tabCount;
							g_bumperFocusMain = true;
							logger::debug("nav: bumper -> tab {}", g_tabRequest);
						}
					}
				}

				// Controller scheme: while a slider/drag is ACTIVE the right stick moves it and the
				// left stick is held off, so navigation and adjustment never fight. Sampled here,
				// inside the frame, and read by the input thread's translation step.
				input::SetItemActive(ImGui::IsAnyItemActive());
			}
			ImGui::End();
		}

		// -----------------------------------------------------------------------------------
		// Present - every frame. Fires BEFORE init completes, hence the atomic gate.
		// -----------------------------------------------------------------------------------
		struct PresentHook
		{
			static inline REL::Relocation<void(std::uint32_t)> func;

			static void thunk(std::uint32_t a_timer)
			{
				func(a_timer);

				if (!g_d3dReady.load(std::memory_order_acquire))
				{
					return;
				}

				// SKSE MENU FRAMEWORK'S EVENTS (2.1.4 - NPC Preset Applier, a Nexus report 2026-10-07: its portraits were
				// queued and never made under AMF). SMF 3 sends its RegisterEvent / RegisterEventPriority listeners four
				// events from its frame (QTR-Modding/SKSE-Menu-Framework-3, Hooks.cpp Render and WindowManager.cpp):
				// 1 open and 2 close as its menu comes and goes, 3 before every frame's ImGui work and 4 after the frame
				// is drawn. AMF kept the listeners and never sent one, so a mod that works on that per-frame tick (NPA
				// renders its portraits there) waited forever. Sent here at the same points: open / close first, as the
				// state is seen at the frame's start, then 3; 4 after the draw below.
				{
					static bool s_smfOpen = false;
					const bool open = IsMainWindowVisible() || consumer::AnyBlockingWindowOpen();
					if (open != s_smfOpen)
					{
						s_smfOpen = open;
						compat::FireMenuEvent(open ? compat::MenuEvent::kOpenMenu : compat::MenuEvent::kCloseMenu);
					}
				}
				compat::FireMenuEvent(compat::MenuEvent::kBeforeRender);

				// A font or text-size change rebuilds the atlas. This MUST happen before
				// ImGui_ImplDX11_NewFrame: that call is where the DX11 backend recreates its
				// device objects (font texture included) when they are missing. The old order -
				// invalidating AFTER the backend NewFrame had already run - destroyed the font
				// texture with nothing left in the frame to recreate it, so the frame rendered
				// its draw data against a dead texture and crashed the moment the font or the
				// text-size slider changed (author playtest, 2026-08-31).
				if (g_fontRebuildPending.exchange(false))
				{
					g_iconFaceAddPending = false;   // the full build takes every wanted face
					ImGui_ImplDX11_InvalidateDeviceObjects();
					BuildFonts();
				}
				else if (g_iconFaceAddPending.exchange(false))
				{
					ImGui_ImplDX11_InvalidateDeviceObjects();
					AddIconFaces();
				}

				ImGui_ImplDX11_NewFrame();
				ImGui_ImplWin32_NewFrame();

				// THE IMAGE, NOT THE WINDOW (2.0.8 - Soulsthat, 2026-10-04, 2560x1440: the menu and its tooltips clipped at the
				// right and bottom, the right side before the left). The Win32 backend sizes the display from the game WINDOW's
				// client rect, but everything ImGui draws lands 1:1 on the swap chain's back buffer. When the game renders a
				// smaller image than its window (a lower render resolution scaled up in a borderless window), the window's
				// centre sat right of and below the image's, the size cap let the menu run off the image, and tooltips clamped
				// to an edge past it. AMF's cursor is its own (mouse deltas clamped to DisplaySize), so nothing maps window
				// coordinates; the back buffer's size is the whole truth.
				if (g_swapChain)
				{
					DXGI_SWAP_CHAIN_DESC sd{};
					if (SUCCEEDED(g_swapChain->GetDesc(&sd)) && sd.BufferDesc.Width > 0 && sd.BufferDesc.Height > 0)
					{
						ImGuiIO& io = ImGui::GetIO();
						const ImVec2 image(static_cast<float>(sd.BufferDesc.Width), static_cast<float>(sd.BufferDesc.Height));
						if (io.DisplaySize.x != image.x || io.DisplaySize.y != image.y)
						{
							static ImVec2 s_logged{ -1.0f, -1.0f };   // render thread only; log each new pair once
							if (s_logged.x != io.DisplaySize.x || s_logged.y != io.DisplaySize.y)
							{
								s_logged = io.DisplaySize;
								logger::info("display: the game window is {}x{} but draws a {}x{} image - the menu uses the image's size",
											 io.DisplaySize.x, io.DisplaySize.y, image.x, image.y);
							}
							io.DisplaySize = image;
						}
					}
				}

				// Give an external launcher its say before this frame's visibility is read, so a
				// menu it just asked for opens on the same frame rather than the next one.
				compat::PumpExternalWindow();

				const bool visible = g_windowVisible.load(std::memory_order_acquire);

				// TWO STATES, NOT ONE (2.0.4). `visible` is OUR menu: it alone pauses the game, takes
				// Escape as "close" and draws the framework window. `interactive` is "someone on screen
				// has the player's input" - our menu, or a mod's own window that is open and blocking -
				// and it is what feeds ImGui, draws the cursor and turns text entry on. A mod's window
				// never pauses the game: [Menu] bPauseGame is the player's setting for OUR menu, and a
				// mod that wants the world stopped behind its window is the one to decide that.
				// The gate is the flags AND what the window really is (see consumer::AnyWindowOwnsInput): open,
				// BlockUserInput, and a window it drew last frame that takes the mouse. The flags alone made
				// StepUpOnto SKSE's passive NPC perf overlay take the whole game's input during play.
				const bool consumerOwnsInput = consumer::AnyWindowOwnsInput();
				const bool interactive = visible || consumerOwnsInput;
				{
					static bool s_lastConsumer = false;   // render thread only; transition log
					if (consumerOwnsInput != s_lastConsumer)
					{
						s_lastConsumer = consumerOwnsInput;
						logger::info("input: a mod's window{} {} the input (framework menu {}){}",
									 consumerOwnsInput ? " \"" + consumer::InputOwnerName() + "\"" : std::string(),
									 consumerOwnsInput ? "took" : "handed back",
									 visible ? "open" : "closed",
									 consumerOwnsInput ? " - cursor shown, game input held; the game is not paused" : "");
					}
				}

				// THE PAUSE LETS A MENU'S SCRIPTS RUN (2.1.5, the owner, 2026-10-08: Atlas Map Markers' other pages "just says loading"
				// until AMF is closed and opened again). In this list the game's script engine stops while the game is paused (logic
				// library 9107), so a converted MCM page's SetPage waited out its 15 s and never drew. While a call has been waiting
				// a moment, the pause lets go; once the queue is empty it holds again. Time moves only while a page is loading.
				{
					static bool s_lifted = false;   // render thread only
					const auto waiting = mcmloader::scripts::WaitingFor();
					// at most 3 s for any one call: a call stuck for some other reason must not leave the world running behind the menu
					const bool lift = waiting < std::chrono::milliseconds(3000) &&
						(waiting >= std::chrono::milliseconds(120) || (s_lifted && waiting.count() > 0));
					if (lift != s_lifted)
					{
						s_lifted = lift;
						if (visible)
						{
							logger::debug("pause: {} for a menu's scripts", lift ? "let go" : "held again");
						}
					}
					// not on top of the journal's own pause (the System row): ours there only added a count to take off again
					const bool nestedNow = g_nested.load(std::memory_order_acquire);
					SyncGamePause(visible && settings::Get().pauseGameWhileOpen && !nestedNow, visible && s_lifted);
				}

				// Open-transition work happens HERE, not in ToggleMainWindow - the toggle is
				// flipped on the input thread, and cursor centring touches ImGui state.
				// It runs on the rising edge of INPUT OWNERSHIP, so a mod's window that takes the
				// input gets the same centred cursor and clean key state as our menu. Our menu opening
				// over a mod's window that already has the input is NOT a new edge: ImGui has been
				// fed all along, so nothing is stale, and the cursor stays where the player has it.
				{
					static bool s_wasInteractive = false;   // render thread only
					const bool justOpened = g_justOpened.exchange(false, std::memory_order_acq_rel);
					if (interactive && (!s_wasInteractive || (justOpened && !consumerOwnsInput)))
					{
						input::OnMenuOpened();
					}
					s_wasInteractive = interactive;
				}
				if (visible) { mcmloader::LearnFromLayoutIfChanged(); }   // the sort learns from menus moved by hand
				// Published only AFTER the rising edge has cleared the queue, so nothing the input thread
				// queues for a mod's window can be thrown away as stale by the edge that let it in.
				g_consumerInput.store(consumerOwnsInput, std::memory_order_release);

				// Translation runs after the backends' NewFrame (so our queued io.Add*Event
				// calls land after, and therefore win over, the Win32 backend's own
				// GetCursorPos-based mouse update) and before ImGui::NewFrame consumes them.
				if (interactive)
				{
					input::ProcessQueuedEvents();
				}

				ImGuiIO& io = ImGui::GetIO();

				if (g_artReloadPending.exchange(false, std::memory_order_acq_rel)) { skin::Reload(); }   // 2.1.6, op=art

				// Software cursor while the menu (or a mod's blocking window) has the input - the game
				// hides and recentres the OS cursor at will, so ImGui draws its own at the position we
				// integrate. RaceMenu Atelier hides the game's own cursor for exactly this reason.
				io.MouseDrawCursor = interactive;

				// Nav mode follows the EXPLICIT setting live (the toggle sits on the settings
				// page itself). Never auto-detected - that is the nav-focus-drift bug.
				if (input::UsingController())
				{
					io.ConfigFlags = (io.ConfigFlags | ImGuiConfigFlags_NavEnableGamepad) & ~ImGuiConfigFlags_NavEnableKeyboard;
					// REQUIRED for gamepad nav to respond at all: ImGui only processes the
					// GamepadFace*/GamepadDpad* key events we feed when the backend advertises a
					// gamepad. Without this flag NavEnableGamepad is inert - which is why toggling
					// controller mode and pressing every button did nothing (design decision, 2026-08-28).
					io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
				}
				else
				{
					io.ConfigFlags = (io.ConfigFlags | ImGuiConfigFlags_NavEnableKeyboard) & ~ImGuiConfigFlags_NavEnableGamepad;
					io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad;
				}

				// Does a text field hold the keyboard? Sampled here, every frame, because the input
				// hook on the game thread turns the ENGINE's text entry on and off from it: Skyrim only
				// turns WM_CHAR into a CharEvent while ControlMap's text-entry count is up, and a
				// CharEvent is the only way a letter ever reaches ImGui in this framework (there is no
				// WndProc hook). Without it the search bar and every mod's text box took clicks and
				// navigation but not a single character (phbd01, 2026-09-19).
				// A mod's own window counts too (2.0.4): RaceMenu Atelier's name field types through here.
				g_wantTextInput.store(interactive && io.WantTextInput, std::memory_order_release);

				// WHY THE KEYBOARD WAS LOST (1.9.5). ImGui drops ActiveId by itself when the item
				// that holds it is NOT SUBMITTED in a frame - ActiveIdIsAlive stops matching
				// ActiveId and NewFrame clears it. That is a different fault from something calling
				// SetKeyboardFocusHere or focusing another window, and from the player clicking
				// elsewhere, and the three are indistinguishable on screen. So the report names
				// which of them it was, with each suspect stamped on the frame it actually fired.
				{
					static ImGuiID s_lastActive = 0;
					static int s_lastAliveFrame = -1;
					// Which window owned the field while it was alive, and which one holds nav now.
					// A consumer mod draws its own windows every frame through the framework, and one
					// of them taking focus would look exactly like this from the player's side.
					static char s_ownerWindow[64] = "";
					const int frame = ImGui::GetFrameCount();
					const ImGuiID nowActive = GImGui ? GImGui->ActiveId : 0u;
					if (s_lastActive != 0 && nowActive != s_lastActive && keyboard::IsTextField(s_lastActive))
					{
						const ImGuiIO& dio = ImGui::GetIO();
						logger::info("input: text field {} lost the keyboard on frame {} -> active now {} | "
									 "searchDrawnFrame={} (age {}), focusHereFrame={} (age {}), "
									 "windowFocusFrame={} (age {}), navConsumedFrame={} (age {}) | "
									 "mouseClicked={} mouseDown={} mousePos=({:.0f},{:.0f}) | "
									 "navId={} hoveredWindowMatters={}",
									 s_lastActive, frame, nowActive,
									 g_frameSearchDrawn, frame - g_frameSearchDrawn,
									 g_frameFocusHere, frame - g_frameFocusHere,
									 g_frameWindowFocus, frame - g_frameWindowFocus,
									 g_frameNavConsumed, frame - g_frameNavConsumed,
									 dio.MouseClicked[0], dio.MouseDown[0],
									 dio.MousePos.x, dio.MousePos.y,
									 GImGui ? GImGui->NavId : 0u,
									 s_lastAliveFrame);
						// EVERY key ImGui saw on the frame it died. An InputText deactivates itself on
						// Enter, on Escape, on Tab and on a nav CANCEL (B on a pad) - and from the
						// player's side all of those look like the box simply going dead. Listing the
						// keys is the only way to tell which, and whether the press was even real.
						{
							std::string keys;
							for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
							{
								const auto key = static_cast<ImGuiKey>(k);
								if (ImGui::IsKeyPressed(key, false)) { keys += std::string(ImGui::GetKeyName(key)) + "(p) "; }
								else if (ImGui::IsKeyDown(key)) { keys += std::string(ImGui::GetKeyName(key)) + "(d) "; }
							}
							if (keys.empty()) { keys = "(none)"; }
							logger::info("input:   keys this frame: {} | navActive={} navActivateId={} navJustMovedTo={} wantCaptureKeyboard={}",
										 keys, ImGui::GetIO().NavActive,
										 GImGui ? GImGui->NavActivateId : 0u,
										 GImGui ? GImGui->NavJustMovedToId : 0u,
										 ImGui::GetIO().WantCaptureKeyboard);
						}
						logger::info("input:   THE FIELD WAS {} on the frame it died - so this is {}",
									 keyboard::WasSubmittedLastFrame(s_lastActive) ? "STILL DRAWN" : "NOT DRAWN",
									 keyboard::WasSubmittedLastFrame(s_lastActive)
										 ? "something taking the focus, not the widget disappearing"
										 : "the widget not being submitted - its page stopped drawing it");
						logger::info("input:   owner window was \"{}\"; nav window now \"{}\"; hovered \"{}\"",
									 s_ownerWindow,
									 (GImGui && GImGui->NavWindow) ? GImGui->NavWindow->Name : "(none)",
									 (GImGui && GImGui->HoveredWindow) ? GImGui->HoveredWindow->Name : "(none)");
					}
					s_lastActive = nowActive;
					if (GImGui && GImGui->ActiveId != 0 && GImGui->ActiveIdWindow)
					{
						std::snprintf(s_ownerWindow, sizeof(s_ownerWindow), "%s", GImGui->ActiveIdWindow->Name);
					}
					s_lastAliveFrame = (GImGui && GImGui->ActiveIdIsAlive == GImGui->ActiveId) ? frame : s_lastAliveFrame;
				}

				// B / CIRCLE LETS GO OF A TEXT BOX (the owner, 2026-09-19: "the text is still
				// highlighted in yellow, which requires me to press Y on controller to exit before I
				// can move out of the box with the D-pad ... we need to make it so that it doesn't
				// default to having the text highlighted after exiting the keyboard or search").
				//
				// This is the other half of a text field owning the D-pad while it is active: with
				// the D-pad no longer able to navigate away, there has to be a deliberate way OUT of
				// the box, and B is the one the whole menu already uses for "back". Done here rather
				// than inside the field so it works for every mod's text box as well as ours.
				// Asked of the input layer, not of ImGui: the B press never reaches ImGui while a text
				// field is active, precisely so ImGui cannot revert the text with it.
				if (interactive && GImGui && GImGui->ActiveId != 0 && keyboard::IsTextField(GImGui->ActiveId) &&
					input::TakeTextFieldCancel())
				{
					logger::debug("input: B released text field {} - navigation is free again", GImGui->ActiveId);
					ImGui::ClearActiveID();
					keyboard::Hide();
				}

				watchdog::Tick();  // liveness signal for the hang watchdog
				ImGui::NewFrame();
				mcmloader::Frame();  // MCM loader: OnConfigClose when an MCM entry stops being drawn
				prisma::Frame();     // 2.1.6: Prisma_OnSettingsApplied when a Prisma page is left

				// The game's own HUD opacity, re-read every frame so the options slider is
				// followed live (theme spec point 3), applied as the ONE global multiplier.
				ImGui::GetStyle().Alpha = theme::GetGameHUDOpacity();

				// The consumer surface draws EVERY frame, whether or not our own menu is up.
				// A HUD element that only appeared while the framework menu was open would not
				// be a HUD element, and a consumer window's visibility is the consumer's to
				// decide through the IsOpen flag it was handed - not ours.
				consumer::DrawHudElements();
				consumer::DrawWindows();

				// Which stick drives ImGui follows "is an item being edited" (the controller scheme in
				// Input.cpp). The framework window samples it while it draws; with only a mod's window
				// up it is sampled here instead, so a value left over from our menu cannot hold the
				// left stick off a mod's window.
				if (!visible && interactive)
				{
					input::SetItemActive(ImGui::IsAnyItemActive());
				}

				if (visible)
				{
					// Escape closes. The keypress was consumed input-side, so the game does
					// not also react to the same stroke.
					if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
					{
						ToggleMainWindow();
					}
					else
					{
						DrawFrameworkWindow();
					}
				}
				// 2.1.5: the theme's frame round the highlighted and hovered items, over every window's own drawing.
				if (IsMainWindowVisible()) { FlushHighlightFrames(); }
				else { g_highlights.clear(); }

				// The on-screen keyboard, after the window so this frame's text fields are known. It
				// closes itself when the window is not up.
				keyboard::Draw();

				// LAST thing in the frame: the curtain covers the framework's own window and every
				// consumer HUD element rather than being interleaved with them.
				curtain::Draw();

				// 2.1.6: the Cursor part, when one is picked, in place of ImGui's arrow. ImGui reads MouseDrawCursor in
				// Render(), so it is switched off here for this frame only; the text-entry and resize pointers stay ImGui's.
				if (ImGui::GetIO().MouseDrawCursor && ImGui::GetMouseCursor() == ImGuiMouseCursor_Arrow && arthooks::DrawCursor(true))
				{
					ImGui::GetIO().MouseDrawCursor = false;
				}

				flick::EndFrame();   // 2.1.6: a FLICK page left (or the menu closed) gets its OnClose

				ImGui::Render();
				ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
				compat::FireMenuEvent(compat::MenuEvent::kAfterRender);   // SMF's event 4 (see the top of this frame)

				// In-process capture: the backbuffer now holds the frame WITH the overlay.
				{
					std::wstring path;
					{ std::scoped_lock l(g_captureLock); path = g_capturePath; }
					if (!path.empty())
					{
						std::string err;
						ID3D11Texture2D* back = nullptr;
						if (!g_swapChain || !g_captureContext) { err = "no swapchain"; }
						else if (FAILED(g_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&back))) || !back) { err = "GetBuffer failed"; }
						else
						{
							const HRESULT hr = DirectX::SaveWICTextureToFile(g_captureContext, back, GUID_ContainerFormatPng, path.c_str());
							if (FAILED(hr)) { err = "SaveWICTextureToFile hr=" + std::to_string(static_cast<long>(hr)); }
							back->Release();
						}
						{
							std::scoped_lock l(g_captureLock);
							g_captureError = err; g_captureDone = true; g_capturePath.clear();
						}
						g_captureCv.notify_all();
					}
				}
			}
		};

		template <class Hook, std::size_t N>
		bool WriteCallGuarded(std::uintptr_t a_address, const char* a_what)
		{
			if (!LooksLikeCallSite(a_address))
			{
				return false;
			}

			auto& trampoline = SKSE::GetTrampoline();
			Hook::func = trampoline.write_call<N>(a_address, Hook::thunk);

			logger::info("{} hooked at {:#x}", a_what, a_address);

			return true;
		}
	}

	bool Install()
	{
		SKSE::AllocTrampoline(static_cast<std::size_t>(14) * 3);  // present + D3D init + PollInputDevices

		// ---- present: settled site, guard anyway ------------------------------------------
		const std::uintptr_t presentSite = offsets::kPresentID.address() + offsets::kPresentOffset.offset();

		if (!WriteCallGuarded<PresentHook, 5>(presentSite, "DXGI present"))
		{
			logger::error("Present site {:#x} does not hold a call instruction on this runtime; "
						  "the framework will not render. This is the 18-repo-corroborated site, so "
						  "an unknown runtime or a conflicting patch is in play.",
						  presentSite);
			return false;
		}

		// ---- D3D init: PROBE the two disputed candidates (Offsets.h) ----------------------
		const std::uintptr_t initBase = offsets::kD3DInitID.address();
		const bool isAE = REL::Module::IsAE();

		for (const auto& candidate : offsets::kD3DInitCandidates)
		{
			const std::uintptr_t site = initBase + (isAE ? candidate.aeOffset : candidate.seOffset);

			if (WriteCallGuarded<D3DInitHook, 5>(site, "D3D init"))
			{
				// The empirical answer to the survey's one unresolved question - log it loudly
				// so PROGRESS.md can record which candidate is real per runtime.
				logger::info("D3D-init offset dispute resolved on this runtime: {} matched (offset {:#x})",
							 candidate.origin, isAE ? candidate.aeOffset : candidate.seOffset);

				return true;
			}

			logger::warn("D3D-init candidate from {} (offset {:#x}) is not a call site on this runtime; trying next",
						 candidate.origin, isAE ? candidate.aeOffset : candidate.seOffset);
		}

		logger::error("No D3D-init candidate matched; the framework stays inert and the game is unaffected.");

		return false;
	}

	void ToggleMainWindow()
	{
		const bool now = !g_windowVisible.load(std::memory_order_relaxed);
		g_nested.store(false, std::memory_order_release);   // the hotkey opens our own window
		g_windowVisible.store(now, std::memory_order_release);

		if (now)
		{
			g_justOpened.store(true, std::memory_order_release);
			g_applyGeometry.store(true, std::memory_order_release);
		}

		logger::info("Framework window {}", now ? "shown" : "hidden");
	}

	void* GetGameWindow()
	{
		return g_gameWindow.load(std::memory_order_acquire);
	}

	bool WantsTextInput()
	{
		return g_wantTextInput.load(std::memory_order_acquire);
	}

	float UiScale()
	{
		return g_uiScale;
	}

	bool IsMainWindowVisible()
	{
		return g_windowVisible.load(std::memory_order_relaxed);
	}

	bool ConsumerWindowOwnsInput()
	{
		return g_consumerInput.load(std::memory_order_acquire);
	}

	// A page declares its own tab bar, and takes back the tab the D-pad asked for (-1 = nothing asked).
	// Called from the page's render function, so it is already on the render thread inside the frame.
	int DeclareInnerTabs(int a_count, int a_current)
	{
		g_innerCount = a_count > 0 ? a_count : 0;
		g_innerIndex = (a_current >= 0 && a_current < g_innerCount) ? a_current : 0;
		g_innerFresh = true;
		const int request = g_innerRequest;
		g_innerRequest = -1;  // handed over once, exactly like the framework's own tab request
		return (request >= 0 && request < g_innerCount) ? request : -1;
	}

	void SetMenuVisible(bool a_visible, bool a_nested)
	{
		g_nested.store(a_visible && a_nested, std::memory_order_release);
		g_windowVisible.store(a_visible, std::memory_order_release);
		if (a_visible)
		{
			g_justOpened.store(true, std::memory_order_release);
			g_applyGeometry.store(true, std::memory_order_release);
			// an MCM menu set up since the last pass shows as the menu opens. Here, on OUR open, not on the render
			// thread's input edge: over a mod's window that already owns the input (RaceMenu Atelier in character
			// creation) the menu opening is no input edge, and the pass never ran (tested 2026-10-04)
			mcmloader::scripts::RequestDiscovery();
		}
		logger::info("Framework window {} ({})", a_visible ? "shown" : "hidden",
			a_nested ? "nested in the game's System menu" : "external/DevBench");
	}

	void SetSelectedNode(const std::string& a_node)
	{
		std::scoped_lock l(g_selLock);
		g_selExternal = true;
		if (a_node.rfind("mod:", 0) == 0)
		{
			g_selNode = "mod";
			try { g_selMod = std::stoi(a_node.substr(4)); } catch (...) {}
			return;
		}
		std::string n = a_node;
		if (n.rfind("system/", 0) == 0) { n = n.substr(7); }  // pre-1.4.4 paths still accepted
		if (n == "settings" || n == "controls" || n == "help" || n == "mod") { g_selNode = n; }
	}

	std::string GetSelectedNode()
	{
		std::scoped_lock l(g_selLock);
		return g_selNode;
	}

	// Runs the action bound to the currently selected node (Save/Quit); categories and mods have
	// no direct action - selecting them IS the interaction. Safe from any thread (RunConsoleCommand
	// marshals to the main thread).
	void ActivateSelectedNode()
	{
		// SMF shape: no node carries an action - selecting a mod or a framework page IS the interaction.
	}

	// JSON snapshot of the menu for DevBench: visibility, the selected node, and every registered
	// mod + its pages. Read-only; safe from the listener thread (registry::Snapshot is thread-safe).
	std::string CaptureBlocking(const std::wstring& a_path, unsigned a_timeoutMs)
	{
		if (!g_d3dReady.load(std::memory_order_acquire)) { return "renderer not ready"; }
		std::unique_lock l(g_captureLock);
		if (!g_capturePath.empty()) { return "a capture is already pending"; }
		g_capturePath = a_path; g_captureDone = false; g_captureError.clear();
		const bool ok = g_captureCv.wait_for(l, std::chrono::milliseconds(a_timeoutMs), [] { return g_captureDone; });
		if (!ok) { g_capturePath.clear(); return "timed out waiting for a frame (is the game presenting?)"; }
		return g_captureError;
	}

	bool SetTheme(const std::string& a_themeId)
	{
		for (const theme::Palette& palette : theme::ListThemes())
		{
			if (palette.id == a_themeId)
			{
				theme::SetActiveTheme(a_themeId);
				theme::Apply();
				skin::Reload();
				settings::Get().themeId = a_themeId;
				settings::Save();
				logger::info("theme switched to \"{}\" (DevBench)", a_themeId);
				return true;
			}
		}
		logger::warn("theme \"{}\" is not registered", a_themeId);
		return false;
	}

	// 2.1.6 - amf.menu op=art: the Appearance > Art page's dropdowns for a driving tool. No kind = list every part per kind;
	// a kind sets the active theme's pick ("" = the theme's own, "none", or a part name) exactly as the page does.
	std::string ArtOp(const std::string& a_kind, const std::string& a_name, bool a_set)
	{
		auto list = [](skin::ArtKind a_k) {
			std::string out;
			for (const auto& n : skin::ListArt(a_k)) { out += (out.empty() ? "\"" : ",\"") + n + "\""; }
			return "[" + out + "]";
		};
		const theme::Palette& active = theme::GetActiveTheme();
		if (a_set)
		{
			std::size_t k = 0;
			auto lower = [](std::string a_s) {
				for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
				return a_s;
			};
			while (k < skin::kArtKindCount && lower(a_kind) != lower(skin::kArtKeys[k] + 1)) { ++k; }   // Frame, Box, TickBox, ...
			if (k == skin::kArtKindCount)
			{
				std::string names;
				for (std::size_t i = 0; i < skin::kArtKindCount; ++i) { names += std::string(i ? ", " : "") + (skin::kArtKeys[i] + 1); }
				return "{\"ok\":false,\"op\":\"art\",\"error\":\"kind must be one of " + names + "\"}";
			}
			const auto parts = skin::ListArt(static_cast<skin::ArtKind>(k));
			if (!a_name.empty() && a_name != skin::kArtNone && std::find(parts.begin(), parts.end(), a_name) == parts.end())
			{
				return "{\"ok\":false,\"op\":\"art\",\"error\":\"no part '" + a_name + "' in assets/" + skin::kArtFolders[k] + "\"}";
			}
			auto& values = settings::Get();
			values.themeArt[active.id][k] = a_name;
			settings::Save();
			g_artReloadPending.store(true, std::memory_order_release);   // the render thread loads it at its next frame
			logger::info("art {} for theme {} -> \"{}\" (DevBench)", skin::kArtKeys[k] + 1, active.id, a_name.empty() ? "theme's own" : a_name);
			return std::string("{\"ok\":true,\"op\":\"art\",\"theme\":\"") + active.id + "\",\"kind\":\"" + (skin::kArtKeys[k] + 1) +
				   "\",\"name\":\"" + a_name + "\",\"note\":\"loads at the next frame - ask op=art again to read it back\"}";
		}
		std::string parts;
		for (std::size_t k = 0; k < skin::kArtKindCount; ++k)
		{
			parts += std::string(k ? "," : "") + "\"" + (skin::kArtKeys[k] + 1) + "\":" + list(static_cast<skin::ArtKind>(k));
		}
		return std::string("{\"ok\":true,\"op\":\"art\",\"theme\":\"") + active.id + "\",\"parts\":{" + parts + "},\"skin\":" +
			   skin::StatusJson() + "}";
	}

	bool SetModAlias(const std::string& a_modName, const std::string& a_alias)
	{
		if (personalization::IsSeparator(a_modName))
		{
			if (a_alias.empty()) { return false; }   // a separator keeps a name
			personalization::SetAlias(a_modName, a_alias);
			settings::Save();
			return true;
		}
		const auto entries = registry::Snapshot();
		for (const registry::Entry& entry : entries)
		{
			if (entry.modName == a_modName)
			{
				personalization::SetAlias(a_modName, a_alias);
				settings::Save();
				return true;
			}
		}
		return false;
	}

	bool MoveModTo(const std::string& a_modName, int a_position)
	{
		const auto entries = registry::Snapshot();
		if (personalization::IsSeparator(a_modName))
		{
			personalization::MoveTo(entries, a_modName, a_position);
			settings::Save();
			return true;
		}
		for (const registry::Entry& entry : entries)
		{
			if (entry.modName == a_modName)
			{
				personalization::MoveTo(entries, a_modName, a_position);
				settings::Save();
				return true;
			}
		}
		return false;
	}

	void ResetModOrder()
	{
		personalization::ResetToAlphabetical();
		settings::Save();
	}

	std::string SeparatorOp(const std::string& a_action, const std::string& a_name, const std::string& a_mod, const std::string& a_separator)
	{
		const auto entries = registry::Snapshot();
		bool ok = false;
		std::string id;
		if (a_action == "add")
		{
			id = personalization::AddSeparator(entries, a_mod, a_name.empty() ? std::string("New separator") : a_name);
			ok = true;
		}
		else if (a_action == "remove") { ok = personalization::RemoveSeparator(a_separator); }
		else if (a_action == "send") { ok = personalization::SendTo(entries, a_mod, a_separator); }
		else if (a_action == "collapse")
		{
			ok = personalization::IsSeparator(a_separator);
			if (ok) { personalization::ToggleCollapsed(a_separator); }
		}
		else if (a_action == "favourite")
		{
			ok = personalization::IsSeparator(a_separator);
			if (ok) { personalization::ToggleFavourite(a_separator); }
		}
		if (ok) { settings::Save(); }
		return std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"op\":\"separator\",\"action\":\"" + a_action + "\"" +
			   (id.empty() ? std::string() : ",\"id\":\"" + id + "\"") + "}";
	}

	bool QueueNav(const std::string& a_direction)
	{
		if (a_direction == "left")  { g_navRequest.store(1, std::memory_order_release); return true; }
		if (a_direction == "right") { g_navRequest.store(2, std::memory_order_release); return true; }
		logger::warn("amf.menu nav: unknown direction \"{}\" (expected left or right)", a_direction);
		return false;
	}

	bool FocusPane(const std::string& a_pane)
	{
		if (a_pane == "list")    { g_focusPane = 1; g_navToSelected = true; return true; }
		if (a_pane == "options") { g_focusPane = 2; return true; }
		logger::warn("amf.menu focus: unknown pane \"{}\" (expected list or options)", a_pane);
		return false;
	}

	// 1.8.9: the active theme's frame, for a consumer's own box (Item Explorer's 3D preview first): the
	// Skyrim theme's knotwork, a UI author's frame art when configured, nothing under a theme without a
	// frame. Drawn just outside the rect like the window's own. Returns whether anything was drawn, so
	// the consumer can fall back to a plain line.
	bool DrawThemeFrameAround(ImDrawList* a_drawList, float a_x0, float a_y0, float a_x1, float a_y1)
	{
		if (!a_drawList || a_x1 <= a_x0 || a_y1 <= a_y0) { return false; }
		const bool knot = skin::DrawsFrame();   // 2.1.6: also off when the player picked no frame
		if (!knot) { return false; }
		DrawKnotworkAround(a_drawList, ImVec2(a_x0, a_y0), ImVec2(a_x1, a_y1));
		return true;
	}

	std::string GetMenuStateJson()
	{
		std::string node, tab, tabName; int selMod, tabIndex, tabCount;
		{
			std::scoped_lock l(g_selLock);
			node = g_selNode; tab = g_selTab; selMod = g_selMod;
			tabName = g_selTabName; tabIndex = g_selTabIndex; tabCount = g_selTabCount;
		}
		const bool visible = g_windowVisible.load(std::memory_order_relaxed);
		const auto entries = registry::Snapshot();
		std::string searchText; { std::scoped_lock l(g_searchTextLock); searchText = g_searchText; }
		const std::string searchJson = "\"search\":{\"text\":\"" + [&]{ std::string o; for (char c : searchText) { if (c == '"' || c == '\\') { o += '\\'; } o += c; } return o; }() +
			"\",\"len\":" + std::to_string(g_searchLen.load()) + ",\"active\":" + (g_searchActive.load() ? "true" : "false") +
			",\"rect\":[" + std::to_string(g_searchRect[0].load()) + "," + std::to_string(g_searchRect[1].load()) + "," + std::to_string(g_searchRect[2].load()) + "," + std::to_string(g_searchRect[3].load()) + "]}," +
			"\"wantTextInput\":" + (g_wantTextInput.load() ? "true" : "false") + ",\"backspaceDown\":" + (g_backspaceDown.load() ? "true" : "false") +
			",\"activeId\":" + std::to_string(g_activeIdMirror.load()) + ",\"keyCtrl\":" + (g_modCtrl.load() ? "true" : "false") + ",\"keyShift\":" + (g_modShift.load() ? "true" : "false") + ",\"keyAlt\":" + (g_modAlt.load() ? "true" : "false") + ",";
		auto esc = [](const std::string& v) { std::string o; for (char c : v) { if (c == '"' || c == '\x5C') { o += '\x5C'; } o += c; } return o; };
		std::string mods;
		for (std::size_t i = 0; i < entries.size(); ++i)
		{
			if (i) { mods += ","; }
			std::string pages;
			for (std::size_t j = 0; j < entries[i].pages.size(); ++j)
			{
				if (j) { pages += ","; }
				pages += "\"" + esc(entries[i].pages[j].pageName) + "\"";
			}
				std::string hiddenPages;
				for (const registry::Page& page : entries[i].pages)
				{
					if (page.hidden) { hiddenPages += (hiddenPages.empty() ? "\"" : ",\"") + esc(page.pageName) + "\""; }
				}
				mods += "{\"index\":" + std::to_string(i) + ",\"name\":\"" + esc(entries[i].modName) + "\",\"pages\":[" + pages + "],\"hiddenPages\":[" + hiddenPages + "]}";
		}
		// Menu-shell personalization: the list AS THE PLAYER SEES IT (position, identity, shown
		// name), so a driving tool can assert the order and the aliases without reading pixels.
		std::string order;
		{
			const auto rows = personalization::Order(entries);
			for (std::size_t i = 0; i < rows.size(); ++i)
			{
				if (i) { order += ","; }
				order += "{\"pos\":" + std::to_string(i + 1) + ",\"index\":" + std::to_string(rows[i].registryIndex) +
						 ",\"mod\":\"" + esc(rows[i].modName) + "\",\"shows\":\"" + esc(rows[i].displayName) + "\"" +
						 (rows[i].separator ? std::string(",\"separator\":true,\"collapsed\":") + (rows[i].collapsed ? "true" : "false") +
											  ",\"children\":" + std::to_string(rows[i].children)
											: std::string(",\"depth\":") + std::to_string(rows[i].depth) + (rows[i].hidden ? ",\"hidden\":true" : "")) +
						 "}";
			}
		}
		// Consumer-window diagnostic (2026-09-12). Mods gate their own hotkeys on
		// IsAnyBlockingWindowOpened(), which is `visible || any consumer window open AND blocking`.
		// Two users reported hotkeys dead under AMF but working on SKSE Menu Framework, and the
		// aggregate on its own would not say WHICH window was latched - so each is listed.
		// blockingWindowOpen is computed from the same copied snapshot, never by calling
		// consumer::AnyBlockingWindowOpen() here, because that takes the lock WindowStates() holds.
		std::string windows;
		bool anyBlocking = false;
		{
			const auto states = consumer::WindowStates();
			for (std::size_t i = 0; i < states.size(); ++i)
			{
				if (states[i].open && states[i].blocking) { anyBlocking = true; }
				if (i) { windows += ","; }
				// 2.0.4 probe: the top-level ImGui windows this entry drew on the last frame, with the
				// flags the input gate decides on (acceptsMouse = one of them lacks NoMouseInputs).
				std::string submitted;
				for (const consumer::SubmittedWindow& sw : states[i].submitted)
				{
					char flags[16];
					std::snprintf(flags, sizeof(flags), "0x%08X", static_cast<unsigned int>(sw.flags));
					if (!submitted.empty()) { submitted += ","; }
					submitted += "{\"name\":\"" + esc(sw.name) + "\",\"flags\":\"" + flags + "\"" +
								 ",\"noMouseInputs\":" + (sw.noMouseInputs ? "true" : "false") +
								 ",\"noInputs\":" + (sw.noInputs ? "true" : "false") +
								 ",\"pos\":[" + std::to_string(static_cast<int>(sw.x)) + "," + std::to_string(static_cast<int>(sw.y)) + "]" +
								 ",\"size\":[" + std::to_string(static_cast<int>(sw.w)) + "," + std::to_string(static_cast<int>(sw.h)) + "]}";
				}
				windows += "{\"open\":" + std::string(states[i].open ? "true" : "false") +
						   ",\"blocking\":" + (states[i].blocking ? "true" : "false") +
						   ",\"acceptsMouse\":" + (states[i].acceptsMouse ? "true" : "false") +
						   ",\"view\":\"" + esc(states[i].view) + "\"" +
						   ",\"submitted\":[" + submitted + "]}";
			}
		}

		float cursorX = 0.0f, cursorY = 0.0f;
		input::GetCursor(cursorX, cursorY);
		std::string mainWindow;
		{
			std::scoped_lock l(g_selLock);
			mainWindow = ",\"mainWindow\":{\"pos\":[" + std::to_string(static_cast<int>(g_mainX)) + "," + std::to_string(static_cast<int>(g_mainY)) +
				"],\"size\":[" + std::to_string(static_cast<int>(g_mainW)) + "," + std::to_string(static_cast<int>(g_mainH)) + "]}";
		}
		return std::string("{" + searchJson + "\"cursor\":{\"x\":") + std::to_string(static_cast<int>(cursorX)) + ",\"y\":" + std::to_string(static_cast<int>(cursorY)) + "}" +
			   ",\"visible\":" + (visible ? "true" : "false") + mainWindow +
			   ",\"blockingWindowOpen\":" + ((visible || anyBlocking) ? "true" : "false") +
			   ",\"consumerWindows\":[" + windows + "]" +
			   // 2.0.4: what the input hook acts on - a mod's window holds the input (cursor, ImGui,
			   // game input held) as the renderer last published it, which can trail blockingWindowOpen
			   // by one frame.
			   ",\"consumerInput\":" + (g_consumerInput.load(std::memory_order_acquire) ? "true" : "false") +
			   ",\"tab\":\"" + esc(tab) + "\",\"selected\":\"" + esc(node) + "\",\"selectedMod\":" + std::to_string(selMod) +
			   ",\"page\":\"" + esc(tabName) + "\",\"pageIndex\":" + std::to_string(tabIndex) +
			   ",\"pageCount\":" + std::to_string(tabCount) +
			   // 2.0.5 (HadToRegister's F1 report): the menu key, the key the input hook really opens on, and
			   // which file it came from - user (User.ini's uToggleKey), user-controls (its [Bindings]
			   // sToggleMenu), shipped, default, or fallback (the value given was not a usable key).
			   ",\"menuKey\":{\"uToggleKey\":" + std::to_string(settings::Get().toggleKey) +
			   ",\"bound\":" + std::to_string(bindings::ToggleKeyboardCode()) +
			   ",\"name\":\"" + esc(settings::Get().toggleKey > 0 ? bindings::KeyName(static_cast<std::uint32_t>(settings::Get().toggleKey)) : std::string("none")) + "\"" +
			   ",\"source\":\"" + settings::ToggleKeySourceName(settings::GetToggleKeySource()) + "\"}" +
			   ",\"controllerMode\":" + (input::UsingController() ? "true" : "false") +
			   ",\"lastDevice\":\"" + (input::LastDevice() == input::Device::kGamepad ? "gamepad" :
										   input::LastDevice() == input::Device::kKeyboardMouse ? "keyboard" : "none") + "\"" +
			   ",\"customOrder\":" + (personalization::IsCustomOrder() ? "true" : "false") +
			   // 1.7.9: ImGui's navigation cursor, so a "the cursor jumps to the top of the list" report
			   // (housem3, 2026-09-13) can be measured by a driving script rather than described.
			   ",\"navId\":" + std::to_string(GImGui ? GImGui->NavId : 0u) +
			   ",\"navWindow\":\"" + esc(GImGui && GImGui->NavWindow && GImGui->NavWindow->Name ? GImGui->NavWindow->Name : "") + "\"" +
			   ",\"displayOrder\":[" + order + "]" +
			   ",\"mods\":[" + mods + "]" +
			   ",\"keyboard\":" + keyboard::StateJson() + "}";
	}
}
