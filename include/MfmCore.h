#pragma once

// ============================================================================================
// MOD FUNCTION MENU ENTRIES, the part with no game and no ImGui in it (so tools\mfm_check.cpp runs the same code
// offline). The page itself is MfmPages.cpp.
//
// Mod Function Menu (ccld, MIT) lets a mod put a button in a menu by dropping a TOML file into
//     Data/SKSE/Plugins/ccld_ModFunctionMenu/{Mod,Config}/<folders>/<label>.toml
// whose keys name a function another SKSE plugin exports:
//     dll        = "ccld_ToggleUI.dll"   required - a module that must already be loaded
//     api        = "ToggleUI"            required - an extern "C" export of that module
//     type       = "Void"                Void (default) | Message | MessageBox; anything else is Void
//     preAction  = "None"                None | CloseMenu | CloseMenuAndResetPath; anything else is None
//     postAction = "None"                the same values as preAction
// Values are case-sensitive. Void is called as void(), Message and MessageBox as void(char* buf, size_t len) and the
// text left in the buffer is shown. This is a clean reimplementation from that description and MFM's public SDK header
// (the type and action names, the function shapes); no MFM code is used.
//
// The safety boundary, as in MFM: a function is found with GetModuleHandleW + GetProcAddress ONLY. Nothing is ever
// loaded, so only code the game has already loaded can be called. Every call is guarded (SEH and C++ exceptions),
// which MFM itself does not do.
//
// TOML: only the five keys above are read, all strings at the top level, so this reads exactly that much TOML - bare
// and quoted keys, basic strings with their escapes, literal strings, multi-line strings, comments, a byte-order mark.
// A key under a [table] or a dotted key is not a top-level key and is ignored, as MFM's own lookup ignores it; a value
// that is not a string counts as missing. (toml++'s vcpkg port builds a compiled library through meson, outside the
// shared build environment's path trimming - rule 45 - so it is not used.)
// ============================================================================================

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace mfm
{
	enum class Type : std::uint32_t { kVoid = 0, kMessage = 1, kMessageBox = 2 };
	enum class Action : std::uint32_t { kNone = 0, kCloseMenu = 1, kCloseMenuAndResetPath = 2 };

	// MFM's buffer for a Message / MessageBox function: 0x4000 chars, the last always made a NUL after the call.
	inline constexpr std::size_t kMessageSize = 0x4000;

	inline Type TypeFrom(const std::string& a_s)
	{
		if (a_s == "Message") { return Type::kMessage; }
		if (a_s == "MessageBox") { return Type::kMessageBox; }
		return Type::kVoid;   // "", "Void" and anything unknown
	}

	inline Action ActionFrom(const std::string& a_s)
	{
		if (a_s == "CloseMenu") { return Action::kCloseMenu; }
		if (a_s == "CloseMenuAndResetPath") { return Action::kCloseMenuAndResetPath; }
		return Action::kNone;   // "", "None" and anything unknown
	}

	inline const char* TypeName(Type a_t)
	{
		switch (a_t)
		{
		case Type::kMessage: return "Message";
		case Type::kMessageBox: return "MessageBox";
		default: return "Void";
		}
	}

	inline const char* ActionName(Action a_a)
	{
		switch (a_a)
		{
		case Action::kCloseMenu: return "CloseMenu";
		case Action::kCloseMenuAndResetPath: return "CloseMenuAndResetPath";
		default: return "None";
		}
	}

	inline std::wstring Widen(const std::string& a_s)
	{
		if (a_s.empty()) { return {}; }
		const int n = ::MultiByteToWideChar(CP_UTF8, 0, a_s.data(), static_cast<int>(a_s.size()), nullptr, 0);
		if (n <= 0) { return {}; }
		std::wstring w(static_cast<std::size_t>(n), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, a_s.data(), static_cast<int>(a_s.size()), w.data(), n);
		return w;
	}

	inline std::string Narrow(const std::wstring& a_w)
	{
		if (a_w.empty()) { return {}; }
		const int n = ::WideCharToMultiByte(CP_UTF8, 0, a_w.data(), static_cast<int>(a_w.size()), nullptr, 0, nullptr, nullptr);
		if (n <= 0) { return {}; }
		std::string s(static_cast<std::size_t>(n), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, a_w.data(), static_cast<int>(a_w.size()), s.data(), n, nullptr, nullptr);
		return s;
	}

	// ---- the TOML subset ------------------------------------------------------------------------------------------

	namespace detail
	{
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

		struct Reader
		{
			const std::string& s;
			std::size_t i = 0;
			int line = 1;

			bool End() const { return i >= s.size(); }
			char Peek(std::size_t a_off = 0) const { return i + a_off < s.size() ? s[i + a_off] : '\0'; }
			bool StartsWith(const char* a_p) const { return s.compare(i, std::strlen(a_p), a_p) == 0; }
			void SkipSpaces() { while (!End() && (s[i] == ' ' || s[i] == '\t')) { ++i; } }
			// spaces, then an optional comment, then the end of the line (or the file); false if anything else is there
			bool EndOfLine()
			{
				SkipSpaces();
				if (Peek() == '#') { while (!End() && s[i] != '\n') { ++i; } }
				if (End()) { return true; }
				if (s[i] == '\r' && Peek(1) == '\n') { i += 2; ++line; return true; }
				if (s[i] == '\n') { ++i; ++line; return true; }
				return false;
			}
			void SkipLine()
			{
				while (!End() && s[i] != '\n') { ++i; }
				if (!End()) { ++i; ++line; }
			}
		};

		// A basic ("...") or literal ('...') string, single- or multi-line, starting at the opening quote.
		inline bool ReadString(Reader& a_r, std::string& a_out, std::string& a_err)
		{
			const char q = a_r.Peek();
			const bool multi = (q == '"' && a_r.StartsWith("\"\"\"")) || (q == '\'' && a_r.StartsWith("'''"));
			const bool basic = q == '"';
			a_r.i += multi ? 3 : 1;
			if (multi)
			{
				// a newline straight after the opening quotes is not part of the string
				if (a_r.Peek() == '\r' && a_r.Peek(1) == '\n') { a_r.i += 2; ++a_r.line; }
				else if (a_r.Peek() == '\n') { ++a_r.i; ++a_r.line; }
			}
			a_out.clear();
			while (!a_r.End())
			{
				const char c = a_r.s[a_r.i];
				if (multi ? (basic ? a_r.StartsWith("\"\"\"") : a_r.StartsWith("'''")) : c == q)
				{
					a_r.i += multi ? 3 : 1;
					// up to two more quotes right before the closing three belong to the string
					for (int extra = 0; multi && extra < 2 && a_r.Peek() == q; ++extra) { a_out += q; ++a_r.i; }
					return true;
				}
				if (c == '\n')
				{
					if (!multi) { a_err = "a string runs past the end of its line"; return false; }
					++a_r.line;
				}
				if (basic && c == '\\')
				{
					const char e = a_r.Peek(1);
					a_r.i += 2;
					switch (e)
					{
					case '"': a_out += '"'; break;
					case '\\': a_out += '\\'; break;
					case 'b': a_out += '\b'; break;
					case 'f': a_out += '\f'; break;
					case 'n': a_out += '\n'; break;
					case 'r': a_out += '\r'; break;
					case 't': a_out += '\t'; break;
					case 'u':
					case 'U':
					{
						const std::size_t n = e == 'u' ? 4 : 8;
						if (a_r.i + n > a_r.s.size()) { a_err = "a \\u escape is cut short"; return false; }
						std::uint32_t cp = 0;
						for (std::size_t k = 0; k < n; ++k)
						{
							const char h = a_r.s[a_r.i + k];
							cp <<= 4;
							if (h >= '0' && h <= '9') { cp |= static_cast<std::uint32_t>(h - '0'); }
							else if (h >= 'a' && h <= 'f') { cp |= static_cast<std::uint32_t>(h - 'a' + 10); }
							else if (h >= 'A' && h <= 'F') { cp |= static_cast<std::uint32_t>(h - 'A' + 10); }
							else { a_err = "a \\u escape has a character that is not hex"; return false; }
						}
						a_r.i += n;
						AppendUtf8(a_out, cp);
						break;
					}
					default:
						if (multi && (e == '\n' || e == '\r' || e == ' ' || e == '\t'))
						{
							// a line-ending backslash: the newline and the white space after it are dropped
							a_r.i -= 1;
							while (!a_r.End() && (a_r.s[a_r.i] == ' ' || a_r.s[a_r.i] == '\t' || a_r.s[a_r.i] == '\r' || a_r.s[a_r.i] == '\n'))
							{
								if (a_r.s[a_r.i] == '\n') { ++a_r.line; }
								++a_r.i;
							}
							break;
						}
						a_err = std::string("an unknown escape \\") + e;
						return false;
					}
					continue;
				}
				a_out += c;
				++a_r.i;
			}
			a_err = "a string is never closed";
			return false;
		}

		// Skips a value that is not a string (a number, a boolean, a date, an array or an inline table), across lines for
		// an array or inline table. Only its end is needed.
		inline void SkipValue(Reader& a_r)
		{
			int depth = 0;
			while (!a_r.End())
			{
				const char c = a_r.s[a_r.i];
				if (c == '"' || c == '\'') { std::string ignored, err; if (!ReadString(a_r, ignored, err)) { return; } continue; }
				if (c == '[' || c == '{') { ++depth; }
				else if (c == ']' || c == '}') { --depth; }
				else if (c == '#' && depth > 0) { while (!a_r.End() && a_r.s[a_r.i] != '\n') { ++a_r.i; } continue; }
				else if (c == '\n')
				{
					if (depth <= 0) { return; }
					++a_r.line;
				}
				++a_r.i;
			}
		}
	}

	// The file's top-level string keys. False with a_err (and the line) when the file is not readable TOML of this shape.
	// A key that appears twice is an error, as it is in TOML.
	inline bool ParseTopLevelStrings(const std::string& a_text, std::map<std::string, std::string>& a_out, std::string& a_err)
	{
		std::string text = a_text;
		if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) { text.erase(0, 3); }
		detail::Reader r{ text };
		bool topLevel = true;
		std::vector<std::string> seen;
		auto fail = [&](const std::string& a_what) { a_err = "line " + std::to_string(r.line) + ": " + a_what; return false; };
		while (!r.End())
		{
			r.SkipSpaces();
			const char c = r.Peek();
			if (c == '#' || c == '\r' || c == '\n' || c == '\0')
			{
				if (!r.EndOfLine()) { return fail("unexpected text"); }
				continue;
			}
			if (c == '[')
			{
				// a [table] or [[array of tables]]: every key from here on belongs to it, not to the top level
				topLevel = false;
				r.SkipLine();
				continue;
			}
			// the key: bare, or quoted; a dot makes it a dotted key (never a top-level string)
			std::string key;
			bool dotted = false;
			for (;;)
			{
				std::string part;
				r.SkipSpaces();
				if (r.Peek() == '"' || r.Peek() == '\'')
				{
					std::string err;
					if (r.StartsWith("\"\"\"") || r.StartsWith("'''")) { return fail("a key cannot be a multi-line string"); }
					if (!detail::ReadString(r, part, err)) { return fail(err); }
				}
				else
				{
					while (!r.End())
					{
						const char k = r.Peek();
						if ((k >= 'A' && k <= 'Z') || (k >= 'a' && k <= 'z') || (k >= '0' && k <= '9') || k == '_' || k == '-') { part += k; ++r.i; }
						else { break; }
					}
					if (part.empty()) { return fail("a key was expected"); }
				}
				key += part;
				r.SkipSpaces();
				if (r.Peek() == '.') { dotted = true; key += '.'; ++r.i; continue; }
				break;
			}
			if (r.Peek() != '=') { return fail("'=' was expected after \"" + key + "\""); }
			++r.i;
			r.SkipSpaces();
			const bool wanted = topLevel && !dotted;
			if (wanted && std::find(seen.begin(), seen.end(), key) != seen.end()) { return fail("\"" + key + "\" is set twice"); }
			if (wanted) { seen.push_back(key); }
			if (r.Peek() == '"' || r.Peek() == '\'')
			{
				std::string value, err;
				if (!detail::ReadString(r, value, err)) { return fail(err); }
				if (!r.EndOfLine()) { return fail("unexpected text after the value of \"" + key + "\""); }
				if (wanted) { a_out[key] = value; }
				continue;
			}
			if (r.End() || r.Peek() == '\n' || r.Peek() == '\r' || r.Peek() == '#') { return fail("\"" + key + "\" has no value"); }
			detail::SkipValue(r);   // not a string: not one of ours (a non-string dll/api reads as missing)
		}
		return true;
	}

	// ---- one entry --------------------------------------------------------------------------------------------------

	struct Function
	{
		std::string dll;
		std::string api;
		std::string typeText, preText, postText;   // as written (for the log); the enums are what is used
		Type type = Type::kVoid;
		Action pre = Action::kNone;
		Action post = Action::kNone;
	};

	inline bool ReadFunction(const std::filesystem::path& a_file, Function& a_out, std::string& a_err)
	{
		std::ifstream in(a_file, std::ios::binary);
		if (!in.is_open()) { a_err = "the file cannot be opened"; return false; }
		std::stringstream ss;
		ss << in.rdbuf();
		std::map<std::string, std::string> keys;
		if (!ParseTopLevelStrings(ss.str(), keys, a_err)) { return false; }
		auto get = [&](const char* a_k) { const auto it = keys.find(a_k); return it == keys.end() ? std::string() : it->second; };
		Function f;
		f.dll = get("dll");
		f.api = get("api");
		if (f.dll.empty()) { a_err = "no \"dll\" (required)"; return false; }
		if (f.api.empty()) { a_err = "no \"api\" (required)"; return false; }
		f.typeText = get("type");
		f.preText = get("preAction");
		f.postText = get("postAction");
		f.type = TypeFrom(f.typeText);
		f.pre = ActionFrom(f.preText);
		f.post = ActionFrom(f.postText);
		a_out = std::move(f);
		return true;
	}

	// GetModuleHandleW + GetProcAddress, never LoadLibrary. Null with a_missing naming what is not there: the DLL
	// ("ccld_ToggleUI.dll") when it is not loaded, "<dll> ! <api>" when the DLL is loaded but does not export it.
	inline void* Resolve(const Function& a_f, std::string& a_missing)
	{
		const std::wstring wide = Widen(a_f.dll);
		HMODULE module = wide.empty() ? nullptr : ::GetModuleHandleW(wide.c_str());
		if (!module) { a_missing = a_f.dll; return nullptr; }
		FARPROC proc = ::GetProcAddress(module, a_f.api.c_str());
		if (!proc) { a_missing = a_f.dll + " ! " + a_f.api; return nullptr; }
		a_missing.clear();
		return reinterpret_cast<void*>(proc);
	}

	// ---- the guarded call -------------------------------------------------------------------------------------------

	namespace detail
	{
		using VoidFn = void (*)();
		using MessageFn = void (*)(char*, std::size_t);

		// No C++ objects in here, so __try is legal. A C++ exception is passed on (CONTINUE_SEARCH) to the caller's catch,
		// which can read its message; any other fault (an access violation in the callee) stops here with its code.
		inline int SehFilter(unsigned long a_code, unsigned long* a_out)
		{
			if (a_code == 0xE06D7363ul) { return EXCEPTION_CONTINUE_SEARCH; }   // MSVC's C++ exception
			*a_out = a_code;
			return EXCEPTION_EXECUTE_HANDLER;
		}

		inline bool CallSeh(void* a_fn, bool a_message, char* a_buf, std::size_t a_len, unsigned long* a_code)
		{
			__try
			{
				if (a_message) { reinterpret_cast<MessageFn>(a_fn)(a_buf, a_len); }
				else { reinterpret_cast<VoidFn>(a_fn)(); }
				return true;
			}
			__except (SehFilter(GetExceptionCode(), a_code))
			{
				return false;
			}
		}
	}

	struct CallResult
	{
		bool ok = false;
		std::string message;   // Message / MessageBox: the text the function left in the buffer
		std::string error;     // why it failed ("access violation 0xC0000005", "threw: ...")
	};

	inline CallResult Call(void* a_fn, Type a_type)
	{
		CallResult out;
		if (!a_fn) { out.error = "no function"; return out; }
		const bool message = a_type != Type::kVoid;
		std::vector<char> buf(message ? kMessageSize : 1, '\0');
		unsigned long code = 0;
		try
		{
			out.ok = detail::CallSeh(a_fn, message, message ? buf.data() : nullptr, message ? kMessageSize : 0, &code);
			if (!out.ok)
			{
				char hex[16];
				std::snprintf(hex, sizeof(hex), "0x%08lX", code);
				out.error = std::string("the function faulted (") + (code == EXCEPTION_ACCESS_VIOLATION ? "access violation " : "exception ") + hex + ")";
			}
		}
		catch (const std::exception& e)
		{
			out.ok = false;
			out.error = std::string("the function threw: ") + e.what();
		}
		catch (...)
		{
			out.ok = false;
			out.error = "the function threw a non-standard exception";
		}
		if (message)
		{
			buf[kMessageSize - 1] = '\0';
			out.message = buf.data();
		}
		return out;
	}

	// ---- the folder tree --------------------------------------------------------------------------------------------

	struct Node
	{
		std::filesystem::path path;   // as found under the root (relative to the game folder in game)
		std::string name;             // UTF-8: a folder's name, or a file's stem (its button label)
		std::string rel;              // UTF-8, "/" separated, from the menu root: "Mod/Toggle UI/Toggle UI.toml"
		bool folder = false;
		std::vector<Node> children;
	};

	// One folder's contents, recursively: sub-folders and files whose extension is exactly ".toml" (lower case, as MFM
	// requires), mixed and sorted by path as MFM sorts them. Errors (an unreadable folder) are appended to a_problems.
	inline void BuildChildren(Node& a_dir, std::vector<std::string>& a_problems, int a_depth = 0)
	{
		namespace fs = std::filesystem;
		if (a_depth > 32) { a_problems.push_back(a_dir.rel + ": folders nested too deep - not read further"); return; }
		std::error_code ec;
		fs::directory_iterator it(a_dir.path, ec);
		if (ec) { a_problems.push_back(a_dir.rel + ": " + ec.message()); return; }
		for (const fs::directory_iterator end; it != end; it.increment(ec))
		{
			if (ec) { a_problems.push_back(a_dir.rel + ": " + ec.message()); break; }
			const fs::directory_entry& e = *it;
			std::error_code ec2;
			Node n;
			n.path = e.path();
			if (e.is_directory(ec2))
			{
				n.folder = true;
				n.name = Narrow(n.path.filename().wstring());
				n.rel = a_dir.rel + "/" + n.name;
				BuildChildren(n, a_problems, a_depth + 1);
				a_dir.children.push_back(std::move(n));
			}
			else if (e.is_regular_file(ec2) && n.path.extension().native() == L".toml")
			{
				n.name = Narrow(n.path.stem().wstring());
				n.rel = a_dir.rel + "/" + Narrow(n.path.filename().wstring());
				a_dir.children.push_back(std::move(n));
			}
		}
		std::sort(a_dir.children.begin(), a_dir.children.end(), [](const Node& a, const Node& b) { return a.path < b.path; });
	}

	// The tree under <a_root>/<a_group> ("Mod" or "Config"). A missing folder is an empty tree, not a problem.
	inline Node BuildGroup(const std::filesystem::path& a_root, const std::string& a_group, std::vector<std::string>& a_problems)
	{
		Node g;
		g.path = a_root / a_group;
		g.name = a_group;
		g.rel = a_group;
		g.folder = true;
		std::error_code ec;
		if (std::filesystem::is_directory(g.path, ec)) { BuildChildren(g, a_problems); }
		return g;
	}

	inline std::size_t CountFiles(const Node& a_n)
	{
		if (!a_n.folder) { return 1; }
		std::size_t n = 0;
		for (const Node& c : a_n.children) { n += CountFiles(c); }
		return n;
	}

	// Every file in the tree, in drawing order.
	inline void Files(const Node& a_n, std::vector<const Node*>& a_out)
	{
		if (!a_n.folder) { a_out.push_back(&a_n); return; }
		for (const Node& c : a_n.children) { Files(c, a_out); }
	}
}
