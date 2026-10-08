#include "PrismaRedux.h"

#include "AmfIcons.h"
#include "HelpBar.h"
#include "Input.h"
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
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>

// See PrismaRedux.h for what this is and the plan it follows.

namespace prisma
{
	namespace
	{
		using json = nlohmann::json;
		namespace fs = std::filesystem;
		using mcmloader::detail::Lower;
		using mcmloader::detail::Trim;

		constexpr const char* kConfigDir = "Data/SKSE/Plugins/PrismaMCMRedux/Configs";
		constexpr const char* kSettingsDir = "Data/PrismaMCMRedux/Settings";
		constexpr const char* kCoreIni = "Data/PrismaMCMRedux/PrismaCore.ini";
		// The player's own Redux key while AMF holds it switched off ("AMF only"), so it can be put back.
		constexpr const char* kHeldKeyPath = "Data/SKSE/Plugins/ApocryphaMenuFramework/PrismaHotkey.txt";
		constexpr const char* kSuffix = " (Prisma)";

		enum class Type { kHeader, kSlider, kToggle, kDropdown, kKeybind, kAction, kUnknown };

		struct Setting
		{
			Type type = Type::kUnknown;
			std::string typeName;
			std::string id;
			std::string label;
			std::string page;
			std::string text;          // an action button's caption
			std::string defaultValue;  // the JSON's "value", as Redux's page would write it
			float min = 0.0f;
			float max = 1.0f;
			float step = 1.0f;
			int decimals = 0;          // the step's: 0.1 -> 1
			std::vector<std::string> options;
			std::string condId;        // "condition": { "id", "value" } - drawn greyed out unless that setting holds the value
			std::string condValue;
		};

		struct Config
		{
			std::string modId;      // the file name: the INI's name and what Prisma.GetX(ModID, ...) asks for
			std::string modName;    // shown, and the events' name
			std::string author;
			std::string entry;
			std::vector<std::string> pages;
			std::vector<Setting> settings;
			bool hasAbout = false;
			std::string aboutText;
			std::string aboutVersion;
			std::vector<std::pair<std::string, std::string>> links;  // label, url
			std::map<std::string, std::string> values;   // lower(id) -> value (the JSON's, with the INI's over them)
			std::vector<std::string> pageNames;           // as registered
			bool duplicate = false;
			bool unsaved = false;   // changed and not yet written to the INI
			bool changed = false;   // changed since the page was opened: Prisma_OnSettingsApplied when it is left
			double writeDue = 0.0;
		};

		std::mutex g_lock;
		std::vector<std::unique_ptr<Config>> g_configs;
		std::atomic<bool> g_loaded{ false };
		bool g_reduxLoaded = false;

		std::string EventName(const Config& a_c) { return a_c.modName.empty() ? a_c.modId : a_c.modName; }

		// ------------------------------------------------------------------------------------- values as text

		// A number the way Redux's page writes it: its slider hands over the input's value - "4", "1.5", never "1.500000".
		std::string FormatNumber(double a_v, int a_decimals)
		{
			char buffer[64];
			std::snprintf(buffer, sizeof(buffer), "%.*f", std::clamp(a_decimals, 0, 6), a_v);
			std::string s = buffer;
			if (s.find('.') != std::string::npos)
			{
				while (!s.empty() && s.back() == '0') { s.pop_back(); }
				if (!s.empty() && s.back() == '.') { s.pop_back(); }
			}
			if (s == "-0") { s = "0"; }
			return s;
		}

		int DecimalsOf(double a_step)
		{
			for (int d = 0; d <= 6; ++d)
			{
				const double scaled = a_step * std::pow(10.0, d);
				if (std::fabs(scaled - std::round(scaled)) < 1e-6) { return d; }
			}
			return 6;
		}

		std::string JsonText(const json& a_v, int a_decimals)
		{
			if (a_v.is_boolean()) { return a_v.get<bool>() ? "true" : "false"; }
			if (a_v.is_number_integer()) { return std::to_string(a_v.get<long long>()); }
			if (a_v.is_number()) { return FormatNumber(a_v.get<double>(), std::max(a_decimals, 6)); }
			if (a_v.is_string()) { return a_v.get<std::string>(); }
			return {};
		}

		bool ParseBool(const std::string& a_v)
		{
			const std::string v = Lower(Trim(a_v));
			return v == "true" || v == "1" || v == "yes" || v == "on";
		}

		double ParseNumber(const std::string& a_v, double a_fallback = 0.0)
		{
			try { return std::stod(Trim(a_v)); }
			catch (...) { return a_fallback; }
		}

