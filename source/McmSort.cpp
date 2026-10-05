#include "McmLoader.h"

#include "McmShared.h"
#include "Personalization.h"
#include "Registry.h"
#include "Settings.h"
#include "Strings.h"
#include "utils/Logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <regex>
#include <unordered_map>

// The MCM auto-sort - see McmLoader.h (SortIntoCategories) for what it does and why. The owner, 2026-10-05: "it can just
// sort by name, it doesn't have to be perfect", so a menu is judged by its name alone: its entry name, plus the mod or
// plugin name in its import key. The name rules are MO2 Modlist Manager's (our own plugin), generated into
// McmCategoryRules.inc by tools/gen_mcm_categories.py; tools/try_mcm_categories.py runs the same steps on a list of names.

namespace mcmloader
{
	namespace
	{
		using json = nlohmann::json;
		using detail::Lower;
		using strings::TR;

		struct RawRule
		{
			const char* pattern;
			const char* group;
			float weight;
		};

		// Credit: MO2 Modlist Manager's TEXT_SIGNALS (GPL-3.0, as AMF) - see the file's header for the commit.
		constexpr RawRule kRawRules[] = {
#include "McmCategoryRules.inc"
		};

		// The separators, in the order a sort makes them. key = the rules' group; the TR key is "AMF_McmCat_" + key.
		struct Group
		{
			const char* key;
			const char* trKey;
			const char* english;
		};
		constexpr Group kGroups[] = {
			{ "Interface", "AMF_McmCat_Interface", "Interface" },
			{ "Controls", "AMF_McmCat_Controls", "Controls" },
			{ "Camera", "AMF_McmCat_Camera", "Camera" },
			{ "Combat", "AMF_McmCat_Combat", "Combat" },
			{ "Animation", "AMF_McmCat_Animation", "Animation" },
			{ "MagicSkills", "AMF_McmCat_MagicSkills", "Magic and Skills" },
			{ "Characters", "AMF_McmCat_Characters", "Characters and Bodies" },
			{ "NpcsCreatures", "AMF_McmCat_NpcsCreatures", "NPCs, Followers and Creatures" },
			{ "Audio", "AMF_McmCat_Audio", "Audio" },
			{ "QuestsPlaces", "AMF_McmCat_QuestsPlaces", "Quests and Places" },
			{ "WorldVisuals", "AMF_McmCat_WorldVisuals", "World and Visuals" },
			{ "Gameplay", "AMF_McmCat_Gameplay", "Gameplay" },
			{ "Utility", "AMF_McmCat_Utility", "Utilities and Fixes" },
			{ "Other", "AMF_McmCat_Other", "Other" },
		};
		constexpr std::size_t kOther = std::size(kGroups) - 1;

		struct Rule
		{
			std::regex rx;
			std::size_t group;
			float weight;
		};

		const std::vector<Rule>& Rules()
		{
			static std::once_flag once;
			static std::vector<Rule> rules;
			std::call_once(once, []() {
				int failed = 0;
				for (const RawRule& raw : kRawRules)
				{
					const auto g = std::find_if(std::begin(kGroups), std::end(kGroups), [&](const Group& x) { return std::string_view(x.key) == raw.group; });
					if (g == std::end(kGroups))
					{
						logger::warn("MCM sort: rule {} names no known group \"{}\" - skipped", raw.pattern, raw.group);
						++failed;
						continue;
					}
					try
					{
						rules.push_back({ std::regex(raw.pattern, std::regex::ECMAScript | std::regex::icase | std::regex::optimize),
							static_cast<std::size_t>(g - std::begin(kGroups)), raw.weight });
					}
					catch (const std::regex_error& e)
					{
						logger::warn("MCM sort: rule {} does not compile ({}) - skipped", raw.pattern, e.what());
						++failed;
					}
				}
				logger::info("MCM sort: {} name rules ready ({} skipped)", rules.size(), failed);
			});
			return rules;
		}

		// "TrueHUD (MCM)" -> "True HUD": the same steps as tools/try_mcm_categories.py prepare(). a_split false keeps the
		// words joined - "SkyUI" split is "Sky UI", which the weather rules claim, while the rule naming SkyUI wants it whole.
		std::string Prepare(std::string a_name, bool a_split)
		{
			static const std::regex suffix(R"(\s*\(MCM\)\s*$)");
			static const std::regex camel(R"(([a-z0-9])([A-Z]))");
			static const std::regex caps(R"(([A-Z]+)([A-Z][a-z]))");
			static const std::regex punct(R"([_\-.]+)");
			static const std::regex mcm(R"(\bmcm\b)", std::regex::ECMAScript | std::regex::icase);
			static const std::regex spaces(R"(\s+)");
			a_name = std::regex_replace(a_name, suffix, "");
			if (a_split)
			{
				a_name = std::regex_replace(a_name, camel, "$1 $2");
				a_name = std::regex_replace(a_name, caps, "$1 $2");
			}
			a_name = std::regex_replace(a_name, punct, " ");
			a_name = std::regex_replace(a_name, mcm, " ");
			a_name = std::regex_replace(a_name, spaces, " ");
			return detail::Trim(a_name);
		}

