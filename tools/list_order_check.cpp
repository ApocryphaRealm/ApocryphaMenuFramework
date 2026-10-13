// Off-game check of the mod list's order, sort and search with names in every script (2.2.0, after the final5 CJK test:
// with the game in Chinese, "模组函数菜单" and "精准度 - 局部伤害 (MCM)" were reported missing from the Mods list).
// Compiles source\Personalization.cpp itself (the list's order: custom sequence, separators, favourites, A-Z / Z-A) with
// include\ListCore.h (the fold the sort, the search box and the Filter use), and feeds it Latin, Cyrillic, Greek, Polish
// and CJK names.
//   tools\list_order_check.bat [<names file> <User.ini>]
// With the two files - one registered mod name per line (e.g. the "first page registered for" names of an AMF log) and
// a player's User.ini - the real list is ordered too and every name must appear exactly once.
// Prints one line per check and "list_order_check: N of N passed"; exits 1 on any failure.
#include "../include/ListCore.h"
#include "../include/Personalization.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
	int g_pass = 0, g_fail = 0;
	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	std::vector<registry::Entry> Entries(const std::vector<std::string>& a_names)
	{
		std::vector<registry::Entry> out;
		for (const auto& n : a_names)
		{
			registry::Entry e;
			e.modName = n;
			e.pages.push_back(registry::Page{ "Settings" });
			out.push_back(e);
		}
		return out;
	}

	// every registered name exactly once, as a mod row (not a separator)
	bool EveryEntryOnce(const std::vector<personalization::DisplayEntry>& a_rows, const std::vector<std::string>& a_names, std::string& a_missing)
	{
		std::map<std::string, int> seen;
		for (const auto& r : a_rows) { if (!r.separator) { ++seen[r.modName]; } }
		bool ok = true;
		for (const auto& n : a_names)
		{
			if (seen[n] != 1) { ok = false; a_missing += (a_missing.empty() ? "" : ", ") + n + " x" + std::to_string(seen[n]); }
		}
		return ok;
	}

	// the loose mods (depth 0, before the first separator row, after any favourites) in display order
	std::vector<std::string> LooseRun(const std::vector<personalization::DisplayEntry>& a_rows)
	{
		std::vector<std::string> out;
		for (const auto& r : a_rows)
		{
			if (r.separator) { break; }
			out.push_back(r.displayName);
		}
		return out;
	}

	std::string Join(const std::vector<std::string>& a_v)
	{
		std::string s;
		for (const auto& x : a_v) { s += (s.empty() ? "" : " | ") + x; }
		return s;
	}

	std::unordered_map<std::string, std::string> ReadIni(const std::string& a_path)
	{
		std::unordered_map<std::string, std::string> out;
		std::ifstream in(a_path, std::ios::binary);
		std::string line, section;
		while (std::getline(in, line))
		{
			if (!line.empty() && line.back() == '\r') { line.pop_back(); }
			if (line.empty() || line[0] == ';') { continue; }
			if (line.front() == '[' && line.back() == ']') { section = line.substr(1, line.size() - 2); continue; }
			const auto eq = line.find('=');
			if (eq != std::string::npos) { out[section + "." + line.substr(0, eq)] = line.substr(eq + 1); }
		}
		return out;
	}
}

