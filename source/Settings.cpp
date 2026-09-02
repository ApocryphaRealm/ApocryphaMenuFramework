#include "Settings.h"

#include "Personalization.h"
#include "Theme.h"
#include "utils/Logger.h"

#include <charconv>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace settings
{
	namespace
	{
		constexpr const char* kIniPath = "Data/SKSE/Plugins/ApocryphaMenuFramework.ini";

		Values g_values;

		std::string_view Trim(std::string_view a_text)
		{
			while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t')) a_text.remove_prefix(1);
			while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t' || a_text.back() == '\r')) a_text.remove_suffix(1);
			return a_text;
		}

		// key -> raw value text, sections flattened ("Input.uToggleKey"). A tiny parser is all
		// an INI this size needs, and it keeps the file the single source of truth.
		std::unordered_map<std::string, std::string> ParseFile(std::ifstream& a_file)
		{
			std::unordered_map<std::string, std::string> entries;
			std::string line;
			std::string section;

			while (std::getline(a_file, line))
			{
				const std::string_view trimmed = Trim(line);

				if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#')
				{
					continue;
				}

				if (trimmed.front() == '[' && trimmed.back() == ']')
				{
					section = std::string(Trim(trimmed.substr(1, trimmed.size() - 2)));
					continue;
				}

				const auto equals = trimmed.find('=');
				if (equals == std::string_view::npos)
				{
					continue;
				}

				const std::string key = section + "." + std::string(Trim(trimmed.substr(0, equals)));
				entries[key] = std::string(Trim(trimmed.substr(equals + 1)));
			}

			return entries;
		}

		template <class T>
		void ReadNumber(const std::unordered_map<std::string, std::string>& a_entries, const char* a_key, T& a_out)
		{
			const auto it = a_entries.find(a_key);
			if (it == a_entries.end())
			{
				logger::debug("settings: {} not present in the INI; keeping compiled default", a_key);
				return;
			}

			if constexpr (std::is_same_v<T, float>)
			{
				try
				{
					a_out = std::stof(it->second);
				}
				catch (...)
				{
					logger::warn("settings: {} = \"{}\" is not a number; keeping compiled default", a_key, it->second);
				}
			}
			else
			{
				T parsed{};
				const auto* first = it->second.data();
				const auto* last = first + it->second.size();
				if (std::from_chars(first, last, parsed).ec == std::errc{})
				{
					a_out = parsed;
				}
				else
				{
					logger::warn("settings: {} = \"{}\" is not a number; keeping compiled default", a_key, it->second);
				}
			}
		}

		void ReadBool(const std::unordered_map<std::string, std::string>& a_entries, const char* a_key, bool& a_out)
		{
			std::int32_t number = a_out ? 1 : 0;
			ReadNumber(a_entries, a_key, number);
			a_out = number != 0;
		}
	}

	Values& Get()
	{
		return g_values;
	}

	void Load()
	{
		std::ifstream file(kIniPath);

		if (!file.is_open())
		{
			logger::info("settings: {} not found; compiled defaults in effect (they match the shipped INI, rule 16)", kIniPath);
		}
		else
		{
			const auto entries = ParseFile(file);

			ReadNumber(entries, "Input.uToggleKey", g_values.toggleKey);
			ReadBool(entries, "Input.bControllerMode", g_values.controllerMode);
			ReadBool(entries, "Input.bAutoInputMode", g_values.autoInputMode);
			ReadNumber(entries, "Display.fTextScale", g_values.textScale);
			{
				auto it = entries.find("Display.sFontPath");
				if (it != entries.end()) { g_values.fontPath = it->second; }
			}
			ReadBool(entries, "Watchdog.bEnabled", g_values.watchdogEnabled);
			ReadNumber(entries, "Watchdog.uSeconds", g_values.watchdogSeconds);
			ReadNumber(entries, "Display.uWindowPreset", g_values.windowPreset);
			ReadBool(entries, "Debug.bShowApiDemo", g_values.showApiDemo);
			if (const auto it = entries.find("Theme.sThemeId"); it != entries.end() && !it->second.empty())
			{
				// Retired ids from the 2026-09-01 theme merge are mapped, not dropped.
				g_values.themeId = theme::MigrateThemeId(it->second);
			}
			ReadNumber(entries, "Log.uLogLevel", g_values.logLevel);
			// Menu-shell personalization (aliases + custom order) lives in the same file.
			personalization::LoadFrom(entries);

			logger::info("settings loaded: uToggleKey=0x{:X}, bControllerMode={}, fTextScale={:.2f}, uLogLevel={}",
						 g_values.toggleKey, g_values.controllerMode, g_values.textScale, g_values.logLevel);
		}

		if (g_values.textScale < 1.0f || g_values.textScale > 2.5f)
		{
			logger::warn("settings: fTextScale {:.2f} outside [1.0, 2.5]; clamped", g_values.textScale);
			g_values.textScale = g_values.textScale < 1.0f ? 1.0f : 2.5f;
		}

		const auto level = static_cast<spdlog::level::level_enum>(
			g_values.logLevel < 0 ? 0 : (g_values.logLevel > 6 ? 6 : g_values.logLevel));
		SKSE::log::set_level(level, level);
		logger::info("settings: log level applied ({})", g_values.logLevel);
	}

	void Save()
	{
		std::ofstream file(kIniPath, std::ios::trunc);

		if (!file.is_open())
		{
			logger::error("settings: could not open {} for writing; the change will not survive this session", kIniPath);
			return;
		}

		file << "; Apocrypha Menu Framework - settings. Rewritten by the in-game settings page;\n"
				"; edits made here while the game is closed are honoured on the next load.\n"
				"\n"
				"[Input]\n"
				"; DirectInput scan code that toggles the framework menu. 59 (0x3B) = F1.\n"
				"uToggleKey=" << g_values.toggleKey << "\n"
				"; 0 = keyboard navigation, 1 = controller navigation. Explicit by design - never\n"
				"; auto-detected from the last-used device, which is what causes nav focus drift.\n"
				"bControllerMode=" << (g_values.controllerMode ? 1 : 0) << "\n"
				"; 1 = follow whatever you last used: press a key or move the mouse and the menu\n"
				"; switches to keyboard navigation; touch the pad and it switches to controller\n"
				"; navigation. 0 = the setting above is obeyed exactly.\n"
				"bAutoInputMode=" << (g_values.autoInputMode ? 1 : 0) << "\n"
				"\n"
				"[Display]\n"
				"; Extra text scale on top of the automatic resolution scaling.\n"
				"fTextScale=" << g_values.textScale << "\n"
				"; Optional .ttf to rasterise the menu text from. Empty = a clean system font.\n"
				"sFontPath=" << g_values.fontPath << "\n"
				"; Window position preset. 0 = centre. Preset anchors, not free placement.\n"
				"uWindowPreset=" << g_values.windowPreset << "\n"
				"\n"
				"[Watchdog]\n"
				"; If the menu renderer stops producing frames for uSeconds the game is treated as\n"
				"; hung and closes itself - no Task Manager needed. 0 or bEnabled=0 disables it.\n"
				"bEnabled=" << (g_values.watchdogEnabled ? 1 : 0) << "\n"
				"uSeconds=" << g_values.watchdogSeconds << "\n"
				"\n"
				"[Debug]\n"
				"; 1 shows the AMF API Demo menu (registered through the public API).\n"
				"bShowApiDemo=" << (g_values.showApiDemo ? 1 : 0) << "\n"
				"\n"
				"[Theme]\n"
				"; Registry id (see the theme picker on the Framework Settings page).\n"
				"sThemeId=" << g_values.themeId << "\n"
				"\n"
				"[Log]\n"
				"; 0 = trace (most comprehensive, the project default) ... 6 = off.\n"
				"uLogLevel=" << g_values.logLevel << "\n";

		// Menu-shell personalization writes its own two sections (aliases, custom order).
		file << personalization::IniBlock();

		logger::debug("settings: saved to {}", kIniPath);
	}
}