		// Redux's own condition test is String(value) === String(condition.value); numbers are compared as numbers here
		// too, because the INI can hold "1.000000" where the JSON says 1.
		bool SameValue(const std::string& a_a, const std::string& a_b)
		{
			if (a_a == a_b) { return true; }
			const std::string a = Lower(Trim(a_a));
			const std::string b = Lower(Trim(a_b));
			if (a == b) { return true; }
			char* endA = nullptr;
			char* endB = nullptr;
			const double na = std::strtod(a.c_str(), &endA);
			const double nb = std::strtod(b.c_str(), &endB);
			return !a.empty() && !b.empty() && *endA == '\0' && *endB == '\0' && std::fabs(na - nb) < 1e-6;
		}

		// ------------------------------------------------------------------------------------------ INI files

		std::string StripBom(std::string a_line)
		{
			if (a_line.size() >= 3 && static_cast<unsigned char>(a_line[0]) == 0xEF && static_cast<unsigned char>(a_line[1]) == 0xBB &&
				static_cast<unsigned char>(a_line[2]) == 0xBF)
			{
				a_line.erase(0, 3);
			}
			return a_line;
		}

		std::vector<std::string> ReadLines(const fs::path& a_path, bool* a_bom = nullptr)
		{
			std::vector<std::string> lines;
			std::ifstream in(a_path, std::ios::binary);
			std::string line;
			bool first = true;
			while (std::getline(in, line))
			{
				if (!line.empty() && line.back() == '\r') { line.pop_back(); }
				if (first)
				{
					const std::string stripped = StripBom(line);
					if (a_bom) { *a_bom = stripped.size() != line.size(); }
					line = stripped;
					first = false;
				}
				lines.push_back(line);
			}
			return lines;
		}

		// lower(key) -> value in one section
		std::map<std::string, std::string> ReadSection(const fs::path& a_path, const std::string& a_section)
		{
			std::map<std::string, std::string> out;
			std::string section;
			for (const std::string& raw : ReadLines(a_path))
			{
				const std::string line = Trim(raw);
				if (line.empty() || line[0] == ';' || line[0] == '#') { continue; }
				if (line.front() == '[' && line.back() == ']')
				{
					section = Lower(Trim(line.substr(1, line.size() - 2)));
					continue;
				}
				if (section != a_section) { continue; }
				const auto eq = line.find('=');
				if (eq == std::string::npos) { continue; }
				out[Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
			}
			return out;
		}

		// Sets keys in one section, keeping every other line, a key's own spelling where it is already there, and the file's
		// byte-order mark (Redux writes one; a new file gets one too).
		bool WriteSection(const fs::path& a_path, const std::string& a_section, const std::vector<std::pair<std::string, std::string>>& a_values)
		{
			bool bom = true;
			std::error_code ec;
			const bool existed = fs::exists(a_path, ec);
			std::vector<std::string> lines = existed ? ReadLines(a_path, &bom) : std::vector<std::string>{};

			std::map<std::string, std::size_t> pending;   // lower(key) -> index in a_values
			for (std::size_t i = 0; i < a_values.size(); ++i) { pending[Lower(a_values[i].first)] = i; }

			std::string section;
			std::optional<std::size_t> sectionEnd;   // the line after the section's last key line
			bool sectionSeen = false;
			for (std::size_t i = 0; i < lines.size(); ++i)
			{
				const std::string line = Trim(lines[i]);
				if (!line.empty() && line.front() == '[' && line.back() == ']')
				{
					section = Lower(Trim(line.substr(1, line.size() - 2)));
					if (section == a_section) { sectionSeen = true; sectionEnd = i + 1; }
					continue;
				}
				if (section != a_section) { continue; }
				const auto eq = line.find('=');
				if (eq == std::string::npos || line[0] == ';' || line[0] == '#')
				{
					continue;
				}
				sectionEnd = i + 1;
				const std::string key = Trim(line.substr(0, eq));
				if (const auto it = pending.find(Lower(key)); it != pending.end())
				{
					lines[i] = key + " = " + a_values[it->second].second;
					pending.erase(it);
				}
			}
			if (!pending.empty())
			{
				std::vector<std::string> add;
				for (const auto& [lower, index] : pending) { add.push_back(a_values[index].first + " = " + a_values[index].second); }
				std::sort(add.begin(), add.end());   // map order is lower-case order; keep it stable either way
				if (!sectionSeen)
				{
					if (!lines.empty() && !Trim(lines.back()).empty()) { lines.emplace_back(); }
					lines.push_back("[" + std::string(a_section == "settings" ? "Settings" : a_section == "core" ? "Core" : a_section) + "]");
					sectionEnd = lines.size();
				}
				lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*sectionEnd), add.begin(), add.end());
			}

