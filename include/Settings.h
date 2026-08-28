#pragma once

// ============================================================================================
// M2: the framework's own settings. Plain std::fstream file I/O ONLY - never the Win32 profile
// API (GetPrivateProfileString et al.), which is how the PrivateProfileRedirector stale-cache
// class of bug was born into six of this project's mods at once (fixed 2026-08-27; rule in
// plan.md decided requirement 7). Compiled defaults here MUST match the shipped INI exactly
// (project rule 16) - the shipped file lives at dist/ApocryphaMenuFramework.ini in this repo.
// ============================================================================================

#include <cstdint>
#include <string>

namespace settings
{
	struct Values
	{
		// [Input]
		std::int32_t toggleKey = 0x3B;   // DirectInput scan code; 0x3B = F1 (framework convention, the author 2026-08-27)
		bool controllerMode = false;     // false = keyboard nav, true = gamepad nav (explicit, never auto-detected)

		// [Display]
		float textScale = 1.30f;         // extra font multiplier on top of the resolution scale (the author, 1.0.2 feedback round)
		// Optional path to a .ttf to rasterise the menu text from. Empty = pick a clean system
		// face automatically. Set it to use any font, e.g. one that matches Skyrim's own lettering.
		std::string fontPath;
		std::int32_t windowPreset = 0;   // 0 = centre (the standard). Preset positions, never free placement -
		                                 // the author 2026-08-27, same anchor philosophy as the minimap; more presets later.

		// [Theme]
		std::string themeId = "vanilla";     // registry id (theme::Palette::id). Default is Vanilla (the author)
		                                     // own Skyrim theme for the current test (the author,
		                                     // 2026-08-27) - "Untarnished" (the original identity)
		                                     // is still registered and selectable, just not default.

		// [Debug]
		bool showApiDemo = true;         // the AMF API Demo menu (registered via the public API); ships on until real mods populate the registry

		// [Log]
		std::int32_t logLevel = 0;       // spdlog level: 0 = trace (project rule: ship the most comprehensive level)
	};

	// The live values. Read freely from any thread; written by Load() at plugin init and by the
	// settings page on the render thread. Torn reads of a float/int are acceptable here (no
	// value is multi-word), so no lock - matching how every mod in this project treats INI state.
	Values& Get();

	// Reads Data/SKSE/Plugins/ApocryphaMenuFramework.ini (plain file read, file-first - the file
	// is the source of truth, rule 16's persistence half). Missing file or missing key keeps the
	// compiled default and logs which happened. Applies the log level.
	void Load();

	// Rewrites the INI with the current values, comments included, so a settings-page change
	// survives the next game load (rule 16). Logs on failure, never throws.
	void Save();
}
