#include "McmLoader.h"

#undef GetObject  // windows.h maps it to GetObjectW; CommonLib's Variable::GetObject is meant

#include "Keyboard.h"
#include "Input.h"
#include "RememberedSettings.h"
#include "McmScripts.h"
#include "McmShared.h"
#include "McmStyle.h"
#include "PreciseSlider.h"
#include "Registry.h"
#include "Settings.h"
#include "Strings.h"
#include "Theme.h"
#include "utils/Logger.h"
#include "utils/ToggleSwitch.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <thread>
#include <unordered_map>

// See McmLoader.h for what this is and the plan it follows. Part of the MCM loader.

namespace mcmloader
{
	namespace
	{
		using json = nlohmann::json;
		namespace fs = std::filesystem;

		enum class Kind { kEmpty, kHeader, kText, kToggle, kHiddenToggle, kSlider, kStepper, kMenu, kEnum, kColor, kKeymap, kInput, kUnknown };
		// kBool..kString: MCM Helper ModSetting*. kGlobal: a TESGlobal (sourceForm). kProp*: a property on a script -
		// the mod's own config script unless sourceForm/scriptName name another (MCM Helper's ValueOptionsHandler
		// defaults). kPhase2 now means only what is still not drawable: an unknown sourceType.
		enum class Source { kNone, kBool, kInt, kFloat, kString, kGlobal, kPropBool, kPropInt, kPropFloat, kPropString, kPhase2 };

		// A control's action (MCM Helper Config/Action.cpp): CallFunction on a script (the config script by default)
		// or CallGlobalFunction, with params "{value}" (the control's new value), "{i}5", "{f}1.5", "{b}1", "{s}text",
		// or a bare string. Run after the value is set and OnSettingChange is sent, as MCM Helper's menu runs it.
		struct Action
		{
			bool global = false;          // CallGlobalFunction
			std::string form;             // "Plugin.esp|800" - CallFunction on another form's script
			std::string scriptName;       // that script (CallFunction) / the script holding the global (CallGlobalFunction)
			std::string function;
			std::vector<std::string> params;
		};
		enum class Behavior { kDisable, kHide, kSkip };

		// MCM Helper's groupCondition: a number (that group), an array (any of them), or one of OR / AND / ONLY / NOT
		// over numbers and nested objects - evaluated the way its GroupConditionTree::GetIsActive does.
		struct Cond
		{
			enum class Op { kOr, kAnd, kOnly, kNot } op = Op::kOr;
			std::vector<int> groups;
			std::vector<Cond> subtrees;
		};

		struct Control
		{
			Kind kind = Kind::kUnknown;
			std::string typeName;      // as written in config.json, for logs and the DevBench tool
			std::string id;            // "<key>:<section>" - MCM Helper's setting name
			std::string text;          // raw ($keys translated at draw time, so a late translator still wins)
			std::string help;
			Source source = Source::kNone;
			std::string sourceTypeName;
			float min = 0.0f;
			float max = 1.0f;
			float step = 1.0f;
			std::string format;        // printf format derived from MCM's "{N}" formatString
			std::vector<std::string> options;
			std::string staticValue;   // text control's "value"
			int groupControl = 0;
			std::optional<Cond> cond;
			Behavior behavior = Behavior::kDisable;
			bool hasAction = false;
			std::optional<Action> action;  // empty when the action type is one we do not run
			std::string key;               // the values-map key: id when there is one, else "#<page>.<index>"
			std::string sourceForm;        // GlobalValue / PropertyValue*
			std::string scriptName;
			std::string propertyName;
		};

		struct Page
		{
			std::string name;
			std::vector<Control> controls;
			bool customContent = false;
		};

		struct Mod
		{
			std::string modName;       // plugin stem = MCM\Config folder name = what MCM Helper keys on
			std::string displayName;   // raw, may be a $key
			std::string entryName;     // the AMF entry it registered under
			std::vector<Page> pages;
			std::map<std::string, std::string> defaults;   // id -> value (settings.ini)
			std::map<std::string, std::string> values;     // id -> value (defaults with the user INI over them)
			std::unordered_map<std::string, std::string> translations;  // $KEY -> text, from the mod's own file
			std::string translationsLanguage;  // the TextLanguage() they were read in; another means read them again
			int controlCount = 0;
			int phase2Count = 0;
			// Main-thread only (SKSE tasks): the config script, resolved lazily after a game is loaded.
			RE::BSTSmartPointer<RE::BSScript::Object> script;
			bool scriptTried = false;
			RE::FormID questId = 0;  // the quest the config script is on (a PropertyValue/action scriptName with no form)
			std::vector<std::string> pageNames;  // as registered, for hiding the entry
			bool hideable = false;   // everything on it is drawable here, so SkyUI's copy may be hidden
			bool hiddenInSkyUI = false;
			std::string scriptState = "not looked up yet";
		};

		std::mutex g_mutex;                      // guards every Mod's values/script fields read off the main thread
		// Filled ONCE: Load() reads and registers into its own list and publishes it here under g_mutex in one move, so the
		// vector never grows while another thread walks it (Main Agent's review, 2026-10-05: Load() from the settings page
		// used to push_back here while a SkyUI-list pass walked it on the main thread). Never shrinks, so indices captured
		// by pages stay valid. Code that walks it off the loading thread takes ModsView().
		std::vector<std::unique_ptr<Mod>> g_mods;

		// What the walkers need of each MCM Helper mod, copied under g_mutex.
		struct ModView
		{
			std::size_t index;
			std::string modName;
			std::string entryName;
			std::vector<std::string> pageNames;
			bool hideable;
		};
		std::vector<ModView> ModsView()
		{
			std::scoped_lock lock(g_mutex);
			std::vector<ModView> view;
			view.reserve(g_mods.size());
			for (std::size_t m = 0; m < g_mods.size(); ++m)
			{
				const Mod& mod = *g_mods[m];
				view.push_back({ m, mod.modName, mod.entryName, mod.pageNames, mod.hideable });
			}
			return view;
		}
		std::vector<std::pair<std::string, std::string>> g_skipped;  // mod, reason
		std::atomic<bool> g_loaded{ false };

		// ---------------------------------------------------------------------------------------- small helpers

