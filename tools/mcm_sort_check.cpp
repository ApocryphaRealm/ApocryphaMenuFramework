// Off-game check of the MCM sort's rules under MSVC's std::regex (which is not Python's re): every rule in
// source/McmCategoryRules.inc must compile, and names go through the same Prepare steps as source/McmSort.cpp.
//   tools\mcm_sort_check.bat  -> prints "rules ok N of N" and one "group <- name" line per name
#include <algorithm>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

struct RawRule { const char* pattern; const char* group; float weight; };
static const RawRule kRawRules[] = {
#include "../source/McmCategoryRules.inc"
};

static std::string Prepare(std::string a, bool split)
{
	a = std::regex_replace(a, std::regex(R"(\s*\(MCM\)\s*$)"), "");
	if (split)
	{
		a = std::regex_replace(a, std::regex(R"(([a-z0-9])([A-Z]))"), "$1 $2");
		a = std::regex_replace(a, std::regex(R"(([A-Z]+)([A-Z][a-z]))"), "$1 $2");
	}
	a = std::regex_replace(a, std::regex(R"([_\-.]+)"), " ");
	a = std::regex_replace(a, std::regex(R"(\bmcm\b)", std::regex::ECMAScript | std::regex::icase), " ");
	a = std::regex_replace(a, std::regex(R"(\s+)"), " ");
	while (!a.empty() && a.front() == ' ') a.erase(a.begin());
	while (!a.empty() && a.back() == ' ') a.pop_back();
	return a;
}

int main(int argc, char** argv)
{
	struct Rule { std::regex rx; std::string group; float w; };
	std::vector<Rule> rules;
	int bad = 0;
	for (const auto& r : kRawRules)
	{
		try { rules.push_back({ std::regex(r.pattern, std::regex::ECMAScript | std::regex::icase | std::regex::optimize), r.group, r.weight }); }
		catch (const std::regex_error& e) { std::cout << "BAD RULE " << r.pattern << " : " << e.what() << "\n"; ++bad; }
	}
	std::cout << "rules ok " << rules.size() << " of " << std::size(kRawRules) << "\n";
	for (int i = 1; i < argc; ++i)
	{
		const std::string whole = Prepare(argv[i], false), split = Prepare(argv[i], true);
		const std::string text = whole == split ? whole : whole + " | " + split;
		std::vector<std::pair<std::string, float>> score;
		for (const auto& r : rules)
		{
			if (!std::regex_search(text, r.rx)) continue;
			auto it = std::find_if(score.begin(), score.end(), [&](auto& p) { return p.first == r.group; });
			if (it == score.end()) score.push_back({ r.group, r.w }); else it->second += r.w;
		}
		std::string best = "Other"; float bw = -1;
		for (auto& [g, w] : score) if (w > bw) { best = g; bw = w; }
		std::cout << best << " <- " << argv[i] << "\n";
	}
	return bad ? 1 : 0;
}
