#include "Renderer.h"

#include "Input.h"
#include "Offsets.h"
#include "Persistence.h"
#include "KnotworkBorder.h"
#include "Personalization.h"
#include "Registry.h"
#include "Settings.h"
#include "Theme.h"
#include "Watchdog.h"
#include "utils/ToggleSwitch.h"
#include "utils/Logger.h"

#include <array>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <mutex>
#include <unordered_map>

#include <imgui.h>
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

namespace renderer
{
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
		std::atomic<bool> g_justOpened{ false };  // set on the input thread, consumed on the render thread

		// Knotwork frame texture (the embedded MO2-Skyrim border-image.png). Created once at
		// D3DInit on the game's own device; used by DrawKnotworkFrame as an ImGui texture id.
		ID3D11ShaderResourceView* g_knotSRV = nullptr;

		// Menu navigation state, promoted from static locals so the DevBench tool can drive and read
		// it from the listener thread (see DevBenchTool.cpp). Guarded by g_selLock; the render loop
		// copies in at frame start and out at frame end.
		std::mutex g_selLock;
		std::string g_selTab  = "mods";            // kept for the DevBench state JSON; the SMF shape has one list
		std::string g_selNode = "settings";        // side-list entry: settings|controls|help|mod
		int g_selMod = 0;
		// Set when the selection is changed from OUTSIDE the UI (the amf.menu DevBench tool).
		// Without this the render loop copied its own state back every frame and ImGui's tab bar,
		// which owns its selected tab internally, stomped the external change immediately - the
		// automated pane sweep on 2026-08-28 showed every select() snapping back to "quests".
		bool g_selExternal = false;