			fs::create_directories(a_path.parent_path(), ec);
			std::ofstream out(a_path, std::ios::binary | std::ios::trunc);
			if (!out) { return false; }
			if (bom) { out << "\xEF\xBB\xBF"; }
			for (const auto& line : lines) { out << line << "\r\n"; }
			return static_cast<bool>(out);
		}

		fs::path SettingsPath(const std::string& a_modId) { return fs::path(kSettingsDir) / (a_modId + ".ini"); }

		// ------------------------------------------------------------------------------------------ loading

		Type TypeOf(const std::string& a_name)
		{
			const std::string n = Lower(a_name);
			if (n == "header") { return Type::kHeader; }
			if (n == "slider") { return Type::kSlider; }
			if (n == "toggle") { return Type::kToggle; }
			if (n == "dropdown") { return Type::kDropdown; }
			if (n == "keybind") { return Type::kKeybind; }
			if (n == "action") { return Type::kAction; }
			return Type::kUnknown;
		}

		std::string Str(const json& a_j, const char* a_key)
		{
			const auto it = a_j.find(a_key);
			return it != a_j.end() && it->is_string() ? it->get<std::string>() : std::string{};
		}

		float Num(const json& a_j, const char* a_key, float a_fallback)
		{
			const auto it = a_j.find(a_key);
			if (it == a_j.end()) { return a_fallback; }
			if (it->is_number()) { return it->get<float>(); }
			if (it->is_string()) { return static_cast<float>(ParseNumber(it->get<std::string>(), a_fallback)); }
			return a_fallback;
		}

		std::unique_ptr<Config> LoadOne(const fs::path& a_file)
		{
			std::ifstream in(a_file, std::ios::binary);
			std::stringstream text;
			text << in.rdbuf();
			const json j = json::parse(StripBom(text.str()), nullptr, false, true);
			if (j.is_discarded() || !j.is_object())
			{
				logger::warn("prisma: {} is not valid JSON - skipped", a_file.filename().string());
				return nullptr;
			}
			auto config = std::make_unique<Config>();
			config->modId = a_file.stem().string();
			config->modName = Trim(Str(j, "modName"));
			config->author = Str(j, "author");
			if (const auto pages = j.find("pages"); pages != j.end() && pages->is_array())
			{
				for (const auto& p : *pages)
				{
					if (p.is_string() && !Trim(p.get<std::string>()).empty()) { config->pages.push_back(p.get<std::string>()); }
				}
			}
			if (const auto about = j.find("about"); about != j.end() && about->is_object())
			{
				config->hasAbout = true;
				config->aboutText = Str(*about, "description");
				config->aboutVersion = Str(*about, "version");
				if (const auto links = about->find("links"); links != about->end() && links->is_array())
				{
					for (const auto& l : *links)
					{
						if (l.is_object()) { config->links.emplace_back(Str(l, "label"), Str(l, "url")); }
					}
				}
			}
			if (const auto settings = j.find("settings"); settings != j.end() && settings->is_array())
			{
				for (const auto& s : *settings)
				{
					if (!s.is_object()) { continue; }
					Setting set;
					set.typeName = Str(s, "type");
					set.type = TypeOf(set.typeName);
					set.id = Str(s, "id");
					set.label = Str(s, "label");
					set.page = Str(s, "page");
					set.text = Str(s, "text");
					set.min = Num(s, "min", 0.0f);
					set.max = Num(s, "max", 1.0f);
					set.step = Num(s, "step", 1.0f);
					if (!(set.step > 0.0f)) { set.step = 1.0f; }
					if (set.max < set.min) { std::swap(set.min, set.max); }
					set.decimals = DecimalsOf(set.step);
					if (const auto opts = s.find("options"); opts != s.end() && opts->is_array())
					{
						for (const auto& o : *opts) { set.options.push_back(JsonText(o, 6)); }
					}
					if (const auto v = s.find("value"); v != s.end())
					{
						set.defaultValue = set.type == Type::kSlider && v->is_number() ? FormatNumber(v->get<double>(), set.decimals) : JsonText(*v, set.decimals);
					}
					if (const auto cond = s.find("condition"); cond != s.end() && cond->is_object())
					{
						set.condId = Str(*cond, "id");
						if (const auto cv = cond->find("value"); cv != cond->end()) { set.condValue = JsonText(*cv, 6); }
					}
					// a setting with no page goes on the first one, as Redux's page shows it with no tab of its own
					if (set.page.empty() && !config->pages.empty()) { set.page = config->pages.front(); }
					if (!set.page.empty() && std::find(config->pages.begin(), config->pages.end(), set.page) == config->pages.end())
					{
						config->pages.push_back(set.page);
					}
					config->settings.push_back(std::move(set));
				}
			}
			if (config->settings.empty() && !config->hasAbout)
			{
				logger::info("prisma: {} has no settings - skipped", a_file.filename().string());
				return nullptr;
			}
			// the values: the JSON's, with the player's INI over them (Redux's page shows savedValue ?? value)
			for (const auto& s : config->settings)
			{
				if (!s.id.empty() && s.type != Type::kHeader && s.type != Type::kAction) { config->values[Lower(s.id)] = s.defaultValue; }
			}
			for (const auto& [key, value] : ReadSection(SettingsPath(config->modId), "settings"))
			{
				if (config->values.contains(key)) { config->values[key] = value; }
			}
			return config;
		}

