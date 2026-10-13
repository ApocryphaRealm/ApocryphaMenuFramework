// Off-game check of the Game Settings Override page's file reader and writer and its type mapping (include/GsoCore.h - the
// same code the page runs).
//   tools\gso_check.bat [<a folder of GSO .toml files>]
// The fixtures are byte strings in this file (so git's line-ending handling can never change them): the example on GSO's
// Nexus page (a byte-order mark, CRLF, a comment, a float, an integer, a quoted string with a comment after it), and one of
// our own with every value form the reader takes, LF endings and no final newline. With a folder, every .toml in it is also
// read and written back unchanged in memory (read only - nothing on disk is touched).
// Prints one line per check and "gso_check: N of N passed"; exits 1 on any failure.
#include "../include/GsoCore.h"

#include <filesystem>
#include <fstream>
#include <iostream>
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

	// our own: LF, no final newline, every form the reader takes
	const std::string kOwn =
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

	std::string Read(const std::filesystem::path& a_p)
	{
		std::ifstream in(a_p, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
		return ss.str();
	}

	gsocore::Document MustParse(const std::string& a_bytes, const std::string& a_name)
	{
		gsocore::Document d;
		gsocore::ParseError e;
		const bool ok = gsocore::Parse(a_bytes, d, e);
		Check(ok, a_name + " reads cleanly" + (ok ? "" : " (problem " + std::to_string(static_cast<int>(e.problem)) + " on line " + std::to_string(e.line) + ")"));
		return d;
	}

	// a_before and a_after differ in exactly one place: a_old replaced by a_new
	bool OnlyChange(const std::string& a_before, const std::string& a_after, const std::string& a_old, const std::string& a_new)
	{
		const auto at = a_before.find(a_old);
		if (at == std::string::npos) { return false; }
		std::string want = a_before;
		want.replace(at, a_old.size(), a_new);
		return want == a_after;
	}

	void WriteCase(const std::string& a_bytes, const std::string& a_name, const std::string& a_key, gsocore::Control a_control, const std::string& a_input,
				   const std::string& a_wantText)
	{
		const gsocore::Document d = MustParse(a_bytes, a_name);
		const gsocore::Entry* e = gsocore::Find(d, a_key);
		Check(e != nullptr, a_name + ": " + a_key + " found");
		if (!e) { return; }
		std::string text, err;
		const bool ok = gsocore::TextFor(a_control, e->value, a_input, text, err);
		Check(ok && text == a_wantText, a_name + ": " + a_key + " <- \"" + a_input + "\" as " + gsocore::ControlName(a_control) + " is written \"" + text + "\"" +
											(ok && text == a_wantText ? "" : " (want \"" + a_wantText + "\"" + (err.empty() ? "" : ", " + err) + ")"));
		std::string after;
		Check(gsocore::WithValue(d, a_key, text, after), a_name + ": " + a_key + " written");
		const std::string oldText = d.lines[e->line].substr(e->valueStart, e->valueEnd - e->valueStart);
		const std::string oldLine = d.lines[e->line];
		const std::string newLine = oldLine.substr(0, e->valueStart) + text + oldLine.substr(e->valueEnd);
		Check(OnlyChange(a_bytes, after, oldLine, newLine), a_name + ": only " + a_key + "'s value changed (\"" + oldText + "\" -> \"" + text + "\"), every other byte kept");
		gsocore::Document d2;
		gsocore::ParseError pe;
		Check(gsocore::Parse(after, d2, pe), a_name + ": still reads cleanly after the write");
		Check(d2.bom == d.bom && d2.endings == d.endings && d2.entries.size() == d.entries.size(), a_name + ": BOM, every line ending and the entry count kept");
		const gsocore::Entry* e2 = gsocore::Find(d2, a_key);
		Check(e2 && e2->comment == e->comment && e2->help == e->help, a_name + ": " + a_key + "'s comment and help kept");
	}

	void BadCase(const std::string& a_bytes, gsocore::Problem a_want, int a_line, const std::string& a_what)
	{
		gsocore::Document d;
		gsocore::ParseError e;
		const bool ok = gsocore::Parse(a_bytes, d, e);
		Check(!ok && e.problem == a_want && e.line == a_line, "not read: " + a_what + " (problem " + std::to_string(static_cast<int>(e.problem)) + ", line " +
																  std::to_string(e.line) + ")");
	}
}

