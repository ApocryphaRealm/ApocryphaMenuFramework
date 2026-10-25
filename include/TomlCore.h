#pragma once

// ============================================================================================
// TOML FILES - the reader and line-preserving writer behind AMF's TOML pages (TomlPages.h), with no game or ImGui code, so
// the offline check (tools\toml_check.cpp) runs the same file the pages do.
//
// READ: tables ([a], [a.b]) and arrays of tables ([[a]]), bare / quoted / dotted keys, and every TOML value - bool,
// integer (decimal, 0x, 0o, 0b, with '_'), float (with exponent, inf, nan), basic and literal strings, multi-line strings,
// dates and times, arrays (over several lines too, with comments inside) and inline tables. The "#" lines directly above
// a key (or a table) are its help, a "# ..." after a value too.
//
// WRITE: only a value on ONE line that is a bool, a number, a one-line string or a date/time. Its text - and nothing else -
// is replaced: the key, the spacing, the comment after it, every other line, the byte-order mark and each line's own
// ending stay byte for byte. Arrays, inline tables and multi-line strings are shown, never written; the file is never
// written back from a parsed model.
//
// A file that does not read cleanly (a line that is neither a table, a setting nor a comment, a value that is not TOML, a
// string or array never closed, a key or a table given twice, UTF-16 text) is not shown at all, so nothing is ever written
// into a file this reader did not fully understand. Leniency on purpose: the TOML rules on mixing a key and a table of the
// same name are not checked - the writer never changes structure, so such a file is shown, not damaged.
// ============================================================================================

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace tomlcore
{
	enum class TomlType { kNone, kBool, kInt, kFloat, kString, kDateTime, kArray, kInlineTable };

	struct Value
	{
		TomlType type = TomlType::kNone;
		bool b = false;
		long long i = 0;
		double f = 0.0;
		std::string s;
		int base = 10;          // an integer's base as written: 10, 16, 8 or 2
		int decimals = -1;      // a float's digits after the '.', as written; -1 = an exponent, inf or nan
		bool literal = false;   // a string in single quotes
		bool multiline = false; // a string in triple quotes (shown, not written)
		std::size_t items = 0;  // an array's or inline table's entries
	};

	struct Entry
	{
		std::size_t line = 0;         // the line the value starts on (Document::lines)
		std::size_t endLine = 0;      // the line it ends on (an array or a multi-line string can run on)
		std::string key;              // as a TOML key, dotted parts joined by '.', a part that needs it in quotes
		std::string id;               // unique in the file: "<table id>.<key>", or the key itself at the top
		std::size_t section = 0;      // Document::sections index
		std::size_t valueStart = 0;   // the value's text on `line` (to the end of `endLine` when it runs on)
		std::size_t valueEnd = 0;
		Value value;
		std::string raw;              // the value's text as written (lines joined by '\n')
		std::string comment;          // after the value, without the '#'
		std::string help;             // the comment lines directly above (a blank line ends a block), then `comment`

		bool Editable() const
		{
			if (endLine != line || value.multiline) { return false; }
			switch (value.type)
			{
			case TomlType::kBool:
			case TomlType::kInt:
			case TomlType::kFloat:
			case TomlType::kString:
			case TomlType::kDateTime: return true;
			default: return false;
			}
		}
	};

	struct Section
	{
		std::string id;               // "" = the top of the file (before any table); "a.b"; "a[0]" for an array-of-tables element
		std::string name;             // shown: "a > b", "Weapon 2"
		std::string help;
		std::vector<std::size_t> entries;
	};

	struct Document
	{
		bool bom = false;
		std::vector<std::string> lines;     // each line's text, without its ending
		std::vector<std::string> endings;   // each line's own ending: "\r\n", "\n", or "" (the last line, when it has none)
		std::vector<Entry> entries;
		std::vector<Section> sections;      // [0] is always the top of the file
	};

	enum class Problem { kNone, kUtf16, kNotSetting, kValue, kUnclosed, kKeyTwice, kTableTwice };

	struct ParseError
	{
		Problem problem = Problem::kNone;
		int line = 0;       // 1-based
		std::string key;    // kKeyTwice / kTableTwice
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
			if (!a_ok) { return false; }   // no digit test: nothing can be read as digits (every caller passes one)
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

		// a bare scalar token: bool, integer or float
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

		inline bool N(const std::string& a_s, std::size_t a_at, std::size_t a_n)
		{
			if (a_at + a_n > a_s.size()) { return false; }
			for (std::size_t i = 0; i < a_n; ++i) { if (!IsDigit(a_s[a_at + i])) { return false; } }
			return true;
		}

		// HH:MM:SS(.fff) at a_at; the length or 0
		inline std::size_t TimeLen(const std::string& a_s, std::size_t a_at)
		{
			if (!N(a_s, a_at, 2) || a_at + 2 >= a_s.size() || a_s[a_at + 2] != ':' || !N(a_s, a_at + 3, 2) || a_at + 5 >= a_s.size() || a_s[a_at + 5] != ':' ||
				!N(a_s, a_at + 6, 2))
			{
				return 0;
			}
			std::size_t n = 8;
			if (a_at + n < a_s.size() && a_s[a_at + n] == '.')
			{
				std::size_t k = n + 1;
				while (a_at + k < a_s.size() && IsDigit(a_s[a_at + k])) { ++k; }
				if (k == n + 1) { return 0; }
				n = k;
			}
			return n;
		}

		// A TOML date, time, local date-time or offset date-time, the whole of a_t.
		inline bool IsDateTime(const std::string& a_t)
		{
			if (const std::size_t t = TimeLen(a_t, 0); t && t == a_t.size()) { return true; }   // local time
			if (!(N(a_t, 0, 4) && a_t.size() >= 10 && a_t[4] == '-' && N(a_t, 5, 2) && a_t[7] == '-' && N(a_t, 8, 2))) { return false; }
			if (a_t.size() == 10) { return true; }   // local date
			if (a_t[10] != 'T' && a_t[10] != 't' && a_t[10] != ' ') { return false; }
			const std::size_t t = TimeLen(a_t, 11);
			if (!t) { return false; }
			std::size_t at = 11 + t;
			if (at == a_t.size()) { return true; }
			if ((a_t[at] == 'Z' || a_t[at] == 'z') && at + 1 == a_t.size()) { return true; }
			return (a_t[at] == '+' || a_t[at] == '-') && at + 6 == a_t.size() && N(a_t, at + 1, 2) && a_t[at + 3] == ':' && N(a_t, at + 4, 2);
		}

		// a one-line quoted string at a_line[a_pos] ('"' or '\''); a_end just past the closing quote
		inline bool ParseString(const std::string& a_line, std::size_t a_pos, Value& a_v, std::size_t& a_end)
		{
			const char q = a_line[a_pos];
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

		// A position in the document's lines.
		struct Cur
		{
			const std::vector<std::string>* lines = nullptr;
			std::size_t ln = 0;
			std::size_t col = 0;
			bool Eof() const { return ln >= lines->size(); }
			bool Eol() const { return Eof() || col >= (*lines)[ln].size(); }
			char Peek() const { return Eol() ? '\0' : (*lines)[ln][col]; }
			const std::string& L() const { return (*lines)[ln]; }
		};

		inline void SkipSpace(Cur& a_c) { while (!a_c.Eol() && (a_c.Peek() == ' ' || a_c.Peek() == '\t')) { ++a_c.col; } }

		// spaces, comments and line breaks (inside an array or an inline table)
		inline void SkipAll(Cur& a_c)
		{
			for (;;)
			{
				SkipSpace(a_c);
				if (a_c.Eof()) { return; }
				if (a_c.Eol() || a_c.Peek() == '#') { ++a_c.ln; a_c.col = 0; continue; }
				return;
			}
		}

		// "a" -> a; "a.b" or "my key" -> quoted, so an id never mixes up two different keys
		inline std::string KeyPart(const std::string& a_part)
		{
			if (!a_part.empty() && std::all_of(a_part.begin(), a_part.end(), IsBareKeyChar)) { return a_part; }
			std::string out = "\"";
			for (const char c : a_part) { if (c == '"' || c == '\\') { out += '\\'; } out += c; }
			return out + "\"";
		}

		// one key part (bare or a one-line quoted string)
		inline bool ParseKeyPart(Cur& a_c, std::string& a_part)
		{
			if (a_c.Peek() == '"' || a_c.Peek() == '\'')
			{
				Value v;
				std::size_t end = 0;
				if (!ParseString(a_c.L(), a_c.col, v, end)) { return false; }
				a_part = v.s;
				a_c.col = end;
				return true;
			}
			const std::size_t start = a_c.col;
			while (!a_c.Eol() && IsBareKeyChar(a_c.Peek())) { ++a_c.col; }
			if (a_c.col == start) { return false; }
			a_part = a_c.L().substr(start, a_c.col - start);
			return true;
		}

		inline bool ParseKey(Cur& a_c, std::vector<std::string>& a_parts)
		{
			a_parts.clear();
			for (;;)
			{
				std::string part;
				if (!ParseKeyPart(a_c, part)) { return false; }
				a_parts.push_back(part);
				SkipSpace(a_c);
				if (a_c.Peek() != '.') { return true; }
				++a_c.col;
				SkipSpace(a_c);
			}
		}

		inline std::string JoinKey(const std::vector<std::string>& a_parts)
		{
			std::string out;
			for (const std::string& p : a_parts) { out += (out.empty() ? "" : ".") + KeyPart(p); }
			return out;
		}

		// Any TOML value at a_c; a_c ends just past it. False on a value that is not TOML or is never closed (a_unclosed).
		inline bool ParseValue(Cur& a_c, Value& a_v, bool& a_unclosed, int a_depth)
		{
			if (a_depth > 64 || a_c.Eol()) { return false; }
			const char ch = a_c.Peek();
			const std::string& line = a_c.L();
			if ((ch == '"' || ch == '\'') && line.compare(a_c.col, 3, std::string(3, ch)) == 0)
			{
				// a multi-line string: to the closing triple quote, on this line or a later one
				const std::string close(3, ch);
				a_c.col += 3;
				for (;;)
				{
					if (a_c.Eof()) { a_unclosed = true; return false; }
					const std::string& l = a_c.L();
					std::size_t i = a_c.col;
					bool found = false;
					while (i < l.size())
					{
						if (ch == '"' && l[i] == '\\') { i += 2; continue; }
						if (l.compare(i, 3, close) == 0)
						{
							i += 3;
							for (int extra = 0; extra < 2 && i < l.size() && l[i] == ch; ++extra) { ++i; }   // up to two more quotes belong to the text
							found = true;
							break;
						}
						++i;
					}
					if (found) { a_c.col = i; break; }
					++a_c.ln;
					a_c.col = 0;
				}
				a_v.type = TomlType::kString;
				a_v.multiline = true;
				a_v.literal = ch == '\'';
				return true;
			}
			if (ch == '"' || ch == '\'')
			{
				std::size_t end = 0;
				if (!ParseString(line, a_c.col, a_v, end))
				{
					a_unclosed = line.find(ch, a_c.col + 1) == std::string::npos;
					return false;
				}
				a_c.col = end;
				return true;
			}
			if (ch == '[')
			{
				++a_c.col;
				a_v.type = TomlType::kArray;
				for (;;)
				{
					SkipAll(a_c);
					if (a_c.Eof()) { a_unclosed = true; return false; }
					if (a_c.Peek() == ']') { ++a_c.col; return true; }
					Value item;
					if (!ParseValue(a_c, item, a_unclosed, a_depth + 1)) { return false; }
					++a_v.items;
					SkipAll(a_c);
					if (a_c.Eof()) { a_unclosed = true; return false; }
					if (a_c.Peek() == ',') { ++a_c.col; continue; }
					if (a_c.Peek() == ']') { ++a_c.col; return true; }
					return false;
				}
			}
			if (ch == '{')
			{
				++a_c.col;
				a_v.type = TomlType::kInlineTable;
				SkipAll(a_c);
				if (a_c.Peek() == '}') { ++a_c.col; return true; }
				for (;;)
				{
					SkipAll(a_c);
					if (a_c.Eof()) { a_unclosed = true; return false; }
					std::vector<std::string> parts;
					if (!ParseKey(a_c, parts)) { return false; }
					SkipSpace(a_c);
					if (a_c.Peek() != '=') { return false; }
					++a_c.col;
					SkipSpace(a_c);
					Value item;
					if (!ParseValue(a_c, item, a_unclosed, a_depth + 1)) { return false; }
					++a_v.items;
					SkipAll(a_c);
					if (a_c.Eof()) { a_unclosed = true; return false; }
					if (a_c.Peek() == ',') { ++a_c.col; continue; }
					if (a_c.Peek() == '}') { ++a_c.col; return true; }
					return false;
				}
			}
			// a bare token: bool, number or date/time (a date and a time may be parted by one space)
			std::size_t end = a_c.col;
			while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '#' && line[end] != ',' && line[end] != ']' && line[end] != '}') { ++end; }
			std::string token = line.substr(a_c.col, end - a_c.col);
			if (token.size() == 10 && IsDateTime(token) && end + 1 < line.size() && line[end] == ' ' && TimeLen(line, end + 1))
			{
				std::size_t e2 = end + 1;
				while (e2 < line.size() && line[e2] != ' ' && line[e2] != '\t' && line[e2] != '#' && line[e2] != ',' && line[e2] != ']' && line[e2] != '}') { ++e2; }
				if (IsDateTime(line.substr(a_c.col, e2 - a_c.col))) { end = e2; token = line.substr(a_c.col, end - a_c.col); }
			}
			if (ParseBare(token, a_v)) { a_c.col = end; return true; }
			if (IsDateTime(token)) { a_v.type = TomlType::kDateTime; a_v.s = token; a_c.col = end; return true; }
			return false;
		}
	}

	// Reads a file's bytes. False with a_err set when the file is not one this reader takes; a_doc must then not be used.
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

		a_doc.sections.push_back(Section{});
		std::size_t current = 0;
		std::set<std::string> ids;          // every key id
		std::set<std::string> tables;       // every [table] id (not array-of-tables elements)
		std::map<std::string, int> aot;     // array-of-tables id -> elements so far
		std::vector<std::string> help;
		auto helpText = [&]() {
			std::string h;
			for (const std::string& l : help) { h += (h.empty() ? "" : "\n") + l; }
			return h;
		};
		std::size_t n = 0;
		while (n < a_doc.lines.size())
		{
			detail::Cur c{ &a_doc.lines, n, 0 };
			const int lineNo = static_cast<int>(n + 1);
			detail::SkipSpace(c);
			if (c.Eol()) { help.clear(); ++n; continue; }
			if (c.Peek() == '#') { help.push_back(detail::Trim(c.L().substr(c.col + 1))); ++n; continue; }
			if (c.Peek() == '[')
			{
				const bool isAot = c.col + 1 < c.L().size() && c.L()[c.col + 1] == '[';
				c.col += isAot ? 2 : 1;
				detail::SkipSpace(c);
				std::vector<std::string> parts;
				if (!detail::ParseKey(c, parts)) { a_err = { Problem::kNotSetting, lineNo, {} }; return false; }
				detail::SkipSpace(c);
				if (c.Peek() != ']') { a_err = { Problem::kNotSetting, lineNo, {} }; return false; }
				++c.col;
				if (isAot)
				{
					if (c.Peek() != ']') { a_err = { Problem::kNotSetting, lineNo, {} }; return false; }
					++c.col;
				}
				detail::SkipSpace(c);
				if (!c.Eol() && c.Peek() != '#') { a_err = { Problem::kNotSetting, lineNo, {} }; return false; }
				Section s;
				for (std::size_t k = 0; k < parts.size(); ++k)
				{
					s.id += (k ? "." : "") + detail::KeyPart(parts[k]);
					s.name += (k ? " > " : "") + parts[k];
					const bool last = k + 1 == parts.size();
					if (last && isAot)
					{
						const int idx = aot[s.id]++;
						s.id += "[" + std::to_string(idx) + "]";
						s.name += " " + std::to_string(idx + 1);
					}
					else if (const auto it = aot.find(s.id); it != aot.end())
					{
						s.id += "[" + std::to_string(it->second - 1) + "]";
						s.name += " " + std::to_string(it->second);
					}
				}
				if (!isAot && !tables.insert(s.id).second) { a_err = { Problem::kTableTwice, lineNo, s.name }; return false; }
				s.help = helpText();
				help.clear();
				a_doc.sections.push_back(std::move(s));
				current = a_doc.sections.size() - 1;
				++n;
				continue;
			}
			std::vector<std::string> parts;
			if (!detail::ParseKey(c, parts)) { a_err = { Problem::kNotSetting, lineNo, {} }; return false; }
			detail::SkipSpace(c);
			if (c.Peek() != '=') { a_err = { Problem::kNotSetting, lineNo, {} }; return false; }
			++c.col;
			detail::SkipSpace(c);
			if (c.Eol()) { a_err = { Problem::kValue, lineNo, {} }; return false; }
			Entry e;
			e.line = c.ln;
			e.valueStart = c.col;
			bool unclosed = false;
			if (!detail::ParseValue(c, e.value, unclosed, 0)) { a_err = { unclosed ? Problem::kUnclosed : Problem::kValue, lineNo, {} }; return false; }
			e.endLine = c.ln;
			e.valueEnd = c.col;
			if (e.endLine == e.line) { e.raw = a_doc.lines[e.line].substr(e.valueStart, e.valueEnd - e.valueStart); }
			else
			{
				e.raw = a_doc.lines[e.line].substr(e.valueStart);
				for (std::size_t k = e.line + 1; k < e.endLine; ++k) { e.raw += "\n" + a_doc.lines[k]; }
				e.raw += "\n" + a_doc.lines[e.endLine].substr(0, e.valueEnd);
			}
			detail::SkipSpace(c);
			if (!c.Eol())
			{
				if (c.Peek() != '#') { a_err = { Problem::kValue, static_cast<int>(c.ln + 1), {} }; return false; }
				e.comment = detail::Trim(c.L().substr(c.col + 1));
			}
			e.key = detail::JoinKey(parts);
			const std::string& sec = a_doc.sections[current].id;
			e.id = sec.empty() ? e.key : sec + "." + e.key;
			if (!ids.insert(e.id).second) { a_err = { Problem::kKeyTwice, lineNo, e.key }; return false; }
			e.section = current;
			e.help = helpText();
			if (!e.comment.empty()) { e.help += (e.help.empty() ? "" : "\n") + e.comment; }
			help.clear();
			a_doc.sections[current].entries.push_back(a_doc.entries.size());
			a_doc.entries.push_back(std::move(e));
			n = c.ln + 1;
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

	inline const Entry* Find(const Document& a_doc, const std::string& a_id)
	{
		for (const Entry& e : a_doc.entries) { if (e.id == a_id) { return &e; } }
		return nullptr;
	}

	// The bytes of a_doc with the value of a_id replaced by a_text, nothing else changed. False when the key is not there or
	// its value is not one that is written (Entry::Editable).
	inline bool WithValue(const Document& a_doc, const std::string& a_id, const std::string& a_text, std::string& a_bytes)
	{
		const Entry* e = Find(a_doc, a_id);
		if (!e || !e->Editable()) { return false; }
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
		if (s.find_first_of(".eE") == std::string::npos) { s += ".0"; }
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

	inline const char* TypeName(TomlType a_t)
	{
		switch (a_t)
		{
		case TomlType::kBool: return "bool";
		case TomlType::kInt: return "int";
		case TomlType::kFloat: return "float";
		case TomlType::kString: return "string";
		case TomlType::kDateTime: return "datetime";
		case TomlType::kArray: return "array";
		case TomlType::kInlineTable: return "inline table";
		default: return "none";
		}
	}

	// ---- controls ----------------------------------------------------------------------------------------------------

	// What a page draws for a value. kInt / kUInt are a game setting's 32-bit numbers (GSO); kInt64 is any TOML integer.
	enum class Control { kSwitch, kFloat, kInt, kUInt, kInt64, kColor, kText, kDate, kReadOnly };

	inline const char* ControlName(Control a_c)
	{
		switch (a_c)
		{
		case Control::kSwitch: return "switch";
		case Control::kFloat: return "number (decimal)";
		case Control::kInt: return "number (whole, 32-bit)";
		case Control::kUInt: return "number (whole, 0 or more)";
		case Control::kInt64: return "number (whole)";
		case Control::kColor: return "colour";
		case Control::kText: return "text";
		case Control::kDate: return "date / time";
		default: return "read-only";
		}
	}

	// A plain TOML value's control: by the value as written.
	inline Control ControlForValue(const Entry& a_e)
	{
		if (!a_e.Editable()) { return Control::kReadOnly; }
		switch (a_e.value.type)
		{
		case TomlType::kBool: return Control::kSwitch;
		case TomlType::kInt: return Control::kInt64;
		case TomlType::kFloat: return Control::kFloat;
		case TomlType::kString: return Control::kText;
		case TomlType::kDateTime: return Control::kDate;
		default: return Control::kReadOnly;
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

	inline std::string FormatColor(std::uint32_t a_rgba)
	{
		char buf[16];
		std::snprintf(buf, sizeof(buf), "0x%08X", a_rgba);
		return buf;
	}

	// The TOML text to write for a_input (what a control or DevBench gives) under a_control, keeping how the old value was
	// written where it can (a float's decimals, an integer's base, single quotes). False with a_err in English (a tool's
	// text) when a_input does not fit the control.
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
		case Control::kInt64:
		{
			Value v;
			if (!detail::ParseBare(in, v) || v.type != TomlType::kInt) { a_err = "takes a whole number"; return false; }
			if (a_control == Control::kUInt && (v.i < 0 || v.i > 0xFFFFFFFFll)) { a_err = "takes a whole number from 0 to 4294967295"; return false; }
			if (a_control == Control::kInt && (v.i < -2147483648ll || v.i > 2147483647ll)) { a_err = "takes a whole number from -2147483648 to 2147483647"; return false; }
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
		case Control::kDate:
		{
			if (!detail::IsDateTime(in)) { a_err = "takes a TOML date or time (1979-05-27, 07:32:00, 1979-05-27T07:32:00Z)"; return false; }
			a_text = in;
			return true;
		}
		default: a_err = "is shown only (an array, an inline table or a text over several lines)"; return false;
		}
	}

	// ---- page switches -----------------------------------------------------------------------------------------------

	// [TomlMenus] sOn: the pages turned on, by id, comma-separated, lower case. Empty = none (each page starts off).
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
