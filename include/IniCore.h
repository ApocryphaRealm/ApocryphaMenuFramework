#pragma once

// ============================================================================================
// INI PAGES - the parts with no game or ImGui code, so the offline check (tools\ini_check.cpp) runs the same file the
// pages do: the shown label of a key, a section (a tab) or a file's name, and the match that takes an INI page away when
// the mod registers a page of its own.
// ============================================================================================

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace inicore
{
	// "Lock Interaction Overhaul (MCM)" and "LockInteractionOverhaul" -> "lockinteractionoverhaul": letters and digits only,
	// anything in brackets left out
	inline std::string Normal(const std::string& a_s)
	{
		std::string out;
		int depth = 0;
		for (const char c : a_s)
		{
			if (c == '(' || c == '[') { ++depth; continue; }
			if (c == ')' || c == ']') { depth = std::max(0, depth - 1); continue; }
			if (depth == 0 && std::isalnum(static_cast<unsigned char>(c))) { out += static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
		}
		return out;
	}

	// THE SHOWN NAME (the owner, 2026-10-12, on Back Pocket For Controller's page: "there are underscores in between words
	// instead of spaces"). Only what is SHOWN: the file is read and written by its own key and section names, unchanged.
	//   "toggle_item_scan_code" -> "Toggle item scan code"     "UnpocketHoldTime" -> "Unpocket Hold Time"
	//   "FPSLimit" -> "FPS Limit" (an acronym stays whole)      "EnableVSync" -> "Enable VSync" (a two-capital run stays whole)
	//   a_key: a key's one-letter or two-letter type prefix before a capital is dropped - b, i, u, f, s, fl, sz
	//   ("bRequireFavourite" -> "Require Favourite", "uLogLevel" -> "Log Level"). Not for sections or file names, where a
	//   name like "iEquip" is the mod's own.
	// A name that already has a space is already words: only its underscores and doubled spaces change.
	inline std::string Label(const std::string& a_name, bool a_key)
	{
		auto upper = [](char c) { return std::isupper(static_cast<unsigned char>(c)) != 0; };
		auto lower = [](char c) { return std::islower(static_cast<unsigned char>(c)) != 0; };
		auto digit = [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; };

		const bool spaced = a_name.find(' ') != std::string::npos;
		std::string s = a_name;
		if (a_key && !spaced)
		{
			bool dropped = false;
			for (const char* p : { "fl", "sz" })
			{
				if (s.size() > 2 && s.compare(0, 2, p) == 0 && upper(s[2])) { s = s.substr(2); dropped = true; break; }
			}
			if (!dropped && s.size() > 1 && upper(s[1]) && std::string("biufs").find(s[0]) != std::string::npos) { s = s.substr(1); }
		}

		std::string out;
		std::size_t run = 0;   // capitals in a row just before s[i]
		for (std::size_t i = 0; i < s.size(); ++i)
		{
			const char c = s[i] == '_' ? ' ' : s[i];
			if (!spaced && upper(c) && i > 0)
			{
				const char p = s[i - 1];
				const bool nextLower = i + 1 < s.size() && lower(s[i + 1]);
				// a word starts at a capital after a small letter or a digit ("holdTime", "Key1Bind"), or at the last
				// capital of a run of three or more that a small letter follows ("FPSLimit" -> "FPS" "Limit"); a lone small
				// letter starting a word stays on it ("iEquip", "iHUD")
				const bool loneSmall = lower(p) && (i == 1 || s[i - 2] == '_' || s[i - 2] == ' ');
				if ((lower(p) && !loneSmall) || digit(p) || (upper(p) && nextLower && run >= 2)) { out += ' '; }
			}
			run = upper(c) ? run + 1 : 0;
			if (c == ' ' && (out.empty() || out.back() == ' ')) { continue; }
			out += c;
		}
		while (!out.empty() && out.back() == ' ') { out.pop_back(); }
		if (out.empty()) { return a_name; }
		// THE FIRST WORD STARTS WITH A CAPITAL (the owner, 2026-10-12: "toggle item scan code" -> "Toggle item scan code").
		// Only its first letter, and only when that word is all small letters: the rest stays as written, and a word with a
		// capital of its own ("iEquip", "moreHUD") is a name, kept as it is.
		{
			const std::size_t end = std::min(out.find(' '), out.size());
			if (lower(out[0]) && std::none_of(out.begin() + 1, out.begin() + static_cast<std::ptrdiff_t>(end), upper))
			{
				out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
			}
		}
		return out;
	}

	// A DLL'S VERSION-RESOURCE NAME, ONLY WHEN IT IS A NAME (the v4 test, 2026-10-12: AlternateConversationCamera.dll's
	// ProductName is Visual Studio's template "TODO: <Product name>", so its page read "TODO: <Product name> (INI)"). Not a
	// name: empty or blank; holding "TODO" (as written - "Mastodon" stays), "<" or ">"; one of the template's defaults,
	// any case; or just the DLL's own file name (a_dllFile, "AlternateConversationCamera.dll"), any case.
	inline bool UsableDllName(const std::string& a_name, const std::string& a_dllFile)
	{
		const auto lower = [](std::string s) {
			const auto b = s.find_first_not_of(" \t\r\n");
			s = b == std::string::npos ? std::string() : s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
			for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return s;
		};
		const std::string n = lower(a_name);
		if (n.empty()) { return false; }
		if (a_name.find("TODO") != std::string::npos || a_name.find('<') != std::string::npos || a_name.find('>') != std::string::npos) { return false; }
		for (const char* d : { "product name", "file description", "filedescription", "productname", "todo" })
		{
			if (n == d) { return false; }
		}
		return n != lower(a_dllFile);
	}

	// the first usable one of a DLL's version-resource names (ProductName before FileDescription), trimmed and at most 60
	// characters; empty when none is, and the page then takes the INI's own words
	inline std::string FirstUsableDllName(const std::vector<std::string>& a_candidates, const std::string& a_dllFile)
	{
		for (const std::string& c : a_candidates)
		{
			const auto b = c.find_first_not_of(" \t\r\n");
			if (b == std::string::npos) { continue; }
			const std::string t = c.substr(b, c.find_last_not_of(" \t\r\n") - b + 1);
			if (t.size() <= 60 && UsableDllName(t, a_dllFile)) { return t; }
		}
		return {};
	}

	// A MOD'S OWN PAGE TAKES ITS INI PAGE AWAY (next after 2.1.9; the 2026-10-12 test: Lock Interaction Overhaul registers as
	// "ApocryphaRealm Lock Interaction Overhaul", so its INI page stayed beside its own). True when a registered entry is
	// that mod's: its title, normalised, is one of the INI's names (a_names - the file's base name, its DLL's product name,
	// the name shown), or the DLL that registered it (a_module, its file name without .dll) is the INI's base name
	// (a_base): LockInteractionOverhaul.dll registers, LockInteractionOverhaul.ini's page goes.
	inline bool IsOwnPage(const std::vector<std::string>& a_names, const std::string& a_base, const std::string& a_title,
						  const std::string& a_module)
	{
		const std::string t = Normal(a_title);
		if (!t.empty() && std::find(a_names.begin(), a_names.end(), t) != a_names.end()) { return true; }
		const std::string m = Normal(a_module), b = Normal(a_base);
		return !m.empty() && m == b;
	}

	// The same answer from names already passed through Normal() (2.3.0: INI / TOML Sync normalises each registry entry
	// once, not once per page - the first frame of every opening spent 5-7 ms in that loop, combined run 2026-10-24).
	inline bool IsOwnPageNormal(const std::vector<std::string>& a_names, const std::string& a_normalBase,
								const std::string& a_normalTitle, const std::string& a_normalModule)
	{
		if (!a_normalTitle.empty() && std::find(a_names.begin(), a_names.end(), a_normalTitle) != a_names.end()) { return true; }
		return !a_normalModule.empty() && a_normalModule == a_normalBase;
	}

	// EACH INI PAGE STARTS OFF (2.2.0; the owner, 2026-10-13: "I want the INI feature of AMF to default to on, but I want all
	// of the named INI files defaulted to off, so that they can just look for the specific INI file they want and not have all
	// of them on"). [IniMenus] sOn holds the pages the player turned on, by id ("skse/plugins/foo.ini", "data/bar.ini"),
	// comma-separated, lower case; a page is shown only when the master switch is on AND its id is listed. A fresh INI lists
	// none, so no INI page is in the Mods list until the player turns one on on Settings > Converted menus > INI.
	inline std::set<std::string> ParseIdList(const std::string& a_list)
	{
		std::set<std::string> out;
		std::stringstream ss(a_list);
		std::string item;
		while (std::getline(ss, item, ',')) {
			const auto b = item.find_first_not_of(" \t\r\n");
			if (b == std::string::npos) continue;
			const auto e = item.find_last_not_of(" \t\r\n");
			std::string t = item.substr(b, e - b + 1);
			std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			out.insert(t);
		}
		return out;
	}

	inline std::string JoinIdList(const std::set<std::string>& a_ids)
	{
		std::string joined;
		for (const std::string& s : a_ids) { joined += (joined.empty() ? "" : ",") + s; }
		return joined;
	}

	// The list after the player flips one page's switch.
	inline std::string WithPage(const std::string& a_list, const std::string& a_id, bool a_on)
	{
		std::set<std::string> ids = ParseIdList(a_list);
		std::string id = a_id;
		std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (a_on) { ids.insert(id); } else { ids.erase(id); }
		return JoinIdList(ids);
	}

	inline bool PageShown(bool a_master, const std::set<std::string>& a_on, const std::string& a_id) { return a_master && a_on.contains(a_id); }
}
