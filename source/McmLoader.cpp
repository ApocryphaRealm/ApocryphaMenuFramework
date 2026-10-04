#include "McmLoader.h"

#include "Keyboard.h"
#include "Input.h"
#include "PreciseSlider.h"
#include "Registry.h"
#include "Settings.h"
#include "Strings.h"
#include "utils/Logger.h"
#include "utils/ToggleSwitch.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>

// See McmLoader.h for what this is and the plan it follows. Everything here is EXPERIMENTAL.

namespace mcmloader
{
	namespace
	{
		using json = nlohmann::json;
		namespace fs = std::filesystem;

		enum class Kind { kEmpty, kHeader, kText, kToggle, kHiddenToggle, kSlider, kStepper, kMenu, kEnum, kColor, kKeymap, kInput, kUnknown };
		enum class Source { kNone, kBool, kInt, kFloat, kString, kPhase2 };
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
			int controlCount = 0;
			int phase2Count = 0;
			// Main-thread only (SKSE tasks): the config script, resolved lazily after a game is loaded.
			RE::BSTSmartPointer<RE::BSScript::Object> script;
			bool scriptTried = false;
			std::string scriptState = "not looked up yet";
		};

		std::mutex g_mutex;                      // guards every Mod's values/script fields read off the main thread
		std::vector<std::unique_ptr<Mod>> g_mods; // never shrinks after Load, so indices captured by pages stay valid
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

