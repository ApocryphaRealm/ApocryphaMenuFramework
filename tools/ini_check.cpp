// Off-game check of the INI pages' shown names and own-page match (include/IniCore.h - the same code the pages run).
//   tools\ini_check.bat [<a mods folder>]
// Default mods folder: D:\Modlists\Njordlinger\mods. Its Back Pocket For Controller and SSE Display Tweaks INIs are read
// (read only) and every key and section label is printed and checked; a file not found there is reported and skipped.
// Prints one line per check and "ini_check: N of N passed"; exits 1 on any failure.
#include "../include/IniCore.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#pragma comment(lib, "version.lib")

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

	// a DLL file's ProductName then FileDescription (the listed translations, then US English), read as the pages read a
	// loaded one - here from the file, so no game is needed
	std::vector<std::string> VersionNames(const std::filesystem::path& a_dll)
	{
		std::vector<std::string> out;
		DWORD handle = 0;
		const DWORD size = GetFileVersionInfoSizeW(a_dll.wstring().c_str(), &handle);
		if (size == 0) { return out; }
		std::vector<unsigned char> data(size);
		if (!GetFileVersionInfoW(a_dll.wstring().c_str(), 0, size, data.data())) { return out; }
		struct LangCp { WORD lang; WORD cp; };
		LangCp* langs = nullptr;
		UINT bytes = 0;
		std::vector<std::wstring> prefixes;
		if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&langs), &bytes) && langs)
		{
			for (UINT i = 0; i < bytes / sizeof(LangCp); ++i)
			{
				wchar_t p[64];
				swprintf_s(p, L"\\StringFileInfo\\%04x%04x\\", langs[i].lang, langs[i].cp);
				prefixes.emplace_back(p);
			}
		}
		prefixes.emplace_back(L"\\StringFileInfo\\040904b0\\");
		for (const wchar_t* field : { L"ProductName", L"FileDescription" })
		{
			for (const std::wstring& pre : prefixes)
			{
				wchar_t* value = nullptr;
				UINT len = 0;
				if (VerQueryValueW(data.data(), (pre + field).c_str(), reinterpret_cast<void**>(&value), &len) && value && len > 1)
				{
					const std::wstring w(value);
					const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
					std::string s(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
					if (!s.empty()) { WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr); }
					out.push_back(s);
				}
			}
		}
		return out;
	}

	// the page's name as IniPages.cpp picks it when no table entry or plugin names the mod: the DLL's usable name, else
	// the INI base name's words
	void ExpectName(const std::vector<std::string>& a_candidates, const std::string& a_base, const std::string& a_want, const std::string& a_what)
	{
		const std::string dll = inicore::FirstUsableDllName(a_candidates, a_base + ".dll");
		const std::string got = !dll.empty() ? inicore::Label(dll, false) : inicore::Label(a_base, false);
		Check(got == a_want, a_what + " -> \"" + got + "\"" + (got == a_want ? "" : " (want \"" + a_want + "\")"));
	}
}

