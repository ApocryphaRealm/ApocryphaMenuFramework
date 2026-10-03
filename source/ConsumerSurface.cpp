#include "ConsumerSurface.h"

#include "utils/Logger.h"

#include <imgui.h>
#include <imgui_internal.h>   // GImGui->Windows: which windows each consumer submitted (2.0.4 input gate)

#include <d3d11.h>
#include <WICTextureLoader.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	struct WindowEntry
	{
		std::unique_ptr<consumer::WindowInterface> iface;
		consumer::RenderFunction render{ nullptr };
		std::string view;   // AddWindowWithView's name; empty for a plain AddWindow

		// The 2.0.4 probe: the top-level ImGui windows this entry's render function submitted on the
		// last drawn frame, and whether any of them takes the mouse. Written by DrawWindows under g_lock.
		std::vector<consumer::SubmittedWindow> submitted;
		bool acceptsMouse{ false };
		bool passiveLogged{ false };   // "open and blocking but takes no mouse" said once per opening
	};

	struct HudEntry
	{
		std::int64_t id{ 0 };
		consumer::HudCallback callback{ nullptr };
	};

	struct TextureEntry
	{
		ID3D11ShaderResourceView* srv{ nullptr };
		float width{ 0.0f };
		float height{ 0.0f };
	};

	std::mutex g_lock;
	std::vector<WindowEntry> g_windows;
	std::vector<HudEntry> g_hud;
	std::unordered_map<std::string, TextureEntry> g_textures;
	std::int64_t g_nextHudId = 1;

	ID3D11Device* g_device = nullptr;

	// How many fonts WE pushed. Pop() only pops what we are responsible for, so a consumer
	// calling Pop() more often than it pushed cannot unbalance the framework's own stack.
	int g_fontDepth = 0;

	std::mutex g_warnLock;
	std::vector<std::string> g_warnedTextures;   // one warning per path, not per frame
}

namespace consumer
{
	WindowInterface* AddWindow(RenderFunction a_render, const char* a_view)
	{
		if (!a_render) {
			logger::warn("AddWindow refused: null render function");
			return nullptr;
		}

		std::scoped_lock lock(g_lock);

		WindowEntry entry;
		entry.iface = std::make_unique<WindowInterface>();
		entry.render = a_render;
		if (a_view) {
			entry.view = a_view;
		}

		auto* const handed = entry.iface.get();
		g_windows.push_back(std::move(entry));

		logger::info("AddWindow{} (SMF-compat): consumer window registered ({} total)",
					 a_view ? "WithView" : "", g_windows.size());

		return handed;
	}

	std::int64_t RegisterHudElement(HudCallback a_callback)
	{
		if (!a_callback) {
			logger::warn("RegisterHudElement refused: null callback");
			return 0;
		}

		std::scoped_lock lock(g_lock);
		const auto id = g_nextHudId++;
		g_hud.push_back(HudEntry{ id, a_callback });
		logger::info("RegisterHudElement (SMF-compat): HUD element {} registered ({} total)", id, g_hud.size());
		return id;
	}

	void UnregisterHudElement(std::int64_t a_id)
	{
		std::scoped_lock lock(g_lock);
		const auto before = g_hud.size();
		std::erase_if(g_hud, [a_id](const HudEntry& e) { return e.id == a_id; });
		if (g_hud.size() != before) {
			logger::info("UnregisterHudElement (SMF-compat): HUD element {} removed ({} left)", a_id, g_hud.size());
		}
	}

