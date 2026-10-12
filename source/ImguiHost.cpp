#include "ImguiHost.h"

#include "Bindings.h"
#include "ConsumerSurface.h"
#include "HostCore.h"
#include "Input.h"
#include "Renderer.h"
#include "Settings.h"
#include "Strings.h"
#include "Theme.h"
#include "utils/Logger.h"
#include "utils/ToggleSwitch.h"

#include "OAR/OpenAnimationReplacerAPI-UI.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <Windows.h>
#include <Psapi.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace imguihost
{
	namespace
	{
		using strings::TR;

		enum Id : int
		{
			kOar = 0,
			kCs,
			kIed,
			kOpti,
			kCount
		};

		constexpr std::array<const char*, kCount> kIds = { "oar", "cs", "ied", "optiscaler" };
		// the mods' own names - proper names, the same in every language
		constexpr std::array<const char*, kCount> kNames = { "Open Animation Replacer", "Community Shaders", "Immersive Equipment Displays",
															 "OptiScaler" };

		struct Target
		{
			bool loaded = false;
			std::wstring modulePath;     // the loaded DLL (OptiScaler: whatever name its proxy has)
			hostcore::KeyCombo key;      // read from its config at the last refresh
			std::string keyFrom;         // where the key was read ("OpenAnimationReplacer.ini", "SettingsUser.json" ...)
			std::string blocked;         // why it cannot be opened from here ("" = it can)
			bool keyOpensOnly = false;   // IED: its key only opens; Escape closes
			int state = -1;              // -1 not known, 0 closed, 1 open
			int presses = 0;
			std::string lastOutcome;
		};

		std::mutex g_lock;   // guards the targets (the render thread draws and acts, DevBench lists)
		std::array<Target, kCount> g_targets;
		bool g_loaded = false;

		// OAR's UI interface: V3 (3.2.1+) when it answers it, else none here (3.0.2 is opened with its key)
		OAR_API::UI::IUIInterface3* g_oarV3 = nullptr;
		// v6: OAR's V2 interface (every OAR from 3.0.2), asked only whether OAR built an ImGui context this session - a null
		// check, nothing of the context is read. OAR reads bEnableUI once at start-up and with it off builds no menu at all;
		// with it on its context exists from the first frame (the v5 bridge log saw it then). So after the first frames a
		// null context means "no menu this session", whatever the INI says now.
		OAR_API::UI::IUIInterface2* g_oarV2 = nullptr;
		std::atomic<int> g_framesSeen{ 0 };   // Frame() calls so far (read by RefreshKeys on any thread)
		constexpr int kOarUiSettledFrames = 120;
		int g_oarUiLogged = -2;               // the last session answer logged (-1 not known, 0 no menu, 1 menu)

		// one key press at a time, stepped over frames (render thread only)
		struct Press
		{
			int target = -1;
			hostcore::KeyCombo combo;
			int startFrame = 0;
			int nextStep = 0;            // 0 modifiers down, 1 key down, 2 key up, 3 modifiers up, 4 done
			bool modsDown = false, keyDown = false;
			std::string sent;            // SendInput's counts, for the log line
		};
		std::optional<Press> g_press;
		// an API call waiting for AMF's menu to be gone (render thread only)
		struct ApiCall
		{
			int target = -1;
			bool open = true;
			int atFrame = 0;
		};
		std::optional<ApiCall> g_apiCall;
		int g_frame = 0;               // Frame()'s own count (render thread)
		bool g_reopenAfterOar = false; // the launcher closed AMF for OAR: open it again on this page when OAR closes
		bool g_oarSeenOpen = false;
		int g_reopenDeadline = 0;
		int g_pageDrawnFrame = -100;   // the last frame DrawPage ran, to refresh the keys when the page is opened

		// ---- phase 2: the bridge and the hosted page (render thread unless noted) ----
		struct Bridge
		{
			bool registered = false;
			std::uint32_t api = 0;
			AMF_HostFrameCallback callback = nullptr;
			void* user = nullptr;
			bool failed = false;          // set by AMF_HostBridgeFailed (any thread) or a fault in the call
			std::string failReason;
			int targetOpen = -1;          // what the bridge saw last frame
			int wantsMouse = 0;
			std::uint64_t placedFrames = 0;
			std::uint64_t calls = 0;
		};
		std::mutex g_bridgeLock;          // registration and failure may come from any thread
		Bridge g_bridge;                  // target "oar"

		struct Hosting
		{
			bool active = false;
			int startFrame = 0;
			bool seenOpen = false;
			float x = 0, y = 0, w = 0, h = 0;   // the empty rectangle, this frame (display pixels)
			int rectFrame = -1;                 // ImGui frame the rectangle was taken on
			std::uint64_t eventsAtOpen = 0;     // AMF's input-hook event count when OAR's menu opened
			std::uint64_t eventsWhileOpen = 0;  // ... and how many reached AMF's hook while it was open (expected 0)
			std::uint64_t osPresses = 0;        // mouse presses AMF took from the OS while hosting
			std::string lastEnd;                // why the last hosting ended
			int sessions = 0;
		};
		Hosting g_host;
		bool g_osDown[3] = {};
		bool g_osToggleDown = false;
		bool g_osPolling = false;

		Bridge BridgeCopy()
		{
			std::scoped_lock l(g_bridgeLock);
			return g_bridge;
		}

		bool HostAvailable()
		{
			const Bridge b = BridgeCopy();
			return b.registered && !b.failed && settings::Get().hostOar;
		}

		int OarState()
		{
			std::scoped_lock l(g_lock);
			return g_targets[kOar].state;
		}

		bool SwitchOn(int a_id)
		{
			const auto& v = settings::Get();
			switch (a_id)
			{
			case kOar: return v.imguiOar;
			case kCs: return v.imguiCs;
			case kIed: return v.imguiIed;
			case kOpti: return v.imguiOpti;
			default: return false;
			}
		}

		bool* SwitchPtr(int a_id)
		{
			auto& v = settings::Get();
			switch (a_id)
			{
			case kOar: return &v.imguiOar;
			case kCs: return &v.imguiCs;
			case kIed: return &v.imguiIed;
			case kOpti: return &v.imguiOpti;
			default: return nullptr;
			}
		}

		std::string Narrow(const std::wstring& a_w)
		{
			if (a_w.empty()) { return std::string(); }
			const int n = ::WideCharToMultiByte(CP_UTF8, 0, a_w.c_str(), static_cast<int>(a_w.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<std::size_t>(n), '\0');
			::WideCharToMultiByte(CP_UTF8, 0, a_w.c_str(), static_cast<int>(a_w.size()), out.data(), n, nullptr, nullptr);
			return out;
		}

		std::wstring ModulePath(HMODULE a_m)
		{
			wchar_t buf[MAX_PATH * 2] = {};
			const DWORD n = ::GetModuleFileNameW(a_m, buf, static_cast<DWORD>(std::size(buf)));
			return std::wstring(buf, n);
		}

		// A module's ProductName from its version resource ("" when it has none)
		std::wstring ProductName(const std::wstring& a_path)
		{
			DWORD handle = 0;
			const DWORD size = ::GetFileVersionInfoSizeW(a_path.c_str(), &handle);
			if (size == 0) { return std::wstring(); }
			std::vector<BYTE> data(size);
			if (!::GetFileVersionInfoW(a_path.c_str(), 0, size, data.data())) { return std::wstring(); }
			struct Lang { WORD lang, cp; } *langs = nullptr;
			UINT len = 0;
			if (!::VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&langs), &len) || len < sizeof(Lang))
			{
				return std::wstring();
			}
			wchar_t key[64];
			swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\ProductName", langs[0].lang, langs[0].cp);
			wchar_t* value = nullptr;
			if (!::VerQueryValueW(data.data(), key, reinterpret_cast<void**>(&value), &len) || !value || len == 0) { return std::wstring(); }
			return std::wstring(value);
		}

		// OptiScaler loads as a proxy with whatever name the player gave it (dxgi.dll, winmm.dll, version.dll ...): the
		// loaded module whose version resource says ProductName "OptiScaler".
		HMODULE FindOptiScaler()
		{
			HMODULE mods[1024];
			DWORD needed = 0;
			if (!::EnumProcessModules(::GetCurrentProcess(), mods, sizeof(mods), &needed)) { return nullptr; }
			const DWORD count = std::min<DWORD>(needed / sizeof(HMODULE), static_cast<DWORD>(std::size(mods)));
			for (DWORD i = 0; i < count; ++i)
			{
				const std::wstring path = ModulePath(mods[i]);
				if (path.empty()) { continue; }
				std::wstring product = ProductName(path);
				for (auto& c : product) { c = static_cast<wchar_t>(::towlower(c)); }
				if (product == L"optiscaler") { return mods[i]; }
			}
			return nullptr;
		}

		// Re-reads each loaded mod's key from its config (cheap; done when the page opens and before every press, since a
		// mod's own menu can change its key while the game runs). Caller holds g_lock.
		void RefreshKeys()
		{
			{
				Target& t = g_targets[kOar];
				if (t.loaded)
				{
					const auto c = hostcore::ParseOarIni(hostcore::ReadFileText(L"Data/SKSE/Plugins/OpenAnimationReplacer.ini"));
					t.key = c.key;
					t.keyFrom = "OpenAnimationReplacer.ini";
					// v6: what OAR really has this session wins over what its INI says now (it reads the INI only at start-up)
					OAR_API::UI::IUIInterface2* ui = g_oarV3 ? static_cast<OAR_API::UI::IUIInterface2*>(g_oarV3) : g_oarV2;
					int session = -1;
					if (ui && g_framesSeen.load(std::memory_order_relaxed) > kOarUiSettledFrames) { session = ui->GetImGuiContext() ? 1 : 0; }
					const char* const offInIni = TR("AMF_ImguiOarUiOff", "Its menu is switched off in OpenAnimationReplacer.ini "
						"(bEnableUI = false), so there is nothing to open.");
					if (session == 1) { t.blocked.clear(); }
					else if (session == 0)
					{
						t.blocked = c.enableUI ? std::string(TR("AMF_ImguiOarUiOffAtStart", "Its menu was switched off in "
							"OpenAnimationReplacer.ini (bEnableUI = false) when the game started, so it has none this session. It "
							"comes back after the game is restarted."))
											   : std::string(offInIni);
					}
					else { t.blocked = c.enableUI ? std::string() : std::string(offInIni); }
					if (session != g_oarUiLogged)
					{
						g_oarUiLogged = session;
						logger::info("imgui: Open Animation Replacer's menu this session: {} (its INI now says bEnableUI = {})",
									 session == 1 ? "built" : (session == 0 ? "NOT built - it has no ImGui context" : "not known yet"),
									 c.enableUI ? "true" : "false");
					}
				}
			}
			{
				Target& t = g_targets[kIed];
				if (t.loaded)
				{
					const auto c = hostcore::ParseIedConfig(hostcore::ReadFileText(L"Data/SKSE/Plugins/ImmersiveEquipmentDisplays.ini"),
															hostcore::ReadFileText(L"Data/SKSE/Plugins/IED/Settings.json"));
					t.key = c.key;
					t.keyFrom = c.source == "ui" ? "IED\\Settings.json" : (c.source == "ini" ? "ImmersiveEquipmentDisplays.ini" : "IED's default");
					t.keyOpensOnly = true;
					t.blocked = c.enabled ? std::string() : std::string(TR("AMF_ImguiIedUiOff", "Its menu is switched off in "
						"ImmersiveEquipmentDisplays.ini ([GUI] Enabled = false)."));
				}
			}
			{
				Target& t = g_targets[kCs];
				if (t.loaded)
				{
					std::string from = "SettingsUser.json";
					hostcore::KeyCombo k = hostcore::ParseCsSettings(hostcore::ReadFileText(L"Data/SKSE/Plugins/CommunityShaders/SettingsUser.json"));
					if (k.Empty())
					{
						from = "SettingsDefault.json";
						k = hostcore::ParseCsSettings(hostcore::ReadFileText(L"Data/SKSE/Plugins/CommunityShaders/SettingsDefault.json"));
					}
					if (k.Empty())
					{
						from = "Community Shaders' default";
						k.keys = { hostcore::VkToDik(VK_END) };
					}
					t.key = k;
					t.keyFrom = from;
					t.blocked.clear();
				}
			}
			{
				Target& t = g_targets[kOpti];
				if (t.loaded)
				{
					std::filesystem::path ini = std::filesystem::path(t.modulePath).parent_path() / L"OptiScaler.ini";
					std::error_code ec;
					if (!std::filesystem::exists(ini, ec)) { ini = L"OptiScaler.ini"; }
					const auto c = hostcore::ParseOptiScalerIni(hostcore::ReadFileText(ini.wstring()));
					t.key = c.key;
					t.keyFrom = "OptiScaler.ini";
					t.blocked = c.hasKey ? std::string() : std::string(TR("AMF_ImguiNoKey", "It has no key to open its menu "
						"(ShortcutKey = -1 in OptiScaler.ini), so it cannot be opened from here."));
				}
			}
		}

		bool GameIsForeground()
		{
			const HWND game = static_cast<HWND>(renderer::GetGameWindow());
			return game && ::GetForegroundWindow() == game;
		}

		// sends one step of the press; false when SendInput did not take every record
		bool SendStep(Press& a_p, hostcore::Step a_step)
		{
			std::vector<INPUT> in = hostcore::StepInputs(a_p.combo, a_step);
			if (in.empty()) { return true; }
			const UINT sent = ::SendInput(static_cast<UINT>(in.size()), in.data(), sizeof(INPUT));
			a_p.sent += (a_p.sent.empty() ? "" : ", ") + std::to_string(sent) + "/" + std::to_string(in.size());
			if (sent != in.size())
			{
				logger::warn("imgui: {} - SendInput sent {} of {} key records (error {})", kNames[a_p.target], sent, in.size(), ::GetLastError());
				return false;
			}
			return true;
		}

		void FinishPress(Press& a_p, const std::string& a_outcome)
		{
			std::scoped_lock l(g_lock);
			Target& t = g_targets[a_p.target];
			t.lastOutcome = a_outcome;
			logger::info("imgui: {} - {} ({}; SendInput {})", kNames[a_p.target], a_outcome, hostcore::ComboName(a_p.combo),
						 a_p.sent.empty() ? "nothing sent" : a_p.sent);
		}

		void StepPress()
		{
			if (!g_press) { return; }
			Press& p = *g_press;
			const int due = p.startFrame + (p.nextStep == 0 ? 0 : p.nextStep == 1 ? 1 : p.nextStep == 2 ? 3 : 4);
			if (g_frame < due) { return; }
			switch (p.nextStep)
			{
			case 0:
				if (!GameIsForeground())
				{
					FinishPress(p, "not pressed: the game is not the foreground window");
					g_press.reset();
					return;
				}
				p.modsDown = SendStep(p, hostcore::Step::kModifiersDown) && p.combo.keys.size() > 1;
				p.nextStep = 1;
				return;
			case 1:
				if (!GameIsForeground())
				{
					if (p.modsDown) { SendStep(p, hostcore::Step::kModifiersUp); }
					FinishPress(p, "not pressed: the game lost the foreground");
					g_press.reset();
					return;
				}
				p.keyDown = SendStep(p, hostcore::Step::kKeyDown);
				p.nextStep = 2;
				return;
			case 2:
				// a key that went down always comes up again, foreground or not, so nothing is left held
				if (p.keyDown) { SendStep(p, hostcore::Step::kKeyUp); }
				p.nextStep = 3;
				return;
			default:
				if (p.combo.keys.size() > 1) { SendStep(p, hostcore::Step::kModifiersUp); }
				FinishPress(p, p.keyDown ? "pressed its key" : "the key press failed");
				g_press.reset();
				return;
			}
		}

		// The hosted page ends: close OAR when asked (and when it is open) - its API's CloseMenu, else Escape (OAR closes its
		// menu on Escape whatever its own key is).
		void EndHosting(bool a_closeTarget, const std::string& a_why)
		{
			if (!g_host.active) { return; }
			g_host.active = false;
			{
				std::scoped_lock l(g_bridgeLock);   // DevBench reads it from its own thread
				g_host.lastEnd = a_why;
			}
			g_osPolling = false;
			const int state = OarState();
			logger::info("imgui: hosted Open Animation Replacer ended - {}{} (AMF's input hook got {} event(s) while it was open; {} press(es) taken from the OS)",
						 a_why, a_closeTarget && state == 1 ? "; closing its menu" : "", g_host.eventsWhileOpen, g_host.osPresses);
			if (!a_closeTarget || state != 1) { return; }
			if (g_oarV3) { g_apiCall = ApiCall{ kOar, false, g_frame }; return; }
			if (g_press) { return; }
			Press p;
			p.target = kOar;
			p.combo.keys = { 0x01 };   // Escape
			p.startFrame = g_frame;
			g_press = p;
		}

		std::string StartHosting(const hostcore::KeyCombo& a_combo, bool a_pressKey = true)
		{
			if (g_press && a_pressKey) { return "a key press is still going on"; }
			g_host.active = true;
			g_host.startFrame = g_frame;
			g_host.seenOpen = false;
			g_host.eventsAtOpen = 0;
			g_host.eventsWhileOpen = 0;
			g_host.osPresses = 0;
			g_host.rectFrame = -1;
			++g_host.sessions;
			renderer::SetSelectedNode("othermenus");   // the hosted page is Other menus (a DevBench open may come from another page)
			const int state = OarState();
			logger::info("imgui: hosting Open Animation Replacer in this page (session {}) - its menu is {}", g_host.sessions,
						 state == 1 ? "already open: snapped here" : (g_oarV3 ? "opened through its API" : "opened with its key"));
			if (state == 1 || !a_pressKey) { return std::string(); }
			if (g_oarV3)
			{
				g_apiCall = ApiCall{ kOar, true, g_frame };
				return std::string();
			}
			Press p;
			p.target = kOar;
			p.combo = a_combo;
			p.startFrame = g_frame;
			g_press = p;
			return std::string();
		}

		// What a press of Open / Close does. Render thread. Returns "" when started, else why not.
		std::string Act(int a_id, bool a_open)
		{
			if (a_id < 0 || a_id >= kCount) { return "unknown id"; }
			hostcore::KeyCombo combo;
			{
				std::scoped_lock l(g_lock);
				RefreshKeys();
				Target& t = g_targets[a_id];
				if (!t.loaded) { return "not loaded"; }
				if (!t.blocked.empty()) { return t.blocked; }
				++t.presses;
				combo = t.key;
			}
			// phase 2: OAR opens INSIDE this page when the bridge is there and its switch is on (AMF stays open)
			// (only with this menu up: the page is where OAR goes - with the menu closed, Open is the launcher's)
			if (a_id == kOar && HostAvailable() && (renderer::IsMainWindowVisible() || g_host.active))
			{
				if (a_open) { return g_host.active ? std::string() : StartHosting(combo); }
				if (g_host.active)
				{
					EndHosting(true, "Close pressed");
					return std::string();
				}
			}
			const bool useApi = a_id == kOar && g_oarV3;
			if (!useApi)
			{
				if (combo.Empty()) { return "no key"; }
				if (g_press) { return "a key press is still going on"; }
			}
			// AMF's menu goes first, so the mod's menu has the mouse, the keys and the screen - and nothing of AMF is behind it
			// (the owner, 2026-10-12: "some of these menus need to have nothing behind them so that you can see the world
			// clearly"). Only here, on the paths where the mod's menu opens ON ITS OWN; a hosted menu keeps AMF open.
			// Close first, then the key or the API call two frames later: with AMF closed its hook passes every key on.
			const bool closeAmf = a_open && renderer::IsMainWindowVisible() && settings::Get().closeOnOpen;
			if (closeAmf)
			{
				logger::info("imgui: {} - closing this menu first, so its menu gets the input", kNames[a_id]);
				renderer::SetMenuVisible(false);
			}
			const int start = g_frame + (closeAmf ? 2 : 0);
			if (useApi)
			{
				g_apiCall = ApiCall{ a_id, a_open, start };
				if (a_open && closeAmf)
				{
					g_reopenAfterOar = true;
					g_oarSeenOpen = false;
					g_reopenDeadline = start + 180;
				}
				return std::string();
			}
			Press p;
			p.target = a_id;
			p.combo = combo;
			p.startFrame = start;
			g_press = p;
			return std::string();
		}

		void RunApiCall()
		{
			if (!g_apiCall || g_frame < g_apiCall->atFrame) { return; }
			const ApiCall c = *g_apiCall;
			g_apiCall.reset();
			if (!g_oarV3) { return; }
			bool ok = false;
			if (c.open) { ok = g_oarV3->OpenMenu(); }
			else { ok = g_oarV3->CloseMenu(); }
			std::scoped_lock l(g_lock);
			g_targets[kOar].lastOutcome = std::string(c.open ? "OpenMenu" : "CloseMenu") + (ok ? " - done" : " - OAR said no");
			logger::info("imgui: Open Animation Replacer - {} through its API: {}", c.open ? "OpenMenu" : "CloseMenu", ok ? "done" : "refused");
		}

		// The bridge's own code runs inside this call; a fault in it must not take the game down with it.
		bool GuardedCall(AMF_HostFrameCallback a_cb, AMF_HostFrame* a_f, void* a_user)
		{
			__try
			{
				a_cb(a_f, a_user);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// Once a frame, after AMF's frame is drawn: hand the bridge the rectangle (or "not hosting") and read back what it saw.
		// Called only from Frame(), which only AMF's present hook calls - the one place a bridge is let at the mod's context.
		void CallBridge()
		{
			AMF_HostFrameCallback cb = nullptr;
			void* user = nullptr;
			{
				std::scoped_lock l(g_bridgeLock);
				if (!g_bridge.registered || g_bridge.failed) { return; }
				cb = g_bridge.callback;
				user = g_bridge.user;
			}
			// v6: the thread the present hook stamped on entry THIS frame. v5's bridge pinned the first thread that called it,
			// and SE 1.5.97 moves its present to another thread in the first second, so hosting switched itself off every launch.
			const std::uint32_t renderThread = consumer::RenderThreadId();
			if (!consumer::OnRenderThread()) { return; }   // not inside the present hook: the bridge is not called at all
			const ImGuiIO& io = ImGui::GetIO();
			AMF_HostFrame f{};
			f.size = sizeof(AMF_HostFrame);
			f.frame = static_cast<std::uint32_t>(g_frame);
			f.hosting = (g_host.active && g_host.rectFrame == ImGui::GetFrameCount() && renderer::IsMainWindowVisible() && g_host.w > 1.0f &&
						 g_host.h > 1.0f) ? 1 : 0;
			f.x = g_host.x;
			f.y = g_host.y;
			f.w = g_host.w;
			f.h = g_host.h;
			f.displayW = io.DisplaySize.x;
			f.displayH = io.DisplaySize.y;
			f.targetOpen = -1;
			f.wantsMouse = 0;
			f.placed = 0;
			f.renderThread = renderThread;
			const bool ok = GuardedCall(cb, &f, user);
			{
				std::scoped_lock l(g_bridgeLock);
				++g_bridge.calls;
				if (!ok)
				{
					g_bridge.failed = true;
					g_bridge.failReason = "the bridge faulted in its frame call";
					logger::error("imgui: AMF-Host-OAR faulted in its frame call - hosting is off for this session");
					return;
				}
				g_bridge.targetOpen = f.targetOpen;
				g_bridge.wantsMouse = f.wantsMouse;
				if (f.placed) { ++g_bridge.placedFrames; }
			}
			// one lock at a time (DevBench's list takes g_lock, then g_bridgeLock)
			if (!g_oarV3)
			{
				std::scoped_lock l2(g_lock);
				g_targets[kOar].state = f.targetOpen;   // OAR 3.0.2 has no IsMenuOpen: the bridge reads it from OAR's context
			}
		}

		void HostingLifecycle()
		{
			if (!g_host.active) { return; }
			const Bridge b = BridgeCopy();
			const int state = g_oarV3 ? (g_oarV3->IsMenuOpen() ? 1 : 0) : b.targetOpen;
			if (b.failed || !b.registered)
			{
				// fail closed: hosting stops; OAR keeps the screen as the launcher would have given it
				const bool open = state == 1;
				EndHosting(false, "the bridge stopped: " + b.failReason);
				if (open && renderer::IsMainWindowVisible() && settings::Get().closeOnOpen)
				{
					logger::info("imgui: falling back to the launcher - this menu closes and Open Animation Replacer keeps the screen");
					renderer::SetMenuVisible(false);
				}
				return;
			}
			if (!renderer::IsMainWindowVisible())
			{
				EndHosting(true, "this menu closed");
				return;
			}
			// the page draws its hosted layout from the frame after the button that started it
			if (g_host.rectFrame != ImGui::GetFrameCount() && g_frame > g_host.startFrame + 1)
			{
				EndHosting(true, "the page was left");
				return;
			}
			if (state == 1)
			{
				if (!g_host.seenOpen)
				{
					g_host.seenOpen = true;
					g_host.eventsAtOpen = input::EventsSeen();
					logger::info("imgui: Open Animation Replacer's menu is open in the page - this menu now reads the pointer, the mouse "
								 "buttons and its key from the OS");
				}
				g_host.eventsWhileOpen = input::EventsSeen() - g_host.eventsAtOpen;
				return;
			}
			if (g_host.seenOpen)
			{
				EndHosting(false, "Open Animation Replacer closed its menu");
				return;
			}
			if (g_frame > g_host.startFrame + 240)
			{
				EndHosting(false, "Open Animation Replacer's menu did not open within 4 s");
			}
		}

		// Is every key of the combo held now (the OS's view; DIK -> virtual key through the layout)?
		bool ComboDown(const hostcore::KeyCombo& a_c)
		{
			if (a_c.Empty()) { return false; }
			for (std::uint32_t dik : a_c.keys)
			{
				const UINT sc = (dik & 0x7F) | ((dik & 0x80) ? 0xE000u : 0u);
				const UINT vk = ::MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX);
				if (vk == 0 || (::GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) == 0) { return false; }
			}
			return true;
		}

		// THE MOD'S OWN KEY WHILE AMF IS OPEN (the owner, 2026-10-12). The mod's own input hook runs before AMF's and opens its
		// menu by itself; AMF follows it: a hosted mod is snapped into the page (AMF stays open), one that opens on its own
		// closes AMF (with the close switch on), so it is shown alone. Read from the OS on the frame, edge only; never while a
		// press of AMF's own is going on, while a text field of AMF has the keyboard, or with the game in the background.
		bool g_keyHeld[kCount] = {};
		void WatchOwnKeys()
		{
			std::array<hostcore::KeyCombo, kCount> keys;
			std::array<bool, kCount> usable{};
			{
				std::scoped_lock l(g_lock);
				for (int i = 0; i < kCount; ++i)
				{
					keys[i] = g_targets[i].key;
					usable[i] = g_targets[i].loaded && g_targets[i].blocked.empty() && SwitchOn(i);
				}
			}
			const bool watch = renderer::IsMainWindowVisible() && !g_press && !renderer::WantsTextInput() && GameIsForeground();
			for (int i = 0; i < kCount; ++i)
			{
				const bool down = usable[i] && ComboDown(keys[i]);
				const bool edge = down && !g_keyHeld[i];
				g_keyHeld[i] = down;
				if (!edge || !watch) { continue; }
				if (i == kOar && HostAvailable())
				{
					if (!g_host.active)
					{
						logger::info("imgui: Open Animation Replacer's own key with this menu open - its menu is shown inside the page");
						StartHosting(keys[i], false);
					}
					continue;
				}
				if (settings::Get().closeOnOpen)
				{
					logger::info("imgui: {}'s own key ({}) with this menu open - this menu closes so its menu is shown alone", kNames[i],
								 hostcore::ComboName(keys[i]));
					renderer::SetMenuVisible(false);
				}
			}
		}

		const char* StateText(int a_state)
		{
			if (a_state == 1) { return TR("AMF_ImguiStateOpen", "Open"); }
			if (a_state == 0) { return TR("AMF_ImguiStateClosed", "Closed"); }
			return TR("AMF_ImguiStateUnknown", "Not known - the mod has no way to say");
		}

		std::string J(const std::string& a_s) { return nlohmann::json(a_s).dump(-1, ' ', false, nlohmann::json::error_handler_t::replace); }
	}

	void Load()
	{
		std::scoped_lock l(g_lock);
		if (g_loaded) { return; }
		g_loaded = true;
		const auto find = [](int a_id, const wchar_t* a_dll) {
			if (HMODULE m = ::GetModuleHandleW(a_dll))
			{
				g_targets[a_id].loaded = true;
				g_targets[a_id].modulePath = ModulePath(m);
			}
		};
		find(kOar, L"OpenAnimationReplacer.dll");
		find(kCs, L"CommunityShaders.dll");
		find(kIed, L"ImmersiveEquipmentDisplays.dll");
		if (HMODULE m = FindOptiScaler())
		{
			g_targets[kOpti].loaded = true;
			g_targets[kOpti].modulePath = ModulePath(m);
		}
		if (g_targets[kOar].loaded)
		{
			using Request = OAR_API::UI::IUIInterface* (*)(OAR_API::UI::InterfaceVersion, const char*, REL::Version);
			HMODULE oar = ::GetModuleHandleW(L"OpenAnimationReplacer.dll");
			const auto request = oar ? reinterpret_cast<Request>(::GetProcAddress(oar, "RequestPluginAPI_UI")) : nullptr;
			if (request)
			{
				// OAR 3.0.2 knows only V2 and answers V3 with nullptr (and a warning in its own log)
				g_oarV3 = request(OAR_API::UI::InterfaceVersion::V3, "ApocryphaMenuFramework", REL::Version(2, 2, 0, 0));
				if (!g_oarV3)
				{
					g_oarV2 = static_cast<OAR_API::UI::IUIInterface2*>(
						request(OAR_API::UI::InterfaceVersion::V2, "ApocryphaMenuFramework", REL::Version(2, 2, 0, 0)));
				}
			}
			logger::info("imgui: Open Animation Replacer - {}", !request ? "no RequestPluginAPI_UI export: opened with its key" :
						 g_oarV3 ? "UI interface V3: opened and closed through its API" :
								   "no UI interface V3 (OAR before 3.2.1): opened with its key");
		}
		RefreshKeys();
		for (int i = 0; i < kCount; ++i)
		{
			const Target& t = g_targets[i];
			if (!t.loaded) { logger::debug("imgui: {} not loaded", kNames[i]); continue; }
			logger::info("imgui: {} loaded ({}) - key {} from {}{}{}", kNames[i], Narrow(t.modulePath), hostcore::ComboName(t.key), t.keyFrom,
						 t.blocked.empty() ? "" : " - cannot be opened: ", t.blocked);
		}
	}

	bool AnyShown()
	{
		std::scoped_lock l(g_lock);
		for (int i = 0; i < kCount; ++i)
		{
			if (g_targets[i].loaded && SwitchOn(i)) { return true; }
		}
		return false;
	}

	void DrawPage()
	{
		if (ImGui::GetFrameCount() > g_pageDrawnFrame + 1)
		{
			std::scoped_lock l(g_lock);
			RefreshKeys();   // the page was just opened: a mod may have changed its key since
		}
		g_pageDrawnFrame = ImGui::GetFrameCount();

		ImGui::TextUnformatted(TR("AMF_OtherMenus", "Other menus"));
		ImGui::Separator();
		ImGui::Spacing();
		ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(TR("AMF_OtherMenusHelp2", "These mods draw menus of their own. Open one from here. A menu shown inside "
			"this menu keeps it open; one that opens on its own closes this menu first, so nothing of it is behind (Settings > "
			"Converted menus > ImGui menus). Close a mod's menu the way the mod does - its key, or Escape."));
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
		ImGui::Spacing();

		// phase 2: the HOSTED PAGE - AMF's frame and an empty rectangle that OAR's own window is moved into each frame
		if (g_host.active)
		{
			ImGui::TextUnformatted(kNames[kOar]);
			ImGui::SameLine();
			ImGui::TextDisabled("%s", TR("AMF_HostLive", "- live in this page"));
			ImGui::SameLine();
			bool close = ImGui::Button(TR("AMF_ImguiClose", "Close"));
			renderer::NoteHoveredItem();
			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(TR("AMF_HostHelp", "Open Animation Replacer draws its own menu here, with its own look and controls; "
				"it does not take a controller. Escape or Close closes it, and this menu's key closes both."));
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			ImGui::BeginChild("##amf_hosted", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
			g_host.x = pos.x;
			g_host.y = pos.y;
			g_host.w = size.x;
			g_host.h = size.y;
			g_host.rectFrame = ImGui::GetFrameCount();
			if (OarState() != 1)
			{
				ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, ImGui::GetStyle().WindowPadding.y));
				ImGui::TextDisabled("%s", TR("AMF_HostOpening", "Opening Open Animation Replacer..."));
			}
			ImGui::EndChild();
			if (close) { EndHosting(true, "Close pressed"); }
			return;
		}

		std::array<Target, kCount> targets;
		{
			std::scoped_lock l(g_lock);
			targets = g_targets;
		}
		int action = -1;
		bool open = true;
		for (int i = 0; i < kCount; ++i)
		{
			const Target& t = targets[i];
			if (!t.loaded || !SwitchOn(i)) { continue; }
			ImGui::PushID(i);
			ImGui::Spacing();
			ImGui::TextUnformatted(kNames[i]);
			ImGui::SameLine();
			ImGui::TextDisabled(TR("AMF_ImguiState", "- %s"), StateText(t.state));
			const std::string keyName = hostcore::ComboName(t.key);
			ImGui::BeginDisabled(!t.blocked.empty());
			if (i == kOar && (g_oarV3 || HostAvailable()))
			{
				if (ImGui::Button(TR("AMF_ImguiOpen", "Open"))) { action = i; open = true; }
				renderer::NoteHoveredItem();
				ImGui::SameLine();
				if (ImGui::Button(TR("AMF_ImguiClose", "Close"))) { action = i; open = false; }
				renderer::NoteHoveredItem();
			}
			else
			{
				char label[160];
				std::snprintf(label, sizeof(label), t.keyOpensOnly ? TR("AMF_ImguiOpenKey", "Open (%s)") : TR("AMF_ImguiToggleKey", "Open / close (%s)"),
							  keyName.c_str());
				const std::string id = std::string(label) + "###press";   // the key in the label may change: keep one ID
				if (ImGui::Button(id.c_str())) { action = i; open = true; }
				renderer::NoteHoveredItem();
			}
			ImGui::EndDisabled();
			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			ImGui::PushTextWrapPos(0.0f);
			if (!t.blocked.empty()) { ImGui::TextUnformatted(t.blocked.c_str()); }
			else if (i == kOar && HostAvailable()) { ImGui::TextUnformatted(TR("AMF_HostRowNote", "Opens inside this page, live (AMF-Host-OAR).")); }
			else if (i == kOar && g_oarV3) { ImGui::TextUnformatted(TR("AMF_ImguiByApi", "Opened and closed through its own interface.")); }
			else
			{
				ImGui::Text(TR("AMF_ImguiKeyFrom", "Pressed for you: %s, the key it reads from %s."), keyName.c_str(), t.keyFrom.c_str());
				if (t.keyOpensOnly) { ImGui::TextUnformatted(TR("AMF_ImguiEscCloses", "Its key only opens it; Escape closes it.")); }
			}
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			ImGui::PopID();
		}
		if (action >= 0)
		{
			const std::string why = Act(action, open);
			if (!why.empty()) { logger::info("imgui: {} - not {}: {}", kNames[action], open ? "opened" : "closed", why); }
		}
	}

	void DrawShowInside();

	void DrawSettings()
	{
		static int s_settingsDrawnFrame = -100;
		if (ImGui::GetFrameCount() > s_settingsDrawnFrame + 1)
		{
			std::scoped_lock l(g_lock);
			RefreshKeys();   // the tab was just opened: OAR's menu-off reason below is read fresh (v6)
		}
		s_settingsDrawnFrame = ImGui::GetFrameCount();
		ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(TR("AMF_ImguiSettingsHelp", "Mods that draw their own ImGui menus get a row on Framework > Other menus, "
			"to open them from here. Each switch shows or hides one mod's row; a mod that is not loaded has no row. Nothing of "
			"these mods is changed."));
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
		ImGui::Spacing();
		std::array<bool, kCount> loaded{};
		{
			std::scoped_lock l(g_lock);
			for (int i = 0; i < kCount; ++i) { loaded[i] = g_targets[i].loaded; }
		}
		for (int i = 0; i < kCount; ++i)
		{
			bool* value = SwitchPtr(i);
			if (!value) { continue; }
			if (widgets::Toggle(kNames[i], value))
			{
				logger::info("settings page: {} row on Other menus -> {}", kNames[i], *value);
				settings::Save();
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(%s)", loaded[i] ? TR("AMF_ImguiLoaded", "loaded") : TR("AMF_ImguiNotLoaded", "not loaded"));
			if (i == kOar) { DrawShowInside(); }
		}
		ImGui::Spacing();
		auto& v = settings::Get();
		if (widgets::Toggle(TR("AMF_ImguiCloseSwitch", "Close AMF when opening another menu"), &v.closeOnOpen))
		{
			logger::info("settings page: close this menu when another mod's menu opens on its own -> {}", v.closeOnOpen);
			settings::Save();
		}
		ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(TR("AMF_ImguiCloseHelp", "On: when a mod's menu opens on its own - from Open on Other menus, or with "
			"that mod's own key while this menu is open - this menu closes, so nothing of it is behind and the world shows clearly. "
			"It never closes for a menu shown inside it. Off: this menu stays open behind."));
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
	}

	// "Show inside AMF" for Open Animation Replacer: on = hosted in the page (AMF stays open), off = it opens on its own. Off
	// by itself, greyed, while the bridge is not there or did not validate.
	void DrawShowInside()
	{
		auto& v = settings::Get();
		const Bridge b = BridgeCopy();
		const bool ready = b.registered && !b.failed;
		ImGui::Indent();
		bool off = false;
		ImGui::BeginDisabled(!ready);
		if (widgets::Toggle(TR("AMF_HostInside", "Show inside AMF"), ready ? &v.hostOar : &off) && ready)
		{
			logger::info("settings page: Open Animation Replacer shown inside AMF -> {}", v.hostOar);
			if (!v.hostOar && g_host.active) { EndHosting(true, "Show inside AMF switched off"); }
			settings::Save();
		}
		ImGui::EndDisabled();
		ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(TR("AMF_HostOarHelp", "On: its Open on Other menus keeps this menu open and shows Open Animation "
			"Replacer's own menu inside the page, live. Needs AMF-Host-OAR.dll (in this mod's package) and a version of Open "
			"Animation Replacer it knows; otherwise it opens over a closed menu, as the others do."));
		std::string oarBlocked;
		{
			std::scoped_lock l(g_lock);
			oarBlocked = g_targets[kOar].blocked;
		}
		if (!oarBlocked.empty()) { ImGui::TextUnformatted(oarBlocked.c_str()); }   // v6: OAR's own INI switched its menu off
		else if (!b.registered) { ImGui::TextUnformatted(TR("AMF_HostNoBridge", "AMF-Host-OAR.dll is not installed or not loaded.")); }
		else if (b.failed) { ImGui::Text(TR("AMF_HostStopped", "Hosting is off for this session: %s"), b.failReason.c_str()); }
		else { ImGui::TextUnformatted(TR("AMF_HostReady", "AMF-Host-OAR.dll is loaded and ready.")); }
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
		ImGui::Unindent();
	}

	void Frame()
	{
		++g_frame;
		g_framesSeen.store(g_frame, std::memory_order_relaxed);
		RunApiCall();
		StepPress();
		CallBridge();
		HostingLifecycle();
		WatchOwnKeys();
		if (g_oarV3)
		{
			const bool open = g_oarV3->IsMenuOpen();
			{
				std::scoped_lock l(g_lock);
				g_targets[kOar].state = open ? 1 : 0;
			}
			// the launcher closed this menu for OAR: open it again on this page once OAR's menu has come and gone
			if (g_reopenAfterOar)
			{
				if (open) { g_oarSeenOpen = true; }
				else if (g_oarSeenOpen)
				{
					g_reopenAfterOar = false;
					logger::info("imgui: Open Animation Replacer's menu closed - this menu opens again on Other menus");
					renderer::SetSelectedNode("othermenus");
					renderer::SetMenuVisible(true);
				}
				else if (g_frame > g_reopenDeadline)
				{
					g_reopenAfterOar = false;   // it never opened: nothing to come back from
					logger::info("imgui: Open Animation Replacer's menu did not open within 3 s of OpenMenu");
				}
			}
		}
	}

	std::string ToolJson(const std::string& a_argsJson)
	{
		const nlohmann::json args = nlohmann::json::parse(a_argsJson.empty() ? std::string("{}") : a_argsJson, nullptr, false);
		auto str = [&](const char* a_k) { return args.is_object() && args.contains(a_k) && args[a_k].is_string() ? args[a_k].get<std::string>() : std::string(); };
		const std::string what = str("do").empty() ? "list" : str("do");
		if (what == "list")
		{
			std::scoped_lock l(g_lock);
			RefreshKeys();
			std::string out;
			for (int i = 0; i < kCount; ++i)
			{
				const Target& t = g_targets[i];
				std::string keys;
				for (std::uint32_t k : t.key.keys) { keys += (keys.empty() ? "" : ",") + std::to_string(k); }
				out += (out.empty() ? "" : ",") + std::string("{\"id\":\"") + kIds[i] + "\",\"name\":" + J(kNames[i]) + ",\"loaded\":" +
					   (t.loaded ? "true" : "false") + ",\"shown\":" + (t.loaded && SwitchOn(i) ? "true" : "false") + ",\"module\":" +
					   J(Narrow(t.modulePath)) + ",\"method\":\"" + (i == kOar && g_oarV3 ? "api" : "key") + "\",\"key\":" +
					   J(hostcore::ComboName(t.key)) + ",\"dik\":[" + keys + "],\"keyFrom\":" + J(t.keyFrom) + ",\"keyOpensOnly\":" +
					   (t.keyOpensOnly ? "true" : "false") + ",\"blocked\":" + J(t.blocked) + ",\"state\":" + std::to_string(t.state) +
					   ",\"presses\":" + std::to_string(t.presses) + ",\"lastOutcome\":" + J(t.lastOutcome) + "}";
			}
			const Bridge b = BridgeCopy();
			std::string lastEnd;
			{
				std::scoped_lock l2(g_bridgeLock);
				lastEnd = g_host.lastEnd;
			}
			const std::string hosting = std::string("{\"bridge\":") + (b.registered ? "true" : "false") + ",\"bridgeFailed\":" +
										(b.failed ? "true" : "false") + ",\"failReason\":" + J(b.failReason) + ",\"switch\":" +
										(settings::Get().hostOar ? "true" : "false") + ",\"active\":" + (g_host.active ? "true" : "false") +
										",\"targetOpen\":" + std::to_string(b.targetOpen) + ",\"wantsMouse\":" + std::to_string(b.wantsMouse) +
										",\"placedFrames\":" + std::to_string(b.placedFrames) + ",\"calls\":" + std::to_string(b.calls) +
										",\"rect\":[" + std::to_string(static_cast<int>(g_host.x)) + "," + std::to_string(static_cast<int>(g_host.y)) + "," +
										std::to_string(static_cast<int>(g_host.w)) + "," + std::to_string(static_cast<int>(g_host.h)) + "]" +
										",\"osPolling\":" + (g_osPolling ? "true" : "false") +
										",\"amfEventsWhileOpen\":" + std::to_string(g_host.eventsWhileOpen) + ",\"osPresses\":" +
										std::to_string(g_host.osPresses) + ",\"sessions\":" + std::to_string(g_host.sessions) + ",\"lastEnd\":" +
										J(lastEnd) + "}";
			return std::string("{\"ok\":true,\"op\":\"imgui\",\"do\":\"list\",\"oarApiV3\":") + (g_oarV3 ? "true" : "false") +
				   ",\"targets\":[" + out + "],\"hosting\":" + hosting + "}";
		}
		if (what == "open" || what == "close")
		{
			const std::string id = str("id");
			int idx = -1;
			for (int i = 0; i < kCount; ++i)
			{
				if (id == kIds[i]) { idx = i; }
			}
			if (idx < 0) { return "{\"ok\":false,\"op\":\"imgui\",\"error\":\"id = oar | cs | ied | optiscaler\"}"; }
			auto why = std::make_shared<std::string>();
			auto done = std::make_shared<std::atomic<bool>>(false);
			const bool open = what == "open";
			const bool ran = renderer::RunOnRenderThreadWait([idx, open, why, done]() {
				*why = Act(idx, open);
				done->store(true);
			}, 5000);
			if (!ran || !done->load())
			{
				return "{\"ok\":false,\"op\":\"imgui\",\"error\":\"no frame took the call within 5 s (is the game drawing?)\"}";
			}
			return std::string("{\"ok\":") + (why->empty() ? "true" : "false") + ",\"op\":\"imgui\",\"do\":\"" + what + "\",\"id\":\"" + kIds[idx] +
				   "\"" + (why->empty() ? std::string(",\"note\":\"started - the press runs over the next frames; read do=list\"") : ",\"error\":" + J(*why)) + "}";
		}
		return "{\"ok\":false,\"op\":\"imgui\",\"error\":\"do = list | open {id} | close {id}\"}";
	}

	bool PollOsInput()
	{
		const bool active = g_host.active && g_host.seenOpen && BridgeCopy().targetOpen != 0;
		if (!active)
		{
			if (g_osPolling)
			{
				// whatever AMF still holds from the OS is let go, so nothing stays down when the game's input comes back
				for (int b = 0; b < 3; ++b)
				{
					if (g_osDown[b]) { input::QueueMouseButton(static_cast<std::uint32_t>(b), false); }
					g_osDown[b] = false;
				}
				g_osPolling = false;
			}
			g_osToggleDown = false;
			return false;
		}
		g_osPolling = true;
		const HWND game = static_cast<HWND>(renderer::GetGameWindow());
		if (!game || ::GetForegroundWindow() != game) { return true; }
		POINT pt{};
		RECT rc{};
		if (::GetCursorPos(&pt) && ::ScreenToClient(game, &pt) && ::GetClientRect(game, &rc) && rc.right > 0 && rc.bottom > 0)
		{
			// the client area onto the back buffer AMF draws in (they differ when the game renders smaller than its window)
			const ImGuiIO& io = ImGui::GetIO();
			input::SetCursorAbsolute(static_cast<float>(pt.x) * io.DisplaySize.x / static_cast<float>(rc.right),
									 static_cast<float>(pt.y) * io.DisplaySize.y / static_cast<float>(rc.bottom));
		}
		const int wants = BridgeCopy().wantsMouse;
		constexpr int kVk[3] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON };
		for (int b = 0; b < 3; ++b)
		{
			const bool down = (::GetAsyncKeyState(kVk[b]) & 0x8000) != 0;
			if (down == g_osDown[b]) { continue; }
			if (down && wants) { continue; }   // a press OAR's window or popup is under: OAR's alone
			g_osDown[b] = down;
			input::QueueMouseButton(static_cast<std::uint32_t>(b), down);
			if (down) { ++g_host.osPresses; }
		}
		// AMF's own menu key, which never reaches AMF's hook while OAR's menu is open
		const std::int32_t dik = bindings::ToggleKeyboardCode();
		if (dik > 0)
		{
			const UINT sc = static_cast<UINT>(dik & 0x7F) | ((dik & 0x80) ? 0xE000u : 0u);
			const UINT vk = ::MapVirtualKeyW(sc, MAPVK_VSC_TO_VK_EX);
			const bool down = vk != 0 && (::GetAsyncKeyState(static_cast<int>(vk)) & 0x8000) != 0;
			if (down && !g_osToggleDown)
			{
				logger::info("imgui: this menu's key, read from the OS while Open Animation Replacer is hosted - closing both");
				renderer::SetMenuVisible(false);
			}
			g_osToggleDown = down;
		}
		return true;
	}

	bool RegisterBridge(const char* a_target, std::uint32_t a_api, AMF_HostFrameCallback a_callback, void* a_user)
	{
		const std::string target = a_target ? a_target : "";
		std::scoped_lock l(g_bridgeLock);
		if (target != "oar" || a_api != AMF_HOST_BRIDGE_API || !a_callback || g_bridge.registered)
		{
			logger::warn("imgui: a host bridge for \"{}\" (api {}) was refused - {}", target, a_api,
						 target != "oar" ? "only \"oar\" is hosted" : a_api != AMF_HOST_BRIDGE_API ? "unknown bridge api" :
						 !a_callback ? "no callback" : "one is registered already");
			return false;
		}
		g_bridge.registered = true;
		g_bridge.api = a_api;
		g_bridge.callback = a_callback;
		g_bridge.user = a_user;
		logger::info("imgui: host bridge for Open Animation Replacer registered (api {})", a_api);
		return true;
	}

	void BridgeFailed(const char* a_target, const char* a_reason)
	{
		const std::string reason = a_reason ? a_reason : "(no reason given)";
		std::scoped_lock l(g_bridgeLock);
		if (!a_target || std::string(a_target) != "oar") { return; }
		if (!g_bridge.failed)
		{
			g_bridge.failed = true;
			g_bridge.failReason = reason;
			logger::warn("imgui: the host bridge for Open Animation Replacer stopped - {}; hosting is off for this session and Open "
						 "falls back to the launcher", reason);
		}
	}
}