		bool IsImported(const Config& a_c) { return mcmloader::detail::IsImported("prisma|" + a_c.modId); }

		void ApplyVisibilityLocked()
		{
			const bool on = g_reduxLoaded && settings::Get().prismaControl != 2;
			for (const auto& c : g_configs)
			{
				const bool show = on && !c->duplicate && IsImported(*c);
				for (const auto& page : c->pageNames) { registry::SetPageVisible(c->entry.c_str(), page.c_str(), show); }
			}
		}

		// ------------------------------------------------------------------------------------------ changes

		void SendEvent(std::string a_event, std::string a_arg)
		{
			if (const auto tasks = SKSE::GetTaskInterface())
			{
				tasks->AddTask([a_event = std::move(a_event), a_arg = std::move(a_arg)]() {
					SKSE::ModCallbackEvent event{ a_event.c_str(), a_arg.c_str(), 0.0f, nullptr };
					if (const auto source = SKSE::GetModCallbackEventSource())
					{
						source->SendEvent(&event);
						logger::info("prisma: sent {} \"{}\"", a_event, a_arg);
					}
				});
			}
		}

		// Locked. The INI gets the changed values (a_all: every value, the way Redux's Apply writes them all).
		void WriteLocked(Config& a_c, bool a_all)
		{
			std::vector<std::pair<std::string, std::string>> out;
			for (const auto& s : a_c.settings)
			{
				if (s.id.empty() || s.type == Type::kHeader || s.type == Type::kAction) { continue; }
				const auto it = a_c.values.find(Lower(s.id));
				if (it != a_c.values.end()) { out.emplace_back(s.id, it->second); }
			}
			if (!a_all && !a_c.unsaved) { return; }
			const bool ok = WriteSection(SettingsPath(a_c.modId), "settings", out);
			a_c.unsaved = false;
			logger::info("prisma: {} - {} value(s) written to {}{}", a_c.modId, out.size(), SettingsPath(a_c.modId).generic_string(), ok ? "" : " (FAILED)");
		}

		// Any thread. a_index into g_configs.
		void SetValue(std::size_t a_index, const std::string& a_id, const std::string& a_value)
		{
			std::scoped_lock lock(g_lock);
			if (a_index >= g_configs.size()) { return; }
			Config& c = *g_configs[a_index];
			const std::string key = Lower(a_id);
			if (!c.values.contains(key)) { return; }
			if (c.values[key] == a_value) { return; }
			c.values[key] = a_value;
			c.unsaved = true;
			c.changed = true;
			c.writeDue = ImGui::GetCurrentContext() ? ImGui::GetTime() + 0.4 : 0.0;   // a slider drag writes once it rests
			logger::info("prisma: {} {} -> {}", c.modId, a_id, a_value);
		}

		// Locked: the page was left (or the menu closed) - Redux's Apply: every value written, then the mod is told.
		void ApplyLocked(Config& a_c)
		{
			if (!a_c.changed && !a_c.unsaved) { return; }
			WriteLocked(a_c, true);
			a_c.changed = false;
			SendEvent("Prisma_OnSettingsApplied", EventName(a_c));
		}

		void RunAction(std::size_t a_index, const std::string& a_id)
		{
			std::string name;
			{
				std::scoped_lock lock(g_lock);
				if (a_index >= g_configs.size()) { return; }
				name = EventName(*g_configs[a_index]);
			}
			SendEvent("Prisma_OnAction_" + name, a_id);
		}

		// ------------------------------------------------------------------------------------------ drawing

		std::optional<std::pair<std::size_t, std::string>> g_capturing;  // render thread: (config, id) awaiting a key
		int g_open = -1;            // render thread: the config whose page was drawn last frame
		int g_lastDrawFrame = -10;

