// Off-game check of AMF's TOML pages (include/TomlCore.h), Game Settings Override's special case (include/GsoCore.h) and
// how a page finds its mod's declared reload (include/RefreshCore.h) - the same code the pages run.
//   tools\toml_check.bat [<a folder> ...]
// The fixtures are byte strings in this file (so git's line-ending handling can never change them). With folders, every
// .toml under them (any depth) is also read and written back unchanged in memory - read only, nothing on disk is touched.
// Prints one line per check and "toml_check: N of N passed"; exits 1 on any failure (a folder's file that does not read
// is listed as "not read", not a failure: a page then lists it with its reason).
#include "../include/GsoCore.h"
#include "../include/RefreshCore.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
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

	// GSO's Nexus page example, as published: a BOM, then CRLF lines
	const std::string kNexusExample =
		"\xEF\xBB\xBF# This is comment.\r\n"
		"\r\n"
		"fActiveEffectConditionUpdateInterval = 1.000000\r\n"
		"iActivatePickLength = 150\r\n"
		"sAccept = \"Accept\"  # String should be quoted.\r\n";

	// flat, LF, no final newline, every scalar form
	const std::string kFlat =
		"# Fall damage\n"
		"# two lines of help\n"
		"fJumpFallHeightMin = 600.0\n"
		"fJumpFallHeightExponent=1.45   # tighter spacing round '='\n"
		"\n"
		"bDisableGore = true\n"
		"iMaxCount = 1_000\n"
		"uSomeCount = 0xFF\n"
		"rColorHighlight = 0xFF8000FF\n"
		"fTinyStep = 2.5e-3\n"
		"fNegative = -0.25\n"
		"sLiteral = 'C:\\path\\kept'\n"
		"sEscaped = \"say \\\"hi\\\"\\tthere \\u00e9\"\n"
		"\t  sIndented = \"indented key\"";

	// a general config: tables, sub-tables, arrays of tables, dotted and quoted keys, arrays over lines with comments,
	// inline tables, multi-line strings, dates - CRLF, as most of Njordlinger's TOMLs are
	const std::string kGeneral =
		"# Engine-Fixes-like header\r\n"
		"top = 1\r\n"
		"\r\n"
		"# the General table\r\n"
		"[General]\r\n"
		"bVerboseLogging = false                         # enable extra log levels\r\n"
		"fScale = 1.25\r\n"
		"name = \"Main\"\r\n"
		"physics.gravity = 9.81\r\n"
		"\"quoted key\" = 'lit'\r\n"
		"when = 1979-05-27T07:32:00Z\r\n"
		"day = 1979-05-27\r\n"
		"at = 07:32:00\r\n"
		"local = 1979-05-27 07:32:00\r\n"
		"\r\n"
		"[General.Sub]\r\n"
		"list = [1, 2, 3]\r\n"
		"long = [\r\n"
		"  \"a\", # first\r\n"
		"  \"b\",\r\n"
		"]\r\n"
		"point = { x = 1, y = 2 }\r\n"
		"notes = \"\"\"\r\n"
		"two\r\n"
		"lines\"\"\"\r\n"
		"after = 5\r\n"
		"\r\n"
		"[[Attack]]\r\n"
		"name = \"Slash\"\r\n"
		"damage = 10\r\n"
		"\r\n"
		"[[Attack]]\r\n"
		"name = \"Thrust\"\r\n"
		"damage = 12\r\n"
		"[Attack.Extra]\r\n"
		"bonus = 0.5\r\n";

	std::string Read(const std::filesystem::path& a_p)
	{
		std::ifstream in(a_p, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}

	tomlcore::Document MustParse(const std::string& a_bytes, const std::string& a_name)
	{
		tomlcore::Document d;
		tomlcore::ParseError e;
		const bool ok = tomlcore::Parse(a_bytes, d, e);
		Check(ok, a_name + " reads cleanly" + (ok ? "" : " (problem " + std::to_string(static_cast<int>(e.problem)) + " on line " + std::to_string(e.line) + ")"));
		return d;
	}

	bool OnlyChange(const std::string& a_before, const std::string& a_after, const std::string& a_old, const std::string& a_new)
	{
		const auto at = a_before.find(a_old);
		if (at == std::string::npos) { return false; }
		std::string want = a_before;
		want.replace(at, a_old.size(), a_new);
		return want == a_after;
	}

	void WriteCase(const std::string& a_bytes, const std::string& a_name, const std::string& a_id, tomlcore::Control a_control, const std::string& a_input,
				   const std::string& a_wantText)
	{
		const tomlcore::Document d = MustParse(a_bytes, a_name);
		const tomlcore::Entry* e = tomlcore::Find(d, a_id);
		Check(e != nullptr, a_name + ": " + a_id + " found");
		if (!e) { return; }
		std::string text, err;
		const bool ok = tomlcore::TextFor(a_control, e->value, a_input, text, err);
		Check(ok && text == a_wantText, a_name + ": " + a_id + " <- \"" + a_input + "\" as " + tomlcore::ControlName(a_control) + " is written \"" + text + "\"" +
											(ok && text == a_wantText ? "" : " (want \"" + a_wantText + "\"" + (err.empty() ? "" : ", " + err) + ")"));
		std::string after;
		Check(tomlcore::WithValue(d, a_id, text, after), a_name + ": " + a_id + " written");
		const std::string oldLine = d.lines[e->line];
		const std::string newLine = oldLine.substr(0, e->valueStart) + text + oldLine.substr(e->valueEnd);
		Check(OnlyChange(a_bytes, after, oldLine, newLine), a_name + ": only " + a_id + "'s value changed (\"" + e->raw + "\" -> \"" + text + "\"), every other byte kept");
		tomlcore::Document d2;
		tomlcore::ParseError pe;
		Check(tomlcore::Parse(after, d2, pe), a_name + ": still reads cleanly after the write");
		Check(d2.bom == d.bom && d2.endings == d.endings && d2.entries.size() == d.entries.size() && d2.sections.size() == d.sections.size(),
			  a_name + ": BOM, every line ending, the entries and the tables kept");
		const tomlcore::Entry* e2 = tomlcore::Find(d2, a_id);
		Check(e2 && e2->comment == e->comment && e2->help == e->help && e2->raw == text, a_name + ": " + a_id + " reads back as written, comment and help kept");
	}

	void BadCase(const std::string& a_bytes, tomlcore::Problem a_want, int a_line, const std::string& a_what)
	{
		tomlcore::Document d;
		tomlcore::ParseError e;
		const bool ok = tomlcore::Parse(a_bytes, d, e);
		Check(!ok && e.problem == a_want && e.line == a_line, "not read: " + a_what + " (problem " + std::to_string(static_cast<int>(e.problem)) + ", line " +
																  std::to_string(e.line) + ")");
	}

	void Folder(const std::filesystem::path& a_dir)
	{
		std::error_code ec;
		std::size_t seen = 0, read = 0, editable = 0, shown = 0;
		std::map<std::string, int> problems;
		for (auto it = std::filesystem::recursive_directory_iterator(a_dir, std::filesystem::directory_options::skip_permission_denied, ec);
			 it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
		{
			if (ec) { break; }
			const auto& p = it->path();
			std::string ext = p.extension().string();
			for (char& c : ext) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			if (!it->is_regular_file(ec) || ext != ".toml") { continue; }
			++seen;
			const std::string bytes = Read(p);
			tomlcore::Document d;
			tomlcore::ParseError pe;
			if (!tomlcore::Parse(bytes, d, pe))
			{
				std::cout << "not read: " << p.string() << " - problem " << static_cast<int>(pe.problem) << " line " << pe.line << "\n";
				++problems[std::to_string(static_cast<int>(pe.problem))];
				continue;
			}
			++read;
			for (const auto& e : d.entries) { (e.Editable() ? editable : shown)++; }
			Check(tomlcore::Serialize(d) == bytes, p.string() + " written back byte for byte (" + std::to_string(d.entries.size()) + " keys, " +
													   std::to_string(d.sections.size() - 1) + " tables)");
			// every editable value written back as it already is: the file is byte-identical
			bool same = true;
			for (const auto& e : d.entries)
			{
				if (!e.Editable()) { continue; }
				std::string after;
				same = same && tomlcore::WithValue(d, e.id, e.raw, after) && after == bytes;
			}
			Check(same, p.string() + ": each value rewritten as it is leaves the file unchanged");
		}
		std::cout << "folder " << a_dir.string() << ": " << seen << " .toml, " << read << " read, " << editable << " editable values, " << shown
				  << " shown only\n";
	}
}

int main(int argc, char** argv)
{
	using gsocore::GameType;
	using tomlcore::Control;
	using tomlcore::TomlType;

	// ---- round trip ----
	for (const auto& [bytes, name] : { std::pair{ kNexusExample, std::string("Nexus example") }, std::pair{ kFlat, std::string("flat fixture") },
									   std::pair{ kGeneral, std::string("general fixture") } })
	{
		const tomlcore::Document d = MustParse(bytes, name);
		Check(tomlcore::Serialize(d) == bytes, name + ": written back byte for byte with nothing changed");
	}

	// ---- flat files (GSO) ----
	{
		const auto d = MustParse(kNexusExample, "Nexus example");
		Check(d.bom && d.entries.size() == 3 && d.sections.size() == 1, "Nexus example: BOM, 3 settings, no table");
		const auto* f = tomlcore::Find(d, "fActiveEffectConditionUpdateInterval");
		Check(f && f->value.type == TomlType::kFloat && f->value.f == 1.0 && f->value.decimals == 6, "float 1.000000 (6 decimals)");
		const auto* s = tomlcore::Find(d, "sAccept");
		Check(s && s->value.s == "Accept" && s->comment == "String should be quoted.", "string \"Accept\" with its comment");
		Check(f && f->help.empty(), "a comment block ended by a blank line is not the next key's help");
	}
	{
		const auto d = MustParse(kFlat, "flat fixture");
		Check(d.endings.back().empty() && d.endings.front() == "\n", "flat fixture: LF endings, no final newline");
		const auto* a = tomlcore::Find(d, "fJumpFallHeightMin");
		Check(a && a->help == "Fall damage\ntwo lines of help", "help from the two comment lines above");
		const auto* u = tomlcore::Find(d, "uSomeCount");
		Check(u && u->value.i == 255 && u->value.base == 16, "0xFF reads as 255, base 16");
		const auto* c = tomlcore::Find(d, "rColorHighlight");
		std::uint8_t parts[4]{};
		if (c) { tomlcore::ColorParts(c->value.i, parts); }
		Check(c && parts[0] == 0xFF && parts[1] == 0x80 && parts[2] == 0x00 && parts[3] == 0xFF, "0xFF8000FF reads as r255 g128 b0 a255");
		const auto* t = tomlcore::Find(d, "fTinyStep");
		Check(t && t->value.decimals == -1 && std::abs(t->value.f - 0.0025) < 1e-12, "2.5e-3 reads as 0.0025 (exponent form)");
		const auto* e = tomlcore::Find(d, "sEscaped");
		Check(e && e->value.s == "say \"hi\"\tthere \xC3\xA9", "escapes \\\" \\t \\u00e9 read");
	}
	WriteCase(kNexusExample, "Nexus example", "fActiveEffectConditionUpdateInterval", Control::kFloat, "0.5", "0.500000");
	WriteCase(kNexusExample, "Nexus example", "iActivatePickLength", Control::kInt, "180", "180");
	WriteCase(kNexusExample, "Nexus example", "sAccept", Control::kText, "Take \"it\"", "\"Take \\\"it\\\"\"");
	WriteCase(kFlat, "flat fixture", "fJumpFallHeightExponent", Control::kFloat, "1.5", "1.50");
	WriteCase(kFlat, "flat fixture", "bDisableGore", Control::kSwitch, "0", "false");
	WriteCase(kFlat, "flat fixture", "uSomeCount", Control::kUInt, "16", "0x10");
	WriteCase(kFlat, "flat fixture", "rColorHighlight", Control::kColor, "#00FF0080", "0x00FF0080");
	WriteCase(kFlat, "flat fixture", "sLiteral", Control::kText, "D:\\other", "'D:\\other'");
	WriteCase(kFlat, "flat fixture", "sIndented", Control::kText, "last line, no newline", "\"last line, no newline\"");
	WriteCase(kFlat, "flat fixture", "fTinyStep", Control::kFloat, "0.004", "0.004");

	// ---- a general file ----
	{
		const auto d = MustParse(kGeneral, "general fixture");
		std::string names;
		for (const auto& s : d.sections) { names += "[" + s.name + "]"; }
		Check(names == "[][General][General > Sub][Attack 1][Attack 2][Attack 2 > Extra]", "tables in order, arrays of tables numbered: " + names);
		Check(d.sections[1].help == "the General table", "the comment above a table is its help");
		const auto* top = tomlcore::Find(d, "top");
		Check(top && top->section == 0 && top->value.i == 1, "a key before any table is at the top");
		const auto* v = tomlcore::Find(d, "General.bVerboseLogging");
		Check(v && v->value.type == TomlType::kBool && !v->value.b && v->comment == "enable extra log levels", "General.bVerboseLogging false, its comment as help");
		Check(tomlcore::Find(d, "General.physics.gravity") != nullptr, "a dotted key's id: General.physics.gravity");
		const auto* q = tomlcore::Find(d, "General.\"quoted key\"");
		Check(q && q->value.literal && q->value.s == "lit", "a quoted key keeps its quotes in the id; a literal string");
		const auto* when = tomlcore::Find(d, "General.when");
		const auto* day = tomlcore::Find(d, "General.day");
		const auto* at = tomlcore::Find(d, "General.at");
		const auto* local = tomlcore::Find(d, "General.local");
		Check(when && day && at && local && when->value.type == TomlType::kDateTime && day->value.type == TomlType::kDateTime &&
				  at->value.type == TomlType::kDateTime && local->value.type == TomlType::kDateTime && local->raw == "1979-05-27 07:32:00",
			  "offset date-time, date, time and a date-time parted by a space read as dates");
		const auto* list = tomlcore::Find(d, "General.Sub.list");
		const auto* lng = tomlcore::Find(d, "General.Sub.long");
		const auto* pt = tomlcore::Find(d, "General.Sub.point");
		const auto* notes = tomlcore::Find(d, "General.Sub.notes");
		const auto* after = tomlcore::Find(d, "General.Sub.after");
		Check(list && list->value.type == TomlType::kArray && list->value.items == 3 && !list->Editable(), "a one-line array: 3 items, shown only");
		Check(lng && lng->value.type == TomlType::kArray && lng->value.items == 2 && lng->endLine == lng->line + 3, "an array over 4 lines with a comment inside: 2 items");
		Check(pt && pt->value.type == TomlType::kInlineTable && pt->value.items == 2 && !pt->Editable(), "an inline table: 2 items, shown only");
		Check(notes && notes->value.multiline && !notes->Editable() && notes->endLine == notes->line + 2, "a multi-line string over 3 lines, shown only");
		Check(after && after->value.i == 5, "the key after a multi-line string reads (after = 5)");
		const auto* d1 = tomlcore::Find(d, "Attack[0].damage");
		const auto* d2 = tomlcore::Find(d, "Attack[1].damage");
		const auto* bonus = tomlcore::Find(d, "Attack[1].Extra.bonus");
		Check(d1 && d2 && d1->value.i == 10 && d2->value.i == 12, "each array-of-tables element keeps its own keys (10, 12)");
		Check(bonus && bonus->value.f == 0.5, "a sub-table of the last element: Attack[1].Extra.bonus");
		Check(tomlcore::ControlForValue(*v) == Control::kSwitch && tomlcore::ControlForValue(*d1) == Control::kInt64 &&
				  tomlcore::ControlForValue(*bonus) == Control::kFloat && tomlcore::ControlForValue(*q) == Control::kText &&
				  tomlcore::ControlForValue(*day) == Control::kDate && tomlcore::ControlForValue(*list) == Control::kReadOnly,
			  "controls: bool switch, int number, float number, string text, date text, array read-only");
		std::string unused;
		Check(!tomlcore::WithValue(d, "General.Sub.list", "[4]", unused) && !tomlcore::WithValue(d, "General.Sub.notes", "\"x\"", unused),
			  "an array and a multi-line string are never written");
	}
	WriteCase(kGeneral, "general fixture", "General.bVerboseLogging", Control::kSwitch, "true", "true");
	WriteCase(kGeneral, "general fixture", "General.fScale", Control::kFloat, "1.5", "1.50");
	WriteCase(kGeneral, "general fixture", "General.\"quoted key\"", Control::kText, "new", "'new'");
	WriteCase(kGeneral, "general fixture", "General.physics.gravity", Control::kFloat, "9.8", "9.80");
	WriteCase(kGeneral, "general fixture", "General.day", Control::kDate, "2026-10-13", "2026-10-13");
	WriteCase(kGeneral, "general fixture", "General.Sub.after", Control::kInt64, "9000000000", "9000000000");
	WriteCase(kGeneral, "general fixture", "Attack[1].damage", Control::kInt64, "15", "15");
	WriteCase(kGeneral, "general fixture", "Attack[1].Extra.bonus", Control::kFloat, "0.75", "0.75");
	{
		std::string text, err;
		tomlcore::Value old;
		old.type = TomlType::kString;
		old.literal = true;
		Check(tomlcore::TextFor(Control::kText, old, "it's", text, err) && text == "\"it's\"", "a single-quoted string that gains a ' is written double-quoted");
		tomlcore::Value fl;
		fl.type = TomlType::kFloat;
		fl.decimals = 1;
		Check(tomlcore::TextFor(Control::kFloat, fl, "0.125", text, err) && text == "0.125", "more decimals typed than the file had are kept (0.125)");
		Check(tomlcore::TextFor(Control::kFloat, fl, "3", text, err) && text == "3.0", "a whole number for a decimal is written as a float (3.0)");
		Check(!tomlcore::TextFor(Control::kInt, tomlcore::Value{}, "2147483648", text, err), "a 32-bit game int refuses 2147483648");
		Check(tomlcore::TextFor(Control::kInt64, tomlcore::Value{}, "2147483648", text, err), "a TOML integer takes 2147483648");
		Check(!tomlcore::TextFor(Control::kUInt, tomlcore::Value{}, "-1", text, err), "a uint refuses -1");
		Check(!tomlcore::TextFor(Control::kSwitch, tomlcore::Value{}, "maybe", text, err), "a switch refuses \"maybe\"");
		Check(!tomlcore::TextFor(Control::kText, tomlcore::Value{}, "two\nlines", text, err), "a text refuses two lines");
		Check(!tomlcore::TextFor(Control::kDate, tomlcore::Value{}, "27/05/1979", text, err), "a date refuses 27/05/1979");
		Check(!tomlcore::TextFor(Control::kReadOnly, tomlcore::Value{}, "1", text, err), "a read-only value refuses any write");
		Check(tomlcore::FormatFloat(0.1f, -1) == "0.1", "a float with no decimals given is written shortest (0.1)");
	}

	// ---- what is not read ----
	BadCase("\xFF\xFE" "f\0", tomlcore::Problem::kUtf16, 0, "UTF-16 text");
	BadCase("just words\n", tomlcore::Problem::kNotSetting, 1, "a line that is not a setting");
	BadCase("[General\nx = 1\n", tomlcore::Problem::kNotSetting, 1, "a table header never closed");
	BadCase("[a]\nx = 1\n[a]\ny = 2\n", tomlcore::Problem::kTableTwice, 3, "a table given twice");
	BadCase("iA = 1\n\niA = 2\n", tomlcore::Problem::kKeyTwice, 3, "a key given twice");
	BadCase("[t]\na.b = 1\na.b = 2\n", tomlcore::Problem::kKeyTwice, 3, "a dotted key given twice");
	BadCase("sA = \"never closed\n", tomlcore::Problem::kUnclosed, 1, "a string never closed");
	BadCase("a = [1, 2\nb = 3\n", tomlcore::Problem::kValue, 1, "an array missing its ']' (the next line is not an item; the line the value starts on)");
	BadCase("a = [1, 2,\n", tomlcore::Problem::kUnclosed, 1, "an array at the end of the file never closed");
	BadCase("s = \"\"\"never\nclosed\n", tomlcore::Problem::kUnclosed, 1, "a multi-line string never closed");
	BadCase("iA = 150 extra\n", tomlcore::Problem::kValue, 1, "text after the value");
	BadCase("iA = 0150\n", tomlcore::Problem::kValue, 1, "a leading zero");
	BadCase("iA = 9223372036854775808\n", tomlcore::Problem::kValue, 1, "an integer past 64 bits");
	BadCase("d = 1979-13\n", tomlcore::Problem::kValue, 1, "half a date");
	{
		tomlcore::Document d;
		tomlcore::ParseError e;
		Check(tomlcore::Parse("", d, e) && d.entries.empty(), "an empty file reads (no settings)");
		Check(tomlcore::Parse("[[a]]\nx = 1\n[[a]]\nx = 2\n", d, e) && d.entries.size() == 2, "the same key in two array-of-tables elements is not a repeat");
	}

	// ---- GSO: types and the files ----
	Check(gsocore::GameTypeFromName("bDisableGore") == GameType::kBool && gsocore::GameTypeFromName("fJumpFallHeightMin") == GameType::kFloat &&
			  gsocore::GameTypeFromName("iActivatePickLength") == GameType::kInt && gsocore::GameTypeFromName("uMaxCount") == GameType::kUInt &&
			  gsocore::GameTypeFromName("sAccept") == GameType::kString && gsocore::GameTypeFromName("SDynamic") == GameType::kString &&
			  gsocore::GameTypeFromName("rColor") == GameType::kColor && gsocore::GameTypeFromName("aColorA") == GameType::kColor,
		  "the game's type from the EditorID's first letter: b f i u s S r a");
	Check(gsocore::GameTypeFromName("cChar") == GameType::kUnknown && gsocore::GameTypeFromName("") == GameType::kUnknown, "c / h / empty: not typed");
	{
		const auto d = MustParse(kNexusExample, "Nexus example");
		const auto& f = *tomlcore::Find(d, "fActiveEffectConditionUpdateInterval");
		const auto& s = *tomlcore::Find(d, "sAccept");
		Check(gsocore::ControlFor(GameType::kBool, f) == Control::kSwitch && gsocore::ControlFor(GameType::kFloat, f) == Control::kFloat &&
				  gsocore::ControlFor(GameType::kInt, f) == Control::kInt && gsocore::ControlFor(GameType::kUInt, f) == Control::kUInt &&
				  gsocore::ControlFor(GameType::kColor, f) == Control::kColor && gsocore::ControlFor(GameType::kString, s) == Control::kText,
			  "GSO: the control by the game setting's type");
		Check(gsocore::ControlFor(GameType::kUnknown, f) == Control::kFloat && gsocore::ControlFor(GameType::kUnknown, s) == Control::kText,
			  "GSO: an unknown key by the value the file holds");
	}
	Check(!gsocore::Matches(GameType::kFloat, TomlType::kInt) && gsocore::Matches(GameType::kColor, TomlType::kInt) &&
			  gsocore::Matches(GameType::kUnknown, TomlType::kString),
		  "GSO: an integer for a float setting is flagged; colour takes an integer; unknown is not a mismatch");
	{
		std::vector<std::string> files = { "b.toml", "GSO - No Fall Damage.toml", "a.toml", "Z.toml" };
		gsocore::SortLoadOrder(files);
		Check(files == std::vector<std::string>{ "GSO - No Fall Damage.toml", "Z.toml", "a.toml", "b.toml" }, "GSO load order by character code: G, Z, a, b");
		Check(gsocore::GsoReadsName("x.toml") && !gsocore::GsoReadsName("x.TOML") && !gsocore::GsoReadsName(".toml"), "GSO reads .toml in small letters only");
		tomlcore::Document a, b;
		tomlcore::ParseError e;
		tomlcore::Parse("fShared = 1.0\nfOnlyA = 2.0\n", a, e);
		tomlcore::Parse("FSHARED = 3.0\n", b, e);
		const std::vector<const tomlcore::Document*> docs = { &a, &b };
		Check(gsocore::Winner(docs, "fShared") == 1 && gsocore::Winner(docs, "fOnlyA") == 0 && gsocore::Winner(docs, "fNone") == -1,
			  "GSO: the later file wins a shared key, case ignored");
	}
	Check(tomlcore::ParseIdList("").empty() && tomlcore::WithId("", "SKSE/Plugins/X.toml", true) == "skse/plugins/x.toml" &&
			  tomlcore::WithId("a,skse/plugins/x.toml", "skse/plugins/X.toml", false) == "a",
		  "page switches: empty = none on (each starts off); on and off by lower-case id");

	// ---- which page a declared reload belongs to ----
	{
		using refreshcore::Decl;
		const std::vector<Decl> decls = {
			{ "Mod/Toggle UI/Toggle UI.toml", "ccld_ToggleUI.dll", "ToggleUI", 0 },       // a Mod button - never a reload
			{ "Config/Toggle UI.toml", "ccld_ToggleUI.dll", "ReloadConfig", 2 },
			{ "Config/Mod Function Menu.toml", "ccld_ModFunctionMenu.dll", "ReloadConfig", 2 },
			{ "Config/Open Settings.toml", "SomeMod.dll", "OpenSettings", 0 },            // Config, but not a reload
			{ "(Game Settings Override)", "ccld_GameSettingsOverride.dll", "ReloadConfig", 2 },
		};
		Check(refreshcore::Match(decls, refreshcore::Candidates("ccld_ToggleUI", "")) == 1, "ccld_ToggleUI.toml -> Toggle UI's Config reload (not its Mod button)");
		Check(refreshcore::Match(decls, refreshcore::Candidates("ccld_ModFunctionMenu_styles", "")) == 2, "ccld_ModFunctionMenu_styles.toml -> MFM's reload (name_ prefix)");
		Check(refreshcore::Match(decls, refreshcore::Candidates("GSO - No Fall Damage", "ccld_GameSettingsOverride")) == 4, "a GSO file -> GSO's ReloadConfig, by its folder");
		Check(refreshcore::Match(decls, refreshcore::Candidates("SomeMod", "")) == -1, "a Config entry that does not reload is never matched");
		Check(refreshcore::Match(decls, refreshcore::Candidates("EngineFixes", "")) == -1, "a mod with no declaration: none");
		Check(refreshcore::Match(decls, refreshcore::Candidates("ccld_ToggleUIx", "")) == -1, "a longer name without '_' does not match");
		Check(refreshcore::Match(decls, refreshcore::Candidates("CCLD_TOGGLEUI", "")) == 1, "case is ignored");
		Check(refreshcore::ShownName(decls[1]) == "Toggle UI" && refreshcore::ShownName(decls[4]) == "Game Settings Override", "shown names: Toggle UI, Game Settings Override");
		Check(refreshcore::EngineIniName("fMouseHeadingSens", "Controls") == "fMouseHeadingSens:Controls", "an engine INI setting's name: key:Section");
	}

	for (int i = 1; i < argc; ++i) { Folder(argv[i]); }

	std::cout << "toml_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
