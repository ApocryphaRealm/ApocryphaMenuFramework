#pragma once

// ================================================================================================
// THE SYSTEM-MENU ROW - mod menus reached from the GAME's own pause menu.
//
// The framework's settings surface is opened from a row in the journal's System tab, next to SAVE,
// LOAD, SETTINGS and QUIT, rather than from a hotkey the player has to know about. That is where a
// player already goes looking for configuration.
//
// WHY THIS IS DONE AT RUNTIME AND NOT BY SHIPPING AN EDITED SWF
// -------------------------------------------------------------
// Design decision, 2026-09-04: "AMF should be compatible with other art replacers". The journal's
// artwork is `Interface/quest_journal.swf`, and every UI replacement mod ships its own copy of that
// exact file - Untarnished UI, Dear Diary Dark Mode, and our own Apocrypha UI Overhaul among them.
// A framework that shipped its own edited copy would collide with all of them, and whichever lost
// the load order would silently lose either its artwork or its row.
//
// So nothing is shipped. The row is pushed into the live menu when it opens, which works over
// whatever artwork happens to be installed, and needs no patch per replacer.
//
// THE TWO HALVES, AND WHY THE SECOND ONE IS THE AWKWARD ONE
// ---------------------------------------------------------
// Adding the row is a push onto the category list's `entryList` followed by `InvalidateData()`.
//
// Making it DO something is not, because `onCategoryListItemPress` is a compiled switch inside the
// SWF: a row added at runtime falls straight through to its `default:` branch, so it draws, plays
// the cancel sound, and does nothing at all. The handler is therefore WRAPPED - the original is
// read out and stashed under a backup name, ours is installed in its place, and every index except
// our own row's is delegated straight back to the original. Delegating by path keeps `this` bound
// to the page, which is what the original expects.
//
// Everything here is best-effort and loudly logged. A menu whose structure we do not recognise
// (a replacer that rebuilt the hierarchy, or a future game patch) gets no row and a warning - it
// never gets a half-installed handler, because the wrap is only installed once the list is found.
// ================================================================================================

namespace systemrow
{
	// Registers the menu sink. Safe to call more than once; returns false only when the UI
	// singleton is not ready yet, so the caller can retry at a later SKSE message.
	bool Install();

	// True once the row has been added to a live journal at least once - the DevBench tool
	// reports this so the feature can be checked without a person watching the screen.
	bool WasInjected();

	// The path the row was found under, or "" if it has not been found yet. Logged and reported
	// rather than assumed, because it is the thing that differs between art replacers.
	const char* FoundPath();
}