	void DrawWindows()
	{
		// Snapshot under the lock, call outside it: a consumer's render function may register
		// another window, and re-entering this mutex from inside it would deadlock the render
		// thread - which presents as the game freezing on a frame, not as an error.
		// Render thread only, so the scratch vectors are reused frame to frame rather than allocated.
		static std::vector<std::pair<std::size_t, RenderFunction>> due;
		due.clear();
		std::size_t count = 0;
		{
			std::scoped_lock lock(g_lock);
			count = g_windows.size();
			for (std::size_t i = 0; i < g_windows.size(); ++i) {
				const auto& w = g_windows[i];
				if (w.render && w.iface->IsOpen.load(std::memory_order_acquire)) {
					due.emplace_back(i, w.render);
				}
			}
		}

		// WHICH IMGUI WINDOWS DID EACH CONSUMER SUBMIT (2.0.4). The flags a mod hands us do not say
		// whether its window wants the player: the stock header's AddWindow(render, doesWindowPauseGame
		// = true) sets BlockUserInput on every window it makes, so StepUpOnto SKSE's always-on NPC perf
		// overlay arrived open AND blocking during ordinary play, and the first 2.0.4 build took all of
		// the game's input for it (the main session, in game, 2026-10-03). What a window really wants is
		// in how the mod DRAWS it: an overlay that must not catch the mouse says so to ImGui with
		// NoMouseInputs (NoInputs includes it), and ImGui then lets every click pass through it. So the
		// windows each render function begins are observed here - every non-child window that became
		// active this frame while that function ran - and the input gate (AnyWindowOwnsInput) asks
		// whether any of them takes the mouse. Windows already active before the call (HUD elements,
		// ImGui's implicit Debug window) are excluded, so nothing is credited to the wrong mod.
		ImGuiContext* const g = GImGui;
		static std::vector<ImGuiWindow*> seen;
		static std::vector<std::vector<SubmittedWindow>> results;
		seen.clear();
		if (results.size() < count) { results.resize(count); }
		for (auto& r : results) { r.clear(); }
		const int frame = g ? g->FrameCount : -1;
		if (g) {
			for (ImGuiWindow* w : g->Windows) {
				if (w && w->LastFrameActive == frame) { seen.push_back(w); }
			}
		}

		for (const auto& [index, fn] : due) {
			// The consumer's callback opens its own ImGui window - SMF's contract, and the
			// reason nothing is begun for it here.
			fn();

			if (!g) { continue; }
			for (ImGuiWindow* w : g->Windows) {
				if (!w || w->LastFrameActive != frame || (w->Flags & ImGuiWindowFlags_ChildWindow) != 0 ||
					std::find(seen.begin(), seen.end(), w) != seen.end()) {
					continue;
				}
				seen.push_back(w);
				SubmittedWindow s;
				std::snprintf(s.name, sizeof(s.name), "%s", w->Name ? w->Name : "");
				s.flags = static_cast<int>(w->Flags);
				s.noMouseInputs = (w->Flags & ImGuiWindowFlags_NoMouseInputs) != 0;
				s.noInputs = (w->Flags & ImGuiWindowFlags_NoInputs) == ImGuiWindowFlags_NoInputs;
				s.x = w->Pos.x; s.y = w->Pos.y; s.w = w->Size.x; s.h = w->Size.y;
				results[index].push_back(s);
			}
		}

		std::scoped_lock lock(g_lock);
		for (std::size_t i = 0; i < g_windows.size(); ++i) {
			auto& e = g_windows[i];
			if (i < results.size()) { e.submitted = results[i]; } else { e.submitted.clear(); }
			e.acceptsMouse = std::any_of(e.submitted.begin(), e.submitted.end(),
				[](const SubmittedWindow& s) { return !s.noMouseInputs; });

			const bool open = e.iface->IsOpen.load(std::memory_order_acquire);
			if (!open) {
				e.passiveLogged = false;
			} else if (!e.passiveLogged && !e.submitted.empty() && !e.acceptsMouse &&
					   e.iface->BlockUserInput.load(std::memory_order_acquire)) {
				e.passiveLogged = true;
				logger::info("input: a mod's window \"{}\" is open and asks to block input, but takes no mouse input "
							 "(NoMouseInputs) - a passive overlay; the game keeps its input",
							 e.submitted.front().name);
			}
		}
	}

	void DrawHudElements()
	{
		std::vector<HudCallback> due;
		{
			std::scoped_lock lock(g_lock);
			due.reserve(g_hud.size());
			for (const auto& h : g_hud) {
				if (h.callback) {
					due.push_back(h.callback);
				}
			}
		}

		for (const auto fn : due) {
			fn();
		}
	}

	bool AnyBlockingWindowOpen()
	{
		std::scoped_lock lock(g_lock);
		return std::any_of(g_windows.begin(), g_windows.end(), [](const WindowEntry& w) {
			return w.iface->IsOpen.load(std::memory_order_acquire) &&
				   w.iface->BlockUserInput.load(std::memory_order_acquire);
		});
	}

	bool AnyWindowOwnsInput()
	{
		// IsOpen is read NOW, so a window that has just closed hands the input back at once; only the
		// "takes the mouse" half is a frame old, because it comes from the last drawn frame.
		std::scoped_lock lock(g_lock);
		return std::any_of(g_windows.begin(), g_windows.end(), [](const WindowEntry& w) {
			return w.acceptsMouse &&
				   w.iface->IsOpen.load(std::memory_order_acquire) &&
				   w.iface->BlockUserInput.load(std::memory_order_acquire);
		});
	}

	std::string InputOwnerName()
	{
		std::scoped_lock lock(g_lock);
		for (const auto& w : g_windows) {
			if (!w.acceptsMouse || !w.iface->IsOpen.load(std::memory_order_acquire) ||
				!w.iface->BlockUserInput.load(std::memory_order_acquire)) {
				continue;
			}
			for (const auto& s : w.submitted) {
				if (!s.noMouseInputs) { return s.name; }
			}
		}
		return {};
	}

