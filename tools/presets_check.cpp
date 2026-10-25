// Off-game check of AMF's settings presets (include/PresetsCore.h) - the file format, the selection, the comparison shown
// before a load, the backup's name and the preset list - the same code the game runs.
//   tools\presets_check.bat [<a folder of preset .json files> ...]
// With folders, every .json in them is also read and written back in memory (read only, nothing on disk is touched) and must
// read again to the same groups. Prints one line per check and "presets_check: N of N passed"; exits 1 on any failure.
#include "../include/PresetsCore.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace
{
	int g_pass = 0, g_fail = 0;

	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	// A Remembered settings profile as 2.1.2 - 2.2.x wrote it (no presetVersion, no files).
	const char* kOldProfile = R"({
	"profile": "Default",
	"menus": {
		"script|Foo.esp|Foo": { "entry": "Foo", "autoRestore": true, "settings": [
			{ "type": "toggle", "value": "1", "page": "General", "text": "Enable", "nth": 0 },
			{ "type": "menu", "value": "Hard", "page": "General", "text": "Difficulty", "nth": 0, "menuIndex": 2 } ] },
		"mcmhelper|Bar": { "entry": "Bar", "autoRestore": false, "settings": [ { "type": "helper", "value": "5", "id": "iCount" } ] }
	}
})";

	presetscore::Preset Sample()
	{
		presetscore::Preset p;
		p.profile = "Hard";
		p.saved = "2026-10-25 01:30";
		p.amfVersion = "2.3.0";
		p.files.push_back({ "ini", "skse/plugins/foo.ini", "Foo (INI)", { { "General", "bEnable", "1" }, { "General", "fScale", "1.50" } } });
		p.files.push_back({ "toml", "skse/plugins/gone.toml", "Gone (TOML)", { { "", "Main.bX", "true" } } });
		p.files.push_back({ "prisma", "MyMod", "My Mod (Prisma)", { { "", "speed", "3" } } });
		p.files.push_back({ "mcmhelper", "Bar", "Bar", { { "", "iCount", "7" }, { "", "sName", "Lydia" } } });
		presetscore::MenuPart foo;
		foo.entry = "Foo";
		foo.settings.push_back({ "toggle", "General", "Enable", 0, "", "0", -1 });
		foo.settings.push_back({ "slider", "General", "Range", 1, "", "40", -1 });
		p.menus["script|Foo.esp|Foo"] = foo;
		presetscore::MenuPart helper;
		helper.entry = "Bar";
		helper.settings.push_back({ "helper", "", "", 0, "gBarGlobal", "2", -1 });
		helper.settings.push_back({ "helper", "", "", 0, "gGoneControl", "9", -1 });
		p.menus["mcmhelper|Bar"] = helper;
		p.extra["author"] = "a modlist author";
		return p;
	}

	void FolderRoundTrips(const std::filesystem::path& a_dir)
	{
		std::error_code ec;
		for (const auto& f : std::filesystem::directory_iterator(a_dir, ec))
		{
			if (f.path().extension() != ".json") { continue; }
			std::ifstream in(f.path(), std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			presetscore::Preset p, q;
			std::string err;
			if (!presetscore::Parse(ss.str(), p, err))
			{
				std::cout << "not read " << f.path().string() << " (" << err << ")\n";
				continue;
			}
			const bool again = presetscore::Parse(presetscore::Write(p), q, err);
			Check(again && presetscore::Groups(p).size() == presetscore::Groups(q).size() && presetscore::Write(p) == presetscore::Write(q),
				"real file round trip: " + f.path().filename().string() + " (" + std::to_string(presetscore::Groups(p).size()) + " groups)");
		}
	}
}

