#pragma once

// ============================================================================================
// OTHER MODS' OWN IMGUI MENUS - the part with no game and no ImGui in it, so tools\imgui_host_check.cpp runs the same
// code offline (2.2.0; the owner, 2026-10-12: a row in AMF for other menus, so AMF takes over their hotkeys to call them
// and closes by itself - 4. plans\amf-imgui-hosting\FINDINGS.md). ImguiHost.cpp draws and drives; this reads each
// target's own config for its open key and turns a key into the presses SendInput sends.
//
// Keys are held here as DirectInput scan codes (DIK), the codes the game's own input events carry: 0x01-0x7F as they
// are, and an E0-prefixed ("extended") key with 0x80 added (Insert 0xD2, End 0xCF, the arrows). A target that names its
// key as a Windows virtual-key code (Community Shaders, OptiScaler) is mapped through the keyboard layout, so the scan
// code sent is the one that key really has.
//
// Where each target keeps its key (read from each target's own shipped config and source; none of their code is used):
//   Open Animation Replacer  Data\SKSE\Plugins\OpenAnimationReplacer.ini [UI] uToggleUIKey (DIK, default 0x18 = O) with
//                            uToggleUIKeyCtrl / Shift / Alt (default Shift); bEnableUI = false installs no menu at all
//   Immersive Equipment Displays  Data\SKSE\Plugins\ImmersiveEquipmentDisplays.ini [GUI] ToggleKeys ("0x0E", or a combo
//                            "0x2A+0x0E", DIK); a key set in its own UI (IED\Settings.json, toggle_keys) wins unless
//                            OverrideToggleKeys=true. Its key only OPENS the menu (IED 1.2.2+); Escape closes it.
//   Community Shaders        Data\SKSE\Plugins\CommunityShaders\SettingsUser.json (else SettingsDefault.json)
//                            Menu.ToggleKey - a virtual-key code, or an array of them for a combo (default 35 = End)
//   OptiScaler               OptiScaler.ini beside its DLL (the game folder): [Menu] ShortcutKey - a virtual-key code,
//                            decimal or 0x hex; "auto" = 0x2D (Insert), -1 = no key
//   ENB                      enblocal.ini beside SkyrimSE.exe: [INPUT] KeyEditor (virtual key, default 13 = Enter) held with
//                            KeyCombination (default 16 = Shift) unless KeyUseCombination=false - files only, see below
// ============================================================================================

