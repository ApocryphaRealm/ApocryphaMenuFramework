// Off-game check of include/HostCore.h - the same code Framework > Other menus runs: each mod's open key read from its own
// config (ENB's enblocal.ini included), the key -> scan code mapping, the SendInput records, and ENB's file marker.
//   tools\imgui_host_check.bat [<a Mod Organizer instance's mods folder> [<its overwrite folder>]]
// With the folders, the real installed configs are read too (Open Animation Replacer, IED, Community Shaders, OptiScaler).
// Prints one line per check and "imgui_host_check: N of N passed"; exits 1 on any failure.
#include "../include/HostCore.h"

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
	}

	std::cout << "imgui_host_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
