// Off-game check of AMF-Host-OAR's frame logic (bridges\oar\source\HostLogic.h - the same code the bridge runs) against a
// REAL Dear ImGui 1.89.6 context, built from the bridge's own vcpkg tree (Open Animation Replacer's pin). A stand-in for
// OAR's main window is drawn the way OAR 3.0.2 draws it (UIMain.cpp / UIWindow.cpp at db3cf0b: SetNextWindowPos and
// SetNextWindowSize with ImGuiCond_FirstUseEver, size constraints, Begin("Open Animation Replacer 3.0.2", &open,
// NoCollapse)), and OAR's early-out is copied: no frame at all while none of its windows shows.
//   tools\host_oar_check.bat   (bridges\oar\build.bat must have configured build\se once)
// Prints one line per check and "host_oar_check: N of N passed"; exits 1 on any failure.
#include "../bridges/oar/source/HostLogic.h"

#include <Windows.h>
#include <Psapi.h>

#include <cstddef>
#include <cstdio>
#include <iostream>
#include <string>

namespace
{
	int g_pass = 0, g_fail = 0;
	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::cout << (a_ok ? "PASS " : "FAIL ") << a_what << "\n";
	}

	const char* const kRenderer = "imgui_impl_dx11";   // a literal in this exe's image, as OAR's is in OAR's
	const std::string kTitle = "Open Animation Replacer 3.0.2";
	bool g_show = true;

	// One of OAR's frames: nothing at all while its window is hidden (UIManager::Render's early-out)
	void OarFrame(ImGuiContext* a_ctx, bool a_dragTo = false, ImVec2 a_drag = {})
	{
		if (!g_show) { return; }
		ImGui::SetCurrentContext(a_ctx);
		ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
		ImGui::NewFrame();
		const ImGuiIO& io = ImGui::GetIO();
		ImGui::SetNextWindowPos(ImVec2(20.0f, io.DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.0f, 0.5f));
		ImGui::SetNextWindowSize(ImVec2(850.0f, io.DisplaySize.y), ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 200.0f), ImVec2(io.DisplaySize.x, io.DisplaySize.y - 40.0f));
		if (ImGui::Begin(kTitle.c_str(), &g_show, ImGuiWindowFlags_NoCollapse))
		{
			ImGui::TextUnformatted("replacer list");
			if (a_dragTo) { ImGui::SetWindowPos(a_drag); }   // the player dragging it
		}
		ImGui::End();
		ImGui::Render();
	}

	AMF_HostFrame MakeFrame(bool a_hosting, float a_x, float a_y, float a_w, float a_h)
	{
		AMF_HostFrame f{};
		f.size = sizeof(f);
		f.hosting = a_hosting ? 1 : 0;
		f.x = a_x;
		f.y = a_y;
		f.w = a_w;
		f.h = a_h;
		f.displayW = 1920.0f;
		f.displayH = 1080.0f;
		f.targetOpen = -1;
		f.renderThread = 4936;
		return f;
	}

	// v6: the render-thread check (HostLogic.h CheckThread) - the v5 in-game failure replayed. Skyrim SE 1.5.97 drew its first
	// frames on thread 4936, which then ended; present carried on on 42524. v5 pinned the first caller and failed closed.
	void ThreadChecks(ImGuiContext* a_oar, std::uintptr_t a_base, std::uintptr_t a_end)
	{
		hostlogic::State s;
		hostlogic::Result r;
		AMF_HostFrame f = MakeFrame(true, 600.0f, 100.0f, 800.0f, 700.0f);
		f.renderThread = 4936;
		Check(hostlogic::CheckThread(&f, 4936, s, r) && r.fail.empty() && r.info.empty(), "thread: first frames on 4936, AMF names 4936 - passes");
		r = {};
		f.renderThread = 42524;
		const bool moved = hostlogic::CheckThread(&f, 42524, s, r);
		Check(moved && r.fail.empty() && r.info.size() == 1, "thread: present moves to 42524 and AMF names 42524 - passes, one 'moved' line");
		r = {};
		Check(hostlogic::CheckThread(&f, 42524, s, r) && r.info.empty(), "thread: the frames after on 42524 - passes, no more lines");
		// the whole frame on the new thread still places OAR's window
		{
			ImGuiContext* other = ImGui::GetCurrentContext();
			ImGui::SetCurrentContext(a_oar);
			ImGui::GetIO().DeltaTime = 1.0f / 60.0f;
			ImGui::NewFrame();
			ImGui::Begin(kTitle.c_str(), nullptr, ImGuiWindowFlags_NoCollapse);
			ImGui::End();
			ImGui::Render();
			ImGui::SetCurrentContext(other);
			hostlogic::State fs;
			const auto fr = hostlogic::Frame(a_oar, &f, fs, a_base, a_end, kTitle);
			Check(fr.fail.empty() && f.placed == 1, "thread: after the move the frame is hosted (placed)");
		}
		// fail closed
		r = {};
		f.renderThread = 4936;
		const bool offThread = hostlogic::CheckThread(&f, 42524, s, r);
		Check(!offThread && !r.fail.empty(), "thread: called on 42524 while AMF names 4936 - fails closed (" + r.fail + ")");
		r = {};
		f.renderThread = 0;
		Check(!hostlogic::CheckThread(&f, 42524, s, r) && !r.fail.empty(), "thread: AMF names no thread (0) - fails closed");
		r = {};
		AMF_HostFrame old = MakeFrame(true, 600.0f, 100.0f, 800.0f, 700.0f);
		old.size = static_cast<std::uint32_t>(offsetof(AMF_HostFrame, renderThread));   // v5's record, before the field
		Check(!hostlogic::CheckThread(&old, 4936, s, r) && !r.fail.empty(), "thread: v5 AMF's shorter frame record - fails closed");
	}
}

