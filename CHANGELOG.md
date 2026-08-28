# ApocryphaMenuFramework - changelog

Written as changes happen, not reconstructed afterwards (rule 61). Each version carries its
**version-ledger status**, so this file cannot quietly claim more than the ledger does:

* **working** - observed running in game
* **untested** - built and packaged, not yet confirmed
* **failed** - built but crashed or malfunctioned; the number was reclaimed
* **scratch** - a hypothesis-test build that never held a real number

## 1.3.5 - 2026-08-28 - untested
### Changed
- REBUILT the "MO2 Skyrim" theme from the real Trosski Skyrim style + a live MO2 screenshot.
  The first port had collapsed the whole style onto one grey (#b0b0b0 for text, border, and
  accents alike). It is now a layered palette: silver frame lines (#b0b0b0), brighter primary
  text (#dddddd), a dim secondary tone (#717171), and a GOLD accent (#a1912b) for selection
  rows, tabs, checkmarks, sliders and nav-highlight. Scrollbars, separators and tab states got
  their own graded tones instead of reusing one alpha ramp.
### Added
- The Nordic KNOTWORK frame that defines the MO2 Skyrim look: the style's border-image.png
  (78x78) is embedded (include/KnotworkBorder.h, RGBA) and drawn as a 9-slice frame around the
  AMF window - four ornate corner knots at fixed size, edges stretched between, transparent
  centre. Uploaded once to a texture on the game's device at init; a new per-theme `knotwork`
  flag gates it (on for MO2 Skyrim, off for Untarnished). Palette gained optional
  border/text/textDim/accent fields that fall back to `frame`, so Untarnished and INI-scanned
  themes are unchanged.

## 1.3.4 - 2026-08-28 - working
### Fixed
- Crash on EVERY successful save load (crash-2026-08-28-10-12-36.log, access violation in
  StripExtension constructing a std::string from address 0x1). Root cause: kPostLoadGame's
  message `data` is a BOOL (1 = load succeeded), not the save-name string the handler assumed;
  it dereferenced (void*)0x1. The earlier "data=0x0" seen on a failed load slipped past the null
  guard because false is 0x0. Fixed by using the correct SKSE payloads: the save name is now
  captured at kPreLoadGame (which really carries it), and kPostLoadGame is read as the bool
  success flag that restores (or, on new game / failure, clears) state.

## 1.3.3 - 2026-08-28 - failed (crashes on save load; number reclaimed by 1.3.4)
### Fixed
- 1.3.2's alias guard compared the module's own filename, but under MO2/usvfs the real DLL and
  the SKSEMenuFramework.dll alias resolve to one file (one module), and the hooked
  GetModuleFileNameW answered with the alias name for BOTH SKSEPlugin_Load calls - so AMF
  refused to load at all ("reported as incompatible during load", both handles). Replaced by a
  process-wide once-only guard (named event): the first SKSEPlugin_Load (always the real
  ApocryphaMenuFramework.dll) initialises, any later call is refused before registering anything.

## 1.3.2 - 2026-08-28 - failed (both loads refused; number reclaimed by 1.3.3)
### Fixed
- Crash on save load (crash-2026-08-28-09-55-35.log, EXCEPTION_ACCESS_VIOLATION in
  SKSEMenuFramework.dll+4C3C during preLoadGame): SKSE loaded this DLL a second time through the
  AMF-MO2-Plugin's SKSEMenuFramework.dll alias; that instance failed Load (hooks refused) but had
  already registered a message listener. SKSEPluginLoad now returns false immediately when the
  module's own filename is the alias - nothing registered, nothing hooked.
- Per-save state files (.amf-state.ini) were written to the real `Saves\` folder while the game
  under MO2 profile-local saves writes to `sLocalSavePath` (`__MO_Saves\`, VFS-mapped to the
  profile). The sibling path now follows `sLocalSavePath:General`, so it lands beside the .ess
  in every configuration.

## 1.3.1 - 2026-08-27 - untested

### Fixed
- kSaveGame/kPostLoadGame message handlers no longer construct a string_view over the message payload unchecked - a real in-game crash occurred during kPostLoadGame dispatch to this plugin while testing the persistence channel (loading a save with ~400 missing masters against a near-empty test mod list; root cause not conclusively isolated since Crash Logger wasn't in the minimal test list, but a null/mismatched data+dataLen is a real possibility this code never guarded against). Both handlers now check data && dataLen>0 before touching the payload, logging and no-oping otherwise, regardless of what the actual cause turns out to be - never dereference an SKSE message payload unchecked.

## 1.3.0 - 2026-08-27 - untested

### Added
- Theme registry (decisions doc S8/S10): AMF's original identity ships as the "Untarnished" theme; a new "MO2 Skyrim" theme, colours read directly from Mod Organizer 2's own real stylesheet (C:\Modlists\Apostasy\stylesheets\Transparent-Style-Skyrim-Trosski.qss - #b0b0b0 dominant grey, solid black background per the project's non-negotiable full-opacity rule), is now the DEFAULT for the current test per the author. Selectable live from the Framework Settings page; additively scans Data/SKSE/Plugins/ApocryphaMenuFramework/themes/*.ini for user-added themes, never overwriting another entry.
- AMF-owned per-save persistence channel (decisions doc S10): hooks kSaveGame/kPostLoadGame (the save's own filename is the message payload), writes/reads a plain-text sibling file next to the save mirroring co-save's SCOPING without its binary format. A shared key-value surface (SetValue/GetValue) any registered mod's page can use. A debug test harness on the Framework Settings page exercises the full round trip (set, save, quit, reload, confirm) without a Papyrus compiler.
- Papyrus native-function binding (decisions doc S3, Path A): AMF_Ping/AMF_SetTestValue/AMF_GetTestValue registered against the game's own Papyrus VM via RE::BSScript::IVirtualMachine::RegisterFunction, proving the native-binding path this project will use for AMF-hosted scripted events instead of embedding a second language.

### Notes
- Priority reset per the author 2026-08-27: other-mod-pipeline work (conversions, the rule-15 verdict backlog) is paused; this version is the direct build-out of the identity/persistence/scripting decisions from the same evening. A dedicated, isolated MO2 test instance (D:\Modlists\AMF-Test) was set up for this and future drastic-change testing, separate from Apostasy/SME.
- Papyrus round-trip verification is PARTIAL, updated: an adversarial sub-agent root-caused the "hang" - the compiler is a .NET Framework 2.0/CLR2 binary and this machine lacks .NET 3.5 (crashes instantly in the native hosting shim before any output is possible; looked like a hang under Start-Process -Wait). AMFTest.psc was successfully compiled to AMFTest.pex via a reflection-based bypass (loading the assembly into an already-running CLR4 host) and deployed into the test instance's Scripts folder. Durable fix recorded for next session: `DISM /Online /Enable-Feature /FeatureName:NetFx3 /All /NoRestart` (needs elevation, not available this session). What remains unverified: actually CALLING AMFTest.RunTest() in game - blocked by a second, unrelated finding: SendKeys cannot navigate Skyrim's own menus (DirectInput, not Win32 messages), so a fresh test instance's main menu could not be advanced past without a real play session.

## 1.2.1 - 2026-08-27 - untested

### Added
- M3 second half: the SMF-compatible export surface is LIVE - all 39 exports from the project's own export inventory (3. analyze mods\AMF export inventory\inventory.md), sized exactly to what this project's 11 SMF-integrated mods actually resolve, no more. AddSectionItem maps SMF's "Section/Item" path onto the native page registry (section = mod entry, item = page/tab). RegisterInpoutEvent/RegisterEventPriority (+ their Unregister siblings) implemented as real callback registries, wired into the input hook (SMF-compat input callbacks get first look at an event, ahead of ImGui) and a new compat::FireMenuEvent hook point for open/close/render events. Full cimgui text/layout/widget/query/draw-list forwarding to the embedded ImGui, including the one pOut case (igGetCursorScreenPos). GetMenuFrameworkVersion reports 1.2.

### Notes
- M3 is now functionally complete (registry + compat surface). NOT YET DONE: no existing mod has actually been pointed at AMF and tested - the pilot (Dragon's Eye Minimap's settings page, dual-resolve against both SMF and AMF, SMF disabled) is the next milestone action, not yet started.

## 1.2.0 - 2026-08-27 - untested

### Added
- M3 first half: the page registry is LIVE. AMF_RegisterPage accepts registrations (was an honest refusal since M0); the left pane lists one entry per registered mod; a mod with several pages renders them as TABS inside its one menu - the one-menu-per-mod rule implemented at the framework level. Thread-safe registration, render-thread snapshot iteration, duplicate and null-argument refusals logged.
- AMF API Demo menu (two pages, toggles/slider/button) registered through the public AMF_RegisterPage path itself - proves the registry end to end and gives gamepad navigation real content to select (the author, 1.1.2: nothing to select yet). Controlled by bShowApiDemo (INI, default 1).
- Second half of M3 (the SMF-compatible ig* export surface + dual-resolve client header) follows in the next versions, driven by the export inventory now being compiled.

## 1.1.3 - 2026-08-27 - untested

### Fixed
- Release-triggered game actions could fire from inside the menu (the author's 1.1.2 report: the shout command worked with the menu open and cascaded into another menu). Root cause: the stuck-key mitigation passed EVERY button release through to the game, and Skyrim's shout activates on RELEASE - so a button pressed inside the menu was consumed on the down-edge but completed as a shout on the up-edge. The hook now tracks which buttons the game actually saw go down; a release passes through only for a button held since before the menu opened, and both edges of anything pressed inside the menu are consumed.

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