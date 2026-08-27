#include "Theme.h"

#include "utils/Logger.h"

#include <imgui.h>

namespace theme
{
	namespace
	{
		// Ported from Dragon's Eye Minimap's GetHUDOpacitySetting, which is PROVEN in game:
		// on 2026-08-27 its log tracked the options slider live ("fHUDOpacity 0.5 -> alpha 50").
		// The hard-won fact it encodes: the engine registers this setting with NO section suffix,
		// as plain "fHUDOpacity", even though SkyrimPrefs.ini files it under [MAIN]. The section
		// in the file is not part of the name GetSetting matches - two earlier suffix guesses
		// each silently fell back to fully opaque before that was learned. Known-correct name
		// first; the rest stay as fallbacks for other runtimes or mods registering differently.
		RE::Setting* GetHUDOpacitySetting()
		{
			// Resolved once, null cached too - if it is absent on first look it will not appear
			// later, and re-scanning per frame would be waste.
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

			// Nothing matched - report what the engine actually has, so the real name lands in
			// the log instead of prompting a fifth guess.
			int reported = 0;

			auto dump = [&](RE::INISettingCollection* a_collection, const char* a_which) {
				if (!a_collection)
				{
					return;
				}

				for (RE::Setting* candidate : a_collection->settings)
				{
					if (!candidate || !candidate->name)
					{
						continue;
					}

					if (std::string_view(candidate->name).find("HUDOpacity") != std::string_view::npos)
					{
						logger::warn("HUD Opacity: {} has a setting named \"{}\" - none of the names tried matched it",
									 a_which, candidate->name);
						++reported;
					}
				}
			};

			dump(prefs, "SkyrimPrefs.ini");
			dump(ini, "Skyrim.ini");

			if (reported == 0)
			{
				logger::warn("HUD Opacity: no setting containing \"HUDOpacity\" exists in either collection; the framework renders fully opaque");
			}

			return nullptr;
		}
	}

	float GetGameHUDOpacity()
	{
		// FULL OPACITY IS THE FALLBACK (the author's spec, verbatim intent): if the setting cannot be
		// resolved, render solid - never fail toward translucent.
		RE::Setting* setting = GetHUDOpacitySetting();

		if (!setting)
		{
			return 1.0f;
		}

		const float value = setting->GetFloat();

		// A mod or hand-edit can put garbage here; clamp rather than propagate it into every
		// draw call. Values are re-read on every call, so an options-menu change is followed
		// live with no reload - the same standard every settings change in this project meets.
		if (value < 0.0f)
		{
			return 0.0f;
		}

		if (value > 1.0f)
		{
			return 1.0f;
		}

		return value;
	}

	void Apply()
	{
		// The decided identity, applied to the live style. True black at full opacity, warm
		// off-white #F5F2E9 framing and text, and a visible border on EVERY interactive element
		// - windows, frames, buttons, popups - which stock ImGui does not do. The game-opacity
		// multiplier is deliberately NOT baked in here: the present hook sets style.Alpha every
		// frame so one value dims everything consistently and follows the options slider live.
		ImGuiStyle& style = ImGui::GetStyle();

		style.WindowBorderSize = kBorderThickness;
		style.FrameBorderSize = kBorderThickness;   // the "framing around all buttons" half
		style.PopupBorderSize = kBorderThickness;
		style.ChildBorderSize = kBorderThickness;
		style.TabBorderSize = kBorderThickness;
		style.WindowRounding = 0.0f;
		style.FrameRounding = 0.0f;

		const ImVec4 black{ 0.0f, 0.0f, 0.0f, 1.0f };
		// #F5F2E9 - the project's established warm off-white. NOT pure white; see Theme.h.
		const ImVec4 frame{ 245.0f / 255.0f, 242.0f / 255.0f, 233.0f / 255.0f, 1.0f };
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
		// Deliberately NOT ImGui's ~50% grey: the TextDisabled problem is a standing project
		// rule (unreadable on black). Dimmed only slightly, still legible.
		c[ImGuiCol_TextDisabled] = frameDim;

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

		c[ImGuiCol_NavHighlight] = frame;  // the nav box is part of the identity, not an afterthought

		const float opacity = GetGameHUDOpacity();

		logger::info("Theme applied: true black / #F5F2E9, borders on all elements; game HUD opacity {:.2f}", opacity);
	}
}