		void NoteDrawn(std::size_t a_index)
		{
			if (g_open >= 0 && g_open != static_cast<int>(a_index))
			{
				std::scoped_lock lock(g_lock);
				if (static_cast<std::size_t>(g_open) < g_configs.size()) { ApplyLocked(*g_configs[g_open]); }
			}
			if (g_open != static_cast<int>(a_index))
			{
				// opening: the INI as it is now (Redux's own window may have changed it while both are in use)
				std::scoped_lock lock(g_lock);
				Config& c = *g_configs[a_index];
				if (!c.unsaved)
				{
					for (const auto& [key, value] : ReadSection(SettingsPath(c.modId), "settings"))
					{
						if (c.values.contains(key)) { c.values[key] = value; }
					}
				}
			}
			g_open = static_cast<int>(a_index);
			g_lastDrawFrame = ImGui::GetFrameCount();
		}

		void Help(const std::string& a_text)
		{
			if (!a_text.empty() && (ImGui::IsItemHovered() || ImGui::IsItemFocused())) { helpbar::OfferForLastItem(a_text); }
		}

		void DrawSetting(std::size_t a_index, const Setting& a_s, const std::string& a_value)
		{
			const std::string label = a_s.label.empty() ? a_s.id : a_s.label;
			const std::string imguiId = label + "##" + a_s.id;
			switch (a_s.type)
			{
			case Type::kHeader:
				ImGui::Spacing();
				mcmstyle::Heading(label);
				break;
			case Type::kToggle:
			{
				bool v = ParseBool(a_value);
				if (widgets::Toggle(imguiId.c_str(), &v)) { SetValue(a_index, a_s.id, v ? "true" : "false"); }
				break;
			}
			case Type::kSlider:
			{
				float v = static_cast<float>(ParseNumber(a_value, a_s.min));
				const float before = v;
				const std::string format = "%." + std::to_string(a_s.decimals) + "f";
				if (precise::SliderFloat(imguiId.c_str(), &v, a_s.min, a_s.max, format.c_str()))
				{
					// one press moves at least one step (rule 68), then the value sits on the step grid as Redux's range input does
					if (v != before && std::fabs(v - before) < a_s.step * 0.5f) { v = before + (v > before ? a_s.step : -a_s.step); }
					v = a_s.min + std::round((v - a_s.min) / a_s.step) * a_s.step;
					v = std::clamp(v, a_s.min, a_s.max);
					if (v != before) { SetValue(a_index, a_s.id, FormatNumber(v, a_s.decimals)); }
				}
				break;
			}
			case Type::kDropdown:
			{
				if (theme::BeginComboTight(imguiId.c_str(), a_value.c_str()))
				{
					for (std::size_t i = 0; i < a_s.options.size(); ++i)
					{
						const std::string opt = a_s.options[i] + "##" + std::to_string(i);
						if (ImGui::Selectable(opt.c_str(), a_s.options[i] == a_value) && a_s.options[i] != a_value) { SetValue(a_index, a_s.id, a_s.options[i]); }
					}
					ImGui::EndCombo();
				}
				break;
			}
			case Type::kKeybind:
			{
				const bool waiting = g_capturing && g_capturing->first == a_index && g_capturing->second == a_s.id;
				const auto code = static_cast<std::int32_t>(ParseNumber(a_value, 0.0));
				ImGui::TextUnformatted(label.c_str());
				ImGui::SameLine();
				const std::string button = std::string(icons::kKeyboard) + "  " +
					(waiting ? std::string(strings::TR("AMF_McmPressKey", "Press a key...")) : mcmloader::detail::KeyName(code)) + "###key" + a_s.id;
				if (ImGui::Button(button.c_str()) && !waiting)
				{
					g_capturing = std::make_pair(a_index, a_s.id);
					input::ArmKeyCapture();
				}
				ImGui::SameLine();
				if (ImGui::Button((std::string(icons::kClear) + "  " + strings::TR("AMF_McmClear", "Clear") + "##clr" + a_s.id).c_str()))
				{
					SetValue(a_index, a_s.id, "0");
				}
				if (waiting && !input::IsKeyCaptureArmed())
				{
					// (device << 32) | code: 0 keyboard (DirectInput scan code, what Redux stores), 1 mouse
					const std::int64_t packed = input::LastCapturedKey();
					g_capturing.reset();
					if (packed >= 0)
					{
						const auto device = static_cast<std::int32_t>(packed >> 32);
						const auto captured = static_cast<std::int32_t>(packed & 0xFFFFFFFF);
						if (device == 0 && captured != 0x01) { SetValue(a_index, a_s.id, std::to_string(captured)); }
						else if (device == 1) { SetValue(a_index, a_s.id, std::to_string(256 + captured)); }
					}
				}
				break;
			}
			case Type::kAction:
			{
				ImGui::TextUnformatted(label.c_str());
				ImGui::SameLine();
				const std::string button = std::string(icons::kAction) + "  " + (a_s.text.empty() ? std::string("EXECUTE") : a_s.text) + "##act" + a_s.id;
				if (ImGui::Button(button.c_str())) { RunAction(a_index, a_s.id); }
				break;
			}
			case Type::kUnknown:
			default:
				ImGui::TextDisabled("%s", icons::kWarning);
				ImGui::SameLine();
				ImGui::TextDisabled(strings::TR("AMF_PrismaNotDrawn", "%s (Prisma type \"%s\" is not drawn yet)"), label.c_str(), a_s.typeName.c_str());
				break;
			}
		}