		std::string Lower(std::string a_s)
		{
			std::transform(a_s.begin(), a_s.end(), a_s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_s;
		}

		std::string Trim(const std::string& a_s)
		{
			const auto b = a_s.find_first_not_of(" \t\r\n");
			if (b == std::string::npos) { return {}; }
			const auto e = a_s.find_last_not_of(" \t\r\n");
			return a_s.substr(b, e - b + 1);
		}

		std::wstring Widen(const std::string& a_s)
		{
			if (a_s.empty()) { return {}; }
			const int n = MultiByteToWideChar(CP_UTF8, 0, a_s.data(), static_cast<int>(a_s.size()), nullptr, 0);
			std::wstring out(static_cast<std::size_t>(n), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, a_s.data(), static_cast<int>(a_s.size()), out.data(), n);
			return out;
		}

		std::string Narrow(std::wstring_view a_w)
		{
			if (a_w.empty()) { return {}; }
			const int n = WideCharToMultiByte(CP_UTF8, 0, a_w.data(), static_cast<int>(a_w.size()), nullptr, 0, nullptr, nullptr);
			std::string out(static_cast<std::size_t>(n), '\0');
			WideCharToMultiByte(CP_UTF8, 0, a_w.data(), static_cast<int>(a_w.size()), out.data(), n, nullptr, nullptr);
			return out;
		}

		// SkyUI text may carry HTML (<font color='#..'>); ImGui draws it literally, so tags are dropped.
		std::string StripTags(const std::string& a_s)
		{
			std::string out;
			out.reserve(a_s.size());
			bool inTag = false;
			for (const char c : a_s)
			{
				if (c == '<') { inTag = true; continue; }
				if (c == '>' && inTag) { inTag = false; continue; }
				if (!inTag) { out += c; }
			}
			return out;
		}

		// See McmShared.h (2.1.5). Atlas Map Markers' headings are "<font color='#FF9900'>$ATLAS_GlobalMarkerSettings</font>":
		// the translators now take the key out of the tags first, so this is reached only for a key no file carries.
		std::string ReadableKey(const std::string& a_key)
		{
			std::string s = Trim(a_key);
			if (!s.empty() && s[0] == '$') { s.erase(0, 1); }
			if (const auto us = s.find('_'); us != std::string::npos && us > 0 && us + 1 < s.size())
			{
				const std::string head = s.substr(0, us);
				const bool prefix = head.size() <= 8 && std::all_of(head.begin(), head.end(),
					[](unsigned char c) { return std::isupper(c) || std::isdigit(c); });
				if (prefix) { s.erase(0, us + 1); }   // "ATLAS_" / "AC_": the mod's own namespace, not words
			}
			std::string out;
			for (std::size_t i = 0; i < s.size(); ++i)
			{
				const unsigned char c = static_cast<unsigned char>(s[i]);
				if (c == '_') { out += ' '; continue; }
				if (c == '&') { out += " & "; continue; }
				if (std::isupper(c) && i > 0)
				{
					const unsigned char p = static_cast<unsigned char>(s[i - 1]);
					const bool nextLower = i + 1 < s.size() && std::islower(static_cast<unsigned char>(s[i + 1]));
					if (std::islower(p) || std::isdigit(p) || (std::isupper(p) && nextLower)) { out += ' '; }
				}
				out += static_cast<char>(c);
			}
			std::string tidy;   // one space between words
			for (const char c : out)
			{
				if (c == ' ' && (tidy.empty() || tidy.back() == ' ')) { continue; }
				tidy += c;
			}
			while (!tidy.empty() && tidy.back() == ' ') { tidy.pop_back(); }
			return tidy;
		}

		std::string Unescape(const std::string& a_s)
		{
			std::string out;
			out.reserve(a_s.size());
			for (std::size_t i = 0; i < a_s.size(); ++i)
			{
				if (a_s[i] == '\\' && i + 1 < a_s.size() && a_s[i + 1] == 'n') { out += '\n'; ++i; continue; }
				out += a_s[i];
			}
			return out;
		}

		// Any non-zero number is on: a hiddenToggle can drive its group from an INT setting (TrueHUD's
		// uInfoBarDisplayDamageCounter: 0 off, 1 and 2 two ways of on), so "1 only" would grey out live rows.
		bool ParseBool(const std::string& a_v)
		{
			const std::string v = Lower(Trim(a_v));
			if (v == "true") { return true; }
			if (v.empty() || v == "false") { return false; }
			try { return std::stod(v) != 0.0; }
			catch (...) { return false; }
		}

		long long ParseInt(const std::string& a_v)
		{
			const std::string v = Trim(a_v);
			try
			{
				if (v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) { return std::stoll(v.substr(2), nullptr, 16); }
				return v.empty() ? 0 : static_cast<long long>(std::stod(v));
			}
			catch (...) { return 0; }
		}

		float ParseFloat(const std::string& a_v)
		{
			try { return Trim(a_v).empty() ? 0.0f : std::stof(Trim(a_v)); }
			catch (...) { return 0.0f; }
		}

		std::string FormatFloat(float a_v)
		{
			std::ostringstream s;
			s << a_v;
			return s.str();
		}

		// --------------------------------------------------------------------------------------------- the files

		// Minimal INI reader for MCM Helper's files: [Section] and key=value, ';' / '#' full-line comments. Returns
		// "<key>:<section>" -> value, the shape MCM Helper's SettingStore keys on.
		std::map<std::string, std::string> ReadIni(const fs::path& a_path)
		{
			std::map<std::string, std::string> out;
			std::ifstream in(a_path, std::ios::binary);
			if (!in) { return out; }
			std::string line;
			std::string section;
			bool first = true;
			while (std::getline(in, line))
			{
				if (first && line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) { line = line.substr(3); }  // UTF-8 BOM
				first = false;
				line = Trim(line);
				if (line.empty() || line[0] == ';' || line[0] == '#') { continue; }
				if (line.front() == '[' && line.back() == ']') { section = Trim(line.substr(1, line.size() - 2)); continue; }
				const auto eq = line.find('=');
				if (eq == std::string::npos || section.empty()) { continue; }
				out[Trim(line.substr(0, eq)) + ":" + section] = Trim(line.substr(eq + 1));
			}
			return out;
		}

		// $keys come from the mod's own translation file, Interface\Translations\<plugin>_<language>.txt - the file the
		// game itself loads for that plugin (UTF-16LE, "$KEY<TAB>text" lines) - read through the game's resource
		// streams so a BSA-packed file is found too, once per mod at load (main thread), English as the fallback.
		// Rule 66: another author's text stays in that author's languages.
		//
		// NOT the game's in-memory table (BSScaleformTranslator::translator.translationMap): on 1.5.97 a find() with a
		// BSFixedStringW missed every key, and iterating the map read 0xFFFFFFFFFFFFFFFF and crashed the game
		// (2026-10-04, crash-2026-10-04-08-59-07.log, McmLoader.cpp:241) - CommonLib's layout of that map does not
		// match this runtime. Files are the stable contract; the game's struct is not.
		using Table = std::unordered_map<std::string, std::string>;
		const Table* g_table = nullptr;  // render thread: the table of the mod being drawn

		std::wstring ReadUtf16Resource(const std::string& a_path)
		{
			std::wstring out;
			RE::BSResourceNiBinaryStream stream(a_path);
			if (!stream.good()) { return out; }
			wchar_t ch = 0;
			while (out.size() < (1u << 22) && stream.read(&ch, 1)) { out.push_back(ch); }  // 4M chars: far past any real file
			if (!out.empty() && out.front() == 0xFEFF) { out.erase(0, 1); }
			return out;
		}

		std::string TextLanguage()
		{
			std::string language = Lower(strings::Language());
			if (language.empty()) { language = Lower(strings::GameLanguageSetting()); }
			return language.empty() ? std::string("english") : language;
		}

		// The language AMF shows first (2.1.1 - it used to read only the game's), then the game's, then English: a mod
		// that ships no file for the picked language keeps the text it had. a_namesOnly: the game's, then English - for the
		// names a menu registers under, which must stay the same whatever language AMF shows.
		Table LoadTranslations(const std::string& a_modName, bool a_namesOnly = false)
		{
			Table table;
			const std::string game = Lower(strings::GameLanguageSetting());
			std::vector<std::string> order{ a_namesOnly ? (game.empty() ? std::string("english") : game) : TextLanguage() };
			if (!game.empty() && game != order.front()) { order.push_back(game); }
			if (order.back() != "english" && order.front() != "english") { order.push_back("english"); }
			for (const std::string& lang : order)
			{
				const std::string path = "Interface\\Translations\\" + a_modName + "_" + lang + ".txt";
				const std::wstring text = ReadUtf16Resource(path);
				if (text.empty()) { continue; }
				std::size_t start = 0;
				while (start < text.size())
				{
					std::size_t end = text.find(L'\n', start);
					if (end == std::wstring::npos) { end = text.size(); }
					std::wstring line = text.substr(start, end - start);
					start = end + 1;
					if (!line.empty() && line.back() == L'\r') { line.pop_back(); }
					const auto tab = line.find(L'\t');
					if (line.empty() || line[0] != L'$' || tab == std::wstring::npos) { continue; }
					table.emplace(Trim(Narrow(line.substr(0, tab))), Narrow(line.substr(tab + 1)));  // first file wins
				}
				logger::debug("MCM loader: {} - {} translation(s) from {}", a_modName, table.size(), path);
			}
			return table;
		}

		std::string Translate(const std::string& a_raw)
		{
			if (a_raw.empty() || a_raw[0] != '$')
			{
				// 2.1.5: a key inside tags ("<font ...>$KEY</font>") is still a key - translate what the tags hold.
				const std::string inner = Trim(StripTags(a_raw));
				if (!inner.empty() && inner[0] == '$' && inner != a_raw) { return Translate(inner); }
				return Unescape(StripTags(a_raw));
			}
			if (g_table)
			{
				if (const auto it = g_table->find(a_raw); it != g_table->end()) { return Unescape(StripTags(it->second)); }
			}
			return ReadableKey(a_raw);
		}

		// MCM's formatString uses "{N}" for the value with N decimals ("{0}", "{1}%", "{2} sec"); printf wants "%.Nf".
		std::string ToPrintf(const std::string& a_mcm, float a_step)
		{
			if (a_mcm.empty())
			{
				int decimals = 0;
				for (float s = a_step; decimals < 4 && std::fabs(s - std::round(s)) > 1e-4f; s *= 10.0f) { ++decimals; }
				return "%." + std::to_string(decimals) + "f";
			}
			std::string out;
			for (std::size_t i = 0; i < a_mcm.size(); ++i)
			{
				const char c = a_mcm[i];
				if (c == '%') { out += "%%"; continue; }
				if (c == '{')
				{
					const auto close = a_mcm.find('}', i);
					if (close != std::string::npos)
					{
						const std::string n = a_mcm.substr(i + 1, close - i - 1);
						const bool digits = !n.empty() && std::all_of(n.begin(), n.end(), [](unsigned char d) { return std::isdigit(d); });
						out += "%." + (digits ? n : std::string("0")) + "f";
						i = close;
						continue;
					}
				}
				out += c;
			}
			return out;
		}

		Kind KindOf(const std::string& a_type)
		{
			static const std::unordered_map<std::string, Kind> kinds{
				{ "empty", Kind::kEmpty }, { "header", Kind::kHeader }, { "text", Kind::kText }, { "toggle", Kind::kToggle },
				{ "hiddenToggle", Kind::kHiddenToggle }, { "slider", Kind::kSlider }, { "stepper", Kind::kStepper },
				{ "menu", Kind::kMenu }, { "enum", Kind::kEnum }, { "color", Kind::kColor }, { "keymap", Kind::kKeymap },
				{ "input", Kind::kInput }
			};
			const auto it = kinds.find(a_type);
			return it == kinds.end() ? Kind::kUnknown : it->second;
		}

		Source SourceOf(const std::string& a_sourceType)
		{
			if (a_sourceType.empty()) { return Source::kNone; }
			if (a_sourceType == "ModSettingBool") { return Source::kBool; }
			if (a_sourceType == "ModSettingInt") { return Source::kInt; }
			if (a_sourceType == "ModSettingFloat") { return Source::kFloat; }
			if (a_sourceType == "ModSettingString") { return Source::kString; }
			if (a_sourceType == "GlobalValue") { return Source::kGlobal; }
			if (a_sourceType == "PropertyValueBool") { return Source::kPropBool; }
			if (a_sourceType == "PropertyValueInt") { return Source::kPropInt; }
			if (a_sourceType == "PropertyValueFloat") { return Source::kPropFloat; }
			if (a_sourceType == "PropertyValueString") { return Source::kPropString; }
			return Source::kPhase2;
		}

		std::optional<Cond> ParseCond(const json& a_j)
		{
			Cond c;
			if (a_j.is_number_integer()) { c.groups.push_back(a_j.get<int>()); return c; }
			if (a_j.is_array())
			{
				for (const auto& v : a_j)
				{
					if (v.is_number_integer()) { c.groups.push_back(v.get<int>()); }
					else if (auto sub = ParseCond(v)) { c.subtrees.push_back(*sub); }
				}
				return c;
			}
			if (a_j.is_object() && !a_j.empty())
			{
				const auto first = a_j.begin();
				const std::string key = first.key();
				const json& val = first.value();
				if (key == "AND") { c.op = Cond::Op::kAnd; }
				else if (key == "ONLY") { c.op = Cond::Op::kOnly; }
				else if (key == "NOT") { c.op = Cond::Op::kNot; }
				else { c.op = Cond::Op::kOr; }
				if (auto inner = ParseCond(val))
				{
					c.groups = inner->groups;
					c.subtrees = inner->subtrees;
				}
				return c;
			}
			return std::nullopt;
		}

		bool Eval(const Cond& a_c, const std::map<int, bool>& a_groups)
		{
			const auto active = [&](int g) { const auto it = a_groups.find(g); return it != a_groups.end() && it->second; };
			switch (a_c.op)
			{
			case Cond::Op::kOr:
				for (int g : a_c.groups) { if (active(g)) { return true; } }
				for (const auto& s : a_c.subtrees) { if (Eval(s, a_groups)) { return true; } }
				return false;
			case Cond::Op::kAnd:
				for (int g : a_c.groups) { if (!active(g)) { return false; } }
				for (const auto& s : a_c.subtrees) { if (!Eval(s, a_groups)) { return false; } }
				return true;
			case Cond::Op::kOnly:
			{
				if (!a_c.subtrees.empty()) { return false; }
				std::set<int> listed(a_c.groups.begin(), a_c.groups.end());
				for (int g : a_c.groups) { if (!active(g)) { return false; } }
				for (const auto& [g, on] : a_groups) { if (on && !listed.contains(g)) { return false; } }
				return true;
			}
			case Cond::Op::kNot:
				for (int g : a_c.groups) { if (active(g)) { return false; } }
				for (const auto& s : a_c.subtrees) { if (Eval(s, a_groups)) { return false; } }
				return true;
			}
			return true;
		}

		std::string JsonStr(const json& a_j, const char* a_key)
		{
			const auto it = a_j.find(a_key);
			return (it != a_j.end() && it->is_string()) ? it->get<std::string>() : std::string{};
		}

		Control ParseControl(const json& a_c)
		{
			Control c;
			c.typeName = JsonStr(a_c, "type");
			c.kind = KindOf(c.typeName);
			c.id = JsonStr(a_c, "id");
			c.text = JsonStr(a_c, "text");
			c.help = JsonStr(a_c, "help");
			if (const auto it = a_c.find("groupControl"); it != a_c.end() && it->is_number_integer()) { c.groupControl = it->get<int>(); }
			if (const auto it = a_c.find("groupCondition"); it != a_c.end()) { c.cond = ParseCond(*it); }
			const std::string behavior = JsonStr(a_c, "groupBehavior");
			c.behavior = behavior == "hide" ? Behavior::kHide : behavior == "skip" ? Behavior::kSkip : Behavior::kDisable;
			c.hasAction = a_c.contains("action");
			if (const auto a = a_c.find("action"); a != a_c.end() && a->is_object())
			{
				const std::string type = JsonStr(*a, "type");
				if (type == "CallFunction" || type == "CallGlobalFunction")
				{
					Action action;
					action.global = type == "CallGlobalFunction";
					action.form = JsonStr(*a, "form");
					action.scriptName = action.global ? JsonStr(*a, "script") : JsonStr(*a, "scriptName");
					action.function = JsonStr(*a, "function");
					if (const auto p = a->find("params"); p != a->end() && p->is_array())
					{
						for (const auto& v : *p)
						{
							// MCM Helper keeps every param as text and types it by its prefix; a JSON number or bool
							// becomes the matching prefixed text so it is typed the same way.
							if (v.is_string()) { action.params.push_back(v.get<std::string>()); }
							else if (v.is_boolean()) { action.params.push_back(std::string("{b}") + (v.get<bool>() ? "1" : "0")); }
							else if (v.is_number_integer()) { action.params.push_back("{i}" + std::to_string(v.get<long long>())); }
							else if (v.is_number()) { action.params.push_back("{f}" + FormatFloat(v.get<float>())); }
						}
					}
					if (!action.function.empty()) { c.action = std::move(action); }
				}
			}

			if (const auto vo = a_c.find("valueOptions"); vo != a_c.end() && vo->is_object())
			{
				c.sourceTypeName = JsonStr(*vo, "sourceType");
				c.source = SourceOf(c.sourceTypeName);
				c.sourceForm = JsonStr(*vo, "sourceForm");
				c.scriptName = JsonStr(*vo, "scriptName");
				c.propertyName = JsonStr(*vo, "propertyName");
				if (!c.sourceForm.empty() && (c.source == Source::kBool || c.source == Source::kInt || c.source == Source::kFloat || c.source == Source::kString))
				{
					c.source = Source::kPhase2;  // a ModSetting tied to a form is not a thing MCM Helper documents; stay safe
				}
				if (c.source == Source::kGlobal && c.sourceForm.empty()) { c.source = Source::kPhase2; }   // MCM Helper needs the form too
				if ((c.source == Source::kPropBool || c.source == Source::kPropInt || c.source == Source::kPropFloat || c.source == Source::kPropString) &&
					c.propertyName.empty())
				{
					c.source = Source::kPhase2;
				}
				c.min = vo->value("min", 0.0f);
				c.max = vo->value("max", 1.0f);
				c.step = vo->value("step", 1.0f);
				if (c.step <= 0.0f) { c.step = 1.0f; }
				c.format = ToPrintf(JsonStr(*vo, "formatString"), c.step);
				if (const auto o = vo->find("options"); o != vo->end() && o->is_array())
				{
					for (const auto& s : *o) { c.options.push_back(s.is_string() ? s.get<std::string>() : s.dump()); }
				}
				c.staticValue = JsonStr(*vo, "value");
			}
			else if (c.kind == Kind::kSlider)
			{
				c.format = ToPrintf({}, c.step);
			}
			return c;
		}

		// ------------------------------------------------------------------------------- applying a change

		class Then : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit Then(std::function<void()> a_fn) : _fn(std::move(a_fn)) {}
			void operator()(RE::BSScript::Variable) override { if (_fn) { _fn(); } }
			bool CanSave() const override { return false; }
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::function<void()> _fn;
		};

		// Main thread. The quest from the mod's own plugin whose script derives from MCM_ConfigBase - MCM Helper's own
		// match (FindBoundObject on the base name finds derived scripts; ScriptObject::FromForm does the same).
		RE::BSTSmartPointer<RE::BSScript::Object> FindConfigScript(const std::string& a_modName, RE::FormID& a_questId)
		{
			RE::BSTSmartPointer<RE::BSScript::Object> found;
			const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			const auto dataHandler = RE::TESDataHandler::GetSingleton();
			const auto policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!vm || !dataHandler || !policy)
			{
				logger::warn("MCM loader: no VM / data handler / handle policy yet - cannot look up {}'s config script", a_modName);
				return found;
			}
			const std::string want = Lower(a_modName);
			for (const auto quest : dataHandler->GetFormArray<RE::TESQuest>())
			{
				const auto file = quest ? quest->GetFile(0) : nullptr;
				if (!file || Lower(fs::path(std::string(file->GetFilename())).stem().string()) != want) { continue; }
				const auto handle = policy->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
				if (!handle) { continue; }
				RE::BSTSmartPointer<RE::BSScript::Object> object;
				if (vm->FindBoundObject(handle, "MCM_ConfigBase", object) && object)
				{
					logger::debug("MCM loader: {}'s config script is on quest {:08X} ({})", a_modName, quest->GetFormID(),
								  quest->GetFormEditorID() ? quest->GetFormEditorID() : "");
					a_questId = quest->GetFormID();
					return object;
				}
			}
			return found;
		}

		// Main thread: the mod's config script, cached until the next game load.
		RE::BSTSmartPointer<RE::BSScript::Object> EnsureScript(std::size_t a_mod)
		{
			RE::BSTSmartPointer<RE::BSScript::Object> script;
			std::string modName;
			{
				std::scoped_lock lock(g_mutex);
				if (a_mod >= g_mods.size()) { return script; }
				script = g_mods[a_mod]->script;
				modName = g_mods[a_mod]->modName;
			}
			if (script) { return script; }
			RE::FormID questId = 0;
			script = FindConfigScript(modName, questId);
			std::scoped_lock lock(g_mutex);
			g_mods[a_mod]->script = script;
			g_mods[a_mod]->questId = questId;
			g_mods[a_mod]->scriptTried = true;
			g_mods[a_mod]->scriptState = script ? "resolved" : "not found (no game loaded yet, or the mod's quest has not started)";
			return script;
		}

