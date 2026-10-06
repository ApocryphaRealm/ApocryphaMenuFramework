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
#include <vector>

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
		// 1.7.8: the KEY-OPENED window keeps only x/y/w/h (a corner drag keeps its shape; nothing scales).
		// 1.8.1: the NESTED profile also remembers which journal ART it was dragged under (the owner,
		// 2026-09-13: the position must only follow the redesign while the redesign is active). A stored
		// position whose art is not the art on screen is ignored and the measured panel is used.
		std::string art;

		bool IsSet() const { return x >= 0.0f && y >= 0.0f && w > 0.0f && h > 0.0f; }
		void Clear() { x = y = w = h = -1.0f; }
	};

	struct Values
	{
		// [Window] - one profile per way in; see WindowGeometry above.
		WindowGeometry nestedWindow;   // opened from the row in the game's System menu
		WindowGeometry hotkeyWindow;   // opened by the hotkey, or by a menu launcher through the API
		// Barzing's asks on Nexus, 2026-10-05 (move it, resize it in height, see through it), each its own switch on the
		// Appearance tab (the owner: "seperate toggles" ... "in apperance teb"), all ON by default ("have it default to
		// on, along with the other settings we just added, like the move the window and see-through window at max
		// opacity"), matching the shipped INI (rule 16). The window always resized from every edge - a saved size 99% of
		// the screen tall had made the height look fixed; "Resize the window" is now a true on/off ("make sure the toggle actually toggles off the resizing").
		bool movableWindow = true;    // [Window] bMovable - drag the top row; it reopens where it was left
		bool freeResize = true;       // [Window] bFreeResize - Resize the window: on, any edge or corner resizes freely; off, NoResize
		bool seeThrough = true;       // [Display] bSeeThrough - uWindowOpacity applies (100 = solid, the default)

		// [Input]
		std::int32_t toggleKey = 0x3B;   // DirectInput scan code; 0x3B = F1 (framework convention, the author 2026-08-27)
		// 1.8.9: the on-screen keyboard for controller players (the owner, 2026-09-18) - a key grid
		// across the bottom of the screen that types into whichever text box a page has highlighted.
		// See Keyboard.h. Off hides it entirely; consumer mods can still summon it by export.
		bool onScreenKeyboard = true;    // bOnScreenKeyboard

		// [Menu] bPauseGame (the owner, 2026-09-22: "next amf update gets a toggle in settings to stop time while
		// menu is active"): while this window is open the game is paused the way its own menus pause it - world
		// time, actors, weather and cooldowns stop. OFF by default, so nothing changes for anyone who does not turn
		// it on. Opened from the System row the game is already paused by the journal; this adds nothing there.
		bool pauseGameWhileOpen = false;

		// [Display]
		float textScale = 1.30f;         // extra font multiplier on top of the resolution scale (the author, 1.0.2 feedback round)
		// [Display] uWindowOpacity (2.1.1, Barzing on Nexus, 2026-10-05: "the semi transparence of the window"; the owner: "ill
		// add ... opacity settings"): how solid the window's background is, in percent, 5-100. Text, frames and the
		// right-click menus stay solid. Matches the shipped INI (rule 16).
		std::int32_t windowOpacity = 100;
		// Optional path to a .ttf to rasterise the menu text from. Empty = pick a clean system
		// face automatically. Set it to use any font, e.g. one that matches Skyrim's own lettering.
		std::string fontPath;
		// [Display] sLanguage - which translation file the framework's own text comes from
		// (Interface\Translations\ApocryphaMenuFramework_<language>.txt). Empty = follow the game's
		// sLanguage. Set from the Language combo on the Framework Settings page or here.
		std::string language;

		// Hang watchdog (the author, 2026-08-28): if the renderer stops producing frames for this many
		// seconds the game is treated as hung and the process terminates itself, so a wedged game
		// never needs Task Manager. Generous by default - a slow cell load still animates frames.
		// The row this framework adds to the GAME's own System menu, so mod settings are reached
		// where a player already looks for configuration rather than from a private hotkey.
		// Injected into the live menu at runtime, so it works over whatever menu artwork is
		// installed and collides with none of it.
		bool systemMenuRow = true;
		// MCM loader: [MCM] bLoadMcmHelperConfigs - draw MCM Helper mods' menus as AMF pages.
		bool loadMcmHelperConfigs = true;
		// MCM loader, phase 3: [MCM] bLoadSkyUIScriptMenus - draw SkyUI menus written only in Papyrus as AMF pages.
		bool loadSkyUIScriptMenus = true;
		// MCM loader: [MCM] bImportNewMenus - a menu the player never switched on or off comes into AMF (1) or stays in SkyUI
		// only (0). Long lists start from none with 0 and switch on the few they use (xLenax, 2026-10-04).
		bool importNewMcmMenus = true;
		// MCM loader: [MCM] bHideInSkyUI - take the mods AMF draws completely out of SkyUI's own MCM list.
		bool hideMcmInSkyUI = false;
		// remembered MCM settings (RememberedSettings.h): [RememberedSettings] bAutoBackup - remember each MCM change made here;
		// bRestoreOnNewGame - apply the profile again after a new game; sProfile - the active profile's name.
		bool mcmAutoBackup = true;
		bool mcmRestoreOnNewGame = true;
		std::string rememberedProfile = "Default";
		bool watchdogEnabled = true;
		// Fast exit (the author, 2026-09-05: "a way to deal with this on exit no kill function issue"):
		// when the game asks Windows to exit, end the process at once instead of running every
		// loaded DLL's and driver's shutdown code - the phase in which a game can wedge into a state
		// no kill, inside or outside the process, can reach. Nothing the game needs happens there.
		bool fastExit = true;
		// Startup curtain (the owner, 2026-09-15): hold the screen black from the first drawn
		// frame until the game's main menu is up, so the logo frames and the half-drawn menu are
		// never shown. Lifts by itself on a timeout - see Curtain.cpp, where failing safe is the
		// whole design.
		bool startupCurtain = true;
		// How long the curtain may stay up before it gives up and lifts anyway. INI-only, because
		// it is a safety valve rather than a preference. 30 proved too short on a heavy list.
		std::uint32_t curtainTimeoutSeconds = 120;
		// [Startup] sCurtainImage - a picture to show on the curtain instead of plain black, given
		// relative to Data (e.g. SKSE\\Plugins\\ApocryphaMenuFramework\\curtain.png). Empty is the
		// default and means black, so a plain install looks exactly as it did. The image is fitted
		// inside the screen with its aspect kept, on black, and fades out with the curtain.
		std::string curtainImage;
		std::uint32_t watchdogSeconds = 120;
		std::int32_t windowPreset = 0;   // 0 = centre (the standard). Preset positions, never free placement -
		                                 // the author 2026-08-27, same anchor philosophy as the minimap; more presets later.

		// [Theme]
		std::string themeId = "skyrim";     // registry id (theme::Palette::id). Default is Skyrim, the knotwork look
		                                     // own Skyrim theme for the current test (the author,
		                                     // 2026-08-27) - "Untarnished" (the original identity)
		                                     // is still registered and selectable, just not default.

		// [Skin] - REPLACEMENT ARTWORK for the menu shell, so a UI author can make the framework
		// match their own interface (requested 2026-09-09 for borokoshow / Dragonborn UI). All
		// four are optional and independent; see Skin.h for what an author actually ships.
		// Every image is PNG - AMF decodes through WIC, which does not read DDS at all.
		// OFF BY DEFAULT, and that is the point. Art that is on unless you turn it off means a
		// player with no art replacer installed can end up looking at whatever placeholder PNGs
		// happen to be on disk - which is exactly what happened during development. A UI author
		// turning the feature on is one line; a player seeing art they never asked for is a bug.
		bool          skinEnabled = false; // bEnabled - master switch for everything in [Skin]
		std::string   skinFrame;            // sFrame - nine-slice frame PNG, transparent centre
		std::uint32_t skinFrameCorner = 64; // uFrameCorner - corner slice in px (192x192/64 suggested)
		std::string   skinBackground;       // sBackground - tiled if <= 512px both sides, else stretched
		std::string   skinPlates;           // sPlates - folder holding toggle.png / slider.png / tab.png

		// [Debug]

		// [Log]
		std::int32_t logLevel = 2;       // spdlog level: 2 = info, the shipped default (rule 14, 2026-09-26); 0 = trace for a report
	};

	// The live values. Read freely from any thread; written by Load() at plugin init and by the
	// settings page on the render thread. Torn reads of a float/int are acceptable here (no
	// value is multi-word), so no lock - matching how every mod in this project treats INI state.
	Values& Get();

	// Reads the shipped Data/SKSE/Plugins/ApocryphaMenuFramework.ini as the defaults, then the player's
	// Data/SKSE/Plugins/ApocryphaMenuFramework/User.ini over it (plain file reads - the files are the source of
	// truth, rule 16's persistence half). A missing file or key keeps the compiled default and logs which
	// happened. Applies the log level.
	void Load();

	// Writes User.ini, comments included, so a settings-page change survives the next game load (rule 16) AND the
	// next update - the download never ships User.ini (2.0.3). Since 2.0.5 it holds only what the player changed:
	// a scalar key at the shipped value (or the compiled default, for a key the shipped file lacks) is left out, so
	// the shipped file goes on deciding it. The list-shaped sections are written whole. Logs on failure, never throws.
	void Save();

	// THE MENU KEY (2.0.5, HadToRegister's report). [Input] uToggleKey and Controls' "Open and close the menu" are
	// one key; where its value came from on the last load or save, for the DevBench report and the log.
	enum class ToggleKeySource : int
	{
		kDefault = 0,    // the compiled default - neither file has uToggleKey
		kShipped,        // the shipped ApocryphaMenuFramework.ini
		kUser,           // User.ini's uToggleKey, which differs from the shipped value
		kUserControls,   // User.ini's [Bindings] sToggleMenu, set on the Controls page
		kFallback,       // the value given was not a usable key, so F1 (or no key, if F1 is taken)
	};
	ToggleKeySource GetToggleKeySource();
	const char* ToggleKeySourceName(ToggleKeySource a_source);
	// The settings page's Rebind: moves the menu key and saves. False (nothing changed) for Escape, a code no key
	// can have, or a key another function already holds.
	bool SetToggleKey(std::int32_t a_scancode);

	// Menu-list layout presets (2.0.3): the order, separators, favourites and renames saved under a name in
	// Data/SKSE/Plugins/ApocryphaMenuFramework/Presets/<name>.ini. Names are cleaned to letters, digits, spaces
	// and - _ ' ( ); loading one replaces the current layout and saves it as the player's.
	std::vector<std::string> ListLayoutPresets();
	bool SaveLayoutPreset(const std::string& a_name);
	// The same file from a layout taken earlier (personalization::IniBlock()): the MCM sort keeps the order from before it.
	bool SaveLayoutPresetFrom(const std::string& a_name, const std::string& a_iniBlock);
	bool LoadLayoutPreset(const std::string& a_name);
	bool DeleteLayoutPreset(const std::string& a_name);
}
