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
		// F13-F24 have no name in most layouts' tables (AMF's held keys use them): named from their virtual key
		const UINT vk = ::MapVirtualKeyW((a_dik & 0x7F) | ((a_dik & 0x80) ? 0xE000u : 0u), MAPVK_VSC_TO_VK_EX);
		if (vk >= VK_F13 && vk <= VK_F24) { return "F" + std::to_string(13 + vk - VK_F13); }
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

	// ---- "AMF HOLDS THIS KEY" (2.2.0; the owner, 2026-10-12: "make it so those menus' hotkey is activated from within AMF, so
	// AMF calls them and no hotkey is needed - using AMF will free up hotkeys") ----------------------------------------------
	// With the switch on, AMF writes a key no keyboard has (F13 and above, a different one per mod, none already bound in any of
	// these mods' configs - AssignHeldKeys) into the mod's OWN config, in place of its menu key, and its Open button presses that
	// key; the player's real key is then free. Only that one value changes: the edit is a byte span replaced inside the file's
	// text, so the encoding, a byte-order mark, the line endings, comments and every other line stay exactly as they were. The
	// file as it was is kept as <file>.amf-key-backup while AMF holds the key; turning the switch off writes the original value
	// back from it and removes it. So a backup beside a config that no longer carries AMF's key means something put the file
	// back - a mod manager that redeploys it at every launch (Root Builder's copy mode, the 2026-10-12 test) - and the switch is
	// then unavailable. The targets that read the game's own input events (InputPath::kGameEvents) get F13-F15, the only keys
	// above F12 that DirectInput names, pressed into the game's event list; the readers of Windows keys get F16-F24 by SendInput.
	struct Span
	{
		bool found = false;
		std::size_t begin = 0, end = 0;   // the value's bytes in the text: [begin, end)
	};

	// The value of [section] key in an INI's text: after '=', without the spaces around it, and before a ';' or '//' comment
	// on the same line. The first such line wins (as the readers above take it).
	inline Span IniValueSpan(const std::string& a_text, const std::string& a_section, const std::string& a_key)
	{
		Span s;
		const std::string wantSection = Lower(a_section), wantKey = Lower(a_key);
		std::string section;
		std::size_t pos = 0;
		while (pos < a_text.size())
		{
			std::size_t eol = a_text.find('\n', pos);
			if (eol == std::string::npos) { eol = a_text.size(); }
			std::size_t lineEnd = eol;
			if (lineEnd > pos && a_text[lineEnd - 1] == '\r') { --lineEnd; }
			std::size_t b = pos;
			if (b == 0 && a_text.size() >= 3 && static_cast<unsigned char>(a_text[0]) == 0xEF && static_cast<unsigned char>(a_text[1]) == 0xBB &&
				static_cast<unsigned char>(a_text[2]) == 0xBF)
			{
				b = 3;
			}
			while (b < lineEnd && (a_text[b] == ' ' || a_text[b] == '\t')) { ++b; }
			if (b < lineEnd && a_text[b] == '[')
			{
				const std::size_t close = a_text.find(']', b);
				section = Lower(Trim(a_text.substr(b + 1, (close == std::string::npos || close > lineEnd ? lineEnd : close) - b - 1)));
			}
			else if (b < lineEnd && a_text[b] != ';' && a_text[b] != '#' && a_text[b] != '/' && section == wantSection)
			{
				const std::size_t eq = a_text.find('=', b);
				if (eq != std::string::npos && eq < lineEnd && Lower(Trim(a_text.substr(b, eq - b))) == wantKey)
				{
					std::size_t vb = eq + 1;
					std::size_t ve = lineEnd;
					const std::size_t sc = a_text.find(';', vb);
					if (sc != std::string::npos && sc < ve) { ve = sc; }
					const std::size_t sl = a_text.find("//", vb);
					if (sl != std::string::npos && sl < ve) { ve = sl; }
					while (vb < ve && (a_text[vb] == ' ' || a_text[vb] == '\t')) { ++vb; }
					while (ve > vb && (a_text[ve - 1] == ' ' || a_text[ve - 1] == '\t')) { --ve; }
					s.found = true;
					s.begin = vb;
					s.end = ve;
					return s;
				}
			}
			pos = eol + 1;
		}
		return s;
	}

	// Community Shaders' menu key: the value of "ToggleKey" directly inside the top-level "Menu" object of its settings JSON
	// (a number, or an array of numbers for a combination) - not "CSEditorToggleKey" or any other key that ends the same way.
	inline Span JsonMenuToggleKeySpan(const std::string& a_text)
	{
		Span s;
		// walk the text once, tracking strings and depth; remember the depth at which "Menu": { opened
		int depth = 0, menuDepth = -1;
		std::string lastString;
		bool lastWasKey = false;
		for (std::size_t i = 0; i < a_text.size(); ++i)
		{
			const char c = a_text[i];
			if (c == '"')
			{
				std::size_t j = i + 1;
				std::string str;
				while (j < a_text.size() && a_text[j] != '"')
				{
					if (a_text[j] == '\\' && j + 1 < a_text.size()) { str += a_text[j + 1]; j += 2; continue; }
					str += a_text[j++];
				}
				std::size_t k = j + 1;
				while (k < a_text.size() && std::isspace(static_cast<unsigned char>(a_text[k]))) { ++k; }
				lastWasKey = k < a_text.size() && a_text[k] == ':';
				lastString = str;
				if (lastWasKey && menuDepth >= 0 && depth == menuDepth + 1 && str == "ToggleKey")
				{
					std::size_t vb = k + 1;
					while (vb < a_text.size() && std::isspace(static_cast<unsigned char>(a_text[vb]))) { ++vb; }
					std::size_t ve = vb;
					if (vb < a_text.size() && a_text[vb] == '[')
					{
						ve = a_text.find(']', vb);
						if (ve == std::string::npos) { return s; }
						++ve;
					}
					else
					{
						while (ve < a_text.size() && (std::isdigit(static_cast<unsigned char>(a_text[ve])) || a_text[ve] == '-')) { ++ve; }
					}
					if (ve == vb) { return s; }
					s.found = true;
					s.begin = vb;
					s.end = ve;
					return s;
				}
				i = j;
				continue;
			}
			if (c == '{' || c == '[')
			{
				if (c == '{' && lastWasKey && depth == 1 && lastString == "Menu" && menuDepth < 0) { menuDepth = depth; }
				++depth;
				lastWasKey = false;
			}
			else if (c == '}' || c == ']')
			{
				--depth;
				if (menuDepth >= 0 && depth == menuDepth) { menuDepth = -2; }   // left "Menu": nothing more to find
			}
			else if (c == ',')
			{
				lastWasKey = false;
			}
		}
		return s;
	}

	// A number written the way the old value was: "0x.." hex when the old one was hex or was not a number at all ("auto"),
	// else decimal; quotes round the old value are kept.
	inline std::string FormatLike(const std::string& a_old, std::uint32_t a_n)
	{
		std::string inner = a_old;
		std::string q;
		if (inner.size() >= 2 && (inner.front() == '"' || inner.front() == '\'') && inner.back() == inner.front())
		{
			q = std::string(1, inner.front());
			inner = inner.substr(1, inner.size() - 2);
		}
		long long dummy = 0;
		const bool hex = Lower(Trim(inner)).rfind("0x", 0) == 0 || !ParseInt(inner, dummy);
		char buf[32];
		if (hex) { std::snprintf(buf, sizeof(buf), "0x%02X", a_n); }
		else { std::snprintf(buf, sizeof(buf), "%u", a_n); }
		return q + buf + q;
	}

	enum class ConfigKind
	{
		kIni,
		kCsJson
	};

	// ---- WHICH INPUT EACH TARGET READS (2.2.0 held-key fix; the final in-game test, 2026-10-12) ----------------------------------
	// A key AMF presses only opens a mod's menu if it arrives on the path that mod reads keys from. The test proved two paths:
	//   kGameEvents - the game's own input events (the BSTEventSource<InputEvent*> dispatch PollInputDevices sends, DirectInput
	//                 scan codes): Open Animation Replacer, Community Shaders and IED. After a relaunch with their keys on
	//                 F13 / F14 / F15, a SendInput F13-F15 (scan code or virtual key) never opened them - Windows' F13-F15 do
	//                 not reach the game's DirectInput - while the same keys spliced into the game's own event list (TestBench
	//                 bench.input, DIK 0x64 / 0x65 / 0x66, Shift + 0x64 for OAR) opened all three. So AMF puts its key into the
	//                 game's event list itself (Input.cpp, the game-key injector), as TestBench does.
	//   kWin32      - Windows' own key state / messages, virtual-key codes in the config: OptiScaler (proven: a SendInput F16
	//                 opened it, F16 closed it, Insert no longer did) and ENB (its enblocal.ini keys are virtual keys, and its
	//                 editor is opened the way a player opens it). These keep SendInput.
	enum class InputPath
	{
		kGameEvents,
		kWin32
	};
	// The classification above, by the ids Framework > Other menus uses (ImguiHost.cpp kIds); an unknown id reads Windows keys.
	inline InputPath TargetPath(const std::string& a_id)
	{
		return a_id == "oar" || a_id == "cs" || a_id == "ied" ? InputPath::kGameEvents : InputPath::kWin32;
	}
	enum class KeyCode
	{
		kDik,   // the config holds a DirectInput scan code (OAR, IED)
		kVk     // the config holds a Windows virtual-key code (Community Shaders, OptiScaler, ENB)
	};

	// The F-keys AMF may write, with both codes. F13-F15 are the only keys above F12 that DirectInput names, so a game-events
	// target gets one of those (with a modifier when all three are taken); a Win32 target gets F16-F24.
	struct FKey
	{
		std::uint32_t vk, dik;
	};
	inline const std::vector<FKey>& HeldKeys(InputPath a_path)
	{
		static const std::vector<FKey> game = { { VK_F13, 0x64 }, { VK_F14, 0x65 }, { VK_F15, 0x66 } };
		static const std::vector<FKey> win32 = { { VK_F16, 0x67 }, { VK_F17, 0x68 }, { VK_F18, 0x69 }, { VK_F19, 0x6A }, { VK_F20, 0x6B },
												 { VK_F21, 0x6C }, { VK_F22, 0x6D }, { VK_F23, 0x6E }, { VK_F24, 0x76 } };
		return a_path == InputPath::kGameEvents ? game : win32;
	}
	inline std::uint32_t ModifierVk(std::uint32_t a_dik)
	{
		switch (a_dik)
		{
		case kDikLShift: return VK_SHIFT;
		case kDikLCtrl: return VK_CONTROL;
		case kDikLAlt: return VK_MENU;
		default: return 0;
		}
	}

	struct HoldSpec
	{
		ConfigKind kind = ConfigKind::kIni;
		std::string section, key;   // INI: where the menu key is (ENB: KeyEditor, or KeyShowEditor in an older file)
		KeyCode code = KeyCode::kDik;
		InputPath path = InputPath::kGameEvents;
		bool combos = false;                    // AMF may write a modifier with the key (IED "0x1D+0x64", Community Shaders [17,124])
		std::vector<std::uint32_t> fixedMods;   // modifiers the mod keeps in its own config, untouched (OAR's flags, ENB's KeyCombination)
		// the key chosen for it (AssignHeldKeys): what the mod reads after the hold - modifiers, then the F-key
		bool assigned = false;
		KeyCombo combo;
		FKey fkey{};
	};

	inline Span HoldSpan(const std::string& a_text, const HoldSpec& a_h)
	{
		return a_h.kind == ConfigKind::kCsJson ? JsonMenuToggleKeySpan(a_text) : IniValueSpan(a_text, a_h.section, a_h.key);
	}

	inline std::string WithSpan(const std::string& a_text, const Span& a_s, const std::string& a_value)
	{
		return a_text.substr(0, a_s.begin) + a_value + a_text.substr(a_s.end);
	}

	inline bool IsUtf16(const std::string& a_raw)
	{
		return a_raw.size() >= 2 && ((static_cast<unsigned char>(a_raw[0]) == 0xFF && static_cast<unsigned char>(a_raw[1]) == 0xFE) ||
									 (static_cast<unsigned char>(a_raw[0]) == 0xFE && static_cast<unsigned char>(a_raw[1]) == 0xFF));
	}

	// The text written for the chosen key. A modifier AMF chose goes in as the config's own combination form (IED "0x1D+0x64",
	// Community Shaders [17,124]); a mod that keeps its modifiers elsewhere (OAR, ENB) gets only the F-key.
	inline std::string HoldValue(const HoldSpec& a_h, const std::string& a_old)
	{
		std::vector<std::uint32_t> chosenMods;
		if (a_h.combos && a_h.combo.keys.size() > 1) { chosenMods.assign(a_h.combo.keys.begin(), a_h.combo.keys.end() - 1); }
		if (a_h.kind == ConfigKind::kCsJson)
		{
			if (chosenMods.empty()) { return std::to_string(a_h.fkey.vk); }
			std::string out = "[";
			for (std::uint32_t m : chosenMods) { out += std::to_string(ModifierVk(m)) + ","; }
			return out + std::to_string(a_h.fkey.vk) + "]";
		}
		if (!chosenMods.empty())
		{
			std::string out;
			char b[16];
			for (std::uint32_t m : chosenMods)
			{
				std::snprintf(b, sizeof(b), "0x%02X+", m);
				out += b;
			}
			std::snprintf(b, sizeof(b), "0x%02X", a_h.fkey.dik);
			return out + b;
		}
		return FormatLike(a_old, a_h.code == KeyCode::kDik ? a_h.fkey.dik : a_h.fkey.vk);
	}

	// The held text: the menu key's value replaced by AMF's key. False (with why) when the file has no such value to change.
	inline bool HoldText(const std::string& a_raw, const HoldSpec& a_h, std::string& a_out, std::string& a_why)
	{
		if (IsUtf16(a_raw)) { a_why = "the file is UTF-16 text"; return false; }
		if (!a_h.assigned) { a_why = "no free key to hold it on"; return false; }
		const Span s = HoldSpan(a_raw, a_h);
		if (!s.found) { a_why = a_h.kind == ConfigKind::kCsJson ? std::string("no Menu.ToggleKey") : "no [" + a_h.section + "] " + a_h.key + " line"; return false; }
		const std::string old = a_raw.substr(s.begin, s.end - s.begin);
		a_out = WithSpan(a_raw, s, HoldValue(a_h, old));
		return true;
	}

	// The restored text: the menu key's value put back as the backup has it. False (with why) when either file lacks it.
	inline bool RestoreText(const std::string& a_raw, const std::string& a_backup, const HoldSpec& a_h, std::string& a_out, std::string& a_why)
	{
		if (IsUtf16(a_raw) || IsUtf16(a_backup)) { a_why = "UTF-16 text"; return false; }
		const Span now = HoldSpan(a_raw, a_h);
		const Span was = HoldSpan(a_backup, a_h);
		if (!now.found || !was.found) { a_why = !was.found ? "the backup has no menu key" : "the file has no menu key now"; return false; }
		a_out = WithSpan(a_raw, now, a_backup.substr(was.begin, was.end - was.begin));
		return true;
	}

	// ---- KEY CLASH (2.2.0 held-key fix: F15 is also Community Shaders' CS Editor key in Njordlinger, so IED on F15 opened both) --
	// Two bindings clash when one press could set off both: the same key, and one's modifiers are all held in the other's (no
	// modifiers at all is inside every set). Shift+F13 and Ctrl+F13 are apart; F13 and Shift+F13 are not - a mod bound to plain
	// F13 opens on Shift+F13 too. Right-hand modifiers count as the left-hand ones.
	inline std::uint32_t NormalKey(std::uint32_t a_dik)
	{
		switch (a_dik)
		{
		case 0x36: return kDikLShift;   // right Shift
		case 0x9D: return kDikLCtrl;    // right Ctrl
		case 0xB8: return kDikLAlt;     // right Alt
		default: return a_dik;
		}
	}
	inline bool Clashes(const KeyCombo& a_a, const KeyCombo& a_b)
	{
		if (a_a.Empty() || a_b.Empty() || NormalKey(a_a.keys.back()) != NormalKey(a_b.keys.back())) { return false; }
		const auto mods = [](const KeyCombo& a_c) {
			std::vector<std::uint32_t> m;
			for (std::size_t i = 0; i + 1 < a_c.keys.size(); ++i) { m.push_back(NormalKey(a_c.keys[i])); }
			std::sort(m.begin(), m.end());
			m.erase(std::unique(m.begin(), m.end()), m.end());
			return m;
		};
		const auto ma = mods(a_a), mb = mods(a_b);
		return std::includes(ma.begin(), ma.end(), mb.begin(), mb.end()) || std::includes(mb.begin(), mb.end(), ma.begin(), ma.end());
	}

	// The other keys the targets' configs bind - besides each one's menu key, which the assignment takes from the targets
	// themselves. Read generously: a number that is not really a key only takes one candidate away.
	//   Community Shaders: every "...Key" / "...Keys" value in its JSON (virtual keys, or arrays of them) except Menu.ToggleKey -
	//                      CSEditorToggleKey (F15 in Njordlinger), the effect and overlay keys
	inline void CsOtherKeysWalk(const nlohmann::json& a_j, bool a_top, std::vector<KeyCombo>& a_out)
	{
		if (!a_j.is_object()) { return; }
		for (auto it = a_j.begin(); it != a_j.end(); ++it)
		{
			const std::string name = Lower(it.key());
			const auto& v = it.value();
			if (v.is_object())
			{
				// Menu.ToggleKey is the menu key itself: skipped by walking "Menu" one level down with the flag set
				if (a_top && it.key() == "Menu")
				{
					for (auto m = v.begin(); m != v.end(); ++m)
					{
						if (m.key() == "ToggleKey") { continue; }
						nlohmann::json one = nlohmann::json::object();
						one[m.key()] = m.value();
						CsOtherKeysWalk(one, false, a_out);
					}
					continue;
				}
				CsOtherKeysWalk(v, false, a_out);
				continue;
			}
			const bool keyName = (name.size() >= 3 && name.compare(name.size() - 3, 3, "key") == 0) ||
								 (name.size() >= 4 && name.compare(name.size() - 4, 4, "keys") == 0);
			if (!keyName) { continue; }
			KeyCombo c;
			if (v.is_number_integer()) { c.keys.push_back(VkToDik(static_cast<std::uint32_t>(v.get<long long>() & 0xFF))); }
			else if (v.is_array())
			{
				for (const auto& e : v)
				{
					if (e.is_number_integer()) { c.keys.push_back(VkToDik(static_cast<std::uint32_t>(e.get<long long>() & 0xFF))); }
				}
			}
			bool ok = !c.keys.empty();
			for (auto k : c.keys) { ok = ok && k != 0; }
			if (ok) { a_out.push_back(c); }
		}
	}
	inline std::vector<KeyCombo> CsOtherKeys(const std::string& a_json)
	{
		std::vector<KeyCombo> out;
		const nlohmann::json j = nlohmann::json::parse(a_json, nullptr, false);
		if (!j.is_discarded()) { CsOtherKeysWalk(j, true, out); }
		return out;
	}

	// INI keys whose name passes a_take, with a number value, as virtual keys (OptiScaler: any "...Key" but [Menu] ShortcutKey;
	// ENB: [INPUT] Key... but the editor key and the combination settings)
	template <class Take>
	inline std::vector<KeyCombo> IniVkKeys(const std::string& a_text, Take a_take)
	{
		std::vector<KeyCombo> out;
		std::istringstream in(a_text);
		std::string line, section;
		while (std::getline(in, line))
		{
			line = Trim(line);
			if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '/') { continue; }
			if (line.front() == '[')
			{
				const auto close = line.find(']');
				section = Lower(Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) { continue; }
			const std::string key = Lower(Trim(line.substr(0, eq)));
			if (!a_take(section, key)) { continue; }
			std::string v = line.substr(eq + 1);
			if (const auto sc = v.find(';'); sc != std::string::npos) { v = v.substr(0, sc); }
			if (const auto sl = v.find("//"); sl != std::string::npos) { v = v.substr(0, sl); }
			long long n = 0;
			if (!ParseInt(v, n) || n <= 0 || n > 0xFE) { continue; }
			const std::uint32_t dik = VkToDik(static_cast<std::uint32_t>(n));
			if (dik == 0) { continue; }
			KeyCombo c;
			c.keys.push_back(dik);
			out.push_back(c);
		}
		return out;
	}
	inline std::vector<KeyCombo> OptiOtherKeys(const std::string& a_ini)
	{
		return IniVkKeys(a_ini, [](const std::string& a_section, const std::string& a_key) {
			return a_key.size() >= 3 && a_key.compare(a_key.size() - 3, 3, "key") == 0 && !(a_section == "menu" && a_key == "shortcutkey");
		});
	}
	inline std::vector<KeyCombo> EnbOtherKeys(const std::string& a_ini)
	{
		return IniVkKeys(a_ini, [](const std::string& a_section, const std::string& a_key) {
			return a_section == "input" && a_key.rfind("key", 0) == 0 && a_key != "keyeditor" && a_key != "keyshoweditor" &&
				   a_key != "keycombination" && a_key != "keyusecombination";
		});
	}

	// ---- WHICH KEY EACH TARGET GETS, AND WHETHER IT HOLDS ONE NOW ------------------------------------------------------------
	struct HoldInput
	{
		bool loaded = false;    // its config's keys count as bound
		bool applies = false;   // the row has the switch
		KeyCombo current;       // the key its config gives it now
		bool backup = false;    // <config>.amf-key-backup exists: AMF wrote its key into this config and has not given it back
		bool eligible = true;   // it may be given a key (its config has a menu key AMF can change, its menu is not off)
		HoldSpec spec;          // where and how its key is written (nothing chosen yet)
	};
	struct HoldOutput
	{
		bool held = false;       // the config carries AMF's key (and AMF's backup is there)
		bool restored = false;   // AMF's backup is there but the config no longer carries AMF's key: something put the file back
		HoldSpec spec;           // with the key chosen (spec.assigned), or kept when held
	};

	// True when a combo's key is one of the F-keys AMF writes for that path
	inline bool CarriesAmfKey(const KeyCombo& a_c, InputPath a_path)
	{
		if (a_c.Empty()) { return false; }
		for (const FKey& f : HeldKeys(a_path))
		{
			if (a_c.keys.back() == f.dik) { return true; }
		}
		return false;
	}

	// Held targets keep the key their config carries. Every other target that has the switch gets the first candidate - its
	// path's F-keys, plain first, then with Shift, Ctrl or Alt where its config can say a combination (or with the modifiers
	// it keeps itself) - that clashes with no key bound in any loaded target's config (each one's menu key, and a_others), and
	// with no key already given to a target before it in this pass. None free: spec.assigned stays false.
	inline std::vector<HoldOutput> AssignHeldKeys(const std::vector<HoldInput>& a_in, const std::vector<KeyCombo>& a_others)
	{
		std::vector<HoldOutput> out(a_in.size());
		std::vector<KeyCombo> given(a_in.size());
		for (std::size_t i = 0; i < a_in.size(); ++i)
		{
			out[i].spec = a_in[i].spec;
			if (!a_in[i].applies) { continue; }
			const bool carries = CarriesAmfKey(a_in[i].current, a_in[i].spec.path);
			out[i].held = a_in[i].backup && carries;
			out[i].restored = a_in[i].backup && !carries;
			if (out[i].held)
			{
				HoldSpec& s = out[i].spec;
				s.assigned = true;
				s.combo = a_in[i].current;
				for (const FKey& f : HeldKeys(s.path))
				{
					if (f.dik == a_in[i].current.keys.back()) { s.fkey = f; }
				}
				given[i] = s.combo;
			}
		}
		for (std::size_t i = 0; i < a_in.size(); ++i)
		{
			if (!a_in[i].applies || !a_in[i].eligible || out[i].held) { continue; }
			HoldSpec& s = out[i].spec;
			std::vector<std::vector<std::uint32_t>> modOptions;
			if (s.combos) { modOptions = { {}, { kDikLShift }, { kDikLCtrl }, { kDikLAlt } }; }
			else { modOptions = { s.fixedMods }; }
			for (const auto& mods : modOptions)
			{
				for (const FKey& f : HeldKeys(s.path))
				{
					KeyCombo cand;
					cand.keys = mods;
					cand.keys.push_back(f.dik);
					bool free = true;
					for (const KeyCombo& o : a_others) { free = free && !Clashes(cand, o); }
					for (std::size_t j = 0; j < a_in.size() && free; ++j)
					{
						if (j == i) { continue; }   // its own key is the one being replaced
						if (a_in[j].loaded) { free = !Clashes(cand, a_in[j].current); }
						if (free && !given[j].Empty()) { free = !Clashes(cand, given[j]); }
					}
					if (!free) { continue; }
					s.assigned = true;
					s.combo = cand;
					s.fkey = f;
					break;
				}
				if (s.assigned) { break; }
			}
			if (s.assigned) { given[i] = s.combo; }
		}
		return out;
	}
}
