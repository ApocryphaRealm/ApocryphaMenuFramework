#pragma once

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
}
