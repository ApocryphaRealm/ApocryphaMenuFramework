# ApocryphaMenuFramework - changelog

Written as changes happen, not reconstructed afterwards (rule 61). Each version carries its
**version-ledger status**, so this file cannot quietly claim more than the ledger does:

* **working** - observed running in game
* **untested** - built and packaged, not yet confirmed
* **failed** - built but crashed or malfunctioned; the number was reclaimed
* **scratch** - a hypothesis-test build that never held a real number

<!-- VERSIONING-RULES -->
> **Versioning rules (CLAUDE.md rules 6 and 48 - identical for mods and documents):**
> * `X.Y.Z`. A change increments the THIRD number. At `.9` the MINOR rolls: `1.0.9 -> 1.1.0`;
>   `1.0.10` never exists.
> * The next number is **LAST WORKING + 1**. A failed, scratch or untested test build does NOT
>   consume its number - the next attempt at the same step REUSES it.
> * Numbers are assigned by the tooling, never by hand: mods via `version-ledger.ps1 -Action next`
>   then `set-version.ps1`; governed documents via `docs-pipeline.ps1 -Action bump`; the rules via
>   `rules-version.ps1 -Action bump`. If a number was typed by hand, it is wrong until the tool
>   agrees.

## 1.6.4 - 2026-09-06 - working

### Added
- Language support for the framework's own text. Every string the framework draws (the Settings, Controls and Help pages and the window chrome) now comes from Interface/Translations/ApocryphaMenuFramework_<language>.txt - the SKSE translation file format - in the game's language, with an INI override (sLanguage) and a Language combo on the Framework Settings page that switches live. Anything a translation lacks falls back to English, then to the compiled text.
- Eleven languages shipped: English, Japanese, Korean, Chinese, Russian, German, French, Spanish, Italian, Polish, Czech (the first four proven in game). The FOMOD asks which to install: all (follow the game) or one.
- The font atlas is built from the loaded translation's own characters, and a system CJK or Hangul face is merged in for the glyphs the Latin face lacks (Meiryo / MS Gothic, Malgun Gothic, Microsoft YaHei / SimSun), so kana, kanji, hangul, hanzi and Cyrillic all draw.
- amf.menu gained op=language (a translation name or auto).

### Proof
- Test Build SE 1.5.97, 2026-09-06 22:52: one launch, live switches to Japanese, Korean, Chinese, Russian and German over amf.menu, each captured on the Framework Settings page with its script rendered; 55 strings read per language; the merged face logged per language; auto returned to English.

## 1.6.3 - 2026-09-06 - working

### Changed
- THE DLL IS NOW NAMED !ApocryphaMenuFramework.dll. SKSE loads plugins in alphabetical order, and a mod that takes hold of the framework at its own load (Ammo Patcher, through the vendored SKSE Menu Framework header) only finds a framework that loaded before it; the leading ! puts this DLL first. Only the DLL and its PDB are renamed - the INI, the log, the data folder and every setting keep their names. The old name still resolves: a mod that looks the framework up as ApocryphaMenuFramework by name is answered with this module.
- If an old ApocryphaMenuFramework.dll is left beside the new file (a hand update), it loads inert, the log says so, and a notice at load asks for it to be deleted. Mod Organizer users replacing the mod folder are unaffected.

## 1.6.2 - 2026-09-06 - working

### Fixed
- A mod with many sections keeps its tab labels whole. The section tab bar now uses the scrolling fitting policy with a tab-list button on its left, so a page with twelve sections (Character Progression Control) no longer squeezes them to "Level... Expe... Skills"; the bar scrolls and the list button opens every section by name. The same fix SkyHUD Settings Menu 1.0.3 made for its own tab bar, now in the framework for every mod.

## 1.6.1 - 2026-09-05 - working

