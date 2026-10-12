#pragma once

// ============================================================================================
// OTHER MODS' OWN IMGUI MENUS (next version after 2.1.9; the owner, 2026-10-12: "host other mods' live ImGui menus inside
// AMF rather than converting them" - 4. plans\amf-imgui-hosting\FINDINGS.md).
//
// PHASE 1 - the LAUNCHER (FINDINGS method c): Framework > Other menus lists each supported mod that is loaded, with Open /
// Close and what is known of its state.
//   Open Animation Replacer   through its own API (RequestPluginAPI_UI, interface V3: OpenMenu / CloseMenu / IsMenuOpen -
//                             include\OAR\OpenAnimationReplacerAPI-UI.h). OAR 3.0.2 (the one installed in Njordlinger) has
//                             only V2, which has no open or close: there it is opened by pressing its key, like the others.
//   Community Shaders, Immersive Equipment Displays, OptiScaler
//                             by pressing the key each one reads from its own config (HostCore.h says where) with SendInput -
//                             only ever from a press of AMF's own button (or the DevBench op), and only while the game is the
//                             foreground window, so a key never lands in another program.
//   ENB                       none: its licence forbids hooking and reading it.
// Pressing Open closes AMF's own menu first, so the mod's menu gets the mouse, the keys and the screen, then opens it a
// couple of frames later (each key of a combo on its own frame: the game reads its keyboard once a frame). With OAR's API,
// AMF opens again on this page when OAR's menu closes.
//
// PHASE 2 - LIVE HOSTING of Open Animation Replacer (FINDINGS method b1, "snap-docking"). With AMF-Host-OAR.dll installed
// (a separate GPL DLL built against OAR's own Dear ImGui 1.89.6 - bridges\oar), OAR's Open on this page keeps AMF open and
// turns the page into a HOSTED PAGE: AMF's frame with an empty content rectangle. Each frame AMF hands the bridge that
// rectangle (AMF_RegisterHostBridge, AMF/API.h); the bridge, between OAR's frames, moves OAR's main window into it. OAR
// draws its real menu there, on top of AMF (its present hook runs after AMF's). OAR swallows the game's input while its menu
// is open, so AMF's input hook gets nothing: while it is open AMF drives its own frame from the OS instead (GetCursorPos,
// GetAsyncKeyState - the pointer, the mouse buttons and AMF's menu key; a click that OAR's ImGui wants is left to OAR), hides
// its own cursor (OAR draws one) and lets go of any text field. Leaving the page, closing AMF or pressing Close closes OAR;
// OAR closing itself (Escape) ends the hosting. The bridge checks OAR's build and context first and every frame (FINDINGS
// section 5); any failure reported by it stops hosting for the session and Open falls back to the launcher.
//
// Settings > Converted menus > ImGui menus: one switch per mod ([ImGuiMenus] b<Mod>) and the hosting switch
// ([ImGuiMenus] bHostOpenAnimationReplacer).
// DevBench amf.menu op=imgui: do = list (default) | open {id} | close {id}; ids oar, cs, ied, optiscaler; list carries the
// bridge and hosting state.
// ============================================================================================

#include "AMF/API.h"

#include <cstdint>
#include <string>

namespace imguihost
{
	// kDataLoaded (every DLL is loaded): find the mods, ask OAR for its API. Safe to call more than once.
	void Load();

	// True when Framework > Other menus has at least one row to show (a supported mod loaded, its switch on).
	bool AnyShown();

	// Framework > Other menus (the launcher rows, or the hosted page while OAR is hosted).
	void DrawPage();

	// Settings > Converted menus > ImGui menus: a switch per mod and the hosting switch (the caller draws the sub-tab).
	void DrawSettings();

	// Render thread, once a frame after AMF's own drawing: the queued key presses, OAR's state, the bridge's call.
	void Frame();

	// Render thread, before AMF feeds its queued input to ImGui. While a hosted menu is open (it swallows the game's input
	// before AMF's hook sees any), queues AMF's pointer, mouse buttons and menu key from the OS. True when it is doing so -
	// the caller then hides its own cursor and lets go of a text field.
	bool PollOsInput();

	// The bridge's surface (AMF_RegisterHostBridge / AMF_HostBridgeFailed in main.cpp). Any thread.
	bool RegisterBridge(const char* a_target, std::uint32_t a_api, AMF_HostFrameCallback a_callback, void* a_user);
	void BridgeFailed(const char* a_target, const char* a_reason);

	// DevBench amf.menu op=imgui (listener thread; open / close run on the render thread).
	std::string ToolJson(const std::string& a_argsJson);
}
