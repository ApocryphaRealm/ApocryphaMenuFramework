#pragma once

#include <cstdint>
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
	void SetMenuVisible(bool a_visible);
	void SetSelectedNode(const std::string& a_node);
	std::string GetSelectedNode();
	void ActivateSelectedNode();
	std::string GetMenuStateJson();

	// HUD widgets (1.4.4). DrawHudWidgets runs every registered callback once per frame; the Hud*
	// primitives are the C exports' implementation and only draw while a callback is running on
	// the render thread (called anywhere else they log once and do nothing).
	void  DrawHudWidgets();

	// In-process CAPTURE (1.4.4): saves the backbuffer to a PNG right after the framework's overlay
	// is rendered, so the image contains what the player sees INCLUDING every ImGui element. The
	// game's own screenshot is taken before the Present hook and can never show the overlay.
	// Callable from any thread; blocks until the render thread has written the file or the timeout
	// passes. Returns an empty string on success, otherwise the reason.
	std::string CaptureBlocking(const std::wstring& a_path, unsigned a_timeoutMs);
	void  HudScreenSize(float* a_w, float* a_h);
	void  HudLine(float x1, float y1, float x2, float y2, std::uint32_t c, float t);
	void  HudCircle(float cx, float cy, float r, std::uint32_t c, float t, int seg);
	void  HudCircleFilled(float cx, float cy, float r, std::uint32_t c, int seg);
	void  HudTriangleFilled(float x1, float y1, float x2, float y2, float x3, float y3, std::uint32_t c);
	void  HudRect(float x1, float y1, float x2, float y2, std::uint32_t c, float t);
	void  HudText(float x, float y, std::uint32_t c, const char* text, float size);
	float HudTextWidth(const char* text, float size);
}
