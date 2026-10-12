#pragma once

#include <string>
#include <string_view>
#include <vector>

// ============================================================================================
// M-adjacent: an AMF-owned, per-save persistence channel mirroring co-save's SCOPING (not its
// binary format) - the author, 2026-08-27 (decisions doc S10). SKSE's kSaveGame/kLoadGame messages
// carry the save's own filename as their payload; this module uses that to write/read a plain
// text sibling file matching whichever save is active, so the value that comes back is always
// the one belonging to the save that was actually loaded - same logic as the real .skse
// co-save, self-owned format.
//
// Functionally testable without visual judgement (the author, 2026-08-27): save, quit, reload,
// confirm via the log that the round-tripped value matches what was written.
// ============================================================================================

namespace persistence
{
	// kSaveGame's payload really IS the save name (SKSE convention), so wire it straight here.
	void OnSaveGame(std::string_view a_saveName);

	// Load is a TWO-message dance and the payloads differ (this cost a crash, 1.3.3):
	//   kPreLoadGame  -> data = the save's name  (char* + dataLen)     -> OnPreLoadGame
	//   kPostLoadGame -> data = a BOOL success flag, NOT a string      -> OnPostLoadGame
	// Treating kPostLoadGame's data as a name dereferenced (void*)0x1 (bool true) and crashed in
	// StripExtension. So: capture the name at pre-load, and at post-load restore it only if the
	// load actually succeeded.
	void OnPreLoadGame(std::string_view a_saveName);
	void OnPostLoadGame(bool a_loadSucceeded);

	// Kept internal-but-declared: the actual file read, given a known-good name.
	void OnLoadGame(std::string_view a_saveName);

	// The shared key-value surface. Values live in memory between save/load events; Set does
	// NOT write to disk immediately - the write happens at the next OnSaveGame, exactly
	// mirroring co-save's own timing (data is committed when the game saves, not on every Set).
	void SetValue(const std::string& a_key, const std::string& a_value);
	std::string GetValue(const std::string& a_key, const std::string& a_default);

	// ORPHANED STATE FILES (2.1.9, AmeliaZ on Nexus, 2026-10-11: "auto clean *.amf-state.ini files not associated to saves").
	// A deleted save leaves its <save>.amf-state.ini behind. CleanOrphanedState lists every *.amf-state.ini in the save folder
	// whose <save>.ess is gone and, unless a_listOnly, DELETES each (the owner, 2026-10-12: "It should be permanently deleted.
	// As the whole point of the feature is so that they don't have to go digging into the files to delete it"; 2.1.9 first
	// moved them to a backup folder). Guards: nothing is deleted unless the save folder was found and read without error AND
	// holds at least one save (none = most likely the wrong folder, e.g. an MO2 profile's local saves - logged, nothing
	// done); a file is deleted only when no save with exactly its base name exists, never the save being played. Each file
	// and the count are logged. Run at kDataLoaded when Settings > General > "Tidy up save state left by deleted saves" is on
	// (the default), and by DevBench amf.menu op=cleanstate.
	struct CleanResult
	{
		bool ran = false;            // false: the save folder could not be found or read (error says why)
		bool listOnly = false;
		int saves = 0;               // *.ess saves found (none: nothing is deleted)
		int stateFiles = 0;          // *.amf-state.ini files found
		int orphaned = 0;            // of those, the ones whose save is gone
		int deleted = 0;
		int failed = 0;
		std::string saveFolder;      // UTF-8
		std::string error;
		std::vector<std::string> files;   // the orphaned files' names, UTF-8
	};
	CleanResult CleanOrphanedState(bool a_listOnly);
}
