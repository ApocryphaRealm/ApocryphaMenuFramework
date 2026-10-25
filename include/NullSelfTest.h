#pragma once

// ============================================================================================================
// The DevBench null self-test (2.3.0; plan 4. plans\amf-null-checks\PLAN.md section 6, rule 64): amf.menu op=nulltest
// passes nulls to a handful of the framework's exports - cimgui (NullSelfTestCore.h), SKSE Menu Framework's API and AMF's
// own C API - on the render thread, and returns JSON listing each call, what was null, what the guard promises and what
// came back. Every call is a no-op by design (nothing is registered, drawn or written), so the op is harmless in a
// shipped build; like every amf.menu op it exists only where DevBench is installed. Each call is logged at info.
// ============================================================================================================

#include <string>

namespace nullselftest
{
	// Render thread, every frame, right after ImGui::NewFrame (Renderer.cpp). Does nothing unless a run is waiting.
	void Frame();

	// DevBench's listener thread: asks the render thread for one run and waits up to a_timeoutMs for its JSON.
	std::string RunJson(unsigned a_timeoutMs);
}
