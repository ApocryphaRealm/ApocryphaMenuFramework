#include "IniPages.h"

#include "HelpBar.h"
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
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace inipages
{
	namespace
	{
		// THE TABLE. One row per INI-only mod AMF can show. dll: the plugin that must be loaded for its page to appear.
		struct Known
		{
			const char* id;        // DevBench's name for it
			const char* name;      // the entry is "<name> (INI)"
			const char* mainIni;   // read, never written
			const char* customIni; // read over the main one, key by key, and the only file written
			const wchar_t* dll;
		};
		constexpr Known kKnown[] = {
			{ "SSEDisplayTweaks", "SSE Display Tweaks", "Data/SKSE/Plugins/SSEDisplayTweaks.ini",
			  "Data/SKSE/Plugins/SSEDisplayTweaks_custom.ini", L"SSEDisplayTweaks.dll" },
		};

		enum class Kind { kBool, kInt, kFloat, kChoice, kText };

		struct Setting
		{
			std::string key;
			std::string value;        // effective: the custom file's if it sets the key, else the main file's
			std::string mainValue;
			bool custom = false;      // the custom file sets it
			std::string title;        // the "##" line
			std::string help;         // the "#" lines
			std::vector<std::string> options;
			Kind kind = Kind::kText;
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
			const Known* known = nullptr;
			std::string entry;
			std::vector<Section> sections;
			std::vector<std::string> pages;   // registered page names (the sections)
			bool backupTaken = false;
			int writes = 0;
			std::string lastError;
		};

		std::mutex g_lock;            // guards g_mods (render thread draws, DevBench reads and sets)
		std::vector<Mod> g_mods;

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

		bool IsBool(const std::string& a_v) { const std::string l = Lower(a_v); return l == "true" || l == "false"; }

		bool IsInt(const std::string& a_v)
		{
			if (a_v.empty()) { return false; }
			std::size_t i = (a_v[0] == '-' || a_v[0] == '+') ? 1 : 0;
			if (i >= a_v.size()) { return false; }
			for (; i < a_v.size(); ++i) { if (!std::isdigit(static_cast<unsigned char>(a_v[i]))) { return false; } }
			return true;
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
			if (IsBool(a_s.value)) { a_s.kind = Kind::kBool; return; }
			if (IsInt(a_s.value)) { a_s.kind = Kind::kInt; return; }
			if (const int d = FloatDecimals(a_s.value); d >= 0) { a_s.kind = Kind::kFloat; a_s.decimals = std::clamp(d, 1, 6); return; }
			const std::string l = Lower(a_s.value);
			if (a_s.options.size() >= 2 && std::any_of(a_s.options.begin(), a_s.options.end(), [&](const std::string& o) { return Lower(o) == l; }))
			{
				a_s.kind = Kind::kChoice;
				return;
			}
			a_s.kind = Kind::kText;
			std::snprintf(a_s.text, sizeof(a_s.text), "%s", a_s.value.c_str());
		}

		// section (lower) -> key (lower) -> value, for the custom file
		using Flat = std::vector<std::pair<std::string, std::string>>;   // "section\nkey" -> value

		// a_bom: the file starts with a UTF-8 byte-order mark (Display Tweaks' own INI does - 2.1.9's offline test found
		// [Main] unread behind it); it is stripped here and put back by WriteKey
		bool ReadLines(const std::string& a_path, std::vector<std::string>& a_lines, bool& a_crlf, bool* a_bom = nullptr)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in.is_open()) { return false; }
			std::stringstream ss;
			ss << in.rdbuf();
			std::string all = ss.str();
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

		// An active "key = value" line: not a comment, has '=' and a name before it. Returns false otherwise.
		bool ParseKeyLine(const std::string& a_line, std::string& a_key, std::string& a_value)
		{
			const std::string t = Trim(a_line);
			if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[') { return false; }
			const auto eq = t.find('=');
			if (eq == std::string::npos || eq == 0) { return false; }
			a_key = Trim(t.substr(0, eq));
			a_value = Trim(t.substr(eq + 1));
			// inih (what Display Tweaks reads with) strips an inline " ;comment"
			if (const auto sc = a_value.find(" ;"); sc != std::string::npos) { a_value = Trim(a_value.substr(0, sc)); }
			return !a_key.empty();
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
				if (t.size() >= 2 && t.front() == '[' && t.back() == ']') { section = Lower(Trim(t.substr(1, t.size() - 2))); continue; }
				std::string k, v;
				if (ParseKeyLine(l, k, v)) { out.emplace_back(section + "\n" + Lower(k), v); }
			}
			return out;
		}

		// The main INI, in its own order, with the comment block above each key as its title and help.
		std::vector<Section> ReadMain(const std::string& a_path)
		{
			std::vector<Section> out;
			std::vector<std::string> lines;
			bool crlf = false;
			if (!ReadLines(a_path, lines, crlf)) { return out; }
			std::string title;
			std::vector<std::string> help;
			std::vector<std::string> options;
			auto reset = [&]() { title.clear(); help.clear(); options.clear(); };
			for (const std::string& l : lines)
			{
				const std::string t = Trim(l);
				if (t.empty()) { continue; }   // a blank line inside a block keeps the block (the key follows it)
				if (t.front() == '[' && t.back() == ']')
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
				// the help: its lines joined, an empty "#" line kept as a paragraph break
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
			const Flat custom = ReadCustom(a_mod.known->customIni);
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

		// Write one key to the custom file: replace it inside its section, or add it to the section, or add the section.
		// The rest of the file is left exactly as it was. Called with g_lock held.
		bool WriteKey(Mod& a_mod, const std::string& a_section, const std::string& a_key, const std::string& a_value, std::string& a_error)
		{
			namespace fs = std::filesystem;
			const std::string path = a_mod.known->customIni;
			std::vector<std::string> lines;
			bool crlf = true;
			bool bom = false;
			std::error_code ec;
			const bool existed = fs::exists(path, ec) && ReadLines(path, lines, crlf, &bom);
			if (existed && !a_mod.backupTaken)
			{
				// the custom file as it first was, kept once (an older backup is never replaced)
				const std::string backup = path + ".amf-backup";
				if (!fs::exists(backup, ec))
				{
					fs::copy_file(path, backup, ec);
					if (ec) { a_error = "could not keep a backup (" + ec.message() + ")"; logger::error("ini pages: {} - {}; nothing written", path, a_error); return false; }
					logger::info("ini pages: {} kept as {} before the first change", path, backup);
				}
				a_mod.backupTaken = true;
			}
			const std::string wantSection = Lower(a_section), wantKey = Lower(a_key);
			const std::string newLine = a_key + " = " + a_value;
			int secStart = -1, secEnd = static_cast<int>(lines.size()), keyLine = -1;
			for (int i = 0; i < static_cast<int>(lines.size()); ++i)
			{
				const std::string t = Trim(lines[i]);
				if (t.size() >= 2 && t.front() == '[' && t.back() == ']')
				{
					if (secStart >= 0 && secEnd == static_cast<int>(lines.size())) { secEnd = i; }
					if (Lower(Trim(t.substr(1, t.size() - 2))) == wantSection && secStart < 0) { secStart = i; secEnd = static_cast<int>(lines.size()); }
					continue;
				}
				std::string k, v;
				if (secStart >= 0 && i < secEnd && ParseKeyLine(lines[i], k, v) && Lower(k) == wantKey) { keyLine = i; }
			}
			if (keyLine >= 0) { lines[keyLine] = newLine; }
			else if (secStart >= 0)
			{
				int at = secEnd;
				while (at - 1 > secStart && Trim(lines[at - 1]).empty()) { --at; }
				lines.insert(lines.begin() + at, newLine);
			}
			else
			{
				if (!lines.empty() && !Trim(lines.back()).empty()) { lines.push_back(""); }
				lines.push_back("[" + a_section + "]");
				lines.push_back(newLine);
			}
			const std::string tmp = path + ".amf-tmp";
			{
				std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
				if (!out.is_open()) { a_error = "could not write " + tmp; logger::error("ini pages: {}; nothing written", a_error); return false; }
				if (bom) { out << "\xEF\xBB\xBF"; }
				for (const std::string& l : lines) { out << l << (crlf ? "\r\n" : "\n"); }
				out.flush();
				if (!out.good()) { a_error = "the write to " + tmp + " failed"; logger::error("ini pages: {}", a_error); return false; }
			}
			fs::rename(tmp, path, ec);
			if (ec)
			{
				a_error = "could not put the new file in place (" + ec.message() + ")";
				logger::error("ini pages: {} - {}; {} left as it was", path, a_error, path);
				fs::remove(tmp, ec);
				return false;
			}
			++a_mod.writes;
			logger::info("ini pages: [{}] {} = {} written to {}{}", a_section, a_key, a_value, path, existed ? "" : " (a new file)");
			return true;
		}

		// Set one value as the page does: checked against its kind, written, the model updated. Called with g_lock held.
		bool SetValue(Mod& a_mod, Section& a_sec, Setting& a_s, std::string a_value, std::string& a_error)
		{
			a_value = Trim(a_value);
			switch (a_s.kind)
			{
			case Kind::kBool:
				if (!IsBool(a_value)) { a_error = a_s.key + " takes true or false"; return false; }
				a_value = Lower(a_value);
				break;
			case Kind::kInt:
				if (!IsInt(a_value)) { a_error = a_s.key + " takes a whole number"; return false; }
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
				if (a_value.find_first_of("\r\n") != std::string::npos) { a_error = "one line only"; return false; }
				break;
			}
			if (!WriteKey(a_mod, a_sec.name, a_s.key, a_value, a_error))
			{
				a_mod.lastError = a_error;
				return false;
			}
			a_s.value = a_value;
			a_s.custom = true;
			if (a_s.kind == Kind::kText) { std::snprintf(a_s.text, sizeof(a_s.text), "%s", a_value.c_str()); }
			a_mod.lastError.clear();
			return true;
		}

		std::string FormatFloat(double a_v, int a_decimals)
		{
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%.*f", a_decimals, a_v);
			return buf;
		}

		void DrawPage(std::size_t a_mod, const std::string& a_section)
		{
			std::scoped_lock l(g_lock);
			if (a_mod >= g_mods.size()) { return; }
			Mod& mod = g_mods[a_mod];
			if (!mod.known) { return; }
			auto sec = std::find_if(mod.sections.begin(), mod.sections.end(), [&](const Section& s) { return s.name == a_section; });
			if (sec == mod.sections.end()) { ImGui::TextUnformatted("?"); return; }
			helpbar::Want("ini|" + std::string(mod.known->id) + "|" + a_section);

			const std::string customName = std::filesystem::path(mod.known->customIni).filename().string();
			const std::string mainName = std::filesystem::path(mod.known->mainIni).filename().string();
			ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
			ImGui::TextWrapped(strings::TR("AMF_IniRestart", "%s reads these when the game starts: a change takes effect the next time you start the game."),
							   mod.known->name);
			ImGui::TextWrapped(strings::TR("AMF_IniWrites", "Changes are written to %s; %s itself is never changed."), customName.c_str(), mainName.c_str());
			if (!mod.lastError.empty())
			{
				ImGui::TextWrapped(strings::TR("AMF_IniError", "Not saved: %s"), mod.lastError.c_str());
			}
			ImGui::PopStyleColor();
			ImGui::Spacing();

			const char* customMark = strings::TR("AMF_IniCustomMark", "(changed)");
			for (Setting& s : sec->settings)
			{
				ImGui::PushID(s.key.c_str());
				const std::string label = s.key;
				const std::string help = s.title.empty() ? s.help : (s.help.empty() ? s.title : s.title + "\n" + s.help);
				std::string error;
				bool wrote = false;
				switch (s.kind)
				{
				case Kind::kBool:
				{
					bool b = Lower(s.value) == "true";
					if (widgets::Toggle(label.c_str(), &b)) { wrote = SetValue(mod, *sec, s, b ? "true" : "false", error); }
					break;
				}
				case Kind::kInt:
				{
					int v = std::atoi(s.value.c_str());
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
					if (ImGui::InputInt(label.c_str(), &v, 1, 10)) { wrote = SetValue(mod, *sec, s, std::to_string(v), error); }
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
						wrote = SetValue(mod, *sec, s, FormatFloat(v, s.decimals), error);
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
							if (ImGui::Selectable(o.c_str(), selected) && !selected) { wrote = SetValue(mod, *sec, s, o, error); }
							if (selected) { ImGui::SetItemDefaultFocus(); }
						}
						ImGui::EndCombo();
					}
					break;
				}
				case Kind::kText:
				{
					ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0f);
					ImGui::InputText(label.c_str(), s.text, sizeof(s.text));
					if (ImGui::IsItemDeactivatedAfterEdit() && s.value != s.text) { wrote = SetValue(mod, *sec, s, s.text, error); }
					break;
				}
				}
				if (!help.empty() && (ImGui::IsItemHovered() || ImGui::IsItemFocused())) { helpbar::OfferForLastItem(help); }
				if (s.custom)
				{
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
					ImGui::TextUnformatted(customMark);
					ImGui::PopStyleColor();
				}
				if (!error.empty()) { logger::warn("ini pages: {} [{}] {} not set - {}", mod.known->id, sec->name, s.key, error); }
				(void)wrote;
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
	}

	void Load()
	{
		std::scoped_lock l(g_lock);
		if (!g_mods.empty()) { return; }   // once per session
		for (const Known& k : kKnown)
		{
			if (!GetModuleHandleW(k.dll))
			{
				logger::info("ini pages: {} is not loaded - no page", k.name);
				continue;
			}
			Mod mod;
			mod.known = &k;
			mod.entry = std::string(k.name) + " (INI)";
			mod.sections = ReadMain(k.mainIni);
			if (mod.sections.empty())
			{
				logger::warn("ini pages: {} is loaded but {} could not be read (or sets nothing) - no page", k.name, k.mainIni);
				continue;
			}
			g_mods.push_back(std::move(mod));
			Mod& m = g_mods.back();
			ApplyCustom(m);
			std::size_t keys = 0, custom = 0;
			for (const Section& s : m.sections)
			{
				keys += s.settings.size();
				for (const Setting& st : s.settings) { custom += st.custom ? 1 : 0; }
			}
			const std::size_t index = g_mods.size() - 1;
			for (const Section& s : m.sections)
			{
				const std::string page = s.name;
				if (registry::RegisterFn(m.entry.c_str(), page.c_str(), [index, page]() { DrawPage(index, page); }, true))
				{
					m.pages.push_back(page);
				}
			}
			logger::info("ini pages: {} - {} key(s) in {} section(s) from {}, {} set by {}", m.entry, keys, m.sections.size(), k.mainIni, custom, k.customIni);
		}
		const bool show = settings::Get().iniMenus;
		for (const Mod& m : g_mods)
		{
			for (const std::string& p : m.pages) { registry::SetPageVisible(m.entry.c_str(), p.c_str(), show); }
		}
	}

	void ApplyVisibility()
	{
		std::scoped_lock l(g_lock);
		const bool show = settings::Get().iniMenus;
		for (const Mod& m : g_mods)
		{
			for (const std::string& p : m.pages) { registry::SetPageVisible(m.entry.c_str(), p.c_str(), show); }
		}
		logger::info("ini pages: {} entr(ies) {}", g_mods.size(), show ? "shown" : "hidden");
	}

	void DrawSettingsList()
	{
		std::scoped_lock l(g_lock);
		ImGui::PushStyleColor(ImGuiCol_Text, theme::HelpTextColor());
		if (g_mods.empty())
		{
			ImGui::TextWrapped("%s", strings::TR("AMF_IniNone", "No INI-only mod this menu can show is installed (so far: SSE Display Tweaks)."));
		}
		for (const Mod& m : g_mods)
		{
			std::size_t keys = 0;
			for (const Section& s : m.sections) { keys += s.settings.size(); }
			ImGui::BulletText(strings::TR("AMF_IniRow", "%s: %d settings in %d tabs, from %s"), m.entry.c_str(), static_cast<int>(keys),
							  static_cast<int>(m.sections.size()), std::filesystem::path(m.known->mainIni).filename().string().c_str());
		}
		ImGui::PopStyleColor();
	}

	std::string ToolJson(const std::string& a_argsJson)
	{
		const nlohmann::json args = nlohmann::json::parse(a_argsJson.empty() ? std::string("{}") : a_argsJson, nullptr, false);
		auto str = [&](const char* a_k) { return args.is_object() && args.contains(a_k) && args[a_k].is_string() ? args[a_k].get<std::string>() : std::string(); };
		const std::string what = str("do").empty() ? "list" : str("do");
		std::scoped_lock l(g_lock);
		if (what == "list")
		{
			std::string mods;
			for (const Mod& m : g_mods)
			{
				std::string secs;
				for (const Section& s : m.sections) { secs += (secs.empty() ? "" : ",") + std::string("{\"name\":") + J(s.name) + ",\"keys\":" + std::to_string(s.settings.size()) + "}"; }
				mods += (mods.empty() ? "" : ",") + std::string("{\"mod\":") + J(m.known->id) + ",\"entry\":" + J(m.entry) + ",\"main\":" + J(m.known->mainIni) +
						",\"custom\":" + J(m.known->customIni) + ",\"writes\":" + std::to_string(m.writes) + ",\"sections\":[" + secs + "]}";
			}
			return std::string("{\"ok\":true,\"op\":\"ini\",\"do\":\"list\",\"shown\":") + (settings::Get().iniMenus ? "true" : "false") + ",\"mods\":[" + mods + "]}";
		}
		const std::string id = str("mod");
		auto mod = std::find_if(g_mods.begin(), g_mods.end(), [&](const Mod& m) { return Lower(m.known->id) == Lower(id); });
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
			return "{\"ok\":true,\"op\":\"ini\",\"do\":\"get\",\"mod\":" + J(mod->known->id) + ",\"settings\":[" + out + "]}";
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
					const bool ok = SetValue(*mod, s, st, value, error);
					return std::string("{\"ok\":") + (ok ? "true" : "false") + ",\"op\":\"ini\",\"do\":\"set\",\"section\":" + J(s.name) + ",\"name\":" + J(st.key) +
						   ",\"value\":" + J(st.value) + ",\"file\":" + J(mod->known->customIni) + (ok ? std::string() : ",\"error\":" + J(error)) + "}";
				}
			}
			return "{\"ok\":false,\"op\":\"ini\",\"error\":\"no such section/name (do=get lists them)\"}";
		}
		return "{\"ok\":false,\"op\":\"ini\",\"error\":\"do = list | get | set\"}";
	}
}
