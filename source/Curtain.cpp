#include "PCH.h"

#include "Curtain.h"

#include "Settings.h"
#include "utils/Logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <filesystem>
#include <system_error>
#include <vector>

#include <imgui.h>

#include <d3d11.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace curtain
{
	namespace
	{
		using clock = std::chrono::steady_clock;

		// How long the fade out takes once the main menu is up. Short: this is a reveal, not an effect.
		constexpr float kFadeSeconds = 0.40f;

		// A fade is a lie told over many frames, and at the end of startup there are not many frames. The main menu
		// registers as open while its movie is still loading, so the frames just after that are enormous - one of them
		// can be most of a second by itself. The fade is timed by the wall clock, so a frame that long leaves the
		// curtain at whatever alpha it had reached and holds it there until the next frame arrives: a black screen
		// that jumps to half-transparent, hangs, then vanishes (the owner, 2026-09-16: "a slight stutter between when
		// it's supposed to close and when it shows the menu where it's partially transparent and it holds that for
		// about a second").
		//
		// So the fade does not start when the menu opens - it starts when the game is presenting frames fast enough
		// to draw one. Until then the curtain stays FULLY opaque, which is what it is for: solid black is not an
		// artifact, half-black frozen for a second is.
		constexpr float kSteadyFrameSeconds = 0.040f;  // ~25 fps; slower than this and the fade is a slideshow
		constexpr int   kSteadyFrames = 6;             // consecutive quick frames before the reveal begins
		// ... but never wait forever for them. If the frames never settle, fade anyway rather than hold black over a
		// game that is already running.
		constexpr float kSettleSeconds = 3.0f;

		// The escape hatch, in seconds, read from the INI so a heavy load order can be given more
		// room without a rebuild. 30s was the first value and it was too short: on the owner's list
		// (2026-09-15) the main menu had still not appeared, so the curtain timed out and uncovered
		// the tail of startup - exactly what it exists to hide.
		float HardTimeoutSeconds()
		{
			return static_cast<float>(settings::Get().curtainTimeoutSeconds);
		}

		// The player being in a loaded world is proof we are past startup, whatever the menu did.
		// parentCell stays null until a save or a new game actually loads, so this cannot fire
		// early. It is what stops a longer timeout from ever stranding anyone: if the curtain is
		// somehow still up when play begins, it goes at once.
		bool PlayerIsInWorld()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			return player && player->parentCell;
		}

		std::atomic<bool> g_lifted{ false };
		bool              g_started = false;
		bool              g_sawMainMenu = false;
		bool              g_fading = false;
		int               g_steadyFrames = 0;
		clock::time_point g_firstFrame{};
		clock::time_point g_menuSeen{};
		clock::time_point g_lastFrame{};
		clock::time_point g_fadeStart{};

		bool MainMenuIsUp()
		{
			// Null the moment the process starts - this runs long before the UI singleton exists.
			auto* ui = RE::UI::GetSingleton();
			return ui && ui->IsMenuOpen(RE::MainMenu::MENU_NAME);
		}

		// ---- the optional picture -------------------------------------------------------------------------
		//
		// [Startup] sCurtainImage names a file relative to Data. Decoding goes through WIC, which Windows
		// already provides: it reads PNG, JPEG and BMP, and it keeps this framework's dependency list as it is -
		// a decoder pulled in for one ornament would ship with every copy of the mod.
		//
		// Everything here fails soft. A missing file, an unreadable one, a device that will not take the
		// texture: the curtain is simply black, the reason is logged once, and the game starts. A picture is
		// decoration; the curtain's job is to cover the screen.
		ID3D11ShaderResourceView* g_imageSRV = nullptr;
		int  g_imageW = 0;
		int  g_imageH = 0;
		bool g_imageTried = false;

		void LoadImageOnce(ID3D11Device* a_device)
		{
			if (g_imageTried) { return; }
			g_imageTried = true;

			const std::string& rel = settings::Get().curtainImage;
			if (rel.empty() || !a_device) { return; }

			std::error_code ec;
			const std::filesystem::path path = std::filesystem::current_path(ec) / "Data" / rel;
			if (ec || !std::filesystem::exists(path))
			{
				logger::warn("startup curtain: sCurtainImage \"{}\" not found at {}; the curtain stays black", rel, path.string());
				return;
			}

			using Microsoft::WRL::ComPtr;
			ComPtr<IWICImagingFactory> factory;
			if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
			{
				logger::warn("startup curtain: no WIC factory; the curtain stays black");
				return;
			}
			ComPtr<IWICBitmapDecoder> decoder;
			if (FAILED(factory->CreateDecoderFromFilename(path.wstring().c_str(), nullptr, GENERIC_READ,
														  WICDecodeMetadataCacheOnDemand, &decoder)))
			{
				logger::warn("startup curtain: {} could not be decoded (is it a PNG, JPEG or BMP?); the curtain stays black", path.string());
				return;
			}
			ComPtr<IWICBitmapFrameDecode> frame;
			ComPtr<IWICFormatConverter> converter;
			if (FAILED(decoder->GetFrame(0, &frame)) || FAILED(factory->CreateFormatConverter(&converter)) ||
				FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone,
											 nullptr, 0.0, WICBitmapPaletteTypeCustom)))
			{
				logger::warn("startup curtain: {} could not be converted to RGBA; the curtain stays black", path.string());
				return;
			}
			UINT w = 0, h = 0;
			if (FAILED(converter->GetSize(&w, &h)) || w == 0 || h == 0)
			{
				logger::warn("startup curtain: {} has no size; the curtain stays black", path.string());
				return;
			}
			std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4u);
			if (FAILED(converter->CopyPixels(nullptr, w * 4u, static_cast<UINT>(pixels.size()), pixels.data())))
			{
				logger::warn("startup curtain: {} could not be read into memory; the curtain stays black", path.string());
				return;
			}

			D3D11_TEXTURE2D_DESC td{};
			td.Width = w;
			td.Height = h;
			td.MipLevels = 1;
			td.ArraySize = 1;
			td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			td.SampleDesc.Count = 1;
			td.Usage = D3D11_USAGE_DEFAULT;
			td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			D3D11_SUBRESOURCE_DATA sd{};
			sd.pSysMem = pixels.data();
			sd.SysMemPitch = w * 4u;

			ID3D11Texture2D* tex = nullptr;
			if (SUCCEEDED(a_device->CreateTexture2D(&td, &sd, &tex)) && tex)
			{
				if (FAILED(a_device->CreateShaderResourceView(tex, nullptr, &g_imageSRV)))
				{
					g_imageSRV = nullptr;
					logger::warn("startup curtain: the picture could not be given a shader view; the curtain stays black");
				}
				else
				{
					g_imageW = static_cast<int>(w);
					g_imageH = static_cast<int>(h);
					logger::info("startup curtain: showing {} ({}x{})", path.string(), g_imageW, g_imageH);
				}
				tex->Release();
			}
			else
			{
				logger::warn("startup curtain: the picture could not be uploaded to the device; the curtain stays black");
			}
		}
	}

	bool IsCovering()
	{
		return !g_lifted.load(std::memory_order_acquire);
	}

	void Lift(const char* a_reason)
	{
		if (g_lifted.exchange(true, std::memory_order_acq_rel))
		{
			return;  // already down; never log twice
		}
		logger::info("startup curtain: lifted ({})", a_reason ? a_reason : "no reason given");
	}

	void Draw()
	{
		if (g_lifted.load(std::memory_order_acquire))
		{
			return;
		}

		// Turned off in the settings page or the INI: never cover anything, and do not keep
		// checking for the rest of the session.
		if (!settings::Get().startupCurtain)
		{
			Lift("the setting is off");
			return;
		}

		const clock::time_point now = clock::now();
		if (!g_started)
		{
			g_started = true;
			g_firstFrame = now;
			logger::info("startup curtain: covering the screen until the main menu is ready "
						 "(lifts by itself after {:.0f}s, or the moment play begins)", HardTimeoutSeconds());
		}

		if (!g_sawMainMenu && PlayerIsInWorld())
		{
			Lift("the player is already in the world");
			return;
		}

		// Frame-to-frame time, measured here because this runs once per presented frame. It is the only thing that
		// says whether a fade drawn now would be seen as a fade or as two stuck alphas.
		const float sinceLastFrame = (g_lastFrame == clock::time_point{}) ? 0.0f : std::chrono::duration<float>(now - g_lastFrame).count();
		g_lastFrame = now;

		if (!g_sawMainMenu && MainMenuIsUp())
		{
			g_sawMainMenu = true;
			g_menuSeen = now;
			g_steadyFrames = 0;
			logger::info("startup curtain: main menu is up; holding black until the frames settle, then fading out over {:.2f}s", kFadeSeconds);
		}

		// The menu is up but the reveal has not begun: count quick frames, and start the fade once there have been
		// enough of them in a row to draw one - or once waiting for them has itself gone on too long.
		if (g_sawMainMenu && !g_fading)
		{
			if (sinceLastFrame > 0.0f && sinceLastFrame <= kSteadyFrameSeconds) { ++g_steadyFrames; }
			else { g_steadyFrames = 0; }

			const float waiting = std::chrono::duration<float>(now - g_menuSeen).count();
			if (g_steadyFrames >= kSteadyFrames || waiting >= kSettleSeconds)
			{
				g_fading = true;
				g_fadeStart = now;
				logger::info("startup curtain: fading out over {:.2f}s ({})", kFadeSeconds,
							 g_steadyFrames >= kSteadyFrames ? std::format("{} steady frames, last {:.0f}ms", g_steadyFrames, sinceLastFrame * 1000.0f) :
															   std::format("frames never settled within {:.1f}s, revealing anyway", kSettleSeconds));
			}
		}

		float alpha = 1.0f;
		if (g_fading)
		{
			const float elapsed = std::chrono::duration<float>(now - g_fadeStart).count();
			if (elapsed >= kFadeSeconds)
			{
				Lift("the main menu is up and the fade finished");
				return;
			}
			// A frame that arrives after a long gap would jump the alpha and then hold it - the artifact the settle
			// check exists to avoid, reappearing mid-fade if the game stalls again. A cut is better than a freeze.
			if (sinceLastFrame > kSteadyFrameSeconds * 4.0f)
			{
				Lift(std::format("a {:.0f}ms frame landed mid-fade; cut rather than hold a half-faded screen", sinceLastFrame * 1000.0f).c_str());
				return;
			}
			alpha = 1.0f - (elapsed / kFadeSeconds);
		}
		else if (std::chrono::duration<float>(now - g_firstFrame).count() >= HardTimeoutSeconds())
		{
			// Something is wrong - a main menu replacer we cannot see, or a start that never gets
			// there. Either way the player gets their screen back rather than a black rectangle.
			Lift("the main menu was never seen within the timeout");
			return;
		}

		const ImGuiIO& io = ImGui::GetIO();

		// Do not draw until the font atlas has a texture.
		//
		// AddRectFilled is not a special case in ImGui - it is geometry sampling the atlas's white pixel. With no
		// texture bound the fill comes out untextured, which on screen is WHITE. At startup that is exactly the state
		// for the first frames: the atlas is built on the first frame that needs it, and this mod also queues a
		// deliberate rebuild for its own fonts, which invalidates the device objects again. So the curtain - the thing
		// whose entire job is to show black - was flashing white instead (the owner, 2026-09-16: "a white shuttering
		// effect at the very beginning of its startup").
		//
		// Skipping the frame is the right response rather than drawing something else: one uncovered frame of the
		// game's own startup is what the curtain would have hidden anyway, and it is over in a frame or two. The state
		// machine above keeps running, and the fade cannot begin while frames are this slow.
		if (!io.Fonts || !io.Fonts->IsBuilt() || io.Fonts->TexID == 0)
		{
			return;
		}

		if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
		{
			return;  // no viewport yet; nothing sensible to cover
		}

		// Decoded once, on the first frame that actually draws - by then the device is up and the atlas is
		// built, so nothing here races the renderer's own initialisation.
		{
			ID3D11Device* device = nullptr;
			if (ImGui::GetIO().BackendRendererUserData)
			{
				if (auto* view = reinterpret_cast<ID3D11ShaderResourceView*>(ImGui::GetIO().Fonts->TexID))
				{
					view->GetDevice(&device);
				}
			}
			LoadImageOnce(device);
			if (device) { device->Release(); }
		}

		// The FOREGROUND draw list, so the curtain is over the framework's own window and over
		// every consumer HUD element, not interleaved with them.
		const auto a = static_cast<ImU32>(std::lround(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
		ImDrawList* draw = ImGui::GetForegroundDrawList();
		draw->AddRectFilled(
			ImVec2(0.0f, 0.0f),
			ImVec2(io.DisplaySize.x, io.DisplaySize.y),
			IM_COL32(0, 0, 0, a));

		// The picture, if one is set: fitted inside the screen with its shape kept, centred, on top of the
		// black that is already there. Fitted rather than cropped - a splash is usually lettering and a
		// composition, and filling the screen would cut it. It fades with the curtain, on the same alpha.
		if (g_imageSRV && g_imageW > 0 && g_imageH > 0 && io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f)
		{
			const float scale = (std::min)(io.DisplaySize.x / static_cast<float>(g_imageW),
										   io.DisplaySize.y / static_cast<float>(g_imageH));
			const float w = static_cast<float>(g_imageW) * scale;
			const float h = static_cast<float>(g_imageH) * scale;
			const ImVec2 tl((io.DisplaySize.x - w) * 0.5f, (io.DisplaySize.y - h) * 0.5f);
			draw->AddImage(reinterpret_cast<ImTextureID>(g_imageSRV), tl, ImVec2(tl.x + w, tl.y + h),
						   ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32(255, 255, 255, a));
		}
	}
}