	void PushNamedFont(const char* a_name)
	{
		// AMF builds ONE atlas font (the shell's face, rebuilt on scale change), so there is no
		// second face to switch to yet. Pushing the current font keeps every consumer's
		// Push/Pop pair balanced, which is what actually matters for the frames around it - a
		// mismatched name changes appearance, never correctness.
		ImGui::PushFont(ImGui::GetFont());
		++g_fontDepth;

		if (a_name && *a_name) {
			static std::mutex once;
			static std::vector<std::string> logged;
			std::scoped_lock lock(once);
			const std::string name(a_name);
			if (std::find(logged.begin(), logged.end(), name) == logged.end()) {
				logged.push_back(name);
				logger::debug("PushFont (SMF-compat): \"{}\" requested; AMF ships a single face, so the current font was pushed", name);
			}
		}
	}

	void PushRegular() { PushNamedFont("regular"); }
	void PushSolid() { PushNamedFont("solid"); }
	void PushBrands() { PushNamedFont("brands"); }

	void PopFont()
	{
		if (g_fontDepth > 0) {
			ImGui::PopFont();
			--g_fontDepth;
		}
	}

	void SetDevice(ID3D11Device* a_device)
	{
		g_device = a_device;
	}

	void* LoadTexture(const char* a_path, ImVec2* a_outSize)
	{
		if (!a_path || !*a_path) {
			return nullptr;
		}

		const std::string path(a_path);

		{
			std::scoped_lock lock(g_lock);
			if (const auto it = g_textures.find(path); it != g_textures.end()) {
				if (a_outSize) {
					a_outSize->x = it->second.width;
					a_outSize->y = it->second.height;
				}
				return it->second.srv;   // cached: consumers call this every frame
			}
		}

		if (!g_device) {
			std::scoped_lock lock(g_warnLock);
			if (std::find(g_warnedTextures.begin(), g_warnedTextures.end(), path) == g_warnedTextures.end()) {
				g_warnedTextures.push_back(path);
				logger::warn("LoadTexture (SMF-compat): the D3D device is not up yet; \"{}\" not loaded", path);
			}
			return nullptr;
		}

		const std::wstring wide(path.begin(), path.end());
		ID3D11Resource* resource = nullptr;
		ID3D11ShaderResourceView* srv = nullptr;

		if (FAILED(DirectX::CreateWICTextureFromFile(g_device, wide.c_str(), &resource, &srv))) {
			std::scoped_lock lock(g_warnLock);
			if (std::find(g_warnedTextures.begin(), g_warnedTextures.end(), path) == g_warnedTextures.end()) {
				g_warnedTextures.push_back(path);
				logger::warn("LoadTexture (SMF-compat): could not decode \"{}\"", path);
			}
			if (resource) {
				resource->Release();
			}
			return nullptr;
		}

		TextureEntry entry;
		entry.srv = srv;

		if (resource) {
			ID3D11Texture2D* tex = nullptr;
			if (SUCCEEDED(resource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&tex))) && tex) {
				D3D11_TEXTURE2D_DESC desc{};
				tex->GetDesc(&desc);
				entry.width = static_cast<float>(desc.Width);
				entry.height = static_cast<float>(desc.Height);
				tex->Release();
			}
			resource->Release();
		}

		{
			std::scoped_lock lock(g_lock);
			g_textures[path] = entry;
		}

		if (a_outSize) {
			a_outSize->x = entry.width;
			a_outSize->y = entry.height;
		}

		logger::info("LoadTexture (SMF-compat): \"{}\" loaded ({}x{})", path, entry.width, entry.height);
		return entry.srv;
	}

	void DisposeTexture(const char* a_path)
	{
		if (!a_path) {
			return;
		}

		std::scoped_lock lock(g_lock);
		if (const auto it = g_textures.find(a_path); it != g_textures.end()) {
			if (it->second.srv) {
				it->second.srv->Release();
			}
			g_textures.erase(it);
			logger::debug("DisposeTexture (SMF-compat): \"{}\" released", a_path);
		}
	}

	std::size_t WindowCount()
	{
		std::scoped_lock lock(g_lock);
		return g_windows.size();
	}

	std::vector<WindowState> WindowStates()
	{
		// Copied out under the lock - no reference to g_windows escapes, and nothing here calls
		// AnyBlockingWindowOpen(), which takes the same lock. The caller computes the aggregate
		// from this vector instead, so the two can never nest.
		std::scoped_lock lock(g_lock);

		std::vector<WindowState> states;
		states.reserve(g_windows.size());
		for (const WindowEntry& w : g_windows) {
			WindowState s;
			s.open = w.iface->IsOpen.load(std::memory_order_acquire);
			s.blocking = w.iface->BlockUserInput.load(std::memory_order_acquire);
			s.acceptsMouse = w.acceptsMouse;
			s.view = w.view;
			s.submitted = w.submitted;
			states.push_back(std::move(s));
		}
		return states;
	}

	std::size_t HudElementCount()
	{
		std::scoped_lock lock(g_lock);
		return g_hud.size();
	}

	std::size_t TextureCount()
	{
		std::scoped_lock lock(g_lock);
		return g_textures.size();
	}
}
