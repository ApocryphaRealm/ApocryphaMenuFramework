// Off-game check of the INI pages' shown names and own-page match (include/IniCore.h - the same code the pages run).
//   tools\ini_check.bat [<a mods folder>]
// Default mods folder: D:\Modlists\Njordlinger\mods. Its Back Pocket For Controller and SSE Display Tweaks INIs are read
// (read only) and every key and section label is printed and checked; a file not found there is reported and skipped.
// Prints one line per check and "ini_check: N of N passed"; exits 1 on any failure.
#include "../include/IniCore.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
	int g_pass = 0, g_fail = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	void Expect(const std::string& a_in, bool a_key, const std::string& a_want)
	{
		const std::string got = inicore::Label(a_in, a_key);
		Check(got == a_want, std::string(a_key ? "key " : "name ") + "\"" + a_in + "\" -> \"" + got + "\"" +
								 (got == a_want ? "" : " (want \"" + a_want + "\")"));
	}

	// a label reads as words: no underscore, no doubled or edge space, never empty
	bool Readable(const std::string& a_s)
	{
		return !a_s.empty() && a_s.find('_') == std::string::npos && a_s.find("  ") == std::string::npos && a_s.front() != ' ' &&
			   a_s.back() != ' ';
	}

	std::string Trim(const std::string& a_s)
	{
		const auto b = a_s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos) { return {}; }
		return a_s.substr(b, a_s.find_last_not_of(" \t\r\n") - b + 1);
	}

	// the file's sections and active keys, as the page reads them (comments skipped)
	bool ReadIni(const std::filesystem::path& a_path, std::vector<std::string>& a_sections, std::vector<std::string>& a_keys)
	{
		std::ifstream in(a_path, std::ios::binary);
		if (!in) { return false; }
		std::string line;
		while (std::getline(in, line))
		{
			const std::string t = Trim(line);
			if (t.empty() || t[0] == ';' || t[0] == '#') { continue; }
			if (t.front() == '[' && t.back() == ']') { a_sections.push_back(t.substr(1, t.size() - 2)); continue; }
			if (const auto eq = t.find('='); eq != std::string::npos && eq > 0) { a_keys.push_back(Trim(t.substr(0, eq))); }
		}
		return true;
	}

	void RealFile(const std::filesystem::path& a_path, const std::map<std::string, std::string>& a_keys,
				  const std::map<std::string, std::string>& a_sections)
	{
		std::vector<std::string> sections, keys;
		if (!ReadIni(a_path, sections, keys))
		{
			std::cout << "SKIP " << a_path.string() << " (not found)\n";
			return;
		}
		std::cout << "-- " << a_path.string() << "\n";
		for (const std::string& s : sections)
		{
			const std::string l = inicore::Label(s, false);
			std::cout << "     [" << s << "] -> \"" << l << "\"\n";
			Check(Readable(l), "section \"" + s + "\" reads as words");
		}
		for (const std::string& k : keys)
		{
			const std::string l = inicore::Label(k, true);
			std::cout << "     " << k << " -> \"" << l << "\"\n";
			Check(Readable(l), "key \"" + k + "\" reads as words");
		}
		for (const auto& [k, want] : a_keys)
		{
			if (std::find(keys.begin(), keys.end(), k) == keys.end()) { Check(false, "key \"" + k + "\" is in the file"); continue; }
			Expect(k, true, want);
		}
		for (const auto& [s, want] : a_sections)
		{
			if (std::find(sections.begin(), sections.end(), s) == sections.end()) { Check(false, "section \"" + s + "\" is in the file"); continue; }
			Expect(s, false, want);
		}
	}
}