		// Main thread: one SkyUI/MCM Helper event on the mod's config script. OnConfigOpen / OnConfigClose carry no
		// argument; OnSettingChange carries the setting id.
		// a_then (optional): run once the event has FINISHED in Papyrus - or at once when it could not be sent. The
		// DevBench path waits on OnConfigOpen this way: SkyUI's OnConfigOpen reloads properties (UnequipArmor =
		// GetGroupFlag(...)), and run after a write it put the old value back (2026-10-04).
		bool DispatchEvent(std::size_t a_mod, const char* a_event, const std::string* a_arg, std::function<void()> a_then = {})
		{
			const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto script = EnsureScript(a_mod);
			const std::string modName = a_mod < g_mods.size() ? g_mods[a_mod]->modName : std::string("?");
			if (!vm || !script)
			{
				logger::info("MCM loader: {} {} not sent - no config script yet (the mod reads its settings when it next loads them)",
					modName, a_event);
				if (a_then) { a_then(); }
				return false;
			}
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
			if (a_then) { none.reset(new Then(a_then)); }
			std::unique_ptr<RE::BSScript::IFunctionArguments> args{ a_arg ? RE::MakeFunctionArguments(std::string(*a_arg)) : RE::MakeFunctionArguments() };
			const bool sent = vm->DispatchMethodCall(script, a_event, args.get(), none);
			if (!sent && a_then) { a_then(); }
			logger::info("MCM loader: {} {}{}{} {}", modName, a_event, a_arg ? " " : "", a_arg ? *a_arg : "", sent ? "sent" : "NOT sent (dispatch failed)");
			return sent;
		}

		// Any thread: queue one event to the main thread.
		void QueueEvent(std::size_t a_mod, const char* a_event)
		{
			if (const auto tasks = SKSE::GetTaskInterface())
			{
				tasks->AddTask([a_mod, a_event]() { DispatchEvent(a_mod, a_event, nullptr); });
			}
		}

		// ------------------------------------------------------- phase 2: forms, scripts, live values, actions (main thread)

		// "Plugin.esp|800" (MCM Helper's FormUtil::GetFormFromIdentifier: the id is hex, relative to the plugin).
		RE::TESForm* FormFromIdentifier(const std::string& a_identifier)
		{
			const auto bar = a_identifier.find('|');
			if (bar == std::string::npos) { return nullptr; }
			const std::string plugin = Trim(a_identifier.substr(0, bar));
			std::string hex = Trim(a_identifier.substr(bar + 1));
			if (hex.size() > 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) { hex = hex.substr(2); }
			RE::FormID id = 0;
			try { id = static_cast<RE::FormID>(std::stoul(hex, nullptr, 16)); }
			catch (...) { return nullptr; }
			const auto dataHandler = RE::TESDataHandler::GetSingleton();
			return dataHandler ? dataHandler->LookupForm(id, plugin) : nullptr;
		}

		// The script a control's property or action lives on. No form named: the mod's own config script - or, when a
		// scriptName is given, that script on the config quest. A form named: its script called scriptName.
		RE::BSTSmartPointer<RE::BSScript::Object> ScriptFor(std::size_t a_mod, const std::string& a_form, const std::string& a_scriptName)
		{
			RE::BSTSmartPointer<RE::BSScript::Object> object;
			const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			const auto policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!vm || !policy) { return object; }