		// The names a menu is judged by: its entry, and the mod (MCM Helper) or plugin and ModName (script menu) in its key,
		// each whole and with its joined words split.
		std::string JudgedText(const std::string& a_key, const std::string& a_entry)
		{
			std::vector<std::string> parts{ a_entry };
			std::string rest = a_key.substr(a_key.find('|') == std::string::npos ? a_key.size() : a_key.find('|') + 1);
			for (std::size_t bar; !rest.empty(); rest = bar == std::string::npos ? std::string() : rest.substr(bar + 1))
			{
				bar = rest.find('|');
				std::string part = rest.substr(0, bar);
				for (const char* ext : { ".esp", ".esm", ".esl" })
				{
					if (part.size() > 4 && Lower(part.substr(part.size() - 4)) == ext) { part.resize(part.size() - 4); }
				}
				if (std::none_of(parts.begin(), parts.end(), [&](const std::string& p) { return Lower(p) == Lower(part); })) { parts.push_back(part); }
			}
			std::string text;
			for (const auto& p : parts)
			{
				const std::string whole = Prepare(p, false);
				const std::string split = Prepare(p, true);
				text += (text.empty() ? "" : " | ") + whole + (split == whole ? std::string() : " | " + split);
			}
			return text;
		}

		std::size_t GroupOf(const std::string& a_key, const std::string& a_entry)
		{
			const std::string text = JudgedText(a_key, a_entry);
			std::vector<float> score(std::size(kGroups), 0.0f);
			std::vector<bool> voted(std::size(kGroups), false);
			std::vector<std::size_t> firstVote;   // groups in the order they first scored, so a tie goes to the earlier rule
			for (const Rule& rule : Rules())
			{
				if (!std::regex_search(text, rule.rx)) { continue; }
				if (!voted[rule.group]) { firstVote.push_back(rule.group); }
				voted[rule.group] = true;
				score[rule.group] += rule.weight;
			}
			std::size_t best = kOther;
			for (const std::size_t g : firstVote)
			{
				if (best == kOther || score[g] > score[best]) { best = g; }
			}
			return best;
		}

		std::string GroupName(std::size_t a_group) { return TR(kGroups[a_group].trKey, kGroups[a_group].english); }

		// An existing separator for a group: one the player or an earlier sort named the group's shown name or its English
		// name, letter case aside.
		std::string FindSeparator(std::size_t a_group)
		{
			const std::string shown = Lower(GroupName(a_group));
			const std::string english = Lower(kGroups[a_group].english);
			for (const auto& sep : personalization::Separators())
			{
				const std::string name = Lower(detail::Trim(sep.name));
				if (name == shown || name == english) { return sep.id; }
			}
			return {};
		}

		std::mutex g_sortLock;   // one sort or restore at a time (the settings page and DevBench may both ask)

		// whether the undo preset exists: asked every frame the settings page is open, so read from disk once, then kept up
		// to date by the sort and the restore (-1 = not read yet)
		std::atomic<int> g_undoAvailable{ -1 };
	}

	std::string CategoryFor(const std::string& a_key, const std::string& a_entry) { return GroupName(GroupOf(a_key, a_entry)); }