int main(int argc, char** argv)
{
	const std::filesystem::path mods = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("D:/Modlists/Njordlinger/mods");

	std::cout << "== labels: the asked-for cases\n";
	Expect("UnpocketHoldTime", true, "Unpocket Hold Time");
	Expect("toggle_item_scan_code", true, "Toggle item scan code");   // the first word capitalised, the rest as written
	Expect("bEnableX", true, "Enable X");
	Expect("FPSLimit", true, "FPS Limit");
	Expect("fHoldSeconds", true, "Hold Seconds");
	Expect("iCount", true, "Count");
	Expect("uLogLevel", true, "Log Level");
	Expect("sName", true, "Name");
	Expect("flSpeed", true, "Speed");
	Expect("szPath", true, "Path");
	Expect("fLevel", true, "Level");               // "f" then a capital, not "fl"
	Expect("b", true, "B");                         // a prefix alone stays (a first word, so capitalised)
	Expect("bX", true, "X");
	Expect("bool_value", true, "Bool value");       // no capital after the letter: not a prefix
	Expect("my__double___gap", true, "My double gap");
	Expect("_edge_", true, "Edge");
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
	Expect("po3_MagicSneakAttacks", false, "Po3 Magic Sneak Attacks");
	Expect("iEquip", false, "iEquip");
	Expect("iHUD", false, "iHUD");
	Expect("my_iEquip_Patch", false, "My iEquip Patch");
	Expect("iEquip_Patch", false, "iEquip Patch");       // a first word with a capital of its own is a name: kept
	Expect("moreHUD Inventory Plugin", false, "moreHUD Inventory Plugin");
	Expect("input", false, "Input");

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
	// 2.3.0: Sync normalises each registry entry once and asks IsOwnPageNormal - the same answer as IsOwnPage every time
	{
		struct Case { std::vector<std::string> names; const char *base, *title, *module; };
		const Case cases[] = {
			{ lio, "LockInteractionOverhaul", "ApocryphaRealm Lock Interaction Overhaul", "lockinteractionoverhaul" },
			{ lio, "LockInteractionOverhaul", "ApocryphaRealm Lock Interaction Overhaul", "" },
			{ lio, "LockInteractionOverhaul", "Lock Interaction Overhaul (MCM)", "" },
			{ { "blockoverhaul" }, "blockOverhaul", "Block Overhaul", "blockoverhaul" },
			{ lio, "LockInteractionOverhaul", "Remember Lockpick Angle", "rememberlockpickangle" },
			{ { "lockedemptycontaineractivatetextfix" }, "LockedEmptyContainerActivateTextFix", "ApocryphaRealm Lock Interaction Overhaul",
			  "lockinteractionoverhaul" },
			{ lio, "LockInteractionOverhaul", "", "" },
		};
		bool same = true;
		for (const Case& c : cases)
		{
			same = same && inicore::IsOwnPage(c.names, c.base, c.title, c.module) ==
							   inicore::IsOwnPageNormal(c.names, inicore::Normal(c.base), inicore::Normal(c.title), inicore::Normal(c.module));
		}
		Check(same, "IsOwnPageNormal (pre-normalised names, 2.3.0 Sync) answers as IsOwnPage in every case above");
	}

	std::cout << "== DLL version-resource names (the v4 test: \"TODO: <Product name> (INI)\")\n";
	const std::string acc = "AlternateConversationCamera";
	const std::string accWords = "Alternate Conversation Camera";
	ExpectName({ "TODO: <Product name>", "TODO: <File description>" }, acc, accWords, "the Visual Studio template's two placeholders");
	ExpectName({}, acc, accWords, "no version names");
	ExpectName({ "", "   " }, acc, accWords, "empty and blank");
	ExpectName({ "Product name" }, acc, accWords, "\"Product name\"");
	ExpectName({ "PRODUCT NAME" }, acc, accWords, "\"PRODUCT NAME\" (any case)");
	ExpectName({ "File description" }, acc, accWords, "\"File description\"");
	ExpectName({ "FileDescription" }, acc, accWords, "\"FileDescription\"");
	ExpectName({ "productname" }, acc, accWords, "\"productname\"");
	ExpectName({ "todo" }, acc, accWords, "\"todo\"");
	ExpectName({ "My Mod TODO" }, acc, accWords, "a name holding TODO");
	ExpectName({ "<Camera>" }, acc, accWords, "a name holding < and >");
	ExpectName({ "Camera > Fix" }, acc, accWords, "a name holding >");
	ExpectName({ "AlternateConversationCamera.dll" }, acc, accWords, "the DLL's own file name");
	ExpectName({ " alternateconversationcamera.DLL " }, acc, accWords, "the DLL's own file name, other case and spaces");
	ExpectName({ "TODO: <Product name>", "Conversation Camera Plus" }, acc, "Conversation Camera Plus", "a placeholder ProductName falls to a real FileDescription");
	ExpectName({ "Back Pocket For Controller" }, "BackPocketForController", "Back Pocket For Controller", "a real product name is kept");
	ExpectName({ "Mastodon Tweaks" }, "MastodonTweaks", "Mastodon Tweaks", "lower-case \"todo\" inside a word is kept");
	ExpectName({ std::string(61, 'x') }, acc, accWords, "a name over 60 characters");
	for (const char* folder : { "Improved Alternate Conversation Camera", "Improved Alternate Conversation Camera SE - Simple" })
	{
		const std::filesystem::path dll = mods / folder / "SKSE/Plugins/AlternateConversationCamera.dll";
		if (!std::filesystem::exists(dll)) { std::cout << "SKIP " << dll.string() << " (not found)\n"; continue; }
		const std::vector<std::string> names = VersionNames(dll);
		std::cout << "-- " << dll.string() << "\n";
		for (const std::string& n : names) { std::cout << "     version name \"" << n << "\"\n"; }
		Check(!names.empty() && inicore::FirstUsableDllName(names, acc + ".dll").empty(), "the real DLL names nothing usable");
		ExpectName(names, acc, accWords, std::string("the real DLL (") + folder + ")");
	}

	std::cout << "== real files (read only)\n";
	RealFile(mods / "Back Pocket For Controller/SKSE/Plugins/BackPocketForController.ini",
			 { { "fHoldSeconds", "Hold Seconds" },
			   { "bRequireFavourite", "Require Favourite" },
			   { "toggle_item_scan_code", "Toggle item scan code" },
			   { "controller_toggle_item_key_code", "Controller toggle item key code" },
			   { "show_notifications", "Show notifications" },
			   { "hide_pocketed_from_disenchanting", "Hide pocketed from disenchanting" },
			   { "uLogLevel", "Log Level" } },
			 { { "General", "General" }, { "input", "Input" }, { "Debug", "Debug" } });
	RealFile(mods / "SSE Display Tweaks/SKSE/Plugins/SSEDisplayTweaks.ini",
			 { { "EnableVSync", "Enable VSync" },
			   { "FramerateLimit", "Framerate Limit" },
			   { "UIFramerateLimitVSyncOff", "UI Framerate Limit VSync Off" },
			   { "OSDStatsEnabled", "OSD Stats Enabled" },
			   { "BorderlessUpscale", "Borderless Upscale" },
			   { "Enable", "Enable" },
			   { "OffsetX", "Offset X" } },
			 { { "Render", "Render" }, { "HAVOK", "HAVOK" }, { "OSD", "OSD" }, { "Miscellaneous", "Miscellaneous" } });

	// 2.2.0 (the owner, 2026-10-13): the master switch on, every INI page off until the player turns it on
	std::cout << "== each INI page starts off ([IniMenus] sOn)\n";
	{
		const std::string bpc = "skse/plugins/backpocketforcontroller.ini", dt = "skse/plugins/ssedisplaytweaks.ini";
		const std::set<std::string> fresh = inicore::ParseIdList("");   // a fresh user INI: no sOn
		Check(!inicore::PageShown(true, fresh, bpc) && !inicore::PageShown(true, fresh, dt),
			  "a fresh INI (no sOn): Back Pocket For Controller and SSE Display Tweaks are off with the master switch on");
		const std::string turnedOn = inicore::WithPage("", bpc, true);
		Check(turnedOn == bpc, "turning Back Pocket For Controller on stores sOn=" + turnedOn);
		const std::set<std::string> reread = inicore::ParseIdList(turnedOn);   // the INI read back at the next start
		Check(inicore::PageShown(true, reread, bpc) && !inicore::PageShown(true, reread, dt),
			  "read back at the next start: it is still on, Display Tweaks still off");
		Check(!inicore::PageShown(false, reread, bpc), "the master switch off hides it whatever sOn says");
		const std::set<std::string> handEdited = inicore::ParseIdList(" SKSE/Plugins/BackPocketForController.ini , data/foo.ini,,");
		Check(handEdited.size() == 2 && inicore::PageShown(true, handEdited, bpc), "a hand-edited sOn (spaces, capitals, empty items) still reads");
		const std::string both = inicore::WithPage(turnedOn, dt, true);
		const std::string backOff = inicore::WithPage(both, bpc, false);
		Check(backOff == dt, "turning one off keeps the other on: sOn=" + backOff);
	}

	std::cout << "ini_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
