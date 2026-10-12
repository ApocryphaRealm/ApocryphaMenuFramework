#pragma once

// ============================================================================================
// INI PAGES - the parts with no game or ImGui code, so the offline check (tools\ini_check.cpp) runs the same file the
// pages do: the shown label of a key, a section (a tab) or a file's name, and the match that takes an INI page away when
// the mod registers a page of its own.
// ============================================================================================

#include <algorithm>
#include <cctype>
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
}
