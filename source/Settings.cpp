#include "Settings.h"

#include "Bindings.h"
#include "Personalization.h"
#include "Skin.h"
#include "Theme.h"
#include "utils/Logger.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>
#include <unordered_set>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace settings
{
	namespace
	{
		// The SHIPPED file: the defaults, replaced by every update. Read first, never written (2.0.3).
		constexpr const char* kIniPath = "Data/SKSE/Plugins/ApocryphaMenuFramework.ini";
		// The PLAYER'S file: everything set in the menu - theme, text size, windows, keys, and the mod list's order,
		// separators, favourites and renames. The download never contains it, so no update can replace it (xLenax via
		// the owner, 2026-10-02: the menu order was lost on every update). Under Mod Organizer 2 it is written to
		// overwrite, which a reinstall does not touch; under any other manager it is a file the manager never installed.
		constexpr const char* kUserDir = "Data/SKSE/Plugins/ApocryphaMenuFramework";
		constexpr const char* kUserPath = "Data/SKSE/Plugins/ApocryphaMenuFramework/User.ini";
		// Saved menu-list layouts, one INI each, holding the [MenuAlias] [MenuOrder] [MenuFavourites] [MenuSeparators]
		// sections. Also never shipped.
		constexpr const char* kPresetDir = "Data/SKSE/Plugins/ApocryphaMenuFramework/Presets";

		Values g_values;

		std::string_view Trim(std::string_view a_text)
		{
			while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t')) a_text.remove_prefix(1);
			while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t' || a_text.back() == '\r')) a_text.remove_suffix(1);
			return a_text;
		}

		// key -> raw value text, sections flattened ("Input.uToggleKey"). A tiny parser is all
		// an INI this size needs, and it keeps the file the single source of truth.
		std::unordered_map<std::string, std::string> ParseFile(std::istream& a_file)
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

		// A WHOLE NUMBER, IN DECIMAL OR HEX (2.0.5). The parse must consume the whole text: from_chars stops at the
		// first character it cannot use and still reports success, so "0x3B" used to read as 0 - the menu key
		// silently became "no key" (the owner, from HadToRegister's report, 2026-10-03). "0x"/"0X" switches to base 16.
		bool ParseInt(std::string_view a_text, long long& a_out)
		{
			a_text = Trim(a_text);
			bool negative = false;
			if (!a_text.empty() && (a_text.front() == '-' || a_text.front() == '+'))
			{
				negative = a_text.front() == '-';
				a_text.remove_prefix(1);
			}
			int base = 10;
			if (a_text.size() > 2 && a_text[0] == '0' && (a_text[1] == 'x' || a_text[1] == 'X'))
			{
				base = 16;
				a_text.remove_prefix(2);
			}
			if (a_text.empty()) { return false; }
			long long value = 0;
			const char* const last = a_text.data() + a_text.size();
			const auto result = std::from_chars(a_text.data(), last, value, base);
			if (result.ec != std::errc{} || result.ptr != last) { return false; }
			a_out = negative ? -value : value;
			return true;
		}

		bool ParseDouble(std::string_view a_text, double& a_out)
		{
			a_text = Trim(a_text);
			if (long long whole = 0; ParseInt(a_text, whole)) { a_out = static_cast<double>(whole); return true; }
			if (a_text.empty()) { return false; }
			const char* const last = a_text.data() + a_text.size();
			const auto result = std::from_chars(a_text.data(), last, a_out);
			return result.ec == std::errc{} && result.ptr == last;
		}

		// Two INI values are the same setting when they are the same number ("59", "0x3B", "59.0") or, for text,
		// the same text. What decides whether User.ini is pinning a value the shipped file already gives.
		bool SameValue(std::string_view a_left, std::string_view a_right)
		{
			double l = 0.0, r = 0.0;
			if (ParseDouble(a_left, l) && ParseDouble(a_right, r))
			{
				const double scale = std::max(1.0, std::max(std::abs(l), std::abs(r)));
				return std::abs(l - r) <= 1e-6 * scale;
			}
			return Trim(a_left) == Trim(a_right);
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
				double parsed = 0.0;
				if (ParseDouble(it->second, parsed)) { a_out = static_cast<float>(parsed); }
				else { logger::warn("settings: {} = \"{}\" is not a number; keeping {}", a_key, it->second, a_out); }
			}
			else
			{
				long long parsed = 0;
				if (ParseInt(it->second, parsed) && parsed >= static_cast<long long>(std::numeric_limits<T>::min()) &&
					parsed <= static_cast<long long>(std::numeric_limits<T>::max()))
				{
					a_out = static_cast<T>(parsed);
				}
				else
				{
					logger::warn("settings: {} = \"{}\" is not a whole number (decimal, or hex as 0x3B); keeping {}",
								 a_key, it->second, a_out);
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

	namespace
	{
		// Every scalar setting as INI text with its comments, for the given values. Save() writes it for the live
		// values and leaves out each key at its shipped value; Load() renders it for a default Values to learn the
		// compiled defaults of keys the shipped file does not carry (2.0.5).
		std::string ScalarBlock(const Values& a_v)
		{
			std::ostringstream file;
			file << "[Menus]\n"
				"; 1 = add a SKSE MENUS row to the game's own System menu, next to SAVE, LOAD and\n"
				"; SETTINGS. Added as the menu opens rather than by replacing any game file, so it\n"
				"; works alongside menu-artwork mods instead of fighting them for the same file.\n"
				"; 0 leaves the game's menu completely untouched.\n"
				"bSystemMenuRow=" << (a_v.systemMenuRow ? 1 : 0) << "\n"
				"\n"
				"[MCM]\n"
				"; 1 = show every MCM Helper mod's menu (Data/MCM/Config/<mod>/config.json) as a\n"
				"; page here as well, read from that mod's own files. Changes go through MCM Helper, exactly\n"
				"; as its own menu makes them. 0 = leave MCM Helper menus to SkyUI only.\n"
				"bLoadMcmHelperConfigs=" << (a_v.loadMcmHelperConfigs ? 1 : 0) << "\n"
				"; 1 = also show every SkyUI menu written only in its mod's own script (no MCM\n"
				"; Helper files) as a page here. This menu makes the same calls on that script as SkyUI's\n"
				"; menu does. 0 = leave those menus to SkyUI only.\n"
				"bLoadSkyUIScriptMenus=" << (a_v.loadSkyUIScriptMenus ? 1 : 0) << "\n"
				"; 1 = every MCM menu found comes into this menu unless you switched it off in the list on the settings\n"
				"; page; 0 = only the menus you switch on there (good for long lists). Your choices per menu are in\n"
				"; ApocryphaMenuFramework\\McmImport.txt.\n"
				"bImportNewMenus=" << (a_v.importNewMcmMenus ? 1 : 0) << "\n"
				"; 1 = take the mods this menu draws in full out of SkyUI's own MCM list, so each one is\n"
				"; set in one place. A mod with anything this menu cannot draw stays in SkyUI's list. 0 = leave\n"
				"; SkyUI's list as it is. The list is kept in your save: to get every menu back in SkyUI without\n"
				"; this mod, type  setstage SKI_ConfigManagerInstance 1  in the console.\n"
				"bHideInSkyUI=" << (a_v.hideMcmInSkyUI ? 1 : 0) << "\n"
				"; Spacing on converted MCM pages, in percent of the text size: the gap between the two columns (0-200) and the\n"
				"; extra space between rows (0-100).\n"
				"uColumnGap=" << a_v.mcmColumnGap << "\n"
				"uRowSpacing=" << a_v.mcmRowSpacing << "\n"
				"\n";
			// 2.1.5: the Mods row's Filter words, kept between games (its own section, after the scalar ones)
			{
				std::string words;
				for (const auto& f : a_v.listFilters)
				{
					if (f.word.empty()) { continue; }
					words += (words.empty() ? "" : ";") + std::string(f.state > 0 ? "+" : f.state < 0 ? "-" : "0") + f.word;
				}
				file << "[ListFilter]\n"
						"; The Mods row's Filter words, kept between games: each is +word (show only rows with it), -word (hide rows with\n"
						"; it) or 0word (kept, not in use), split by ';'. bMatchAll=1: a row needs every + word; 0: any one of them.\n"
						"sWords=" << words << "\n"
						"bMatchAll=" << (a_v.listFilterMatchAll ? 1 : 0) << "\n\n";
			}
			// 2.1.5: the player's own colours, kept per theme (Appearance > Colours) - one [Colors.<theme id>] section for each
			// theme changed, a key per role picked (#RRGGBB); a role left out is that theme's own colour.
			for (const auto& [themeId, picks] : a_v.themeColors)
			{
				bool any = false;
				for (const auto& c : picks) { any = any || !c.empty(); }
				if (!any) { continue; }
				file << "[Colors." << themeId << "]\n";
				for (int r = 0; r < theme::kRoleCount; ++r)
				{
					if (!picks[r].empty()) { file << theme::kColorRoleKeys[r] << "=" << picks[r] << "\n"; }
				}
				file << "\n";
			}
			// 2.1.6: the player's own art, kept per theme (Appearance > Art) - one [Art.<theme id>] section for each theme
			// changed: a part name from assets/frames, assets/backgrounds, assets/toggles, or "none"; a key left out is the theme's.
			for (const auto& [themeId, picks] : a_v.themeArt)
			{
				bool any = false;
				for (const auto& c : picks) { any = any || !c.empty(); }
				if (!any) { continue; }
				file << "[Art." << themeId << "]\n";
				for (std::size_t k = 0; k < picks.size(); ++k)
				{
					if (!picks[k].empty()) { file << skin::kArtKeys[k] << "=" << picks[k] << "\n"; }
				}
				file << "\n";
			}
			file <<
				"[RememberedSettings]\n"
				"; Remembered MCM settings: AMF remembers MCM settings and sets them again on a new game. A new game forgets what SkyUI menus written in a mod's script, and MCM Helper\n"
				"; settings kept in a global or a script property, were set to (MCM Helper's own INI settings it\n"
				"; already keeps). 1 = remember each such change made in this menu, in the profile below.\n"
				"bAutoBackup=" << (a_v.mcmAutoBackup ? 1 : 0) << "\n"
				"; 1 = after a NEW game (not a loaded save), set those menus to the profile again once they appear.\n"
				"bRestoreOnNewGame=" << (a_v.mcmRestoreOnNewGame ? 1 : 0) << "\n"
				"; The profile in use: SKSE\\Plugins\\ApocryphaMenuFramework\\RememberedSettings\\<name>.json.\n"
				"sProfile=" << a_v.rememberedProfile << "\n"
				"\n"
				"[Input]\n"
				"; DirectInput scan code that toggles the framework menu, decimal or hex: 59 (0x3B) = F1.\n"
				"; 0 = no key at all, which is the way to leave F1 entirely to the game. The same key as\n"
				"; Controls > Open and close the menu; a code no key can have falls back to F1.\n"
				"uToggleKey=" << a_v.toggleKey << "\n"
				"; 1 = the on-screen keyboard for controller players: highlight a text box and press A,\n"
				"; and a key grid appears across the bottom of the screen; the D-pad walks it, A types,\n"
				"; B goes back to the box. 0 turns it off.\n"
				"bOnScreenKeyboard=" << (a_v.onScreenKeyboard ? 1 : 0) << "\n"
				"; Keyboard or controller navigation is DETECTED from whatever you last used, and is\n"
				"; not a setting: press a key or move the mouse for keyboard navigation, touch the\n"
				"; pad for controller navigation. The menu shows which one it is reading.\n"
				"\n"
				"[Menu]\n"
				"; 1 = pause the game while this menu is open, the way the game's own menus do:\n"
				"; world time, actors and weather stop until it closes. 0 (the default) leaves the\n"
				"; game running behind it.\n"
				"bPauseGame=" << (a_v.pauseGameWhileOpen ? 1 : 0) << "\n"
				"\n"
				"[Display]\n"
				"; Extra text scale on top of the automatic resolution scaling.\n"
				"fTextScale=" << a_v.textScale << "\n"
				"; See-through window (0/1): on, uWindowOpacity below fades the menu's background. Off:\n"
				"; the background is solid. On (with uWindowOpacity=100, solid) is the default.\n"
				"bSeeThrough=" << (a_v.seeThrough ? 1 : 0) << "\n"
				"; How solid the menu's background is, in percent (5-100). The background fades most, text least.\n"
				"uWindowOpacity=" << a_v.windowOpacity << "\n"
				"; Help bar (0/1): on, the highlighted option's help on a converted MCM page shows in a bar under the\n"
				"; right pane, inside the menu. Off: it shows as a popup beside the option. On is the default.\n"
				"bHelpBar=" << (a_v.helpBar ? 1 : 0) << "\n"
				"; Appearance > Colours (0/1): on, a colour you pick changes the whole menu at once. Off: picks show only in the\n"
				"; page's preview until you press Apply, beside the switch. Off is the default.\n"
				"bColorsApplyNow=" << (a_v.colorsApplyNow ? 1 : 0) << "\n"
				"; The Mods row's Fold switch (0/1): switched on it folds every separator, off opens them all. It acts only when\n"
				"; switched - a separator folded or opened by hand afterwards stays as it is.\n"
				"bFoldAllSeparators=" << (a_v.foldAllSeparators ? 1 : 0) << "\n"
				"; Optional .ttf to rasterise the menu text from. Empty = a clean system font.\n"
				"sFontPath=" << a_v.fontPath << "\n"
				"; Language of the framework's own text: empty = the game's language; or a translation\n"
				"; file's name - english, german, french, spanish, italian, russian, polish, czech,\n"
				"; japanese, chinese (Interface/Translations/ApocryphaMenuFramework_<language>.txt).\n"
				"sLanguage=" << a_v.language << "\n"
				"; Window position preset. 0 = centre. Preset anchors, not free placement.\n"
				"uWindowPreset=" << a_v.windowPreset << "\n"
				"\n"
				"[Window]\n"
				"; Where each way of opening the menu leaves it, as FRACTIONS of the screen so the\n"
				"; numbers stay right at any resolution. -1 means the player has never moved that\n"
				"; window, so it takes its default: the journal panel it is hosted in when opened\n"
				"; from the System row, and the centre of the screen when opened by the key. Move or\n"
				"; resize either one and it is remembered here; the settings page can reset it.\n"
				"; Move the window (0/1): on, drag its top row (the name and version) to move it, and it opens where it\n"
				"; was left. Off: it sits in the middle of the screen. On is the default.\n"
				"bMovable=" << (a_v.movableWindow ? 1 : 0) << "\n"
				"; Resize the window (0/1): on, drag any edge or corner to resize it, freely. Off: its size is fixed.\n"
				"; On is the default.\n"
				"bFreeResize=" << (a_v.freeResize ? 1 : 0) << "\n"
				"fNestedX=" << a_v.nestedWindow.x << "\n"
				"fNestedY=" << a_v.nestedWindow.y << "\n"
				"fNestedW=" << a_v.nestedWindow.w << "\n"
				"fNestedH=" << a_v.nestedWindow.h << "\n"
				"; The journal art the nested position was dragged under (panel = the game's own layout,\n"
				"; qjo-redesign = Quest Journal Overhaul - Entire Journal Redesigned). Under any other art\n"
				"; the position is ignored and the journal is measured afresh.\n"
				"sNestedArt=" << a_v.nestedWindow.art << "\n"
				"fHotkeyX=" << a_v.hotkeyWindow.x << "\n"
				"fHotkeyY=" << a_v.hotkeyWindow.y << "\n"
				"fHotkeyW=" << a_v.hotkeyWindow.w << "\n"
				"fHotkeyH=" << a_v.hotkeyWindow.h << "\n"
				"\n"
				"[Watchdog]\n"
				"; If the menu renderer stops producing frames for uSeconds the game is treated as\n"
				"; hung and closes itself - no Task Manager needed. 0 or bEnabled=0 disables it.\n"
				"bEnabled=" << (a_v.watchdogEnabled ? 1 : 0) << "\n"
				"uSeconds=" << a_v.watchdogSeconds << "\n"
				"\n"
				"[FastExit]\n"
				"; When the game exits, end the process at once instead of running every DLL's and\n"
				"; driver's shutdown code - the phase where a closing game can get stuck beyond any kill.\n"
				"; Saves and settings are written when you save or change them, not at exit. 0 disables.\n"
				"bEnabled=" << (a_v.fastExit ? 1 : 0) << "\n"
				"\n"
				"[Startup]\n"
				"; Hold the screen black from the first frame the game draws until its main menu is\n"
				"; up, so the logo frames and the half-drawn menu behind them are never shown. It\n"
				"; lifts the moment play begins, or after uTimeoutSeconds if the main menu never\n"
				"; appears, so a slow start can never leave you looking at nothing. 0 disables.\n"
				"bBlackCurtain=" << (a_v.startupCurtain ? 1 : 0) << "\n"
				"; Seconds the curtain may stay up before it lifts anyway. It also lifts the moment\n"
				"; play begins, so this only matters for a start that never reaches either.\n"
				"uTimeoutSeconds=" << a_v.curtainTimeoutSeconds << "\n"
				"sCurtainImage=" << a_v.curtainImage << "\n"
				"\n"
				"[Theme]\n"
				"; Registry id (see the theme picker on the Framework Settings page).\n"
				"sThemeId=" << a_v.themeId << "\n"
				"\n"
				"[Skin]\n"
				"; Replacement ARTWORK for the menu shell, for a UI author matching their own\n"
				"; interface. Every image is a 32-bit RGBA PNG - DDS is not decoded. All optional.\n"
				";\n"
				"; bEnabled is the master switch and is OFF by default: with it off nothing below is\n"
				"; even loaded and the menu keeps its built-in look, whatever the paths say. Turn it\n"
				"; on only when you actually have artwork to point it at. There is a matching switch\n"
				"; on the Framework Settings page, so it can be turned off without editing this file.\n"
				";   sFrame       one square PNG with a TRANSPARENT CENTRE, drawn as a nine-slice\n"
				";                around the window. 192x192 with 64px corners is a good default.\n"
				";   uFrameCorner how many pixels of that PNG are the corner ornament.\n"
				";   sBackground  a small tileable PNG (<=512px each side) OR a full-screen one -\n"
				";                which it is is decided by its own size, so both just work.\n"
				";   sPlates      a FOLDER holding any of toggle.png, slider.png, tab.png, to restyle\n"
				";                individual controls. Supply only the ones you want changed.\n"
				"; Paths are under Data (Interface/YourMod/frame.png), or absolute while working.\n"
				"bEnabled=" << (a_v.skinEnabled ? 1 : 0) << "\n"
				"sFrame=" << a_v.skinFrame << "\n"
				"uFrameCorner=" << a_v.skinFrameCorner << "\n"
				"sBackground=" << a_v.skinBackground << "\n"
				"sPlates=" << a_v.skinPlates << "\n"
				"\n"
				"[Log]\n"
				"; 0 = trace, 1 = debug, 2 = info (the shipped default), 3 = warn, 4 = error, 5 = critical, 6 = off. Raise to 0 for a bug report.\n"
				"uLogLevel=" << a_v.logLevel << "\n";
			return file.str();
		}

		// ---- User.ini holds only what the player changed (2.0.5) -----------------------------------------------------
		// HadToRegister, Nexus, 2026-10-03: "Updated to 2.04: it refuses to open with F1. It's set in the INI file as
		// F1 ... I couldn't open the menu with F1 until I deleted the INI file in Data/SKSE/Plugins/ApocryphaMenuFramework".
		// Since 2.0.3 Save() wrote EVERY setting into User.ini the first time anything changed, and User.ini wins every
		// key it holds - so the shipped file, which still reads like the settings file, was silently ignored for every
		// key from then on and a hand edit there did nothing. Now a scalar key is written only when its value differs
		// from what the shipped file (or, for a key it does not carry, the compiled default) would give, and on load a
		// User.ini value equal to the shipped one counts as not set. The list-shaped sections - renames, the mod order,
		// favourites, separators - keep their wholesale semantics and are written whole, exactly as before.

		std::atomic<int> g_toggleSource{ static_cast<int>(ToggleKeySource::kDefault) };

		using Entries = std::unordered_map<std::string, std::string>;

		bool IsListKey(const std::string& a_key)
		{
			return a_key.rfind("MenuAlias.", 0) == 0 || a_key.rfind("MenuOrder.", 0) == 0 ||
				   a_key.rfind("MenuFavourites.", 0) == 0 || a_key.rfind("MenuSeparators.", 0) == 0;
		}

		Entries ParseText(const std::string& a_text)
		{
			std::istringstream in(a_text);
			return ParseFile(in);
		}

		// The value each scalar key has when the player has not set it: the shipped file's, else the compiled
		// default (rendered from a default Values and the default bindings, so the two can never disagree).
		Entries Baseline(bool* a_haveShipped = nullptr, Entries* a_shipped = nullptr)
		{
			Entries base = ParseText(ScalarBlock(Values{}) + bindings::DefaultIniBlock());
			Entries shipped;
			bool found = false;
			if (std::ifstream file(kIniPath); file.is_open())
			{
				shipped = ParseFile(file);
				found = true;
			}
			for (const auto& [key, value] : shipped)
			{
				if (!IsListKey(key)) { base[key] = value; }
			}
			if (a_haveShipped) { *a_haveShipped = found; }
			if (a_shipped) { *a_shipped = std::move(shipped); }
			return base;
		}

		// "Input.uToggleKey" -> "[Input] uToggleKey", the way a player sees it in the file.
		std::string KeyLabel(const std::string& a_key)
		{
			const auto dot = a_key.find('.');
			return dot == std::string::npos ? a_key : "[" + a_key.substr(0, dot) + "] " + a_key.substr(dot + 1);
		}

		// A value as the log shows it; the menu key also gets its key's name.
		std::string Describe(const std::string& a_key, const std::string& a_value)
		{
			long long code = 0;
			if (a_key == "Input.uToggleKey" && ParseInt(a_value, code))
			{
				if (code == 0) { return a_value + " (no key)"; }
				if (code > 0 && code <= 0xFF) { return a_value + " (" + bindings::KeyName(static_cast<std::uint32_t>(code)) + ")"; }
			}
			return a_value.empty() ? std::string("(empty)") : a_value;
		}

		// The scalar text Save() renders, with every key at its baseline value left out - its comment lines go with
		// it, and a section left with no key is left out too.
		std::string KeepChanged(const std::string& a_text, const Entries& a_baseline, int& a_kept, int& a_dropped)
		{
			std::string out;
			std::string section;
			std::string sectionLine;
			bool sectionWritten = false;
			std::string pending;   // comment and blank lines waiting for the key they describe
			std::istringstream in(a_text);
			std::string line;
			while (std::getline(in, line))
			{
				const std::string_view trimmed = Trim(line);
				if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']')
				{
					section = std::string(Trim(trimmed.substr(1, trimmed.size() - 2)));
					sectionLine = std::string(trimmed);
					sectionWritten = false;
					pending.clear();
					continue;
				}
				const auto equals = trimmed.find('=');
				if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#' || equals == std::string_view::npos)
				{
					if (!trimmed.empty() || !pending.empty()) { pending += line + "\n"; }
					continue;
				}
				const std::string key = section + "." + std::string(Trim(trimmed.substr(0, equals)));
				const std::string value(Trim(trimmed.substr(equals + 1)));
				const auto base = a_baseline.find(key);
				if (base != a_baseline.end() && SameValue(value, base->second))
				{
					++a_dropped;
					pending.clear();
					continue;
				}
				if (!sectionWritten)
				{
					out += (out.empty() ? "" : "\n") + sectionLine + "\n";
					sectionWritten = true;
				}
				out += pending + line + "\n";
				pending.clear();
				++a_kept;
			}
			return out;
		}

		// Puts the menu key into effect: kToggleMenu's keyboard key and toggleKey become the same value. A code no
		// key can have, or one another function already holds, falls back to F1 with a warning; 0 is "no key".
		void ApplyToggleKey(std::int32_t a_key, ToggleKeySource a_source)
		{
			std::int32_t key = a_key;
			if (key < 0 || key > 0xFF || key == 0x01)
			{
				logger::warn("settings: uToggleKey {} (0x{:X}) is not a key the menu can open on - a DirectInput keyboard scan "
							 "code is 1-255 and Escape is the menu's own close key; the menu key is F1 (59) instead",
							 a_key, static_cast<std::uint32_t>(a_key));
				key = 0x3B;
				a_source = ToggleKeySource::kFallback;
			}
			std::string holder;
			if (key != bindings::ToggleKeyboardCode() && !bindings::SetToggleKeyboard(key, &holder))
			{
				logger::warn("settings: uToggleKey {} ({}) is already the key for \"{}\" - one key cannot do both; the menu key "
							 "is F1 (59) instead", key, bindings::KeyName(static_cast<std::uint32_t>(key)), holder);
				key = 0x3B;
				a_source = ToggleKeySource::kFallback;
				if (!bindings::SetToggleKeyboard(key, &holder))
				{
					logger::warn("settings: F1 is also taken (by \"{}\") - the menu has no key; the journal's SKSE MENUS row "
								 "still opens it, and Controls can bind one", holder);
					key = 0;
					bindings::SetToggleKeyboard(0, nullptr);
				}
			}
			g_values.toggleKey = key;
			g_toggleSource.store(static_cast<int>(a_source), std::memory_order_release);
		}
	}

	ToggleKeySource GetToggleKeySource()
	{
		return static_cast<ToggleKeySource>(g_toggleSource.load(std::memory_order_acquire));
	}

	const char* ToggleKeySourceName(ToggleKeySource a_source)
	{
		switch (a_source)
		{
		case ToggleKeySource::kShipped:      return "shipped";
		case ToggleKeySource::kUser:         return "user";
		case ToggleKeySource::kUserControls: return "user-controls";
		case ToggleKeySource::kFallback:     return "fallback";
		default:                             return "default";
		}
	}

	bool SetToggleKey(std::int32_t a_scancode)
	{
		std::string holder;
		if (a_scancode < 0 || a_scancode > 0xFF || a_scancode == 0x01) { return false; }
		if (!bindings::SetToggleKeyboard(a_scancode, &holder))
		{
			logger::info("settings: the menu key was not moved to {} - it is already the key for \"{}\"",
						 bindings::KeyName(static_cast<std::uint32_t>(a_scancode)), holder);
			return false;
		}
		g_values.toggleKey = a_scancode;
		Save();
		return true;
	}

	void Load()
	{
		// The shipped INI gives the defaults; User.ini, when it exists, overrides the keys it holds a DIFFERENT value
		// for (2.0.5 - one equal to the shipped value no longer pins it). A key a later version adds is missing from
		// an older User.ini and so comes from the shipped file. Renames are a set rather than single keys: once
		// User.ini exists its [MenuAlias] is the whole set, so one the player cleared does not come back from an old
		// shipped file kept by the installer's "updating" option.
		bool haveShipped = false;
		Entries shipped;
		const Entries baseline = Baseline(&haveShipped, &shipped);
		Entries entries = shipped;
		Entries userEntries;
		std::unordered_set<std::string> userKept;   // scalar keys User.ini really sets (differs from the baseline)
		bool haveUser = false;
		if (std::ifstream user(kUserPath); user.is_open())
		{
			userEntries = ParseFile(user);
			std::erase_if(entries, [](const auto& a_entry) { return a_entry.first.rfind("MenuAlias.", 0) == 0; });
			int pinned = 0;
			for (const auto& [key, value] : userEntries)
			{
				if (IsListKey(key)) { entries[key] = value; continue; }
				const auto base = baseline.find(key);
				if (base != baseline.end() && SameValue(value, base->second))
				{
					++pinned;
					logger::debug("settings: {} in User.ini equals the {} value ({}) - not counted as yours", KeyLabel(key),
								  shipped.contains(key) ? "shipped" : "default", value);
					continue;
				}
				entries[key] = value;
				userKept.insert(key);
				if (base != baseline.end())
				{
					// Said once per load: from the player's side a setting that "will not take" in the shipped file
					// is exactly this, and the line names which file won.
					logger::info("settings: {} - your User.ini says {} over the {} {}; User.ini wins", KeyLabel(key),
								 Describe(key, value), shipped.contains(key) ? "shipped" : "default", Describe(key, base->second));
				}
			}
			haveUser = true;
			logger::info("settings: User.ini sets {} value(s) of its own; {} more equal the shipped values and no longer "
						 "pin them (they are left out the next time the settings are saved)", userKept.size(), pinned);
		}
		logger::info("settings: defaults from {} ({}); your settings from {} ({})", kIniPath, haveShipped ? "read" : "not found",
			kUserPath, haveUser ? "read" : "not saved yet - written the first time a setting changes");

		if (!haveShipped && !haveUser)
		{
			logger::info("settings: no settings file; compiled defaults in effect (they match the shipped INI, rule 16)");
		}
		{

			ReadNumber(entries, "Input.uToggleKey", g_values.toggleKey);
			ReadBool(entries, "Input.bOnScreenKeyboard", g_values.onScreenKeyboard);
			ReadBool(entries, "Menu.bPauseGame", g_values.pauseGameWhileOpen);
			ReadBool(entries, "Menus.bSystemMenuRow", g_values.systemMenuRow);
			ReadBool(entries, "MCM.bLoadMcmHelperConfigs", g_values.loadMcmHelperConfigs);
			ReadBool(entries, "MCM.bLoadSkyUIScriptMenus", g_values.loadSkyUIScriptMenus);
			ReadBool(entries, "MCM.bImportNewMenus", g_values.importNewMcmMenus);
			ReadNumber(entries, "MCM.uColumnGap", g_values.mcmColumnGap);
			ReadNumber(entries, "MCM.uRowSpacing", g_values.mcmRowSpacing);
			g_values.mcmColumnGap = std::clamp(g_values.mcmColumnGap, 0, 200);
			g_values.mcmRowSpacing = std::clamp(g_values.mcmRowSpacing, 0, 100);
			ReadBool(entries, "MCM.bHideInSkyUI", g_values.hideMcmInSkyUI);
			ReadBool(entries, "RememberedSettings.bAutoBackup", g_values.mcmAutoBackup);
			ReadBool(entries, "RememberedSettings.bRestoreOnNewGame", g_values.mcmRestoreOnNewGame);
			if (const auto it = entries.find("RememberedSettings.sProfile"); it != entries.end() && !it->second.empty()) { g_values.rememberedProfile = it->second; }

			// Window profiles. Each field defaults to -1, which the renderer reads as "this profile
			// has never been moved, so use its default geometry"; a missing key therefore behaves
			// exactly like a fresh install rather than pinning the window at 0,0.
			ReadNumber(entries, "Window.fNestedX", g_values.nestedWindow.x);
			ReadNumber(entries, "Window.fNestedY", g_values.nestedWindow.y);
			ReadNumber(entries, "Window.fNestedW", g_values.nestedWindow.w);
			ReadNumber(entries, "Window.fNestedH", g_values.nestedWindow.h);
			if (const auto it = entries.find("Window.sNestedArt"); it != entries.end()) { g_values.nestedWindow.art = it->second; }
			ReadNumber(entries, "Window.fHotkeyX", g_values.hotkeyWindow.x);
			ReadNumber(entries, "Window.fHotkeyY", g_values.hotkeyWindow.y);
			ReadNumber(entries, "Window.fHotkeyW", g_values.hotkeyWindow.w);
			ReadNumber(entries, "Window.fHotkeyH", g_values.hotkeyWindow.h);
			ReadNumber(entries, "Display.fTextScale", g_values.textScale);
			ReadNumber(entries, "Display.uWindowOpacity", g_values.windowOpacity);
			ReadBool(entries, "Window.bMovable", g_values.movableWindow);
			ReadBool(entries, "Window.bFreeResize", g_values.freeResize);
			ReadBool(entries, "Display.bSeeThrough", g_values.seeThrough);
			ReadBool(entries, "Display.bHelpBar", g_values.helpBar);
			ReadBool(entries, "Display.bColorsApplyNow", g_values.colorsApplyNow);
			ReadBool(entries, "Display.bFoldAllSeparators", g_values.foldAllSeparators);
			ReadBool(entries, "ListFilter.bMatchAll", g_values.listFilterMatchAll);
			if (const auto lf = entries.find("ListFilter.sWords"); lf != entries.end())
			{
				// "+word;-word;0word" - a word without a sign is an include, as typed
				g_values.listFilters.clear();
				std::string item;
				for (const char c : lf->second + ";")
				{
					if (c != ';') { item += c; continue; }
					while (!item.empty() && item.front() == ' ') { item.erase(0, 1); }
					while (!item.empty() && item.back() == ' ') { item.pop_back(); }
					if (!item.empty())
					{
						Values::FilterWord f;
						if (item[0] == '+' || item[0] == '-' || item[0] == '0')
						{
							f.state = item[0] == '+' ? 1 : item[0] == '-' ? -1 : 0;
							item.erase(0, 1);
						}
						f.word = item;
						if (!f.word.empty()) { g_values.listFilters.push_back(f); }
					}
					item.clear();
				}
			}
			{
				auto it = entries.find("Display.sFontPath");
				if (it != entries.end()) { g_values.fontPath = it->second; }
				if (const auto lt = entries.find("Display.sLanguage"); lt != entries.end()) { g_values.language = lt->second; }
				// 2.1.5: the player's own colours (empty = the theme's)
				// per theme: "Colors.<theme id>.<key>" (a theme id may itself hold dots, so the key is split off the end)
				static_assert(std::tuple_size_v<decltype(Values{}.themeColors)::mapped_type> == theme::kRoleCount,
							  "Values::themeColors must hold one entry per theme::ColorRole");
				for (const auto& [key, value] : entries)
				{
					if (key.rfind("Colors.", 0) != 0 || value.empty()) { continue; }
					const auto dot = key.rfind('.');
					if (dot <= 7) { continue; }
					const std::string themeId = key.substr(7, dot - 7);
					const std::string roleKey = key.substr(dot + 1);
					for (int r = 0; r < theme::kRoleCount; ++r)
					{
						if (roleKey == theme::kColorRoleKeys[r]) { g_values.themeColors[themeId][r] = value; }
					}
				}
				// 2.1.6: the player's own art per theme: "Art.<theme id>.<key>"
				static_assert(std::tuple_size_v<decltype(Values{}.themeArt)::mapped_type> == static_cast<std::size_t>(skin::ArtKind::kCount),
							  "Values::themeArt must hold one entry per skin::ArtKind");
				for (const auto& [key, value] : entries)
				{
					if (key.rfind("Art.", 0) != 0 || value.empty()) { continue; }
					const auto dot = key.rfind('.');
					if (dot <= 4) { continue; }
					const std::string themeId = key.substr(4, dot - 4);
					const std::string artKey = key.substr(dot + 1);
					for (std::size_t k = 0; k < static_cast<std::size_t>(skin::ArtKind::kCount); ++k)
					{
						if (artKey == skin::kArtKeys[k]) { g_values.themeArt[themeId][k] = value; }
					}
				}
			}
			ReadBool(entries, "Watchdog.bEnabled", g_values.watchdogEnabled);
			ReadBool(entries, "FastExit.bEnabled", g_values.fastExit);
			ReadBool(entries, "Startup.bBlackCurtain", g_values.startupCurtain);
			ReadNumber(entries, "Startup.uTimeoutSeconds", g_values.curtainTimeoutSeconds);
			{
				auto it = entries.find("Startup.sCurtainImage");
				if (it != entries.end()) { g_values.curtainImage = it->second; }
			}
			ReadNumber(entries, "Watchdog.uSeconds", g_values.watchdogSeconds);
			ReadNumber(entries, "Display.uWindowPreset", g_values.windowPreset);
			if (const auto it = entries.find("Theme.sThemeId"); it != entries.end() && !it->second.empty())
			{
				// Retired ids from the 2026-09-01 theme merge are mapped, not dropped.
				g_values.themeId = theme::MigrateThemeId(it->second);
			}
			ReadBool(entries, "Skin.bEnabled", g_values.skinEnabled);
			if (const auto it = entries.find("Skin.sFrame"); it != entries.end()) { g_values.skinFrame = it->second; }
			if (const auto it = entries.find("Skin.sBackground"); it != entries.end()) { g_values.skinBackground = it->second; }
			if (const auto it = entries.find("Skin.sPlates"); it != entries.end()) { g_values.skinPlates = it->second; }
			ReadNumber(entries, "Skin.uFrameCorner", g_values.skinFrameCorner);
			ReadNumber(entries, "Log.uLogLevel", g_values.logLevel);
			// Menu-shell personalization (aliases + custom order) lives in the same file.
			personalization::LoadFrom(entries);
			// Always, even with no file at all: LoadFrom is also what puts the default bindings in place (2.0.5 -
			// with neither INI present it used to be skipped and every control, the menu key included, was unbound).
			bindings::LoadFrom(entries);

			// THE MENU KEY (2.0.5). The input hook opens the menu on Controls' "Open and close the menu" key; until
			// now [Input] uToggleKey was a second copy that only the settings page's label and the reserved-key
			// export read, so editing it - in either file - never changed the key that opens the menu. One value
			// now: uToggleKey decides, unless only the Controls page's binding was changed by the player.
			{
				ToggleKeySource source = userKept.contains("Input.uToggleKey") ? ToggleKeySource::kUser
									   : shipped.contains("Input.uToggleKey")  ? ToggleKeySource::kShipped
																			   : ToggleKeySource::kDefault;
				const std::int32_t bound = bindings::ToggleKeyboardCode();
				const bool controlsChanged = userKept.contains("Bindings.sToggleMenu");
				if (source != ToggleKeySource::kUser && controlsChanged)
				{
					logger::info("settings: the menu key is {} ({}) from your User.ini's [Bindings] sToggleMenu, set on the "
								 "Controls page; it wins over the {} uToggleKey {}", bound,
								 bound > 0 ? bindings::KeyName(static_cast<std::uint32_t>(bound)) : std::string("no key"),
								 ToggleKeySourceName(source), g_values.toggleKey);
					g_values.toggleKey = bound;
					source = ToggleKeySource::kUserControls;
				}
				else if (controlsChanged && bound != g_values.toggleKey)
				{
					logger::warn("settings: your User.ini gives the menu two keys - uToggleKey {} and [Bindings] sToggleMenu {}; "
								 "uToggleKey wins", g_values.toggleKey, bound);
				}
				ApplyToggleKey(g_values.toggleKey, source);
				logger::info("settings: menu key {} ({}) - from {}", g_values.toggleKey,
							 g_values.toggleKey > 0 ? bindings::KeyName(static_cast<std::uint32_t>(g_values.toggleKey)) : std::string("no key"),
							 ToggleKeySourceName(GetToggleKeySource()));
			}

			logger::info("settings loaded: uToggleKey=0x{:X}, bSystemMenuRow={}, fTextScale={:.2f}, uLogLevel={}",
						 g_values.toggleKey, g_values.systemMenuRow, g_values.textScale, g_values.logLevel);
		}

		if (g_values.textScale < 1.0f || g_values.textScale > 2.5f)
		{
			logger::warn("settings: fTextScale {:.2f} outside [1.0, 2.5]; clamped", g_values.textScale);
			g_values.textScale = g_values.textScale < 1.0f ? 1.0f : 2.5f;
		}

		if (g_values.windowOpacity < 5 || g_values.windowOpacity > 100)
		{
			logger::warn("settings: uWindowOpacity {} outside [5, 100]; clamped", g_values.windowOpacity);
			g_values.windowOpacity = g_values.windowOpacity < 5 ? 5 : 100;
		}

		const auto level = static_cast<spdlog::level::level_enum>(
			g_values.logLevel < 0 ? 0 : (g_values.logLevel > 6 ? 6 : g_values.logLevel));
		SKSE::log::set_level(level, level);
		logger::info("settings: log level applied ({})", g_values.logLevel);
	}

	void Save()
	{
		// The menu key is the Controls page's binding; uToggleKey follows it, so a rebind there and the settings
		// page's own Rebind (settings::SetToggleKey) both end up as the one value written below (2.0.5).
		g_values.toggleKey = bindings::ToggleKeyboardCode();

		std::error_code ec;
		std::filesystem::create_directories(kUserDir, ec);
		std::ofstream file(kUserPath, std::ios::trunc);

		if (!file.is_open())
		{
			logger::error("settings: could not open {} for writing; the change will not survive this session", kUserPath);
			return;
		}

		bool haveShipped = false;
		Entries shipped;
		const Entries baseline = Baseline(&haveShipped, &shipped);
		int kept = 0;
		int dropped = 0;
		const std::string changed = KeepChanged(ScalarBlock(g_values) + bindings::IniBlock(), baseline, kept, dropped);

		file << "; ApocryphaRealm Menu Framework - YOUR settings: only what you changed in the menu. Every setting not\n"
				"; listed here comes from SKSE\\Plugins\\ApocryphaMenuFramework.ini, the shipped defaults; one that is listed\n"
				"; wins over that file. Edits made here while the game is closed are honoured on the next load. Delete\n"
				"; this file to go back to the defaults. The download never contains it, so an update cannot replace it.\n"
				"\n"
			 << changed;

		// Menu-shell personalization writes its own sections (renames, order, favourites, separators), whole.
		file << personalization::IniBlock();

		// Where the menu key now comes from, for the DevBench report: the player's file when it differs.
		const auto base = baseline.find("Input.uToggleKey");
		const bool atBaseline = base != baseline.end() && SameValue(std::to_string(g_values.toggleKey), base->second);
		g_toggleSource.store(static_cast<int>(atBaseline ? (shipped.contains("Input.uToggleKey") ? ToggleKeySource::kShipped
																								 : ToggleKeySource::kDefault)
														 : ToggleKeySource::kUser),
							 std::memory_order_release);

		logger::debug("settings: saved to {} - {} value(s) of your own, {} at the shipped value left out", kUserPath, kept, dropped);
	}

	// ---- Menu-list layout presets (2.0.3) ------------------------------------------------------------------------------
	std::string PresetFileName(const std::string& a_name)
	{
		// Letters, digits, spaces and - _ ' ( ) only; a name is a file name and must stay one.
		std::string clean;
		for (const char c : a_name)
		{
			const auto u = static_cast<unsigned char>(c);
			if (std::isalnum(u) || c == ' ' || c == '-' || c == '_' || c == '\'' || c == '(' || c == ')') { clean += c; }
		}
		while (!clean.empty() && clean.back() == ' ') { clean.pop_back(); }
		while (!clean.empty() && clean.front() == ' ') { clean.erase(clean.begin()); }
		if (clean.size() > 48) { clean.resize(48); }
		return clean;
	}

	std::vector<std::string> ListLayoutPresets()
	{
		std::vector<std::string> names;
		std::error_code ec;
		for (std::filesystem::directory_iterator it(kPresetDir, ec), end; !ec && it != end; it.increment(ec))
		{
			if (it->is_regular_file(ec) && it->path().extension() == ".ini") { names.push_back(it->path().stem().string()); }
		}
		std::sort(names.begin(), names.end());
		return names;
	}

	bool SaveLayoutPreset(const std::string& a_name) { return SaveLayoutPresetFrom(a_name, personalization::IniBlock()); }

	bool SaveLayoutPresetFrom(const std::string& a_name, const std::string& a_iniBlock)
	{
		const std::string name = PresetFileName(a_name);
		if (name.empty()) { return false; }
		std::error_code ec;
		std::filesystem::create_directories(kPresetDir, ec);
		const std::string path = std::string(kPresetDir) + "/" + name + ".ini";
		std::ofstream file(path, std::ios::trunc);
		if (!file.is_open())
		{
			logger::error("presets: could not write {}", path);
			return false;
		}
		file << "; ApocryphaRealm Menu Framework - a saved menu-list layout (order, separators, favourites, renames).\n"
				"; Load it from Framework Settings > Menu list > Layout presets.\n";
		file << a_iniBlock;
		logger::info("presets: the menu list was saved as \"{}\" ({})", name, path);
		return true;
	}

	bool LoadLayoutPreset(const std::string& a_name)
	{
		const std::string name = PresetFileName(a_name);
		const std::string path = std::string(kPresetDir) + "/" + name + ".ini";
		std::ifstream file(path);
		if (name.empty() || !file.is_open())
		{
			logger::warn("presets: \"{}\" was not found at {}", a_name, path);
			return false;
		}
		personalization::LoadFrom(ParseFile(file));
		Save();
		logger::info("presets: \"{}\" loaded - the menu list now follows it", name);
		return true;
	}

	bool DeleteLayoutPreset(const std::string& a_name)
	{
		const std::string name = PresetFileName(a_name);
		std::error_code ec;
		const bool removed = !name.empty() && std::filesystem::remove(std::string(kPresetDir) + "/" + name + ".ini", ec);
		if (removed) { logger::info("presets: \"{}\" deleted", name); }
		else { logger::warn("presets: \"{}\" could not be deleted ({})", a_name, ec ? ec.message() : "not found"); }
		return removed;
	}
}
