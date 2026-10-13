#pragma once

// ============================================================================================
// OTHER MODS' OWN MENUS - THE LAUNCHER (2.2.0; the owner, 2026-10-12: "just add a row in AMF for other menus so that AMF can
// take over their hotkeys to call them and close automatically" - 4. plans\amf-imgui-hosting\FINDINGS.md method c).
// Framework > Other menus lists each supported mod that is loaded, with Open / Close and what is known of its state:
//   Open Animation Replacer   through its own API (RequestPluginAPI_UI, interface V3: OpenMenu / CloseMenu / IsMenuOpen -
//                             include\OAR\OpenAnimationReplacerAPI-UI.h). OAR 3.0.2 (the one installed in Njordlinger) has
//                             only V2, which has no open or close: there it is opened by pressing its key, like the others.
//   Community Shaders, Immersive Equipment Displays, OptiScaler, ENB
//                             by pressing the key each one reads from its own config (HostCore.h says where) with SendInput -
//                             only ever from a press of AMF's own button (or the DevBench op), and only while the game is the
//                             foreground window, so a key never lands in another program. ENB is found from its files only
//                             and its editor key read from enblocal.ini: its licence forbids reverse engineering it, and a
//                             key press is what a player does.
// Pressing Open always closes AMF's own menu first, so the mod's menu has the mouse, the keys and the screen and nothing of
// AMF is behind it, then opens it a couple of frames later (each key of a combo on its own frame: the game reads its keyboard
// once a frame). With OAR's API, AMF opens again on this page when OAR's menu closes. And a mod's own key pressed while AMF
// is open closes AMF, so that mod's menu is shown alone (AMF takes over their hotkeys).
//
// Live hosting of another mod's menu inside AMF's page (AMF-Host-OAR) and the Performance page were taken out of 2.2.0 by
// the owner's decision of 2026-10-12; that work is kept on the repository's imgui-hosting branch.
//
// AMF HOLDS THIS KEY (the owner, 2026-10-12: "make it so those menus' hotkey is activated from within AMF, so AMF calls them
// and no hotkey is needed - using AMF will free up hotkeys"). A switch on each row that presses a key (all but OAR 3.2.1+,
// which is opened through its API): on, AMF writes a key no keyboard has (F13-F17, one per mod - HostCore.h) into the mod's
// OWN config in place of its menu key, keeping the file as it was once as <config>.amf-key-backup, and Open presses that
// key; off writes the original value back from the backup. Off by default: it changes another mod's file, so it is the
// player's choice. The switch shows what the config says; a mod reads its config at start-up, so until the next launch
// AMF keeps pressing (and watching) the key the mod started with.
//
// Settings > Converted menus > ImGui menus: one switch per mod ([ImGuiMenus] b<Mod>).
// DevBench amf.menu op=imgui: do = list (default) | open {id} | close {id} | hold {id, on}; ids oar, cs, ied, optiscaler, enb.
// Nothing here acts while the loading guard holds (LoadGuard.h).
// ============================================================================================

#include <string>

namespace imguihost
{
	// kDataLoaded (every DLL is loaded): find the mods, ask OAR for its API. Safe to call more than once.
	void Load();

	// True when Framework > Other menus has at least one row to show (a supported mod loaded, its switch on).
	bool AnyShown();

	// Framework > Other menus (the launcher rows).
	void DrawPage();

	// Settings > Converted menus > ImGui menus: a switch per mod (the caller draws the sub-tab).
	void DrawSettings();

	// Render thread, once a frame after AMF's own drawing: the queued key presses, OAR's state, the mods' own keys.
	void Frame();

	// DevBench amf.menu op=imgui (listener thread; open / close run on the render thread).
	std::string ToolJson(const std::string& a_argsJson);
}
