#pragma once

#include <string>
#include <string_view>

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
	// Wire these directly to the SKSE kSaveGame/kLoadGame message handlers. a_saveName is the
	// message payload verbatim (whatever SKSE hands over - base name, with or without .ess).
	void OnSaveGame(std::string_view a_saveName);
	void OnLoadGame(std::string_view a_saveName);

	// The shared key-value surface. Values live in memory between save/load events; Set does
	// NOT write to disk immediately - the write happens at the next OnSaveGame, exactly
	// mirroring co-save's own timing (data is committed when the game saves, not on every Set).
	void SetValue(const std::string& a_key, const std::string& a_value);
	std::string GetValue(const std::string& a_key, const std::string& a_default);
}