			RE::TESForm* form = nullptr;
			if (a_form.empty())
			{
				auto config = EnsureScript(a_mod);
				if (a_scriptName.empty() || !config) { return config; }
				std::scoped_lock lock(g_mutex);
				form = RE::TESForm::LookupByID(g_mods[a_mod]->questId);
			}
			else
			{
				form = FormFromIdentifier(a_form);
			}
			if (!form)
			{
				logger::warn("MCM loader: form \"{}\" not found", a_form);
				return object;
			}
			if (a_scriptName.empty())
			{
				// MCM Helper reads the VM's attached-script table here; no real config uses this case (census
				// 2026-10-04), so it is refused rather than read through a struct that may not match this runtime.
				logger::warn("MCM loader: form \"{}\" has no scriptName - not supported", a_form);
				return object;
			}
			const auto handle = policy->GetHandleForObject(form->GetFormType(), form);
			if (handle) { vm->FindBoundObject(handle, a_scriptName.c_str(), object); }
			if (!object) { logger::warn("MCM loader: script {} is not attached to {:08X}", a_scriptName, form->GetFormID()); }
			return object;
		}

		bool IsLive(Source a_source)
		{
			return a_source == Source::kGlobal || a_source == Source::kPropBool || a_source == Source::kPropInt ||
			       a_source == Source::kPropFloat || a_source == Source::kPropString;
		}

		// Reads a GlobalValue / PropertyValue* control as text ("1"/"0", an integer, a float, a string). nullopt: not readable now.
		std::optional<std::string> ReadLive(std::size_t a_mod, const Control& a_c)
		{
			if (a_c.source == Source::kGlobal)
			{
				const auto form = FormFromIdentifier(a_c.sourceForm);
				const auto global = form ? form->As<RE::TESGlobal>() : nullptr;
				if (!global) { return std::nullopt; }
				return a_c.kind == Kind::kToggle || a_c.kind == Kind::kHiddenToggle ? std::string(global->value != 0.0f ? "1" : "0") :
				       a_c.kind == Kind::kSlider ? FormatFloat(global->value) : std::to_string(std::lround(global->value));
			}
			const auto script = ScriptFor(a_mod, a_c.sourceForm, a_c.scriptName);
			const auto variable = script ? script->GetProperty(a_c.propertyName) : nullptr;
			if (!variable) { return std::nullopt; }
			if (variable->IsBool()) { return std::string(variable->GetBool() ? "1" : "0"); }
			if (variable->IsInt()) { return std::to_string(variable->GetSInt()); }
			if (variable->IsFloat()) { return FormatFloat(variable->GetFloat()); }
			if (variable->IsString()) { return std::string(variable->GetString()); }
			return std::nullopt;
		}

		bool WriteLive(std::size_t a_mod, const Control& a_c, const std::string& a_value)
		{
			if (a_c.source == Source::kGlobal)
			{
				const auto form = FormFromIdentifier(a_c.sourceForm);
				const auto global = form ? form->As<RE::TESGlobal>() : nullptr;
				if (!global)
				{
					logger::warn("MCM loader: global {} not found - {} not written", a_c.sourceForm, a_c.key);
					return false;
				}
				global->value = ParseFloat(a_value);
				return true;
			}
			const auto script = ScriptFor(a_mod, a_c.sourceForm, a_c.scriptName);
			const auto variable = script ? script->GetProperty(a_c.propertyName) : nullptr;
			if (!variable)
			{
				logger::warn("MCM loader: property {} not found - {} not written", a_c.propertyName, a_c.key);
				return false;
			}
			switch (a_c.source)
			{
			case Source::kPropBool: variable->SetBool(ParseBool(a_value)); break;
			case Source::kPropInt: variable->SetSInt(static_cast<std::int32_t>(ParseInt(a_value))); break;
			case Source::kPropFloat: variable->SetFloat(ParseFloat(a_value)); break;
			default: variable->SetString(a_value); break;
			}
			return true;
		}

		// An action's argument list (MCM Helper's Function::FunctionArguments::Make) - shared with phase 3.
		using detail::VarArgs;

		void RunAction(std::size_t a_mod, const Control& a_c, const std::string& a_value)
		{
			if (!a_c.action) { return; }
			const Action& action = *a_c.action;
			const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			if (!vm) { return; }

			VarArgs args;
			args.args.resize(static_cast<std::uint32_t>(action.params.size()));
			for (std::uint32_t i = 0; i < action.params.size(); ++i)
			{
				auto& var = args.args[i];
				const std::string& p = action.params[i];
				if (p == "{value}")
				{
					// typed as MCM Helper's control GetValue() is: toggle bool, slider float, menu/input/text string, others int
					switch (a_c.kind)
					{
					case Kind::kToggle:
					case Kind::kHiddenToggle: var.SetBool(ParseBool(a_value)); break;
					case Kind::kSlider: var.SetFloat(ParseFloat(a_value)); break;
					case Kind::kMenu:
					case Kind::kInput:
					case Kind::kText: var.SetString(a_value); break;
					default: var.SetSInt(static_cast<std::int32_t>(ParseInt(a_value))); break;
					}
				}
				else if (p.rfind("{i}", 0) == 0) { var.SetSInt(static_cast<std::int32_t>(ParseInt(p.substr(3)))); }
				else if (p.rfind("{u}", 0) == 0) { var.SetUInt(static_cast<std::uint32_t>(ParseInt(p.substr(3)))); }
				else if (p.rfind("{b}", 0) == 0) { var.SetBool(ParseBool(p.substr(3))); }
				else if (p.rfind("{f}", 0) == 0) { var.SetFloat(ParseFloat(p.substr(3))); }
				else if (p.rfind("{s}", 0) == 0) { var.SetString(p.substr(3)); }
				else { var.SetString(p); }
			}

			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
			bool sent = false;
			if (action.global)
			{
				std::string script = action.scriptName;
				if (script.empty())
				{
					if (const auto config = EnsureScript(a_mod); config && config->GetTypeInfo()) { script = config->GetTypeInfo()->GetName(); }
				}
				if (!script.empty()) { sent = vm->DispatchStaticCall(script, action.function, &args, none); }
				logger::info("MCM loader: action {}.{}({} param(s)) {}", script, action.function, action.params.size(), sent ? "called" : "NOT called");
			}
			else
			{
				auto object = ScriptFor(a_mod, action.form, action.scriptName);
				if (object) { sent = vm->DispatchMethodCall(object, action.function, &args, none); }
				logger::info("MCM loader: action {}({} param(s)) on {} {}", action.function, action.params.size(),
					action.form.empty() ? std::string("the config script") : action.form, sent ? "called" : "NOT called (no script)");
			}
		}

		// Main thread: re-read every GlobalValue / PropertyValue* control of a mod, so the page shows what the mod's own
		// scripts set (several set properties in OnConfigOpen / OnSettingChange that drive hiddenToggle groups).
		void RefreshLive(std::size_t a_mod)
		{
			if (a_mod >= g_mods.size()) { return; }
			std::vector<std::pair<std::string, std::string>> read;
			for (const Page& page : g_mods[a_mod]->pages)
			{
				for (const Control& c : page.controls)
				{
					if (!IsLive(c.source)) { continue; }
					if (auto v = ReadLive(a_mod, c)) { read.emplace_back(c.key, std::move(*v)); }
				}
			}
			std::scoped_lock lock(g_mutex);
			for (auto& [k, v] : read) { g_mods[a_mod]->values[k] = std::move(v); }
		}

		void QueueRefresh(std::size_t a_mod)
		{
			if (const auto tasks = SKSE::GetTaskInterface()) { tasks->AddTask([a_mod]() { RefreshLive(a_mod); }); }
		}

		// Any thread: record the new value now (the page shows it at once), then hand the write to the main thread.
		// The order is MCM Helper's (MCM_ConfigBase.cpp, OnOptionSliderAccept and friends): set the value, send
		// OnSettingChange(id) when the control has an id, run its action with the new value.
		// a_closeAfter: also send OnConfigClose at the end - for a change made with no page open (the DevBench tool), so
		// a mod that applies its settings only when its menu closes (TrueHUD, True Directional Movement, Precision:
		// "Event OnConfigClose() native") sees it, as it would after SkyUI's menu.
		// Set while the remembered MCM settings are written its own values back, so they are not recorded again.
		std::atomic<bool> g_memoryWriting{ false };

		void Apply(std::size_t a_mod, const Control& a_control, const std::string& a_value, bool a_closeAfter = false)
		{
			std::string modName;
			std::string entryName;
			{
				std::scoped_lock lock(g_mutex);
				if (a_mod >= g_mods.size()) { return; }
				if (a_control.source != Source::kNone) { g_mods[a_mod]->values[a_control.key] = a_value; }
				modName = g_mods[a_mod]->modName;
				entryName = g_mods[a_mod]->entryName;
			}
			// Remembered MCM settings: a value kept in the save (a global or a script property) is remembered for the next new
			// game; MCM Helper's own ModSetting values are in its INI already
			if (IsLive(a_control.source) && !g_memoryWriting.load())
			{
				rememberedsettings::Record record;
				record.type = rememberedsettings::Type::kHelper;
				record.id = a_control.key;
				record.value = a_value;
				rememberedsettings::Remember("mcmhelper|" + modName, entryName, record);
			}
			const auto tasks = SKSE::GetTaskInterface();
			if (!tasks)
			{
				logger::error("MCM loader: no SKSE task interface - {}:{} = {} kept in the menu only", modName, a_control.key, a_value);
				return;
			}
			const Control* control = &a_control;  // pages never change after Load, so the pointer outlives the task
			logger::debug("MCM loader: {} {} -> {} (queued)", modName, a_control.key, a_value);

			tasks->AddTask([a_mod, modName, control, a_value, a_closeAfter]() {
				const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
				if (!vm)
				{
					logger::warn("MCM loader: no Papyrus VM - {} {} not written", modName, control->key);
					return;
				}
				// 2. after the value is stored: OnSettingChange, the action, and (no page open) OnConfigClose
				auto after = [a_mod, control, a_value, a_closeAfter]() {
					if (!control->id.empty()) { DispatchEvent(a_mod, "OnSettingChange", &control->id); }
					RunAction(a_mod, *control, a_value);
					if (a_closeAfter) { DispatchEvent(a_mod, "OnConfigClose", nullptr); }
					QueueRefresh(a_mod);
				};

				const Source source = control->source;
				if (source == Source::kNone)  // a button (text control with an action): nothing to store
				{
					after();
					return;
				}
				if (IsLive(source))  // 1. a global or a script property, set where MCM Helper sets it
				{
					if (WriteLive(a_mod, *control, a_value)) { logger::info("MCM loader: {} {} = {} ({})", modName, control->key, a_value, control->sourceTypeName); }
					after();
					return;
				}

				// 1. MCM Helper's own store and user INI (MCM.SetModSetting*)
				auto notify = [modName, control, after]() {
					logger::info("MCM loader: {} {} saved by MCM Helper", modName, control->id);
					after();
				};
				const char* fn = source == Source::kBool ? "SetModSettingBool" : source == Source::kInt ? "SetModSettingInt" :
				                 source == Source::kFloat ? "SetModSettingFloat" : "SetModSettingString";
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> then{ new Then(std::move(notify)) };
				std::unique_ptr<RE::BSScript::IFunctionArguments> args;
				const std::string id = control->id;
				switch (source)
				{
				case Source::kBool: args.reset(RE::MakeFunctionArguments(std::string(modName), std::string(id), ParseBool(a_value))); break;
				case Source::kInt: args.reset(RE::MakeFunctionArguments(std::string(modName), std::string(id), static_cast<std::int32_t>(ParseInt(a_value)))); break;
				case Source::kFloat: args.reset(RE::MakeFunctionArguments(std::string(modName), std::string(id), ParseFloat(a_value))); break;
				default: args.reset(RE::MakeFunctionArguments(std::string(modName), std::string(id), std::string(a_value))); break;
				}
				if (!vm->DispatchStaticCall("MCM"sv, fn, args.get(), then))
				{
					static bool warned = false;
					if (!warned)
					{
						warned = true;
						logger::error("MCM loader: MCM.{} could not be called - is MCM Helper installed? Changes stay in this menu only", fn);
					}
				}
			});
		}

		// -------------------------------------------------- SkyUI's own MCM list: hide the mods managed here (main thread)
		//
		// SkyUI's config manager (SKI_ConfigManager) re-announces itself every 5 s for half a minute after a load and every
		// 30 s after that, and a config script registers on every announcement UNLESS it already holds that manager
		// (SKI_ConfigBase.OnConfigManagerReady: "if (_configManager == newManager) return"). UnregisterMod(config) takes the
		// mod out of the list without clearing that, so one call after the mod has registered keeps it out - no
		// re-registration, and no "MCM: Registered N new menu(s)" notification. RegisterMod(config, ModName) puts it back.
		// Both are SkyUI's own @interface functions. The list lives in the save; `setstage SKI_ConfigManagerInstance 1`
		// (SkyUI's documented reset) brings every menu back without this mod.

		RE::BSTSmartPointer<RE::BSScript::Object> FindSkyUIManager()
		{
			RE::BSTSmartPointer<RE::BSScript::Object> found;
			const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			const auto dataHandler = RE::TESDataHandler::GetSingleton();
			const auto policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
			if (!vm || !dataHandler || !policy) { return found; }
			for (const auto quest : dataHandler->GetFormArray<RE::TESQuest>())
			{
				const auto file = quest ? quest->GetFile(0) : nullptr;
				if (!file || Lower(std::string(file->GetFilename())) != "skyui_se.esp") { continue; }
				const auto handle = policy->GetHandleForObject(RE::TESQuest::FORMTYPE, quest);
				RE::BSTSmartPointer<RE::BSScript::Object> object;
				if (handle && vm->FindBoundObject(handle, "SKI_ConfigManager", object) && object) { return object; }
			}
			return found;
		}

		std::atomic<int> g_hiddenCount{ 0 };

		// The menus AMF itself took out of SkyUI's list, kept across sessions (the list lives in the save). Only these are
		// ever given back: on 2026-10-04 a give-back that sent RegisterMod for EVERY config added Honed Metal, which was not
		// in SkyUI's list at all, and would undo what other mods do to the list (MenuMaid2 hides entries the same way).
		// One key per line: "mcmhelper|<mod>" or "script|<plugin>|<ModName>".
		constexpr const char* kLedgerPath = "Data/SKSE/Plugins/ApocryphaMenuFramework/McmHiddenInSkyUI.txt";
		std::mutex g_ledgerMutex;
		std::set<std::string> g_ledger;
		bool g_ledgerLoaded = false;

		void LoadLedgerLocked()
		{
			if (g_ledgerLoaded) { return; }
			g_ledgerLoaded = true;
			std::ifstream in(kLedgerPath, std::ios::binary);
			std::string line;
			while (std::getline(in, line))
			{
				line = Trim(line);
				if (!line.empty() && line[0] != ';') { g_ledger.insert(line); }
			}
			logger::debug("MCM loader: {} menu(s) in the hidden-by-AMF ledger", g_ledger.size());
		}

		// Which menus come into AMF (xLenax, 2026-10-04: "an option to choose which MCMs I'd like to import instead of importing
		// all of them or None"). One line per menu the player switched: "<ledger key>=1|0"; a menu not listed follows
		// [MCM] bImportNewMenus. Kept beside the ledger, outside the download, so an update never resets it.
		constexpr const char* kImportPath = "Data/SKSE/Plugins/ApocryphaMenuFramework/McmImport.txt";
		std::mutex g_importLock;
		std::map<std::string, bool> g_import;
		bool g_importLoaded = false;

		void LoadImportLocked()
		{
			if (g_importLoaded) { return; }
			g_importLoaded = true;
			std::ifstream in(kImportPath, std::ios::binary);
			std::string line;
			while (std::getline(in, line))
			{
				line = Trim(line);
				const auto eq = line.rfind('=');
				if (line.empty() || line[0] == ';' || eq == std::string::npos) { continue; }
				g_import[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1)) == "1";
			}
			logger::debug("MCM loader: {} menu choice(s) read from {}", g_import.size(), kImportPath);
		}

		void SaveImportLocked()
		{
			std::error_code ec;
			fs::create_directories(fs::path(kImportPath).parent_path(), ec);
			std::ofstream out(kImportPath, std::ios::binary | std::ios::trunc);
			out << "; Which MCM menus Apocrypha Menu Framework shows: 1 = shown here, 0 = left to SkyUI only.\n";
			out << "; A menu not listed follows [MCM] bImportNewMenus. Set from the Framework Settings page.\n";
			for (const auto& [key, on] : g_import) { out << key << "=" << (on ? 1 : 0) << "\n"; }
		}

		void SetImported(const std::string& a_key, bool a_on)
		{
			std::scoped_lock lock(g_importLock);
			LoadImportLocked();
			g_import[a_key] = a_on;
			SaveImportLocked();
		}

		std::vector<std::string> LedgerKeys()
		{
			std::scoped_lock lock(g_ledgerMutex);
			LoadLedgerLocked();
			return { g_ledger.begin(), g_ledger.end() };
		}

		bool InLedger(const std::string& a_key)
		{
			std::scoped_lock lock(g_ledgerMutex);
			LoadLedgerLocked();
			return g_ledger.contains(a_key);
		}

		void SetInLedger(const std::string& a_key, bool a_in)
		{
			std::scoped_lock lock(g_ledgerMutex);
			LoadLedgerLocked();
			if ((a_in ? g_ledger.insert(a_key).second : g_ledger.erase(a_key) > 0) == false) { return; }
			std::error_code ec;
			fs::create_directories(fs::path(kLedgerPath).parent_path(), ec);
			std::ofstream out(kLedgerPath, std::ios::binary | std::ios::trunc);
			out << "; Menus Apocrypha Menu Framework took out of SkyUI's MCM list - only these are given back.\n";
			for (const auto& key : g_ledger) { out << key << "\n"; }
		}
		std::atomic<unsigned> g_hideGeneration{ 0 };

		// SKI_ConfigManager's RegisterMod / UnregisterMod return the slot (>= 0), -1 (not in the list) - or -2 in its
		// BUSY state, which lasts while the Journal is open (OnMenuOpen..OnMenuClose) and does NOTHING (2026-10-04: a hide
		// with the Journal open left all 83 entries in place). So the result is read, not assumed.
		class ResultThen : public RE::BSScript::IStackCallbackFunctor
		{
		public:
			explicit ResultThen(std::function<void(std::int32_t)> a_fn) : _fn(std::move(a_fn)) {}
			void operator()(RE::BSScript::Variable a_result) override
			{
				if (!a_result.IsInt())
				{
					static int logged = 0;  // a handful, not one per mod
					if (logged++ < 3)
					{
						logger::warn("MCM loader: SkyUI call returned no int (raw type {}, none={})",
							static_cast<std::uint64_t>(a_result.GetType().GetRawType()), a_result.IsNoneObject());
					}
				}
				if (_fn) { _fn(a_result.IsInt() ? a_result.GetSInt() : -3); }
			}
			bool CanSave() const override { return false; }
			void SetObject(const RE::BSTSmartPointer<RE::BSScript::Object>&) override {}

		private:
			std::function<void(std::int32_t)> _fn;
		};

		void RecountHidden()
		{
			std::scoped_lock lock(g_mutex);
			int hidden = 0;
			for (const auto& mod : g_mods) { hidden += mod->hiddenInSkyUI ? 1 : 0; }
			g_hiddenCount.store(hidden);
		}

		void QueueSyncSkyUI(bool a_hide);

		// ONE SkyUI-list pass at a time. A pass reads the switches when it starts, judges every menu from SkyUI's own list
		// and sends its calls; it ends only when every call it sent has returned and been judged. A request while a pass is
		// running marks "again", and one more pass follows with the switches as they are then.
		//
		// Why: under MCM Unlocked every check is an asynchronous native call, and passes started a second apart used to
		// interleave - a newer pass read a menu as still in the list while an older pass's UnregisterMod was on its way, crossed
		// it off the ledger, and the UnregisterMod then took it out for good (Njordlinger Test, 2026-10-04: loaders on then
		// hide off lost 1 menu, a burst of switch changes lost 3). Three patches each closed one interleaving and opened
		// another; serializing removes the whole class - no pass judges a menu while another pass's call to it is in flight.
		std::mutex g_passLock;
		bool g_passRunning = false;
		bool g_passAgain = false;
		std::chrono::steady_clock::time_point g_passStarted{};
		int g_busyPasses = 0;  // consecutive passes that ended with something SkyUI did not take
		// A load starts a new generation (Main Agent's review, 2026-10-05). A load drops Papyrus calls in flight, so a pass
		// started just before one never finished: g_passRunning stayed true, the give-back passes after the load only set
		// "again", and nothing asked again after the 60 s override - "hide off, then load at once" left AMF-hidden menus out
		// of SkyUI until the next switch change. Now OnGameLoaded clears the state and bumps this; a pass task queued before
		// the load does not run, and a late finish from the old pass is ignored.
		unsigned g_passGeneration = 0;

		void RunPass(unsigned a_generation);

		void StartPassTask(unsigned a_generation)
		{
			if (const auto tasks = SKSE::GetTaskInterface())
			{
				tasks->AddTask([a_generation]() { RunPass(a_generation); });
				return;
			}
			std::scoped_lock l(g_passLock);
			if (a_generation == g_passGeneration) { g_passRunning = false; }
		}

		void ResetPassForLoad()
		{
			std::scoped_lock l(g_passLock);
			if (g_passRunning) { logger::info("MCM loader: a SkyUI-list pass was running when a game was loaded - it is dropped"); }
			++g_passGeneration;
			g_passRunning = false;
			g_passAgain = false;
			g_busyPasses = 0;
		}

		void RequestPass()
		{
			unsigned generation = 0;
			{
				std::scoped_lock l(g_passLock);
				if (g_passRunning && std::chrono::steady_clock::now() - g_passStarted < std::chrono::seconds(60))
				{
					g_passAgain = true;
					return;
				}
				if (g_passRunning) { logger::warn("MCM loader: a SkyUI-list pass has not finished after 60 s - starting a new one"); }
				g_passRunning = true;
				g_passStarted = std::chrono::steady_clock::now();
				generation = g_passGeneration;
			}
			StartPassTask(generation);
		}

		// Any thread, exactly once per pass. The next pass when one was asked for meanwhile; otherwise, when SkyUI did not take
		// something (busy while its Journal is open, a list that is full, a call that never ran), another pass in 3 s - for up
		// to 200 such passes in a row (10 minutes).
		void FinishPass(bool a_busy, unsigned a_generation)
		{
			bool again = false;
			int busyPasses = 0;
			{
				std::scoped_lock l(g_passLock);
				if (a_generation != g_passGeneration)
				{
					logger::info("MCM loader: a SkyUI-list pass from before the last load finished late - ignored");
					return;
				}
				g_busyPasses = a_busy ? g_busyPasses + 1 : 0;
				busyPasses = g_busyPasses;
				again = g_passAgain;
				g_passAgain = false;
				if (!again) { g_passRunning = false; }
			}
			if (again)
			{
				{
					std::scoped_lock l(g_passLock);
					g_passStarted = std::chrono::steady_clock::now();
				}
				StartPassTask(a_generation);
				return;
			}
			if (!a_busy) { return; }
			if (busyPasses > 200)
			{
				logger::warn("MCM loader: SkyUI has not taken every change after 200 passes - giving up until the next switch change or load");
				std::scoped_lock l(g_passLock);
				g_busyPasses = 0;
				return;
			}
			std::thread([]() {
				std::this_thread::sleep_for(std::chrono::seconds(3));
				RequestPass();
			}).detach();
		}

		// SkyUI's MCM list is kept three ways in the wild, and membership is read from whichever one runs:
		//  - stock SkyUI: SKI_ConfigManager._modConfigs (128 slots);
		//  - "Barzing" (a 128-limit lift MCM Helper also reads): _MainMenu, then _modConfigsP1, P2 ... (128 each);
		//  - MCM Unlocked (Nexus 180186): no arrays at all - its DLL keeps the list, behind the global natives
		//    MCMUnlocked.GetConfigCount() / GetModIDFromConfigID(int) -> string / GetConfigBase(string modID), where the
		//    mod ID is the name the config registered with (its ModName); RegisterMod returns 1 / -1, UnregisterMod 1.
		// Read 2026-10-04 from MCM Unlocked 2.0.1's SKI_ConfigManager.pex: Njordlinger runs it, and reading _modConfigs there
		// found nothing - the hide switch took nothing out and its give-back retried for ten minutes.
		enum class ListLayout : int { kStock = 0, kBarzing = 1, kUnlocked = 2, kUnknown = 3 };
		std::atomic<int> g_listLayout{ -1 };  // -1: not read yet

		ListLayout DetectLayout(const RE::BSTSmartPointer<RE::BSScript::Object>& a_manager)
		{
			const auto isArray = [&](const char* a_name) {
				const auto v = detail::ScriptVar(a_manager, a_name);
				return v && v->IsArray();
			};
			ListLayout layout = ListLayout::kUnknown;
			if (isArray("_modConfigs")) { layout = ListLayout::kStock; }
			else if (isArray("_MainMenu")) { layout = ListLayout::kBarzing; }
			else
			{
				const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
				RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> info;
				if (vm && vm->GetScriptObjectType("MCMUnlocked", info) && info) { layout = ListLayout::kUnlocked; }
			}
			if (g_listLayout.exchange(static_cast<int>(layout)) != static_cast<int>(layout))
			{
				static const char* names[] = { "stock SkyUI (_modConfigs)", "Barzing (_MainMenu + _modConfigsP<n>)", "MCM Unlocked (its natives)",
					"UNKNOWN - membership cannot be read, so the hide switch takes nothing out" };
				logger::info("MCM loader: SkyUI's MCM list is kept by {}", names[static_cast<int>(layout)]);
			}
			return layout;
		}

		bool InArray(const RE::BSTSmartPointer<RE::BSScript::Object>& a_manager, const std::string& a_name, const RE::BSTSmartPointer<RE::BSScript::Object>& a_config)
		{
			const auto var = detail::ScriptVar(a_manager, a_name);
			const auto array = var && var->IsArray() ? var->GetArray() : nullptr;
			if (!array || !a_config) { return false; }
			for (std::uint32_t i = 0; i < array->size(); ++i)
			{
				const auto& v = (*array)[i];
				if (v.IsObject() && v.GetObject().get() == a_config.get()) { return true; }
			}
			return false;
		}

		// Is this config in SkyUI's list right now? a_then gets true / false, or nullopt when the list cannot be read. Runs
		// a_then at once for the array layouts; under MCM Unlocked, from the VM thread when its native answers.
		void CheckInSkyUIList(ListLayout a_layout, const RE::BSTSmartPointer<RE::BSScript::Object>& a_manager,
			const RE::BSTSmartPointer<RE::BSScript::Object>& a_config, const std::string& a_regName, std::function<void(std::optional<bool>)> a_then)
		{
			switch (a_layout)
			{
			case ListLayout::kStock:
				a_then(InArray(a_manager, "_modConfigs", a_config));
				return;
			case ListLayout::kBarzing:
			{
				bool in = InArray(a_manager, "_MainMenu", a_config);
				for (int page = 1; !in && page < 32; ++page)
				{
					const std::string name = "_modConfigsP" + std::to_string(page);
					if (!detail::ScriptVar(a_manager, name)) { break; }
					in = InArray(a_manager, name, a_config);
				}
				a_then(in);
				return;
			}
			case ListLayout::kUnlocked:
			{
				const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
				VarArgs args;
				args.args.resize(1);
				args.args[0].SetString(a_regName);
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> then{ new detail::ResultFn(
					[config = a_config, a_then](const RE::BSScript::Variable& a_result) {
						a_then(a_result.IsObject() && a_result.GetObject() && a_result.GetObject().get() == config.get());
					}) };
				if (!vm || !vm->DispatchStaticCall("MCMUnlocked", "GetConfigBase", &args, then)) { a_then(std::nullopt); }
				return;
			}
			default:
				a_then(std::nullopt);
				return;
			}
		}

		// One pass (main thread to start; finishes on whichever thread the last answer arrives). With the hide switch on,
		// UnregisterMod every hideable mod of a loader that is switched on; otherwise (and for a loader switched off)
		// RegisterMod the ones the ledger says AMF hid - the save may hold them hidden from an earlier session. MCM Helper mods
		// (phases 1-2) and script-only menus (phase 3) each follow their own switch.
		//
		// The ledger records the INTENT before the call (a menu AMF is about to take out is AMF's to give back, whatever the
		// call returns), and an entry leaves it only when SkyUI's list shows the menu back. Each outcome is judged from the
		// list itself (CheckInSkyUIList, whichever manager keeps it), never from the call's return value. A config SkyUI
		// never registered (discovery also finds those - late, past 128, or under MCM Unlocked) is "already out" and not
		// AMF's: nothing is sent for it.
		void RunPass(unsigned a_generation)
		{
			{
				std::scoped_lock l(g_passLock);
				if (a_generation != g_passGeneration)
				{
					logger::info("MCM loader: a SkyUI-list pass queued before the last load was dropped");
					return;
				}
			}
			const bool hide = settings::Get().hideMcmInSkyUI;
			const bool hideHelper = hide && settings::Get().loadMcmHelperConfigs;
			const bool hideScripts = hide && settings::Get().loadSkyUIScriptMenus;
			scripts::Discover();  // main thread; idempotent - a config registered since the last pass is found
			const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto manager = FindSkyUIManager();
			if (!vm || !manager)
			{
				logger::info("MCM loader: SkyUI's config manager is not running yet - SkyUI's list left as it is for now");
				FinishPass(false, a_generation);
				return;
			}
			// The parameter is SKI_ConfigBase, and a call dispatched from here is NOT upcast: the mod's own script type was
			// refused (Papyrus.0.log, 2026-10-04: "RegisterMod(SKI_ConfigBase a_menu,string a_modName) received incompatible
			// arguments! Received types (TrueHUD_MCM,string)"), as were a bare object and the quest form. Typed as the base it passes.
			RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> baseInfo;
			if (!vm->GetScriptObjectType("SKI_ConfigBase", baseInfo) || !baseInfo)
			{
				logger::warn("MCM loader: SKI_ConfigBase is not a loaded script type - SkyUI's list left as it is");
				FinishPass(false, a_generation);
				return;
			}
			const auto baseType = baseInfo->GetRawType();
			const ListLayout layout = DetectLayout(manager);

			struct Target
			{
				RE::BSTSmartPointer<RE::BSScript::Object> config;
				std::string regName;   // the name the config registers itself with (its ModName property)
				std::string key;       // its ledger line
				bool hide;
				std::function<void(bool)> setHidden;
			};
			std::vector<Target> targets;
			const std::vector<ModView> mods = ModsView();
			for (const ModView& mod : mods)
			{
				if (!mod.hideable) { continue; }
				const std::size_t m = mod.index;
				const std::string key = "mcmhelper|" + mod.modName;
				const bool hideThis = hideHelper && detail::IsImported(key);  // a menu not imported here is SkyUI's
				if (!hideThis && !InLedger(key)) { continue; }  // not hidden by AMF: SkyUI's list is not ours to change
				auto config = EnsureScript(m);
				if (!config) { continue; }
				const auto name = config->GetProperty("ModName");
				targets.push_back({ config, name && name->IsString() ? std::string(name->GetString()) : mod.modName, key, hideThis,
					[m](bool a_hidden) {
						{
							std::scoped_lock lock(g_mutex);
							if (m < g_mods.size()) { g_mods[m]->hiddenInSkyUI = a_hidden; }
						}
						RecountHidden();
					} });
			}
			// An MCM Helper menu AMF hid while its loader was on, with the loader off since the game started (no entry was
			// read): found from the ledger line itself, so it still goes back.
			for (const auto& key : LedgerKeys())
			{
				if (key.rfind("mcmhelper|", 0) != 0) { continue; }
				const std::string modName = key.substr(10);
				if (std::any_of(mods.begin(), mods.end(), [&](const ModView& m) { return m.modName == modName; })) { continue; }
				RE::FormID questId = 0;
				auto config = FindConfigScript(modName, questId);
				if (!config) { continue; }
				const auto name = config->GetProperty("ModName");
				targets.push_back({ config, name && name->IsString() ? std::string(name->GetString()) : modName, key, false, [](bool) {} });
			}
			for (const auto& t : scripts::HideTargets())
			{
				const bool hideThis = hideScripts && detail::IsImported(t.key);  // a menu not imported here is SkyUI's
				if (!hideThis && !InLedger(t.key)) { continue; }  // not hidden by AMF: left exactly as SkyUI (or MenuMaid2) has it
				const std::size_t index = t.index;
				targets.push_back({ t.config, t.modName, t.key, hideThis, [index](bool a_hidden) { scripts::SetHidden(index, a_hidden); } });
			}

			// The pass ends when every target has finished (+1 holds it open until all are started).
			struct PassState
			{
				std::atomic<int> left{ 0 };
				std::atomic<bool> busy{ false };
			};
			auto state = std::make_shared<PassState>();
			state->left.store(static_cast<int>(targets.size()) + 1);
			auto done = [state, a_generation]() {
				if (state->left.fetch_sub(1) == 1) { FinishPass(state->busy.load(), a_generation); }
			};

			// The call for one target, its outcome judged from the list afterwards (VM thread when the call returns).
			auto send = [vm, manager, baseType, layout, state, done](const Target& a_t) {
				VarArgs args;
				args.args.resize(a_t.hide ? 1 : 2);
				args.args[0].SetObject(a_t.config, baseType);
				if (!a_t.hide) { args.args[1].SetString(a_t.regName); }
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> then{ new ResultThen([manager, layout, state, done, t = a_t](std::int32_t a_result) {
					CheckInSkyUIList(layout, manager, t.config, t.regName, [state, done, t, a_result](std::optional<bool> a_in) {
						if (!a_in)
						{
							// the list cannot be read (only a give-back gets here): the result is the only evidence
							if (!t.hide && a_result >= 0)
							{
								SetInLedger(t.key, false);
								t.setHidden(false);
							}
							logger::info("MCM loader: SkyUI RegisterMod {} -> {} (SkyUI's list not readable here)", t.regName, a_result);
							done();
							return;
						}
						const bool took = t.hide ? !*a_in : *a_in;
						if (!took)
						{
							// SkyUI busy (-2, its Journal open), a full list on give-back, or a call that never ran: next pass
							state->busy.store(true);
							logger::info("MCM loader: SkyUI {} {} did not take (result {}) - tried again", t.hide ? "UnregisterMod" : "RegisterMod", t.regName, a_result);
							done();
							return;
						}
						if (!t.hide) { SetInLedger(t.key, false); }
						t.setHidden(t.hide);
						logger::debug("MCM loader: SkyUI {} {} - {} SkyUI's list (result {})", t.hide ? "UnregisterMod" : "RegisterMod", t.regName,
							t.hide ? "out of" : "back in", a_result);
						done();
					});
				}) };
				auto target = manager;
				if (!vm->DispatchMethodCall(target, a_t.hide ? "UnregisterMod" : "RegisterMod", &args, then))
				{
					logger::warn("MCM loader: SkyUI {} {} could not be sent", a_t.hide ? "UnregisterMod" : "RegisterMod", a_t.regName);
					state->busy.store(true);
					done();
				}
			};

			logger::info("MCM loader: SkyUI's MCM list - pass over {} menu(s) ({}), each judged from the list itself",
				targets.size(), hide ? "take out" : "give back");
			for (const auto& t : targets)
			{
				CheckInSkyUIList(layout, manager, t.config, t.regName, [t, send, done](std::optional<bool> a_in) {
					if (!a_in)
					{
						// unreadable list: nothing is taken out (it could never be proven back); a give-back is still sent
						if (!t.hide) { send(t); }
						else { done(); }
						return;
					}
					if (t.hide && !*a_in)
					{
						t.setHidden(InLedger(t.key));  // already out - AMF's only if the ledger says AMF took it out
						done();
						return;
					}
					if (!t.hide && *a_in)
					{
						SetInLedger(t.key, false);  // already back
						t.setHidden(false);
						done();
						return;
					}
					if (t.hide) { SetInLedger(t.key, true); }  // the intent, before the call
					send(t);
				});
			}
			done();  // all started
		}

		// Any thread. The switches are read when the pass starts, so a_hide only documents the caller's reason.
		void QueueSyncSkyUI(bool /*a_hide*/)
		{
			RequestPass();
		}

		// After a load the mods register over the first seconds (SkyUI's announcements at 0, 5, 10 ... 30 s): take them out
		// a few times over the first minute and a half so a late registration is caught too. Each pass is idempotent.
		void ScheduleHideAfterLoad()
		{
			const unsigned generation = ++g_hideGeneration;
			std::thread([generation]() {
				for (const int gap : { 3, 5, 7, 20, 35 })  // passes at 3, 8, 15, 35 and 70 s after the load
				{
					std::this_thread::sleep_for(std::chrono::seconds(gap));
					if (g_hideGeneration.load() != generation || !settings::Get().hideMcmInSkyUI ||
						(!settings::Get().loadMcmHelperConfigs && !settings::Get().loadSkyUIScriptMenus)) { return; }
					QueueSyncSkyUI(true);
				}
			}).detach();
		}

		// ---------------------------------------------------------------------------------------------- drawing

		std::string KeyName(std::int32_t a_code)
		{
			if (a_code <= 0) { return "-"; }
			if (a_code >= 256 && a_code < 266) { return "Mouse " + std::to_string(a_code - 256 + 1); }
			if (a_code >= 266) { return "Pad " + std::to_string(a_code); }
			const LONG lParam = ((a_code & 0x7F) << 16) | ((a_code & 0x80) ? (1 << 24) : 0);
			wchar_t name[64]{};
			if (GetKeyNameTextW(lParam, name, 64) > 0) { return Narrow(name); }
			return "Key " + std::to_string(a_code);
		}

		std::optional<std::pair<std::size_t, std::string>> g_capturing;  // render thread: (mod, id) awaiting a key

		// The control's help, to the help bar under the pane (2.1.5) - or a popup kept inside it with the bar switched off.
		// Hover and controller highlight alike.
		void Tooltip(const Control& a_c, const std::string& a_value)
		{
			if (a_c.help.empty() || (!ImGui::IsItemHovered() && !ImGui::IsItemFocused())) { return; }
			std::string help = Translate(a_c.help);
			if (const auto at = help.find("{value}"); at != std::string::npos) { help.replace(at, 7, a_value); }
			helpbar::OfferForLastItem(help);
		}

		void DrawControl(std::size_t a_mod, const Control& a_c, const std::string& a_value)
		{
			const std::string label = Translate(a_c.text);
			const std::string imguiId = label + "##" + a_c.id + std::to_string(reinterpret_cast<std::uintptr_t>(&a_c));

			const bool holdsValue = a_c.kind == Kind::kToggle || a_c.kind == Kind::kSlider || a_c.kind == Kind::kStepper ||
			                        a_c.kind == Kind::kMenu || a_c.kind == Kind::kEnum || a_c.kind == Kind::kColor ||
			                        a_c.kind == Kind::kKeymap || a_c.kind == Kind::kInput;
			if (a_c.source == Source::kPhase2 || (a_c.hasAction && !a_c.action) || (holdsValue && a_c.source == Source::kNone))
			{
				ImGui::BeginDisabled();
				ImGui::TextUnformatted((std::string(icons::kWarning) + "  " + label).c_str());
				ImGui::SameLine();
				ImGui::TextDisabled(strings::TR("AMF_McmSetInSkyUI", "(%s - set it in SkyUI's MCM)"),
					(a_c.hasAction && !a_c.action) ? strings::TR("AMF_McmActionNotRun", "an action type AMF does not run") :
					a_c.sourceTypeName.empty() ? strings::TR("AMF_McmNoSource", "no value source") : a_c.sourceTypeName.c_str());
				ImGui::EndDisabled();
				return;
			}
			// A text control with an action is a button in SkyUI's menu (Load, Default, LoadPreset...).
			if (a_c.kind == Kind::kText && a_c.action)
			{
				const std::string value = a_c.source == Source::kString || a_c.source == Source::kPropString ? a_value : Translate(a_c.staticValue);
				const std::string button = std::string(icons::kAction) + "  " + (label.empty() ? a_c.action->function : label) + "##btn" + a_c.key;
				if (ImGui::Button(button.c_str())) { Apply(a_mod, a_c, value); }
				if (!value.empty())
				{
					ImGui::SameLine();
					ImGui::TextDisabled("%s", value.c_str());
				}
				Tooltip(a_c, value);
				return;
			}

			switch (a_c.kind)
			{
			case Kind::kEmpty:
				ImGui::Spacing();
				break;
			case Kind::kHeader:
				ImGui::Spacing();
				mcmstyle::Heading(label);   // 2.1.5: the theme's heading colour
				break;
			case Kind::kText:
				ImGui::TextUnformatted(label.c_str());
				if (const std::string shown = a_c.source == Source::kString || a_c.source == Source::kPropString ? a_value : Translate(a_c.staticValue); !shown.empty())
				{
					ImGui::SameLine();
					ImGui::TextDisabled("%s", shown.c_str());
				}
				break;
			case Kind::kToggle:
			{
				bool v = ParseBool(a_value);
				if (widgets::Toggle(imguiId.c_str(), &v)) { Apply(a_mod, a_c, v ? "1" : "0"); }
				Tooltip(a_c, v ? "on" : "off");
				break;
			}
			case Kind::kSlider:
			{
				const bool whole = a_c.source == Source::kInt || a_c.source == Source::kPropInt;
				float v = whole ? static_cast<float>(ParseInt(a_value)) : ParseFloat(a_value);
				const float before = v;
				if (precise::SliderFloat(imguiId.c_str(), &v, a_c.min, a_c.max, a_c.format.c_str()))
				{
					// MCM's step can be coarser than the shown digit (step 5 on "{0}"): a one-digit nudge then moves a
					// whole step in its direction instead of snapping straight back (rule 68's intent: every press moves).
					if (v != before && std::fabs(v - before) < a_c.step * 0.5f) { v = before + (v > before ? a_c.step : -a_c.step); }
					v = a_c.min + std::round((v - a_c.min) / a_c.step) * a_c.step;
					v = std::clamp(v, a_c.min, a_c.max);
					if (v != before)
					{
						Apply(a_mod, a_c, whole ? std::to_string(static_cast<long long>(std::lround(v))) : FormatFloat(v));
					}
				}
				Tooltip(a_c, a_value);
				break;
			}
			case Kind::kStepper:
			case Kind::kEnum:
			{
				int index = static_cast<int>(ParseInt(a_value));
				const std::string current = (index >= 0 && index < static_cast<int>(a_c.options.size())) ? Translate(a_c.options[index]) : std::to_string(index);
				if (theme::BeginComboTight(imguiId.c_str(), current.c_str()))
				{
					for (int i = 0; i < static_cast<int>(a_c.options.size()); ++i)
					{
						const std::string opt = Translate(a_c.options[i]) + "##" + std::to_string(i);
						if (ImGui::Selectable(opt.c_str(), i == index) && i != index) { Apply(a_mod, a_c, std::to_string(i)); }
					}
					ImGui::EndCombo();
				}
				Tooltip(a_c, current);
				break;
			}
			case Kind::kMenu:
			{
				const std::string current = Translate(a_value);
				if (theme::BeginComboTight(imguiId.c_str(), current.c_str()))
				{
					for (std::size_t i = 0; i < a_c.options.size(); ++i)
					{
						const std::string opt = Translate(a_c.options[i]) + "##" + std::to_string(i);
						if (ImGui::Selectable(opt.c_str(), a_c.options[i] == a_value) && a_c.options[i] != a_value) { Apply(a_mod, a_c, a_c.options[i]); }
					}
					ImGui::EndCombo();
				}
				Tooltip(a_c, current);
				break;
			}
			case Kind::kInput:
			{
				char buffer[512]{};
				strncpy_s(buffer, a_value.c_str(), _TRUNCATE);
				ImGui::InputText((std::string(icons::kPen) + "  " + imguiId).c_str(), buffer, sizeof(buffer));
				keyboard::NoteTextField(ImGui::GetItemID());
				if (ImGui::IsItemDeactivatedAfterEdit() && a_value != buffer) { Apply(a_mod, a_c, buffer); }
				Tooltip(a_c, a_value);
				break;
			}
			case Kind::kColor:
			{
				const auto rgb = static_cast<std::uint32_t>(ParseInt(a_value));
				float col[3]{ ((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f };
				if (ImGui::ColorEdit3((std::string(icons::kPalette) + "  " + imguiId).c_str(), col))
				{
					const auto c = [](float f) { return static_cast<std::uint32_t>(std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f)); };
					Apply(a_mod, a_c, std::to_string((c(col[0]) << 16) | (c(col[1]) << 8) | c(col[2])));
				}
				Tooltip(a_c, a_value);
				break;
			}
			case Kind::kKeymap:
			{
				const bool waiting = g_capturing && g_capturing->first == a_mod && g_capturing->second == a_c.id;
				ImGui::TextUnformatted(label.c_str());
				ImGui::SameLine();
				const std::string button = std::string(icons::kKeyboard) + "  " + (waiting ? std::string(strings::TR("AMF_McmPressKey", "Press a key...")) : KeyName(static_cast<std::int32_t>(ParseInt(a_value)))) + "##key" + a_c.id;
				if (ImGui::Button(button.c_str()) && !waiting)
				{
					g_capturing = std::make_pair(a_mod, a_c.id);
					input::ArmKeyCapture();
				}
				ImGui::SameLine();
				if (ImGui::Button((std::string(icons::kClear) + "  " + strings::TR("AMF_McmClear", "Clear") + "##clr" + a_c.id).c_str())) { Apply(a_mod, a_c, "-1"); }
				if (waiting && !input::IsKeyCaptureArmed())
				{
					// LastCapturedKey: (device << 32) | code - 0 keyboard (DirectInput scan code), 1 mouse, 2 gamepad
					const std::int64_t packed = input::LastCapturedKey();
					g_capturing.reset();
					if (packed >= 0)
					{
						const auto device = static_cast<std::int32_t>(packed >> 32);
						const auto code = static_cast<std::int32_t>(packed & 0xFFFFFFFF);
						if (device == 0 && code != 0x01) { Apply(a_mod, a_c, std::to_string(code)); }
						else if (device == 1) { Apply(a_mod, a_c, std::to_string(256 + code)); }
						else { logger::info("MCM loader: keymap {} - Escape or a controller button; keymaps take keyboard and mouse in phase 1", a_c.id); }
					}
				}
				Tooltip(a_c, KeyName(static_cast<std::int32_t>(ParseInt(a_value))));
				break;
			}
			case Kind::kHiddenToggle:
				break;  // never drawn; it still drives its group
			case Kind::kUnknown:
			default:
				ImGui::TextDisabled("%s", icons::kWarning);
				ImGui::SameLine();
				ImGui::TextDisabled(strings::TR("AMF_McmNotDrawn", "%s (MCM type \"%s\" is not drawn yet)"), label.c_str(), a_c.typeName.c_str());
				break;
			}
		}

		// SkyUI's lifecycle, mirrored: OnConfigOpen when a mod's entry starts being drawn, OnConfigClose when it stops
		// (another entry chosen, or the menu closed). Render thread; the open mod is also read by the DevBench tool.
		std::atomic<int> g_openMod{ -1 };
		int g_lastDrawFrame = -10;

		void NoteDrawn(std::size_t a_mod)
		{
			const int open = g_openMod.load();
			static double lastRefresh = -1.0;
			const double now = ImGui::GetTime();
			if (open != static_cast<int>(a_mod))
			{
				if (open >= 0) { QueueEvent(static_cast<std::size_t>(open), "OnConfigClose"); }
				QueueEvent(a_mod, "OnConfigOpen");
				QueueRefresh(a_mod);
				lastRefresh = now;
				g_openMod.store(static_cast<int>(a_mod));
			}
			else if (now - lastRefresh > 0.5)
			{
				// Globals and script properties are the mod's own state: re-read them twice a second while the page is
				// open (the mod's scripts change them in OnConfigOpen / OnSettingChange), never every frame.
				QueueRefresh(a_mod);
				lastRefresh = now;
			}
			g_lastDrawFrame = ImGui::GetFrameCount();
		}

		void DrawPage(std::size_t a_mod, std::size_t a_page)
		{
			NoteDrawn(a_mod);
			const Mod* mod = nullptr;
			std::map<std::string, std::string> values;
			{
				std::scoped_lock lock(g_mutex);
				if (a_mod >= g_mods.size() || a_page >= g_mods[a_mod]->pages.size()) { return; }
				mod = g_mods[a_mod].get();
				// The page's text follows a language change (2.1.1), read again here on the drawing thread under the lock
				// every other reader takes. Entry and tab names keep the language they registered in: a menu's entry name
				// is what the player's order and renames are keyed on.
				if (Mod& owned = *g_mods[a_mod]; owned.translationsLanguage != TextLanguage())
				{
					owned.translations = LoadTranslations(owned.modName);
					owned.translationsLanguage = TextLanguage();
				}
				values = mod->values;  // a copy: the page draws from a stable view while a set lands from another thread
			}
			const Page& page = mod->pages[a_page];
			g_table = &mod->translations;

			helpbar::Want(mod->modName + "|" + std::to_string(a_page));   // 2.1.5: the controls' help goes to the bar
			mcmstyle::PageNote(icons::kInfo, mcmstyle::Fmt(strings::TR("AMF_McmHelperNote",
				"Read from %s's MCM Helper files. Changes go through MCM Helper, as in its own menu."), mod->modName.c_str()));
			if (page.customContent)
			{
				mcmstyle::PageNote(icons::kWarning, strings::TR("AMF_McmCustomPage", "This MCM page is a custom picture or SWF - not drawable here."));
				return;
			}

			// The page's groups, from its toggles' groupControl (MCM Helper keeps them per page as well).
			std::map<int, bool> groups;
			for (const Control& c : page.controls)
			{
				if (c.groupControl > 0)
				{
					const auto it = values.find(c.key);
					groups[c.groupControl] = it != values.end() && ParseBool(it->second);
				}
			}

			const mcmstyle::PageScope look;   // 2.1.5: the player's row spacing and label / value colours for the body
			for (const Control& c : page.controls)
			{
				const bool active = !c.cond || Eval(*c.cond, groups);
				if (!active && c.behavior != Behavior::kDisable) { continue; }
				const auto it = values.find(c.key);
				const std::string value = it != values.end() ? it->second : std::string{};
				if (!active) { ImGui::BeginDisabled(); }
				ImGui::PushID(&c);
				DrawControl(a_mod, c, value);
				ImGui::PopID();
				if (!active) { ImGui::EndDisabled(); }
			}
		}

		// ------------------------------------------------------------------------------------------- loading

		bool PluginLoaded(const std::string& a_name)
		{
			const auto dataHandler = RE::TESDataHandler::GetSingleton();
			if (!dataHandler) { return false; }
			if (dataHandler->LookupLoadedModByName(a_name) || dataHandler->LookupLoadedLightModByName(a_name)) { return true; }
			return false;
		}

		bool OwnPluginLoaded(const std::string& a_modName)
		{
			for (const char* ext : { ".esp", ".esm", ".esl" })
			{
				if (PluginLoaded(a_modName + ext)) { return true; }
			}
			return false;
		}

		void Skip(const std::string& a_mod, const std::string& a_reason)
		{
			logger::info("MCM loader: skipped {} - {}", a_mod, a_reason);
			g_skipped.emplace_back(a_mod, a_reason);
		}

		void LoadOne(const fs::path& a_folder, std::vector<std::unique_ptr<Mod>>& a_into)
		{
			const std::string modName = a_folder.filename().string();
			const fs::path configPath = a_folder / "config.json";

			if (!OwnPluginLoaded(modName))
			{
				Skip(modName, "its plugin (" + modName + ".esp/.esm/.esl) is not loaded");
				return;
			}

			json config;
			try
			{
				std::ifstream in(configPath, std::ios::binary);
				config = json::parse(in, nullptr, true, true);  // allow comments, as MCM Helper's reader does
			}
			catch (const std::exception& e)
			{
				Skip(modName, std::string("config.json does not parse: ") + e.what());
				return;
			}

			if (const auto req = config.find("pluginRequirements"); req != config.end() && req->is_array())
			{
				for (const auto& p : *req)
				{
					if (p.is_string() && !PluginLoaded(p.get<std::string>()))
					{
						Skip(modName, "it requires " + p.get<std::string>() + ", which is not loaded");
						return;
					}
				}
			}

			auto mod = std::make_unique<Mod>();
			mod->modName = modName;
			mod->displayName = JsonStr(config, "displayName");
			if (mod->displayName.empty()) { mod->displayName = modName; }

			auto readPage = [&](const std::string& a_name, const json& a_page) {
				Page page;
				page.name = a_name;
				page.customContent = a_page.contains("customContent") && !a_page.contains("content");
				if (const auto content = a_page.find("content"); content != a_page.end() && content->is_array())
				{
					for (const auto& c : *content)
					{
						if (!c.is_object()) { continue; }
						Control control = ParseControl(c);
						// A property- or global-backed control usually has no id; it still needs its own slot in values.
						control.key = !control.id.empty() ? control.id :
							"#" + std::to_string(mod->pages.size()) + "." + std::to_string(page.controls.size());
						++mod->controlCount;
						if (control.source == Source::kPhase2 || (control.hasAction && !control.action)) { ++mod->phase2Count; }
						page.controls.push_back(std::move(control));
					}
				}
				mod->pages.push_back(std::move(page));
			};

			if (const auto pages = config.find("pages"); pages != config.end() && pages->is_array())
			{
				for (const auto& p : *pages)
				{
					if (p.is_object()) { readPage(JsonStr(p, "pageDisplayName"), p); }
				}
			}
			else if (config.contains("content") || config.contains("customContent"))
			{
				readPage(mod->displayName, config);  // a single-page MCM
			}
			if (mod->pages.empty())
			{
				Skip(modName, "config.json has no pages");
				return;
			}
			mod->hideable = mod->phase2Count == 0 &&
				std::none_of(mod->pages.begin(), mod->pages.end(), [](const Page& p) { return p.customContent; });

			mod->translations = LoadTranslations(modName);
			mod->translationsLanguage = TextLanguage();
			mod->defaults = ReadIni(a_folder / "settings.ini");
			mod->values = mod->defaults;
			const fs::path userIni = fs::path("Data/MCM/Settings") / (modName + ".ini");
			const auto user = ReadIni(userIni);
			for (const auto& [k, v] : user) { mod->values[k] = v; }

			logger::debug("MCM loader: {} - {} page(s), {} control(s) ({} phase 2), {} default(s), {} user value(s)",
				modName, mod->pages.size(), mod->controlCount, mod->phase2Count, mod->defaults.size(), user.size());

			a_into.push_back(std::move(mod));   // Load()'s own list - published to g_mods in one move
		}

		// Every MCM Helper entry's pages shown exactly when its loader is on and the menu is imported.
		void ApplyHelperVisibility()
		{
			const bool on = settings::Get().loadMcmHelperConfigs;
			for (const auto& mod : ModsView())
			{
				const bool show = on && detail::IsImported("mcmhelper|" + mod.modName);
				for (const auto& page : mod.pageNames) { registry::SetPageVisible(mod.entryName.c_str(), page.c_str(), show); }
			}
		}

		// Before the publish: a page drawn in between finds a_mod past g_mods' end (under g_mutex) and draws nothing.
		void RegisterPages(std::vector<std::unique_ptr<Mod>>& a_mods)
		{
			std::size_t pageCount = 0;
			for (std::size_t m = 0; m < a_mods.size(); ++m)
			{
				Mod& mod = *a_mods[m];
				// " (MCM)" keeps it apart from a mod's own AMF page of the same name - the registry merges equal names
				// into one entry's tabs, which would mix our pages into theirs.
				// Entry and tab names are fixed at registration, so translate them now - in the GAME's language, not the one
				// AMF shows (2.1.1): the entry name keys the player's order, renames and learned placements.
				const Table nameTable = LoadTranslations(mod.modName, true);
				g_table = &nameTable;
				mod.entryName = Translate(mod.displayName);
				if (mod.entryName.empty()) { mod.entryName = mod.modName; }
				mod.entryName += " (MCM)";
				std::set<std::string> used;
				for (std::size_t p = 0; p < mod.pages.size(); ++p)
				{
					std::string name = mod.pages[p].name.empty() ? std::string(strings::TR("AMF_McmSettingsTab", "Settings")) : Translate(mod.pages[p].name);
					if (name.empty()) { name = strings::TR("AMF_McmSettingsTab", "Settings"); }
					for (int n = 2; used.contains(name); ++n) { name = name + " (" + std::to_string(n) + ")"; }
					used.insert(name);
					if (registry::RegisterFn(mod.entryName.c_str(), name.c_str(), [m, p]() { DrawPage(m, p); }, true))
					{
						++pageCount;
						mod.pageNames.push_back(name);
					}
				}
			}
			logger::info("MCM loader: {} MCM Helper mod(s) registered as AMF entries ({} page(s)); {} skipped",
				a_mods.size(), pageCount, g_skipped.size());
		}
	}

	void Load()
	{
		if (!settings::Get().loadMcmHelperConfigs)
		{
			logger::info("MCM loader: off ([MCM] bLoadMcmHelperConfigs=0) - switching it on in the settings page loads the configs then");
			return;
		}
		if (g_loaded.exchange(true)) { return; }

		const fs::path root("Data/MCM/Config");
		std::error_code ec;
		if (!fs::is_directory(root, ec))
		{
			logger::info("MCM loader: no Data/MCM/Config folder - no MCM Helper mods to read");
			return;
		}

		logger::info("MCM loader: reading MCM Helper configs from {}", root.string());
		std::vector<std::unique_ptr<Mod>> loaded;
		for (const auto& entry : fs::directory_iterator(root, ec))
		{
			if (!entry.is_directory(ec)) { continue; }
			if (!fs::exists(entry.path() / "config.json", ec)) { continue; }
			try { LoadOne(entry.path(), loaded); }
			catch (const std::exception& e) { Skip(entry.path().filename().string(), std::string("unexpected error: ") + e.what()); }
		}
		RegisterPages(loaded);
		{
			std::scoped_lock lock(g_mutex);
			g_mods = std::move(loaded);   // Load runs once (g_loaded), so g_mods was empty: the indices pages captured hold
		}
		ApplyHelperVisibility();  // menus the player left out of AMF stay out
		g_table = nullptr;  // each page sets its own mod's table when it draws
	}

	void Frame()
	{
		scripts::Frame();
		const int open = g_openMod.load();
		if (open >= 0 && g_lastDrawFrame < ImGui::GetFrameCount() - 1)
		{
			QueueEvent(static_cast<std::size_t>(open), "OnConfigClose");
			g_openMod.store(-1);
		}
	}

	void OnGameLoaded()
	{
		ResetPassForLoad();   // before the after-load passes below ask for one
		scripts::OnGameLoaded();
		{
			std::scoped_lock lock(g_mutex);
			for (auto& mod : g_mods)
			{
				mod->script.reset();
				mod->scriptTried = false;
				mod->scriptState = "not looked up yet (a game was loaded)";
				mod->hiddenInSkyUI = false;  // what the save holds is re-established below
			}
		}
		g_hiddenCount.store(0);
		// No early return when both loaders are off: the give-back below still returns whatever the ledger says AMF hid.
		if (settings::Get().hideMcmInSkyUI && (settings::Get().loadMcmHelperConfigs || settings::Get().loadSkyUIScriptMenus))
		{
			ScheduleHideAfterLoad();
		}
		else
		{
			// a save made while they were hidden keeps them hidden: give SkyUI its menus back once they have registered
			// (RegisterMod returns at once for one already in the list, so this costs nothing when nothing was hidden)
			const unsigned generation = ++g_hideGeneration;
			std::thread([generation]() {
				for (const int gap : { 10, 20 })  // at 10 and 30 s: a config registers in the first half-minute after a load
				{
					std::this_thread::sleep_for(std::chrono::seconds(gap));
					if (g_hideGeneration.load() != generation) { return; }
					QueueSyncSkyUI(false);
				}
			}).detach();
		}
	}

	void SetEnabled(bool a_on)
	{
		if (a_on && !g_loaded.load())
		{
			Load();  // first switched on in this session: read the configs now (the data is long loaded)
		}
		ApplyHelperVisibility();  // the caller has already set the switch; a menu left out of AMF stays out
		logger::info("MCM loader: switched {} on the settings page ({} entries {})", a_on ? "on" : "off", ModsView().size(), a_on ? "shown" : "hidden");
		QueueSyncSkyUI(settings::Get().hideMcmInSkyUI);  // each loader's mods follow its own switch: off gives them back
	}

	void SetHideInSkyUI(bool a_on)
	{
		++g_hideGeneration;  // cancel any pending after-load passes
		if (!settings::Get().loadMcmHelperConfigs && !settings::Get().loadSkyUIScriptMenus && a_on) { return; }  // nothing is managed here
		QueueSyncSkyUI(a_on);
	}

	int HiddenInSkyUI() { return g_hiddenCount.load() + scripts::Hidden(); }

	void SetImportNew(bool a_on)
	{
		// the caller has set and saved the switch: every menu not chosen by hand follows it
		ApplyHelperVisibility();
		scripts::RefreshVisibility();
		QueueSyncSkyUI(settings::Get().hideMcmInSkyUI);
		logger::info("MCM loader: menus not chosen by hand now {} AMF", a_on ? "come into" : "stay out of");
	}

	void SetMenuImported(const std::string& a_key, bool a_on)
	{
		SetImported(a_key, a_on);
		ApplyHelperVisibility();
		scripts::RefreshVisibility();
		QueueSyncSkyUI(settings::Get().hideMcmInSkyUI);  // a menu taken out of AMF goes back to SkyUI if AMF had hidden it
		logger::info("MCM loader: {} {} AMF", a_key, a_on ? "brought into" : "left out of");
	}

	std::vector<ImportRow> ImportList()
	{
		std::vector<ImportRow> rows;
		if (settings::Get().loadMcmHelperConfigs)
		{
			for (const auto& mod : ModsView())
			{
				const std::string key = "mcmhelper|" + mod.modName;
				rows.push_back({ key, mod.entryName, false, detail::IsImported(key) });
			}
		}
		if (settings::Get().loadSkyUIScriptMenus)
		{
			for (const auto& m : scripts::Menus())
			{
				if (m.present) { rows.push_back({ m.key, m.entry, true, detail::IsImported(m.key) }); }
			}
		}
		std::sort(rows.begin(), rows.end(), [](const ImportRow& a, const ImportRow& b) { return Lower(a.entry) < Lower(b.entry); });
		return rows;
	}

	bool SkyUIListUnreadable() { return g_listLayout.load() == static_cast<int>(ListLayout::kUnknown); }

	int HideableInSkyUI()
	{
		int n = 0;
		if (settings::Get().loadMcmHelperConfigs)
		{
			for (const auto& mod : ModsView()) { n += mod.hideable && detail::IsImported("mcmhelper|" + mod.modName) ? 1 : 0; }
		}
		if (settings::Get().loadSkyUIScriptMenus)
		{
			for (const auto& m : scripts::Menus()) { n += m.present && detail::IsImported(m.key) ? 1 : 0; }  // drawn in full
		}
		return n;
	}

	void SetScriptsEnabled(bool a_on)
	{
		scripts::SetEnabled(a_on);
		QueueSyncSkyUI(settings::Get().hideMcmInSkyUI);  // their place in SkyUI's list follows the switch
	}

	std::string ToolJson(const std::string& a_argsJson)
	{
		json args;
		try { args = json::parse(a_argsJson.empty() ? "{}" : a_argsJson); }
		catch (...) { return R"({"ok":false,"error":"arguments are not JSON"})"; }
		const std::string op = JsonStr(args, "op");
		const std::string modArg = JsonStr(args, "mod");
		const std::string id = JsonStr(args, "id");

		auto findMod = [&]() -> std::optional<std::size_t> {
			for (const auto& mod : ModsView())
			{
				if (Lower(mod.modName) == Lower(modArg) || mod.entryName == modArg) { return mod.index; }
			}
			return std::nullopt;
		};

		json out;
		if (op == "skyui") { return scripts::ToolJson(a_argsJson); }  // phase 3: script-only SkyUI menus
		if (op == "remembered") { return rememberedsettings::ToolJson(a_argsJson); }  // the remembered MCM settings (RememberedSettings.cpp)
		if (op == "sort") { return SortToolJson(a_argsJson); }      // the auto-sort into separators (McmSort.cpp)
		if (op == "import")
		{
			// the settings page's import list, the same calls: action list | set {key,on} | all {on} | new {on}
			const std::string action = JsonStr(args, "action").empty() ? std::string("list") : JsonStr(args, "action");
			const auto on = args.find("on");
			const bool hasOn = on != args.end() && on->is_boolean();
			if (action == "set" && hasOn && !JsonStr(args, "key").empty()) { SetMenuImported(JsonStr(args, "key"), on->get<bool>()); }
			else if (action == "all" && hasOn)
			{
				for (const auto& row : ImportList()) { SetImported(row.key, on->get<bool>()); }
				SetImportNew(settings::Get().importNewMcmMenus);  // re-applies visibility and the SkyUI list
			}
			else if (action == "new" && hasOn)
			{
				settings::Get().importNewMcmMenus = on->get<bool>();
				settings::Save();
				SetImportNew(on->get<bool>());
			}
			else if (action != "list")
			{
				return R"J({"ok":false,"error":"import needs action list | set (key, on) | all (on) | new (on)"})J";
			}
			json rows = json::array();
			int imported = 0;
			for (const auto& row : ImportList())
			{
				imported += row.imported ? 1 : 0;
				rows.push_back({ { "key", row.key }, { "entry", row.entry }, { "kind", row.script ? "script" : "mcmhelper" }, { "imported", row.imported } });
			}
			return json{ { "ok", true }, { "importNew", settings::Get().importNewMcmMenus }, { "imported", imported }, { "menus", rows } }.dump();
		}
		if (op == "skyuilist")
		{
			// SkyUI's own list, read on the main thread from whichever manager keeps it (stock _modConfigs/_modNames, Barzing's
			// _MainMenu + _modConfigsP<n>, MCM Unlocked's natives) - the proof that a menu is in or out of SkyUI, independent
			// of SkyUI's Journal (which stalls Papyrus in some lists) and of AMF's own counters.
			auto promise = std::make_shared<std::promise<std::string>>();
			auto future = promise->get_future();
			const auto tasks = SKSE::GetTaskInterface();
			if (!tasks) { return R"({"ok":false,"error":"no SKSE task interface"})"; }
			tasks->AddTask([promise]() {
				auto manager = FindSkyUIManager();
				if (!manager)
				{
					promise->set_value(R"({"ok":false,"error":"SkyUI's config manager is not running yet"})");
					return;
				}
				static const char* layoutNames[] = { "stock", "barzing", "mcm-unlocked", "unknown" };
				const ListLayout layout = DetectLayout(manager);
				json r{ { "ok", true }, { "layout", layoutNames[static_cast<int>(layout)] } };
				const auto configName = [](const RE::BSScript::Variable& a_v) {
					const auto obj = a_v.IsObject() ? a_v.GetObject() : nullptr;
					const auto name = obj ? obj->GetProperty("ModName") : nullptr;
					return name && name->IsString() ? std::string(name->GetString()) : std::string();
				};
				json list = json::array();
				if (layout == ListLayout::kStock || layout == ListLayout::kBarzing)
				{
					std::vector<std::string> arrays;
					if (layout == ListLayout::kStock) { arrays.push_back("_modConfigs"); }
					else
					{
						arrays.push_back("_MainMenu");
						for (int page = 1; page < 32 && detail::ScriptVar(manager, "_modConfigsP" + std::to_string(page)); ++page)
						{
							arrays.push_back("_modConfigsP" + std::to_string(page));
						}
					}
					for (const auto& a : arrays)
					{
						const auto var = detail::ScriptVar(manager, a);
						const auto arr = var && var->IsArray() ? var->GetArray() : nullptr;
						for (std::uint32_t i = 0; arr && i < arr->size(); ++i)
						{
							const auto& c = (*arr)[i];
							if (!c.IsObject() || !c.GetObject()) { continue; }  // an empty slot is an object-typed None that IsNoneObject does not report
							list.push_back(configName(c));
						}
					}
					r["count"] = list.size();
					r["names"] = list;
					promise->set_value(r.dump());
					return;
				}
				if (layout == ListLayout::kUnlocked)
				{
					// GetConfigCount, then every GetModIDFromConfigID(i) at once; answered when the last one returns
					const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
					VarArgs none;
					RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> onCount{ new detail::ResultFn([promise, r, vm](const RE::BSScript::Variable& a_count) mutable {
						const int count = a_count.IsInt() ? a_count.GetSInt() : 0;
						if (count <= 0)
						{
							r["count"] = 0;
							r["names"] = json::array();
							promise->set_value(r.dump());
							return;
						}
						struct Gather
						{
							std::mutex lock;
							std::vector<std::string> names;
							int left;
						};
						auto gather = std::make_shared<Gather>();
						gather->names.resize(static_cast<std::size_t>(count));
						gather->left = count;
						for (int i = 0; i < count; ++i)
						{
							VarArgs args;
							args.args.resize(1);
							args.args[0].SetSInt(i);
							RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> onId{ new detail::ResultFn([promise, r, gather, i](const RE::BSScript::Variable& a_id) mutable {
								std::scoped_lock l(gather->lock);
								gather->names[static_cast<std::size_t>(i)] = a_id.IsString() ? std::string(a_id.GetString()) : std::string();
								if (--gather->left == 0)
								{
									r["count"] = gather->names.size();
									r["names"] = gather->names;
									promise->set_value(r.dump());
								}
							}) };
							if (!vm->DispatchStaticCall("MCMUnlocked", "GetModIDFromConfigID", &args, onId))
							{
								std::scoped_lock l(gather->lock);
								if (--gather->left == 0)
								{
									r["count"] = gather->names.size();
									r["names"] = gather->names;
									promise->set_value(r.dump());
								}
							}
						}
					}) };
					if (!vm || !vm->DispatchStaticCall("MCMUnlocked", "GetConfigCount", &none, onCount))
					{
						promise->set_value(R"({"ok":false,"error":"MCMUnlocked.GetConfigCount could not be called"})");
					}
					return;
				}
				promise->set_value(R"({"ok":false,"layout":"unknown","error":"SkyUI's config manager keeps its list in no form AMF can read"})");
			});
			if (future.wait_for(std::chrono::seconds(8)) != std::future_status::ready) { return R"({"ok":false,"error":"no answer within 8 s"})"; }
			return future.get();
		}
		if (op == "switch")
		{
			// the settings page's three toggles, the same calls and the same save: name load | scripts | hideskyui, on true/false
			const std::string name = JsonStr(args, "name");
			const auto on = args.find("on");
			if (on == args.end() || !on->is_boolean() || (name != "load" && name != "scripts" && name != "hideskyui"))
			{
				return R"J({"ok":false,"error":"switch needs name (load | scripts | hideskyui) and on (true/false)"})J";
			}
			if (name == "scripts")
			{
				settings::Get().loadSkyUIScriptMenus = on->get<bool>();
				settings::Save();
				SetScriptsEnabled(on->get<bool>());
			}
			else if (name == "load")
			{
				settings::Get().loadMcmHelperConfigs = on->get<bool>();
				settings::Save();
				SetEnabled(on->get<bool>());
			}
			else
			{
				settings::Get().hideMcmInSkyUI = on->get<bool>();
				settings::Save();
				SetHideInSkyUI(on->get<bool>());
			}
			return json{ { "ok", true }, { "switch", name }, { "on", on->get<bool>() } }.dump();
		}
		if (op.empty() || op == "list")
		{
			std::scoped_lock lock(g_mutex);
			out["ok"] = true;
			out["enabled"] = settings::Get().loadMcmHelperConfigs;
			out["hideInSkyUI"] = settings::Get().hideMcmInSkyUI;
			out["hiddenInSkyUI"] = g_hiddenCount.load();
			out["skyuiScriptMenus"] = scripts::Count();
			out["skyuiScriptMenusHidden"] = scripts::Hidden();
			for (const auto& mod : g_mods)
			{
				json m{ { "mod", mod->modName }, { "entry", mod->entryName }, { "pages", mod->pages.size() },
					{ "controls", mod->controlCount }, { "phase2", mod->phase2Count }, { "script", mod->scriptState }, { "hideable", mod->hideable }, { "hiddenInSkyUI", mod->hiddenInSkyUI } };
				out["mods"].push_back(m);
			}
			for (const auto& [mod, reason] : g_skipped) { out["skipped"].push_back({ { "mod", mod }, { "reason", reason } }); }
			return out.dump();
		}

		const auto index = findMod();
		if (!index) { return json{ { "ok", false }, { "error", "no MCM mod '" + modArg + "'" } }.dump(); }

		if (op == "controls")
		{
			// every control's key (its id, or "#<page>.<index>" when it has none), so a driving script can address it
			json list = json::array();
			std::scoped_lock lock(g_mutex);
			for (const Page& page : g_mods[*index]->pages)
			{
				for (const Control& c : page.controls)
				{
					if (c.kind == Kind::kHeader || c.kind == Kind::kEmpty) { continue; }
					const auto it = g_mods[*index]->values.find(c.key);
					list.push_back({ { "key", c.key }, { "type", c.typeName }, { "source", c.sourceTypeName }, { "form", c.sourceForm },
						{ "property", c.propertyName }, { "action", c.action ? c.action->function : "" },
						{ "value", it != g_mods[*index]->values.end() ? it->second : "" } });
				}
			}
			return json{ { "ok", true }, { "controls", list } }.dump();
		}
		if (op == "refresh")
		{
			QueueRefresh(*index);  // re-read the mod's globals and script properties on the main thread
			return json{ { "ok", true }, { "queued", "refresh" } }.dump();
		}
		if (op == "script")
		{
			std::scoped_lock lock(g_mutex);
			return json{ { "ok", true }, { "mod", g_mods[*index]->modName }, { "script", g_mods[*index]->scriptState } }.dump();
		}

		const Control* control = nullptr;
		for (const Page& page : g_mods[*index]->pages)
		{
			for (const Control& c : page.controls)
			{
				if (!id.empty() && (c.id == id || c.key == id)) { control = &c; }
			}
		}
		if (!control) { return json{ { "ok", false }, { "error", "no control with id '" + id + "'" } }.dump(); }

		if (op == "get")
		{
			std::scoped_lock lock(g_mutex);
			const auto& values = g_mods[*index]->values;
			const auto it = values.find(control->key);
			return json{ { "ok", true }, { "id", id }, { "type", control->typeName }, { "source", control->sourceTypeName },
				{ "value", it != values.end() ? it->second : "" } }.dump();
		}
		if (op == "set" || op == "press")
		{
			if (control->source == Source::kPhase2 || (control->hasAction && !control->action))
			{
				return json{ { "ok", false }, { "error", "not drawable: " + (control->sourceTypeName.empty() ? std::string("an action type AMF does not run") : control->sourceTypeName) } }.dump();
			}
			if (op == "press" && !control->action) { return R"({"ok":false,"error":"press needs a control with an action"})"; }
			if (op == "set" && control->source == Source::kNone) { return R"({"ok":false,"error":"this control holds no value - use press"})"; }
			const auto value = args.find("value");
			if (op == "set" && value == args.end()) { return R"({"ok":false,"error":"set needs a value"})"; }
			std::string text;
			if (value != args.end()) { text = value->is_string() ? value->get<std::string>() : value->is_boolean() ? (value->get<bool>() ? "1" : "0") : value->dump(); }
			else
			{
				std::scoped_lock lock(g_mutex);
				const auto it = g_mods[*index]->values.find(control->key);
				text = it != g_mods[*index]->values.end() ? it->second : control->staticValue;
			}
			// With the mod's page not open, the tool plays a whole SkyUI visit: open, change, close.
			const bool pageOpen = g_openMod.load() == static_cast<int>(*index);
			if (pageOpen) { Apply(*index, *control, text); }
			else if (const auto tasks = SKSE::GetTaskInterface())
			{
				// the write waits for OnConfigOpen to FINISH - a mod's OnConfigOpen reloads its properties, and run after
				// the write it put the old value back (SkyUI's UnequipArmor, 2026-10-04)
				const std::size_t mod = *index;
				tasks->AddTask([mod, control, text]() {
					DispatchEvent(mod, "OnConfigOpen", nullptr, [mod, control, text]() { Apply(mod, *control, text, true); });
				});
			}
			return json{ { "ok", true }, { "id", id }, { "queued", text }, { "configCloseAfter", !pageOpen } }.dump();
		}
		return json{ { "ok", false }, { "error", "unknown op '" + op + "' (list, get, set, press, refresh, script)" } }.dump();
	}

	// ------------------------------------------------------------------------------------- remembered MCM settings

	std::vector<MemoryMenu> MemoryMenus()
	{
		std::vector<MemoryMenu> out;
		std::scoped_lock lock(g_mutex);
		for (const auto& mod : g_mods)
		{
			bool live = false;
			for (const Page& page : mod->pages)
			{
				for (const Control& c : page.controls) { live = live || IsLive(c.source); }
			}
			if (live) { out.push_back({ "mcmhelper|" + mod->modName, mod->entryName }); }
		}
		return out;
	}

	namespace
	{
		std::optional<std::size_t> MemoryModIndex(const std::string& a_key)
		{
			if (a_key.rfind("mcmhelper|", 0) != 0) { return std::nullopt; }
			const std::string name = a_key.substr(10);
			std::scoped_lock lock(g_mutex);
			for (std::size_t m = 0; m < g_mods.size(); ++m)
			{
				if (g_mods[m]->modName == name) { return m; }
			}
			return std::nullopt;
		}
	}

	void MemorySnapshot(const std::string& a_key, std::function<void(std::vector<std::pair<std::string, std::string>>)> a_done)
	{
		const auto index = MemoryModIndex(a_key);
		const auto tasks = SKSE::GetTaskInterface();
		if (!index || !tasks)
		{
			a_done({});
			return;
		}
		tasks->AddTask([mod = *index, a_done]() {
			std::vector<std::pair<std::string, std::string>> values;
			if (EnsureScript(mod))  // the forms and script of a game that is loaded
			{
				for (const Page& page : g_mods[mod]->pages)
				{
					for (const Control& c : page.controls)
					{
						if (!IsLive(c.source)) { continue; }
						if (auto v = ReadLive(mod, c)) { values.emplace_back(c.key, std::move(*v)); }
					}
				}
			}
			a_done(std::move(values));
		});
	}

	void MemoryRestore(const std::string& a_key, std::vector<std::pair<std::string, std::string>> a_values, std::function<void(int, int)> a_done)
	{
		const auto index = MemoryModIndex(a_key);
		const auto tasks = SKSE::GetTaskInterface();
		if (!index || !tasks)
		{
			a_done(0, static_cast<int>(a_values.size()));
			return;
		}
		tasks->AddTask([mod = *index, values = std::move(a_values), a_done]() {
			if (!EnsureScript(mod))
			{
				a_done(0, static_cast<int>(values.size()));
				return;
			}
			// the writes wait for OnConfigOpen to FINISH (a mod's OnConfigOpen reloads its properties - SkyUI's UnequipArmor)
			DispatchEvent(mod, "OnConfigOpen", nullptr, [mod, values, a_done]() {
				const auto tasks = SKSE::GetTaskInterface();
				if (!tasks)
				{
					a_done(0, static_cast<int>(values.size()));
					return;
				}
				tasks->AddTask([mod, values, a_done]() {
					int applied = 0;
					int missing = 0;
					g_memoryWriting = true;
					for (const auto& [key, value] : values)
					{
						const Control* control = nullptr;
						for (const Page& page : g_mods[mod]->pages)
						{
							for (const Control& c : page.controls)
							{
								if (c.key == key && IsLive(c.source)) { control = &c; }
							}
						}
						const auto now = control ? ReadLive(mod, *control) : std::nullopt;
						if (!control || !now)
						{
							++missing;
							continue;
						}
						if (*now == value) { continue; }
						Apply(mod, *control, value);  // queues the write, OnSettingChange and the action as one task
						++applied;
					}
					g_memoryWriting = false;
					// OnConfigClose after those tasks (the task queue is in order) - TrueHUD-style mods apply only there
					if (const auto later = SKSE::GetTaskInterface())
					{
						later->AddTask([mod, applied, missing, a_done]() {
							DispatchEvent(mod, "OnConfigClose", nullptr);
							a_done(applied, missing);
						});
					}
					else { a_done(applied, missing); }
				});
			});
		});
	}
}

// Phase 3 (McmScripts.cpp) uses these through McmShared.h.
namespace mcmloader::detail
{
	std::string Lower(std::string a_s) { return ::mcmloader::Lower(std::move(a_s)); }
	std::string Trim(const std::string& a_s) { return ::mcmloader::Trim(a_s); }
	std::string Narrow(std::wstring_view a_w) { return ::mcmloader::Narrow(a_w); }
	std::string StripTags(const std::string& a_s) { return ::mcmloader::StripTags(a_s); }
	std::string ReadableKey(const std::string& a_key) { return ::mcmloader::ReadableKey(a_key); }
	std::string Unescape(const std::string& a_s) { return ::mcmloader::Unescape(a_s); }
	std::string KeyName(std::int32_t a_code) { return ::mcmloader::KeyName(a_code); }
	Table LoadTranslations(const std::string& a_plugin) { return ::mcmloader::LoadTranslations(a_plugin); }
	std::string TextLanguage() { return ::mcmloader::TextLanguage(); }
	Table LoadNameTranslations(const std::string& a_plugin) { return ::mcmloader::LoadTranslations(a_plugin, true); }
	bool IsImported(const std::string& a_key)
	{
		std::scoped_lock lock(::mcmloader::g_importLock);
		::mcmloader::LoadImportLocked();
		const auto it = ::mcmloader::g_import.find(a_key);
		return it != ::mcmloader::g_import.end() ? it->second : settings::Get().importNewMcmMenus;
	}
	RE::BSTSmartPointer<RE::BSScript::Object> FindSkyUIManager() { return ::mcmloader::FindSkyUIManager(); }
}