int main(int argc, char** argv)
{
	using gsocore::Control;
	using gsocore::GameType;
	using gsocore::TomlType;

	// ---- round trip ----
	for (const auto& [bytes, name] : { std::pair{ kNexusExample, std::string("Nexus example") }, std::pair{ kOwn, std::string("own fixture") } })
	{
		const gsocore::Document d = MustParse(bytes, name);
		Check(gsocore::Serialize(d) == bytes, name + ": written back byte for byte with nothing changed");
	}

	// ---- what was read ----
	{
		const gsocore::Document d = MustParse(kNexusExample, "Nexus example");
		Check(d.bom, "Nexus example: BOM seen");
		Check(d.entries.size() == 3, "Nexus example: 3 settings");
		const auto* f = gsocore::Find(d, "fActiveEffectConditionUpdateInterval");
		Check(f && f->value.type == TomlType::kFloat && f->value.f == 1.0 && f->value.decimals == 6, "float 1.000000 (6 decimals)");
		const auto* i = gsocore::Find(d, "iActivatePickLength");
		Check(i && i->value.type == TomlType::kInt && i->value.i == 150, "int 150");
		const auto* s = gsocore::Find(d, "sAccept");
		Check(s && s->value.type == TomlType::kString && s->value.s == "Accept" && s->comment == "String should be quoted.", "string \"Accept\" with its comment");
		Check(f && f->help.empty(), "a comment block ended by a blank line is not the next key's help");
	}
	{
		const gsocore::Document d = MustParse(kOwn, "own fixture");
		Check(d.endings.back().empty() && d.endings.front() == "\n", "own fixture: LF endings, no final newline");
		const auto* a = gsocore::Find(d, "fJumpFallHeightMin");
		Check(a && a->help == "Fall damage\ntwo lines of help", "help from the two comment lines above");
		const auto* b = gsocore::Find(d, "bDisableGore");
		Check(b && b->value.type == TomlType::kBool && b->value.b, "bool true");
		const auto* n = gsocore::Find(d, "iMaxCount");
		Check(n && n->value.i == 1000, "1_000 reads as 1000");
		const auto* u = gsocore::Find(d, "uSomeCount");
		Check(u && u->value.i == 255 && u->value.base == 16, "0xFF reads as 255, base 16");
		const auto* c = gsocore::Find(d, "rColorHighlight");
		std::uint8_t parts[4]{};
		if (c) { gsocore::ColorParts(c->value.i, parts); }
		Check(c && parts[0] == 0xFF && parts[1] == 0x80 && parts[2] == 0x00 && parts[3] == 0xFF, "0xFF8000FF reads as r255 g128 b0 a255");
		const auto* t = gsocore::Find(d, "fTinyStep");
		Check(t && t->value.type == TomlType::kFloat && t->value.decimals == -1 && std::abs(t->value.f - 0.0025) < 1e-12, "2.5e-3 reads as 0.0025 (exponent form)");
		const auto* l = gsocore::Find(d, "sLiteral");
		Check(l && l->value.literal && l->value.s == "C:\\path\\kept", "a single-quoted string keeps its backslashes");
		const auto* e = gsocore::Find(d, "sEscaped");
		Check(e && e->value.s == "say \"hi\"\tthere \xC3\xA9", "escapes \\\" \\t \\u00e9 read");
		Check(gsocore::Find(d, "sIndented") != nullptr, "an indented key reads");
	}

	// ---- writing one value ----
	WriteCase(kNexusExample, "Nexus example", "fActiveEffectConditionUpdateInterval", Control::kFloat, "0.5", "0.500000");
	WriteCase(kNexusExample, "Nexus example", "iActivatePickLength", Control::kInt, "180", "180");
	WriteCase(kNexusExample, "Nexus example", "sAccept", Control::kText, "Take \"it\"", "\"Take \\\"it\\\"\"");
	WriteCase(kOwn, "own fixture", "fJumpFallHeightExponent", Control::kFloat, "1.5", "1.50");
	WriteCase(kOwn, "own fixture", "bDisableGore", Control::kSwitch, "0", "false");
	WriteCase(kOwn, "own fixture", "uSomeCount", Control::kUInt, "16", "0x10");
	WriteCase(kOwn, "own fixture", "rColorHighlight", Control::kColor, "#00FF0080", "0x00FF0080");
	WriteCase(kOwn, "own fixture", "sLiteral", Control::kText, "D:\\other", "'D:\\other'");
	WriteCase(kOwn, "own fixture", "sIndented", Control::kText, "last line, no newline", "\"last line, no newline\"");
	WriteCase(kOwn, "own fixture", "fTinyStep", Control::kFloat, "0.004", "0.004");
	{
		std::string text, err;
		gsocore::Value old;
		old.type = TomlType::kString;
		old.literal = true;
		Check(gsocore::TextFor(Control::kText, old, "it's", text, err) && text == "\"it's\"", "a single-quoted string that gains a ' is written double-quoted");
		gsocore::Value fl;
		fl.type = TomlType::kFloat;
		fl.decimals = 1;
		Check(gsocore::TextFor(Control::kFloat, fl, "0.125", text, err) && text == "0.125", "more decimals typed than the file had are kept (0.125)");
		Check(gsocore::TextFor(Control::kFloat, fl, "3", text, err) && text == "3.0", "a whole number for a decimal setting is written as a float (3.0)");
		Check(!gsocore::TextFor(Control::kInt, gsocore::Value{}, "2147483648", text, err), "an int setting refuses 2147483648");
		Check(!gsocore::TextFor(Control::kUInt, gsocore::Value{}, "-1", text, err), "a uint setting refuses -1");
		Check(!gsocore::TextFor(Control::kSwitch, gsocore::Value{}, "maybe", text, err), "a switch refuses \"maybe\"");
		Check(!gsocore::TextFor(Control::kText, gsocore::Value{}, "two\nlines", text, err), "a text refuses two lines");
		Check(!gsocore::TextFor(Control::kColor, gsocore::Value{}, "0x1FFFFFFFF", text, err), "a colour refuses more than 8 hex digits");
		Check(gsocore::FormatFloat(0.1f, -1) == "0.1", "a float with no decimals given is written shortest (0.1)");
		std::string unused;
		Check(!gsocore::WithValue(gsocore::Document{}, "fNotThere", "1.0", unused), "a key that is not in the file is not written");
	}

	// ---- type mapping ----
	Check(gsocore::GameTypeFromName("bDisableGore") == GameType::kBool && gsocore::GameTypeFromName("fJumpFallHeightMin") == GameType::kFloat &&
			  gsocore::GameTypeFromName("iActivatePickLength") == GameType::kInt && gsocore::GameTypeFromName("uMaxCount") == GameType::kUInt &&
			  gsocore::GameTypeFromName("sAccept") == GameType::kString && gsocore::GameTypeFromName("SDynamic") == GameType::kString &&
			  gsocore::GameTypeFromName("rColor") == GameType::kColor && gsocore::GameTypeFromName("aColorA") == GameType::kColor,
		  "the game's type from the EditorID's first letter: b f i u s S r a");
	Check(gsocore::GameTypeFromName("cChar") == GameType::kUnknown && gsocore::GameTypeFromName("hByte") == GameType::kUnknown &&
			  gsocore::GameTypeFromName("") == GameType::kUnknown,
		  "one-byte c / h settings and an empty name are not typed (never set in the game)");
	Check(gsocore::ControlFor(GameType::kBool, TomlType::kInt) == Control::kSwitch, "game bool -> switch (whatever the file holds)");
	Check(gsocore::ControlFor(GameType::kFloat, TomlType::kInt) == Control::kFloat, "game float -> decimal number box");
	Check(gsocore::ControlFor(GameType::kInt, TomlType::kInt) == Control::kInt, "game int -> whole number box");
	Check(gsocore::ControlFor(GameType::kUInt, TomlType::kInt) == Control::kUInt, "game uint -> whole number box, 0 or more");
	Check(gsocore::ControlFor(GameType::kColor, TomlType::kInt) == Control::kColor, "game color -> colour editor");
	Check(gsocore::ControlFor(GameType::kString, TomlType::kString) == Control::kText, "game string -> text box");
	Check(gsocore::ControlFor(GameType::kUnknown, TomlType::kFloat) == Control::kFloat, "unknown key, float in the file -> decimal number box");
	Check(gsocore::ControlFor(GameType::kUnknown, TomlType::kBool) == Control::kSwitch, "unknown key, bool in the file -> switch");
	Check(gsocore::ControlFor(GameType::kUnknown, TomlType::kString) == Control::kText, "unknown key, string in the file -> text box");
	Check(!gsocore::Matches(GameType::kFloat, TomlType::kInt), "an integer written for a float setting is flagged");
	Check(gsocore::Matches(GameType::kColor, TomlType::kInt) && gsocore::Matches(GameType::kUInt, TomlType::kInt), "colour and uint settings take an integer");
	Check(!gsocore::Matches(GameType::kString, TomlType::kInt), "an integer written for a string setting is flagged");
	Check(gsocore::Matches(GameType::kUnknown, TomlType::kString), "an unknown key is not a type mismatch (it is flagged as unknown)");

	// ---- what is not read ----
	BadCase("\xFF\xFE" "f\0", gsocore::Problem::kUtf16, 0, "UTF-16 text");
	BadCase("# x\n[General]\nfA = 1.0\n", gsocore::Problem::kTable, 2, "a [table]");
	BadCase("fA.b = 1.0\n", gsocore::Problem::kNotSetting, 1, "a dotted key");
	BadCase("\"fA\" = 1.0\n", gsocore::Problem::kNotSetting, 1, "a quoted key");
	BadCase("just words\n", gsocore::Problem::kNotSetting, 1, "a line that is not a setting");
	BadCase("fA = [1, 2]\n", gsocore::Problem::kValue, 1, "an array");
	BadCase("sA = \"\"\"long\"\"\"\n", gsocore::Problem::kValue, 1, "a multi-line string");
	BadCase("sA = \"never closed\n", gsocore::Problem::kValue, 1, "a string never closed");
	BadCase("iA = 150 extra\n", gsocore::Problem::kValue, 1, "text after the value");
	BadCase("iA = 0150\n", gsocore::Problem::kValue, 1, "a leading zero");
	BadCase("iA = 9223372036854775808\n", gsocore::Problem::kValue, 1, "an integer past 64 bits");
	BadCase("dA = 1979-05-27\n", gsocore::Problem::kValue, 1, "a date");
	BadCase("iA = 1\n\niA = 2\n", gsocore::Problem::kKeyTwice, 3, "a key given twice");
	{
		gsocore::Document d;
		gsocore::ParseError e;
		Check(gsocore::Parse("iA = 1\r\niB = 2", d, e) && gsocore::Parse("", d, e) && d.entries.empty(), "an empty file reads (no settings)");
		Check(gsocore::Parse("iA = 1\niB = 1\nia = 3\n", d, e), "fFoo and ffoo are two TOML keys (read)");
	}

	// ---- the files ----
	{
		std::vector<std::string> files = { "b.toml", "GSO - No Fall Damage.toml", "a.toml", "Z.toml" };
		gsocore::SortLoadOrder(files);
		Check(files == std::vector<std::string>{ "GSO - No Fall Damage.toml", "Z.toml", "a.toml", "b.toml" }, "load order by character code: G, Z, a, b");
		Check(gsocore::GsoReadsName("x.toml") && !gsocore::GsoReadsName("x.TOML") && !gsocore::GsoReadsName(".toml") && !gsocore::GsoReadsName("x.toml.bak"),
			  "GSO reads .toml in small letters only");
		gsocore::Document a, b;
		gsocore::ParseError e;
		gsocore::Parse("fShared = 1.0\nfOnlyA = 2.0\n", a, e);
		gsocore::Parse("FSHARED = 3.0\n", b, e);
		const std::vector<const gsocore::Document*> docs = { &a, &b };
		Check(gsocore::Winner(docs, "fShared") == 1, "the later file wins a shared key, case ignored");
		Check(gsocore::Winner(docs, "fOnlyA") == 0 && gsocore::Winner(docs, "fNone") == -1, "a key in one file is that file's; a key in none is -1");
		Check(gsocore::ParseIdList("").empty(), "an empty [GsoMenus] sOn turns no file on (each starts off)");
		const std::string on = gsocore::WithId("", "My File.toml", true);
		Check(on == "my file.toml" && gsocore::ParseIdList(on).contains("my file.toml"), "turning a file on stores its lower-case name");
		Check(gsocore::WithId(on + ",b.toml", "MY FILE.toml", false) == "b.toml", "turning it off again removes it");
	}

	// ---- a folder of real files (read only) ----
	if (argc > 1)
	{
		std::error_code ec;
		std::size_t seen = 0;
		for (const auto& de : std::filesystem::directory_iterator(argv[1], ec))
		{
			if (!de.is_regular_file() || !gsocore::GsoReadsName(de.path().filename().string())) { continue; }
			++seen;
			const std::string bytes = Read(de.path());
			gsocore::Document d;
			gsocore::ParseError pe;
			const bool ok = gsocore::Parse(bytes, d, pe);
			Check(ok, de.path().filename().string() + " reads (" + std::to_string(d.entries.size()) + " settings)" +
						  (ok ? "" : " - problem " + std::to_string(static_cast<int>(pe.problem)) + " line " + std::to_string(pe.line)));
			if (ok) { Check(gsocore::Serialize(d) == bytes, de.path().filename().string() + " written back byte for byte"); }
		}
		std::cout << "folder " << argv[1] << ": " << seen << " .toml file(s)\n";
	}

	std::cout << "gso_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