		// Draws the 78x78 knotwork PNG as a 9-slice frame around the given screen rect: the four
		// ornate corners at fixed size, the four edges stretched between them, the centre left
		// transparent so the window shows through. Faithful reproduction, so the art is drawn at
		// its own colour (white tint = no recolour). No-op if the texture failed to create.
		void DrawKnotworkFrame(ImDrawList* dl, const ImVec2& p0, const ImVec2& p1)
		{
			if (!g_knotSRV || !dl)
			{
				return;
			}

			const float W = static_cast<float>(knotwork::kWidth);
			const float H = static_cast<float>(knotwork::kHeight);
			const float cs = static_cast<float>(knotwork::kCorner);

			// UV split points (source), and screen split points (dest, corners at fixed cs px).
			const float u0 = 0.0f, u1 = cs / W, u2 = (W - cs) / W, u3 = 1.0f;
			const float v0 = 0.0f, v1 = cs / H, v2 = (H - cs) / H, v3 = 1.0f;
			const float x0 = p0.x, x1 = p0.x + cs, x2 = p1.x - cs, x3 = p1.x;
			const float y0 = p0.y, y1 = p0.y + cs, y2 = p1.y - cs, y3 = p1.y;

			// Degenerate guard: a window smaller than two corners would flip the middle slices.
			if (x2 <= x1 || y2 <= y1)
			{
				return;
			}

			const auto tex = reinterpret_cast<ImTextureID>(g_knotSRV);
			const ImU32 white = IM_COL32_WHITE;
			auto slice = [&](float ax, float ay, float bx, float by, float au, float av, float bu, float bv) {
				dl->AddImage(tex, ImVec2(ax, ay), ImVec2(bx, by), ImVec2(au, av), ImVec2(bu, bv), white);
			};

			// corners
			slice(x0, y0, x1, y1, u0, v0, u1, v1);  // top-left
			slice(x2, y0, x3, y1, u2, v0, u3, v1);  // top-right
			slice(x0, y2, x1, y3, u0, v2, u1, v3);  // bottom-left
			slice(x2, y2, x3, y3, u2, v2, u3, v3);  // bottom-right
			// edges (stretched along their run)
			slice(x1, y0, x2, y1, u1, v0, u2, v1);  // top
			slice(x1, y2, x2, y3, u1, v2, u2, v3);  // bottom
			slice(x0, y1, x1, y2, u0, v1, u1, v2);  // left
			slice(x2, y1, x3, y2, u2, v1, u3, v2);  // right
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

		// Rebuilds the font atlas at the current scale. Call OUTSIDE a frame (before NewFrame).
		void BuildFonts()
		{
			ImGuiIO& io = ImGui::GetIO();
			const float px = kBaseFontPx * g_uiScale * settings::Get().textScale;

			io.Fonts->Clear();
			ImFont* loaded = nullptr;

			const std::string& custom = settings::Get().fontPath;
			if (!custom.empty())
			{
				loaded = io.Fonts->AddFontFromFileTTF(custom.c_str(), px);
				if (!loaded) { logger::warn("font: sFontPath \"{}\" could not be loaded; falling back", custom); }
			}
			for (const char* cand : kFontCandidates)
			{
				if (loaded) { break; }
				loaded = io.Fonts->AddFontFromFileTTF(cand, px);
				if (loaded) { logger::info("font: rasterised \"{}\" at {:.1f}px", cand, px); }
			}
			if (!loaded)
			{
				// Never fail to render: fall back to the built-in face, magnified as before.
				io.Fonts->AddFontDefault();
				io.FontGlobalScale = g_uiScale * settings::Get().textScale;
				logger::warn("font: no TrueType face could be loaded; using the built-in bitmap font");
				return;
			}

			io.FontGlobalScale = 1.0f;  // native size - no magnification, so no pixelation
			io.Fonts->Build();
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
				const HWND hwnd = reinterpret_cast<HWND>(window.hWnd);

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

				ImGui_ImplWin32_Init(hwnd);
				ImGui_ImplDX11_Init(device, context);
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

				g_uiScale = window.windowHeight > 0 ? static_cast<float>(window.windowHeight) / 1080.0f : 1.0f;
				if (g_uiScale < 1.0f)
				{
					g_uiScale = 1.0f;  // never shrink below the 1080p baseline
				}

				// Text is rasterised at native size for this display (see BuildFonts) rather than
				// magnifying the built-in bitmap font, which is what made it look pixelated.
				ScanFonts();
				BuildFonts();
				watchdog::Init();
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
		void DrawMenuListSection();  // defined below, next to the other leaf panes

		void DrawFrameworkSettingsPane()
		{
			auto& values = settings::Get();

			ImGui::TextUnformatted("Framework Settings");
			ImGui::Separator();
			ImGui::Spacing();

			// Theme picker (design decision, 2026-08-27) - supersedes the original "no theme UI by design"
			// stance; the registry is additive (theme::Theme.h), never overwriting an entry.
			{
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
				if (ImGui::Combo("Theme", &currentIndex, names.data(), static_cast<int>(names.size())))
				{
					theme::SetActiveTheme(themes[currentIndex].id);
					theme::Apply();
					values.themeId = themes[currentIndex].id;
					settings::Save();
				}
				ImGui::TextWrapped("\"Untarnished\" is the framework's original identity; \"MO2 Skyrim\" "
								   "is ported from Mod Organizer 2's own real stylesheet.");
			}
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
				if (!labels.empty() && ImGui::Combo("Font", &current, labels.data(), static_cast<int>(labels.size())))
				{
					values.fontPath = g_fontChoices[current].path;
					settings::Save();
					g_fontRebuildPending = true;  // re-rasterise in the new face
					logger::info("settings page: font -> \"{}\" ({})",
								 g_fontChoices[current].label,
								 values.fontPath.empty() ? "auto" : values.fontPath.c_str());
				}
				ImGui::TextWrapped("Drop a .ttf into Data/SKSE/Plugins/ApocryphaMenuFramework/fonts "
								   "to add it to this list.");
			}
			ImGui::Spacing();
			ImGui::Spacing();

			// First setting by standing decision: the explicit input-mode toggle.
			if (widgets::Toggle("Controller input mode", &values.controllerMode))
			{
				logger::info("settings page: controller input mode -> {}", values.controllerMode);
				settings::Save();
			}
			ImGui::TextWrapped("Off: keyboard navigation (arrow keys, Enter, Escape). "
							   "On: gamepad navigation (D-pad moves, A activates, B cancels).");
			ImGui::Spacing();
			ImGui::Spacing();

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
			if (ImGui::SliderFloat("Text size", &values.textScale, 1.0f, 2.0f, "%.2f"))
			{
				// applied live via FontGlobalScale each frame
			}
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				logger::info("settings page: text scale -> {:.2f}", values.textScale);
				settings::Save();
				g_fontRebuildPending = true;  // re-rasterise at the new size rather than stretch
			}
			ImGui::TextWrapped("Extra text scaling on top of the automatic resolution scale.");
			ImGui::Spacing();
			ImGui::Spacing();

			// Menu toggle-key rebinding, live (design decision, 2026-08-28: "a key binding function ...
			// to change the key that opens and closes the menu"). Click Rebind, then the next
			// key pressed becomes the toggle key (Escape cancels); capture runs in the input
			// hook, so it works whether the menu is driven by keyboard or controller.
			if (input::IsAwaitingRebind())
			{
				ImGui::TextUnformatted("Menu toggle key: press any key...  (Escape cancels)");
			}
			else
			{
				if (values.toggleKey == 0x3B)
				{
					ImGui::TextUnformatted("Menu toggle key: F1");
				}
				else
				{
					ImGui::Text("Menu toggle key: scan code %d", values.toggleKey);
				}
				ImGui::SameLine();
				if (ImGui::Button("Rebind"))
				{
					input::BeginRebindToggleKey();
				}
			}
			ImGui::TextWrapped("Click Rebind, then press the key you want to open and close the "
							   "menu. In controller mode, the Start button also closes the menu.");
			ImGui::Spacing();
			ImGui::TextUnformatted("Window position: Centre");
			ImGui::TextWrapped("Preset positions rather than free placement; more presets arrive "
							   "in a later milestone.");

			// Persistence-channel test harness (decisions doc S10) - lets the per-save round
			// trip be exercised end to end (write, save, quit, reload, confirm) with no Papyrus
			// compiler involved. Debug-only surface; not a real setting.
			ImGui::Spacing();
			ImGui::Separator();
			DrawMenuListSection();
			ImGui::Spacing();

			ImGui::TextUnformatted("Persistence test (S10)");
			static char testBuffer[128] = "";
			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.7f);
			ImGui::InputText("##persistValue", testBuffer, sizeof(testBuffer));
			ImGui::SameLine();
			if (ImGui::Button("Set"))
			{
				persistence::SetValue("test-value", testBuffer);
			}
			ImGui::Text("Currently stored: \"%s\"", persistence::GetValue("test-value", "<unset>").c_str());
			ImGui::TextWrapped("Set a value, save the game, quit, reload the same save - the "
							   "value should still be here. A DIFFERENT save should show <unset>.");
		}

		// ---- Menu list: rename and reorder (author verdict 2026-09-01) ----------------------
		// Presentation only - the registry and the mods themselves are untouched. Numbering is
		// insert-and-shift: type a position and every other entry re-flows around it, so nobody
		// has to number the whole list by hand.
		void DrawMenuListSection()
		{
			const std::vector<registry::Entry> entries = registry::Snapshot();
			ImGui::SeparatorText("Menu list");
			ImGui::TextWrapped("Rename any mod's entry, and set the order of the list. Type a position "
							   "number to move an entry there - every other entry re-flows around it.");

			ImGui::Text("Order: %s", personalization::IsCustomOrder() ? "custom" : "alphabetical");
			ImGui::SameLine();
			if (ImGui::Button("Reset to alphabetical"))
			{
				personalization::ResetToAlphabetical();
				settings::Save();
			}

			if (entries.empty())
			{
				ImGui::TextDisabled("No mods have registered a page yet.");
				return;
			}

			const std::vector<personalization::DisplayEntry> rows = personalization::Order(entries);
			static std::unordered_map<std::string, std::array<char, 64>> aliasBuffers;

			if (ImGui::BeginTable("##menulist", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 3.2f);
				ImGui::TableSetupColumn("Mod");
				ImGui::TableSetupColumn("Shows as");
				ImGui::TableHeadersRow();

				for (int i = 0; i < static_cast<int>(rows.size()); ++i)
				{
					const personalization::DisplayEntry& row = rows[i];
					ImGui::TableNextRow();
					ImGui::PushID(row.modName.c_str());

					ImGui::TableSetColumnIndex(0);
					int position = i + 1;
					ImGui::SetNextItemWidth(-FLT_MIN);
					if (ImGui::InputInt("##pos", &position, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue) &&
						position != i + 1)
					{
						personalization::MoveTo(entries, row.modName, position);
						settings::Save();
					}

					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(row.modName.c_str());

					ImGui::TableSetColumnIndex(2);
					auto buffer = aliasBuffers.find(row.modName);
					if (buffer == aliasBuffers.end())
					{
						std::array<char, 64> fresh{};
						const std::string alias = personalization::GetAlias(row.modName);
						std::snprintf(fresh.data(), fresh.size(), "%s", alias.c_str());
						buffer = aliasBuffers.emplace(row.modName, fresh).first;
					}
					ImGui::SetNextItemWidth(-FLT_MIN);
					const bool committed =
						ImGui::InputTextWithHint("##alias", row.modName.c_str(), buffer->second.data(),
												 buffer->second.size(), ImGuiInputTextFlags_EnterReturnsTrue);
					if (committed || ImGui::IsItemDeactivatedAfterEdit())
					{
						personalization::SetAlias(row.modName, buffer->second.data());
						settings::Save();
					}

					ImGui::PopID();
				}
				ImGui::EndTable();
			}
		}

		// ---- nested game-menu leaf panes (the author's game-menu-replacement model, 2026-08-28) ----

		// Controls: the vanilla System tab has a Controls entry; ours documents the framework's
		// own bindings and hosts the toggle-key rebind (the same control as on Settings).
		void DrawControlsPane()
		{
			auto& values = settings::Get();
			ImGui::TextUnformatted("Controls");
			ImGui::Separator();
			if (input::IsAwaitingRebind())
			{
				ImGui::TextUnformatted("Menu toggle key: press any key...  (Escape cancels)");
			}
			else
			{
				if (values.toggleKey == 0x3B) { ImGui::TextUnformatted("Menu toggle key: F1"); }
				else { ImGui::Text("Menu toggle key: scan code %d", values.toggleKey); }
				ImGui::SameLine();
				if (ImGui::Button("Rebind##controls")) { input::BeginRebindToggleKey(); }
			}
			ImGui::Spacing();
			ImGui::TextUnformatted("Keyboard:  arrow keys move, Enter activates, Escape closes.");
			ImGui::TextUnformatted("Controller (controller mode on):");
			ImGui::BulletText("Left stick moves through the list and across to the options - no button needed.");
			ImGui::BulletText("A takes hold of a slider; the RIGHT stick then moves it. A again lets go.");
			ImGui::BulletText("A on a drop-down opens it; the sticks choose; A confirms.");
			ImGui::BulletText("B cancels, START closes the menu. The D-pad does everything the left stick does.");
		}

		void DrawHelpPane()
		{
			ImGui::TextUnformatted("Help");
			ImGui::Separator();
			ImGui::TextWrapped("Apocrypha Menu Framework presents mod settings in one menu, laid out like "
							   "the game's own: tabs across the top, a list down the side, and the "
							   "selected entry's options here.");
			ImGui::Spacing();
			ImGui::TextWrapped("Mod settings live under System -> Mod menus, the same place SkyUI puts Mod "
							   "Configuration. Framework options are under System -> Settings, and key "
							   "bindings under System -> Controls.");
		}

		void DrawFrameworkWindow()
		{
			const ImVec2 display = ImGui::GetIO().DisplaySize;

			// PRESET positions, never free placement (design decision, 2026-08-27) - the same anchor
			// philosophy as the minimap's corner presets, computed from the display centre so
			// it is correct at any resolution. Preset 0 = centre is the standard; further
			// presets become a settings-page dropdown once worked out. Position is therefore
			// ImGuiCond_Always + NoMove; only the SIZE belongs to the user.
			ImVec2 anchor{ 0.5f, 0.5f };
			switch (settings::Get().windowPreset)
			{
			default:
				break;  // 0 (and any unknown value) = centre
			}
			ImGui::SetNextWindowPos(ImVec2(display.x * anchor.x, display.y * anchor.y), ImGuiCond_Always, anchor);
			ImGui::SetNextWindowSize(ImVec2(display.x * 0.55f, display.y * 0.70f), ImGuiCond_FirstUseEver);

			if (ImGui::Begin("Apocrypha Menu Framework##m2", nullptr, ImGuiWindowFlags_NoMove))
			{
				// Version always on show - a version-less status line reads as a stale build
				// (the author, third smoke test).
				static const std::string version =
					SKSE::PluginDeclaration::GetSingleton()->GetVersion().string(".");
				ImGui::Text("Apocrypha Menu Framework  v%s", version.c_str());
				ImGui::Separator();

				// Whether this theme wants the knotwork frame - captured once, applied to every
				// panel below and the outer window for a consistent framed look.
				const bool knot = theme::GetActiveTheme().knotwork;

				const float leftWidth = ImGui::GetContentRegionAvail().x * 0.30f;

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
				const std::vector<registry::Entry> entries = registry::Snapshot();
				if (selMod >= static_cast<int>(entries.size())) { selMod = 0; }

				// ---- SIDE LIST -------------------------------------------------------------------
				ImGui::BeginChild("##side", ImVec2(leftWidth, 0.0f), true, ImGuiWindowFlags_NavFlattened);
				auto sideItem = [&](const char* label, const char* id) {
					if (ImGui::Selectable(label, sel == id)) { sel = id; changed = true; }
				};
				ImGui::TextDisabled("Framework");
				sideItem("Settings", "settings");
				sideItem("Controls", "controls");
				sideItem("Help",     "help");
				ImGui::Separator();
				ImGui::TextDisabled("Mods");
				// Player-facing order and names (menu-shell personalization). The rows carry the
				// REGISTRY index, so selection, the C API and DevBench addressing are unaffected.
				for (const personalization::DisplayEntry& row : personalization::Order(entries))
				{
					if (ImGui::Selectable(row.displayName.c_str(), sel == "mod" && selMod == row.registryIndex))
					{
						sel = "mod";
						selMod = row.registryIndex;
						changed = true;
					}
				}
				if (entries.empty()) { ImGui::TextDisabled("none registered"); }
				ImGui::EndChild();
				if (knot)
				{
					DrawKnotworkFrame(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
				}

				ImGui::SameLine();

				// ---- CONTENT PANE -------------------------------------------------------------
				ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_NavFlattened);
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
					if (entry.pages.size() == 1)
					{
						entry.pages[0].render();
					}
					else if (ImGui::BeginTabBar("##pages"))
					{
						for (const registry::Page& page : entry.pages)
						{
							if (ImGui::BeginTabItem(page.pageName.c_str()))
							{
								page.render();
								ImGui::EndTabItem();
							}
						}
						ImGui::EndTabBar();
					}
				}
				else { DrawFrameworkSettingsPane(); }
				ImGui::EndChild();
				if (knot)
				{
					DrawKnotworkFrame(ImGui::GetWindowDrawList(), ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
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

				// A font or text-size change rebuilds the atlas. This MUST happen before
				// ImGui_ImplDX11_NewFrame: that call is where the DX11 backend recreates its
				// device objects (font texture included) when they are missing. The old order -
				// invalidating AFTER the backend NewFrame had already run - destroyed the font
				// texture with nothing left in the frame to recreate it, so the frame rendered
				// its draw data against a dead texture and crashed the moment the font or the
				// text-size slider changed (author playtest, 2026-08-31).
				if (g_fontRebuildPending.exchange(false))
				{
					ImGui_ImplDX11_InvalidateDeviceObjects();
					BuildFonts();
				}

				ImGui_ImplDX11_NewFrame();
				ImGui_ImplWin32_NewFrame();

				const bool visible = g_windowVisible.load(std::memory_order_acquire);

				// Open-transition work happens HERE, not in ToggleMainWindow - the toggle is
				// flipped on the input thread, and cursor centring touches ImGui state.
				if (g_justOpened.exchange(false, std::memory_order_acq_rel))
				{
					input::OnMenuOpened();
				}

				// Translation runs after the backends' NewFrame (so our queued io.Add*Event
				// calls land after, and therefore win over, the Win32 backend's own
				// GetCursorPos-based mouse update) and before ImGui::NewFrame consumes them.
				if (visible)
				{
					input::ProcessQueuedEvents();
				}

				ImGuiIO& io = ImGui::GetIO();

				// Software cursor while the menu is open - the game hides and recentres the OS
				// cursor at will, so ImGui draws its own at the position we integrate.
				io.MouseDrawCursor = visible;

				// Nav mode follows the EXPLICIT setting live (the toggle sits on the settings
				// page itself). Never auto-detected - that is the nav-focus-drift bug.
				if (settings::Get().controllerMode)
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

				watchdog::Tick();  // liveness signal for the hang watchdog
				ImGui::NewFrame();

				// The game's own HUD opacity, re-read every frame so the options slider is
				// followed live (theme spec point 3), applied as the ONE global multiplier.
				ImGui::GetStyle().Alpha = theme::GetGameHUDOpacity();

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

				ImGui::Render();
				ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

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
		g_windowVisible.store(now, std::memory_order_release);

		if (now)
		{
			g_justOpened.store(true, std::memory_order_release);
		}

		logger::info("Framework window {}", now ? "shown" : "hidden");
	}

	bool IsMainWindowVisible()
	{
		return g_windowVisible.load(std::memory_order_relaxed);
	}

	void SetMenuVisible(bool a_visible)
	{
		g_windowVisible.store(a_visible, std::memory_order_release);
		if (a_visible) { g_justOpened.store(true, std::memory_order_release); }
		logger::info("Framework window {} (external/DevBench)", a_visible ? "shown" : "hidden");
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

	bool SetModAlias(const std::string& a_modName, const std::string& a_alias)
	{
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

	std::string GetMenuStateJson()
	{
		std::string node, tab; int selMod;
		{ std::scoped_lock l(g_selLock); node = g_selNode; tab = g_selTab; selMod = g_selMod; }
		const bool visible = g_windowVisible.load(std::memory_order_relaxed);
		const auto entries = registry::Snapshot();
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
				mods += "{\"index\":" + std::to_string(i) + ",\"name\":\"" + esc(entries[i].modName) + "\",\"pages\":[" + pages + "]}";
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
						 ",\"mod\":\"" + esc(rows[i].modName) + "\",\"shows\":\"" + esc(rows[i].displayName) + "\"}";
			}
		}
		return std::string("{\"visible\":") + (visible ? "true" : "false") +
			   ",\"tab\":\"" + esc(tab) + "\",\"selected\":\"" + esc(node) + "\",\"selectedMod\":" + std::to_string(selMod) +
			   ",\"customOrder\":" + (personalization::IsCustomOrder() ? "true" : "false") +
			   ",\"displayOrder\":[" + order + "]" +
			   ",\"mods\":[" + mods + "]}";
	}
}