int main(int argc, char** argv)
{
	// 1. the fold: whole UTF-8 characters, no locale, length kept
	Check(listcore::Fold("ABC xyz") == "abc xyz", "fold: ASCII");
	Check(listcore::Fold("ÄRGER À Þ ×") == "ärger à þ ×", "fold: Latin-1 (the multiplication sign kept)");
	Check(listcore::Fold("ŁÓDŹ ŻÓŁW ČŘŠŽ ĄĘŃŚ") == "łódź żółw čřšž ąęńś", "fold: Polish and Czech (Latin Extended-A)");
	Check(listcore::Fold("БЫСТРО ЁЖ ЀЏ") == "быстро ёж ѐџ", "fold: Cyrillic");
	Check(listcore::Fold("ΑΒΓ ΩΣ") == "αβγ ωσ", "fold: Greek");
	Check(listcore::Fold("模组函数菜单") == "模组函数菜单", "fold: CJK kept byte for byte");
	Check(listcore::Fold("精准度 - 局部伤害 (MCM)") == "精准度 - 局部伤害 (mcm)", "fold: a CJK name's Latin part folds, the rest is kept");
	Check(listcore::Fold("ひらがな カタカナ 한글") == "ひらがな カタカナ 한글", "fold: kana and Hangul kept");
	const std::string stray = std::string("A\xFF\xC3") + "B";
	Check(listcore::Fold(stray) == std::string("a\xFF\xC3") + "b", "fold: bytes that are not UTF-8 are copied as they are");
	for (const char* s : { "ÄRGER", "БЫСТРО", "ŁÓDŹ", "模组函数菜单" }) { Check(listcore::Fold(s).size() == std::string(s).size(), std::string("fold keeps the length: ") + s); }

	// 2. the order: code point order of the folded names - Latin, Greek, Cyrillic, kana, CJK, Hangul
	const std::vector<std::string> ladder{ "apple", "Banana", "zebra", "Échos", "Ωmega", "Зола", "ひらがな", "模组函数菜单", "精准度 - 局部伤害 (MCM)", "한글" };
	bool ladderOk = true;
	for (std::size_t i = 0; i + 1 < ladder.size(); ++i)
	{
		if (!listcore::Less(ladder[i], ladder[i + 1]) || listcore::Less(ladder[i + 1], ladder[i])) { ladderOk = false; std::cout << "  out of order: " << ladder[i] << " / " << ladder[i + 1] << "\n"; }
	}
	Check(ladderOk, "order: Latin, then accented Latin, Greek, Cyrillic, kana, CJK (by code point: 模 U+6A21 before 精 U+7CBE), Hangul");
	Check(!listcore::Less("Зола", "зола") && !listcore::Less("зола", "Зола"), "order: Cyrillic letter case does not decide the order");

	// 3. search and Filter words
	Check(listcore::Contains("模组函数菜单", "函数"), "search: a CJK word inside a CJK name");
	Check(listcore::Contains("精准度 - 局部伤害 (MCM)", "精准"), "search: the start of a CJK name");
	Check(!listcore::Contains("模组函数菜单", "精准"), "search: a CJK word that is not there");
	Check(listcore::Contains("Зелья (MCM)", "зел"), "search: Cyrillic, any letter case");
	Check(listcore::Contains("ŁÓDŹ Tweaks", "łódź"), "search: Polish, any letter case");
	Check(listcore::Contains("Accuracy - Localized Damage (MCM)", "ACC"), "search: ASCII, any letter case");
	Check(listcore::Contains("anything", ""), "search: an empty box matches everything");

	// 4. the list itself - the final5 case in small: a custom order saved in English, the game now in Chinese, so the two
	// menus whose names follow the game's language are names the order has never seen
	const std::vector<std::string> names{ "AMF Example", "Block Overhaul", "精准度 - 局部伤害 (MCM)", "模组函数菜单", "Зелья (MCM)", "зола",
		"Apple", "banana", "Échos", "Ωmega", "ひらがな" };
	const auto entries = Entries(names);
	std::unordered_map<std::string, std::string> ini{
		{ "MenuOrder.bCustomOrder", "1" },
		{ "MenuOrder.sOrder", "AMF Example|::sep:1|Block Overhaul|Accuracy - Localized Damage (MCM)" },
		{ "MenuAlias.::sep:1", "Combat" },
	};
	for (const char* sort : { "0", "1", "2" })
	{
		for (const char* custom : { "1", "0" })
		{
			ini["MenuOrder.iSort"] = sort;
			ini["MenuOrder.bCustomOrder"] = custom;
			personalization::LoadFrom(ini);
			const auto rows = personalization::Order(entries);
			std::string missing;
			const std::string what = std::string("list: sort ") + (sort[0] == '0' ? "list order" : sort[0] == '1' ? "A-Z" : "Z-A") +
				", custom order " + (custom[0] == '1' ? "on" : "off");
			Check(EveryEntryOnce(rows, names, missing), what + " - every entry appears once" + (missing.empty() ? "" : " (" + missing + ")"));
			if (sort[0] != '0')
			{
				auto run = LooseRun(rows);
				auto sorted = run;
				std::stable_sort(sorted.begin(), sorted.end(), [](const std::string& a, const std::string& b) { return listcore::Less(a, b); });
				if (sort[0] == '2') { std::reverse(sorted.begin(), sorted.end()); }
				Check(run == sorted, what + " - the loose mods in order: " + Join(run));
			}
		}
	}
	// the place a newcomer takes in the custom order with the A-Z switch off: among the loose mods, at its A-Z position -
	// for a CJK name that is after every Latin and Cyrillic name, directly above the first separator
	ini["MenuOrder.iSort"] = "0";
	ini["MenuOrder.bCustomOrder"] = "1";
	personalization::LoadFrom(ini);
	{
		const auto rows = personalization::Order(entries);
		const auto run = LooseRun(rows);
		Check(run.size() >= 2 && run[run.size() - 2] == "模组函数菜单" && run.back() == "精准度 - 局部伤害 (MCM)",
			"list: the two CJK newcomers sit at the end of the loose mods, above the first separator: " + Join(run));
	}
	// a pinned CJK menu goes to the top, a folded separator's CJK mod is still in the list (hidden, not dropped)
	personalization::SetFavourite("精准度 - 局部伤害 (MCM)", true);
	{
		const auto rows = personalization::Order(entries);
		Check(!rows.empty() && rows.front().modName == "精准度 - 局部伤害 (MCM)", "list: a pinned CJK menu is first");
		std::string missing;
		Check(EveryEntryOnce(rows, names, missing), "list: with a pin, every entry appears once" + (missing.empty() ? "" : " (" + missing + ")"));
	}
	personalization::SetFavourite("精准度 - 局部伤害 (MCM)", false);
	ini["MenuOrder.sOrder"] = "AMF Example|::sep:1|Block Overhaul|模组函数菜单|Зелья (MCM)";
	ini["MenuSeparators.sCollapsed"] = "::sep:1";
	personalization::LoadFrom(ini);
	{
		const auto rows = personalization::Order(entries);
		std::string missing;
		bool hidden = false;
		for (const auto& r : rows) { if (r.modName == "模组函数菜单") { hidden = r.hidden && r.depth == 1; } }
		Check(EveryEntryOnce(rows, names, missing) && hidden, "list: a CJK mod under a folded separator stays in the list, folded away");
	}

	// 5. optional: a real list (the final5 run's registered names and the player's User.ini)
	if (argc >= 3)
	{
		std::vector<std::string> real;
		std::set<std::string> seen;
		std::ifstream in(argv[1], std::ios::binary);
		for (std::string line; std::getline(in, line);)
		{
			if (!line.empty() && line.back() == '\r') { line.pop_back(); }
			if (!line.empty() && seen.insert(line).second) { real.push_back(line); }
		}
		personalization::LoadFrom(ReadIni(argv[2]));
		const auto rows = personalization::Order(Entries(real));
		std::string missing;
		Check(!real.empty() && EveryEntryOnce(rows, real, missing),
			"real list: " + std::to_string(real.size()) + " names, " + std::to_string(rows.size()) + " rows - every entry appears once" +
				(missing.empty() ? "" : " (" + missing + ")"));
		for (std::size_t i = 0; i < rows.size(); ++i)
		{
			const unsigned char first = rows[i].displayName.empty() ? 0 : static_cast<unsigned char>(rows[i].displayName[0]);
			if (!rows[i].separator && first >= 0xE0)
			{
				std::string above;
				for (std::size_t j = i + 1; j < rows.size(); ++j) { if (rows[j].separator) { above = rows[j].displayName; break; } }
				std::cout << "  row " << (i + 1) << " of " << rows.size() << ": " << rows[i].displayName << (rows[i].depth ? " (in a group)" : " (loose)")
						  << (above.empty() ? "" : ", next separator below: " + above) << "\n";
			}
		}
	}

	std::cout << "list_order_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail ? 1 : 0;
}
