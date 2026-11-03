#pragma once

// The part of Remembered settings (RememberedSettings.h) that needs no game, so tools\remembered_core_check.cpp can run it.
//
// Telling a row that RUNS something from a row that HOLDS a value. A script menu has no such flag: SkyUI's option types say
// how a row is drawn, and a mod may draw "Reset all settings" or "Uninstall" as a toggle. Playing one back on every load would
// run it on every load, so a row is left out when its words name an action: the mod's raw key ("$ResetAll") or the shown
// text. A row wrongly left out only loses its memory; a row wrongly kept could uninstall a mod, so the list leans to
// leaving out. Words that also fit real settings on their own ("force", "clear", "load", "restore") only count in a pair.
// The other test needs a game: a toggle that reports the same value after being pressed is a button (McmScripts.cpp).

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace rememberedcore
{
	// The words of a label, lower case: "$MCM_ResetAll Now" -> mcm, reset, all, now.
	inline std::vector<std::string> Words(std::string_view a_text)
	{
		std::vector<std::string> out;
		std::string word;
		char last = 0;
		for (const char c : a_text)
		{
			const bool lower = c >= 'a' && c <= 'z';
			const bool upper = c >= 'A' && c <= 'Z';
			const bool digit = c >= '0' && c <= '9';
			if (!lower && !upper && !digit)
			{
				if (!word.empty()) { out.push_back(word); }
				word.clear();
			}
			else
			{
				if (upper && last >= 'a' && last <= 'z' && !word.empty())
				{
					out.push_back(word);
					word.clear();
				}
				word += upper ? static_cast<char>(c - 'A' + 'a') : c;
			}
			last = c;
		}
		if (!word.empty()) { out.push_back(word); }
		return out;
	}

	inline bool WordsNameAction(const std::vector<std::string>& a_words)
	{
		static constexpr std::array<std::string_view, 16> kAlone{ "reset", "resets", "resetting", "uninstall", "uninstalls", "uninstaller", "uninstalling",
			"deinstall", "reinstall", "wipe", "purge", "respec", "defaults", "factory", "shutdown", "cleanup" };
		struct Pair
		{
			std::array<std::string_view, 7> first;
			std::array<std::string_view, 6> second;
		};
		static constexpr std::array<Pair, 4> kPairs{ {
			{ { "clear", "delete", "remove", "erase", "", "", "" }, { "all", "everything", "data", "cache", "saved", "" } },
			{ { "load", "save", "apply", "export", "import", "restore", "backup" }, { "preset", "presets", "profile", "profiles", "settings", "config" } },
			{ { "stop", "shut", "disable", "", "", "", "" }, { "mod", "down", "", "", "", "" } },
			{ { "force", "run", "start", "update", "refresh", "rebuild", "" }, { "update", "reload", "restart", "refresh", "now", "scan" } },
		} };
		const auto in = [](const auto& a_list, std::string_view a_word) {
			for (const std::string_view w : a_list)
			{
				if (!w.empty() && w == a_word) { return true; }
			}
			return false;
		};
		for (std::size_t i = 0; i < a_words.size(); ++i)
		{
			if (in(kAlone, a_words[i])) { return true; }
			if (i + 1 >= a_words.size()) { continue; }
			for (const Pair& pair : kPairs)
			{
				if (in(pair.first, a_words[i]) && in(pair.second, a_words[i + 1])) { return true; }
			}
		}
		return false;
	}

	// a_raw: the mod's raw label ("$ResetAll"); a_shown: the same label as the page shows it.
	inline bool LooksLikeAction(std::string_view a_raw, std::string_view a_shown)
	{
		return WordsNameAction(Words(a_raw)) || WordsNameAction(Words(a_shown));
	}
}
