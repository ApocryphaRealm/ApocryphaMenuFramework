#pragma once

// ============================================================================================
// GAME SETTINGS OVERRIDE - its files' special case of AMF's TOML pages (TomlCore.h reads and writes them), with no game or
// ImGui code, so the offline check (tools\toml_check.cpp) runs the same file the page does: the game setting's type from
// its EditorID, the control that type gets, the file that wins a key, and the files GSO reads in the order it reads them.
//
// The format, as GSO's Nexus page (114911) documents it and its published files show it - read for the format only; GSO is
// GPL-3.0 and none of its code is used here: Data/SKSE/Plugins/ccld_GameSettingsOverride/*.toml (top level; the extension
// in small letters), one "EditorID = value" per line, "#" comments, bool / integer / float / quoted string values, colours
// as an integer 0xRRGGBBAA. The files load in A-Z order and the later one wins.
// ============================================================================================

#include "TomlCore.h"

#include <algorithm>
#include <string>
#include <vector>

namespace gsocore
{
	using tomlcore::Control;
	using tomlcore::Document;
	using tomlcore::Entry;
	using tomlcore::TomlType;
	using tomlcore::Value;

	// The game setting's type.
	enum class GameType { kUnknown = 0, kBool, kFloat, kInt, kColor, kString, kUInt };

	// A game setting's type is its EditorID's first letter - the engine's own rule (CommonLib's Setting::GetType reads it so
	// on both build lines, whose type enums differ, so the page reads the letter itself): b bool, f float, i int, u uint,
	// s / S string, r (RGB) / a (RGBA) colour. c and h (one-byte numbers) are left unknown: a write of four bytes would not
	// fit them, so the page never sets them in the game.
	inline GameType GameTypeFromName(const std::string& a_editorId)
	{
		if (a_editorId.empty()) { return GameType::kUnknown; }
		switch (a_editorId[0])
		{
		case 'b': return GameType::kBool;
		case 'f': return GameType::kFloat;
		case 'i': return GameType::kInt;
		case 'u': return GameType::kUInt;
		case 's':
		case 'S': return GameType::kString;
		case 'r':
		case 'a': return GameType::kColor;
		default: return GameType::kUnknown;
		}
	}

	inline const char* GameTypeName(GameType a_t)
	{
		switch (a_t)
		{
		case GameType::kBool: return "bool";
		case GameType::kFloat: return "float";
		case GameType::kInt: return "int";
		case GameType::kColor: return "color";
		case GameType::kString: return "string";
		case GameType::kUInt: return "uint";
		default: return "unknown";
		}
	}

	// The control by the GAME setting's type; for a key the game does not know (kUnknown), by the value the file holds. A
	// value this reader shows only (an array, a multi-line string) stays read-only whatever the type.
	inline Control ControlFor(GameType a_game, const Entry& a_e)
	{
		if (!a_e.Editable()) { return Control::kReadOnly; }
		switch (a_game)
		{
		case GameType::kBool: return Control::kSwitch;
		case GameType::kFloat: return Control::kFloat;
		case GameType::kInt: return Control::kInt;
		case GameType::kUInt: return Control::kUInt;
		case GameType::kColor: return Control::kColor;
		case GameType::kString: return Control::kText;
		default: break;
		}
		switch (a_e.value.type)
		{
		case TomlType::kBool: return Control::kSwitch;
		case TomlType::kInt: return Control::kInt;
		case TomlType::kFloat: return Control::kFloat;
		case TomlType::kString: return Control::kText;
		case TomlType::kDateTime: return Control::kDate;
		default: return Control::kReadOnly;
		}
	}

	// Whether the file's value is written as the game setting's type (bool / float / an integer for int, uint and colour /
	// a string). A key the game does not know matches anything - it is flagged as unknown instead.
	inline bool Matches(GameType a_game, TomlType a_toml)
	{
		switch (a_game)
		{
		case GameType::kBool: return a_toml == TomlType::kBool;
		case GameType::kFloat: return a_toml == TomlType::kFloat;
		case GameType::kInt:
		case GameType::kUInt:
		case GameType::kColor: return a_toml == TomlType::kInt;
		case GameType::kString: return a_toml == TomlType::kString;
		default: return true;
		}
	}

	// GSO reads a file only when its name ends in ".toml" in small letters (its Nexus page says so).
	inline bool GsoReadsName(const std::string& a_file)
	{
		return a_file.size() > 5 && a_file.compare(a_file.size() - 5, 5, ".toml") == 0;
	}

	// The load order: by file name, character by character (std::string's order - capitals before small letters). The page
	// shows the files in this order; the in-game check compares it with the order GSO's own log lists.
	inline void SortLoadOrder(std::vector<std::string>& a_files) { std::sort(a_files.begin(), a_files.end()); }

	// The file that sets a key last, by load order (a_docs in that order) - the one whose value the game ends up with. Only
	// keys at the top of a file count (GSO's files are flat). The game's own lookup ignores case, so "fFoo" in one file and
	// "ffoo" in a later one are the same setting. -1 = none.
	inline int Winner(const std::vector<const Document*>& a_docs, const std::string& a_key)
	{
		const std::string want = tomlcore::detail::Lower(a_key);
		int out = -1;
		for (std::size_t i = 0; i < a_docs.size(); ++i)
		{
			if (!a_docs[i]) { continue; }
			for (const Entry& e : a_docs[i]->entries)
			{
				if (e.section == 0 && tomlcore::detail::Lower(e.key) == want) { out = static_cast<int>(i); }
			}
		}
		return out;
	}
}