### Changed
- THE EMBEDDED DEAR IMGUI IS NOW THE ONE SKSE MENU FRAMEWORK USES: 1.90.8, docking branch, on both build lines (an overlay port; vcpkg's registry never carried 1.90.8). Mods built against the SKSE Menu Framework header now meet exactly the colour, style-variable, item-flag and struct layout that header was generated from, so nothing needs translating; the translation added in 1.5.9 stays in place as a safety net.

### Added
- FAST EXIT, so a closing game can never get stuck where nothing can end it. When the game asks Windows to exit, the framework flushes its log and ends the process at once instead of letting every loaded DLL and driver run its shutdown code - the phase in which a Skyrim 1.7.104 test game wedged on 2026-09-05 into a state no in-process or external kill could reach. On by default (bEnabled under [FastExit]; a toggle on the Settings page). Nothing the game needs is lost: saves are written when you save, settings when you change them.

## 1.6.0 - 2026-09-05 - untested

### Changed
- The log now names WHICH mod asked for the framework. Every line the alias writes when it answers a module-name or file-name lookup carries the asking DLL's name, so a mod that reaches the framework and still shows no page can be told apart from one that never reached it (Ammo Patcher and Critical Hit Tweaks, 2026-09-05).

## 1.5.9 - 2026-09-05 - untested

### Fixed
- THE FRAMEWORK NO LONGER GOES SILENT WHEN AN EARLIER SKYRIM IS STILL RUNNING. The once-only load guard used a name shared across the whole logon session, so a SkyrimSE.exe left behind from a previous session - stuck on exit, which happens - made the next game's framework believe it had already loaded: SKSE reported it loaded, and it did nothing, with no log line and no menu. The guard is now scoped to the running game process.
- PAGES FROM THOSE MODS NO LONGER CRASH THE GAME, AND THEIR COLOURS AND SPACING COME OUT RIGHT. Once the pages above could reach this framework, the first one opened (Simple Follower Framework) crashed the game: it asked for the style object (igGetStyle), which this framework did not provide, and read from the null it got back. Eighteen framework calls the ten mods on hand use were missing (igGetStyle, igBeginGroup, igEndGroup, igRadioButton_Bool, igPushStyleVar_Float/Vec2, igPopStyleVar, igPushItemFlag, igPopItemFlag, igPushStyleColor_U32, igAlignTextToFramePadding, igCalcTextSize, igIsItemDeactivatedAfterEdit, igInputFloat3, igSliderFloat3, igSliderFloat4, igSetNextItemOpen, igTreeNode_Str) and are now provided. Underneath that: the shared consumer header numbers colours, style variables and item flags the way the Dear ImGui 1.90 docking branch does, which is not how either of this framework's build lines number them, so every such index is now translated by name, and igGetStyle hands back the style laid out the way the consumer reads it. Verified by driving all 30 registered pages in game without a crash or a missing-call warning.
- THIRD-PARTY SKSE MENU FRAMEWORK MODS NOW APPEAR. Since release, most mods built against SKSE Menu Framework registered nothing with this framework and showed no page (Nexus reports 2026-09-05; Discord list: Bobbing Framework, Casting Bar, Cinematic Conversation Camera, First Person FOV SKSE, HorsePower, Mesh Rendering Framework, Simple Follower Framework, Tears of Kyne, True Flasks). Their shared consumer header first checks that a file named Data/SKSE/Plugins/SKSEMenuFramework.dll exists and skips registration when it does not - and this framework ships no file under that name. A file-system query for exactly that name is now answered with this framework's own DLL, the same way a module-name lookup already was, so the check passes and the mod goes on to register. The C++ runtime DLL is covered too, since a mod built against the dynamic runtime performs that check inside it. Found by reproducing on this machine with the MO2 alias plugin removed: only the framework's own mods registered, every third-party mod logged 'SKSE Menu Framework not installed'.

## 1.5.8 - 2026-09-05 - untested

### Added
- SCREEN-WIDE DRAWING FOR MOD MENUS. A mod's settings page can now draw an overlay anywhere on
  the screen, not only inside its own window - the foreground and background draw lists are
  exposed (igGetForegroundDrawList, igGetBackgroundDrawList) along with the rectangle and text
  draw calls. The first use is a HUD-position preview that shows where a HUD element will sit
  while you adjust it; any mod can use it.

## 1.5.7 - 2026-09-04 - working

> Line 1 observed on Apostasy Test Build (SE 1.5.97) 2026-09-04 23:53: log opens "Build line: SE
> 1.5.97 / AE 1.6.x (CommonLibSSE-NG 3.7.0); game 1.5.97.0", 21 sections listed, menu opened and
> captured, no crash log. Line 17 observed on Steam 1.7.104 + SKSE 2.3.1 + Address Library v13
> 2026-09-04 23:47-23:55: "Build line: Skyrim 1.7.x (CommonLibSSE-NG 7.2.0); game 1.7.104.0", DXGI
> present + D3D init + PollInputDevices hooked, DevBench answered amf.menu state. Wrong-pick test:
> the line-1 DLL on 1.7.104 stops with this mod's own "re-run the installer" box, not CommonLib's.

### Added
- A SKYRIM 1.7.x BUILD, AND AN INSTALLER THAT ASKS WHICH GAME YOU HAVE. Skyrim 1.7.99 / 1.7.104
  (what Steam installs since 28 August 2026) ship their Address Library databases in a new format
  that the CommonLibSSE-NG this mod was built on cannot read, so on those games the mod could
  never start. The same source now builds twice: the SE 1.5.97 / AE 1.6.x line as before, and a
  Skyrim 1.7.x line on CommonLibSSE-NG 7.2.0. The download is a FOMOD whose one question is your
  game version; it installs the matching DLL. The 1.7 DLL is distributed under GPL-3.0-or-later
  because of the library it links (its licence texts and a NOTICE install alongside it); the
  mod's own source and the SE/1.6 DLL stay MIT.
- The load-time log now names the build line and the game version on its third line, so a log
  from a bug report says at once which DLL the player has and which game they ran it on.

### Changed
- THE ADDRESS LIBRARY CHECK NOW TELLS THE TRUTH ABOUT SKYRIM 1.7.x. Skyrim 1.7.99 and 1.7.104
  ship their Address Library databases in a new file format (5) that the CommonLibSSE-NG this
  build links cannot read, so on those games the mod could never get past its first address
  lookup - and 1.5.6's message would have sent the player to install a library that changes
  nothing. The pre-check now reads the database's format number and, on a 1.7.x game or a
  format this build does not read, stops with: which game versions ARE supported (SE 1.5.97,
  AE 1.6.x), that updating the library will not help, and the Discord hub. The missing-file
  message now names the "All in One" download plainly.

## 1.5.6 - 2026-09-05 - untested

### Added
- POINT-AND-CLICK DRIVING FOR TESTS. `amf.menu` gains `op=cursor` (x, y in display pixels) and `op=click` (optional x, y, button), and `op=state` now reports the software cursor. While the menu is open the game recentres the OS cursor every frame, so the framework's own integrated cursor is the only position Dear ImGui ever sees - an outside driver could not point at a widget at all. The cursor is placed and its position published ahead of any queued button in the same frame, and a click's press and release land on separate frames because event trickling is off in this framework. Verified: a third-party mod's checkbox toggled by a driven click, its own Save Settings pressed the same way, the value written to its INI on disk, the game saved and reloaded with the page showing the change.
- A PLAYER-FACING ADDRESS LIBRARY CHECK. Every address here resolves through Address Library, and CommonLibSSE opens its database on the first lookup with an error that names a build-directory hash and nothing to act on (bug report 2026-09-04). The framework now checks for the database file for the running game version before its first lookup and, when it is missing, stops with the file name, the download to install (Address Library for SKSE Plugins - All in One), and where to get help.

## 1.5.5 - 2026-09-04 - untested

### Added
- A NULL GUARD FOR EXPORTS THIS FRAMEWORK DOES NOT HAVE, AND A LISTENER THAT NAMES THEM. The stock SKSE Menu Framework header calls whatever `GetProcAddress` returns without checking it, so a consumer asking for a widget export this framework lacks used to receive null and crash the game the first time its page drew. This framework already answers the module-name lookup for SKSE plugins; it now answers their `GetProcAddress` for its own module too. A name it exports resolves normally; a name shaped like the SMF consumer API that it lacks resolves to a generated stub that returns zero (in both the integer and floating-point return registers) and logs the name once - the page draws with a hole in it instead of the game dying, and the log says which export to add next. Names not shaped like the consumer API get the honest null: the first live run showed a plugin probing every module for `ReShadeRegisterAddon`, taking a stub as "this is ReShade", and calling it.
- Every resolved export is logged once at debug as an inventory of what consumers actually use.

### Fixed
- Argument null guards across the widget exports: a null label, format string, value pointer, buffer or draw list no longer reaches Dear ImGui.

## 1.5.4 - 2026-09-04 - untested

### Fixed
- MODS BUILT FOR SKSE MENU FRAMEWORK NOW ACTUALLY REGISTER. 1.5.3's module-name alias was right and arrived too late: measured against a real-SMF reference run, two third-party consumers resolve the framework inside their own `SKSEPlugin_Load` - about 100 ms after this framework loaded, before any SKSE message exists - and the stock header caches whatever that first call returns. Our sweeps at load and `kPostLoad` could not reach an import table that did not exist yet. The alias is now applied from a loader DLL-load notification (`LdrRegisterDllNotification`), which fires for each plugin after its imports are resolved and before its entry point runs, so it precedes every possible first lookup. Verified: nine third-party SMF mods plus DevBench registered - Block Overhaul, Dynamic Wind, Input Manager, Quick Commands, STB Widgets, Show Player In Inventory, StepUpOnto, Typing Mode, devbench - taking the menu from 12 sections to 21, and DevBench reports the framework version it resolved (`v1.2`) instead of `v0.0`.
- OPENING A THIRD-PARTY PAGE NO LONGER CRASHES THE GAME. With registration working, the first page opened (Block Overhaul) called an ImGui widget export this framework did not have; the stock header's widget wrappers are not null-guarded, so a missing export is a call to address zero. Added the 61 widget exports the installed consumers reference and this framework lacked - windows, popups, combos, tab bars, tables, tooltips, drag/input floats, tree nodes, style colours, text wrap, `igGetIO`, `ImDrawList_AddLine` and the rest - with the public consumer header's signatures verbatim. Every third-party page was then opened in turn with the game surviving each one, and Input Manager's and STB Widgets' pages photographed rendering their tabs, sliders, combos and tables.

## 1.5.3 - 2026-09-04 - untested

### Added
- MODS BUILT FOR SKSE MENU FRAMEWORK NOW FIND THIS FRAMEWORK. The stock SMF consumer header resolves the framework by module name - `GetModuleHandleW(L"SKSEMenuFramework")` - which returned null against AMF, so every such mod registered nothing. AMF now answers that name: it redirects the `GetModuleHandleW`/`A` imports of SKSE plugin modules so a lookup of `SKSEMenuFramework` resolves to this module, and everything else passes through untouched. No second binary ships, nothing is renamed, and no system code is patched - an import-table entry is a data pointer. Measured in game: four mods that had never registered (Carry Weight Per Level, Remember Lockpick Angle, Sure of Stealing, Wait Your Turn Redux) now appear, taking the menu from eight sections to twelve.
- THE CONSUMER SURFACE beyond settings pages, which was missing entirely: `AddWindow`, `AddWindowWithView`, `RegisterHudElement`, `UnregisterHudElement`, `LoadTexture`, `DisposeTexture`, `PushFont`, `PushRegular`, `PushSolid`, `PushBrands`, `Pop` and `IsHotkeyEnabled`. A mod that wants its own window or a HUD element calls these rather than `AddSectionItem`, and their absence was invisible from its side - the header's wrappers are `if (func) { ... }` with no else, so a missing export is a silent no-op rather than an error. Consumer windows and HUD elements draw every frame, independent of whether this framework's own menu is open; textures are decoded once and cached by path.

### Changed
- `IsAnyBlockingWindowOpened` now also reports a consumer window that is open and taking input, not just this framework's own menu, so a menu launcher gets one truthful answer for the whole process.

### Fixed
- The import redirect is confined to modules under `SKSE\Plugins`. The first build applied it process-wide and the game died a few seconds after load with no crash log at all: MO2's usvfs virtualises the file system by hooking this same call family, so redirecting its imports breaks the thing every file access depends on.

## 1.5.2 - 2026-09-04 - working

### Changed
- THE NESTED SURFACE NOW FITS THE JOURNAL IT IS HOSTED IN. Opened from the SKSE MENUS row, the framework is sized and placed to the journal panel around it instead of to its own default proportions, so it reads as a page of that menu rather than a larger window sitting on top of it.
- The panel is MEASURED off the live movie, every frame, rather than assumed. Its size differs under every art replacer - which is the same reason the row is injected instead of shipped - so a measured rectangle fits Untarnished UI, Dear Diary and vanilla alike with no patch for any of them. The last good measurement is kept while the journal fades its panel in and out, so the window never flickers between two sizes.
- This is GEOMETRY ONLY, and it applies to the nested row ALONE. Opened by the hotkey, or by a menu launcher such as Risa's All In One Menu through the API, the framework keeps its own window exactly as before. What it draws is identical either way.

### Removed
- THE AMF API DEMO IS GONE, along with its `bShowApiDemo` setting and the `[Debug]` section that held it. It existed to dogfood the public API and to give the window enough selectable content to judge gamepad navigation while the registry was empty; the registry is not empty any more, and a demo entry sitting among a player's real mods is a development artifact showing up in a shipped product.

### Changed
- THE WINDOW REMEMBERS WHERE YOU LEAVE IT, SEPARATELY FOR EACH WAY OF OPENING IT. These are profiles rather than fixed presets: one for the System menu row, one for the key. Each starts at a sensible default - the journal panel it is hosted in, and the centre of the screen - and the first time you move or resize either, that is what it opens at from then on. A menu replacer whose panel sits somewhere else therefore needs dragging once rather than a patch.
- Both windows can now be moved and resized. This supersedes the earlier "preset positions, never free placement" rule for this window; that rule existed so the window could not be left somewhere useless, and a remembered position with a reset button gives the same safety while letting people fix a layout we cannot predict.
- "Reset both to default" on the settings page puts them back. It matters more than a reset usually does, because the nested default is the journal panel measured live - not something you could drag your way back to.
- Geometry is stored as fractions of the screen, so a saved position stays right if you change resolution.
- The shipped INI now carries the `[Window]` and `[Watchdog]` sections. `[Watchdog]` was written by the settings page but never shipped, so the file the mod saves had a section the file it installed did not.
- KEYBOARD OR CONTROLLER NAVIGATION IS NOW DETECTED, ALWAYS, AND IS NO LONGER A SETTING. There were two switches for it - one holding the mode, one deciding whether the detector was allowed to write it - which is two controls describing one fact the game already knows, and they could be left contradicting reality: a player who picked up a pad kept keyboard navigation until they found the toggle. `bControllerMode` and `bAutoInputMode` are both gone from the INI and the settings page; the menu simply follows whatever you last used and shows which one it is reading, so its accuracy can still be judged while playing.
- Only DELIBERATE input moves it, unchanged from when detection was opt-in: a button going down, a mouse click, real mouse movement, or a stick past the navigation deadzone. A resting stick or a nudged mouse cannot flip navigation mid-menu.
- `AMF_GetInputMode()` now reports the LIVE device rather than a stored setting. Consumers use it to word their own prompts, and a saved value could say "keyboard" while the player was on a controller. The export, its name and its signature are unchanged.
- ONE BUILD, NO INSTALLER. The FOMOD is gone and 1.5.1's two options are retired. Both ways in are now on by default - the SKSE MENUS row in the game's System tab, and F1 - and either can be switched off from the settings page or the INI. Which one you prefer is a preference, and a preference that needs a reinstall to change is the wrong shape for it; it also forked the INI, the documentation and every support answer in two for no gain.
- The System row is now ON THE SETTINGS PAGE, where the installer question used to be. It was previously reachable only by editing the INI by hand, which was tolerable while the installer asked about it and is not now that nothing else does.

### Fixed
- THE MENU NOW CALLS ITSELF BY THE PRODUCT'S NAME. The window title, the version line, the help text and the INI header read "ApocryphaRealm Menu Framework"; they had been showing "Apocrypha Menu Framework", which is the form reserved for the repository, the DLL and the INI filename rather than for anything a player reads. The banner has said ApocryphaRealm since 1.4.x, so the menu was the odd one out.
- The window identities moved from `##` to `###`. ImGui hashes the whole label when the separator is `##`, so renaming the visible part would have silently orphaned each window's saved layout entry; with `###` the identity is fixed and the displayed name can change freely.
- A SIZE SET BY HAND ON THE HOTKEY WINDOW NO LONGER GETS OVERWRITTEN BY THE NESTED ONE. Both modes drew the same ImGui window name, so both shared one saved geometry in the layout file - and the nested mode rewrites its size every frame, so it clobbered the entry and the hotkey window came back the wrong size. Each mode now has its own window identity and its own remembered geometry. The hotkey window's identity is unchanged, so a size already saved survives the update.
- The nested window is inset by the window padding rather than filling the panel rectangle exactly, so its border no longer sits flush on top of the journal's own and read as one thick misaligned rule. It also no longer offers a resize handle, because the size there belongs to the journal - a handle that visibly does nothing is what the snapping looked like from the outside.
- THE NESTED WINDOW WAS BIGGER THAN THE SCREEN. It was measured from `Menu_mc`, and `getBounds` returns the union of a clip and ALL its children whether or not they are visible - the journal keeps its Save/Load, Creations, Help and Confirm panels parented and hidden rather than removed, so the honest bounds of `Menu_mc` really are larger than the panel you can see. In game it measured 113% of the screen wide and 125% tall. It now measures `PanelRect`, the invisible rectangle the page already carries for exactly this purpose and which has no children of its own, and any candidate that comes back larger than the stage is rejected rather than used.
- THE NESTED INSTALL NOW KEEPS F1. It shipped with the key unbound, on the reasoning that the row is the thing you are meant to find; that left the journal as the only way in, so reaching mod settings always meant pausing first. Both work now - the row opens the framework fitted to the journal, F1 opens it as its own centred window - and `uToggleKey=0` still gives the key back to the game for anyone who wants that. A menu launcher that takes the key over, such as Risa's All In One Menu, continues to do so.
- APPEARANCE SETTINGS ARE NO LONGER HIDDEN in the nested install. They were hidden on the belief that a nested surface would wear the game's own menu artwork and so have nothing to theme. It does not - nesting changes geometry only, and everything inside that rectangle is still drawn by this framework in this theme - so the install that most needs the controls was the one that could not reach them, and was told something untrue about its own menu while it happened.
- SystemRow.h described a handler-wrapping approach that was tried, does not work, and is not what the code does. `GetVariable` does not return an AS2 function - not off the page instance and not off the class prototype - so there is nothing to read out and stash. The file now documents the second listener that is actually installed, and why leaving the game's own handler untouched is the safer shape.

## 1.5.1 - 2026-09-04 - working

### Added
- MOD SETTINGS ARE NOW REACHED FROM THE GAME'S OWN PAUSE MENU. The journal's System tab carries a new row, SKSE MENUS, below QUIT and beside SAVE, LOAD and SETTINGS. Selecting it opens this framework, so settings are found where a player already looks for settings instead of behind a hotkey they have to be told about.
- The row is added to the menu AS IT OPENS rather than by shipping a replacement for any of the game's files. That matters for compatibility: the journal's artwork is one file, and every menu-replacement mod ships its own copy of it, so a framework that shipped one too would collide with all of them. This one collides with none, and takes on whatever look the installed artwork gives it.
- A listener is added BESIDE the menu's own handler rather than replacing it, so save, load, installed content, settings, controls, help and quit keep behaving exactly as they did. In the worst case the new row does nothing; it cannot break the menu it lives in.
- A FOMOD installer with two choices. Nested puts the row in the game's menu, unbinds the hotkey, and hides the appearance settings because the surface wears your own menu artwork. Standalone keeps the framework as its own window on its own key with the full settings, which is also what menu launchers expect to find. The binary is the same either way - only the settings it starts with differ - and `bSystemMenuRow` switches between them at any time.

### Notes
- Checked against Risa's All In One Menu 5.0: it detects this framework, lists it in its launcher, reads the nested build's unbound key correctly, and edits none of our settings. The two run together with no errors on either side.

## 1.5.0 - 2026-09-03 - working

### Added
- THE LAUNCHER-CONTROL API. A menu-launcher mod - one that gathers other mods' menus behind a
  single key - drives a menu framework through three exports rather than a consumer header:
  `GetMainWindow`, `IsAnyBlockingWindowOpened` and `SetHotkeyEnabled`. None of the three existed
  here, so a launcher that reached this framework could do nothing with it. All three are now
  exported with the signatures and semantics those launchers already expect.
- `GetMainWindow` returns a static object whose LAYOUT is the contract, not just its address:
  `std::atomic<bool> IsOpen` then `std::atomic<bool> BlockUserInput`, nothing before them. The
  caller stores into `IsOpen` directly to show or hide the menu. It has static storage duration,
  so a caller holding the pointer across a save load cannot end up writing into freed memory.
- A once-per-frame reconcile on the render thread keeps that object and the framework's own
  visibility agreeing in BOTH directions: a menu opened from outside appears, and a menu closed
  in game - by the toggle key, by a DevBench call, by anything - stops reporting itself as open.
- `SetHotkeyEnabled(false)` hands the framework's own toggle key to the launcher for the session.
  Runtime only: it is never written to the INI, so the player's own binding comes back if the
  launcher is uninstalled.

### Fixed
- The aliased second `SKSEPlugin_Load` returned false, and SKSE calls `FreeLibrary` on any plugin
  whose load reports incompatible. It now returns true and does nothing else - the once-guard
  still refuses to initialise twice, it just no longer asks for the module to be thrown away. In
  the shipped configuration both names resolve to one module, so nothing was actually lost; this
  removes a misleading "reported as incompatible" line from every SKSE log and makes the
  behaviour correct if the alias is ever a distinct file.

### Known limitation, stated rather than papered over
- These exports are NOT yet reachable through the MO2 companion plugin's virtual
  `SKSEMenuFramework.dll` name. That alias is a usvfs PATH mapping, so the loader opens the same
  real file and dedupes it into one module named `ApocryphaMenuFramework.dll` - read from a
  running game's module list, no module of the SMF name exists in the process at all. A launcher
  that resolves the framework with `GetModuleHandleW(L"SKSEMenuFramework.dll")` therefore finds
  nothing, and no amount of exporting on this side can change that.
- Found while checking this framework against Risa's All In One Menu 4.9 (2026-09-03), whose
  framework button logged `GetMainWindow returned null`. The route chosen is to ask that mod to
  probe `ApocryphaMenuFramework.dll` as a second module name - one line on their side, and
  everything after it already works. Nothing here waits on it: the exports ship either way, and
  any launcher that reaches this framework by a working route can drive it today.

## 1.4.9 - 2026-09-01 - working
(author pad confirm 22:36: "the controller nav works properly including selecting a slider with
a"; pane crossing both ways, the return landing and three auto-input switches all observed live
over DevBench)

### Fixed
- Navigation could not cross from the mod list INTO the options - only back the other way
  (author playtest: "neither the d-pad the left or the right stick will let me go from the left
  to the right pane"). ImGuiWindowFlags_NavFlattened, used in 1.4.8 to let navigation cross the
  pane border, is documented for child windows with NO scrolling; the options pane scrolls, and
  flattening it produced exactly that asymmetry. Navigation is contained in each pane again and
  the crossing is explicit: right in the list enters the options, left in the options returns,
  from the left stick, the D-pad or the arrow keys alike. It is ignored while a slider is being
  adjusted, so pushing left inside a slider changes the value instead of leaving the pane.
- Coming back from the options now lands on the entry whose page is open, not wherever the
  list's cursor was left (the author: "if I select settings and go right and I scroll to the
  bottom and then I go back left then it should take me back to the settings menu selector not
  to the bottom of the left pane").
- The knotwork themes (Vanilla, MO2 Skyrim) drew their frame art directly ON each box's rect,
  so the art's own hairlines landed on the pane borders and its 26px corner ornaments covered
  the first line of text - the window's version line read "pocrypha Menu Framework". The art is
  now drawn just OUTSIDE each box so it frames the border instead of covering it, the window's
  padding clears the ornament's width, and the two panes have room between them. Themes without
  the art (Untarnished) are unchanged. Author, comparing the two: "the Skyrim theme doesn't let
  them fully see all the corners and lines of a box with a border ... you might have to change
  the margin between those areas and the edge of the menu frame".

### Changed
- THE THEMES ARE NOW TWO (author, 2026-09-01: "we don't need the mo2 Skyrim or the vanilla
  versions anymore as they're extremely similar in colour and design"). "Vanilla" and
  "MO2 Skyrim" differed only in the tone of their text; they are merged into ONE theme named
  "Skyrim" - the knotwork frame, silver and gold lines, and the crisper warm off-white text -
  which is the default. "Untarnished", the framework's original identity, stays as the plain
  alternative. An INI naming a retired id ("vanilla", "mo2-skyrim") is migrated to "skyrim"
  rather than dropped, so nobody's saved choice is lost.
- EVERY theme now uses the same padding, so the two look alike in spacing and switching theme
  changes the colours and the art but never the geometry (the author: "edit the untarnished
  theme to have the same margin edits so they look similar in spacing"). The figure comes from
  the knotwork ornament's fixed 26px band; the plain theme simply shares it.

### Added
- "Detect input automatically" (bAutoInputMode, OFF by default): the menu follows whatever you
  last really used - a key press, a mouse button, real mouse movement or a stick past the
  navigation deadzone - and switches between keyboard and controller navigation on its own. Idle
  noise is ignored by design, because a mode that guesses wrong is what drives nav focus astray;
  the explicit toggle remains the rule and this simply steers it. The settings page shows what
  the detector currently reads, and how long ago, so its accuracy can be judged while playing.
- `amf.menu` gains a theme op (switch theme by id at runtime) and reports controllerMode,
  autoInputMode and lastDevice - so a two-theme visual comparison, and the auto-switch itself,
  can be tested in ONE game session.

## 1.4.8 - 2026-09-01 - working

### Added
- MENU-SHELL personalization (author verdict 2026-09-01), a presentation layer over the page
  registry - registered mods are untouched and know nothing about it:
  - ALIAS: rename any mod's menu entry from the Framework Settings page. The alias replaces the
    mod's name in the list and in the content pane's heading, and it is what alphabetical
    sorting uses.
  - ORDER: the list is alphabetical by default and EVERY entry always shows its position
    number. Typing a new number moves that entry there and every other number re-flows
    (insert-and-shift), so nobody has to number the whole list by hand. A mod installed later
    inserts at its alphabetical position within the existing sequence. "Reset to alphabetical"
    is one button. Repositioning/floating per-mod windows are deliberately NOT included.
  - Both live in AMF's own INI, in new [MenuAlias] and [MenuOrder] sections.
- CONTROLLER NAVIGATION scheme (author spec, 2026-08-31): the left stick moves through the list
  AND across into the options with no button press (both panes are nav-flattened, so ImGui's
  navigation is no longer trapped inside each child window); A takes hold of a slider and the
  RIGHT stick then moves it; A opens a drop-down, the sticks choose, A confirms. Both sticks are
  captured while the menu is open and exactly one is wired to ImGui's navigation axes per frame -
  the left while nothing is being edited, the right while something is - so adjusting a value can
  never also move the selection, and the idle stick is released so it cannot leave an axis stuck.
- `amf.menu` gains alias / move / resetorder ops and reports `customOrder` plus the player-facing
  `displayOrder` (position, mod, shown name), so the shell is drivable and assertable headlessly.

## 1.4.7 - 2026-09-01 - working

### Fixed
- CRASH changing the font or the text size (author playtest, 2026-08-31): the font-atlas
  rebuild ran AFTER ImGui_ImplDX11_NewFrame - the only place the DX11 backend recreates its
  device objects - so the rebuild destroyed the font texture with nothing left in the frame
  to recreate it, and the frame then rendered its draw data against a dead texture. The
  rebuild (invalidate + BuildFonts) now runs BEFORE the backend NewFrame, so the backend
  recreates the font texture in the same frame.

## 1.4.6 - 2026-08-30 - working
(capture observed in game 2026-08-30 - the Nexus banner pictures; keybind widget observed in
game 2026-08-31 - gate `amf-keybind-test.ps1` PASS: spliced K captured un-reserved, Tab
captured reserved, observe-only confirmed at the main menu)
### Added
- IN-PROCESS CAPTURE ported from the Overhaul line: `amf.process op=capture` saves the presented
  frame WITH the menu overlay to Data\SKSE\Plugins\ApocryphaMenuFramework\captures\<name>.png.
  Needed because native screenshots are pre-overlay; first use: real in-game pictures inside the
  Nexus banners (design decision, 2026-08-30).
- KEYBIND-CAPTURE WIDGET (`amf.keybind`, design decision 2026-08-31, queue L26): a reusable
  DevBench surface for testing binds without a mod's own settings page. `arm` records the next
  keyboard/gamepad press WITHOUT consuming it (observe-only - the game and menu still see it);
  `state` reports the captured key with its name, device and the framework's reserved-key
  verdict plus the current toggle key; `rebind` arms the real toggle-key rebind path; `cancel`
  disarms. Reuses the existing input-hook capture design and `SMF_GetReservedKeyCodes`.

## 1.4.5 - 2026-08-30 - working
### Removed
- Papyrus native-function scaffolding (AMF_Ping / AMF_SetTestValue / AMF_GetTestValue and the
  AMFTest.psc test script) - design decision, 2026-08-30: of the features beyond SKSE Menu
  Framework, "8 can be taken out, the rest are fine". The persistence channel, watchdog,
  DevBench tools, themes, fonts, controller nav and rebinding all stay.

## 1.4.4 - 2026-08-30 - working
### Changed
- REVERTED TO THE SMF SHAPE (design decision, 2026-08-30): AMF is a one-for-one replacement of
  SKSE Menu Framework plus user-friendly features that do not overhaul the game. The window is
  again a SIDE LIST (the framework's Settings / Controls / Help, then the registered mods) and a
  CONTENT pane for the selected mod's pages. The nested game menu - Quests / General / Stats /
  System top tabs, the Save / Load / Save and Quit / Quit actions, the live Stats page, the
  Mod-menus index - is gone from this project; it continues in the Apocrypha Framework Overhaul.
- `amf.menu select` paths are now `settings`, `controls`, `help` and `mod:<index>`; the old
  `system/...` paths are still accepted. `activate` is a no-op (no node carries an action).
### Removed
- Game-menu takeover code (console-command runner, system action panes, Stats/Quest/General panes).
### Known
- Observed on the Test Build (2026-08-30, headless): the menu opens, both registered mods list, every
  `select` path lands and holds - but on the FIRST open after a load the reported selection was one
  entry off (settings -> controls) once; repeated probes were stable. Watch for it with a mouse.

## 1.4.3 - 2026-08-28 - working
### Added
- HANG WATCHDOG + forced exit (the author: the game must be closable without Task Manager, even hung).
  A monitor thread watches the renderer's frame counter; if no frame is produced for `uSeconds`
  (default 120, `[Watchdog]` in the INI) the game is declared hung and the process terminates
  ITSELF. This works where taskkill/Task Manager fail: a wedged Skyrim's main thread is stuck in a
  kernel wait, but our watchdog thread is still scheduled, so `TerminateProcess(GetCurrentProcess())`
  from inside succeeds. Logged and flushed before terminating so the reason survives.
- `amf.process` DevBench tool - `op:"status"` (frames, seconds since the last frame, whether it is
  considered hung) and `op:"kill"` (force-exit on demand). It runs on devbench's LISTENER thread,
  which keeps answering while the main thread is wedged, so it can close a hung game deliberately.
- FONT PICKER on the settings page, separate from the theme selector (a theme sets colours; the
  face is an independent choice, so any font pairs with any theme). Lists the curated Windows faces
  that are present plus any .ttf/.otf dropped into
  `Data/SKSE/Plugins/ApocryphaMenuFramework/fonts`. Selecting one re-rasterises immediately.

## 1.4.2 - 2026-08-28 - working
### Fixed
- Menu text looked PIXELATED (the author). Cause was not the MO2-Skyrim theme carrying anything over - it
  was the font: AMF used ImGui's built-in ProggyClean, a 13px BITMAP face, and then magnified it with
  `FontGlobalScale = uiScale * textScale` (about 2.17x at 3200x1800). Magnifying a bitmap font is
  what produced the blocky edges. AMF now rasterises a real TrueType face at the NATIVE pixel size
  for the display (16px * uiScale * textScale) and keeps FontGlobalScale at 1.0, so nothing is
  stretched. Moving the text-size slider REBUILDS the atlas at the new size (crisp at any scale)
  instead of stretching it, done before NewFrame with the DX11 font texture invalidated.
### Added
- `sFontPath` in the INI (Display section): point the menu at any .ttf. Empty picks a clean system
  face automatically (Segoe UI, then Calibri, then Trebuchet). If none load, it falls back to the
  old built-in font rather than failing to render.

## 1.4.1 - 2026-08-28 - working
### Fixed
- External menu selection (the `amf.menu` DevBench tool) was overwritten every frame and snapped
  back to whatever tab ImGui thought was open - an automated sweep of all 14 menu nodes reported
  `tab=quests` for every one of them. Cause: the render loop copied its own selection state back to
  the shared globals unconditionally each frame, and ImGui's tab bar owns its selected tab
  internally, so it always won. Now the copy-back happens ONLY when a real UI interaction changed
  the selection, and a selection set from outside is flagged so that frame FORCES ImGui to the
  requested tab instead of adopting the tab bar's opinion. (Found by the framework's own automated
  pane sweep - the driving tool catching a bug in the thing it drives.)

## 1.4.0 - 2026-08-28 - working
### Changed
- MENU RESTRUCTURED to the real vanilla shape, corrected from the author's in-game screenshots: the game
  menu is THREE levels - TOP TABS across the top, a SIDE LIST belonging to the active tab, then a
  CONTENT pane - not the single vertical tree 1.3.8 shipped.
  * Top tabs: **Quests | General | Stats | System**.
  * The **System** side list: Save, Load, **Mod menus** (with each registered mod indented beneath
    it - the SkyUI/MCM equivalent's home), Settings, Controls, Help, Save and Quit, Quit.
  * Content pane renders the selected side entry: framework settings under System -> Settings, key
    bindings under System -> Controls, a Help page, a Mod menus index, live Stats, and each mod's
    pages (as tabs when a mod has several).
- `amf.menu` DevBench tool follows the new shape: `select` accepts `tab:<quests|general|stats|
  system>`, side paths like `system/controls`, and `mod:<index>`; a side path implies its tab, and
  `state` now reports the active `tab` alongside `selected`.

## 1.3.9 - 2026-08-28 - working
### Added
- `amf.mainmenu` DevBench driver (rule 64 applied to the GAME's start menu, the author's idea): the
  vanilla Main Menu can now be driven and inspected headlessly - op `state` (menu open, movie
  present), `userevent` (post the kUserEvent/BSUIMessageData message the menu's own buttons
  produce, e.g. text "New Game"), `invoke` (call an ActionScript method on the menu movie), and
  `getvar` (read an ActionScript variable). userevent/invoke queue to the main thread; state and
  getvar answer synchronously (2s main-thread wait). This is the exploration surface for pressing
  the REAL New Game button headlessly - the one start-menu action nothing could reach before
  (coc-from-menu is flaky by design; DevBench's cold load needed an existing save).
  VERIFIED live: `invoke path="_root.MenuHolder.Menu_mc.FadeOutAndCall" arg="StartNewGame"` (also
  reachable as op `delegate` name=StartNewGame) started a new game headlessly - lifecycle=newGame,
  playerLoaded=true, loaded into Tamriel. NOTE: calling the game's GameDelegate handler DIRECTLY
  (fxDelegate->Callback with fabricated args) crashes the game and was removed; the safe path is
  invoking the menu's own ActionScript so the game builds the delegate context itself.

## 1.3.8 - 2026-08-28 - working
### Added
- NESTED GAME-MENU MODEL (the author's directional project - build the model overnight): the menu is now
  a tree whose top-level categories mirror the vanilla game menu - Game Settings, Stats, Quest,
  General, and a System category (Save, Load, Mods, Save and Quit, Quit). The per-mod settings
  pages (the SkyUI/MCM equivalent) are nested under System -> Mods, exactly where Mod Configuration
  sits in SkyUI. "Game Settings" holds the framework's own settings (theme/controller/text/rebind);
  "Stats" shows a live read of player level/health/magicka/stamina (proof the categories can host
  real game data). Save/Load/Quit/Save-and-Quit are disabled placeholders: this is the STRUCTURE.
  Wiring the real game actions and intercepting Skyrim's own pause/journal menu are the next steps
  (PLANNED-MODS "AMF as a full nested game-menu replacement"). Built on 1.3.7, so it also carries
  the controller Start-closes-menu, left-stick nav, and live keybind-rebind fixes.

### Added (cont.)
- DevBench driving tool `amf.menu` (rule 31): the menu can now be opened, navigated, activated and
  read over DevBench REST (POST /api/tool/amf.menu {op:open|close|select|activate|state}), so it is
  testable HEADLESSLY - Claude can push the buttons itself. Vendors the MIT DevBenchAPI.
- System menu: Save and Quit-to-desktop are now WIRED (console exec via RE::Script), Quit behind a
  confirm. Load and Save-and-Quit stay placeholders (save enumeration / flush-before-quit sequencing).

## 1.3.7 - 2026-08-28 - untested
### Added
- Menu-toggle-key REBINDING on the Framework Settings page (the author): a "Rebind" button captures the
  next key pressed (Escape cancels) as the key that opens/closes the menu, saved immediately.
- Controller: the gamepad START button now CLOSES the menu in controller mode - the way out with a
  pad (the author: "no way to use the controller to leave the menu"). It only closes, never opens, so the
  game keeps its own Start button when the menu is down.
- Controller: the LEFT ANALOG STICK now drives menu navigation (fed as ImGui GamepadLStick analog
  nav with a deadzone). The live test showed gamepad button events arriving but no D-pad - the author was
  using the stick, which was not being captured, so he "couldn't switch menus".
### Observability (rule 31)
- Thumbstick x/y logged when controller mode is on, so the live test can confirm the stick reaches
  nav and whether the up/down axis needs flipping.

## 1.3.6 - 2026-08-28 - working
### Added
- New "Vanilla" theme (now the DEFAULT): the MO2 Skyrim look (knotwork frame + silver/gold) but
  with the cleaner, brighter text from the original Untarnished theme (#F5F2E9 warm white instead
  of #dddddd), which reads crisper on solid black. MO2 Skyrim and Untarnished remain selectable.
### Fixed
- Controller input mode did nothing when toggled on (the author pressed every button, no response).
  Cause: gamepad ImGui navigation needs the backend to advertise a gamepad - `io.BackendFlags |=
  ImGuiBackendFlags_HasGamepad` - which was never set, leaving NavEnableGamepad inert. Now set
  whenever controller mode is on (cleared otherwise).
### Observability (rule 31)
- Every gamepad event reaching the input apply-loop is now logged (code, down, controllerMode,
  mapped ImGuiKey), so a live DevBench-monitored test can distinguish "no gamepad events arrive"
  (a game/input-device-mode issue - e.g. Auto Input Switch absent) from "events arrive but nav is
  inert" (ImGui-side). Under live investigation with the minimap zoom-key regression report.

## 1.3.5 - 2026-08-28 - working
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
  flag gates it (on for MO2 Skyrim, off for Untarnished). The frame is drawn around
  every panel (outer window + both child panes), not just the outer window, matching the MO2
  style where each framed panel carries the ornament. Palette gained optional
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