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
	// A REMEMBERED WINDOW GEOMETRY, held as FRACTIONS of the display so it survives a resolution
	// change, a different monitor, or borderless-to-fullscreen.
	//
	// There is one of these per way of opening the framework, and they are PROFILES rather than
	// presets (author, 2026-09-04: "lets have it treat them as profiles to save the users settings
	// to so that we set the default to vanilla positioning and size and if their ui mod does
	// different then they can change it and it will remember"). Each starts unset, which means
	// "use this profile's default" - the measured journal panel when nested, the centre anchor
	// otherwise. The moment the player moves or resizes the window, the result is stored here and
	// that profile stops taking the default. Vanilla's geometry is therefore a starting point, not
	// a cage: a menu replacer whose panel sits somewhere else only needs dragging once.
	struct WindowGeometry
	{
		float x = -1.0f;   // -1 on any field = never set by the player
		float y = -1.0f;
		float w = -1.0f;
		float h = -1.0f;

		bool IsSet() const { return x >= 0.0f && y >= 0.0f && w > 0.0f && h > 0.0f; }
		void Clear() { x = y = w = h = -1.0f; }
	};

	struct Values
	{
		// [Window] - one profile per way in; see WindowGeometry above.
		WindowGeometry nestedWindow;   // opened from the row in the game's System menu
		WindowGeometry hotkeyWindow;   // opened by the hotkey, or by a menu launcher through the API

		// [Input]
		std::int32_t toggleKey = 0x3B;   // DirectInput scan code; 0x3B = F1 (framework convention, the author 2026-08-27)

		// [Display]
		float textScale = 1.30f;         // extra font multiplier on top of the resolution scale (the author, 1.0.2 feedback round)
		// Optional path to a .ttf to rasterise the menu text from. Empty = pick a clean system
		// face automatically. Set it to use any font, e.g. one that matches Skyrim's own lettering.
		std::string fontPath;

		// Hang watchdog (the author, 2026-08-28): if the renderer stops producing frames for this many
		// seconds the game is treated as hung and the process terminates itself, so a wedged game
		// never needs Task Manager. Generous by default - a slow cell load still animates frames.
		// The row this framework adds to the GAME's own System menu, so mod settings are reached
		// where a player already looks for configuration rather than from a private hotkey.
		// Injected into the live menu at runtime, so it works over whatever menu artwork is
		// installed and collides with none of it.
		bool systemMenuRow = true;
		bool watchdogEnabled = true;
		// Fast exit (the author, 2026-09-05: "a way to deal with this on exit no kill function issue"):
		// when the game asks Windows to exit, end the process at once instead of running every
		// loaded DLL's and driver's shutdown code - the phase in which a game can wedge into a state
		// no kill, inside or outside the process, can reach. Nothing the game needs happens there.
		bool fastExit = true;
		std::uint32_t watchdogSeconds = 120;
		std::int32_t windowPreset = 0;   // 0 = centre (the standard). Preset positions, never free placement -
		                                 // the author 2026-08-27, same anchor philosophy as the minimap; more presets later.

		// [Theme]
		std::string themeId = "skyrim";     // registry id (theme::Palette::id). Default is Skyrim, the knotwork look
		                                     // own Skyrim theme for the current test (the author,
		                                     // 2026-08-27) - "Untarnished" (the original identity)
		                                     // is still registered and selectable, just not default.

		// [Debug]

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
