// Off-game check of include/HostCore.h - the same code Framework > Other menus runs: each mod's open key read from its own
// config (ENB's enblocal.ini included), the key -> scan code mapping, the SendInput records, and ENB's file marker.
//   tools\imgui_host_check.bat [<a Mod Organizer instance's mods folder> [<its overwrite folder>]]
// With the folders, the real installed configs are read too (Open Animation Replacer, IED, Community Shaders, OptiScaler).
// Prints one line per check and "imgui_host_check: N of N passed"; exits 1 on any failure.
#include "../include/HostCore.h"
#include "../include/GameKeysCore.h"
#include <windowsx.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace
{
	int g_pass = 0, g_fail = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	std::string Keys(const hostcore::KeyCombo& a_c)
	{
		std::string out;
		char b[8];
		for (auto k : a_c.keys)
		{
			std::snprintf(b, sizeof(b), "%02X", k);
			out += (out.empty() ? "" : "+") + std::string(b);
		}
		return out;
	}
	// "AMF holds this key" specs as ImguiHost.cpp's BaseSpec builds them, and one with a key chosen
	hostcore::HoldSpec OarSpec(std::vector<std::uint32_t> a_ownMods)
	{
		hostcore::HoldSpec h;
		h.kind = hostcore::ConfigKind::kIni;
		h.section = "UI";
		h.key = "uToggleUIKey";
		h.code = hostcore::KeyCode::kDik;
		h.path = hostcore::InputPath::kGameEvents;
		h.fixedMods = std::move(a_ownMods);
		return h;
	}
	hostcore::HoldSpec CsSpec()
	{
		hostcore::HoldSpec h;
		h.kind = hostcore::ConfigKind::kCsJson;
		h.code = hostcore::KeyCode::kVk;
		h.path = hostcore::InputPath::kGameEvents;
		h.combos = true;
		return h;
	}
	hostcore::HoldSpec IedSpec()
	{
		hostcore::HoldSpec h;
		h.kind = hostcore::ConfigKind::kIni;
		h.section = "GUI";
		h.key = "ToggleKeys";
		h.code = hostcore::KeyCode::kDik;
		h.path = hostcore::InputPath::kGameEvents;
		h.combos = true;
		return h;
	}
	hostcore::HoldSpec OptiSpec()
	{
		hostcore::HoldSpec h;
		h.kind = hostcore::ConfigKind::kIni;
		h.section = "Menu";
		h.key = "ShortcutKey";
		h.code = hostcore::KeyCode::kVk;
		h.path = hostcore::InputPath::kWin32;
		return h;
	}
	hostcore::HoldSpec EnbSpec(std::vector<std::uint32_t> a_ownMods)
	{
		hostcore::HoldSpec h;
		h.kind = hostcore::ConfigKind::kIni;
		h.section = "INPUT";
		h.key = "KeyEditor";
		h.code = hostcore::KeyCode::kVk;
		h.path = hostcore::InputPath::kWin32;
		h.fixedMods = std::move(a_ownMods);
		return h;
	}
	const hostcore::FKey kF13{ VK_F13, 0x64 }, kF14{ VK_F14, 0x65 }, kF15{ VK_F15, 0x66 }, kF16{ VK_F16, 0x67 }, kF17{ VK_F17, 0x68 };
	hostcore::HoldSpec Chosen(hostcore::HoldSpec a_h, std::vector<std::uint32_t> a_mods, const hostcore::FKey& a_f)
	{
		a_h.assigned = true;
		a_h.fkey = a_f;
		a_h.combo.keys = std::move(a_mods);
		a_h.combo.keys.push_back(a_f.dik);
		return a_h;
	}
	hostcore::HoldInput Input(bool a_loaded, const hostcore::KeyCombo& a_current, bool a_backup, const hostcore::HoldSpec& a_spec)
	{
		hostcore::HoldInput in;
		in.loaded = a_loaded;
		in.applies = a_loaded;
		in.current = a_current;
		in.backup = a_backup;
		in.spec = a_spec;
		return in;
	}
	std::string csJsonForClash()
	{
		return "{\n    \"Menu\": {\n        \"AutoHideFeatureList\": false,\n        \"CSEditorToggleKey\": 126,\n        \"EffectToggleKey\": 135,\n"
			   "        \"ToggleKey\": 116\n    },\n    \"Performance Overlay\": {\n        \"ToggleKey\": 999\n    }\n}\n";
	}
}

