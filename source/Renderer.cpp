#include "Renderer.h"

#include "Offsets.h"
#include "Theme.h"
#include "utils/Logger.h"

#include <imgui.h>
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

				ImGui_ImplWin32_Init(hwnd);
				ImGui_ImplDX11_Init(device, context);

				theme::Apply();

				g_uiScale = window.windowHeight > 0 ? static_cast<float>(window.windowHeight) / 1080.0f : 1.0f;
				if (g_uiScale < 1.0f)
				{
					g_uiScale = 1.0f;  // never shrink below the 1080p baseline
				}

				ImGui::GetIO().FontGlobalScale = g_uiScale;
				ImGui::GetStyle().ScaleAllSizes(g_uiScale);

				logger::info("UI scale set to {:.2f} for a {}px-tall display (1080p baseline)", g_uiScale, window.windowHeight);

				logger::info("ImGui initialized on the game's device (window {}, {}x{}); theme applied",
							 static_cast<const void*>(hwnd), window.windowWidth, window.windowHeight);
			}
		};

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
				ImGui::NewFrame();

				// The game's own HUD opacity, re-read every frame so the options slider is
				// followed live (theme spec point 3), applied as the ONE global multiplier.
				ImGui::GetStyle().Alpha = theme::GetGameHUDOpacity();

				if (g_windowVisible.load(std::memory_order_relaxed))
				{
					// Centre-relative default position - the same principle as Local Map Upgrade's
					// border (the author, from the smoke test): anchor to the DISPLAY CENTRE with a
					// centre pivot, so the default placement is correct at any resolution instead
					// of falling wherever ImGui's top-left default lands (a sliver in the corner
					// at 3200x1800, per the 16:35 capture). FirstUseEver on both, so a player who
					// moves or resizes the window keeps their arrangement.
					const ImVec2 display = ImGui::GetIO().DisplaySize;
					ImGui::SetNextWindowPos(ImVec2(display.x * 0.5f, display.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
					ImGui::SetNextWindowSize(ImVec2(520.0f * g_uiScale, 340.0f * g_uiScale), ImGuiCond_FirstUseEver);

					if (ImGui::Begin("Apocrypha Menu Framework"))
					{
						ImGui::TextUnformatted("M1 render loop is live.");
						ImGui::Separator();
						ImGui::Text("Game HUD opacity: %.2f", theme::GetGameHUDOpacity());
						ImGui::TextUnformatted("Input capture arrives in M2; this window is display-only.");
					}
					ImGui::End();
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
		SKSE::AllocTrampoline(static_cast<std::size_t>(14) * 2);

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
		g_windowVisible.store(now, std::memory_order_relaxed);

		logger::info("Framework window {}", now ? "shown" : "hidden");
	}

	bool IsMainWindowVisible()
	{
		return g_windowVisible.load(std::memory_order_relaxed);
	}
}
