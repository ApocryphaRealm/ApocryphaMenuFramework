#include "Theme.h"

#include "Settings.h"
#include "utils/Logger.h"

#include <imgui.h>

#include <charconv>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace theme
{
	namespace
	{
		std::vector<Palette> g_themes;
		std::string g_activeId;

		std::string_view Trim(std::string_view a_text)
		{
			while (!a_text.empty() && (a_text.front() == ' ' || a_text.front() == '\t')) a_text.remove_prefix(1);
			while (!a_text.empty() && (a_text.back() == ' ' || a_text.back() == '\t' || a_text.back() == '\r')) a_text.remove_suffix(1);
			return a_text;
		}

		bool ParseColor(std::string_view a_hex, std::uint32_t& a_out)
		{
			// Accepts "RRGGBB" (as written in a theme file); stored/consumed as ABGR to match
			// the rest of this codebase's packed-colour convention.
			if (!a_hex.empty() && a_hex.front() == '#')
			{
				a_hex.remove_prefix(1);
			}
			if (a_hex.size() != 6)
			{
				return false;
			}

			std::uint32_t rgb = 0;
			const auto result = std::from_chars(a_hex.data(), a_hex.data() + a_hex.size(), rgb, 16);
			if (result.ec != std::errc{})
			{
				return false;
			}

			const std::uint32_t r = (rgb >> 16) & 0xFF;
			const std::uint32_t g = (rgb >> 8) & 0xFF;
			const std::uint32_t b = rgb & 0xFF;
			a_out = 0xFF000000u | (b << 16) | (g << 8) | r;  // ABGR
			return true;
		}

		// Ported unchanged from Dragon's Eye Minimap's GetHUDOpacitySetting, proven in game
		// 2026-08-27. See Theme.h for the full provenance note.
		RE::Setting* GetHUDOpacitySetting()
		{
			static bool resolved = false;
			static RE::Setting* setting = nullptr;

			if (resolved)
			{
				return setting;
			}

			resolved = true;

			auto* prefs = RE::INIPrefSettingCollection::GetSingleton();
			auto* ini = RE::INISettingCollection::GetSingleton();

			constexpr const char* kCandidates[] = {
				"fHUDOpacity",
				"fHUDOpacity:MAIN",
				"fHUDOpacity:Interface",
				"fHUDOpacity:Display",
			};

			for (const char* name : kCandidates)
			{
				if (prefs)
				{
					if (RE::Setting* found = prefs->GetSetting(name))
					{
						logger::info("HUD Opacity setting resolved as \"{}\" in SkyrimPrefs.ini", name);
						setting = found;
						return setting;
					}
				}
				if (ini)
				{
					if (RE::Setting* found = ini->GetSetting(name))
					{
						logger::info("HUD Opacity setting resolved as \"{}\" in Skyrim.ini", name);
						setting = found;
						return setting;
					}
				}
			}

			logger::warn("HUD Opacity: no candidate name matched; the framework renders fully opaque");
			return nullptr;
		}
	}

	void RegisterTheme(Palette a_palette)
	{
		for (Palette& existing : g_themes)
		{
			if (existing.id == a_palette.id)
			{
				logger::info("theme \"{}\" ({}) replaced (re-registration)", a_palette.name, a_palette.id);
				existing = std::move(a_palette);
				return;
			}
		}

		logger::info("theme \"{}\" ({}) registered ({} theme(s) total)", a_palette.name, a_palette.id, g_themes.size() + 1);
		g_themes.push_back(std::move(a_palette));
	}

	void ScanUserThemes()
	{
		constexpr const char* kDir = "Data/SKSE/Plugins/ApocryphaMenuFramework/themes";

		std::error_code ec;
		if (!std::filesystem::exists(kDir, ec) || ec)
		{
			logger::debug("theme scan: {} does not exist; only built-in themes are available", kDir);
			return;
		}

		std::size_t found = 0;

		for (const auto& entry : std::filesystem::directory_iterator(kDir, ec))
		{
			if (ec || !entry.is_regular_file())
			{
				continue;
			}
			if (entry.path().extension() != ".ini")
			{
				continue;
			}

			std::ifstream file(entry.path());
			if (!file.is_open())
			{
				logger::warn("theme scan: could not open {}", entry.path().string());
				continue;
			}

			Palette palette{};
			palette.id = entry.path().stem().string();
			palette.name = palette.id;
			palette.background = 0xFF000000;
			palette.frame = 0xFFE9F2F5;
			palette.borderThickness = 1.0f;

			std::string line;
			while (std::getline(file, line))
			{
				const std::string_view trimmed = Trim(line);
				if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#' || trimmed.front() == '[')
				{
					continue;
				}
				const auto equals = trimmed.find('=');
				if (equals == std::string_view::npos)
				{
					continue;
				}
				const std::string_view key = Trim(trimmed.substr(0, equals));
				const std::string_view value = Trim(trimmed.substr(equals + 1));

				if (key == "sName")
				{
					palette.name = std::string(value);
				}
				else if (key == "sBackground")
				{
					ParseColor(value, palette.background);
				}
				else if (key == "sFrame")
				{
					ParseColor(value, palette.frame);
				}
				else if (key == "fBorderThickness")
				{
					float v{};
					if (std::from_chars(value.data(), value.data() + value.size(), v).ec == std::errc{})
					{
						palette.borderThickness = v;
					}
				}
			}

			RegisterTheme(std::move(palette));
			++found;
		}

		logger::info("theme scan: {} user theme(s) loaded from {}", found, kDir);
	}

	std::vector<Palette> ListThemes()
	{
		return g_themes;
	}

	void SetActiveTheme(const std::string& a_id)
	{
		if (a_id.empty())
		{
			return;
		}

		for (const Palette& p : g_themes)
		{
			if (p.id == a_id)
			{
				g_activeId = a_id;
				logger::info("active theme -> \"{}\" ({})", p.name, p.id);
				return;
			}
		}

		logger::warn("SetActiveTheme(\"{}\") refused: no such theme registered", a_id);
	}

	const Palette& GetActiveTheme()
	{
		for (const Palette& p : g_themes)
		{
			if (p.id == g_activeId)
			{
				return p;
			}
		}

		// Fall back to whatever registered first (the compiled-in default) rather than crash -
		// this only happens if g_activeId was never set to a real id, which Apply()'s caller
		// prevents by registering built-ins before ever calling this.
		return g_themes.front();
	}

	float GetGameHUDOpacity()
	{
		RE::Setting* setting = GetHUDOpacitySetting();
		if (!setting)
		{
			return 1.0f;
		}

		const float value = setting->GetFloat();
		if (value < 0.0f) return 0.0f;
		if (value > 1.0f) return 1.0f;
		return value;
	}

	void Apply()
	{
		if (g_themes.empty())
		{
			// Built-ins, registered here rather than at a separate call site so Apply() is
			// always safe to call standalone (e.g. from a test/DevBench path).
			//
			// "Untarnished" - the ORIGINAL identity (solid black, #F5F2E9 warm off-white),
			// shipped as a selectable theme per the author's instruction, no longer the only option.
			RegisterTheme({ "untarnished", "Untarnished", 0xFF000000, 0xFFE9F2F5, 1.0f });

			// "MO2 Skyrim" - ported from the REAL C:\Modlists\Apostasy\stylesheets\
			// Transparent-Style-Skyrim-Trosski.qss (read directly, not guessed): #b0b0b0 is the
			// dominant text/border colour (41 occurrences, by far the most-used), #717171 the
			// secondary tone. Background stays solid black - the project's full-opacity rule is
			// non-negotiable regardless of source theme, since MO2's own near-transparent look
			// (rgba(0,0,0,10)) exists to show desktop/game art through it, which does not apply
			// to a menu drawn over live gameplay.
			RegisterTheme({ "mo2-skyrim", "MO2 Skyrim", 0xFF000000, 0xFFB0B0B0, 1.0f });

			// DEFAULT set to MO2 Skyrim for the current test, per the author 2026-08-27: "I want the
			// default theme to use MO2's Skyrim theme in the current test."
			g_activeId = "mo2-skyrim";

			ScanUserThemes();

			// Honour a saved preference from the INI (file-first, rule 16) if it names a real
			// theme; otherwise the compiled default above stands. Only checked on this first
			// call - the live picker calls SetActiveTheme() itself before every later Apply().
			SetActiveTheme(settings::Get().themeId);
		}
		else
		{
			ScanUserThemes();
		}

		const Palette& active = GetActiveTheme();

		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowBorderSize = active.borderThickness;
		style.FrameBorderSize = active.borderThickness;
		style.PopupBorderSize = active.borderThickness;
		style.ChildBorderSize = active.borderThickness;
		style.TabBorderSize = active.borderThickness;
		style.WindowRounding = 0.0f;
		style.FrameRounding = 0.0f;

		auto unpack = [](std::uint32_t abgr) {
			const float a = ((abgr >> 24) & 0xFF) / 255.0f;
			const float b = ((abgr >> 16) & 0xFF) / 255.0f;
			const float g = ((abgr >> 8) & 0xFF) / 255.0f;
			const float r = (abgr & 0xFF) / 255.0f;
			return ImVec4{ r, g, b, a };
		};

		const ImVec4 black = unpack(active.background);
		const ImVec4 frame = unpack(active.frame);
		const ImVec4 frameDim{ frame.x, frame.y, frame.z, 0.55f };
		const ImVec4 frameFaint{ frame.x, frame.y, frame.z, 0.14f };
		const ImVec4 frameSoft{ frame.x, frame.y, frame.z, 0.28f };

		ImVec4* c = style.Colors;
		c[ImGuiCol_WindowBg] = black;
		c[ImGuiCol_ChildBg] = black;
		c[ImGuiCol_PopupBg] = black;
		c[ImGuiCol_MenuBarBg] = black;
		c[ImGuiCol_TitleBg] = black;
		c[ImGuiCol_TitleBgActive] = black;
		c[ImGuiCol_TitleBgCollapsed] = black;

		c[ImGuiCol_Text] = frame;
		c[ImGuiCol_TextDisabled] = frameDim;  // NOT ImGui's ~50% grey - the readability rule applies to every theme

		c[ImGuiCol_Border] = frame;
		c[ImGuiCol_Separator] = frameDim;

		c[ImGuiCol_FrameBg] = black;
		c[ImGuiCol_FrameBgHovered] = frameFaint;
		c[ImGuiCol_FrameBgActive] = frameSoft;
		c[ImGuiCol_Button] = black;
		c[ImGuiCol_ButtonHovered] = frameFaint;
		c[ImGuiCol_ButtonActive] = frameSoft;
		c[ImGuiCol_Header] = frameFaint;
		c[ImGuiCol_HeaderHovered] = frameSoft;
		c[ImGuiCol_HeaderActive] = frameSoft;

		c[ImGuiCol_SliderGrab] = frame;
		c[ImGuiCol_SliderGrabActive] = frame;
		c[ImGuiCol_CheckMark] = frame;

		c[ImGuiCol_NavHighlight] = frame;

		logger::info("Theme applied: \"{}\" ({}); game HUD opacity {:.2f}", active.name, active.id, GetGameHUDOpacity());
	}
}
