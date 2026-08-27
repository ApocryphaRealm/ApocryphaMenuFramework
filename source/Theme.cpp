#include "Theme.h"

#include "utils/Logger.h"

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
		// M1: applies kBackground/kFrame/kBorderThickness to the live ImGui style, with
		// GetGameHUDOpacity() as the global alpha multiplier at draw time. Until then, resolve
		// and log the opacity once - which both proves the resolver against a live game in M0
		// and keeps /OPT:REF from dead-stripping it (verified stripped from the first M0 build:
		// the "fHUDOpacity" strings were absent from the DLL because nothing called this path).
		const float opacity = GetGameHUDOpacity();

		logger::info("Theme: game HUD opacity resolved to {:.2f} (applied as the global alpha multiplier from M1)", opacity);
	}
}
