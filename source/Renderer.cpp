#include "Renderer.h"

#include "Input.h"
#include "Offsets.h"
#include "Persistence.h"
#include "Registry.h"
#include "Settings.h"
#include "Theme.h"
#include "utils/ToggleSwitch.h"
#include "utils/Logger.h"

#include <imgui.h>
#include <vector>
// The vcpkg imgui port installs the binding headers FLAT at the include root, not under
// backends/ as in the upstream repo layout.
#include <d3d11.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <atomic>

namespace renderer
{
	namespace
	{
		std::atomic<bool> g_d3dReady{ false };
		std::atomic<bool> g_windowVisible{ false };
		std::atomic<bool> g_justOpened{ false };  // set on the input thread, consumed on the render thread

		// M1.1 (the author's smoke-test feedback): at 3200x1800 the stock ImGui font and a fixed
		// 520x340 window are "far too small". One scale factor, derived from the real display
		// height against 1080p as the baseline, applied to the font, the style metrics and the
		// default window size together so everything stays proportioned.
		float g_uiScale = 1.0f;

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

				theme::Apply();

				g_uiScale = window.windowHeight > 0 ? static_cast<float>(window.windowHeight) / 1080.0f : 1.0f;
				if (g_uiScale < 1.0f)
				{
					g_uiScale = 1.0f;  // never shrink below the 1080p baseline
				}

				// Text runs 30% larger than the pure resolution scale - the author's 1.0.1 feedback:
				// "bigger text relative to the current size." Fonts only; widget/padding
				// geometry keeps the unboosted scale below.
				ImGui::GetIO().FontGlobalScale = g_uiScale * 1.30f;
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
			}
			ImGui::TextWrapped("Extra text scaling on top of the automatic resolution scale.");
			ImGui::Spacing();
			ImGui::Spacing();

			if (values.toggleKey == 0x3B)
			{
				ImGui::TextUnformatted("Menu toggle key: F1");
			}
			else
			{
				ImGui::Text("Menu toggle key: scan code %d", values.toggleKey);
			}
			ImGui::TextWrapped("Rebinding arrives with the Controls page in a later milestone; "
							   "until then the key can be changed in ApocryphaMenuFramework.ini.");
			ImGui::Spacing();
			ImGui::TextUnformatted("Window position: Centre");
			ImGui::TextWrapped("Preset positions rather than free placement; more presets arrive "
							   "in a later milestone.");

			// Persistence-channel test harness (decisions doc S10) - lets the per-save round
			// trip be exercised end to end (write, save, quit, reload, confirm) with no Papyrus
			// compiler involved. Debug-only surface; not a real setting.
			ImGui::Spacing();
			ImGui::Separator();
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

				const float leftWidth = ImGui::GetContentRegionAvail().x * 0.30f;

				// Left pane: ONE entry per mod (index 0 is the framework itself). Right pane:
				// the selected mod's pages - several pages render as TABS inside the one menu
				// (project rule: one menu per mod, never several).
				static int selectedIndex = 0;
				const std::vector<registry::Entry> entries = registry::Snapshot();

				if (selectedIndex > static_cast<int>(entries.size()))
				{
					selectedIndex = 0;
				}

				ImGui::BeginChild("##menuList", ImVec2(leftWidth, 0.0f), true);
				ImGui::TextUnformatted("Menus");
				ImGui::Separator();
				if (ImGui::Selectable("Framework Settings", selectedIndex == 0))
				{
					selectedIndex = 0;
				}
				for (int i = 0; i < static_cast<int>(entries.size()); ++i)
				{
					if (ImGui::Selectable(entries[i].modName.c_str(), selectedIndex == i + 1))
					{
						selectedIndex = i + 1;
					}
				}
				if (entries.empty())
				{
					ImGui::Spacing();
					ImGui::TextWrapped("No mod has registered a menu yet.");
				}
				ImGui::EndChild();

				ImGui::SameLine();

				ImGui::BeginChild("##settingsPane", ImVec2(0.0f, 0.0f), true);
				if (selectedIndex == 0)
				{
					DrawFrameworkSettingsPane();
				}
				else
				{
					const registry::Entry& entry = entries[selectedIndex - 1];

					ImGui::TextUnformatted(entry.modName.c_str());
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
				ImGui::EndChild();
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
				}
				else
				{
					io.ConfigFlags = (io.ConfigFlags | ImGuiConfigFlags_NavEnableKeyboard) & ~ImGuiConfigFlags_NavEnableGamepad;
				}

				// Live setting: the fTextScale slider must take effect while being dragged.
				io.FontGlobalScale = g_uiScale * settings::Get().textScale;

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
}