int main(int argc, char** argv)
{
	using namespace presetscore;
	std::string err;

	// ---- the format
	{
		Preset p;
		Check(Parse(kOldProfile, p, err), "a pre-2.3.0 Remembered settings profile reads as a preset");
		Check(p.version == kVersion && p.files.empty() && p.menus.size() == 2, "  ... with its two menus and no files");
		Check(p.menus["script|Foo.esp|Foo"].settings.size() == 2 && p.menus["script|Foo.esp|Foo"].settings[1].menuIndex == 2, "  ... records and a menu's index kept");
		Check(!p.menus["mcmhelper|Bar"].autoRestore, "  ... a menu switched out of the automatic restore stays out");
		const std::string w = Write(p);
		const json j = json::parse(w);
		Check(j["menus"]["script|Foo.esp|Foo"]["settings"][0].contains("page") && !j["menus"]["script|Foo.esp|Foo"]["settings"][0].contains("menuIndex"),
			"menus block written as Remembered settings writes it (menuIndex only on a menu row)");
		Check(j["menus"]["mcmhelper|Bar"]["settings"][0].contains("id") && !j["menus"]["mcmhelper|Bar"]["settings"][0].contains("page"),
			"an MCM Helper record keeps its id and no page");
	}
	{
		const Preset s = Sample();
		Preset q;
		Check(Parse(Write(s), q, err), "a full preset writes and reads back");
		Check(q.files.size() == 4 && q.menus.size() == 2 && q.profile == "Hard" && q.saved == s.saved && q.amfVersion == "2.3.0", "  ... every part kept");
		Check(q.files[0].values[1].section == "General" && q.files[0].values[1].value == "1.50", "  ... an INI value keeps its section and its text as written");
		Check(q.extra.contains("author") && Write(q) == Write(s), "  ... a key this version does not know survives, byte for byte");
	}
	{
		Preset p;
		Check(!Parse("not json", p, err) && !err.empty(), "text that is not JSON is refused with a reason");
		Check(!Parse("[1,2]", p, err), "a JSON array is refused");
		Check(!Parse(R"({"presetVersion": 99, "menus": {}})", p, err) && err.find("newer") != std::string::npos, "a newer presetVersion is refused, not half-read");
		Check(Parse(R"({"presetVersion":1,"files":[{"kind":"flick","id":"x","values":[{"name":"a","value":"1"}]},{"kind":"ini","id":"","values":[]},{"kind":"INI","id":"a.ini","values":[{"name":"","value":"1"},{"name":"k","value":2}]}]})", p, err),
			"a file with odd parts still reads");
		Check(p.files.size() == 1 && p.files[0].kind == "ini" && p.files[0].values.size() == 1 && p.files[0].values[0].value == "2",
			"  ... an unknown kind and an empty id are dropped, a kind's case does not matter, a value written as a number is text");
	}

	// ---- groups and selection
	{
		const Preset s = Sample();
		Check(GroupKey("ini", "SKSE/Plugins/Foo.ini") == "ini|skse/plugins/foo.ini", "a file's group is its kind and its id in small letters");
		Check(GroupKey("mcmhelper", "Bar") == "mcmhelper-ini|bar" && GroupKey("mcmhelper", "bar") != "mcmhelper|bar",
			"MCM Helper file values never share a group with the same mod's save-kept values");
		const auto groups = Groups(s);
		Check(groups.size() == 6, "six groups: four files, two menus");
		const Preset only = Select(s, { "ini|skse/plugins/foo.ini", "script|Foo.esp|Foo" });
		Check(only.files.size() == 1 && only.menus.size() == 1 && only.menus.contains("script|Foo.esp|Foo"), "a chosen set keeps only the mods chosen");
		Check(Select(s, {}).files.size() == 4, "an empty choice keeps every mod");
	}
	{
		std::map<std::string, MenuPart> base, over;
		base["k"].settings = { { "toggle", "P", "A", 0, "", "0", -1 }, { "slider", "P", "B", 0, "", "1", -1 } };
		over["k"].settings = { { "slider", "P", "B", 0, "", "9", -1 }, { "toggle", "P", "C", 0, "", "1", -1 } };
		over["n"].settings = { { "helper", "", "", 0, "x", "1", -1 } };
		MergeMenus(base, over);
		Check(base["k"].settings.size() == 3 && base["k"].settings[1].value == "9" && base["k"].settings[2].text == "C" && base["k"].settings[0].text == "A",
			"a live read merges over what was remembered: same setting in place, a new one last, the first-changed order kept");
		Check(base.contains("n"), "  ... and a menu only read now is added");
	}

	// ---- the comparison shown before a load
	{
		const Preset preset = Sample();
		Preset current;
		current.files.push_back({ "ini", "SKSE/Plugins/Foo.ini", "Foo (INI)", { { "general", "BENABLE", "1" }, { "General", "fScale", "1.00" } } });
		current.files.push_back({ "prisma", "MyMod", "My Mod (Prisma)", { { "", "speed", "3" } } });
		current.files.push_back({ "mcmhelper", "Bar", "Bar", { { "", "iCount", "5" } } });
		MenuPart helperNow;
		helperNow.settings = { { "helper", "", "", 0, "gBarGlobal", "2", -1 } };
		current.menus["mcmhelper|Bar"] = helperNow;
		// the script menu is installed but was not read (no game loaded): its rows say so
		const std::set<std::string> installed{ "ini|skse/plugins/foo.ini", "prisma|mymod", "mcmhelper-ini|bar", "mcmhelper|Bar", "script|Foo.esp|Foo" };
		const Diff d = Compare(preset, current, installed);
		auto find = [&](const std::string& a_setting) -> const DiffRow* {
			for (const auto& r : d.rows) { if (r.setting == a_setting) { return &r; } }
			return nullptr;
		};
		Check(d.notInstalled.size() == 1 && d.notInstalled[0].second == "Gone (TOML)", "a mod not installed is listed as skipped, never compared");
		const DiffRow* scale = find("General > fScale");
		Check(scale && scale->state == 0 && scale->current == "1.00" && scale->preset == "1.50", "a value that differs: current -> preset");
		Check(!find("General > bEnable") && !find("speed"), "a value already the same is not a row (matched by section and name, any case)");
		const DiffRow* count = find("iCount");
		const DiffRow* name = find("sName");
		Check(count && count->state == 0 && name && name->state == 1, "an MCM Helper file value: one differs, one is not in this game");
		const DiffRow* gone = find("gGoneControl");
		Check(gone && gone->state == 1 && !find("gBarGlobal"), "a save-kept value: one not found, one already the same");
		const DiffRow* enable = find("General > Enable");
		const DiffRow* range = find("General > Range (2)");
		Check(enable && enable->state == 2 && range && range->state == 2, "a script menu not read now: every row says 'not read' (the 2nd same-named row numbered)");
		Check(d.same == 3, "three settings are already the same");
		Check(d.changes.size() == 2 && d.changes.at("ini|skse/plugins/foo.ini") == 1 && d.changes.at("mcmhelper-ini|bar") == 1,
			"changes counted per mod (rows that cannot be read or found are not counted as changes)");
		Check(!d.changes.contains("mcmhelper|Bar"), "a mod whose only rows are 'not found' has no change to count");
		bool sorted = true;
		for (std::size_t i = 1; i < d.rows.size(); ++i) { sorted = sorted && detail::Lower(d.rows[i - 1].entry) <= detail::Lower(d.rows[i].entry); }
		Check(sorted, "rows sorted by mod name");
		const Diff none = Compare(preset, current, {});
		Check(none.rows.empty() && none.notInstalled.size() == 6, "with nothing installed, every mod is skipped and nothing would be written");
	}

	// ---- names and the list
	{
		std::tm t{};
		t.tm_year = 126; t.tm_mon = 9; t.tm_mday = 25; t.tm_hour = 1; t.tm_min = 5; t.tm_sec = 9;
		Check(BackupName(t) == "Backup 2026-10-25 01-05-09", "the backup before a load is named by the time, with no ':' (a file name)");
		Check(SavedStamp(t) == "2026-10-25 01:05", "the saved stamp");
		Check(SafeName("Easy: 1/2?") == "Easy 12" && SafeName("  ...  ") == "Default" && SafeName(" Hard. ") == "Hard", "a preset name is made safe as a file name, never empty");
		const auto list = MergeLists({ "Default", "hard", "Backup 2026-10-25 01-05-09", "Default" }, { "Njordlinger Recommended", "hard" });
		Check(list.size() == 5 && list[0].shipped && list[0].name == "hard" && list[1].shipped && !list[2].shipped,
			"shipped presets first (read only), then the player's; a name in both is listed twice, a duplicate once");
	}

	for (int i = 1; i < argc; ++i) { FolderRoundTrips(argv[i]); }

	std::cout << "presets_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
