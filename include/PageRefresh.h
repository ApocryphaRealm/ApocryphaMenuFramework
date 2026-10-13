#pragma once

// ============================================================================================
// HOW A CONVERTED PAGE'S CHANGE TAKES EFFECT (2.2.1; the owner, 2026-10-13: "give AMF a refresh feature for these TOML files,
// just like what MFM does, so that the changes take effect in-game and not after a restart? And could we apply a similar
// logic to the INI files?"). Three ways, and each INI and TOML page says which is its own:
//
// - "reloads in game (via <mod>)": the mod declares a reload function - a Mod Function Menu Config entry naming its DLL,
//   function and type, or Game Settings Override's ReloadConfig (RefreshCore.h says how a page is matched to it). AMF calls
//   it after a save (once the control is let go) and from the page's Reload button, and shows the message it returns. The
//   function is found with GetModuleHandleW + GetProcAddress (never loaded) and called guarded, exactly as AMF's MFM menu
//   calls it. Nothing that is not declared is ever called - a guessed name or signature could crash the game.
// - "applied live": the game's own INI settings (a Data\<plugin>.ini beside its plugin, or an INI whose every key is one of
//   the engine's settings) are set at once in the engine's INISettingCollection (or INIPrefSettingCollection), as GSO does
//   for game settings. The game uses a setting at once wherever it reads it live; some settings are read only at start.
// - "applies the next time the game starts": everything else, said plainly.
// ============================================================================================

#include "RefreshCore.h"

#include <string>
#include <vector>

namespace RE
{
	class Setting;
}

namespace pagerefresh
{
	enum class Kind { kNextLaunch, kLive, kReload };

	struct Reload
	{
		bool found = false;
		refreshcore::Decl decl;
		std::string via;   // shown: "Toggle UI", "Game Settings Override"
	};

	// The declared reload for a page with these names: Mod Function Menu's entries (mfmpages::Declarations), then Game
	// Settings Override's ReloadConfig. Thread-safe.
	Reload Find(const std::vector<std::string>& a_candidates);

	// Whether the declared function's DLL is loaded now.
	bool Loaded(const Reload& a_r);

	struct Called
	{
		bool ran = false;        // found and called
		bool ok = false;
		std::string message;     // Message / MessageBox: the text it returned
		std::string error;       // English: "not loaded", "no export", the fault
	};

	// Render thread only: find (never load) and call the declared function, guarded.
	Called Call(const Reload& a_r);

	// The outcome as the page shows it, in the player's language.
	std::string CalledText(const Reload& a_r, const Called& a_c);

	// The page's line saying how its changes take effect, in the player's language.
	std::string MethodText(Kind a_kind, const std::string& a_via);

	// The engine's game INI setting "<key>:<section>" (INISettingCollection, then INIPrefSettingCollection), or null.
	RE::Setting* EngineIni(const std::string& a_key, const std::string& a_section);

	// Sets an engine setting from its INI text, by its name's type letter (b f i u s/S). False - nothing set - for a type
	// with no plain text form here (colour, one-byte) or text that does not read as the type.
	bool SetFromText(RE::Setting* a_setting, const std::string& a_text);

	// A string set into the game lives for the session (the game's own string is left where it was).
	char* KeepString(const std::string& a_s);
}
