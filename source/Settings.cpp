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
			ReadBool(entries, "Menus.bSystemMenuRow", g_values.systemMenuRow);

			// Window profiles. Each field defaults to -1, which the renderer reads as "this profile
			// has never been moved, so use its default geometry"; a missing key therefore behaves
			// exactly like a fresh install rather than pinning the window at 0,0.
			ReadNumber(entries, "Window.fNestedX", g_values.nestedWindow.x);
			ReadNumber(entries, "Window.fNestedY", g_values.nestedWindow.y);
			ReadNumber(entries, "Window.fNestedW", g_values.nestedWindow.w);
			ReadNumber(entries, "Window.fNestedH", g_values.nestedWindow.h);
			ReadNumber(entries, "Window.fHotkeyX", g_values.hotkeyWindow.x);
			ReadNumber(entries, "Window.fHotkeyY", g_values.hotkeyWindow.y);
			ReadNumber(entries, "Window.fHotkeyW", g_values.hotkeyWindow.w);
			ReadNumber(entries, "Window.fHotkeyH", g_values.hotkeyWindow.h);
			ReadNumber(entries, "Display.fTextScale", g_values.textScale);
			{
				auto it = entries.find("Display.sFontPath");
				if (it != entries.end()) { g_values.fontPath = it->second; }
			}
			ReadBool(entries, "Watchdog.bEnabled", g_values.watchdogEnabled);
			ReadBool(entries, "FastExit.bEnabled", g_values.fastExit);
			ReadNumber(entries, "Watchdog.uSeconds", g_values.watchdogSeconds);
			ReadNumber(entries, "Display.uWindowPreset", g_values.windowPreset);
			if (const auto it = entries.find("Theme.sThemeId"); it != entries.end() && !it->second.empty())
			{
				// Retired ids from the 2026-09-01 theme merge are mapped, not dropped.
				g_values.themeId = theme::MigrateThemeId(it->second);
			}
			ReadNumber(entries, "Log.uLogLevel", g_values.logLevel);
			// Menu-shell personalization (aliases + custom order) lives in the same file.
			personalization::LoadFrom(entries);

			logger::info("settings loaded: uToggleKey=0x{:X}, bSystemMenuRow={}, fTextScale={:.2f}, uLogLevel={}",
						 g_values.toggleKey, g_values.systemMenuRow, g_values.textScale, g_values.logLevel);
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

		file << "; ApocryphaRealm Menu Framework - settings. Rewritten by the in-game settings page;\n"
				"; edits made here while the game is closed are honoured on the next load.\n"
				"\n"
				"[Menus]\n"
				"; 1 = add a SKSE MENUS row to the game's own System menu, next to SAVE, LOAD and\n"
				"; SETTINGS. Added as the menu opens rather than by replacing any game file, so it\n"
				"; works alongside menu-artwork mods instead of fighting them for the same file.\n"
				"; 0 leaves the game's menu completely untouched.\n"
				"bSystemMenuRow=" << (g_values.systemMenuRow ? 1 : 0) << "\n"
				"\n"
				"[Input]\n"
				"; DirectInput scan code that toggles the framework menu. 59 (0x3B) = F1.\n"
				"; 0 = no key at all, which is the way to leave F1 entirely to the game.\n"
				"uToggleKey=" << g_values.toggleKey << "\n"
				"; Keyboard or controller navigation is DETECTED from whatever you last used, and is\n"
				"; not a setting: press a key or move the mouse for keyboard navigation, touch the\n"
				"; pad for controller navigation. The menu shows which one it is reading.\n"
				"\n"
				"[Display]\n"
				"; Extra text scale on top of the automatic resolution scaling.\n"
				"fTextScale=" << g_values.textScale << "\n"
				"; Optional .ttf to rasterise the menu text from. Empty = a clean system font.\n"
				"sFontPath=" << g_values.fontPath << "\n"
				"; Window position preset. 0 = centre. Preset anchors, not free placement.\n"
				"uWindowPreset=" << g_values.windowPreset << "\n"
				"\n"
				"[Window]\n"
				"; Where each way of opening the menu leaves it, as FRACTIONS of the screen so the\n"
				"; numbers stay right at any resolution. -1 means the player has never moved that\n"
				"; window, so it takes its default: the journal panel it is hosted in when opened\n"
				"; from the System row, and the centre of the screen when opened by the key. Move or\n"
				"; resize either one and it is remembered here; the settings page can reset it.\n"
				"fNestedX=" << g_values.nestedWindow.x << "\n"
				"fNestedY=" << g_values.nestedWindow.y << "\n"
				"fNestedW=" << g_values.nestedWindow.w << "\n"
				"fNestedH=" << g_values.nestedWindow.h << "\n"
				"fHotkeyX=" << g_values.hotkeyWindow.x << "\n"
				"fHotkeyY=" << g_values.hotkeyWindow.y << "\n"
				"fHotkeyW=" << g_values.hotkeyWindow.w << "\n"
				"fHotkeyH=" << g_values.hotkeyWindow.h << "\n"
				"\n"
				"[Watchdog]\n"
				"; If the menu renderer stops producing frames for uSeconds the game is treated as\n"
				"; hung and closes itself - no Task Manager needed. 0 or bEnabled=0 disables it.\n"
				"bEnabled=" << (g_values.watchdogEnabled ? 1 : 0) << "\n"
				"uSeconds=" << g_values.watchdogSeconds << "\n"
				"\n"
				"[FastExit]\n"
				"; When the game exits, end the process at once instead of running every DLL's and\n"
				"; driver's shutdown code - the phase where a closing game can get stuck beyond any kill.\n"
				"; Saves and settings are written when you save or change them, not at exit. 0 disables.\n"
				"bEnabled=" << (g_values.fastExit ? 1 : 0) << "\n"
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
