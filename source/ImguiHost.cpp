#include "ImguiHost.h"

#include "HostCore.h"
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
					t.blocked = c.enableUI ? std::string() : std::string(TR("AMF_ImguiOarUiOff", "Its menu is switched off in "
						"OpenAnimationReplacer.ini (bEnableUI = false), so there is nothing to open."));
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
			const bool useApi = a_id == kOar && g_oarV3;
			if (!useApi)
			{
				if (combo.Empty()) { return "no key"; }
				if (g_press) { return "a key press is still going on"; }
			}
			// AMF's menu goes first, so the mod's menu has the mouse, the keys and the screen
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
		ImGui::TextUnformatted(TR("AMF_OtherMenusHelp", "These mods draw menus of their own. Open one from here: this menu closes "
			"first, so the mod's menu has the mouse and the keys. Close it the way the mod does - its key, or Escape - and open "
			"this menu again with its key."));
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
			ImGui::BeginDisabled(!t.blocked.empty());
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
		}
	}

	void Frame()
	{
		++g_frame;
		RunApiCall();
		StepPress();
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
				   ",\"targets\":[" + out + "]}";
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
}
