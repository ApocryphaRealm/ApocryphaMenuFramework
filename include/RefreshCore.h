#pragma once

// ============================================================================================
// HOW A CONVERTED PAGE'S CHANGE TAKES EFFECT - the part with no game code (PageRefresh.h runs it), so the offline check
// (tools\toml_check.cpp) runs the same matching the pages do.
//
// (the owner, 2026-10-13: "give AMF a refresh feature for these TOML files, just like what MFM does, so that the changes
// take effect in-game and not after a restart ... apply a similar logic to the INI files")
//
// A mod reloads its settings in game only through a function it EXPORTS for that and DECLARES: a Mod Function Menu Config
// entry (ccld_ModFunctionMenu\Config\*.toml: dll, api, type) - which is how MFM's own refresh works. AMF calls only such a
// declared function (and Game Settings Override's ReloadConfig, the same declaration GSO ships), never a guessed name or
// signature: a function called with the wrong signature can crash the game.
//
// Which page a declaration belongs to - by the DLL's name:
// - the page's names: its file's name without the extension; for a file in a sub-folder of SKSE\Plugins, that folder's name;
// - a name matches a DLL when, ignoring case, it IS the DLL's name without ".dll" ("ccld_ToggleUI.toml" -> ccld_ToggleUI.dll)
//   or starts with it and a '_' ("ccld_ModFunctionMenu_styles.toml" -> ccld_ModFunctionMenu.dll); an exact match wins;
// - only a Config entry whose function or file name says "reload" is a reload declaration - a Config entry that does
//   something else is never run after a save.
// ============================================================================================

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace refreshcore
{
	struct Decl
	{
		std::string rel;    // the entry's file, from MFM's folder ("Config/Toggle UI.toml"), or "(Game Settings Override)"
		std::string dll;    // "ccld_ToggleUI.dll"
		std::string api;    // "ReloadConfig"
		int type = 0;       // MFM's: 0 Void, 1 Message, 2 MessageBox
	};

	inline std::string Lower(std::string a_s)
	{
		for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
		return a_s;
	}

	// "ccld_ToggleUI.dll" -> "ccld_toggleui"
	inline std::string Stem(const std::string& a_dll)
	{
		std::string s = Lower(a_dll);
		const auto slash = s.find_last_of("/\\");
		if (slash != std::string::npos) { s = s.substr(slash + 1); }
		if (s.size() > 4 && s.compare(s.size() - 4, 4, ".dll") == 0) { s.resize(s.size() - 4); }
		return s;
	}

	// a Config entry (by its path from MFM's folder) whose function or file name says "reload"
	inline bool IsReload(const Decl& a_d)
	{
		const std::string rel = Lower(a_d.rel);
		const bool config = rel.rfind("config/", 0) == 0 || rel.rfind("config\\", 0) == 0 || rel == "(game settings override)";
		return config && (Lower(a_d.api).find("reload") != std::string::npos || rel.find("reload") != std::string::npos);
	}

	// The page names for a file: "Data/SKSE/Plugins/<folder>/<name>.<ext>" -> { <name>, <folder> }; a top-level file -> { <name> }.
	inline std::vector<std::string> Candidates(const std::string& a_fileBase, const std::string& a_folder)
	{
		std::vector<std::string> out{ a_fileBase };
		if (!a_folder.empty()) { out.push_back(a_folder); }
		return out;
	}

	// The declaration that belongs to a page with these names: index into a_decls, or -1. Only reload declarations.
	inline int Match(const std::vector<Decl>& a_decls, const std::vector<std::string>& a_candidates)
	{
		int prefix = -1;
		for (std::size_t i = 0; i < a_decls.size(); ++i)
		{
			if (!IsReload(a_decls[i])) { continue; }
			const std::string stem = Stem(a_decls[i].dll);
			if (stem.empty()) { continue; }
			for (const std::string& c : a_candidates)
			{
				const std::string l = Lower(c);
				if (l == stem) { return static_cast<int>(i); }
				if (prefix < 0 && l.size() > stem.size() + 1 && l.compare(0, stem.size(), stem) == 0 && l[stem.size()] == '_') { prefix = static_cast<int>(i); }
			}
		}
		return prefix;
	}

	// The name a declaration is shown by: its entry file's name without folder and ".toml" ("Toggle UI").
	inline std::string ShownName(const Decl& a_d)
	{
		std::string s = a_d.rel;
		const auto slash = s.find_last_of("/\\");
		if (slash != std::string::npos) { s = s.substr(slash + 1); }
		if (s.size() > 5 && Lower(s.substr(s.size() - 5)) == ".toml") { s.resize(s.size() - 5); }
		if (!s.empty() && s.front() == '(' && s.back() == ')') { s = s.substr(1, s.size() - 2); }
		return s;
	}

	// The engine's name for a game INI setting: "<key>:<section>" ("fMouseHeadingSens:Controls").
	inline std::string EngineIniName(const std::string& a_key, const std::string& a_section) { return a_key + ":" + a_section; }
}
