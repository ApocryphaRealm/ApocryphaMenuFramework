#pragma once

#include <string>

// ============================================================================================
// M1: the render loop. Two trampoline call-hooks (survey §7.1), a probed D3D-init site
// (Offsets.h explains the dispute), one ImGui context for the whole process.
//
// Hook installation happens in SKSEPluginLoad because renderer bring-up PRECEDES kDataLoaded -
// there is no later moment that still catches init. This is the established pattern across the
// surveyed ecosystem. It is in deliberate tension with this project's DEM crash lesson (avoid
// early relocation work); the mitigations are the byte-pattern guards - a site that does not
// look like a call instruction is never written, the framework logs why and stays inert, and
// the game boots untouched.
// ============================================================================================

namespace renderer
{
	// Installs the D3D-init and present hooks. Returns false (after logging exactly what did
	// not match) when any pattern guard refuses; the plugin then loads inert rather than crashing.
	bool Install();

	// The framework window's visibility toggle - flipped by the M1 input sink, consumed by the
	// present thunk. Atomic: touched from the input thread, read on the render thread.
	void ToggleMainWindow();
	bool IsMainWindowVisible();

	// External menu control + query, used by the DevBench tool (DevBenchTool.cpp) so the menu can
	// be driven and inspected headlessly for testing (rule 31): open/close, move the selection to a
	// node path (e.g. "system/quit", "mod" with the mod set via the tool, "stats"), run the
	// selected node's action, and read the whole menu as JSON. Thread-safe.
	// a_nested says the surface was opened from the row in the GAME's own System menu rather than
	// from the hotkey or a menu launcher. It only affects GEOMETRY: a nested window is sized and
	// placed to the journal panel hosting it, so it reads as a page of that menu instead of a
	// larger window on top of it. Everything it draws is identical either way.
	void SetMenuVisible(bool a_visible, bool a_nested = false);
	void SetSelectedNode(const std::string& a_node);
	std::string GetSelectedNode();
	void ActivateSelectedNode();
	// Menu-shell personalization, driven from DevBench for testing (the settings page drives the
	// same personalization:: calls directly). Return false when no such mod is registered.
	// Switch the active theme by registry id (e.g. "skyrim", "untarnished") and
	// save it. Exposed for DevBench so a visual comparison of two themes can be photographed in
	// ONE game session instead of one launch per theme. Returns false for an unknown id.
	bool SetTheme(const std::string& a_themeId);

	bool SetModAlias(const std::string& a_modName, const std::string& a_alias);
	bool MoveModTo(const std::string& a_modName, int a_position);
	void ResetModOrder();

	std::string GetMenuStateJson();

	// In-process capture (ported from the Overhaul line, 2026-08-30): saves the NEXT presented
	// frame - WITH the ImGui overlay - as a PNG at a_path. Blocks the calling (DevBench listener)
	// thread until the render thread serviced it or a_timeoutMs passed. Empty return = success.
	std::string CaptureBlocking(const std::wstring& a_path, unsigned a_timeoutMs);
}