int main(int argc, char** argv)
{
	using namespace hostcore;

	// ---- keys ----
	Check(sizeof(INPUT) == 40, "INPUT is 40 bytes on x64 (a 32-byte struct makes SendInput refuse every record)");
	Check(VkToDik(VK_INSERT) == 0xD2, "VK_INSERT -> DIK 0xD2 (extended, not numpad 0)");
	Check(VkToDik(VK_END) == 0xCF, "VK_END -> DIK 0xCF");
	Check(VkToDik(VK_F5) == 0x3F, "VK_F5 -> DIK 0x3F");
	Check(VkToDik('O') == 0x18, "'O' -> DIK 0x18");
	Check(VkToDik(VK_SHIFT) == 0x2A && VkToDik(VK_CONTROL) == 0x1D, "Shift / Ctrl -> left Shift / left Ctrl");
	Check(VkToDik(VK_LBUTTON) == 0 && VkToDik(0) == 0, "a mouse button or 0 has no scan code");
	std::cout << "     key names: Insert=" << DikName(0xD2) << " End=" << DikName(0xCF) << " F5=" << DikName(0x3F) << " O=" << DikName(0x18) << "\n";

	{
		KeyCombo c;
		c.keys = { 0x2A, 0x18 };
		const auto md = StepInputs(c, Step::kModifiersDown), kd = StepInputs(c, Step::kKeyDown), ku = StepInputs(c, Step::kKeyUp),
				   mu = StepInputs(c, Step::kModifiersUp);
		Check(md.size() == 1 && md[0].ki.wScan == 0x2A && md[0].ki.dwFlags == KEYEVENTF_SCANCODE, "Shift+O: Shift down as scan code 0x2A");
		Check(kd.size() == 1 && kd[0].ki.wScan == 0x18 && !(kd[0].ki.dwFlags & KEYEVENTF_KEYUP), "Shift+O: O down");
		Check(ku.size() == 1 && (ku[0].ki.dwFlags & KEYEVENTF_KEYUP), "Shift+O: O up");
		Check(mu.size() == 1 && mu[0].ki.wScan == 0x2A && (mu[0].ki.dwFlags & KEYEVENTF_KEYUP), "Shift+O: Shift up");
		KeyCombo ins;
		ins.keys = { 0xD2 };
		const auto d = StepInputs(ins, Step::kKeyDown);
		Check(d.size() == 1 && d[0].ki.wScan == 0x52 && (d[0].ki.dwFlags & KEYEVENTF_EXTENDEDKEY), "Insert: scan 0x52 with the extended flag");
		Check(StepInputs(ins, Step::kModifiersDown).empty(), "a single key has no modifier step");
	}

	// ---- OAR ----
	{
		const auto d = ParseOarIni("");
		Check(d.enableUI && Keys(d.key) == "2A+18", "OAR with no INI: its own default Shift+O, menu on");
		const auto o = ParseOarIni("[UI]\nbEnableUI = false\nuToggleUIKey = 24\nuToggleUIKeyCtrl = 1\nuToggleUIKeyShift = 0\n");
		Check(!o.enableUI && Keys(o.key) == "1D+18", "OAR bEnableUI = false is read; Ctrl+O");
		const auto h = ParseOarIni("[UI]\r\nuToggleUIKey=0x3B\r\nuToggleUIKeyShift=0\r\n");
		Check(Keys(h.key) == "3B", "OAR hex key, CRLF lines, no modifiers");
	}
	// ---- IED ----
	{
		const auto d = ParseIedConfig("", "");
		Check(Keys(d.key) == "0E" && d.source == "default", "IED with nothing: its default Backspace");
		const auto i = ParseIedConfig("[GUI]\nToggleKeys=0x2A+0x0E\nOverrideToggleKeys=false\n", "");
		Check(Keys(i.key) == "2A+0E" && i.source == "ini", "IED combo from the INI");
		const std::string ui = R"({"data":{"ui":{"data":{"toggle_keys":{"data":{"key":68,"combo_key":42},"version":1}}}}})";
		const auto u = ParseIedConfig("[GUI]\nToggleKeys=0x0E\n", ui);
		Check(Keys(u.key) == "2A+44" && u.source == "ui", "IED key set in its own UI wins over the INI");
		const auto ov = ParseIedConfig("[GUI]\nToggleKeys=0x44\nOverrideToggleKeys=true\n", ui);
		Check(Keys(ov.key) == "44" && ov.source == "ini", "IED OverrideToggleKeys=true: the INI wins");
		const auto off = ParseIedConfig("[GUI]\nEnabled=false\n", "");
		Check(!off.enabled, "IED [GUI] Enabled=false is read");
	}
	// ---- Community Shaders ----
	{
		Check(Keys(ParseCsSettings(R"({"Menu":{"ToggleKey":116}})")) == "3F", "CS ToggleKey 116 (F5)");
		Check(Keys(ParseCsSettings(R"({"Menu":{"ToggleKey":[16,35]}})")) == "2A+CF", "CS combo [Shift, End]");
		Check(ParseCsSettings(R"({"Menu":{}})").Empty() && ParseCsSettings("not json").Empty(), "CS with no key or a broken file: none");
	}
	// ---- OptiScaler ----
	{
		const auto a = ParseOptiScalerIni("[Menu]\nShortcutKey=auto\n");
		Check(a.hasKey && Keys(a.key) == "D2", "OptiScaler auto = Insert");
		Check(Keys(ParseOptiScalerIni("[Menu]\n; Shortcut key\nShortcutKey=0x24\n").key) == "C7", "OptiScaler 0x24 = Home");
		Check(Keys(ParseOptiScalerIni("[Menu]\nShortcutKey=116\n").key) == "3F", "OptiScaler decimal 116 = F5");
		Check(!ParseOptiScalerIni("[Menu]\nShortcutKey=-1\n").hasKey, "OptiScaler -1 = no key");
		Check(Keys(ParseOptiScalerIni("[Spoofing]\nShortcutKey=0x08\n").key) == "D2", "a ShortcutKey in another section is not the menu's");
	}
	// ---- 2.2.0: ENB - enblocal.ini's editor key, and its marker in a file (files only: ENB's licence forbids reverse engineering) ----
	{
		const std::string def = "[PROXY]\nEnableProxyLibrary=false\n\n[INPUT]\n//shift\nKeyCombination=16\n//f12\nKeyUseEffect=123\n"
								"//enter\nKeyEditor=13\n//insert\nKeyFPSLimit=36\n";
		const auto d = ParseEnbLocal(def);
		Check(d.editorOn && d.hasKey && Keys(d.key) == "2A+1C", "ENB default enblocal.ini: Shift + Enter, got " + Keys(d.key));
		Check(Keys(ParseEnbLocal("[GLOBAL]\nUsePatchSpeedhackWithoutGraphics=false\n").key) == "2A+1C",
			  "ENB with no [INPUT] keys: the defaults, Shift + Enter");
		const auto custom = ParseEnbLocal("[INPUT]\nKeyCombination=17\nKeyEditor=121\n");
		Check(custom.hasKey && Keys(custom.key) == "1D+44", "ENB custom key: Ctrl (17) + F10 (121), got " + Keys(custom.key));
		const auto noCombo = ParseEnbLocal("[INPUT]\nKeyCombination=16\nKeyUseCombination=false\nKeyEditor=36\n");
		Check(Keys(noCombo.key) == "C7", "ENB KeyUseCombination=false: Home alone, got " + Keys(noCombo.key));
		const auto older = ParseEnbLocal("[INPUT]\nKeyShowEditor=123\n");
		Check(Keys(older.key) == "2A+58", "ENB older file's KeyShowEditor=123: Shift + F12, got " + Keys(older.key));
		const auto off = ParseEnbLocal("[GLOBAL]\nEnableEditor=false\n[INPUT]\nKeyEditor=13\n");
		Check(!off.editorOn && off.offKey == "[GLOBAL] EnableEditor", "ENB editor switched off in [GLOBAL]: blocked, naming the key - got '" + off.offKey + "'");
		const auto offInput = ParseEnbLocal("[INPUT]\nKeyEditor=13\nUseEditor=0 // off\n");
		Check(!offInput.editorOn && offInput.offKey == "[INPUT] UseEditor", "ENB editor switched off in [INPUT] (0, with a trailing // comment)");
		Check(ParseEnbLocal("[WINDOW]\nEnableEditor=false\n").editorOn, "an editor key outside [GLOBAL] / [INPUT] does not switch it off");
		Check(ParseEnbLocal("[GLOBAL]\nEnableEditor=true\n").editorOn, "EnableEditor=true leaves it on");
		Check(!ParseEnbLocal("[INPUT]\nKeyEditor=0\n").hasKey, "ENB KeyEditor=0: no key");
		Check(!ParseEnbLocal("[INPUT]\nKeyEditor=word\n").hasKey, "ENB KeyEditor not a number: no key");
		Check(HasEnbMarker("Boris Vorontsov ENBSeries ") && HasEnbMarker(std::string("MZ\0\0garbage enbseries d3d11", 27)),
			  "the ENBSeries marker is found in a version string and in file bytes (any case)");
		Check(!HasEnbMarker("Microsoft Windows Operating System Direct3D 11 Runtime"), "Windows' own d3d11.dll is not ENB");
	}
	// ---- 2.2.0: "AMF holds this key" - each format rewritten and restored, only the one value changing ----
	{
		// a hold, then a restore from the backup: the value is AMF's key, the parser reads it back, every other byte is the
		// same, and the restore gives back the original file exactly
		auto roundTrip = [](const std::string& a_name, const std::string& a_file, const HoldSpec& a_h, const std::string& a_expectValue,
							auto a_parse) {
			std::string held, why;
			const bool ok = HoldText(a_file, a_h, held, why);
			Check(ok, a_name + ": held (" + why + ")");
			if (!ok) { return; }
			const Span s = HoldSpan(held, a_h);
			Check(s.found && held.substr(s.begin, s.end - s.begin) == a_expectValue,
				  a_name + ": the value written is " + a_expectValue + ", got " + (s.found ? held.substr(s.begin, s.end - s.begin) : std::string("nothing")));
			const Span o = HoldSpan(a_file, a_h);
			Check(held.substr(0, o.begin) == a_file.substr(0, o.begin) && held.substr(s.end) == a_file.substr(o.end),
				  a_name + ": every byte before and after the value is unchanged (encoding, BOM, line endings, comments)");
			const KeyCombo k = a_parse(held.rfind("\xEF\xBB\xBF", 0) == 0 ? held.substr(3) : held);   // as ReadFileText gives it
			Check(!k.Empty() && k.keys.back() == a_h.fkey.dik, a_name + ": its own reader now reads AMF's key " + ComboName(a_h.combo) + ", got " + Keys(k));
			std::string back;
			Check(RestoreText(held, a_file, a_h, back, why) && back == a_file, a_name + ": restored from the backup, the file equals the backup");
		};
		const HoldSpec oar = Chosen(OarSpec({ 0x2A }), { 0x2A }, kF13);
		const HoldSpec cs = Chosen(CsSpec(), {}, kF14);
		const HoldSpec ied = Chosen(IedSpec(), {}, kF15);
		const HoldSpec opti = Chosen(OptiSpec(), {}, kF16);
		const HoldSpec enb = Chosen(EnbSpec({ 0x2A }), { 0x2A }, kF17);
		const std::string bom = "\xEF\xBB\xBF";
		roundTrip("OAR INI (UTF-8 BOM, CRLF)", bom + "[General]\r\nbEnableUI = true\r\n\r\n[UI]\r\nbEnableUI = true\r\nuToggleUIKey = 24\r\nuToggleUIKeyShift = 1\r\n",
				  oar, "100", [](const std::string& a) { return ParseOarIni(a).key; });
		roundTrip("IED INI (LF, # comments, hex)", "[GUI]\n# ToggleKeys=0x0E\n#\nToggleKeys=0x0E\n\n## Override keys configured via UI\nOverrideToggleKeys=false\n",
				  ied, "0x66", [](const std::string& a) { return ParseIedConfig(a, "").key; });
		roundTrip("IED INI with a combo", "[GUI]\r\nToggleKeys=0x2A+0x0E\r\n", ied, "0x66", [](const std::string& a) { return ParseIedConfig(a, "").key; });
		roundTrip("OptiScaler INI (auto, BOM, CRLF)", bom + "[Menu]\r\n; -1 -> No shortcut key\r\nShortcutKey=auto\r\n\r\n[Spoofing]\r\nShortcutKey=0x08\r\n",
				  opti, "0x7F", [](const std::string& a) { return ParseOptiScalerIni(a).key; });
		roundTrip("OptiScaler INI (decimal)", "[Menu]\nShortcutKey=116\n", opti, "127", [](const std::string& a) { return ParseOptiScalerIni(a).key; });
		roundTrip("ENB enblocal.ini (// comments)", "[INPUT]\r\n//shift\r\nKeyCombination=16\r\n//enter\r\nKeyEditor=13\r\n//insert\r\nKeyFPSLimit=36\r\n",
				  enb, "128", [](const std::string& a) { return ParseEnbLocal(a).key; });
		const std::string csJson =
			"{\n    \"Menu\": {\n        \"AutoHideFeatureList\": false,\n        \"CSEditorToggleKey\": 126,\n        \"EffectToggleKey\": 135,\n"
			"        \"Position\": [\n            10.0,\n            10.0\n        ],\n        \"ToggleKey\": 116,\n        \"UseResolutionFont\": true\n    },\n"
			"    \"Performance Overlay\": {\n        \"ToggleKey\": 999\n    }\n}\n";
		roundTrip("Community Shaders SettingsUser.json", csJson, cs, "125", [](const std::string& a) { return ParseCsSettings(a); });
		// a modifier AMF chose goes in as the config's own combination form, and the mod's own reader reads the whole combo back
		{
			const HoldSpec iedCtrl = Chosen(IedSpec(), { 0x1D }, kF13);
			std::string held, why;
			Check(HoldText("[GUI]\r\nToggleKeys=0x44\r\n", iedCtrl, held, why) && held == "[GUI]\r\nToggleKeys=0x1D+0x64\r\n" &&
					  Keys(ParseIedConfig(held, "").key) == "1D+64",
				  "IED given Ctrl + F13: written as 0x1D+0x64, read back as Ctrl + F13 - got " + Keys(ParseIedConfig(held, "").key));
			const HoldSpec csCtrl = Chosen(CsSpec(), { 0x1D }, kF13);
			Check(HoldText(csJson, csCtrl, held, why) && Keys(ParseCsSettings(held)) == "1D+64",
				  "Community Shaders given Ctrl + F13: written as [17,124], read back as Ctrl + F13 - got " + Keys(ParseCsSettings(held)));
			const HoldSpec oarShift = Chosen(OarSpec({ 0x2A }), { 0x2A }, kF13);
			Check(HoldText("[UI]\nuToggleUIKey = 24\nuToggleUIKeyShift = 1\n", oarShift, held, why) && Keys(ParseOarIni(held).key) == "2A+64",
				  "OAR keeps its own Shift: only uToggleUIKey is written, read back as Shift + F13");
			HoldSpec none = IedSpec();
			Check(!HoldText("[GUI]\nToggleKeys=0x44\n", none, held, why), "no key chosen (none free): nothing is written");
		}
		{
			const Span s = JsonMenuToggleKeySpan(csJson);
			Check(s.found && csJson.substr(s.begin, s.end - s.begin) == "116", "CS: Menu.ToggleKey, not CSEditorToggleKey or another object's ToggleKey");
			const std::string arr = "{\"Menu\":{\"ToggleKey\":[16,116]}}";
			const Span a = JsonMenuToggleKeySpan(arr);
			Check(a.found && arr.substr(a.begin, a.end - a.begin) == "[16,116]", "CS: a combination (an array) is the value as a whole");
			Check(!JsonMenuToggleKeySpan("{\"Other\":{\"ToggleKey\":116}}").found, "CS: a ToggleKey outside Menu is not taken");
		}
		std::string out, why;
		Check(!HoldText("[UI]\nbEnableUI=true\n", oar, out, why) && why.find("uToggleUIKey") != std::string::npos,
			  "a file with no menu key line is not held, and says which key is missing");
		Check(!HoldText(std::string("\xFF\xFE[\0U\0", 6), oar, out, why), "a UTF-16 file is never rewritten");
		Check(FormatLike("\"0x0E\"", 0x66) == "\"0x66\"" && FormatLike("24", 100) == "100" && FormatLike("auto", 0x7F) == "0x7F",
			  "the new value is written the way the old one was (quotes, hex, decimal; a word becomes hex)");
		Check(VkToDik(VK_F13) == 0x64 && VkToDik(VK_F14) == 0x65 && VkToDik(VK_F15) == 0x66, "F13-F15 are DirectInput's 0x64-0x66 (the keys a scan-code reader can see)");
		bool fkeysMatch = true;
		for (const auto& f : HeldKeys(InputPath::kWin32)) { fkeysMatch = fkeysMatch && VkToDik(f.vk) == f.dik; }
		Check(fkeysMatch, "F16-F24: the scan code AMF records for each is the one Windows maps its virtual key to");
		Check(DikName(0x64) == "F13", "F13 shows by its name, got " + DikName(0x64));
	}

	// ---- 2.2.0 held-key fix: which input each target reads (the final in-game test, 2026-10-12) ----
	{
		Check(TargetPath("oar") == InputPath::kGameEvents && TargetPath("cs") == InputPath::kGameEvents && TargetPath("ied") == InputPath::kGameEvents,
			  "OAR, Community Shaders and IED read the game's own input events (AMF presses their keys into the game's event list)");
		Check(TargetPath("optiscaler") == InputPath::kWin32 && TargetPath("enb") == InputPath::kWin32,
			  "OptiScaler and ENB read Windows keys (they keep SendInput)");
		bool gameKeys = HeldKeys(InputPath::kGameEvents).size() == 3;
		for (const auto& f : HeldKeys(InputPath::kGameEvents)) { gameKeys = gameKeys && f.dik >= 0x64 && f.dik <= 0x66; }
		Check(gameKeys, "a game-events target is only ever given F13-F15 (the only keys above F12 DirectInput names)");
		bool winKeys = !HeldKeys(InputPath::kWin32).empty();
		for (const auto& f : HeldKeys(InputPath::kWin32)) { winKeys = winKeys && f.vk >= VK_F16 && f.vk <= VK_F24; }
		Check(winKeys, "a Windows-keys target is given F16-F24");
	}

	// ---- 2.2.0 held-key fix: the game-key injector's sequencing (GameKeysCore.h) ----
	{
		using gamekeys::KeyEvent;
		using gamekeys::VirtualKeys;
		const auto show = [](const std::vector<KeyEvent>& a_e) {
			std::string s;
			char b[48];
			for (const auto& e : a_e)
			{
				std::snprintf(b, sizeof(b), "%02X:%s", e.dik, e.value == 0.0f ? "up" : (e.heldSecs == 0.0f ? "down" : "held"));
				s += (s.empty() ? "" : " ") + std::string(b);
			}
			return s.empty() ? std::string("-") : s;
		};
		VirtualKeys v;
		Check(v.Idle() && v.Poll(0.0).empty(), "nothing asked: idle, no events (the game's list goes on untouched)");
		// Shift + F13 as ImguiHost's steps ask for it, one step per frame (modifiers down, key down, -, key up, modifiers up)
		v.Down(0x2A);
		const auto p0 = v.Poll(1.000);
		v.Down(0x64);
		const auto p1 = v.Poll(1.016);
		const auto p2 = v.Poll(1.033);
		v.Up(0x64);
		const auto p3 = v.Poll(1.050);
		v.Up(0x2A);
		const auto p4 = v.Poll(1.066);
		const auto p5 = v.Poll(1.083);
		Check(show(p0) == "2A:down", "Shift + F13, dispatch 1: Shift down - got " + show(p0));
		Check(show(p1) == "2A:held 64:down", "dispatch 2: Shift held, F13 down - got " + show(p1));
		Check(show(p2) == "2A:held 64:held", "dispatch 3: both held - got " + show(p2));
		Check(show(p3) == "2A:held 64:up", "dispatch 4: Shift held, F13 released - got " + show(p3));
		Check(show(p4) == "2A:up", "dispatch 5: Shift released - got " + show(p4));
		Check(p5.empty() && v.Idle(), "dispatch 6: nothing left down, idle again");
		Check(!p1.empty() && p1[0].heldSecs > 0.0f && p3.size() == 2 && p3[1].heldSecs > 0.03f && p3[1].value == 0.0f,
			  "held repeats and the release carry how long the key has been down");
		// a press and its release asked for before one dispatch still give a dispatch with the key down
		VirtualKeys q;
		q.Down(0x65);
		q.Up(0x65);
		const auto a = q.Poll(2.0), b = q.Poll(2.016), c = q.Poll(2.033);
		Check(show(a) == "65:down" && show(b) == "65:up" && c.empty(), "down and up asked together: down on one dispatch, up on the next - got " +
																	   show(a) + " / " + show(b));
		VirtualKeys r;
		r.Up(0x66);
		Check(r.Poll(3.0).empty() && r.Idle(), "a release for a key that is not down sends nothing");
		r.Down(0x66);
		r.Poll(3.1);
		r.ReleaseAll();
		Check(show(r.Poll(3.2)) == "66:up" && r.Idle(), "ReleaseAll lets go of every key down on the next dispatch");
	}

	// ---- 2.2.0 held-key fix: clash avoidance (F15 is Community Shaders' CS Editor key in Njordlinger) ----
	{
		const auto combo = [](std::initializer_list<std::uint32_t> a_k) { KeyCombo c; c.keys = a_k; return c; };
		Check(Clashes(combo({ 0x64 }), combo({ 0x2A, 0x64 })), "F13 and Shift + F13 clash (a mod bound to plain F13 opens on Shift + F13 too)");
		Check(!Clashes(combo({ 0x2A, 0x64 }), combo({ 0x1D, 0x64 })), "Shift + F13 and Ctrl + F13 do not clash");
		Check(Clashes(combo({ 0x2A, 0x64 }), combo({ 0x36, 0x64 })), "right Shift counts as Shift");
		Check(Clashes(combo({ 0x2A, 0x64 }), combo({ 0x2A, 0x1D, 0x64 })), "Shift + F13 and Ctrl + Shift + F13 clash (the first opens on the second)");
		Check(!Clashes(combo({ 0x64 }), combo({ 0x65 })), "different keys do not clash");
		const auto csOthers = CsOtherKeys(csJsonForClash());
		bool hasEditor = false, hasMenuKey = false;
		for (const auto& k : csOthers)
		{
			hasEditor = hasEditor || Keys(k) == "66";
			hasMenuKey = hasMenuKey || Keys(k) == "3F";
		}
		Check(hasEditor && !hasMenuKey, "Community Shaders' other keys: CSEditorToggleKey (F15) counted, its menu key (Menu.ToggleKey) left to the assignment");
		const auto enbOthers = EnbOtherKeys("[INPUT]\nKeyCombination=16\nKeyUseCombination=true\nKeyEditor=13\nKeyFPSLimit=36\nKeyScreenshot=44\n");
		Check(enbOthers.size() == 2, "ENB's other keys: KeyFPSLimit and KeyScreenshot, not the editor key or the combination settings - got " +
										 std::to_string(enbOthers.size()));
		Check(OptiOtherKeys("[Menu]\nShortcutKey=0x2D\nFpsShortcutKey=0x21\n").size() == 1, "OptiScaler's other keys: FpsShortcutKey, not ShortcutKey");

		// Njordlinger as the 2026-10-12 test had it: OAR Shift+O, Community Shaders F5 with CS Editor on F15, IED F10, OptiScaler
		// Insert, no ENB - nothing held yet
		std::vector<HoldInput> in(5);
		in[0] = Input(true, combo({ 0x2A, 0x18 }), false, OarSpec({ 0x2A }));
		in[1] = Input(true, combo({ 0x3F }), false, CsSpec());
		in[2] = Input(true, combo({ 0x44 }), false, IedSpec());
		in[3] = Input(true, combo({ 0xD2 }), false, OptiSpec());
		in[4] = HoldInput{};   // ENB not loaded
		const auto out = AssignHeldKeys(in, csOthers);
		Check(out[0].spec.assigned && Keys(out[0].spec.combo) == "2A+64", "OAR (keeps its own Shift) is given Shift + F13 - got " + Keys(out[0].spec.combo));
		Check(out[1].spec.assigned && Keys(out[1].spec.combo) == "65", "Community Shaders is given F14 (plain F13 would clash with OAR's Shift + F13) - got " +
																		   Keys(out[1].spec.combo));
		Check(out[2].spec.assigned && out[2].spec.combo.keys.back() != 0x66, "IED is NOT given F15 (Community Shaders' CS Editor key) - got " + Keys(out[2].spec.combo));
		Check(out[2].spec.assigned && Keys(out[2].spec.combo) == "1D+64", "IED is given Ctrl + F13 (F13-F15 alone and with Shift are all taken) - got " +
																			  Keys(out[2].spec.combo));
		Check(out[3].spec.assigned && Keys(out[3].spec.combo) == "67", "OptiScaler is given F16 - got " + Keys(out[3].spec.combo));
		bool apart = true;
		for (std::size_t i = 0; i < out.size(); ++i)
		{
			for (std::size_t j = i + 1; j < out.size(); ++j)
			{
				if (out[i].spec.assigned && out[j].spec.assigned) { apart = apart && !Clashes(out[i].spec.combo, out[j].spec.combo); }
			}
			for (const auto& o : csOthers)
			{
				if (out[i].spec.assigned) { apart = apart && !Clashes(out[i].spec.combo, o); }
			}
		}
		Check(apart, "no key given clashes with another given key or with any key bound in the configs");
		// none free: F13-F15 all bound plain elsewhere
		const std::vector<KeyCombo> allTaken = { combo({ 0x64 }), combo({ 0x65 }), combo({ 0x66 }) };
		const auto none = AssignHeldKeys(in, allTaken);
		Check(!none[0].spec.assigned && !none[1].spec.assigned && !none[2].spec.assigned,
			  "F13, F14 and F15 all bound plain in the configs: no game-events target is given a key (the switch is unavailable, with the reason)");
		Check(none[3].spec.assigned, "...while OptiScaler still gets F16");
		// a held target keeps its key, and the others go round it
		std::vector<HoldInput> heldIn = in;
		heldIn[1] = Input(true, combo({ 0x64 }), true, CsSpec());   // Community Shaders already on F13, its backup beside it
		const auto h = AssignHeldKeys(heldIn, csOthers);
		Check(h[1].held && Keys(h[1].spec.combo) == "64", "a held target keeps the key its config carries (F13)");
		Check(Keys(h[0].spec.combo) != "2A+64" && h[0].spec.assigned, "OAR then goes round it - got " + Keys(h[0].spec.combo));
		// ENB keeps its own Shift: Shift + F16 would clash with OptiScaler's F16, so it is given Shift + F17
		std::vector<HoldInput> w = in;
		w[4] = Input(true, combo({ 0x2A, 0x1C }), false, EnbSpec({ 0x2A }));
		const auto we = AssignHeldKeys(w, csOthers);
		Check(Keys(we[4].spec.combo) == "2A+68", "ENB (its own Shift) is given Shift + F17, apart from OptiScaler's F16 - got " + Keys(we[4].spec.combo));
		// a target that cannot be held (its menu off, no key line) takes no key from the others
		std::vector<HoldInput> blocked = in;
		blocked[0].eligible = false;
		const auto b = AssignHeldKeys(blocked, csOthers);
		Check(!b[0].spec.assigned && Keys(b[1].spec.combo) == "64", "a target that cannot be held takes no key: Community Shaders then gets F13");
	}

	// ---- 2.2.0 held-key fix: a mod manager that puts the file back at launch (Root Builder, OptiScaler, 2026-10-12) ----
	{
		const auto combo = [](std::initializer_list<std::uint32_t> a_k) { KeyCombo c; c.keys = a_k; return c; };
		std::vector<HoldInput> in(1);
		in[0] = Input(true, combo({ 0xD2 }), true, OptiSpec());   // backup beside it, but the file says Insert again
		auto o = AssignHeldKeys(in, {});
		Check(o[0].restored && !o[0].held, "backup beside OptiScaler.ini, but the file is back on Insert: restored (the switch is unavailable, with the reason)");
		in[0] = Input(true, combo({ 0x67 }), true, OptiSpec());   // backup, file on F16
		o = AssignHeldKeys(in, {});
		Check(o[0].held && !o[0].restored && Keys(o[0].spec.combo) == "67", "backup beside it and the file on F16: held");
		in[0] = Input(true, combo({ 0xD2 }), false, OptiSpec());   // no backup, its own key: switched off (off removes the backup)
		o = AssignHeldKeys(in, {});
		Check(!o[0].held && !o[0].restored && o[0].spec.assigned, "no backup and its own key: simply off, the switch available");
		in[0] = Input(true, combo({ 0x67 }), false, OptiSpec());   // F16 but no backup (deleted by hand)
		o = AssignHeldKeys(in, {});
		Check(!o[0].held && !o[0].restored, "AMF's key with no backup: not counted as held (nothing to restore from)");
	}

	// ---- 2.2.0 OptiScaler fix (2026-10-13): its key is POSTED to the game window, with the lParam Windows itself builds ----
	{
		// lParam: repeat 1, scan code in 16-23, extended flag (bit 24) for an E0 key, previous state + transition on a key-up
		Check(KeyMessageLParam(0xD2, false) == 0x01520001u, "Insert down: lParam 0x01520001 (repeat 1, scan 0x52, extended)");
		Check(KeyMessageLParam(0xD2, true) == 0xC1520001u, "Insert up: lParam 0xC1520001 (previous state and transition set)");
		Check(KeyMessageLParam(0x18, false) == 0x00180001u, "O down: lParam 0x00180001 (not extended)");
		Check(KeyMessageLParam(0x67, true) == 0xC0670001u, "F16 up: lParam 0xC0670001");
		Check(KeyMessageLParam(0x1C, false, true) == 0x201C0001u, "Enter down with Alt held: context bit 29 set");
		KeyCombo ins;
		ins.keys = { 0xD2 };
		const auto d = StepMessages(ins, Step::kKeyDown);
		const auto u = StepMessages(ins, Step::kKeyUp);
		Check(d.size() == 1 && d[0].msg == WM_KEYDOWN && d[0].vk == VK_INSERT && d[0].lParam == 0x01520001u, "Insert key-down step: WM_KEYDOWN, wParam VK_INSERT");
		Check(u.size() == 1 && u[0].msg == WM_KEYUP && u[0].vk == VK_INSERT && u[0].lParam == 0xC1520001u,
			  "Insert key-up step: WM_KEYUP, wParam VK_INSERT (OptiScaler 0.9.4 toggles on WM_KEYUP == ShortcutKey)");
		Check(StepMessages(ins, Step::kModifiersDown).empty() && StepMessages(ins, Step::kModifiersUp).empty(), "a single key posts no modifier messages");
		{
			// THE MOUSE FOR OPTISCALER'S MENU (2.2.0): the game's mouse events as the window messages OptiScaler's ImGui reads
			const POINT cl{ 640, 360 }, sc{ 1920 + 640, 360 };
			const auto ld = MouseForwardMessage(0, true, false, cl, sc);
			const auto lu = MouseForwardMessage(0, false, true, cl, sc);
			Check(ld && ld->msg == WM_LBUTTONDOWN && ld->wParam == MK_LBUTTON && ld->lParam == MAKELPARAM(640, 360), "left press: WM_LBUTTONDOWN, MK_LBUTTON, client (640, 360)");
			Check(lu && lu->msg == WM_LBUTTONUP && lu->wParam == 0 && lu->lParam == MAKELPARAM(640, 360), "left release: WM_LBUTTONUP, no buttons, client position");
			const auto rd = MouseForwardMessage(1, true, false, cl, sc);
			const auto ru = MouseForwardMessage(1, false, true, cl, sc);
			Check(rd && rd->msg == WM_RBUTTONDOWN && rd->wParam == MK_RBUTTON && ru && ru->msg == WM_RBUTTONUP, "right press / release: WM_RBUTTONDOWN (MK_RBUTTON) / WM_RBUTTONUP");
			const auto md = MouseForwardMessage(2, true, false, cl, sc);
			Check(md && md->msg == WM_MBUTTONDOWN && md->wParam == MK_MBUTTON, "middle press: WM_MBUTTONDOWN");
			Check(!MouseForwardMessage(0, false, false, cl, sc), "a held left button (neither press nor release) posts nothing");
			const auto wu = MouseForwardMessage(8, true, false, cl, sc);
			const auto wd = MouseForwardMessage(9, true, false, cl, sc);
			Check(wu && wu->msg == WM_MOUSEWHEEL && GET_WHEEL_DELTA_WPARAM(static_cast<WPARAM>(wu->wParam)) == WHEEL_DELTA &&
					  GET_X_LPARAM(static_cast<LPARAM>(wu->lParam)) == 1920 + 640 && GET_Y_LPARAM(static_cast<LPARAM>(wu->lParam)) == 360,
				  "wheel up: WM_MOUSEWHEEL, +120, screen position");
			Check(wd && wd->msg == WM_MOUSEWHEEL && GET_WHEEL_DELTA_WPARAM(static_cast<WPARAM>(wd->wParam)) == -WHEEL_DELTA, "wheel down: WM_MOUSEWHEEL, -120");
			Check(!MouseForwardMessage(8, false, true, cl, sc), "a wheel 'release' posts nothing");
			Check(!MouseForwardMessage(3, true, false, cl, sc) && !MouseForwardMessage(4, true, false, cl, sc), "other mouse buttons post nothing");
			const POINT neg{ -5, -7 };
			const auto ln = MouseForwardMessage(0, true, false, neg, neg);
			Check(ln && GET_X_LPARAM(static_cast<LPARAM>(ln->lParam)) == -5 && GET_Y_LPARAM(static_cast<LPARAM>(ln->lParam)) == -7, "a cursor left of / above the client area keeps its sign");

			// open as far as AMF set it
			ForwardTracker t;
			Check(!t.open, "tracker: closed at start (nothing AMF did)");
			t.OnPressSent(false, true);
			Check(!t.open, "tracker: an Open whose key could not be sent leaves it closed");
			t.OnPressSent(true, true);
			Check(t.open, "tracker: AMF's Open sent its key - open, forwarding on");
			t.OnPressSent(false, false);
			Check(t.open, "tracker: a Close whose key could not be sent leaves it open");
			t.OnPressSent(true, false);
			Check(!t.open, "tracker: AMF's Close sent its key - closed, forwarding off");
			t.OnPressSent(true, true);
			t.OnOwnKey();
			Check(!t.open, "tracker: the target's own key while open - closed, forwarding off");
			t.OnPressSent(true, true);
			t.OnLoading();
			Check(!t.open, "tracker: loading - closed, forwarding off");
		}
		KeyCombo f16;
		f16.keys = { 0x67 };
		Check(StepMessages(f16, Step::kKeyUp).size() == 1 && StepMessages(f16, Step::kKeyUp)[0].vk == VK_F16, "AMF's held F16 posts VK_F16");
		KeyCombo shiftEnter;
		shiftEnter.keys = { 0x2A, 0x1C };
		const auto sm = StepMessages(shiftEnter, Step::kModifiersDown);
		Check(sm.size() == 1 && sm[0].msg == WM_KEYDOWN && sm[0].vk == VK_SHIFT && sm[0].lParam == 0x002A0001u, "Shift + Enter: Shift down posted first");
		KeyCombo altF10;
		altF10.keys = { 0x38, 0x44 };
		const auto ad = StepMessages(altF10, Step::kKeyDown);
		Check(StepMessages(altF10, Step::kModifiersDown)[0].msg == WM_SYSKEYDOWN && ad.size() == 1 && ad[0].msg == WM_SYSKEYDOWN && (ad[0].lParam & (1u << 29)),
			  "Alt + F10: WM_SYSKEYDOWN with the context bit, as Windows sends it");
		Check(DikToVk(0xD2) == VK_INSERT && DikToVk(VkToDik(VK_END)) == VK_END && DikToVk(VkToDik('O')) == 'O' && DikToVk(0x2A) == VK_SHIFT,
			  "DikToVk undoes VkToDik (Insert, End, O, Shift)");

		// which way each target's key goes
		Check(SendMethodFor("optiscaler") == SendMethod::kPostMessage, "OptiScaler: posted to the game window (its SendInput detour drops input while its menu is open)");
		Check(SendMethodFor("optiscaler", true) == SendMethod::kSendInput, "OptiScaler with ManualInputPolling = true: SendInput (it polls GetAsyncKeyState)");
		Check(SendMethodFor("enb") == SendMethod::kSendInput, "ENB: SendInput, as before");
		Check(SendMethodFor("oar") == SendMethod::kGameEvents && SendMethodFor("cs") == SendMethod::kGameEvents && SendMethodFor("ied") == SendMethod::kGameEvents,
			  "OAR, Community Shaders, IED: the game's input events");
		Check(SendMethodFor("unknown") == SendMethod::kSendInput, "an unknown id: SendInput");
		for (const char* id : { "oar", "cs", "ied", "optiscaler", "enb" })
		{
			Check((SendMethodFor(id) == SendMethod::kGameEvents) == (TargetPath(id) == InputPath::kGameEvents),
				  std::string(id) + ": the send method agrees with its input path");
		}
		Check(!ParseOptiScalerIni("[Menu]\nShortcutKey=auto\n[Hotfix]\nManualInputPolling=auto\n").manualPolling, "ManualInputPolling=auto: its WndProc (posted messages reach it)");
		Check(ParseOptiScalerIni("[Hotfix]\nManualInputPolling=true\n").manualPolling, "ManualInputPolling=true is read");
		Check(!ParseOptiScalerIni("[Menu]\nManualInputPolling=true\n").manualPolling, "ManualInputPolling outside [Hotfix] is not it");

		// the owner's 04:19:57 switch under Root Builder: next launch the file is back on Insert with AMF's backup beside it -
		// restored (switch unavailable with the reason), Open uses its own key Insert, posted to the game window
		const auto combo = [](std::initializer_list<std::uint32_t> a_k) { KeyCombo c; c.keys = a_k; return c; };
		const auto restoredIni = ParseOptiScalerIni("[Menu]\nShortcutKey=auto\n");
		std::vector<HoldInput> in(1);
		in[0] = Input(true, restoredIni.key, true, OptiSpec());
		const auto o = AssignHeldKeys(in, {});
		Check(o[0].restored && !o[0].held && Keys(restoredIni.key) == "D2", "Root Builder put OptiScaler.ini back: restored, not held, its key Insert");
		Check(MethodFor(false, o[0].held) == OpenMethod::kOwnKey, "restored: Open uses its own key (Insert), not AMF's F16");
		(void) combo;
	}

	// ---- the real files ----
	if (argc > 1)
	{
		const std::filesystem::path mods = argv[1];
		const std::filesystem::path over = argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path();
		const auto oarIni = ReadFileText((mods / "Open Animation - No Main Menu GUI/SKSE/Plugins/OpenAnimationReplacer.ini").wstring());
		if (!oarIni.empty())
		{
			const auto o = ParseOarIni(oarIni);
			Check(!o.enableUI && Keys(o.key) == "2A+18", "real: Open Animation - No Main Menu GUI's INI - menu off, Shift+O");
		}
		const auto iedIni = ReadFileText((mods / "Immersive Equipment Displays/SKSE/Plugins/ImmersiveEquipmentDisplays.ini").wstring());
		const auto iedJson = over.empty() ? std::string() : ReadFileText((over / "SKSE/Plugins/IED/Settings.json").wstring());
		if (!iedIni.empty())
		{
			const auto c = ParseIedConfig(iedIni, iedJson);
			Check(Keys(c.key) == "0E", "real: IED's shipped INI (+ the player's Settings.json, no key set in its UI) - Backspace, got " + Keys(c.key));
		}
		const auto ovr = ReadFileText((mods / "IED Key Override/SKSE/Plugins/ImmersiveEquipmentDisplays.ini").wstring());
		if (!ovr.empty()) { Check(Keys(ParseIedConfig(ovr, iedJson).key) == "44", "real: IED Key Override's INI - F10"); }
		if (!over.empty())
		{
			const auto cs = ReadFileText((over / "SKSE/Plugins/CommunityShaders/SettingsUser.json").wstring());
			if (!cs.empty()) { Check(Keys(ParseCsSettings(cs)) == "3F", "real: Community Shaders SettingsUser.json - F5, got " + Keys(ParseCsSettings(cs))); }
		}
		const auto opti = ReadFileText((mods / "OptiScaler 0.9.4/Root/OptiScaler.ini").wstring());
		if (!opti.empty())
		{
			const auto c = ParseOptiScalerIni(opti);
			Check(c.hasKey && Keys(c.key) == "D2" && c.raw == "auto", "real: OptiScaler 0.9.4's OptiScaler.ini - auto = Insert");
		}
		// "AMF holds this key" on the real files, in memory only (nothing on disk is written): held, read back, restored byte-exact
		const auto realHold = [](const std::string& a_name, const std::string& a_file, const HoldSpec& a_h, auto a_parse) {
			if (a_file.empty()) { return; }
			std::string held, back, why;
			const bool ok = HoldText(a_file, a_h, held, why);
			const KeyCombo k = ok ? a_parse(held) : KeyCombo{};
			Check(ok && !k.Empty() && k.keys == a_h.combo.keys && RestoreText(held, a_file, a_h, back, why) && back == a_file &&
					  held.size() + 8 > a_file.size() && held.size() < a_file.size() + 8,
				  "real hold: " + a_name + " - " + ComboName(a_h.combo) + " written and read back, restore equals the file (" + why + ")");
		};
		realHold("OAR No-Main-Menu INI", oarIni, Chosen(OarSpec({ 0x2A }), { 0x2A }, kF13),
				 [](const std::string& a) { return ParseOarIni(a).key; });
		realHold("IED's shipped INI", iedIni, Chosen(IedSpec(), { 0x1D }, kF13),
				 [](const std::string& a) { return ParseIedConfig(a, "").key; });
		realHold("IED Key Override's INI", ovr, Chosen(IedSpec(), { 0x1D }, kF13),
				 [](const std::string& a) { return ParseIedConfig(a, "").key; });
		realHold("OptiScaler 0.9.4's INI", opti, Chosen(OptiSpec(), {}, kF16),
				 [](const std::string& a) { return ParseOptiScalerIni(a).key; });
		if (!over.empty())
		{
			realHold("Community Shaders SettingsUser.json", ReadFileText((over / "SKSE/Plugins/CommunityShaders/SettingsUser.json").wstring()),
					 Chosen(CsSpec(), {}, kF14), [](const std::string& a) { return ParseCsSettings(a); });
		}
	}

	// ---- the keys AMF would give in the real instance: none clashes with anything its configs bind (CS Editor on F15 there) ----
	if (argc > 2)
	{
		const std::filesystem::path mods = argv[1];
		const std::filesystem::path over = argv[2];
		const auto csUser = ReadFileText((over / "SKSE/Plugins/CommunityShaders/SettingsUser.json").wstring());
		if (!csUser.empty())
		{
			std::vector<KeyCombo> others = CsOtherKeys(csUser);
			const auto opti = ReadFileText((mods / "OptiScaler 0.9.4/Root/OptiScaler.ini").wstring());
			const auto oth = OptiOtherKeys(opti);
			others.insert(others.end(), oth.begin(), oth.end());
			const auto oarIni = ReadFileText((mods / "Open Animation - No Main Menu GUI/SKSE/Plugins/OpenAnimationReplacer.ini").wstring());
			auto ied = ReadFileText((mods / "IED Key Override/SKSE/Plugins/ImmersiveEquipmentDisplays.ini").wstring());
			if (ied.empty()) { ied = ReadFileText((mods / "Immersive Equipment Displays/SKSE/Plugins/ImmersiveEquipmentDisplays.ini").wstring()); }
			const KeyCombo oarKey = ParseOarIni(oarIni).key;
			std::vector<std::uint32_t> oarMods(oarKey.keys.begin(), oarKey.keys.end() - 1);
			std::vector<HoldInput> in(4);
			in[0] = Input(true, oarKey, false, OarSpec(oarMods));
			in[1] = Input(true, ParseCsSettings(csUser), false, CsSpec());
			in[2] = Input(true, ParseIedConfig(ied, "").key, false, IedSpec());
			in[3] = Input(true, ParseOptiScalerIni(opti).key, false, OptiSpec());
			const auto out = AssignHeldKeys(in, others);
			bool apart = true, all = true;
			for (std::size_t i = 0; i < out.size(); ++i)
			{
				all = all && out[i].spec.assigned;
				for (const auto& o : others) { apart = apart && !(out[i].spec.assigned && Clashes(out[i].spec.combo, o)); }
				for (std::size_t j = 0; j < out.size(); ++j)
				{
					if (j != i && out[i].spec.assigned) { apart = apart && !Clashes(out[i].spec.combo, in[j].current) && !(out[j].spec.assigned && Clashes(out[i].spec.combo, out[j].spec.combo)); }
				}
			}
			Check(all && apart, "real: Njordlinger's configs - OAR " + Keys(out[0].spec.combo) + ", CS " + Keys(out[1].spec.combo) + ", IED " +
									Keys(out[2].spec.combo) + ", OptiScaler " + Keys(out[3].spec.combo) +
									" - each free, none clashing with another or with any key the configs bind (" + std::to_string(others.size()) + " other keys read)");
			Check(out[2].spec.combo.keys.back() != 0x66 || out[2].spec.combo.keys.size() > 1, "real: IED is not given plain F15 (Community Shaders' CS Editor key here)");
		}
	}

	// ---- direct open (HostCore.h DirectTable) ----
	{
		const std::string h321 = "DB61B8B691E709BEDE355514871918BAA63EABE8F6D0734BF17FFCF793B4C894";
		const std::string h302 = "A433CF83AF7878DCAD14177FD6FF209FBE5DBA5059741F18B4DF82E24400AF4E";
		const auto v = DirectOpen("oar", { 3, 2, 1, 0 }, h321, true);
		Check(v.available, "direct: OAR 3.2.1 (allowlisted hash) that answers V3 opens directly (" + v.why + ")");
		const auto later = DirectOpen("oar", { 3, 3, 0, 0 }, "00", true);
		Check(!later.available, "direct: a later OAR whose hash is not on the allowlist falls back to its key - " + later.why);
		Check(!DirectOpen("oar", { 3, 0, 2, 0 }, h302, false).available, "direct: OAR 3.0.2 (no V3) falls back to its key - " + DirectOpen("oar", { 3, 0, 2, 0 }, h302, false).why);
		Check(!DirectOpen("oar", { 3, 0, 2, 0 }, h321, true).available, "direct: a file saying 3.0.2 is refused even with an allowlisted hash and a V3 answer (the version gate)");
		Check(!DirectOpen("oar", { 3, 2, 1, 0 }, h321, false).available, "direct: OAR 3.2.1 that did not answer V3 falls back to its key");
		Check(!DirectOpen("oar", {}, h321, true).available, "direct: an OAR whose file version can't be read falls back to its key");
		Check(!DirectOpen("oar", { 3, 2, 1, 0 }, "", true).available, "direct: an OAR whose hash can't be read falls back to its key");
		Check(DirectBuildKnown("oar", { 3, 2, 1, 0 }, h321) && !DirectBuildKnown("oar", { 3, 0, 2, 0 }, h302), "direct: V3 is asked for only from a known build");
		bool none = true;
		for (const char* id : { "cs", "ied", "optiscaler", "enb", "somethingelse" }) { none = none && !DirectOpen(id, { 9, 9, 9, 9 }, h321, true).available; }
		Check(none, "direct: Community Shaders, IED, OptiScaler, ENB and an unknown id never open directly (" + DirectOpen("cs", { 1, 8, 4, 0 }, "", false).why + "; ENB: " +
						DirectOpen("enb", {}, "", false).why + ")");
		Check(std::string(MethodName(MethodFor(true, true))) == "direct" && std::string(MethodName(MethodFor(false, true))) == "held key" &&
				  std::string(MethodName(MethodFor(false, false))) == "own key",
			  "direct: the method is direct before a held key, a held key before the mod's own key");
		Check(VerAtLeast({ 3, 2, 1, 0 }, { 3, 2, 1, 0 }) && VerAtLeast({ 3, 10, 0, 0 }, { 3, 2, 1, 0 }) && !VerAtLeast({ 3, 2, 0, 9 }, { 3, 2, 1, 0 }),
			  "direct: version compare is per field (3.10 > 3.2, 3.2.0.9 < 3.2.1)");
	}
	if (argc >= 2)
	{
		// the real installed DLLs (read only): each one's file version, and what the table makes of it
		const std::filesystem::path mods = argv[1];
		const auto oar = FileVersionOf((mods / "Open Animation Replacer/SKSE/Plugins/OpenAnimationReplacer.dll").wstring());
		const auto cs = FileVersionOf((mods / "Community Shaders/SKSE/Plugins/CommunityShaders.dll").wstring());
		const auto opti = FileVersionOf((mods / "OptiScaler 0.9.4/Root/dxgi.dll").wstring());
		if (oar != FileVer{})
		{
			// the installed 3.0.2 answers V3 with nullptr; the gate refuses it before the handshake is even asked
			const std::string h = Sha256OfFile((mods / "Open Animation Replacer/SKSE/Plugins/OpenAnimationReplacer.dll").wstring());
			const auto d = DirectOpen("oar", oar, h, false);
			Check(!d.available && !DirectBuildKnown("oar", oar, h), "real: installed OAR " + VerText(oar) + " - direct open not available (" + d.why + "), opens with its key");
		}
		if (cs != FileVer{}) { Check(!DirectOpen("cs", cs, "", false).available, "real: installed Community Shaders " + VerText(cs) + " - opens with its key"); }
		if (opti != FileVer{}) { Check(!DirectOpen("optiscaler", opti, "", false).available, "real: installed OptiScaler " + VerText(opti) + " - opens with its key"); }
	}
	if (argc >= 4)
	{
		// a downloaded OAR 3.2.1 (read only): its file version and hash pass the gate, and it exports RequestPluginAPI_UI
		const std::wstring dll = std::filesystem::path(argv[3]).wstring();
		const auto ver = FileVersionOf(dll);
		const std::string h = Sha256OfFile(dll);
		Check(DirectBuildKnown("oar", ver, h), "real: OAR at argv[3] - " + VerText(ver) + ", SHA-256 " + h + " is a known build (V3 will be asked for)");
		HMODULE m = ::LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
		Check(m && ::GetProcAddress(m, "RequestPluginAPI_UI") != nullptr, "real: OAR at argv[3] exports RequestPluginAPI_UI (mapped without running it)");
		if (m) { ::FreeLibrary(m); }
		Check(DirectOpen("oar", ver, h, true).available, "real: OAR at argv[3] answering V3 opens directly");
	}

	std::cout << "imgui_host_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