int main(int argc, char** argv)
{
	const std::filesystem::path mods = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("D:/Modlists/Njordlinger/mods");

	std::cout << "== labels: the asked-for cases\n";
	Expect("UnpocketHoldTime", true, "Unpocket Hold Time");
	Expect("toggle_item_scan_code", true, "toggle item scan code");
	Expect("bEnableX", true, "Enable X");
	Expect("FPSLimit", true, "FPS Limit");
	Expect("fHoldSeconds", true, "Hold Seconds");
	Expect("iCount", true, "Count");
	Expect("uLogLevel", true, "Log Level");
	Expect("sName", true, "Name");
	Expect("flSpeed", true, "Speed");
	Expect("szPath", true, "Path");
	Expect("fLevel", true, "Level");               // "f" then a capital, not "fl"
	Expect("b", true, "b");                         // a prefix alone stays
	Expect("bX", true, "X");
	Expect("bool_value", true, "bool value");       // no capital after the letter: not a prefix
	Expect("my__double___gap", true, "my double gap");
	Expect("_edge_", true, "edge");
	Expect("Already Spaced", true, "Already Spaced");
	Expect("Spaced_and under", true, "Spaced and under");
	Expect("EnableVSync", true, "Enable VSync");
	Expect("UIFramerateLimitVSyncOff", true, "UI Framerate Limit VSync Off");
	Expect("OSDWarnVMOverstressed", true, "OSD Warn VM Overstressed");
	Expect("Key1Bind", true, "Key1 Bind");
	Expect("HAVOK", false, "HAVOK");
	Expect("LocalizedDamage:Global:Head", false, "Localized Damage:Global:Head");
	std::cout << "== labels: names (sections and files - no type prefix dropped)\n";
	Expect("BackPocketForController", false, "Back Pocket For Controller");
	Expect("po3_MagicSneakAttacks", false, "po3 Magic Sneak Attacks");
	Expect("iEquip", false, "iEquip");
	Expect("iHUD", false, "iHUD");
	Expect("my_iEquip_Patch", false, "my iEquip Patch");
	Expect("moreHUD Inventory Plugin", false, "moreHUD Inventory Plugin");
	Expect("input", false, "input");

	std::cout << "== own page (the 2026-10-12 test: Lock Interaction Overhaul kept its INI page beside its own)\n";
	const std::vector<std::string> lio = { "lockinteractionoverhaul" };
	Check(inicore::IsOwnPage(lio, "LockInteractionOverhaul", "ApocryphaRealm Lock Interaction Overhaul", "lockinteractionoverhaul"),
		  "LIO's page, registered by LockInteractionOverhaul.dll under another title, takes the INI page away");
	Check(!inicore::IsOwnPage(lio, "LockInteractionOverhaul", "ApocryphaRealm Lock Interaction Overhaul", ""),
		  "the same title with no module does not match by name alone (the old miss)");
	Check(inicore::IsOwnPage(lio, "LockInteractionOverhaul", "Lock Interaction Overhaul (MCM)", ""), "a title naming the mod still matches");
	Check(inicore::IsOwnPage({ "blockoverhaul" }, "blockOverhaul", "Block Overhaul", "blockoverhaul"), "Block Overhaul (title and module)");
	Check(!inicore::IsOwnPage(lio, "LockInteractionOverhaul", "Remember Lockpick Angle", "rememberlockpickangle"),
		  "another mod's page does not take it");
	Check(!inicore::IsOwnPage({ "lockedemptycontaineractivatetextfix" }, "LockedEmptyContainerActivateTextFix", "ApocryphaRealm Lock Interaction Overhaul",
							  "lockinteractionoverhaul"),
		  "a different INI with a similar name keeps its page");

	std::cout << "== real files (read only)\n";
	RealFile(mods / "Back Pocket For Controller/SKSE/Plugins/BackPocketForController.ini",
			 { { "fHoldSeconds", "Hold Seconds" },
			   { "bRequireFavourite", "Require Favourite" },
			   { "toggle_item_scan_code", "toggle item scan code" },
			   { "controller_toggle_item_key_code", "controller toggle item key code" },
			   { "show_notifications", "show notifications" },
			   { "hide_pocketed_from_disenchanting", "hide pocketed from disenchanting" },
			   { "uLogLevel", "Log Level" } },
			 { { "General", "General" }, { "input", "input" }, { "Debug", "Debug" } });
	RealFile(mods / "SSE Display Tweaks/SKSE/Plugins/SSEDisplayTweaks.ini",
			 { { "EnableVSync", "Enable VSync" },
			   { "FramerateLimit", "Framerate Limit" },
			   { "UIFramerateLimitVSyncOff", "UI Framerate Limit VSync Off" },
			   { "OSDStatsEnabled", "OSD Stats Enabled" },
			   { "BorderlessUpscale", "Borderless Upscale" },
			   { "Enable", "Enable" },
			   { "OffsetX", "Offset X" } },
			 { { "Render", "Render" }, { "HAVOK", "HAVOK" }, { "OSD", "OSD" }, { "Miscellaneous", "Miscellaneous" } });

	std::cout << "ini_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
