#include "ImguiHost.h"

#include "HostCore.h"
#include "LoadGuard.h"
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
			kEnb,
			kCount
		};

		constexpr std::array<const char*, kCount> kIds = { "oar", "cs", "ied", "optiscaler", "enb" };
		// the mods' own names - proper names, the same in every language
		constexpr std::array<const char*, kCount> kNames = { "Open Animation Replacer", "Community Shaders", "Immersive Equipment Displays",
															 "OptiScaler", "ENB" };

		struct Target
		{
			bool loaded = false;
			std::wstring modulePath;     // the loaded DLL (OptiScaler: whatever name its proxy has; ENB: its d3d11.dll / dxgi.dll)
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
		std::wstring g_enbIni;   // ENB: enblocal.ini beside SkyrimSE.exe

		// OAR's UI interface: V3 (3.2.1+) when it answers it, else none here (3.0.2 is opened with its key)
		OAR_API::UI::IUIInterface3* g_oarV3 = nullptr;
		// v6: OAR's V2 interface (every OAR from 3.0.2), asked only whether OAR built an ImGui context this session - a null
		// check, nothing of the context is read. OAR reads bEnableUI once at start-up and with it off builds no menu at all;
		// with it on its context exists from the first frame. So after the first frames a null context means "no menu this
		// session", whatever the INI says now.
		OAR_API::UI::IUIInterface2* g_oarV2 = nullptr;
		std::atomic<int> g_framesSeen{ 0 };   // Frame() calls past the loading guard so far (read by RefreshKeys on any thread)
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

		bool SwitchOn(int a_id)
		{
			const auto& v = settings::Get();
			switch (a_id)
			{
			case kOar: return v.imguiOar;
			case kCs: return v.imguiCs;
			case kIed: return v.imguiIed;
			case kOpti: return v.imguiOpti;
			case kEnb: return v.imguiEnb;
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
			case kEnb: return &v.imguiEnb;
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

		// One string of a module's version resource ("ProductName", "FileDescription" ...; "" when it has none)
		std::wstring VersionString(const std::wstring& a_path, const wchar_t* a_name)
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
			wchar_t key[96];
			swprintf_s(key, L"\\StringFileInfo\\%04x%04x\\%s", langs[0].lang, langs[0].cp, a_name);
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
				std::wstring product = VersionString(path, L"ProductName");
				for (auto& c : product) { c = static_cast<wchar_t>(::towlower(c)); }
				if (product == L"optiscaler") { return mods[i]; }
			}
			return nullptr;
		}

		// ENB (2.2.0; the owner, 2026-10-12): active when enblocal.ini sits beside SkyrimSE.exe and a d3d11.dll or dxgi.dll is
		// loaded whose file says it is ENBSeries - in its version resource, or as text in the file. FILES ONLY: ENB's licence
		// forbids reverse engineering it, so nothing of it is hooked, patched or read in memory. "" when it is not active.
		std::wstring FindEnb(std::string& a_how)
		{
			const std::filesystem::path game = std::filesystem::path(ModulePath(nullptr)).parent_path();
			std::error_code ec;
			const std::filesystem::path ini = game / L"enblocal.ini";
			if (!std::filesystem::exists(ini, ec)) { return std::wstring(); }
			for (const wchar_t* name : { L"d3d11.dll", L"dxgi.dll" })
			{
				HMODULE m = ::GetModuleHandleW(name);
				if (!m) { continue; }
				const std::wstring path = ModulePath(m);
				std::string version;
				for (const wchar_t* field : { L"ProductName", L"FileDescription", L"CompanyName", L"InternalName", L"LegalCopyright" })
				{
					version += Narrow(VersionString(path, field)) + " ";
				}
				if (hostcore::HasEnbMarker(version)) { a_how = "its version resource"; }
				else if (hostcore::HasEnbMarker(hostcore::ReadFileText(path))) { a_how = "the ENBSeries text in the file"; }
				else { continue; }
				g_enbIni = ini.wstring();
				if (std::filesystem::path(path).parent_path() != game)
				{
					logger::info("imgui: ENB's {} is loaded from {}, not from the game folder", Narrow(name), Narrow(path));
				}
				return path;
			}
			return std::wstring();
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
			{
				Target& t = g_targets[kEnb];
				if (t.loaded)
				{
					const auto c = hostcore::ParseEnbLocal(hostcore::ReadFileText(g_enbIni));
					t.key = c.key;
					t.keyFrom = "enblocal.ini";
					if (!c.editorOn)
					{
						char line[320];
						std::snprintf(line, sizeof(line), TR("AMF_ImguiEnbEditorOff", "Its editor is switched off in enblocal.ini (%s = false), "
							"so there is nothing to open."), c.offKey.c_str());
						t.blocked = line;
					}
					else if (!c.hasKey)
					{
						t.blocked = TR("AMF_ImguiEnbNoKey", "It has no editor key in enblocal.ini ([INPUT] KeyEditor), so it cannot be "
							"opened from here.");
					}
					else { t.blocked.clear(); }
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
				if (loadguard::Blocked() || !GameIsForeground())
				{
					FinishPress(p, loadguard::Blocked() ? "not pressed: the game is loading" : "not pressed: the game is not the foreground window");
					g_press.reset();
					return;
				}
				p.modsDown = SendStep(p, hostcore::Step::kModifiersDown) && p.combo.keys.size() > 1;
				p.nextStep = 1;
				return;
			case 1:
				if (loadguard::Blocked() || !GameIsForeground())
				{
					if (p.modsDown) { SendStep(p, hostcore::Step::kModifiersUp); }
					FinishPress(p, loadguard::Blocked() ? "not pressed: the game started loading" : "not pressed: the game lost the foreground");
					g_press.reset();
					return;
				}
				p.keyDown = SendStep(p, hostcore::Step::kKeyDown);
				p.nextStep = 2;
				return;
			case 2:
				// a key that went down always comes up again, foreground or not, loading or not, so nothing is left held
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

		// What a press of Open / Close does. Render thread. Returns "" when started, else why not.
		std::string Act(int a_id, bool a_open)
		{
			if (a_id < 0 || a_id >= kCount) { return "unknown id"; }
			if (loadguard::Blocked())
			{
				loadguard::NoteIgnored("an Other menus button");
				return "the game is loading or not at its main menu";
			}
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
			const bool useApi = a_id == kOar && g_oarV3;
			if (!useApi)
			{
				if (combo.Empty()) { return "no key"; }
				if (g_press) { return "a key press is still going on"; }
			}
			// AMF's menu always goes first, so the mod's menu has the mouse, the keys and the screen, and nothing of AMF is
			// behind it (the owner, 2026-10-12: "close automatically"). Close first, then the key or the API call two frames
			// later: with AMF closed its input hook passes every key on.
			const bool closeAmf = a_open && renderer::IsMainWindowVisible();
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

		// THE MOD'S OWN KEY WHILE AMF IS OPEN (the owner, 2026-10-12: AMF takes over their hotkeys). The mod opens its menu on
		// its own key by itself; AMF closes, so that menu is shown alone. Read from the OS on the frame, edge only; never while a
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
				logger::info("imgui: {}'s own key ({}) with this menu open - this menu closes so its menu is shown alone", kNames[i],
							 hostcore::ComboName(keys[i]));
				renderer::SetMenuVisible(false);
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
		{
			std::string how;
			const std::wstring enb = FindEnb(how);
			if (!enb.empty())
			{
				g_targets[kEnb].loaded = true;
				g_targets[kEnb].modulePath = enb;
				logger::info("imgui: ENB found - {} (told by {}), enblocal.ini beside SkyrimSE.exe", Narrow(enb), how);
			}
			else { logger::debug("imgui: ENB not active (no enblocal.ini beside SkyrimSE.exe, or no loaded d3d11.dll / dxgi.dll that is ENBSeries)"); }
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
		ImGui::TextUnformatted(TR("AMF_OtherMenusHelp", "These mods draw menus of their own. Open one from here: this menu closes first, "
			"so the mod's menu has the mouse and the keys. Close it the way the mod does - its key, or Escape - and open this menu "
			"again with its key."));
		ImGui::TextUnformatted(TR("AMF_OtherMenusOwnKey", "A mod's own key pressed while this menu is open closes this menu too, so "
			"that mod's menu is shown alone."));
		ImGui::PopTextWrapPos();
		ImGui::PopStyleColor();
		ImGui::Spacing();

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
			// a row that cannot act is inert AND looks it (2.2.0, v6 run: OAR's Open / Close with its UI switched off read as white,
			// like the live buttons): the theme's disabled text colour on top of ImGui's disabled fade, as for an MFM entry that
			// cannot run (MfmPages.cpp)
			const bool rowBlocked = !t.blocked.empty();
			if (rowBlocked) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); }
			ImGui::BeginDisabled(rowBlocked);
			if (i == kOar && g_oarV3)
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
			if (rowBlocked) { ImGui::PopStyleColor(); }
			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			ImGui::PushTextWrapPos(0.0f);
			if (!t.blocked.empty()) { ImGui::TextUnformatted(t.blocked.c_str()); }
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

	void DrawSettings()
	{
		static int s_settingsDrawnFrame = -100;
		if (ImGui::GetFrameCount() > s_settingsDrawnFrame + 1)
		{
			std::scoped_lock l(g_lock);
			RefreshKeys();
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
		std::string oarBlocked;
		{
			std::scoped_lock l(g_lock);
			for (int i = 0; i < kCount; ++i) { loaded[i] = g_targets[i].loaded; }
			oarBlocked = g_targets[kOar].blocked;
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
			if (i == kOar && loaded[i] && !oarBlocked.empty())
			{
				// v6: OAR's own INI switched its menu off - said here too, not only on its greyed row
				ImGui::Indent();
				ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
				ImGui::PushTextWrapPos(0.0f);
				ImGui::TextUnformatted(oarBlocked.c_str());
				ImGui::PopTextWrapPos();
				ImGui::PopStyleColor();
				ImGui::Unindent();
			}
		}
	}

	void Frame()
	{
		++g_frame;
		// THE LOADING GUARD (2.2.0, LoadGuard.h): while the game loads or is not at its main menu, nothing is started, no
		// mod's API is called and no key is watched - only a key press already under way is let go of, so nothing stays held.
		if (loadguard::Blocked())
		{
			if (g_apiCall)
			{
				logger::info("imgui: Open Animation Replacer - {} through its API dropped: the game started loading", g_apiCall->open ? "OpenMenu" : "CloseMenu");
				g_apiCall.reset();
			}
			g_reopenAfterOar = false;
			StepPress();
			return;
		}
		g_framesSeen.fetch_add(1, std::memory_order_relaxed);
		RunApiCall();
		StepPress();
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
			return std::string("{\"ok\":true,\"op\":\"imgui\",\"do\":\"list\",\"oarApiV3\":") + (g_oarV3 ? "true" : "false") +
				   ",\"loadGuard\":" + (loadguard::Blocked() ? "true" : "false") + ",\"targets\":[" + out + "]}";
		}
		if (what == "open" || what == "close")
		{
			const std::string id = str("id");
			int idx = -1;
			for (int i = 0; i < kCount; ++i)
			{
				if (id == kIds[i]) { idx = i; }
			}
			if (idx < 0) { return "{\"ok\":false,\"op\":\"imgui\",\"error\":\"id = oar | cs | ied | optiscaler | enb\"}"; }
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
}
