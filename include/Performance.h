#pragma once

// ============================================================================================
// AMF'S OWN PERFORMANCE GRAPH (next version after 2.1.9; FINDINGS.md section 8: a native FPS / frame-time graph is the
// low-risk way to give the owner a performance graph without hosting OptiScaler's). AMF measures it itself: the time
// between two calls of its present hook - one per frame the game shows - kept for the last 240 frames
// (hostcore::FrameTimes). Shown on the Framework > Performance page, and, with its switch on ([Performance] bOverlay,
// off by default), as a small graph in a corner of the screen whether or not the menu is open - the corner is chosen on the
// page ([Performance] iCorner, bottom-right by default since v6: top-right sat on the minimap). It is AMF's measurement,
// not OptiScaler's or any other tool's.
// ============================================================================================

#include <string>

namespace perf
{
	// Render thread, first thing in each present-hook call: one frame time since the last call.
	void NoteFrame();

	// Framework > Performance (the page in AMF's window).
	void DrawPage();

	// Each frame after ImGui::NewFrame: the corner graph when its switch is on (never takes input or focus).
	void DrawOverlay();

	// DevBench: {"fps":{"min","avg","max"},"frameMs":{"min","avg","max"},"frames":N,"overlay":bool,"corner":0-3} - any thread.
	std::string StateJson();
}