		void DrawPage(std::size_t a_index, const std::string& a_page)
		{
			NoteDrawn(a_index);
			const Config* config = nullptr;
			std::map<std::string, std::string> values;
			{
				std::scoped_lock lock(g_lock);
				if (a_index >= g_configs.size()) { return; }
				config = g_configs[a_index].get();
				values = config->values;
			}
			helpbar::Want("prisma|" + config->modId + "|" + a_page);
			mcmstyle::PageNote(icons::kInfo, mcmstyle::Fmt(strings::TR("AMF_PrismaNote",
				"Read from %s's Prisma MCM Redux file. Changes are saved where Prisma MCM Redux keeps them, and the mod is told when you leave the page."),
				EventName(*config).c_str()));

			const mcmstyle::PageScope look;
			for (const Setting& s : config->settings)
			{
				if (s.page != a_page) { continue; }
				bool active = true;
				if (!s.condId.empty())
				{
					const auto it = values.find(Lower(s.condId));
					active = it != values.end() && SameValue(it->second, s.condValue);
				}
				const auto it = values.find(Lower(s.id));
				const std::string value = it != values.end() ? it->second : s.defaultValue;
				if (!active) { ImGui::BeginDisabled(); }
				ImGui::PushID(&s);
				DrawSetting(a_index, s, value);
				ImGui::PopID();
				if (!active) { ImGui::EndDisabled(); }
			}
		}

		void DrawAbout(std::size_t a_index)
		{
			NoteDrawn(a_index);
			const Config* config = nullptr;
			{
				std::scoped_lock lock(g_lock);
				if (a_index >= g_configs.size()) { return; }
				config = g_configs[a_index].get();
			}
			mcmstyle::Heading(EventName(*config));
			if (!config->aboutVersion.empty()) { ImGui::TextDisabled("%s", config->aboutVersion.c_str()); }
			if (!config->author.empty())
			{
				ImGui::TextDisabled(strings::TR("AMF_PrismaAuthor", "By %s"), config->author.c_str());
			}
			if (!config->aboutText.empty())
			{
				ImGui::Spacing();
				ImGui::TextWrapped("%s", config->aboutText.c_str());
			}
			if (!config->links.empty())
			{
				ImGui::Spacing();
				for (const auto& [label, url] : config->links)
				{
					ImGui::BulletText("%s", label.empty() ? url.c_str() : label.c_str());
					if (!label.empty() && !url.empty())
					{
						ImGui::SameLine();
						ImGui::TextDisabled("%s", url.c_str());
					}
				}
			}
		}

		void Register(std::vector<std::unique_ptr<Config>>& a_configs)
		{
			std::set<std::string> usedEntries;
			for (std::size_t i = 0; i < a_configs.size(); ++i)
			{
				Config& c = *a_configs[i];
				c.entry = (c.modName.empty() ? c.modId : c.modName);
				for (int n = 2; usedEntries.contains(c.entry); ++n) { c.entry = (c.modName.empty() ? c.modId : c.modName) + " " + std::to_string(n); }
				usedEntries.insert(c.entry);
				c.entry += kSuffix;
				if (c.pages.empty()) { c.pages.push_back(strings::TR("AMF_McmSettingsTab", "Settings")); }
				for (const auto& page : c.pages)
				{
					if (registry::RegisterFn(c.entry.c_str(), page.c_str(), [i, page]() { DrawPage(i, page); }, true)) { c.pageNames.push_back(page); }
				}
				if (c.hasAbout)
				{
					std::string about = strings::TR("AMF_PrismaAbout", "About");
					if (std::find(c.pages.begin(), c.pages.end(), about) != c.pages.end()) { about += " (Prisma)"; }
					if (registry::RegisterFn(c.entry.c_str(), about.c_str(), [i]() { DrawAbout(i); }, true)) { c.pageNames.push_back(about); }
				}
				logger::info("prisma: \"{}\" from {}.json - {} page(s), {} setting(s)", c.entry, c.modId, c.pageNames.size(), c.settings.size());
			}
		}