		Table LoadTranslations(const std::string& a_modName)
		{
			Table table;
			std::string language = Lower(strings::GameLanguageSetting());
			if (language.empty()) { language = "english"; }
			for (const std::string& lang : { language, std::string("english") })
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
			if (a_raw.empty() || a_raw[0] != '$') { return StripTags(a_raw); }
			if (g_table)
			{
				if (const auto it = g_table->find(a_raw); it != g_table->end()) { return StripTags(it->second); }
			}
			return a_raw.substr(1);
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
			return Source::kPhase2;  // GlobalValue, PropertyValue* - need the Papyrus/form layer (phase 2)
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

			if (const auto vo = a_c.find("valueOptions"); vo != a_c.end() && vo->is_object())
			{
				c.sourceTypeName = JsonStr(*vo, "sourceType");
				c.source = SourceOf(c.sourceTypeName);
				if (vo->contains("sourceForm") && c.source != Source::kPhase2 && c.source != Source::kNone)
				{
					c.source = Source::kPhase2;  // a ModSetting tied to a form is not a thing MCM Helper documents; stay safe
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
		RE::BSTSmartPointer<RE::BSScript::Object> FindConfigScript(const std::string& a_modName)
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
			script = FindConfigScript(modName);
			std::scoped_lock lock(g_mutex);
			g_mods[a_mod]->script = script;
			g_mods[a_mod]->scriptTried = true;
			g_mods[a_mod]->scriptState = script ? "resolved" : "not found (no game loaded yet, or the mod's quest has not started)";
			return script;
		}

		// Main thread: one SkyUI/MCM Helper event on the mod's config script. OnConfigOpen / OnConfigClose carry no
		// argument; OnSettingChange carries the setting id.
		bool DispatchEvent(std::size_t a_mod, const char* a_event, const std::string* a_arg)
		{
			const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
			auto script = EnsureScript(a_mod);
			const std::string modName = a_mod < g_mods.size() ? g_mods[a_mod]->modName : std::string("?");
			if (!vm || !script)
			{
				logger::info("MCM loader: {} {} not sent - no config script yet (the mod reads its settings when it next loads them)",
					modName, a_event);
				return false;
			}
			RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> none;
			std::unique_ptr<RE::BSScript::IFunctionArguments> args{ a_arg ? RE::MakeFunctionArguments(std::string(*a_arg)) : RE::MakeFunctionArguments() };
			const bool sent = vm->DispatchMethodCall(script, a_event, args.get(), none);
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

		// Any thread: record the new value now (the page shows it at once), then hand the write to the main thread.
		// a_closeAfter: also send OnConfigClose once the value is stored - for a change made with no page open (the
		// DevBench tool), so a mod that applies its settings only when its menu closes (TrueHUD, True Directional
		// Movement, Precision: "Event OnConfigClose() native") sees it, as it would after SkyUI's menu.
		void Apply(std::size_t a_mod, const Control& a_control, const std::string& a_value, bool a_closeAfter = false)
		{
			std::string modName;
			{
				std::scoped_lock lock(g_mutex);
				if (a_mod >= g_mods.size()) { return; }
				g_mods[a_mod]->values[a_control.id] = a_value;
				modName = g_mods[a_mod]->modName;
			}
			const auto tasks = SKSE::GetTaskInterface();
			if (!tasks)
			{
				logger::error("MCM loader: no SKSE task interface - {}:{} = {} kept in the menu only", modName, a_control.id, a_value);
				return;
			}
			const Source source = a_control.source;
			const std::string id = a_control.id;
			logger::debug("MCM loader: {} {} -> {} (queued)", modName, id, a_value);

			tasks->AddTask([a_mod, modName, id, source, a_value, a_closeAfter]() {
				const auto vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
				if (!vm)
				{
					logger::warn("MCM loader: no Papyrus VM - {} {} not written", modName, id);
					return;
				}
				// 2. once MCM Helper's store holds the value: OnSettingChange, as MCM Helper's menu sends it; then, for a
				//    change made with no page open, OnConfigClose, which is where several mods apply their settings
				auto notify = [a_mod, modName, id, a_closeAfter]() {
					logger::info("MCM loader: {} {} saved by MCM Helper", modName, id);
					DispatchEvent(a_mod, "OnSettingChange", &id);
					if (a_closeAfter) { DispatchEvent(a_mod, "OnConfigClose", nullptr); }
				};

				// 1. MCM Helper's own store and user INI (MCM.SetModSetting*)
				const char* fn = source == Source::kBool ? "SetModSettingBool" : source == Source::kInt ? "SetModSettingInt" :
				                 source == Source::kFloat ? "SetModSettingFloat" : "SetModSettingString";
				RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> then{ new Then(std::move(notify)) };
				std::unique_ptr<RE::BSScript::IFunctionArguments> args;
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

		void Tooltip(const Control& a_c, const std::string& a_value)
		{
			if (a_c.help.empty() || !ImGui::IsItemHovered()) { return; }
			std::string help = Translate(a_c.help);
			if (const auto at = help.find("{value}"); at != std::string::npos) { help.replace(at, 7, a_value); }
			ImGui::SetTooltip("%s", help.c_str());
		}

		void DrawControl(std::size_t a_mod, const Control& a_c, const std::string& a_value)
		{
			const std::string label = Translate(a_c.text);
			const std::string imguiId = label + "##" + a_c.id + std::to_string(reinterpret_cast<std::uintptr_t>(&a_c));

			const bool holdsValue = a_c.kind == Kind::kToggle || a_c.kind == Kind::kSlider || a_c.kind == Kind::kStepper ||
			                        a_c.kind == Kind::kMenu || a_c.kind == Kind::kEnum || a_c.kind == Kind::kColor ||
			                        a_c.kind == Kind::kKeymap || a_c.kind == Kind::kInput;
			if (a_c.source == Source::kPhase2 || a_c.hasAction || (holdsValue && a_c.source == Source::kNone))
			{
				ImGui::BeginDisabled();
				ImGui::TextUnformatted(label.c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("(%s - set it in SkyUI's MCM for now; AMF phase 2)",
					a_c.hasAction ? "a script action" : a_c.sourceTypeName.empty() ? "no value source" : a_c.sourceTypeName.c_str());
				ImGui::EndDisabled();
				return;
			}

			switch (a_c.kind)
			{
			case Kind::kEmpty:
				ImGui::Spacing();
				break;
			case Kind::kHeader:
				ImGui::Spacing();
				ImGui::SeparatorText(label.empty() ? " " : label.c_str());
				break;
			case Kind::kText:
				ImGui::TextUnformatted(label.c_str());
				if (const std::string shown = a_c.source == Source::kString ? a_value : Translate(a_c.staticValue); !shown.empty())
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
				float v = a_c.source == Source::kInt ? static_cast<float>(ParseInt(a_value)) : ParseFloat(a_value);
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
						Apply(a_mod, a_c, a_c.source == Source::kInt ? std::to_string(static_cast<long long>(std::lround(v))) : FormatFloat(v));
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
				if (ImGui::BeginCombo(imguiId.c_str(), current.c_str()))
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
				if (ImGui::BeginCombo(imguiId.c_str(), current.c_str()))
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
				ImGui::InputText(imguiId.c_str(), buffer, sizeof(buffer));
				keyboard::NoteTextField(ImGui::GetItemID());
				if (ImGui::IsItemDeactivatedAfterEdit() && a_value != buffer) { Apply(a_mod, a_c, buffer); }
				Tooltip(a_c, a_value);
				break;
			}
			case Kind::kColor:
			{
				const auto rgb = static_cast<std::uint32_t>(ParseInt(a_value));
				float col[3]{ ((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f };
				if (ImGui::ColorEdit3(imguiId.c_str(), col))
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
				const std::string button = (waiting ? std::string("Press a key...") : KeyName(static_cast<std::int32_t>(ParseInt(a_value)))) + "##key" + a_c.id;
				if (ImGui::Button(button.c_str()) && !waiting)
				{
					g_capturing = std::make_pair(a_mod, a_c.id);
					input::ArmKeyCapture();
				}
				ImGui::SameLine();
				if (ImGui::Button(("Clear##clr" + a_c.id).c_str())) { Apply(a_mod, a_c, "-1"); }
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
				ImGui::TextDisabled("%s (MCM type \"%s\" is not drawn yet)", label.c_str(), a_c.typeName.c_str());
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
			if (open != static_cast<int>(a_mod))
			{
				if (open >= 0) { QueueEvent(static_cast<std::size_t>(open), "OnConfigClose"); }
				QueueEvent(a_mod, "OnConfigOpen");
				g_openMod.store(static_cast<int>(a_mod));
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
				values = mod->values;  // a copy: the page draws from a stable view while a set lands from another thread
			}
			const Page& page = mod->pages[a_page];
			g_table = &mod->translations;

			ImGui::TextDisabled("Read from %s's MCM Helper files (experimental). Changes go through MCM Helper, as in its own menu.",
				mod->modName.c_str());
			if (page.customContent)
			{
				ImGui::TextDisabled("This MCM page is a custom picture or SWF - not drawable here.");
				return;
			}

			// The page's groups, from its toggles' groupControl (MCM Helper keeps them per page as well).
			std::map<int, bool> groups;
			for (const Control& c : page.controls)
			{
				if (c.groupControl > 0)
				{
					const auto it = values.find(c.id);
					groups[c.groupControl] = it != values.end() && ParseBool(it->second);
				}
			}

			for (const Control& c : page.controls)
			{
				const bool active = !c.cond || Eval(*c.cond, groups);
				if (!active && c.behavior != Behavior::kDisable) { continue; }
				const auto it = values.find(c.id);
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

		void LoadOne(const fs::path& a_folder)
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
						++mod->controlCount;
						if (control.source == Source::kPhase2 || control.hasAction) { ++mod->phase2Count; }
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

			mod->translations = LoadTranslations(modName);
			mod->defaults = ReadIni(a_folder / "settings.ini");
			mod->values = mod->defaults;
			const fs::path userIni = fs::path("Data/MCM/Settings") / (modName + ".ini");
			const auto user = ReadIni(userIni);
			for (const auto& [k, v] : user) { mod->values[k] = v; }

			logger::debug("MCM loader: {} - {} page(s), {} control(s) ({} phase 2), {} default(s), {} user value(s)",
				modName, mod->pages.size(), mod->controlCount, mod->phase2Count, mod->defaults.size(), user.size());

			std::scoped_lock lock(g_mutex);
			g_mods.push_back(std::move(mod));
		}

		void RegisterPages()
		{
			std::size_t pageCount = 0;
			for (std::size_t m = 0; m < g_mods.size(); ++m)
			{
				Mod& mod = *g_mods[m];
				// " (MCM)" keeps it apart from a mod's own AMF page of the same name - the registry merges equal names
				// into one entry's tabs, which would mix our pages into theirs.
				g_table = &mod.translations;  // entry and tab names are fixed at registration, so translate them now
				mod.entryName = Translate(mod.displayName);
				if (mod.entryName.empty()) { mod.entryName = mod.modName; }
				mod.entryName += " (MCM)";
				std::set<std::string> used;
				for (std::size_t p = 0; p < mod.pages.size(); ++p)
				{
					std::string name = mod.pages[p].name.empty() ? std::string("Settings") : Translate(mod.pages[p].name);
					if (name.empty()) { name = "Settings"; }
					for (int n = 2; used.contains(name); ++n) { name = name + " (" + std::to_string(n) + ")"; }
					used.insert(name);
					if (registry::RegisterFn(mod.entryName.c_str(), name.c_str(), [m, p]() { DrawPage(m, p); })) { ++pageCount; }
				}
			}
			logger::info("MCM loader: {} MCM Helper mod(s) registered as AMF entries ({} page(s)); {} skipped",
				g_mods.size(), pageCount, g_skipped.size());
		}
	}

	void Load()
	{
		if (g_loaded.exchange(true)) { return; }
		if (!settings::Get().loadMcmHelperConfigs)
		{
			logger::info("MCM loader: off ([MCM] bLoadMcmHelperConfigs=0)");
			return;
		}

		const fs::path root("Data/MCM/Config");
		std::error_code ec;
		if (!fs::is_directory(root, ec))
		{
			logger::info("MCM loader: no Data/MCM/Config folder - no MCM Helper mods to read");
			return;
		}

		logger::info("MCM loader (EXPERIMENTAL, exp/mcm-loader): reading MCM Helper configs from {}", root.string());
		for (const auto& entry : fs::directory_iterator(root, ec))
		{
			if (!entry.is_directory(ec)) { continue; }
			if (!fs::exists(entry.path() / "config.json", ec)) { continue; }
			try { LoadOne(entry.path()); }
			catch (const std::exception& e) { Skip(entry.path().filename().string(), std::string("unexpected error: ") + e.what()); }
		}
		RegisterPages();
		g_table = nullptr;  // each page sets its own mod's table when it draws
	}

	void Frame()
	{
		const int open = g_openMod.load();
		if (open >= 0 && g_lastDrawFrame < ImGui::GetFrameCount() - 1)
		{
			QueueEvent(static_cast<std::size_t>(open), "OnConfigClose");
			g_openMod.store(-1);
		}
	}

	void OnGameLoaded()
	{
		std::scoped_lock lock(g_mutex);
		for (auto& mod : g_mods)
		{
			mod->script.reset();
			mod->scriptTried = false;
			mod->scriptState = "not looked up yet (a game was loaded)";
		}
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
			for (std::size_t i = 0; i < g_mods.size(); ++i)
			{
				if (Lower(g_mods[i]->modName) == Lower(modArg) || g_mods[i]->entryName == modArg) { return i; }
			}
			return std::nullopt;
		};

		json out;
		if (op.empty() || op == "list")
		{
			std::scoped_lock lock(g_mutex);
			out["ok"] = true;
			out["enabled"] = settings::Get().loadMcmHelperConfigs;
			for (const auto& mod : g_mods)
			{
				json m{ { "mod", mod->modName }, { "entry", mod->entryName }, { "pages", mod->pages.size() },
					{ "controls", mod->controlCount }, { "phase2", mod->phase2Count }, { "script", mod->scriptState } };
				out["mods"].push_back(m);
			}
			for (const auto& [mod, reason] : g_skipped) { out["skipped"].push_back({ { "mod", mod }, { "reason", reason } }); }
			return out.dump();
		}

		const auto index = findMod();
		if (!index) { return json{ { "ok", false }, { "error", "no MCM mod '" + modArg + "'" } }.dump(); }

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
				if (c.id == id && !id.empty()) { control = &c; }
			}
		}
		if (!control) { return json{ { "ok", false }, { "error", "no control with id '" + id + "'" } }.dump(); }

		if (op == "get")
		{
			std::scoped_lock lock(g_mutex);
			const auto& values = g_mods[*index]->values;
			const auto it = values.find(id);
			return json{ { "ok", true }, { "id", id }, { "type", control->typeName }, { "source", control->sourceTypeName },
				{ "value", it != values.end() ? it->second : "" } }.dump();
		}
		if (op == "set")
		{
			if (control->source == Source::kPhase2 || control->source == Source::kNone || control->hasAction)
			{
				return json{ { "ok", false }, { "error", "phase 1 sets ModSetting values only; this is " + control->sourceTypeName } }.dump();
			}
			const auto value = args.find("value");
			if (value == args.end()) { return R"({"ok":false,"error":"set needs a value"})"; }
			const std::string text = value->is_string() ? value->get<std::string>() : value->is_boolean() ? (value->get<bool>() ? "1" : "0") : value->dump();
			// With the mod's page not open, the tool plays a whole SkyUI visit: open, change, close.
			const bool pageOpen = g_openMod.load() == static_cast<int>(*index);
			if (!pageOpen) { QueueEvent(*index, "OnConfigOpen"); }
			Apply(*index, *control, text, !pageOpen);
			return json{ { "ok", true }, { "id", id }, { "queued", text }, { "configCloseAfter", !pageOpen } }.dump();
		}
		return json{ { "ok", false }, { "error", "unknown op '" + op + "' (list, get, set, script)" } }.dump();
	}
}