	SortResult SortIntoCategories(bool a_all)
	{
		std::scoped_lock sortLock(g_sortLock);
		SortResult result;
		const std::string before = personalization::IniBlock();
		std::vector<registry::Entry> entries = registry::Snapshot();

		// where each menu sits now: under a separator (depth 1) or loose, and which separator by its shown name
		std::unordered_map<std::string, int> depth;
		std::unordered_map<std::string, std::string> groupBefore;
		std::string current;
		for (const auto& row : personalization::Order(entries))
		{
			if (row.separator) { current = Lower(detail::Trim(row.displayName)); }
			else
			{
				depth[row.modName] = row.depth;
				groupBefore[row.modName] = row.depth > 0 ? current : std::string();
			}
		}

		struct Move
		{
			std::string entry;
			std::size_t group;
		};
		std::vector<Move> moves;
		for (const auto& row : ImportList())
		{
			if (!row.imported) { continue; }
			const auto at = depth.find(row.entry);
			if (at == depth.end()) { continue; }   // not registered (yet) - nothing in the list to move
			if (!a_all && at->second > 0)
			{
				++result.kept;
				continue;
			}
			moves.push_back({ row.entry, GroupOf(row.key, row.entry) });
		}
		std::sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) {
			return a.group != b.group ? a.group < b.group : Lower(a.entry) < Lower(b.entry);
		});

		std::unordered_map<std::size_t, std::string> separatorOf;
		for (const Move& m : moves)
		{
			auto sep = separatorOf.find(m.group);
			if (sep == separatorOf.end())
			{
				std::string id = FindSeparator(m.group);
				if (id.empty())
				{
					id = personalization::AddSeparator(entries, "", GroupName(m.group));
					++result.separatorsMade;
				}
				sep = separatorOf.emplace(m.group, id).first;
			}
			if (personalization::SendTo(entries, m.entry, sep->second)) { result.placed.emplace_back(m.entry, GroupName(m.group)); }
		}

		const std::string after = personalization::IniBlock();
		result.changed = after != before;
		if (result.changed)
		{
			for (const auto& [entry, group] : result.placed) { result.moved += groupBefore[entry] == Lower(group) ? 0 : 1; }
			g_undoAvailable = settings::SaveLayoutPresetFrom(kMcmSortUndoPreset, before) ? 1 : 0;
			settings::Save();
		}
		logger::info("MCM sort ({}): {} menus sorted, {} changed group, {} left where they were, {} separators made{}",
			a_all ? "all" : "loose only", result.placed.size(), result.moved, result.kept, result.separatorsMade,
			result.changed ? "" : " - the list is as it was");
		return result;
	}

	bool CanRestoreBeforeSort()
	{
		if (g_undoAvailable < 0)
		{
			const auto names = settings::ListLayoutPresets();
			g_undoAvailable = std::find(names.begin(), names.end(), kMcmSortUndoPreset) != names.end() ? 1 : 0;
		}
		return g_undoAvailable > 0;
	}

	bool RestoreBeforeSort()
	{
		std::scoped_lock sortLock(g_sortLock);
		const bool loaded = settings::LoadLayoutPreset(kMcmSortUndoPreset);
		g_undoAvailable = 0;   // used up, or deleted from the layout presets meanwhile
		if (!loaded) { return false; }
		settings::DeleteLayoutPreset(kMcmSortUndoPreset);
		logger::info("MCM sort: the menu list is back to its order from before the sort");
		return true;
	}

	std::string SortToolJson(const std::string& a_argsJson)
	{
		json args;
		try { args = json::parse(a_argsJson.empty() ? "{}" : a_argsJson); }
		catch (...) { return R"({"ok":false,"error":"arguments are not JSON"})"; }
		const std::string action = args.contains("action") && args["action"].is_string() ? args["action"].get<std::string>() : std::string("preview");

		json out{ { "ok", true }, { "action", action }, { "rules", Rules().size() } };
		if (action == "run" || action == "all")
		{
			const SortResult r = SortIntoCategories(action == "all");
			out["sorted"] = r.placed.size();
			out["moved"] = r.moved;
			out["kept"] = r.kept;
			out["separatorsMade"] = r.separatorsMade;
			out["changed"] = r.changed;
		}
		else if (action == "undo") { out["restored"] = RestoreBeforeSort(); }
		else if (action != "preview")
		{
			return R"({"ok":false,"error":"sort needs action preview | run | all | undo"})";
		}

		json preview = json::object();   // every imported menu -> the separator a sort would give it
		for (const auto& row : ImportList())
		{
			if (row.imported) { preview[row.entry] = CategoryFor(row.key, row.entry); }
		}
		out["categories"] = preview;

		// the menu list as it stands: separators in order, each with its menus; "(loose)" for those under none
		json layout = json::array();
		json loose = json::array();
		json* current = &loose;
		const auto rows = personalization::Order(registry::Snapshot());
		for (const auto& row : rows)
		{
			if (row.separator)
			{
				layout.push_back({ { "separator", row.displayName }, { "id", row.modName }, { "menus", json::array() } });
				current = &layout.back()["menus"];
			}
			else { (row.depth > 0 ? *current : loose).push_back(row.displayName); }
		}
		out["separators"] = layout;
		out["loose"] = loose;
		out["undoAvailable"] = CanRestoreBeforeSort();
		return out.dump();
	}
}