		// ------------------------------------------------------------------------ Redux's own key ("AMF only")

		int ReadHotkey()
		{
			const auto core = ReadSection(kCoreIni, "core");
			const auto it = core.find("hotkey");
			return it != core.end() ? static_cast<int>(ParseNumber(it->second, 0.0)) : 0;
		}

		std::optional<int> HeldKey()
		{
			std::ifstream in(kHeldKeyPath, std::ios::binary);
			int key = 0;
			if (in >> key) { return key; }
			return std::nullopt;
		}

		void ApplyHotkey()
		{
			if (!g_reduxLoaded) { return; }
			std::error_code ec;
			const bool amfOnly = settings::Get().prismaControl == 1;
			const int now = ReadHotkey();
			const auto held = HeldKey();
			if (amfOnly && now != 0)
			{
				// switch Redux's key off, remembering the player's
				fs::create_directories(fs::path(kHeldKeyPath).parent_path(), ec);
				std::ofstream(kHeldKeyPath, std::ios::binary | std::ios::trunc) << now;
				WriteSection(kCoreIni, "core", { { "Hotkey", "0" } });
				logger::info("prisma: AMF only - Prisma MCM Redux's own key ({}) switched off in PrismaCore.ini (applies from the next game start)", now);
			}
			else if (!amfOnly && held)
			{
				// give it back - unless the player has set a key of their own since
				if (now == 0) { WriteSection(kCoreIni, "core", { { "Hotkey", std::to_string(*held) } }); }
				fs::remove(kHeldKeyPath, ec);
				logger::info("prisma: Prisma MCM Redux's own key {} (applies from the next game start)", now == 0 ? "put back to " + std::to_string(*held) : "left as the player set it");
			}
		}

