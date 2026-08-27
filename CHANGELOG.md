# ApocryphaMenuFramework - changelog

Written as changes happen, not reconstructed afterwards (rule 61). Each version carries its
**version-ledger status**, so this file cannot quietly claim more than the ledger does:

* **working** - observed running in game
* **untested** - built and packaged, not yet confirmed
* **failed** - built but crashed or malfunctioned; the number was reclaimed
* **scratch** - a hypothesis-test build that never held a real number

## 1.1.2 - 2026-08-27 - untested

### Changed
- Window placement moved from free dragging to PRESET positions (the author: "preset positions, just like the minimap... standard position is in the center"). Position is centre-anchored from the display centre every frame (resolution-independent, ImGuiWindowFlags_NoMove); uWindowPreset INI key reserved (0 = centre) for the preset list to be worked out later. Size remains user-adjustable.

## 1.1.1 - 2026-08-27 - untested

### Fixed
- Software cursor flicker/jump/teleport (the author's 1.1.0 in-game report, suspected DPI): actually a two-source fight. The Win32 backend's fallback poll pushed the OS cursor position (which the game recentres at will) into ImGui's event queue every frame, while ours was only emitted on movement - so still frames teleported to the OS position, and event trickling let the two sources alternate across frames. Now the integrated position is emitted unconditionally every frame AFTER the backend (last writer wins) and ConfigInputTrickleEventQueue is off so all sources resolve within a single frame. DPI scaling stops mattering because the OS-space position never wins again.

## 1.1.0 - 2026-08-27 - untested

### Added
- M2 input capture (the user-validated top priority). One pattern-guarded call-hook at BSInputDeviceManager::PollInputDevices (SE 67315 / AE 68617 + 0x7B, twice-corroborated MIT prior art). While the menu is open: mouse movement, wheel, presses and thumbsticks are consumed - halting the camera, the scroll-zoom and movement - while button RELEASES pass through so a key held across the open transition can never stick down. Events are queued on the input thread and translated to ImGui on the render thread (1.87+ io.Add*Event API), with a software cursor integrated from mouse deltas. Escape closes the menu; the toggle key is handled inside the hook (the M1 event sink is retired).
- Framework settings page - the window's first real content, in the SMF two-pane structure the author specified (left pane lists menus - the framework itself is the only entry until M3's registry - right pane shows the selected page). Settings: explicit keyboard/controller input-mode toggle switch (the standing first-setting decision; wired live to ImGui nav flags), a Text size slider (live-applied), and the toggle-key readout. Settings persist to Data/SKSE/Plugins/ApocryphaMenuFramework.ini via plain file I/O (never the profile API); compiled defaults match the shipped INI exactly; uLogLevel honoured (trace default).
- Window now displays its own version string, sourced from the single CMake-declared version (the author read 1.0.2's version-less status text as a stale build; AMF_GetVersionString also stops hand-maintaining a literal, which had already drifted).

## 1.0.2 - 2026-08-27 - untested

### Changed
- Text 30% larger relative to the resolution scale (FontGlobalScale = uiScale * 1.30; widget geometry keeps the unboosted scale) and default window grown from 45%x60% to 55%x70% of the display - the author's 1.0.1 in-game feedback: "I want bigger text relative to the current size. And I want the overall size to be bigger."

## 1.0.1 - 2026-08-27 - untested

### Changed
- Default window size is now display-proportional (45% width x 60% height, FirstUseEver) instead of a fixed 520x340 box scaled by g_uiScale - the author's M1.1 in-game feedback: centred and bigger confirmed working, but "I want it to match the same size as the original" (SMF's large menu window). The exact proportion is a first calibration to be tuned against his next look.
- ImGui layout persistence moved from the default imgui.ini in the game CWD to a plugin-owned Data/SKSE/Plugins/ApocryphaMenuFramework_layout.ini (routed into MO2 overwrite by the VFS). Also guarantees the new default size isn't shadowed by previously saved geometry.

## 1.0.0 - 2026-08-27 - untested

### Added
- M1 render loop: ImGui embedded via the vcpkg port (dx11+win32 binding features), present hook on the 18-repo-corroborated site, and the DISPUTED D3D-init offset resolved by probing both candidates behind byte-pattern guards at runtime - the winner is logged per runtime. Full theme applied (true black, #F5F2E9, borders on every element, readable TextDisabled); game HUD opacity re-read per frame as one global multiplier; K toggles a display-only window. Every guard fails toward loaded-but-inert with the reason logged, never toward a crash. Corrections this milestone: vcpkg installs imgui backend headers FLAT (not backends/); CommonLibSSE-NG renamed BSRenderManager to BSGraphics::Renderer leaving a zero-byte tombstone header.
- M0 scaffold: original framework (not an SMF fork - licence rails in plan.md), CommonLibSSE-NG dual-runtime plugin skeleton, public C API with SMF_GetReservedKeyCodes as the first export, theme constants (true black / #F5F2E9 / borders everywhere) and the in-game-proven fHUDOpacity resolver ported from DEM. Rendering, input and the page registry are M1-M3.

### Changed
- Default window position is now centre-relative (display centre, centre pivot) - the same resolution-independence principle as LMU's map border, per the author after seeing the 16:35 capture where the unscaled window sat as a sliver in the top-left at 3200x1800. FirstUseEver, so player-moved windows keep their arrangement.
- Menu toggle moved from K to F1 (0x3B), decided during the first in-game smoke test: K collided with Dragon's Eye Minimap's rule-28 default the moment both ran, and F1 matches the established framework convention (SMF). Reserved-keys export updated to report F1. Rule-28 K/L defaults now read as mod-scoped; the framework is the arbiter and takes F1.
### Fixed
- M1.1, from the first smoke test: resolution-aware UI scaling. Font, style metrics and the default window size now scale by displayHeight/1080 (1.67x at 1800p), fixing the far-too-small window the author reported. Camera-still-moves is NOT fixed here - input capture is M2 and now its user-validated top priority.