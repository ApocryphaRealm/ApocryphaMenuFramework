#pragma once

// ============================================================================================
// OTHER MODS' OWN IMGUI MENUS (next version after 2.1.9; the owner, 2026-10-12: "host other mods' live ImGui menus inside
// AMF rather than converting them" - 4. plans\amf-imgui-hosting\FINDINGS.md). Phase 1 is the LAUNCHER (FINDINGS method c):
// Framework > Other menus lists each supported mod that is loaded, with Open / Close and what is known of its state.
//
//   Open Animation Replacer   through its own API (RequestPluginAPI_UI, interface V3: OpenMenu / CloseMenu / IsMenuOpen -
//                             include\OAR\OpenAnimationReplacerAPI-UI.h). OAR 3.0.2 (the one installed in Njordlinger) has
//                             only V2, which has no open or close: there it is opened by pressing its key, like the others.
//   Community Shaders, Immersive Equipment Displays, OptiScaler
//                             by pressing the key each one reads from its own config (HostCore.h says where) with SendInput -
//                             only ever from a press of AMF's own button (or the DevBench op), and only while the game is the
//                             foreground window, so a key never lands in another program.
//   ENB                       none: its licence forbids hooking and reading it.
//
// Pressing Open closes AMF's own menu first, so the mod's menu gets the mouse, the keys and the screen, then opens it a
// couple of frames later (each key of a combo on its own frame: the game reads its keyboard once a frame). With OAR's API,
// AMF opens again on this page when OAR's menu closes.
//
// Settings > Converted menus > ImGui menus: one switch per mod ([ImGuiMenus] b<Mod>), its row shown or not.
// DevBench amf.menu op=imgui: do = list (default) | open {id} | close {id}; ids oar, cs, ied, optiscaler.
// ============================================================================================

#include <string>

namespace imguihost
{
	// kPostLoad (every DLL is loaded): find the mods, ask OAR for its API. Safe to call more than once.
	void Load();

	// True when Framework > Other menus has at least one row to show (a supported mod loaded, its switch on).
	bool AnyShown();

	// Framework > Other menus.
	void DrawPage();

	// Settings > Converted menus > ImGui menus: a switch per mod (the caller draws the sub-tab).
	void DrawSettings();

	// Render thread, once a frame after AMF's own drawing: the queued key presses and OAR's state.
	void Frame();

	// DevBench amf.menu op=imgui (listener thread; open / close run on the render thread).
	std::string ToolJson(const std::string& a_argsJson);
}