int main()
{
	MODULEINFO mi{};
	GetModuleInformation(GetCurrentProcess(), GetModuleHandleW(nullptr), &mi, sizeof(mi));
	const auto base = reinterpret_cast<std::uintptr_t>(mi.lpBaseOfDll);
	const auto end = base + mi.SizeOfImage;

	Check(std::string(IMGUI_VERSION) == "1.89.6", std::string("built against Dear ImGui ") + IMGUI_VERSION + " (OAR's pin 1.89.6)");
#ifdef IMGUI_HAS_DOCK
	Check(false, "no docking branch (OAR's vcpkg imgui has no docking-experimental)");
#else
	Check(true, "no docking branch (OAR's vcpkg imgui has no docking-experimental)");
#endif

	ImGuiContext* oar = ImGui::CreateContext();
	ImGuiContext* other = ImGui::CreateContext();   // stands for the bridge's own current context
	ImGui::SetCurrentContext(oar);
	{
		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2(1920.0f, 1080.0f);
		io.IniFilename = nullptr;
		io.BackendRendererName = kRenderer;
		unsigned char* px = nullptr;
		int w = 0, h = 0;
		io.Fonts->GetTexDataAsRGBA32(&px, &w, &h);
	}
	ImGui::SetCurrentContext(other);

	hostlogic::State st;
	AMF_HostFrame f = MakeFrame(false, 0, 0, 0, 0);
	auto r = hostlogic::Frame(oar, &f, st, base, end, kTitle);
	Check(r.fail.empty() && f.targetOpen == 0, "before OAR has drawn its window: closed, no failure");
	Check(ImGui::GetCurrentContext() == other, "the caller's own context is put back");

	OarFrame(oar);
	ImGui::SetCurrentContext(other);
	f = MakeFrame(false, 0, 0, 0, 0);
	r = hostlogic::Frame(oar, &f, st, base, end, kTitle);
	ImGuiWindow* win = nullptr;
	{
		ImGui::SetCurrentContext(oar);
		win = ImGui::FindWindowByName(kTitle.c_str());
		ImGui::SetCurrentContext(other);
	}
	Check(r.fail.empty() && f.targetOpen == 1 && win, "after a frame with its window: open");
	const ImVec2 ownPos = win->Pos, ownSize = win->SizeFull;
	std::printf("     OAR's own placement: (%.0f, %.0f) %.0f x %.0f\n", ownPos.x, ownPos.y, ownSize.x, ownSize.y);

	// hosting: placed into the rectangle, and OAR's next frame keeps it there (its FirstUseEver does not move it back)
	f = MakeFrame(true, 600.0f, 100.0f, 800.0f, 700.0f);
	r = hostlogic::Frame(oar, &f, st, base, end, kTitle);
	Check(r.fail.empty() && f.placed == 1, "hosting: placed this frame");
	OarFrame(oar);
	Check(win->Pos.x == 600.0f && win->Pos.y == 100.0f && win->Size.x == 800.0f && win->Size.y == 700.0f,
		  "OAR's next frame draws its window at AMF's rectangle (600, 100) 800 x 700, got (" + std::to_string(win->Pos.x) + ", " +
			  std::to_string(win->Pos.y) + ") " + std::to_string(win->Size.x) + " x " + std::to_string(win->Size.y));
	// the player drags it inside its frame: the next call snaps it back
	OarFrame(oar, true, ImVec2(50.0f, 50.0f));
	f = MakeFrame(true, 600.0f, 100.0f, 800.0f, 700.0f);
	r = hostlogic::Frame(oar, &f, st, base, end, kTitle);
	OarFrame(oar);
	Check(win->Pos.x == 600.0f && win->Pos.y == 100.0f, "a drag inside OAR's frame is snapped back the next frame");
	// AMF's window resized: the rectangle follows
	f = MakeFrame(true, 500.0f, 80.0f, 1000.0f, 900.0f);
	r = hostlogic::Frame(oar, &f, st, base, end, kTitle);
	OarFrame(oar);
	Check(win->Pos.x == 500.0f && win->Size.x == 1000.0f && win->Size.y == 900.0f, "a new rectangle is followed the next frame");
	// smaller than OAR's own minimum: OAR's constraint wins (300 x 200), still placed
	f = MakeFrame(true, 500.0f, 80.0f, 120.0f, 120.0f);
	r = hostlogic::Frame(oar, &f, st, base, end, kTitle);
	OarFrame(oar);
	Check(f.placed == 1 && win->Size.x == 300.0f && win->Size.y == 200.0f, "a rectangle under OAR's minimum is held at OAR's 300 x 200");
	// a rectangle past OAR's display is not placed (warned once, no failure)
	f = MakeFrame(true, 1800.0f, 80.0f, 800.0f, 700.0f);
	r = hostlogic::Frame(oar, &f, st, base, end, kTitle);
	Check(r.fail.empty() && f.placed == 0 && r.warn.size() == 1, "a rectangle past OAR's display: not placed, one warning, no failure");
	// hosting ends: OAR's window goes back where it was
	f = MakeFrame(false, 0, 0, 0, 0);
	r = hostlogic::Frame(oar, &f, st, base, end, kTitle);
	OarFrame(oar);
	Check(win->Pos.x == ownPos.x && win->Pos.y == ownPos.y && win->SizeFull.x == ownSize.x && win->SizeFull.y == ownSize.y,
		  "hosting ended: OAR's window is back at its own place and size");

	// OAR closes its menu (Escape): no frame is drawn any more -> closed. The bridge is called every frame in the game, so
	// it sees the last drawn frame first (still open), then the frame OAR skipped.
	OarFrame(oar);
	f = MakeFrame(true, 600.0f, 100.0f, 800.0f, 700.0f);
	r = hostlogic::Frame(oar, &f, st, base, end, kTitle);
	Check(f.targetOpen == 1, "the frame before it closes: still open");
	g_show = false;
	OarFrame(oar);
	f = MakeFrame(true, 600.0f, 100.0f, 800.0f, 700.0f);
	r = hostlogic::Frame(oar, &f, st, base, end, kTitle);
	Check(r.fail.empty() && f.targetOpen == 0, "OAR's menu closed (no frame drawn): closed");
	g_show = true;

	// another OAR version's title is not found: closed, nothing touched
	{
		hostlogic::State s2;
		OarFrame(oar);
		AMF_HostFrame g = MakeFrame(true, 600.0f, 100.0f, 800.0f, 700.0f);
		r = hostlogic::Frame(oar, &g, s2, base, end, "Open Animation Replacer 3.2.1");
		Check(r.fail.empty() && g.targetOpen == 0 && g.placed == 0, "another version's title: not found, nothing placed");
	}

	// FAIL CLOSED
	{
		hostlogic::State s3;
		ImGui::SetCurrentContext(oar);
		ImGui::NewFrame();
		AMF_HostFrame g = MakeFrame(true, 600.0f, 100.0f, 800.0f, 700.0f);
		r = hostlogic::Frame(oar, &g, s3, base, end, kTitle);
		ImGui::Render();
		Check(!r.fail.empty() && g.placed == 0, "called inside OAR's NewFrame -> Render: fails closed (" + r.fail + ")");
	}
	{
		hostlogic::State s4;
		ImGui::SetCurrentContext(oar);
		char* heapName = new char[32];   // the right text, but on the heap - not inside OAR's image (a short std::string would sit in the image)
		std::snprintf(heapName, 32, "%s", "imgui_impl_dx11");
		ImGui::GetIO().BackendRendererName = heapName;
		AMF_HostFrame g = MakeFrame(true, 600.0f, 100.0f, 800.0f, 700.0f);
		r = hostlogic::Frame(oar, &g, s4, base, end, kTitle);
		Check(!r.fail.empty() && g.placed == 0, "a renderer name outside OAR's image: fails closed");
		ImGui::GetIO().BackendRendererName = "imgui_impl_dx12";
		r = hostlogic::Frame(oar, &g, s4, base, end, kTitle);
		Check(!r.fail.empty(), "another renderer name: fails closed");
		ImGui::GetIO().BackendRendererName = kRenderer;
	}

	ImGui::SetCurrentContext(other);
	ThreadChecks(oar, base, end);

	ImGui::DestroyContext(oar);
	ImGui::DestroyContext(other);
	std::cout << "host_oar_check: " << g_pass << " of " << (g_pass + g_fail) << " passed\n";
	return g_fail == 0 ? 0 : 1;
}