#ifndef NOMINMAX
#	define NOMINMAX
#endif
#include <Windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace hostcore
{
	// ---- keys ----------------------------------------------------------------------------------------------------
	struct KeyCombo
	{
		std::vector<std::uint32_t> keys;   // DIK; the last is the key, any before it are held with it (Shift, Ctrl...)
		bool Empty() const { return keys.empty() || keys.back() == 0; }
	};

	constexpr std::uint32_t kDikLCtrl = 0x1D;
	constexpr std::uint32_t kDikLShift = 0x2A;
	constexpr std::uint32_t kDikLAlt = 0x38;

	inline std::string Trim(std::string a_s)
	{
		const auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
		while (!a_s.empty() && ws(static_cast<unsigned char>(a_s.back()))) { a_s.pop_back(); }
		std::size_t i = 0;
		while (i < a_s.size() && ws(static_cast<unsigned char>(a_s[i]))) { ++i; }
		return a_s.substr(i);
	}

	inline std::string Lower(std::string a_s)
	{
		std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return a_s;
	}

	// "24", "0x18", " 0x2D " -> the number; false for anything else (an empty value, "auto", a word)
	inline bool ParseInt(const std::string& a_text, long long& a_out)
	{
		const std::string t = Trim(a_text);
		if (t.empty()) { return false; }
		char* end = nullptr;
		const long long v = std::strtoll(t.c_str(), &end, 0);
		if (!end || *end != '\0') { return false; }
		a_out = v;
		return true;
	}

	// A Windows virtual-key code -> its DIK, through the keyboard layout (MAPVK_VK_TO_VSC_EX gives 0xE0xx for an extended
	// key). 0 for a mouse button or a key with no scan code.
	inline std::uint32_t VkToDik(std::uint32_t a_vk)
	{
		if (a_vk == 0 || a_vk > 0xFE || a_vk <= VK_XBUTTON2) { return 0; }
		switch (a_vk)   // the extended keys MapVirtualKey reports without their E0 on some layouts
		{
		case VK_INSERT: return 0xD2;
		case VK_DELETE: return 0xD3;
		case VK_HOME: return 0xC7;
		case VK_END: return 0xCF;
		case VK_PRIOR: return 0xC9;
		case VK_NEXT: return 0xD1;
		case VK_LEFT: return 0xCB;
		case VK_RIGHT: return 0xCD;
		case VK_UP: return 0xC8;
		case VK_DOWN: return 0xD0;
		case VK_DIVIDE: return 0xB5;
		case VK_RCONTROL: return 0x9D;
		case VK_RMENU: return 0xB8;
		case VK_SNAPSHOT: return 0xB7;
		case VK_SHIFT: return kDikLShift;
		case VK_CONTROL: return kDikLCtrl;
		case VK_MENU: return kDikLAlt;
		default: break;
		}
		const UINT sc = ::MapVirtualKeyW(a_vk, MAPVK_VK_TO_VSC_EX);
		if (sc == 0) { return 0; }
		const std::uint32_t low = sc & 0x7F;
		return ((sc & 0xFF00) == 0xE000) ? (low | 0x80) : low;
	}

	// The key's name in the player's keyboard layout ("INSERT", "F5", "O"); "0x.." when Windows has none.
	inline std::string DikName(std::uint32_t a_dik)
	{
		if (a_dik == 0) { return std::string(); }
		const LONG lparam = static_cast<LONG>(((a_dik & 0x7F) << 16) | ((a_dik & 0x80) ? (1 << 24) : 0));
		wchar_t buf[64] = {};
		const int n = ::GetKeyNameTextW(lparam, buf, 64);
		if (n > 0)
		{
			const int len = ::WideCharToMultiByte(CP_UTF8, 0, buf, n, nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<std::size_t>(len), '\0');
			::WideCharToMultiByte(CP_UTF8, 0, buf, n, out.data(), len, nullptr, nullptr);
			return out;
		}
		char hex[16];
		std::snprintf(hex, sizeof(hex), "0x%02X", a_dik);
		return hex;
	}

	inline std::string ComboName(const KeyCombo& a_c)
	{
		std::string out;
		for (std::uint32_t k : a_c.keys) { out += (out.empty() ? "" : " + ") + DikName(k); }
		return out;
	}

	// The SendInput records for one edge of a combo. Each key goes as its SCAN code (KEYEVENTF_SCANCODE, with
	// KEYEVENTF_EXTENDEDKEY for an E0 key), so a game reading DirectInput sees the same code a real press makes and a
	// window reading WM_KEYDOWN gets the virtual key Windows maps it to.
	//   modifiers down / key down / key up / modifiers up are separate steps (ImguiHost sends each on its own frame).
	inline INPUT KeyInput(std::uint32_t a_dik, bool a_up)
	{
		INPUT in{};
		in.type = INPUT_KEYBOARD;
		in.ki.wVk = 0;
		in.ki.wScan = static_cast<WORD>(a_dik & 0x7F);
		in.ki.dwFlags = KEYEVENTF_SCANCODE | ((a_dik & 0x80) ? KEYEVENTF_EXTENDEDKEY : 0u) | (a_up ? KEYEVENTF_KEYUP : 0u);
		return in;
	}

	enum class Step
	{
		kModifiersDown,
		kKeyDown,
		kKeyUp,
		kModifiersUp
	};

	inline std::vector<INPUT> StepInputs(const KeyCombo& a_c, Step a_step)
	{
		std::vector<INPUT> out;
		if (a_c.Empty()) { return out; }
		const std::size_t mods = a_c.keys.size() - 1;
		switch (a_step)
		{
		case Step::kModifiersDown:
			for (std::size_t i = 0; i < mods; ++i) { out.push_back(KeyInput(a_c.keys[i], false)); }
			break;
		case Step::kKeyDown: out.push_back(KeyInput(a_c.keys.back(), false)); break;
		case Step::kKeyUp: out.push_back(KeyInput(a_c.keys.back(), true)); break;
		case Step::kModifiersUp:
			for (std::size_t i = mods; i-- > 0;) { out.push_back(KeyInput(a_c.keys[i], true)); }
			break;
		}
		return out;
	}

	// ---- reading the targets' configs ----------------------------------------------------------------------------------
	inline std::string ReadFileText(const std::wstring& a_path)
	{
		std::ifstream f(a_path, std::ios::binary);
		if (!f) { return std::string(); }
		std::ostringstream ss;
		ss << f.rdbuf();
		std::string s = ss.str();
		if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF && static_cast<unsigned char>(s[1]) == 0xBB &&
			static_cast<unsigned char>(s[2]) == 0xBF)
		{
			s.erase(0, 3);
		}
		return s;
	}

	// One value of an INI (sections and keys matched without case; ';' and '#' start a comment line; "key = value" or
	// "key=value"; a trailing ";" comment after a value is cut). False when the key is not there.
	inline bool IniValue(const std::string& a_text, const std::string& a_section, const std::string& a_key, std::string& a_out)
	{
		std::istringstream in(a_text);
		std::string line, section;
		const std::string wantSection = Lower(a_section), wantKey = Lower(a_key);
		while (std::getline(in, line))
		{
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#') { continue; }
			if (line.front() == '[')
			{
				const auto close = line.find(']');
				section = Lower(Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
				continue;
			}
			if (section != wantSection) { continue; }
			const auto eq = line.find('=');
			if (eq == std::string::npos) { continue; }
			if (Lower(Trim(line.substr(0, eq))) != wantKey) { continue; }
			std::string v = line.substr(eq + 1);
			if (const auto sc = v.find(';'); sc != std::string::npos) { v = v.substr(0, sc); }
			a_out = Trim(v);
			return true;
		}
		return false;
	}

	inline bool IniBool(const std::string& a_text, const std::string& a_section, const std::string& a_key, bool a_default)
	{
		std::string v;
		if (!IniValue(a_text, a_section, a_key, v)) { return a_default; }
		v = Lower(v);
		if (v == "true" || v == "1" || v == "yes" || v == "on") { return true; }
		if (v == "false" || v == "0" || v == "no" || v == "off") { return false; }
		long long n = 0;
		return ParseInt(v, n) ? n != 0 : a_default;
	}

	struct OarConfig
	{
		bool enableUI = true;
		KeyCombo key;
	};

	inline OarConfig ParseOarIni(const std::string& a_text)
	{
		OarConfig c;
		c.enableUI = IniBool(a_text, "UI", "bEnableUI", true);
		long long key = 0x18, ctrl = 0, shift = 1, alt = 0;   // OAR's own defaults (Settings::Initialize)
		std::string v;
		if (IniValue(a_text, "UI", "uToggleUIKey", v)) { ParseInt(v, key); }
		if (IniValue(a_text, "UI", "uToggleUIKeyCtrl", v)) { ParseInt(v, ctrl); }
		if (IniValue(a_text, "UI", "uToggleUIKeyShift", v)) { ParseInt(v, shift); }
		if (IniValue(a_text, "UI", "uToggleUIKeyAlt", v)) { ParseInt(v, alt); }
		if (ctrl) { c.key.keys.push_back(kDikLCtrl); }
		if (shift) { c.key.keys.push_back(kDikLShift); }
		if (alt) { c.key.keys.push_back(kDikLAlt); }
		c.key.keys.push_back(static_cast<std::uint32_t>(key & 0xFF));
		return c;
	}

	// "0x0E", "0x2A+0x0E", "42 + 14" -> DIK codes in order (the last is the key)
	inline KeyCombo ParseDikCombo(const std::string& a_text)
	{
		KeyCombo c;
		std::string part;
		std::istringstream in(a_text);
		while (std::getline(in, part, '+'))
		{
			long long n = 0;
			if (!ParseInt(part, n) || n <= 0 || n > 0xFF) { return KeyCombo{}; }
			c.keys.push_back(static_cast<std::uint32_t>(n));
		}
		return c;
	}

	struct IedConfig
	{
		bool enabled = true;      // [GUI] Enabled
		bool override = false;    // [GUI] OverrideToggleKeys
		KeyCombo iniKey;          // [GUI] ToggleKeys
		KeyCombo uiKey;           // set in IED's own UI (Settings.json), empty when none
		KeyCombo key;             // the one that applies
		std::string source;       // "ini" | "ui" | "default"
	};

	// IED's own UI keeps its open key under data.ui ... "toggle_keys" as { key, combo_key } (or the same inside a
	// { data, version } wrapper). Found by name wherever it sits under "ui", so a wrapper level more or less still reads.
	inline KeyCombo FindIedUiKey(const nlohmann::json& a_j)
	{
		if (a_j.is_object())
		{
			for (auto it = a_j.begin(); it != a_j.end(); ++it)
			{
				if (it.key() == "toggle_keys" && it.value().is_object())
				{
					const nlohmann::json* o = &it.value();
					if (o->contains("data") && (*o)["data"].is_object()) { o = &(*o)["data"]; }
					const auto num = [&](const char* a_k) -> std::uint32_t {
						return o->contains(a_k) && (*o)[a_k].is_number_integer() ? static_cast<std::uint32_t>((*o)[a_k].get<long long>() & 0xFF) : 0u;
					};
					KeyCombo c;
					const std::uint32_t combo = num("combo_key"), key = num("key");
					if (key == 0) { return KeyCombo{}; }
					if (combo) { c.keys.push_back(combo); }
					c.keys.push_back(key);
					return c;
				}
				KeyCombo deeper = FindIedUiKey(it.value());
				if (!deeper.Empty()) { return deeper; }
			}
		}
		return KeyCombo{};
	}

	inline IedConfig ParseIedConfig(const std::string& a_ini, const std::string& a_settingsJson)
	{
		IedConfig c;
		c.enabled = IniBool(a_ini, "GUI", "Enabled", true);
		c.override = IniBool(a_ini, "GUI", "OverrideToggleKeys", false);
		std::string v;
		if (IniValue(a_ini, "GUI", "ToggleKeys", v)) { c.iniKey = ParseDikCombo(v); }
		if (!a_settingsJson.empty())
		{
			const nlohmann::json j = nlohmann::json::parse(a_settingsJson, nullptr, false);
			if (!j.is_discarded() && j.is_object())
			{
				const nlohmann::json* ui = nullptr;
				if (j.contains("data") && j["data"].is_object() && j["data"].contains("ui")) { ui = &j["data"]["ui"]; }
				else if (j.contains("ui")) { ui = &j["ui"]; }
				if (ui) { c.uiKey = FindIedUiKey(*ui); }
			}
		}
		if (!c.uiKey.Empty() && !c.override) { c.key = c.uiKey; c.source = "ui"; }
		else if (!c.iniKey.Empty()) { c.key = c.iniKey; c.source = "ini"; }
		else { c.key.keys = { 0x0E }; c.source = "default"; }   // IED's shipped default, Backspace
		return c;
	}

	// Community Shaders: Menu.ToggleKey, a virtual-key code or an array of them (modifiers first). 0 keys when the file
	// says nothing usable.
	inline KeyCombo ParseCsSettings(const std::string& a_json)
	{
		KeyCombo c;
		const nlohmann::json j = nlohmann::json::parse(a_json, nullptr, false);
		if (j.is_discarded() || !j.is_object() || !j.contains("Menu") || !j["Menu"].is_object()) { return c; }
		const nlohmann::json& m = j["Menu"];
		if (!m.contains("ToggleKey")) { return c; }
		const nlohmann::json& k = m["ToggleKey"];
		std::vector<long long> vks;
		if (k.is_number_integer()) { vks.push_back(k.get<long long>()); }
		else if (k.is_array())
		{
			for (const auto& e : k)
			{
				if (e.is_number_integer()) { vks.push_back(e.get<long long>()); }
			}
		}
		for (long long vk : vks)
		{
			const std::uint32_t dik = VkToDik(static_cast<std::uint32_t>(vk));
			if (dik == 0) { return KeyCombo{}; }
			c.keys.push_back(dik);
		}
		return c;
	}

	struct OptiConfig
	{
		bool hasKey = true;    // false: ShortcutKey=-1
		KeyCombo key;
		std::string raw;       // the value as written ("auto" when the key is missing)
	};

	inline OptiConfig ParseOptiScalerIni(const std::string& a_text)
	{
		OptiConfig c;
		std::string v;
		c.raw = IniValue(a_text, "Menu", "ShortcutKey", v) ? v : std::string("auto");
		long long vk = 0x2D;   // "auto" = Insert (OptiScaler.ini's own comment)
		if (Lower(c.raw) != "auto")
		{
			if (!ParseInt(c.raw, vk)) { vk = 0x2D; }
		}
		if (vk < 0) { c.hasKey = false; return c; }
		const std::uint32_t dik = VkToDik(static_cast<std::uint32_t>(vk));
		if (dik == 0) { c.hasKey = false; return c; }
		c.key.keys.push_back(dik);
		return c;
	}

	// ---- ENB (2.2.0; the owner, 2026-10-12: "Add enb detection as well so it works for that too") ----------------------------
	// ENB's licence forbids reverse engineering it, so nothing of it is hooked, patched or read in memory: it is found from
	// FILES only (its DLL's version resource or the "ENBSeries" text in the file, and enblocal.ini beside it), and its editor
	// is opened the way a player opens it - its key, read from enblocal.ini, pressed with SendInput.
	//   [INPUT] KeyEditor (older files KeyShowEditor) - a virtual-key code, default 13 (Enter)
	//   [INPUT] KeyCombination - the key held with it, default 16 (Shift); [INPUT] KeyUseCombination=false drops it
	//   any [GLOBAL] or [INPUT] key naming the editor (other than the key itself) set false switches the editor off
	inline bool HasEnbMarker(const std::string& a_text)
	{
		return a_text.find("ENBSeries") != std::string::npos || Lower(a_text).find("enbseries") != std::string::npos;
	}

	struct EnbConfig
	{
		KeyCombo key;
		bool editorOn = true;
		std::string offKey;      // the enblocal.ini key that switched the editor off ("[GLOBAL] EnableEditor")
		bool hasKey = true;      // false: KeyEditor is 0, negative or not a key
	};

	inline EnbConfig ParseEnbLocal(const std::string& a_text)
	{
		EnbConfig c;
		long long editorVk = 13, comboVk = 16;
		std::string v;
		if (IniValue(a_text, "INPUT", "KeyEditor", v) || IniValue(a_text, "INPUT", "KeyShowEditor", v))
		{
			if (!ParseInt(v, editorVk)) { editorVk = 0; }
		}
		if (IniValue(a_text, "INPUT", "KeyCombination", v) && !ParseInt(v, comboVk)) { comboVk = 0; }
		const bool useCombo = IniBool(a_text, "INPUT", "KeyUseCombination", true);
		// the editor switched off: a key in [GLOBAL] or [INPUT] whose name says editor, set false
		{
			std::istringstream in(a_text);
			std::string line, section;
			while (std::getline(in, line) && c.editorOn)
			{
				line = Trim(line);
				if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '/') { continue; }
				if (line.front() == '[')
				{
					const auto close = line.find(']');
					section = Lower(Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
					continue;
				}
				if (section != "global" && section != "input") { continue; }
				const auto eq = line.find('=');
				if (eq == std::string::npos) { continue; }
				const std::string key = Trim(line.substr(0, eq));
				const std::string lk = Lower(key);
				if (lk.find("editor") == std::string::npos || lk == "keyeditor" || lk == "keyshoweditor") { continue; }
				std::string val = line.substr(eq + 1);
				if (const auto sc = val.find(';'); sc != std::string::npos) { val = val.substr(0, sc); }
				if (const auto sl = val.find("//"); sl != std::string::npos) { val = val.substr(0, sl); }
				val = Lower(Trim(val));
				if (val == "false" || val == "0" || val == "no" || val == "off")
				{
					c.editorOn = false;
					c.offKey = "[" + std::string(section == "global" ? "GLOBAL" : "INPUT") + "] " + key;
				}
			}
		}
		const std::uint32_t dik = editorVk > 0 && editorVk <= 0xFE ? VkToDik(static_cast<std::uint32_t>(editorVk)) : 0;
		if (dik == 0) { c.hasKey = false; return c; }
		if (useCombo && comboVk > 0 && comboVk <= 0xFE)
		{
			const std::uint32_t mod = VkToDik(static_cast<std::uint32_t>(comboVk));
			if (mod != 0 && mod != dik) { c.key.keys.push_back(mod); }
		}
		c.key.keys.push_back(dik);
		return c;
	}
}
