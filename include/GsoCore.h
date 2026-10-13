#pragma once

// ============================================================================================
// GAME SETTINGS OVERRIDE FILES - the parts with no game or ImGui code, so the offline check (tools\gso_check.cpp) runs the
// same file the page does: a reader and writer for the flat TOML subset Game Settings Override's files use, the control a
// setting gets from the game setting's type, the text a value is written back as, and the order the files load in.
//
// The format, as GSO's Nexus page (114911) documents it and its published files show it - read for the format only; GSO is
// GPL-3.0 and none of its code is used here:
//   Data/SKSE/Plugins/ccld_GameSettingsOverride/*.toml (top level; the extension in small letters), one "EditorID = value"
//   per line, "#" comments (on their own line or after a value), bool / integer / float / quoted string values, colours
//   as an integer 0xRRGGBBAA. The files load in A-Z order and the later one wins.
//
// What this reader takes is deliberately narrower than TOML: bare keys only, one per line, no [tables], no dotted or
// quoted keys, no arrays, inline tables, dates or multi-line strings. A file with anything else is not read (the page lists
// it with the line and the reason) - so nothing is ever written into a file this reader did not fully understand.
//
// The writer changes ONE value's text on its line: the key, the spacing, a comment after the value, every other line, the
// byte-order mark and each line's own ending are written back byte for byte as they were read.
// ============================================================================================

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace gsocore
{
	enum class TomlType { kNone, kBool, kInt, kFloat, kString };

	// The game setting's type.
	enum class GameType { kUnknown = 0, kBool, kFloat, kInt, kColor, kString, kUInt };

	// A game setting's type is its EditorID's first letter - the engine's own rule (CommonLib's Setting::GetType reads it so
	// on both build lines, whose type enums differ, so the page reads the letter itself): b bool, f float, i int, u uint,
	// s / S string, r (RGB) / a (RGBA) colour. c and h (one-byte numbers) are left unknown: a write of four bytes would not
	// fit them, so the page never sets them in the game.
	inline GameType GameTypeFromName(const std::string& a_editorId)
	{
		if (a_editorId.empty()) { return GameType::kUnknown; }
		switch (a_editorId[0])
		{
		case 'b': return GameType::kBool;
		case 'f': return GameType::kFloat;
		case 'i': return GameType::kInt;
		case 'u': return GameType::kUInt;
		case 's':
		case 'S': return GameType::kString;
		case 'r':
		case 'a': return GameType::kColor;
		default: return GameType::kUnknown;
		}
	}

	// What the page draws for a setting.
	enum class Control { kSwitch, kFloat, kInt, kUInt, kColor, kText };

	struct Value
	{
		TomlType type = TomlType::kNone;
		bool b = false;
		long long i = 0;
		double f = 0.0;
		std::string s;
		int base = 10;          // an integer's base as written: 10, 16, 8 or 2
		int decimals = -1;      // a float's digits after the '.', as written; -1 = written with an exponent, or inf / nan
		bool literal = false;   // a string written in single quotes
	};

	struct Entry
	{
		std::size_t line = 0;         // index in Document::lines
		std::string key;
		std::size_t valueStart = 0;   // the value's text in the line: [valueStart, valueEnd)
		std::size_t valueEnd = 0;
		Value value;
		std::string comment;          // the "# ..." after the value, without the mark
		std::string help;             // the comment lines directly above (a blank line ends a block)
	};

	struct Document
	{
		bool bom = false;
		std::vector<std::string> lines;     // each line's text, without its ending
		std::vector<std::string> endings;   // each line's own ending: "\r\n", "\n", or "" (the last line, when the file has none)
		std::vector<Entry> entries;
	};

	enum class Problem { kNone, kUtf16, kTable, kNotSetting, kValue, kKeyTwice };

	struct ParseError
	{
		Problem problem = Problem::kNone;
		int line = 0;       // 1-based
		std::string key;    // kKeyTwice
	};

	namespace detail
	{
		inline bool IsDigit(char c) { return c >= '0' && c <= '9'; }
		inline bool IsBareKeyChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-'; }

		inline std::string Trim(const std::string& a_s)
		{
			const auto b = a_s.find_first_not_of(" \t");
			if (b == std::string::npos) { return {}; }
			return a_s.substr(b, a_s.find_last_not_of(" \t") - b + 1);
		}

		inline std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		inline void AppendUtf8(std::string& a_out, std::uint32_t a_cp)
		{
			if (a_cp < 0x80) { a_out += static_cast<char>(a_cp); }
			else if (a_cp < 0x800) { a_out += static_cast<char>(0xC0 | (a_cp >> 6)); a_out += static_cast<char>(0x80 | (a_cp & 0x3F)); }
			else if (a_cp < 0x10000)
			{
				a_out += static_cast<char>(0xE0 | (a_cp >> 12));
				a_out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
			}
			else
			{
				a_out += static_cast<char>(0xF0 | (a_cp >> 18));
				a_out += static_cast<char>(0x80 | ((a_cp >> 12) & 0x3F));
				a_out += static_cast<char>(0x80 | ((a_cp >> 6) & 0x3F));
				a_out += static_cast<char>(0x80 | (a_cp & 0x3F));
			}
		}

		// TOML digits with '_' only between two digits; the digits without the underscores, or false
		inline bool Digits(const std::string& a_t, bool (*a_ok)(char), std::string& a_out)
		{
			a_out.clear();
			if (a_t.empty() || a_t.front() == '_' || a_t.back() == '_') { return false; }
			for (std::size_t i = 0; i < a_t.size(); ++i)
			{
				if (a_t[i] == '_')
				{
					if (!a_ok(a_t[i - 1]) || !a_ok(a_t[i + 1])) { return false; }
					continue;
				}
				if (!a_ok(a_t[i])) { return false; }
				a_out += a_t[i];
			}
			return !a_out.empty();
		}

		inline bool Dec(char c) { return IsDigit(c); }
		inline bool Hex(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }
		inline bool Oct(char c) { return c >= '0' && c <= '7'; }
		inline bool Bin(char c) { return c == '0' || c == '1'; }

		// A bare value token ("true", "150", "-0.5", "1e3", "0xFF8000FF", "inf").
		inline bool ParseBare(const std::string& a_t, Value& a_v)
		{
			if (a_t == "true" || a_t == "false") { a_v.type = TomlType::kBool; a_v.b = a_t == "true"; return true; }
			std::string d;
			if (a_t.size() > 2 && a_t[0] == '0' && (a_t[1] == 'x' || a_t[1] == 'o' || a_t[1] == 'b'))
			{
				const int base = a_t[1] == 'x' ? 16 : (a_t[1] == 'o' ? 8 : 2);
				if (!Digits(a_t.substr(2), base == 16 ? Hex : (base == 8 ? Oct : Bin), d)) { return false; }
				unsigned long long u = 0;
				const auto r = std::from_chars(d.data(), d.data() + d.size(), u, base);
				if (r.ec != std::errc() || r.ptr != d.data() + d.size() || u > 0x7FFFFFFFFFFFFFFFull) { return false; }
				a_v.type = TomlType::kInt;
				a_v.i = static_cast<long long>(u);
				a_v.base = base;
				return true;
			}
			std::size_t p = 0;
			const bool neg = !a_t.empty() && a_t[0] == '-';
			if (!a_t.empty() && (a_t[0] == '+' || a_t[0] == '-')) { p = 1; }
			const std::string body = a_t.substr(p);
			if (body == "inf" || body == "nan")
			{
				a_v.type = TomlType::kFloat;
				a_v.f = body == "inf" ? (neg ? -INFINITY : INFINITY) : NAN;
				a_v.decimals = -1;
				return true;
			}
			const auto dot = body.find('.');
			const auto exp = body.find_first_of("eE");
			const std::string intPart = body.substr(0, std::min(dot, exp));
			std::string intDigits;
			if (!Digits(intPart, Dec, intDigits)) { return false; }
			if (intDigits.size() > 1 && intDigits[0] == '0') { return false; }   // TOML: no leading zeros
			if (dot == std::string::npos && exp == std::string::npos)
			{
				long long v = 0;
				const std::string all = (neg ? "-" : "") + intDigits;
				const auto r = std::from_chars(all.data(), all.data() + all.size(), v);
				if (r.ec != std::errc() || r.ptr != all.data() + all.size()) { return false; }
				a_v.type = TomlType::kInt;
				a_v.i = v;
				a_v.base = 10;
				return true;
			}
			std::string norm = (neg ? "-" : "") + intDigits;
			int decimals = -1;
			if (dot != std::string::npos)
			{
				if (exp != std::string::npos && exp < dot) { return false; }
				const std::string frac = body.substr(dot + 1, (exp == std::string::npos ? body.size() : exp) - dot - 1);
				std::string fracDigits;
				if (!Digits(frac, Dec, fracDigits)) { return false; }
				norm += "." + fracDigits;
				decimals = static_cast<int>(fracDigits.size());
			}
			if (exp != std::string::npos)
			{
				std::string e = body.substr(exp + 1);
				std::string sign;
				if (!e.empty() && (e[0] == '+' || e[0] == '-')) { sign = e.substr(0, 1); e = e.substr(1); }
				std::string expDigits;
				if (!Digits(e, Dec, expDigits)) { return false; }
				norm += "e" + sign + expDigits;
				decimals = -1;
			}
			double f = 0.0;
			const auto r = std::from_chars(norm.data(), norm.data() + norm.size(), f);
			if (r.ec != std::errc() || r.ptr != norm.data() + norm.size()) { return false; }
			a_v.type = TomlType::kFloat;
			a_v.f = f;
			a_v.decimals = decimals;
			return true;
		}

		// A quoted string starting at a_line[a_pos] (a '"' or a '\''); a_end is just past its closing quote.
		inline bool ParseString(const std::string& a_line, std::size_t a_pos, Value& a_v, std::size_t& a_end)
		{
			const char q = a_line[a_pos];
			if (a_line.compare(a_pos, 3, std::string(3, q)) == 0) { return false; }   // a multi-line string
			std::string out;
			std::size_t i = a_pos + 1;
			for (; i < a_line.size(); ++i)
			{
				const char c = a_line[i];
				if (c == q) { break; }
				if ((static_cast<unsigned char>(c) < 0x20 && c != '\t') || c == 0x7F) { return false; }
				if (q == '\'' || c != '\\') { out += c; continue; }
				if (++i >= a_line.size()) { return false; }
				switch (a_line[i])
				{
				case 'b': out += '\b'; break;
				case 't': out += '\t'; break;
				case 'n': out += '\n'; break;
				case 'f': out += '\f'; break;
				case 'r': out += '\r'; break;
				case 'e': out += '\x1B'; break;
				case '"': out += '"'; break;
				case '\\': out += '\\'; break;
				case 'u':
				case 'U':
				{
					const std::size_t n = a_line[i] == 'u' ? 4 : 8;
					if (i + n >= a_line.size()) { return false; }   // the hex digits are a_line[i + 1 .. i + n]
					std::uint32_t cp = 0;
					const auto r = std::from_chars(a_line.data() + i + 1, a_line.data() + i + 1 + n, cp, 16);
					if (r.ec != std::errc() || r.ptr != a_line.data() + i + 1 + n || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { return false; }
					AppendUtf8(out, cp);
					i += n;
					break;
				}
				default: return false;
				}
			}
			if (i >= a_line.size()) { return false; }   // never closed
			a_v.type = TomlType::kString;
			a_v.s = std::move(out);
			a_v.literal = q == '\'';
			a_end = i + 1;
			return true;
		}
	}

	// Reads a file's bytes. False with a_err set when the file is not one this reader takes; a_doc then holds what was read
	// up to the problem and must not be written.
	inline bool Parse(const std::string& a_bytes, Document& a_doc, ParseError& a_err)
	{
		a_doc = Document{};
		a_err = ParseError{};
		if (a_bytes.size() >= 2 && ((a_bytes[0] == '\xFF' && a_bytes[1] == '\xFE') || (a_bytes[0] == '\xFE' && a_bytes[1] == '\xFF')))
		{
			a_err.problem = Problem::kUtf16;
			return false;
		}
		std::size_t start = 0;
		if (a_bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) { a_doc.bom = true; start = 3; }
		while (start < a_bytes.size())
		{
			const auto nl = a_bytes.find('\n', start);
			if (nl == std::string::npos)
			{
				a_doc.lines.push_back(a_bytes.substr(start));
				a_doc.endings.emplace_back();
				break;
			}
			const bool cr = nl > start && a_bytes[nl - 1] == '\r';
			a_doc.lines.push_back(a_bytes.substr(start, nl - start - (cr ? 1 : 0)));
			a_doc.endings.emplace_back(cr ? "\r\n" : "\n");
			start = nl + 1;
		}

		std::set<std::string> keys;
		std::vector<std::string> help;
		for (std::size_t n = 0; n < a_doc.lines.size(); ++n)
		{
			const std::string& line = a_doc.lines[n];
			const int lineNo = static_cast<int>(n + 1);
			std::size_t p = line.find_first_not_of(" \t");
			if (p == std::string::npos) { help.clear(); continue; }
			if (line[p] == '#') { help.push_back(detail::Trim(line.substr(p + 1))); continue; }
			if (line[p] == '[') { a_err = { Problem::kTable, lineNo, {} }; return false; }
			const std::size_t keyStart = p;
			while (p < line.size() && detail::IsBareKeyChar(line[p])) { ++p; }
			if (p == keyStart) { a_err = { Problem::kNotSetting, lineNo, {} }; return false; }   // a quoted key, or not a setting
			Entry e;
			e.line = n;
			e.key = line.substr(keyStart, p - keyStart);
			p = line.find_first_not_of(" \t", p);
			if (p == std::string::npos || line[p] != '=') { a_err = { Problem::kNotSetting, lineNo, {} }; return false; }   // dotted keys land here too
			p = line.find_first_not_of(" \t", p + 1);
			if (p == std::string::npos) { a_err = { Problem::kValue, lineNo, {} }; return false; }
			std::size_t end = p;
			if (line[p] == '"' || line[p] == '\'')
			{
				if (!detail::ParseString(line, p, e.value, end)) { a_err = { Problem::kValue, lineNo, {} }; return false; }
			}
			else
			{
				while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '#') { ++end; }
				if (!detail::ParseBare(line.substr(p, end - p), e.value)) { a_err = { Problem::kValue, lineNo, {} }; return false; }
			}
			e.valueStart = p;
			e.valueEnd = end;
			const std::size_t after = line.find_first_not_of(" \t", end);
			if (after != std::string::npos)
			{
				if (line[after] != '#') { a_err = { Problem::kValue, lineNo, {} }; return false; }
				e.comment = detail::Trim(line.substr(after + 1));
			}
			if (!keys.insert(e.key).second) { a_err = { Problem::kKeyTwice, lineNo, e.key }; return false; }
			std::string h;
			for (const std::string& l : help) { h += (h.empty() ? "" : "\n") + l; }
			if (!e.comment.empty()) { h += (h.empty() ? "" : "\n") + e.comment; }
			e.help = h;
			help.clear();
			a_doc.entries.push_back(std::move(e));
		}
		return true;
	}

	// The document's bytes: each line with its own ending, the byte-order mark put back.
	inline std::string Serialize(const Document& a_doc)
	{
		std::string out = a_doc.bom ? "\xEF\xBB\xBF" : "";
		for (std::size_t i = 0; i < a_doc.lines.size(); ++i) { out += a_doc.lines[i] + (i < a_doc.endings.size() ? a_doc.endings[i] : std::string()); }
		return out;
	}

	inline const Entry* Find(const Document& a_doc, const std::string& a_key)
	{
		for (const Entry& e : a_doc.entries) { if (e.key == a_key) { return &e; } }
		return nullptr;
	}

	// The bytes of a_doc with a_key's value text replaced by a_text, nothing else changed. False when the key is not there.
	inline bool WithValue(const Document& a_doc, const std::string& a_key, const std::string& a_text, std::string& a_bytes)
	{
		const Entry* e = Find(a_doc, a_key);
		if (!e) { return false; }
		Document copy = a_doc;
		std::string& line = copy.lines[e->line];
		line = line.substr(0, e->valueStart) + a_text + line.substr(e->valueEnd);
		a_bytes = Serialize(copy);
		return true;
	}

	// ---- values as text ----------------------------------------------------------------------------------------------

	inline std::string FormatBool(bool a_b) { return a_b ? "true" : "false"; }

	inline std::string FormatInt(long long a_v, int a_base)
	{
		char buf[80];
		if (a_base == 16 && a_v >= 0) { std::snprintf(buf, sizeof(buf), "0x%llX", a_v); }
		else if ((a_base == 8 || a_base == 2) && a_v >= 0)
		{
			std::string d;
			unsigned long long u = static_cast<unsigned long long>(a_v);
			do { d.insert(d.begin(), static_cast<char>('0' + (u % static_cast<unsigned>(a_base)))); u /= static_cast<unsigned>(a_base); } while (u);
			return std::string(a_base == 8 ? "0o" : "0b") + d;
		}
		else { std::snprintf(buf, sizeof(buf), "%lld", a_v); }
		return buf;
	}

	// A colour as GSO writes it: 0xRRGGBBAA, all eight digits.
	inline std::string FormatColor(std::uint32_t a_rgba)
	{
		char buf[16];
		std::snprintf(buf, sizeof(buf), "0x%08X", a_rgba);
		return buf;
	}

	// Always a TOML float (a '.' or an exponent in it): a_decimals digits after the '.', at least one; -1 = the shortest
	// text that reads back as the same float.
	inline std::string FormatFloat(double a_v, int a_decimals)
	{
		if (std::isnan(a_v)) { return "nan"; }
		if (std::isinf(a_v)) { return a_v < 0 ? "-inf" : "inf"; }
		char buf[64];
		if (a_decimals >= 1) { std::snprintf(buf, sizeof(buf), "%.*f", std::min(a_decimals, 9), a_v); return buf; }
		for (int prec = 6; prec <= 17; ++prec)
		{
			std::snprintf(buf, sizeof(buf), "%.*g", prec, a_v);
			if (static_cast<float>(std::strtod(buf, nullptr)) == static_cast<float>(a_v)) { break; }
		}
		std::string s = buf;
		if (s.find_first_of(".eEn") == std::string::npos) { s += ".0"; }
		return s;
	}

	// A string as a TOML value: single quotes when it was written so and still can be, else double quotes with escapes.
	inline std::string FormatString(const std::string& a_s, bool a_literal)
	{
		const bool plain = std::none_of(a_s.begin(), a_s.end(), [](char c) { return c == '\'' || (static_cast<unsigned char>(c) < 0x20 && c != '\t') || c == 0x7F; });
		if (a_literal && plain) { return "'" + a_s + "'"; }
		std::string out = "\"";
		for (const char c : a_s)
		{
			switch (c)
			{
			case '"': out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			case '\b': out += "\\b"; break;
			case '\f': out += "\\f"; break;
			default:
				if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F)
				{
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(static_cast<unsigned char>(c)));
					out += buf;
				}
				else { out += c; }
			}
		}
		return out + "\"";
	}

	// ---- types -------------------------------------------------------------------------------------------------------

	inline const char* TomlTypeName(TomlType a_t)
	{
		switch (a_t)
		{
		case TomlType::kBool: return "bool";
		case TomlType::kInt: return "int";
		case TomlType::kFloat: return "float";
		case TomlType::kString: return "string";
		default: return "none";
		}
	}

	inline const char* GameTypeName(GameType a_t)
	{
		switch (a_t)
		{
		case GameType::kBool: return "bool";
		case GameType::kFloat: return "float";
		case GameType::kInt: return "int";
		case GameType::kColor: return "color";
		case GameType::kString: return "string";
		case GameType::kUInt: return "uint";
		default: return "unknown";
		}
	}

	inline const char* ControlName(Control a_c)
	{
		switch (a_c)
		{
		case Control::kSwitch: return "switch";
		case Control::kFloat: return "number (decimal)";
		case Control::kInt: return "number (whole)";
		case Control::kUInt: return "number (whole, 0 or more)";
		case Control::kColor: return "colour";
		default: return "text";
		}
	}

	// The control by the GAME setting's type; for a key the game does not know (kUnknown), by the value the file holds.
	inline Control ControlFor(GameType a_game, TomlType a_toml)
	{
		switch (a_game)
		{
		case GameType::kBool: return Control::kSwitch;
		case GameType::kFloat: return Control::kFloat;
		case GameType::kInt: return Control::kInt;
		case GameType::kUInt: return Control::kUInt;
		case GameType::kColor: return Control::kColor;
		case GameType::kString: return Control::kText;
		default: break;
		}
		switch (a_toml)
		{
		case TomlType::kBool: return Control::kSwitch;
		case TomlType::kInt: return Control::kInt;
		case TomlType::kFloat: return Control::kFloat;
		default: return Control::kText;
		}
	}

	// Whether the file's value is written as the game setting's type (bool / float / an integer for int, uint and colour /
	// a string). A key the game does not know matches anything - it is flagged as unknown instead.
	inline bool Matches(GameType a_game, TomlType a_toml)
	{
		switch (a_game)
		{
		case GameType::kBool: return a_toml == TomlType::kBool;
		case GameType::kFloat: return a_toml == TomlType::kFloat;
		case GameType::kInt:
		case GameType::kUInt:
		case GameType::kColor: return a_toml == TomlType::kInt;
		case GameType::kString: return a_toml == TomlType::kString;
		default: return true;
		}
	}

	// An integer value read as a colour: 0xRRGGBBAA -> r, g, b, a.
	inline void ColorParts(long long a_v, std::uint8_t a_out[4])
	{
		const auto u = static_cast<std::uint32_t>(a_v);
		a_out[0] = static_cast<std::uint8_t>(u >> 24);
		a_out[1] = static_cast<std::uint8_t>(u >> 16);
		a_out[2] = static_cast<std::uint8_t>(u >> 8);
		a_out[3] = static_cast<std::uint8_t>(u);
	}

	inline std::uint32_t ColorFromParts(const std::uint8_t a_p[4])
	{
		return (static_cast<std::uint32_t>(a_p[0]) << 24) | (static_cast<std::uint32_t>(a_p[1]) << 16) | (static_cast<std::uint32_t>(a_p[2]) << 8) | a_p[3];
	}

	// The TOML text to write for a_input (what the page's control or DevBench gives) under a_control, keeping how the old
	// value was written where it can (a float's decimals, an integer's base, single quotes). False with a_err in English
	// (a tool's text) when a_input does not fit the control.
	inline bool TextFor(Control a_control, const Value& a_old, const std::string& a_input, std::string& a_text, std::string& a_err)
	{
		const std::string in = detail::Trim(a_input);
		switch (a_control)
		{
		case Control::kSwitch:
		{
			const std::string l = detail::Lower(in);
			if (l == "true" || l == "1" || l == "on") { a_text = "true"; return true; }
			if (l == "false" || l == "0" || l == "off") { a_text = "false"; return true; }
			a_err = "takes true or false";
			return false;
		}
		case Control::kFloat:
		{
			Value v;
			if (!detail::ParseBare(in, v) || (v.type != TomlType::kFloat && v.type != TomlType::kInt)) { a_err = "takes a number"; return false; }
			const double f = v.type == TomlType::kInt ? static_cast<double>(v.i) : v.f;
			int decimals = a_old.type == TomlType::kFloat ? a_old.decimals : 1;
			if (v.type == TomlType::kFloat && decimals >= 1 && v.decimals > decimals) { decimals = v.decimals; }
			if (v.type == TomlType::kFloat && v.decimals < 0) { decimals = -1; }
			a_text = FormatFloat(f, decimals);
			return true;
		}
		case Control::kInt:
		case Control::kUInt:
		{
			Value v;
			if (!detail::ParseBare(in, v) || v.type != TomlType::kInt) { a_err = "takes a whole number"; return false; }
			const bool u = a_control == Control::kUInt;
			if (u ? (v.i < 0 || v.i > 0xFFFFFFFFll) : (v.i < -2147483648ll || v.i > 2147483647ll))
			{
				a_err = u ? "takes a whole number from 0 to 4294967295" : "takes a whole number from -2147483648 to 2147483647";
				return false;
			}
			a_text = FormatInt(v.i, a_old.type == TomlType::kInt ? a_old.base : 10);
			return true;
		}
		case Control::kColor:
		{
			std::string t = in;
			if (!t.empty() && t[0] == '#') { t = "0x" + t.substr(1); }
			Value v;
			if (!detail::ParseBare(t, v) || v.type != TomlType::kInt || v.i < 0 || v.i > 0xFFFFFFFFll) { a_err = "takes a colour, 0xRRGGBBAA"; return false; }
			a_text = FormatColor(static_cast<std::uint32_t>(v.i));
			return true;
		}
		case Control::kText:
		{
			if (a_input.find_first_of("\r\n") != std::string::npos) { a_err = "one line only"; return false; }
			a_text = FormatString(a_input, a_old.literal);
			return true;
		}
		}
		return false;
	}

	// ---- the files ---------------------------------------------------------------------------------------------------

	// GSO reads a file only when its name ends in ".toml" in small letters (its Nexus page says so).
	inline bool GsoReadsName(const std::string& a_file)
	{
		return a_file.size() > 5 && a_file.compare(a_file.size() - 5, 5, ".toml") == 0;
	}

	// The load order: by file name, character by character (std::string's order - capitals before small letters). The page
	// shows the files in this order; the in-game check compares it with the order GSO's own log lists.
	inline void SortLoadOrder(std::vector<std::string>& a_files) { std::sort(a_files.begin(), a_files.end()); }

	// The file that sets a key last, by load order (a_docs in that order) - the one whose value the game ends up with. The
	// game's own lookup ignores case, so "fFoo" in one file and "ffoo" in a later one are the same setting. -1 = none.
	inline int Winner(const std::vector<const Document*>& a_docs, const std::string& a_key)
	{
		const std::string want = detail::Lower(a_key);
		int out = -1;
		for (std::size_t i = 0; i < a_docs.size(); ++i)
		{
			if (!a_docs[i]) { continue; }
			for (const Entry& e : a_docs[i]->entries)
			{
				if (detail::Lower(e.key) == want) { out = static_cast<int>(i); }
			}
		}
		return out;
	}

	// [GsoMenus] sOn: the files turned on, by id (the lower-case file name), comma-separated. Empty = none (each file starts
	// off, as each INI page does).
	inline std::set<std::string> ParseIdList(const std::string& a_list)
	{
		std::set<std::string> out;
		std::stringstream ss(a_list);
		std::string item;
		while (std::getline(ss, item, ','))
		{
			const std::string t = detail::Lower(detail::Trim(item));
			if (!t.empty()) { out.insert(t); }
		}
		return out;
	}

	inline std::string WithId(const std::string& a_list, const std::string& a_id, bool a_on)
	{
		std::set<std::string> ids = ParseIdList(a_list);
		const std::string id = detail::Lower(a_id);
		if (a_on) { ids.insert(id); } else { ids.erase(id); }
		std::string joined;
		for (const std::string& s : ids) { joined += (joined.empty() ? "" : ",") + s; }
		return joined;
	}
}