		std::optional<std::size_t> IndexOf(const std::string& a_modId)
		{
			for (std::size_t i = 0; i < g_configs.size(); ++i)
			{
				if (Lower(g_configs[i]->modId) == Lower(a_modId)) { return i; }
			}
			return std::nullopt;
		}
	}

	void Load()
	{
		if (g_loaded.exchange(true)) { return; }
		g_reduxLoaded = GetModuleHandleW(L"PrismaMCMRedux.dll") != nullptr;
		if (!g_reduxLoaded)
		{
			logger::info("prisma: Prisma MCM Redux is not loaded - no Prisma menus to convert");
			return;
		}
		std::error_code ec;
		if (!fs::is_directory(kConfigDir, ec))
		{
			logger::info("prisma: Prisma MCM Redux is loaded, with no configs in {}", kConfigDir);
			ApplyHotkey();
			return;
		}
		std::vector<std::unique_ptr<Config>> loaded;
		for (const auto& entry : fs::directory_iterator(kConfigDir, ec))
		{
			if (!entry.is_regular_file(ec) || Lower(entry.path().extension().string()) != ".json") { continue; }
			try
			{
				if (auto c = LoadOne(entry.path())) { loaded.push_back(std::move(c)); }
			}
			catch (const std::exception& e) { logger::warn("prisma: {} - {}", entry.path().filename().string(), e.what()); }
		}
		std::sort(loaded.begin(), loaded.end(), [](const auto& a, const auto& b) { return Lower(a->modId) < Lower(b->modId); });
		Register(loaded);
		{
			std::scoped_lock lock(g_lock);
			g_configs = std::move(loaded);
			ApplyVisibilityLocked();
		}
		ApplyHotkey();
		logger::info("prisma: {} Prisma MCM Redux menu(s) registered; control {} (0 both, 1 AMF only, 2 Prisma only)", g_configs.size(), settings::Get().prismaControl);
	}

	void Frame()
	{
		const double now = ImGui::GetTime();
		// the page left, or the menu closed: Redux's Apply
		if (g_open >= 0 && g_lastDrawFrame < ImGui::GetFrameCount() - 1)
		{
			std::scoped_lock lock(g_lock);
			if (static_cast<std::size_t>(g_open) < g_configs.size()) { ApplyLocked(*g_configs[g_open]); }
			g_open = -1;
			g_capturing.reset();
		}
		static double s_lastCheck = -10.0;
		const bool check = now - s_lastCheck >= 2.0;
		{
			std::scoped_lock lock(g_lock);
			for (auto& c : g_configs)
			{
				if (c->unsaved && now >= c->writeDue) { WriteLocked(*c, false); }
			}
		}
		if (!check || g_configs.empty()) { return; }
		s_lastCheck = now;
		// THE ONE-ENTRY RULE, as for FLICK: a mod with its own page here, or its MCM menu converted, keeps that one entry.
		// Checked every couple of seconds - a mod's own page can register after these.
		std::set<std::string> names;
		for (const auto& e : registry::Snapshot()) { names.insert(e.modName); }
		std::scoped_lock lock(g_lock);
		bool changed = false;
		for (auto& c : g_configs)
		{
			if (c->duplicate) { continue; }
			const std::string base = c->modName.empty() ? c->modId : c->modName;
			if (names.contains(base) || names.contains(base + " (MCM)"))
			{
				c->duplicate = true;
				changed = true;
				logger::info("prisma: \"{}\" also has its own or an MCM page here - its Prisma copy is not listed (one entry per mod)", base);
			}
		}
		if (changed) { ApplyVisibilityLocked(); }
	}

	void ApplyControl()
	{
		{
			std::scoped_lock lock(g_lock);
			ApplyVisibilityLocked();
		}
		ApplyHotkey();
		logger::info("prisma: control -> {} (0 both, 1 AMF only, 2 Prisma only)", settings::Get().prismaControl);
	}

	bool ReduxLoaded() { return g_reduxLoaded; }
	int ReduxHotkey() { return g_reduxLoaded ? ReadHotkey() : 0; }
	bool HotkeyHeldByAmf() { return HeldKey().has_value(); }

	std::vector<ConfigRow> Configs()
	{
		std::vector<ConfigRow> rows;
		std::scoped_lock lock(g_lock);
		for (const auto& c : g_configs)
		{
			const int count = static_cast<int>(std::count_if(c->settings.begin(), c->settings.end(),
				[](const Setting& s) { return s.type != Type::kHeader; }));
			rows.push_back({ "prisma|" + c->modId, c->modId, c->entry, count, IsImported(*c), c->duplicate });
		}
		return rows;
	}

	void SetImported(const std::string& a_key, bool a_on)
	{
		mcmloader::detail::SetImported(a_key, a_on);
		std::scoped_lock lock(g_lock);
		ApplyVisibilityLocked();
		logger::info("prisma: {} {} AMF", a_key, a_on ? "brought into" : "left out of");
	}

	std::string ToolJson(const std::string& a_argsJson)
	{
		const json args = json::parse(a_argsJson, nullptr, false);
		const auto get = [&args](const char* k) { return args.is_object() && args.contains(k) && args[k].is_string() ? args[k].get<std::string>() : std::string{}; };
		const std::string what = get("do").empty() ? "list" : get("do");
		json out{ { "ok", true }, { "op", "prisma" }, { "do", what }, { "reduxLoaded", g_reduxLoaded },
			{ "control", settings::Get().prismaControl } };
		if (what == "list")
		{
			out["hotkey"] = ReduxHotkey();
			out["hotkeyHeld"] = HotkeyHeldByAmf();
			json list = json::array();
			for (const auto& r : Configs())
			{
				list.push_back({ { "modId", r.modId }, { "entry", r.entry }, { "settings", r.settings }, { "imported", r.imported }, { "duplicate", r.duplicate } });
			}
			out["configs"] = list;
			out["open"] = g_open;
			return out.dump();
		}
		const std::string mod = get("mod");
		std::optional<std::size_t> index;
		{
			std::scoped_lock lock(g_lock);
			index = IndexOf(mod);
		}
		if (!index)
		{
			out["ok"] = false;
			out["error"] = "no Prisma config " + mod;
			return out.dump();
		}
		if (what == "set")
		{
			SetValue(*index, get("id"), get("value"));
		}
		else if (what == "action")
		{
			RunAction(*index, get("id"));
		}
		else if (what == "apply")
		{
			std::scoped_lock lock(g_lock);
			Config& c = *g_configs[*index];
			c.changed = true;
			ApplyLocked(c);
		}
		std::scoped_lock lock(g_lock);
		const Config& c = *g_configs[*index];
		json values = json::object();
		for (const auto& s : c.settings)
		{
			if (const auto it = c.values.find(Lower(s.id)); it != c.values.end()) { values[s.id] = it->second; }
		}
		out["values"] = values;
		json ini = json::object();
		for (const auto& [k, v] : ReadSection(SettingsPath(c.modId), "settings")) { ini[k] = v; }
		out["ini"] = ini;
		out["unsaved"] = c.unsaved;
		out["changed"] = c.changed;
		return out.dump();
	}
}
