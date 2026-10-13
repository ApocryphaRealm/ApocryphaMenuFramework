#include "IniPages.h"

#include "HelpBar.h"
#include "IniCore.h"
#include "PageRefresh.h"
#include "Registry.h"
#include "Settings.h"
#include "Strings.h"
#include "Theme.h"
#include "utils/Logger.h"
#include "utils/ToggleSwitch.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace inipages
{
	namespace
	{
		namespace fs = std::filesystem;

		constexpr std::uintmax_t kMaxBytes = 256 * 1024;   // a settings file is far smaller; anything bigger is data

		// Files whose reader is known. SSE Display Tweaks (SlavicPotato, MIT; read from SSETweaks/config.cpp) reads
		// SSEDisplayTweaks.ini and SSEDisplayTweaks_custom.ini ONCE at game start, the custom file winning key by key, so its
		// changes always go to the custom file - created if it is not there yet.
		struct Known
		{
			const char* base;       // the file name without .ini, lower case
			const char* name;       // the entry is "<name> (INI)"
			bool alwaysCustom;      // write <base>_custom.ini even when it does not exist yet
		};
		constexpr Known kKnown[] = {
			{ "ssedisplaytweaks", "SSE Display Tweaks", true },
		};

		enum class Kind { kBool, kInt, kFloat, kChoice, kText };

		struct Setting
		{
			std::string key;
			std::string value;        // effective: the custom file's if it sets the key, else the main file's
			std::string mainValue;
			bool custom = false;      // the custom file sets it
			std::string title;        // the "##" line
			std::string help;         // the comment lines
			std::vector<std::string> options;
			Kind kind = Kind::kText;
			bool digitBool = false;   // a switch written as 1 / 0 (a "bName" key), not true / false
			bool readOnly = false;    // a text too long for the edit box is shown, never written back cut short
			int decimals = 0;
			char text[256]{};         // kText's edit buffer
		};

		struct Section
		{
			std::string name;
			std::vector<Setting> settings;
		};

		struct Mod
		{
			std::string id;           // "skse/plugins/foo.ini" / "data/foo.ini" - the switch and DevBench name
			std::string base;         // "Foo" (the file name without .ini)
			std::string folder;       // "Data/SKSE/Plugins" or "Data", as shown
			bool dataFolder = false;
			bool gameSettings = false;   // in Data beside a plugin of the same name
			std::string name;         // shown name, without the suffix
			std::string entry;        // the registered entry
			std::string mainIni;      // "Data/SKSE/Plugins/Foo.ini"
			std::string customIni;    // the override file written instead of the main one, or empty (written in place)
			std::vector<Section> sections;
			std::vector<std::string> matchNames;   // normalised names a mod's own page would carry
			std::vector<std::string> pages;        // registered page names (the sections)
			bool registered = false;
			std::string ownPage;      // the entry that has this mod's own page (the INI page was taken away for it)
			bool backupTaken = false;
			int writes = 0;
			std::string lastError;
			// 2.2.1 - how a change takes effect (PageRefresh.h), looked up the first time the page is drawn or a value set
			bool refreshLooked = false;
			pagerefresh::Reload reload;   // the mod's declared in-game reload (its Mod Function Menu Config entry), if any
			bool engine = false;          // the game's own INI settings: set live in the engine's INI collections
			std::vector<std::pair<std::string, std::string>> pendingLive;   // (section, key) saved, to set in the engine next frame
			bool reloadPending = false;
			double pendingSince = 0.0;
			std::string lastApply;
		};

		struct Skipped
		{
			std::string folder;
			std::string file;
			std::string reason;       // shown as written (translated when found)
		};

		std::mutex g_lock;            // guards g_mods / g_skipped (render thread draws, DevBench reads and sets)
		std::vector<Mod> g_mods;      // filled once by Load; never resized after, so a page's index stays good
		std::vector<Skipped> g_skipped;
		bool g_loaded = false;

		std::string Trim(const std::string& a_s)
		{
			const auto b = a_s.find_first_not_of(" \t\r\n");
			if (b == std::string::npos) { return {}; }
			const auto e = a_s.find_last_not_of(" \t\r\n");
			return a_s.substr(b, e - b + 1);
		}

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		using inicore::Label;
		using inicore::Normal;

		bool IsBool(const std::string& a_v) { const std::string l = Lower(a_v); return l == "true" || l == "false"; }

		bool IsInt(const std::string& a_v)
		{
			if (a_v.empty()) { return false; }
			std::size_t i = (a_v[0] == '-' || a_v[0] == '+') ? 1 : 0;
			if (i >= a_v.size()) { return false; }
			for (; i < a_v.size(); ++i) { if (!std::isdigit(static_cast<unsigned char>(a_v[i]))) { return false; } }
			return true;
		}

		// a whole number the number box can hold (an int); a bigger one stays text, so it is never written back changed
		bool FitsInt(const std::string& a_v)
		{
			if (!IsInt(a_v)) { return false; }
			int out = 0;
			const char* b = a_v.data() + (a_v[0] == '+' ? 1 : 0);
			const auto r = std::from_chars(b, a_v.data() + a_v.size(), out);
			return r.ec == std::errc() && r.ptr == a_v.data() + a_v.size();
		}

		// "0.75" -> 2; -1 when not a plain decimal
		int FloatDecimals(const std::string& a_v)
		{
			const auto dot = a_v.find('.');
			if (dot == std::string::npos || dot == 0 || dot + 1 >= a_v.size()) { return -1; }
			if (!IsInt(a_v.substr(0, dot)) && a_v.substr(0, dot) != "-") { return -1; }
			for (std::size_t i = dot + 1; i < a_v.size(); ++i) { if (!std::isdigit(static_cast<unsigned char>(a_v[i]))) { return -1; } }
			return static_cast<int>(a_v.size() - dot - 1);
		}

		void Classify(Setting& a_s)
		{
			a_s.digitBool = false;
			a_s.readOnly = false;
			if (IsBool(a_s.value)) { a_s.kind = Kind::kBool; return; }
			// Skyrim's own convention, and most SKSE plugins': a "bName" key holds 1 or 0
			if ((a_s.value == "0" || a_s.value == "1") && a_s.key.size() > 1 && a_s.key[0] == 'b' &&
				std::isupper(static_cast<unsigned char>(a_s.key[1])))
			{
				a_s.kind = Kind::kBool;
				a_s.digitBool = true;
				return;
			}
			if (FitsInt(a_s.value)) { a_s.kind = Kind::kInt; return; }
			if (const int d = FloatDecimals(a_s.value); d >= 0) { a_s.kind = Kind::kFloat; a_s.decimals = std::clamp(d, 1, 6); return; }
			const std::string l = Lower(a_s.value);
			if (a_s.options.size() >= 2 && std::any_of(a_s.options.begin(), a_s.options.end(), [&](const std::string& o) { return Lower(o) == l; }))
			{
				a_s.kind = Kind::kChoice;
				return;
			}
			a_s.kind = Kind::kText;
			a_s.readOnly = a_s.value.size() >= sizeof(a_s.text) - 1;
			std::snprintf(a_s.text, sizeof(a_s.text), "%s", a_s.value.c_str());
		}

		bool On(const Setting& a_s) { return a_s.digitBool ? a_s.value == "1" : Lower(a_s.value) == "true"; }

		using Flat = std::vector<std::pair<std::string, std::string>>;   // "section\nkey" (lower) -> value

		// The file's lines. a_bom: it starts with a UTF-8 byte-order mark (Display Tweaks' own INI does), stripped here and put
		// back by WriteKey. a_utf16: it is UTF-16 text, which this reader does not take.
		bool ReadLines(const std::string& a_path, std::vector<std::string>& a_lines, bool& a_crlf, bool* a_bom = nullptr, bool* a_utf16 = nullptr)
		{
			std::ifstream in(fs::path(a_path), std::ios::binary);
			if (!in.is_open()) { return false; }
			std::stringstream ss;
			ss << in.rdbuf();
			std::string all = ss.str();
			if (a_utf16) { *a_utf16 = all.size() >= 2 && ((all[0] == '\xFF' && all[1] == '\xFE') || (all[0] == '\xFE' && all[1] == '\xFF')); }
			const bool bom = all.size() >= 3 && all.compare(0, 3, "\xEF\xBB\xBF") == 0;
			if (bom) { all.erase(0, 3); }
			if (a_bom) { *a_bom = bom; }
			a_crlf = all.find("\r\n") != std::string::npos;
			std::string line;
			std::istringstream ls(all);
			while (std::getline(ls, line))
			{
				if (!line.empty() && line.back() == '\r') { line.pop_back(); }
				a_lines.push_back(line);
			}
			return true;
		}

		bool IsSection(const std::string& a_t) { return a_t.size() >= 2 && a_t.front() == '[' && a_t.back() == ']'; }

		// An active "key = value" line: not a comment, has '=' and a name before it. Returns false otherwise.
		bool ParseKeyLine(const std::string& a_line, std::string& a_key, std::string& a_value)
		{
			const std::string t = Trim(a_line);
			if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[') { return false; }
			const auto eq = t.find('=');
			if (eq == std::string::npos || eq == 0) { return false; }
			a_key = Trim(t.substr(0, eq));
			a_value = Trim(t.substr(eq + 1));
			// an inline " ;comment" is not part of the value (inih, and the game, read it so)
			for (const char* mark : { " ;", "\t;" })
			{
				if (const auto sc = a_value.find(mark); sc != std::string::npos) { a_value = Trim(a_value.substr(0, sc)); }
			}
			return !a_key.empty();
		}

		// Whether the file reads cleanly: every line blank, a comment, a [section] or a key = value under a section; no section
		// and no key (within its section) given twice. Empty = clean; otherwise the reason, in the player's language.
		std::string CleanProblem(const std::vector<std::string>& a_lines)
		{
			std::set<std::string> sections;
			std::set<std::string> keys;   // "section\nkey"
			std::string section;
			bool any = false;
			char buf[512];
			for (std::size_t i = 0; i < a_lines.size(); ++i)
			{
				const std::string t = Trim(a_lines[i]);
				if (t.empty() || t[0] == '#' || t[0] == ';') { continue; }
				if (IsSection(t))
				{
					section = Lower(Trim(t.substr(1, t.size() - 2)));
					if (!sections.insert(section).second)
					{
						std::snprintf(buf, sizeof(buf), strings::TR("AMF_IniWhySectionTwice", "[%s] appears twice"), Trim(t.substr(1, t.size() - 2)).c_str());
						return buf;
					}
					any = true;
					continue;
				}
				std::string k, v;
				if (!ParseKeyLine(a_lines[i], k, v))
				{
					std::snprintf(buf, sizeof(buf), strings::TR("AMF_IniWhyBadLine", "line %d is not a section, a setting or a comment"), static_cast<int>(i + 1));
					return buf;
				}
				if (!any)
				{
					std::snprintf(buf, sizeof(buf), strings::TR("AMF_IniWhyKeyFirst", "line %d sets a key before any [section]"), static_cast<int>(i + 1));
					return buf;
				}
				if (!keys.insert(section + "\n" + Lower(k)).second)
				{
					std::snprintf(buf, sizeof(buf), strings::TR("AMF_IniWhyKeyTwice", "%s is set twice in one section"), k.c_str());
					return buf;
				}
			}
			if (!any) { return strings::TR("AMF_IniWhyNoSection", "no [section] in it"); }
			return {};
		}

		Flat ReadCustom(const std::string& a_path)
		{
			Flat out;
			std::vector<std::string> lines;
			bool crlf = false;
			if (!ReadLines(a_path, lines, crlf)) { return out; }
			std::string section;
			for (const std::string& l : lines)
			{
				const std::string t = Trim(l);
				if (IsSection(t)) { section = Lower(Trim(t.substr(1, t.size() - 2))); continue; }
				std::string k, v;
				if (ParseKeyLine(l, k, v)) { out.emplace_back(section + "\n" + Lower(k), v); }
			}
			return out;
		}

		// The main INI, in its own order, with the comment block above each key as its title and help.
		std::vector<Section> ReadMain(const std::vector<std::string>& a_lines)
		{
			std::vector<Section> out;
			std::string title;
			std::vector<std::string> help;
			std::vector<std::string> options;
			auto reset = [&]() { title.clear(); help.clear(); options.clear(); };
			for (const std::string& l : a_lines)
			{
				const std::string t = Trim(l);
				if (t.empty()) { continue; }   // a blank line inside a block keeps the block (the key follows it)
				if (IsSection(t))
				{
					out.push_back({ Trim(t.substr(1, t.size() - 2)), {} });
					reset();
					continue;
				}
				if (t[0] == '#' || t[0] == ';')
				{
					// "#Key=value" with no space after the mark: a key commented out - its block is not the next key's
					if (t.size() > 1 && std::isalpha(static_cast<unsigned char>(t[1])) && t.find('=') != std::string::npos)
					{
						reset();
						continue;
					}
					if (t.rfind("##", 0) == 0) { reset(); title = Trim(t.substr(2)); continue; }
					std::string body = t.substr(1);
					// "#    word" or "#    word  - what it does": an option, if the key's value turns out to be one of them
					// (indented four or more, the word alone or followed by " - ", as Display Tweaks lists them)
					if (body.size() > 4 && body.compare(0, 4, "    ") == 0)
					{
						const std::string b = Trim(body);
						const std::string word = b.substr(0, b.find_first_of(" \t"));
						const std::string rest = Trim(b.substr(word.size()));
						if (!word.empty() && word.find_first_of("=:'\"") == std::string::npos && (rest.empty() || rest[0] == '-'))
						{
							options.push_back(word);
						}
					}
					help.push_back(Trim(body));
					continue;
				}
				std::string k, v;
				if (out.empty() || !ParseKeyLine(l, k, v)) { reset(); continue; }
				Setting s;
				s.key = k;
				s.value = s.mainValue = v;
				s.title = title;
				// the help: its lines joined, an empty comment line kept as a paragraph break
				std::string h;
				for (const std::string& hl : help)
				{
					if (hl.empty()) { if (!h.empty() && h.back() != '\n') { h += "\n"; } continue; }
					if (!h.empty() && h.back() != '\n') { h += ' '; }
					h += hl;
				}
				s.help = Trim(h);
				s.options = options;
				out.back().settings.push_back(std::move(s));
				reset();
			}
			// sections with nothing to set are not pages
			out.erase(std::remove_if(out.begin(), out.end(), [](const Section& a_s) { return a_s.settings.empty(); }), out.end());
			return out;
		}

		void ApplyCustom(Mod& a_mod)
		{
			const Flat custom = a_mod.customIni.empty() ? Flat{} : ReadCustom(a_mod.customIni);
			for (Section& sec : a_mod.sections)
			{
				for (Setting& s : sec.settings)
				{
					const std::string want = Lower(sec.name) + "\n" + Lower(s.key);
					s.value = s.mainValue;
					s.custom = false;
					for (const auto& [k, v] : custom)
					{
						if (k == want) { s.value = v; s.custom = true; }   // the last one wins, as inih reads it
					}
					Classify(s);
				}
			}
		}

		// The line with only its value changed: the key, the spacing round '=', an inline comment and anything after it kept.
		std::string WithValue(const std::string& a_line, const std::string& a_value)
		{
			const auto eq = a_line.find('=');
			std::size_t start = eq + 1;
			while (start < a_line.size() && (a_line[start] == ' ' || a_line[start] == '\t')) { ++start; }
			std::size_t end = a_line.size();
			for (const char* mark : { " ;", "\t;" })
			{
				if (const auto sc = a_line.find(mark, start); sc != std::string::npos) { end = std::min(end, sc); }
			}
			while (end > start && (a_line[end - 1] == ' ' || a_line[end - 1] == '\t')) { --end; }
			return a_line.substr(0, start) + a_value + a_line.substr(end);
		}

		// Write one key: to the custom file (replaced in place, or added to its section, or the section added), or in place in
		// the main file (only that key's value). The rest of the file is left exactly as it was. Called with g_lock held.
		bool WriteKey(Mod& a_mod, const std::string& a_section, const std::string& a_key, const std::string& a_value, std::string& a_error)
		{
			const bool inPlace = a_mod.customIni.empty();
			const std::string path = inPlace ? a_mod.mainIni : a_mod.customIni;
			std::vector<std::string> lines;
			bool crlf = true;
			bool bom = false;
			bool utf16 = false;
			std::error_code ec;
			const bool existed = fs::exists(fs::path(path), ec) && ReadLines(path, lines, crlf, &bom, &utf16);
			if (inPlace && !existed) { a_error = "could not read " + path; logger::error("ini pages: {}; nothing written", a_error); return false; }
			// never write a file that does not read cleanly NOW (it may have changed since the game started)
			if (existed)
			{
				std::string problem = utf16 ? std::string(strings::TR("AMF_IniWhyUtf16", "UTF-16 text, which this reader does not take")) : CleanProblem(lines);
				if (!problem.empty())
				{
					a_error = path + " does not read cleanly (" + problem + ")";
					logger::error("ini pages: {}; nothing written", a_error);
					return false;
				}
			}
			const std::string wantSection = Lower(a_section), wantKey = Lower(a_key);
			int secStart = -1, secEnd = static_cast<int>(lines.size()), keyLine = -1;
			for (int i = 0; i < static_cast<int>(lines.size()); ++i)
			{
				const std::string t = Trim(lines[i]);
				if (IsSection(t))
				{
					if (secStart >= 0 && secEnd == static_cast<int>(lines.size())) { secEnd = i; }
					if (Lower(Trim(t.substr(1, t.size() - 2))) == wantSection && secStart < 0) { secStart = i; secEnd = static_cast<int>(lines.size()); }
					continue;
				}
				std::string k, v;
				if (secStart >= 0 && i < secEnd && ParseKeyLine(lines[i], k, v) && Lower(k) == wantKey) { keyLine = i; }
			}
			if (inPlace && keyLine < 0)
			{
				a_error = "[" + a_section + "] " + a_key + " is no longer in " + path;
				logger::error("ini pages: {}; nothing written", a_error);
				return false;
			}
			if (existed && !a_mod.backupTaken)
			{
				// the file as it first was, kept once (an older backup is never replaced)
				const std::string backup = path + ".amf-backup";
				if (!fs::exists(fs::path(backup), ec))
				{
					fs::copy_file(fs::path(path), fs::path(backup), ec);
					if (ec) { a_error = "could not keep a backup (" + ec.message() + ")"; logger::error("ini pages: {} - {}; nothing written", path, a_error); return false; }
					logger::info("ini pages: {} kept as {} before the first change", path, backup);
				}
				a_mod.backupTaken = true;
			}
			if (keyLine >= 0) { lines[keyLine] = WithValue(lines[keyLine], a_value); }
			else if (secStart >= 0)
			{
				int at = secEnd;
				while (at - 1 > secStart && Trim(lines[at - 1]).empty()) { --at; }
				lines.insert(lines.begin() + at, a_key + " = " + a_value);
			}
			else
			{
				if (!lines.empty() && !Trim(lines.back()).empty()) { lines.push_back(""); }
				lines.push_back("[" + a_section + "]");
				lines.push_back(a_key + " = " + a_value);
			}
			const std::string tmp = path + ".amf-tmp";
			{
				std::ofstream out(fs::path(tmp), std::ios::binary | std::ios::trunc);
				if (!out.is_open()) { a_error = "could not write " + tmp; logger::error("ini pages: {}; nothing written", a_error); return false; }
				if (bom) { out << "\xEF\xBB\xBF"; }
				for (const std::string& l : lines) { out << l << (crlf ? "\r\n" : "\n"); }
				out.flush();
				if (!out.good()) { a_error = "the write to " + tmp + " failed"; logger::error("ini pages: {}", a_error); return false; }
			}
			fs::rename(fs::path(tmp), fs::path(path), ec);
			if (ec)
			{
				a_error = "could not put the new file in place (" + ec.message() + ")";
				logger::error("ini pages: {} - {}; {} left as it was", path, a_error, path);
				fs::remove(fs::path(tmp), ec);
				return false;
			}
			++a_mod.writes;
			logger::info("ini pages: [{}] {} = {} written to {}{}", a_section, a_key, a_value, path,
						 existed ? (inPlace ? " (in place, that line only)" : "") : " (a new file)");
			return true;
		}

		// Set one value as the page does: checked against its kind, written, the model updated. Called with g_lock held.
		bool SetValue(Mod& a_mod, Section& a_sec, Setting& a_s, std::string a_value, std::string& a_error)
		{
			a_value = Trim(a_value);
			switch (a_s.kind)
			{
			case Kind::kBool:
				if (a_s.digitBool)
				{
					if (IsBool(a_value)) { a_value = Lower(a_value) == "true" ? "1" : "0"; }
					if (a_value != "0" && a_value != "1") { a_error = a_s.key + " takes 1 or 0"; return false; }
				}
				else
				{
					if (!IsBool(a_value)) { a_error = a_s.key + " takes true or false"; return false; }
					a_value = Lower(a_value);
				}
				break;
			case Kind::kInt:
				if (!FitsInt(a_value)) { a_error = a_s.key + " takes a whole number"; return false; }
				break;
			case Kind::kFloat:
				if (!IsInt(a_value) && FloatDecimals(a_value) < 0) { a_error = a_s.key + " takes a number"; return false; }
				break;
			case Kind::kChoice:
				if (std::none_of(a_s.options.begin(), a_s.options.end(), [&](const std::string& o) { return Lower(o) == Lower(a_value); }))
				{
					a_error = a_s.key + " takes one of its listed options";
					return false;
				}
				break;
			case Kind::kText:
				if (a_s.readOnly) { a_error = a_s.key + " is too long to change here"; return false; }
				if (a_value.find_first_of("\r\n") != std::string::npos) { a_error = "one line only"; return false; }
				break;
			}
			if (!WriteKey(a_mod, a_sec.name, a_s.key, a_value, a_error))
			{
				a_mod.lastError = a_error;
				return false;
			}
			a_s.value = a_value;
			a_s.custom = !a_mod.customIni.empty();
			if (a_s.kind == Kind::kText) { std::snprintf(a_s.text, sizeof(a_s.text), "%s", a_value.c_str()); }
			a_mod.lastError.clear();
			// 2.2.1: the change takes effect the page's way - set in the engine, and / or the mod's declared reload - on the
			// render thread, by RefreshTick (a DevBench set arrives on its listener thread)
			if (a_mod.engine) { a_mod.pendingLive.emplace_back(a_sec.name, a_s.key); }
			if (a_mod.reload.found)
			{
				a_mod.reloadPending = true;
				a_mod.pendingSince = ImGui::GetCurrentContext() ? ImGui::GetTime() : 0.0;
			}
			return true;
		}

		// 2.2.1: the page's declared reload and whether it is the game's own INI settings, found once. The game's own: a Data INI
		// beside its plugin (the game reads it as that plugin's game settings), or an INI whose every key is one of the
		// engine's INI settings ("<key>:<section>"). Called with g_lock held, once the game's settings exist (kDataLoaded on).
		void LookRefresh(Mod& a_mod)
		{
			if (a_mod.refreshLooked) { return; }
			a_mod.refreshLooked = true;
			a_mod.reload = pagerefresh::Find(refreshcore::Candidates(a_mod.base, ""));
			std::size_t keys = 0, known = 0;
			for (const Section& sec : a_mod.sections)
			{
				for (const Setting& s : sec.settings)
				{
					++keys;
					known += pagerefresh::EngineIni(s.key, sec.name) ? 1 : 0;
				}
			}
			a_mod.engine = a_mod.gameSettings || (keys > 0 && known == keys);
			logger::info("ini pages: {} - {}{} ({} of {} key(s) are the game's own INI settings)", a_mod.entry,
						 a_mod.reload.found ? "reloads in game via " + a_mod.reload.decl.dll + " ! " + a_mod.reload.decl.api : std::string("no declared reload"),
						 a_mod.engine ? ", set live in the game's INI settings" : "", known, keys);
		}

		pagerefresh::Kind KindOf(Mod& a_mod)
		{
			LookRefresh(a_mod);
			if (a_mod.reload.found && pagerefresh::Loaded(a_mod.reload)) { return pagerefresh::Kind::kReload; }
			return a_mod.engine ? pagerefresh::Kind::kLive : pagerefresh::Kind::kNextLaunch;
		}

		const Setting* FindSetting(const Mod& a_mod, const std::string& a_section, const std::string& a_key)
		{
			for (const Section& sec : a_mod.sections)
			{
				if (sec.name != a_section) { continue; }
				for (const Setting& s : sec.settings) { if (s.key == a_key) { return &s; } }
			}
			return nullptr;
		}

		// The saved values into the engine, now. Render thread, g_lock held.
		void ApplyLive(Mod& a_mod)
		{
			if (a_mod.pendingLive.empty()) { return; }
			std::string now, next;
			for (const auto& [section, key] : a_mod.pendingLive)
			{
				const Setting* s = FindSetting(a_mod, section, key);
				RE::Setting* engine = pagerefresh::EngineIni(key, section);
				const bool set = s && engine && pagerefresh::SetFromText(engine, s->value);
				std::string& list = set ? now : next;
				if (list.find("[" + section + "] " + key) == std::string::npos) { list += (list.empty() ? "" : ", ") + ("[" + section + "] " + key); }
				logger::info("ini pages: {} [{}] {} = {} - {}", a_mod.id, section, key, s ? s->value : std::string("?"),
							 set ? "set in the game's INI settings now" : "not one the game holds (or no plain value) - applies the next time the game starts");
			}
			a_mod.pendingLive.clear();
			char buf[1024];
			a_mod.lastApply.clear();
			if (!now.empty())
			{
				std::snprintf(buf, sizeof(buf), strings::TR("AMF_RefreshLiveSet", "Set in the game now: %s."), now.c_str());
				a_mod.lastApply = buf;
			}
			if (!next.empty())
			{
				std::snprintf(buf, sizeof(buf), strings::TR("AMF_RefreshLiveNot", "Applies the next time the game starts: %s."), next.c_str());
				a_mod.lastApply += (a_mod.lastApply.empty() ? "" : " ") + std::string(buf);
			}
		}

		// The mod's declared reload, now. Render thread, g_lock held.
		void RunReload(Mod& a_mod, const char* a_why)
		{
			a_mod.reloadPending = false;
			if (!a_mod.reload.found) { return; }
			const pagerefresh::Called c = pagerefresh::Call(a_mod.reload);
			const std::string text = pagerefresh::CalledText(a_mod.reload, c);
			a_mod.lastApply += (a_mod.lastApply.empty() ? "" : " ") + text;
			logger::info("ini pages: {} - reload ({}): {}", a_mod.entry, a_why, text);
		}

		std::string FormatFloat(double a_v, int a_decimals)
		{
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%.*f", a_decimals, a_v);
			return buf;
		}

		std::string FileName(const std::string& a_path) { return fs::path(a_path).filename().string(); }

		void DrawPage(std::size_t a_mod, const std::string& a_section)
		{
			std::scoped_lock l(g_lock);
			if (a_mod >= g_mods.size()) { return; }
			Mod& mod = g_mods[a_mod];
			auto sec = std::find_if(mod.sections.begin(), mod.sections.end(), [&](const Section& s) { return s.name == a_section; });
			if (sec == mod.sections.end()) { ImGui::TextUnformatted("?"); return; }
			helpbar::Want("ini|" + mod.id + "|" + a_section);
			LookRefresh(mod);

			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			// 2.2.1: how a change takes effect - reloaded in game, set live, or the next time the game starts (PageRefresh.h)
			const pagerefresh::Kind kind = KindOf(mod);
			if (kind != pagerefresh::Kind::kNextLaunch) { ImGui::TextWrapped("%s", pagerefresh::MethodText(kind, mod.reload.via).c_str()); }
			else if (mod.gameSettings)
			{
				ImGui::TextWrapped(strings::TR("AMF_IniGameRestart", "The game reads this file as %s's game settings when it starts: a change takes "
								   "effect the next time you start the game."), mod.name.c_str());
			}
			else
			{
				ImGui::TextWrapped(strings::TR("AMF_IniRestart", "%s reads these when the game starts: a change takes effect the next time you start the game."),
								   mod.name.c_str());
			}
			if (!mod.customIni.empty())
			{
				ImGui::TextWrapped(strings::TR("AMF_IniWrites", "Changes are written to %s; %s itself is never changed."), FileName(mod.customIni).c_str(),
								   FileName(mod.mainIni).c_str());
			}
			else
			{
				const std::string file = FileName(mod.mainIni);
				ImGui::TextWrapped(strings::TR("AMF_IniWritesInPlace", "Changes are written into %s itself, only the value you change; the file as it "
								   "first was is kept as %s.amf-backup."), file.c_str(), file.c_str());
			}
			if (!mod.lastError.empty())
			{
				ImGui::TextWrapped(strings::TR("AMF_IniError", "Not saved: %s"), mod.lastError.c_str());
			}
			if (!mod.lastApply.empty()) { ImGui::TextWrapped("%s", mod.lastApply.c_str()); }
			ImGui::PopStyleColor();
			if (mod.reload.found)
			{
				const bool loaded = pagerefresh::Loaded(mod.reload);
				if (!loaded) { ImGui::BeginDisabled(); }
				if (ImGui::Button(strings::TR("AMF_RefreshButton", "Reload")))
				{
					mod.lastApply.clear();
					RunReload(mod, "Reload button");
				}
				if (!loaded) { ImGui::EndDisabled(); }
			}
			ImGui::Spacing();

			const char* customMark = strings::TR("AMF_IniCustomMark", "(changed)");
			for (Setting& s : sec->settings)
			{
				ImGui::PushID(s.key.c_str());
				const std::string label = Label(s.key, true);   // shown only: the file keeps its own key name
				const std::string help = s.title.empty() ? s.help : (s.help.empty() ? s.title : s.title + "\n" + s.help);
				std::string error;
				switch (s.kind)
				{
				case Kind::kBool:
				{
					bool b = On(s);
					if (widgets::Toggle(label.c_str(), &b))
					{
						SetValue(mod, *sec, s, s.digitBool ? (b ? "1" : "0") : (b ? "true" : "false"), error);
					}
					break;
				}
				case Kind::kInt:
				{
					int v = std::atoi(s.value.c_str());
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
					if (ImGui::InputInt(label.c_str(), &v, 1, 10)) { SetValue(mod, *sec, s, std::to_string(v), error); }
					break;
				}
				case Kind::kFloat:
				{
					// a number box stepping ONE unit of the last digit the file shows (rule 68): "0.75" steps 0.01
					double v = std::atof(s.value.c_str());
					const double step = std::pow(10.0, -s.decimals);
					char fmt[16];
					std::snprintf(fmt, sizeof(fmt), "%%.%df", s.decimals);
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
					if (ImGui::InputDouble(label.c_str(), &v, step, step * 10.0, fmt))
					{
						v = std::round(v / step) * step;
						SetValue(mod, *sec, s, FormatFloat(v, s.decimals), error);
					}
					break;
				}
				case Kind::kChoice:
				{
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
					if (ImGui::BeginCombo(label.c_str(), s.value.c_str()))
					{
						for (const std::string& o : s.options)
						{
							const bool selected = Lower(o) == Lower(s.value);
							if (ImGui::Selectable(o.c_str(), selected) && !selected) { SetValue(mod, *sec, s, o, error); }
							if (selected) { ImGui::SetItemDefaultFocus(); }
						}
						ImGui::EndCombo();
					}
					break;
				}
				case Kind::kText:
				{
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
					ImGui::InputText(label.c_str(), s.text, sizeof(s.text), s.readOnly ? ImGuiInputTextFlags_ReadOnly : 0);
					if (!s.readOnly && ImGui::IsItemDeactivatedAfterEdit() && s.value != s.text) { SetValue(mod, *sec, s, s.text, error); }
					break;
				}
				}
				if (!help.empty() && (ImGui::IsItemHovered() || ImGui::IsItemFocused())) { helpbar::OfferForLastItem(help); }
				if (s.custom || s.value != s.mainValue)
				{
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
					ImGui::TextUnformatted(customMark);
					ImGui::PopStyleColor();
				}
				if (!error.empty()) { logger::warn("ini pages: {} [{}] {} not set - {}", mod.id, sec->name, s.key, error); }
				ImGui::PopID();
			}
		}

		const char* KindName(Kind a_k)
		{
			switch (a_k)
			{
			case Kind::kBool: return "bool";
			case Kind::kInt: return "int";
			case Kind::kFloat: return "float";
			case Kind::kChoice: return "choice";
			default: return "text";
			}
		}

		std::string J(const std::string& a_s) { return nlohmann::json(a_s).dump(); }

		// a loaded DLL's ProductName, else FileDescription - the first that is a real name (inicore::UsableDllName: not a
		// Visual Studio template placeholder, not the file name); empty when the DLL is not loaded or names neither
		std::string DllName(const std::wstring& a_dll, const std::string& a_dllFile)
		{
			HMODULE module = GetModuleHandleW(a_dll.c_str());
			if (!module) { return {}; }
			wchar_t path[MAX_PATH]{};
			if (!GetModuleFileNameW(module, path, MAX_PATH)) { return {}; }
			DWORD handle = 0;
			const DWORD size = GetFileVersionInfoSizeW(path, &handle);
			if (size == 0) { return {}; }
			std::vector<std::uint8_t> data(size);
			if (!GetFileVersionInfoW(path, 0, size, data.data())) { return {}; }
			struct LangCp { WORD lang; WORD cp; };
			LangCp* langs = nullptr;
			UINT bytes = 0;
			std::vector<std::wstring> prefixes;
			if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&langs), &bytes) && langs)
			{
				for (UINT i = 0; i < bytes / sizeof(LangCp); ++i)
				{
					wchar_t p[64];
					swprintf_s(p, L"\\StringFileInfo\\%04x%04x\\", langs[i].lang, langs[i].cp);
					prefixes.emplace_back(p);
				}
			}
			prefixes.emplace_back(L"\\StringFileInfo\\040904b0\\");
			std::vector<std::string> candidates;
			for (const wchar_t* field : { L"ProductName", L"FileDescription" })
			{
				for (const std::wstring& pre : prefixes)
				{
					wchar_t* value = nullptr;
					UINT len = 0;
					if (VerQueryValueW(data.data(), (pre + field).c_str(), reinterpret_cast<void**>(&value), &len) && value && len > 1)
					{
						const std::wstring w(value);
						std::string s(static_cast<std::size_t>(std::max(0, WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr))), '\0');
						if (!s.empty()) { WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), static_cast<int>(s.size()), nullptr, nullptr); }
						candidates.push_back(s);
					}
				}
			}
			return inicore::FirstUsableDllName(candidates, a_dllFile);
		}

		void Skip(const std::string& a_folder, const std::string& a_file, const std::string& a_reason)
		{
			g_skipped.push_back({ a_folder, a_file, a_reason });
			logger::info("ini pages: {}/{} - no page: {}", a_folder, a_file, a_reason);
		}

		// 2.2.0: each page starts off; [IniMenus] sOn lists the ones the player turned on (IniCore.h, inicore::ParseIdList)
		std::set<std::string> OnSet() { return inicore::ParseIdList(settings::Get().iniPagesOn); }

		bool PageOn(const Mod& a_mod, const std::set<std::string>& a_on) { return inicore::PageShown(settings::Get().iniMenus, a_on, a_mod.id); }

		// Called with g_lock held.
		void ApplyVisibilityLocked()
		{
			const std::set<std::string> on = OnSet();
			for (const Mod& m : g_mods)
			{
				if (!m.registered) { continue; }
				const bool show = PageOn(m, on);
				for (const std::string& p : m.pages) { registry::SetPageVisible(m.entry.c_str(), p.c_str(), show); }
			}
		}

		void Scan(const fs::path& a_dir, const std::string& a_shown, bool a_data)
		{
			std::error_code ec;
			if (!fs::is_directory(a_dir, ec)) { return; }
			std::vector<fs::path> files;
			for (const auto& de : fs::directory_iterator(a_dir, ec))
			{
				std::error_code fe;
				if (!de.is_regular_file(fe)) { continue; }
				if (Lower(de.path().extension().string()) == ".ini") { files.push_back(de.path()); }
			}
			std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) { return Lower(a.filename().string()) < Lower(b.filename().string()); });
			std::set<std::string> present;
			for (const fs::path& f : files) { present.insert(Lower(f.filename().string())); }
			char buf[512];
			for (const fs::path& f : files)
			{
				// a name outside plain ASCII is left out: its narrow form would not name the same file again
				{
					const std::wstring w = f.filename().wstring();
					if (std::any_of(w.begin(), w.end(), [](wchar_t c) { return c > 0x7E || c < 0x20; }))
					{
						Skip(a_shown, "?.ini", strings::TR("AMF_IniWhyName", "its file name is not plain ASCII"));
						continue;
					}
				}
				const std::string file = f.filename().string();
				const std::string lowerFile = Lower(file);
				const std::string base = f.stem().string();
				const std::string lowerBase = Lower(base);
				if (lowerBase.rfind("apocryphamenuframework", 0) == 0) { Skip(a_shown, file, strings::TR("AMF_IniWhyOwn", "AMF's own settings")); continue; }
				if (lowerFile == "skyrim.ini" || lowerFile == "skyrimprefs.ini" || lowerFile == "skyrimcustom.ini")
				{
					Skip(a_shown, file, strings::TR("AMF_IniWhyGame", "the game's own INI - never touched here"));
					continue;
				}
				if (lowerBase.size() > 7 && lowerBase.compare(lowerBase.size() - 7, 7, "_custom") == 0 &&
					present.contains(lowerBase.substr(0, lowerBase.size() - 7) + ".ini"))
				{
					const std::string mainFile = base.substr(0, base.size() - 7) + ".ini";
					std::snprintf(buf, sizeof(buf), strings::TR("AMF_IniWhyCustom", "read as part of %s's page"), mainFile.c_str());
					Skip(a_shown, file, buf);
					continue;
				}
				const std::uintmax_t bytes = fs::file_size(f, ec);
				if (ec || bytes > kMaxBytes) { Skip(a_shown, file, strings::TR("AMF_IniWhyBig", "over 256 KB - too big for a settings file")); continue; }
				std::vector<std::string> lines;
				bool crlf = false, utf16 = false;
				if (!ReadLines(f.string(), lines, crlf, nullptr, &utf16)) { Skip(a_shown, file, strings::TR("AMF_IniWhyUnread", "could not be opened")); continue; }
				if (utf16) { Skip(a_shown, file, strings::TR("AMF_IniWhyUtf16", "UTF-16 text, which this reader does not take")); continue; }
				if (const std::string problem = CleanProblem(lines); !problem.empty()) { Skip(a_shown, file, problem); continue; }
				Mod mod;
				mod.base = base;
				mod.folder = a_shown;
				mod.dataFolder = a_data;
				mod.id = Lower((a_data ? std::string("data/") : std::string("skse/plugins/")) + file);
				mod.mainIni = (a_dir / file).generic_string();
				mod.sections = ReadMain(lines);
				if (mod.sections.empty()) { Skip(a_shown, file, strings::TR("AMF_IniWhyNoKeys", "no setting under a section")); continue; }
				const Known* known = nullptr;
				for (const Known& k : kKnown) { if (lowerBase == k.base) { known = &k; } }
				const fs::path custom = a_dir / (base + "_custom.ini");
				if ((known && known->alwaysCustom) || present.contains(lowerBase + "_custom.ini"))
				{
					mod.customIni = custom.generic_string();
					if (fs::exists(custom, ec))
					{
						std::vector<std::string> cl;
						bool ccrlf = false, cutf16 = false;
						const bool read = ReadLines(custom.string(), cl, ccrlf, nullptr, &cutf16);
						const std::string problem = !read ? std::string(strings::TR("AMF_IniWhyUnread", "could not be opened"))
														  : (cutf16 ? std::string(strings::TR("AMF_IniWhyUtf16", "UTF-16 text, which this reader does not take")) : CleanProblem(cl));
						if (!problem.empty())
						{
							std::snprintf(buf, sizeof(buf), strings::TR("AMF_IniWhyCustomBad", "its %s does not read cleanly (%s)"),
										  custom.filename().string().c_str(), problem.c_str());
							Skip(a_shown, file, buf);
							continue;
						}
					}
				}
				// the name: the table's, a loaded DLL's own, the plugin's (a Data INI beside one), else the file's words
				const std::string dll = DllName(fs::path(base + ".dll").wstring(), base + ".dll");
				std::string plugin;
				if (a_data)
				{
					for (const char* ext : { ".esp", ".esm", ".esl" })
					{
						if (fs::exists(a_dir / (base + ext), ec)) { plugin = base + ext; break; }
					}
				}
				mod.gameSettings = !plugin.empty();
				mod.name = known ? std::string(known->name) : (!plugin.empty() ? plugin : (!dll.empty() ? Label(dll, false) : Label(base, false)));
				mod.entry = mod.name + (mod.gameSettings ? " (INI, game settings)" : " (INI)");
				for (const std::string& n : { base, dll, mod.name, plugin.empty() ? std::string() : base })
				{
					if (const std::string k = Normal(n); !k.empty() && std::find(mod.matchNames.begin(), mod.matchNames.end(), k) == mod.matchNames.end())
					{
						mod.matchNames.push_back(k);
					}
				}
				ApplyCustom(mod);
				g_mods.push_back(std::move(mod));
			}
		}
	}

	void Load()
	{
		std::scoped_lock l(g_lock);
		if (g_loaded) { return; }   // once per session
		g_loaded = true;
		Scan(fs::path("Data/SKSE/Plugins"), "Data/SKSE/Plugins", false);
		Scan(fs::path("Data"), "Data", true);
		// two files giving the same entry name (the same name in both folders): the second says which folder it is in
		std::set<std::string> names;
		for (Mod& m : g_mods)
		{
			if (!names.insert(Lower(m.entry)).second) { m.entry = m.name + " (INI, " + m.folder + ")"; names.insert(Lower(m.entry)); }
		}
		for (const Mod& m : g_mods)
		{
			std::size_t keys = 0, custom = 0;
			for (const Section& s : m.sections)
			{
				keys += s.settings.size();
				for (const Setting& st : s.settings) { custom += st.custom ? 1 : 0; }
			}
			logger::info("ini pages: {} - {} key(s) in {} section(s) from {}{}", m.entry, keys, m.sections.size(), m.mainIni,
						 m.customIni.empty() ? std::string(", written in place") : ", " + std::to_string(custom) + " set by " + m.customIni + " (written there)");
		}
		logger::info("ini pages: {} file(s) with a page, {} not shown", g_mods.size(), g_skipped.size());
	}

	void Sync()
	{
		std::scoped_lock l(g_lock);
		if (g_mods.empty()) { return; }
		std::set<std::string> ours;
		for (const Mod& m : g_mods) { ours.insert(m.entry); }
		// every OTHER entry, against each INI: by its title, and by the DLL that registered it
		std::vector<registry::Entry> others;
		for (registry::Entry& e : registry::Snapshot())
		{
			if (ours.contains(e.modName)) { continue; }
			e.pages.clear();   // only the name and the module are read here
			others.push_back(std::move(e));
		}
		bool changed = false;
		for (std::size_t i = 0; i < g_mods.size(); ++i)
		{
			Mod& m = g_mods[i];
			if (!m.ownPage.empty()) { continue; }
			std::string own;
			for (const registry::Entry& e : others)
			{
				if (inicore::IsOwnPage(m.matchNames, m.base, e.modName, e.module)) { own = e.modName; break; }
			}
			if (!own.empty())
			{
				m.ownPage = own;
				if (m.registered)
				{
					registry::Remove(m.entry, "");
					m.registered = false;
					m.pages.clear();
				}
				logger::info("ini pages: {} - no page: {} has its own page here (\"{}\")", m.mainIni, m.name, own);
				continue;
			}
			if (m.registered) { continue; }
			for (const Section& s : m.sections)
			{
				// the tab shows the section's words; the page reads and writes the section by its own name. Two sections
				// whose words come out the same keep their own names
				std::string tab = Label(s.name, false);
				if (std::find(m.pages.begin(), m.pages.end(), tab) != m.pages.end()) { tab = s.name; }
				const std::string section = s.name;
				if (registry::RegisterFn(m.entry.c_str(), tab.c_str(), [i, section]() { DrawPage(i, section); }, true)) { m.pages.push_back(tab); }
			}
			m.registered = true;
			changed = true;
		}
		if (changed) { ApplyVisibilityLocked(); }
	}

	std::string DllProductName(const std::string& a_base) { return DllName(fs::path(a_base + ".dll").wstring(), a_base + ".dll"); }

	void RefreshTick()
	{
		if (!ImGui::GetCurrentContext()) { return; }
		std::scoped_lock l(g_lock);
		const double t = ImGui::GetTime();
		for (Mod& m : g_mods)
		{
			if (!m.pendingLive.empty()) { LookRefresh(m); ApplyLive(m); }
			// the reload once the controls are let go and the last change has rested: one call for a run of changes
			if (m.reloadPending && !ImGui::IsAnyItemActive() && t - m.pendingSince >= 0.4) { RunReload(m, "after a save"); }
		}
	}

	void SyncIfRegistryChanged()
	{
		static std::uint64_t s_seen = 0;   // render thread only
		const std::uint64_t now = registry::Generation();
		if (now == s_seen) { return; }
		s_seen = now;   // read BEFORE Sync, so a registration arriving during it is seen next frame (Sync's own bump costs one idle re-check)
		Sync();
	}

	void ApplyVisibility()
	{
		std::scoped_lock l(g_lock);
		ApplyVisibilityLocked();
		std::size_t shown = 0;
		const std::set<std::string> on = OnSet();
		for (const Mod& m : g_mods) { shown += m.registered && PageOn(m, on) ? 1 : 0; }
		logger::info("ini pages: {} of {} entr(ies) shown{}", shown, g_mods.size(), settings::Get().iniMenus ? "" : " (all off)");
	}

	void DrawSettingsList()
	{
		Sync();
		std::scoped_lock l(g_lock);
		const std::set<std::string> onSet = OnSet();
		bool anyShown = false;
		// 2.2.0: alphabetical by the name shown, within each folder, so the one INI wanted is easy to find
		std::vector<Mod*> sorted;
		sorted.reserve(g_mods.size());
		for (Mod& m : g_mods) { sorted.push_back(&m); }
		std::stable_sort(sorted.begin(), sorted.end(), [](const Mod* a, const Mod* b) { return Lower(a->entry) < Lower(b->entry); });
		for (const bool data : { false, true })
		{
			bool heading = false;
			for (Mod* mp : sorted)
			{
				Mod& m = *mp;
				if (m.dataFolder != data || !m.ownPage.empty()) { continue; }
				if (!heading)
				{
					heading = true;
					ImGui::Spacing();
					ImGui::SeparatorText(data ? strings::TR("AMF_IniGroupData", "In Data") : strings::TR("AMF_IniGroupSkse", "In Data/SKSE/Plugins"));
				}
				anyShown = true;
				bool on = onSet.contains(m.id);
				const std::string label = m.entry + "##ini-" + m.id;
				if (widgets::Toggle(label.c_str(), &on))
				{
					settings::Get().iniPagesOn = inicore::WithPage(settings::Get().iniPagesOn, m.id, on);
					logger::info("settings page: INI page {} -> {}", m.id, on ? "on" : "off");
					settings::Save();
					ApplyVisibilityLocked();
				}
				std::size_t keys = 0;
				for (const Section& s : m.sections) { keys += s.settings.size(); }
				ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
				ImGui::Indent();
				// v4: each count in its own singular or plural ("1 tabs", "1 settings" - the owner, 2026-10-12), so the row is
				// built from two counted phrases (strings::TRN) in a row that places them
				char settingsText[96], tabsText[96];
				std::snprintf(settingsText, sizeof(settingsText), strings::TRN("AMF_IniSettings", static_cast<long long>(keys), "%d setting", "%d settings"),
							  static_cast<int>(keys));
				std::snprintf(tabsText, sizeof(tabsText), strings::TRN("AMF_IniTabs", static_cast<long long>(m.sections.size()), "%d tab", "%d tabs"),
							  static_cast<int>(m.sections.size()));
				ImGui::TextWrapped(strings::TR("AMF_IniRowCounted", "%s: %s in %s, from %s"), m.name.c_str(), settingsText, tabsText,
								   FileName(m.mainIni).c_str());
				ImGui::Unindent();
				ImGui::PopStyleColor();
			}
		}
		if (!anyShown)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			ImGui::TextWrapped("%s", strings::TR("AMF_IniNone", "No INI file with settings was found in Data/SKSE/Plugins or Data."));
			ImGui::PopStyleColor();
		}
		bool skippedHeading = false;
		auto skippedLine = [&](const std::string& a_folder, const std::string& a_file, const std::string& a_reason) {
			if (!skippedHeading)
			{
				skippedHeading = true;
				ImGui::Spacing();
				ImGui::SeparatorText(strings::TR("AMF_IniSkipped", "Not shown"));
			}
			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			ImGui::BulletText("%s/%s: %s", a_folder.c_str(), a_file.c_str(), a_reason.c_str());
			ImGui::PopStyleColor();
		};
		char buf[512];
		for (const Mod& m : g_mods)
		{
			if (m.ownPage.empty()) { continue; }
			std::snprintf(buf, sizeof(buf), strings::TR("AMF_IniWhyOwnPage", "%s already has its own page here"), m.ownPage.c_str());
			skippedLine(m.folder, FileName(m.mainIni), buf);
		}
		for (const Skipped& s : g_skipped) { skippedLine(s.folder, s.file, s.reason); }
	}

	std::string ToolJson(const std::string& a_argsJson)
	{
		const nlohmann::json args = nlohmann::json::parse(a_argsJson.empty() ? std::string("{}") : a_argsJson, nullptr, false);
		auto str = [&](const char* a_k) { return args.is_object() && args.contains(a_k) && args[a_k].is_string() ? args[a_k].get<std::string>() : std::string(); };
		const std::string what = str("do").empty() ? "list" : str("do");
		Sync();
		std::scoped_lock l(g_lock);
		if (what == "list")
		{
			const std::set<std::string> on = OnSet();
			std::string mods;
			for (const Mod& m : g_mods)
			{
				std::string secs;
				for (const Section& s : m.sections) { secs += (secs.empty() ? "" : ",") + std::string("{\"name\":") + J(s.name) + ",\"keys\":" + std::to_string(s.settings.size()) + "}"; }
				mods += (mods.empty() ? "" : ",") + std::string("{\"mod\":") + J(m.id) + ",\"base\":" + J(m.base) + ",\"entry\":" + J(m.entry) +
						",\"folder\":" + J(m.folder) + ",\"main\":" + J(m.mainIni) + ",\"custom\":" + J(m.customIni) +
						",\"gameSettings\":" + (m.gameSettings ? "true" : "false") + ",\"registered\":" + (m.registered ? "true" : "false") +
						",\"switch\":" + (on.contains(m.id) ? "true" : "false") + ",\"shown\":" + (m.registered && PageOn(m, on) ? "true" : "false") +
						(m.ownPage.empty() ? std::string() : ",\"ownPage\":" + J(m.ownPage)) +
						",\"writes\":" + std::to_string(m.writes) + ",\"sections\":[" + secs + "]}";
			}
			std::string skipped;
			for (const Skipped& s : g_skipped)
			{
				skipped += (skipped.empty() ? "" : ",") + std::string("{\"folder\":") + J(s.folder) + ",\"file\":" + J(s.file) + ",\"reason\":" + J(s.reason) + "}";
			}
			return std::string("{\"ok\":true,\"op\":\"ini\",\"do\":\"list\",\"shown\":") + (settings::Get().iniMenus ? "true" : "false") +
				   ",\"count\":" + std::to_string(g_mods.size()) + ",\"mods\":[" + mods + "],\"skipped\":[" + skipped + "]}";
		}
		const std::string id = Lower(str("mod"));
		auto mod = std::find_if(g_mods.begin(), g_mods.end(), [&](const Mod& m) { return m.id == id || Lower(m.base) == id || Lower(m.entry) == id; });
		if (mod == g_mods.end()) { return "{\"ok\":false,\"op\":\"ini\",\"error\":\"no such mod (do=list names them)\"}"; }
		if (what == "get")
		{
			std::string out;
			for (const Section& s : mod->sections)
			{
				for (const Setting& st : s.settings)
				{
					out += (out.empty() ? "" : ",") + std::string("{\"section\":") + J(s.name) + ",\"name\":" + J(st.key) + ",\"kind\":\"" + KindName(st.kind) +
						   "\",\"value\":" + J(st.value) + ",\"main\":" + J(st.mainValue) + ",\"custom\":" + (st.custom ? "true" : "false") + "}";
				}
			}
			return "{\"ok\":true,\"op\":\"ini\",\"do\":\"get\",\"mod\":" + J(mod->id) + ",\"settings\":[" + out + "]}";
		}
		if (what == "set")
		{
			const std::string section = str("section"), name = str("name");
			std::string value = str("value");
			if (value.empty() && args.is_object() && args.contains("value") && args["value"].is_number()) { value = args["value"].dump(); }
			if (value.empty() && args.is_object() && args.contains("value") && args["value"].is_boolean()) { value = args["value"].get<bool>() ? "true" : "false"; }
			for (Section& s : mod->sections)
			{
				if (Lower(s.name) != Lower(section)) { continue; }
				for (Setting& st : s.settings)
				{
					if (Lower(st.key) != Lower(name)) { continue; }
					std::string error;
					LookRefresh(*mod);   // 2.2.1: so the save is applied the page's way (RefreshTick, next frame)
					const bool ok = SetValue(*mod, s, st, value, error);
					return std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"op\":\"ini\",\"do\":\"set\",\"section\":" + J(s.name) + ",\"name\":" + J(st.key) +
						   ",\"value\":" + J(st.value) + ",\"file\":" + J(mod->customIni.empty() ? mod->mainIni : mod->customIni) +
						   (ok ? std::string() : ",\"error\":" + J(error)) + "}";
				}
			}
			return "{\"ok\":false,\"op\":\"ini\",\"error\":\"no such section/name (do=get lists them)\"}";
		}
		return "{\"ok\":false,\"op\":\"ini\",\"error\":\"do = list | get | set\"}";
	}
}
