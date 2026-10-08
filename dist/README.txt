ApocryphaRealm Menu Framework
=============================
Version 2.1.4

An original, GPL-3.0-or-later in-game menu framework (embedding Dear ImGui) - a one-for-one
replacement for SKSE Menu Framework's consumer surface, plus user-friendly features that do
not overhaul the game. Mods built against the SMF header register and draw against AMF
unchanged; AMF loads under its own module name.

WHAT YOU GET
------------
  * Two ways in, both on by default: a SKSE MENUS row in the game's own System tab, which
    opens the framework sized to the journal panel around it, or F1 for a window of its own.
    The row is added to the menu as it opens rather than by replacing any game file, so it
    works alongside whatever menu artwork you have installed.
  * Each way in remembers where you leave it: move or resize either window and it keeps what
    you chose, with one button to put both back.
  * The Mod Control Panel: a side list (Framework Settings / Controls / Help, then every
    registered mod) with a content pane for the selected mod's settings pages.
  * Your list, your way: rename any mod's entry and set the order of the list. It starts
    alphabetical and every entry shows its number; type a new number and the rest re-flow
    around it. One button puts it back.
  * Six themes - Skyrim (the knotwork frame, the default), Untarnished (the same layout with
    clean lines and no frame art), and four made to sit beside other interfaces, each with its
    own frame, background and switches: Vel'dun, Oathvein, Norden and Norden - Black - plus a
    font picker (drop a .ttf into Data/SKSE/Plugins/ApocryphaMenuFramework/fonts) and a
    text-size slider. A theme is one INI and a few PNGs, so you can add your own
    (CUSTOM-MENU-ART.md).
  * Full controller navigation. Keyboard or controller is detected from whatever you last
    used - there is nothing to configure and nothing to switch on.
  * Menu-key rebinding (default F1), per-save persistence of menu state, and a hang watchdog
    so a wedged game can always be closed.
  * Every control the framework claims can be switched off or rebound - no key is taken
    from you permanently.

USING IT WITH A CONTROLLER
--------------------------
Just pick up the pad - the menu follows whatever you last used, and shows which one it is
reading.
  * The left stick moves through the list AND across into the options - no button needed.
  * A takes hold of a slider; the RIGHT stick then moves it, so changing a value can never
    also move your selection. A again lets go.
  * A opens a drop-down, the sticks choose, A confirms.
  * B cancels, START closes the menu. The D-pad does everything the left stick does.
  * Leaving the options to the left puts you back on the entry whose page is open.

WHAT CHANGED
------------

Version 2.1.4
NPC Preset Applier and other mods made for SKSE Menu Framework 3.18 work under AMF: their preset portraits are made and shown. AMF now sends SMF's menu and frame events (NPA's portrait maker waits on them), has SMF 3.18's three new functions (API version, rename and delete a menu section), loads .dds images, and reads image paths with non-English letters. D-pad right on the Mods row now steps through the tick box, the A-Z switch and Sort before crossing to the settings. Thanks to ladydias for the report.

Version 2.1.3
SVG icons: mods made for SKSE Menu Framework that ship their icons as .svg files (Walk With Me, for one) now show them. No more freeze (on 2.1.1, a crash) the first time a page drawing Font Awesome icons opens - Cinematic Conversation Camera or MCM Memory, for example - in a large load order: the icons are added without taking away the fonts other mods are drawing with. Thanks to KnightQueen1 for the report.

Version 2.1.2
The menu's own words in every language: a converted menu's "(MCM)" ending and every mention of MCM are written in the language you pick. Your menu names, order and renames are unchanged.

Version 2.1.1
The window, on Appearance - three switches, all on by default: Move the window (drag its top row; it reopens where you left it), Resize the window (any edge or corner, height and width alike; off fixes the size), and See-through window with a Window opacity slider down to 5% - the black background fades most, text least. Thanks to Barzing for the suggestions. The Mods row has a tick box for alphabetical order, one A-Z / Z-A switch, and a Sort button that files the MCM menus into categories. Separators and tabs read in every language, and the Controls page fits any window size. One place to set the menu key: Controls > Open and close the menu (the duplicate under Settings is gone). Controls no longer cuts its keys and descriptions off in the window the journal's SKSE MENUS row opens. The category separators the sort makes, and converted MCM pages whose mod ships a translation, follow the language you pick. Help is up to date in all eleven languages. Dropdowns open without an empty band at the top and bottom, and the rename boxes lost their empty title strip. The sort follows a few more placements, and a developer test box is gone from the General tab.

Version 2.1.0
Framework Settings is split into tabs: General, Appearance, MCM menus and Menu list. Choose which MCM menus come into AMF: the MCM menus tab lists every MCM menu found, with a switch for each, a filter, and All on / All off, and "Bring in from SkyUI" picks one menu from those left to SkyUI. A menu switched off stays in SkyUI's own menu only; right-click an entry in the Menu list and choose "Keep in SkyUI only" to do the same from there. By default every menu comes in and SkyUI keeps its whole menu too. "Bring in MCM menus not switched below" decides menus you haven't chosen, so a long list can start from none and switch on just the few you use. One button at the top of the Menu list tab sorts the MCM menus in AMF into categories by name (Interface, Combat, Gameplay, World and Visuals and so on, with Other for the rest), and Undo puts the list back. Move a menu under another category's separator yourself and the sort remembers it: the next sort puts it there too. Your choices are kept in McmImport.txt and McmSortLearned.txt, which the download never contains, so updates never reset them. MCM menus that are switched off no longer clutter the Menu list on the settings page. Thanks to xLenax for the suggestions.

Version 2.0.9
Your MCM menus show in AMF even when SkyUI has not taken them into its own list - past SkyUI's 128-menu limit, registered late, or under a mod that replaces SkyUI's MCM manager (MCM Unlocked and similar). AMF also looks again for new menus for up to 8 minutes after a load, and each time you open the menu. "Take those mods out of SkyUI's list" now works with MCM Unlocked and the larger-list layouts too. Thanks to Soulsthat for the report.

Version 2.0.8
The menu and its tooltips fit the screen when the game draws a smaller image than its window (a lower render resolution scaled up): they no longer run off the right and bottom. The log says when the window and the image differ. Thanks to Soulsthat for the report.

Version 2.0.7
Your MCM menus inside AMF. Every mod with a SkyUI MCM menu now also gets its settings as a page here - MCM Helper menus (from the mod's config.json) and menus written only in Papyrus - read from the mod itself while the game runs, with changes applied live as in SkyUI's menu. Three switches on Framework Settings (the [MCM] section): MCM Helper menus, SkyUI script menus, and taking those menus out of SkyUI's own list. Turning the feature off gives every menu back to SkyUI; setstage SKI_ConfigManagerInstance 1 restores SkyUI's whole list in any case. Not yet: binding a controller button, a mod's own key-conflict warning, MCM pages that are a custom picture.

Version 2.0.6
Collapsing headers in other mods' pages line up as they do under SKSE Menu Framework: dMenu NG's section icons no longer sit on top of the header's arrow. Thanks to C0kAdam for the report.

Version 2.0.5
The menu key works again after an update, and editing it in the INI takes effect. Your User.ini now holds only what you changed in the menu; everything else comes from the shipped ApocryphaMenuFramework.ini, so the two files no longer fight each other. A User.ini from 2.0.3 or 2.0.4 that copied every setting is cleaned up the next time a setting is saved, without losing anything you changed.
uToggleKey and Controls > Open and close the menu are now one key: setting either one changes the key that opens the menu. uToggleKey may be written in hex (0x3B) as well as decimal (59), and a value that is not a usable key falls back to F1 with a note in the log.
The log names every setting your User.ini sets over a different shipped value, and where the menu key came from.
Thanks to HadToRegister for the report.

Version 2.0.4
A mod's own window now gets the mouse, the keyboard and the controller. Mods that open a window of their own - RaceMenu Atelier's editor, FSMP, Equip or Unequip All - drew with no cursor and ignored clicks, keys and typing unless the framework menu was open too. The game is not paused for such a window, the mod's own hotkeys (Atelier's F4) still reach it, the console key still reaches the game, and the game has its controls back the moment the window closes. An always-on overlay the mouse passes through never takes the game's input.
Icons from Font Awesome now draw in mods that use them (RaceMenu Atelier's buttons showed "?"). Font Awesome Free 6.7.2 ships in SKSE\Plugins\ApocryphaMenuFramework\icons (SIL OFL 1.1, licence beside it) and is only loaded when a mod asks for it.
Less work per frame for mods that look up every drawing call by name.
For mod authors: the SDK optional file - AMF.h (the same header as the Oblivion Remastered framework's), PreciseSlider.h and a complete example plugin, MIT.

Version 2.0.3
Your settings survive updates. Everything you set in the menu - theme, text size, window positions, keys, and the mod list's order, separators, favourites and names - is saved to SKSE\Plugins\ApocryphaMenuFramework\User.ini, a file the download never contains, so an update cannot replace it. The ApocryphaMenuFramework.ini beside the DLL now holds only the defaults.
Layout presets: Framework Settings > Menu list can save the list under a name, load it back, or delete it.
Updating from 2.0.2 or older: your mod order and other settings reset once with this update. Set them again and they are saved to User.ini, which no update replaces.

Version 2.0.2
The start-up curtain fills screens of any shape. A modlist can ship its splash art at several shapes in a "splash" folder beside splash.png (splash-16x9.png, splash-21x9.png, splash-4x3.png, ...); the curtain uses the one closest to your screen and fills the screen with it. Lists without the folder look as before.

Version 2.0.1 (the first release of 2.0 - 2.0.0 was not released on its own)
Separators in the mod list. Right-click a row (Y on a controller) and pick "New separator above": the menus below it, up to the next separator, are its group, as in Mod Organizer 2. A separator folds and unfolds with A or a click (folded, it shows how many menus it holds), can be renamed, favourited, moved and deleted, and "Send to" on any mod moves it into a group or out of every group. A-Z and Z-A sort only the mods outside separators; favourite mods stay at the top.
"Move to the top" now moves a mod to the top of its own separator, so it is no longer the same as favouriting it.
"Reorder" in a mod's options opens a small window with an up and a down arrow that move the mod one place per press.
Grab and move on a controller: R3 picks up the highlighted mod, the right stick moves it one place per push (held, it keeps going), and R3 or B puts it down. All three are rebindable on the Controls page.
The menu has no title bar any more - the frame runs round the whole window - and it sits in the middle of the screen whether opened by its key or from the System menu row. The mod list widens to show every name in full, and the window widens once to fit both panes.
The Text size slider moves one step (0.01) per keyboard or D-pad press. The log ships at info (uLogLevel=2); set 0 for a full log when reporting a bug.

Version 1.9.9
The controller triggers now reach the menu. L2 and R2 were never passed on, so a mod's page could not see them - a "press a button to bind" window, such as Show Player In Inventory's rotate key, never caught L2. Nothing else changes: the framework's own controls do not use the triggers.
Version 1.9.8
Added four themes, picked from the Theme list on the Framework Settings page: Vel'dun (bone-coloured lines with cut corners on warm dark brown), Oathvein (thin grey lines with crossed scratch marks on charcoal, and a blood-red highlight), Norden (slate lines with bright corner ticks on grey) and Norden - Black (the same on black). Each is made to sit beside the interface of the same name by Nithog; none of their files are included and none of them is needed.
A theme file can now carry its own frame, background and switch art and a full set of colours, so a theme you drop into the themes folder can look as complete as the built-in ones.
Version 1.9.7
Added a setting to pause the game while this menu is open, the way the game's own menus do. It is on the Framework Settings page and off by default.
Version 1.9.6
The right-click menu on a mod in the list now fits closely around its three options instead of sitting in a box with an empty band around them.
Fixed text boxes being cancelled the moment they opened, so nothing could be typed or deleted until Escape was pressed. The menu thought Escape was still held after it had been used to close the menu; it now starts with no key held and releases any key the keyboard reports as up.
The F1 window moves again by its top bar, opens where it was last left, and still grows evenly on both sides when resized.
Fixed typing in the search bar and other text boxes stopping after a while, until Escape was pressed. Another mod can switch the game's text entry off while a box is focused; the framework now switches it back on every frame the box has focus, and notes the first time it happens in its log.
Fixed typing never starting on load orders where another mod had left the game's text entry switched off more times than on.
The window now moves only by its top bar, so dragging a slider, a 3D preview or a list no longer drags the whole menu, and a plain click on a row works again.
Added: the bumpers switch tabs (L1 previous, R1 next; Page Up / Page Down on the keyboard), a bindable "open a mod's options" action (Y by default), and "favourite the highlighted mod" (L3, or F on the keyboard).

Version 1.8.4
Fixed the SKSE MENUS row in the game's System menu doing nothing when it is pressed. The row was recognised by the position it was added at, which moves when the game inserts its own Mod Manager row, and its press listener was skipped whenever the row was already in the menu. The row is now recognised by its own name, and its listener is attached every time the journal opens.
Changed the controller navigation box to bright blue in every theme, so the item the controller is on stands out from the gold selection.
Added a driving-tool report of the game's own System menu list and whether this framework's row is live in it.

Version 1.8.0
Fixed the fit of the menu opened from the System row under Quest Journal Overhaul - Entire Journal Redesigned: that journal keeps the game's panel rectangle where it was but draws its System page as a button column and a content pane, so the menu sat across the buttons. It now measures that journal's own pane and opens inside it; a remembered position from other journal art is set aside. The menu opened by its key is unchanged.

Version 1.7.9
Fixed a crash a few seconds into the game on some load orders: the input hook wrote its pruned event list back through the caller's pointer, which with other input hooks in the chain could point into another mod's read-only memory. The write now happens only when the list actually changed and the memory is writable; otherwise the list is passed on through the framework's own array.

Version 1.7.8
Fixed the corner drag of the menu opened by its key: it now keeps the menu's shape and grows only the width and height, the text no longer scales with it, the menu can never be larger than the game screen, and it no longer jumps while being resized. Dragging an edge still grows the menu on both sides about the centre.

Version 1.7.7
Changed the menu opened by its key so that it stays fixed to the centre of the screen. Dragging an edge grows it on both sides, and dragging a corner scales the whole menu, text included, keeping its shape. The menu opened from the game's System menu row is unchanged. The scale is remembered.
Changed the last colours that were still Dear ImGui's own defaults, such as the text selection, resize grips and tables, to the theme's palette, so nothing in the menu looks blue against the Skyrim look.
Added a way for other mods to open this menu on their own page from their own key. Wheeler - Refined 1.1.0 uses it.

Version 1.7.6
Changed the list of reserved keys that other mods ask for so that it names the menu key you currently have set, first, then the menu's navigation keys. It named a fixed list with F1 built in, so a mod that checked it could still take the menu key from a player who had moved the menu to another key. F1 is reserved only while it is the menu key.

Version 1.7.5
Fixed the search box and the reorder fields going dead after the last key. When the framework's menu consumed every key event in a frame it handed the game an empty list but left the game's own pointer on the key it had just consumed, so with the hands off the keyboard the game re-sent that same key every frame: a released Backspace read as still held and deleted each character as it was typed, and every later press was swallowed. The empty list is now written back properly, so keys release when you release them.
Fixed a key pressed inside the menu reaching the game on its release. The check that decides whether the game saw the press was being overwritten, so every release went through; a shout key pressed inside the menu could fire the shout on release.
Added a log line naming any mod whose input callback claims a key while the menu is open, since from the player's side that looks the same as the menu freezing.
Added driving-tool operations that type, press keys and inject real key events, so keyboard paths can be proven without a person at the keyboard.

Version 1.7.4
Fixed mods refusing to appear in the framework at all. This framework tells a mod which SKSE Menu Framework interface it is talking to, and it was answering 1.2 - far below what current mods ask for - so a mod checked the number, gave up before registering anything, and logged that the expected minimum version was not met. It now answers 3.7, the number the real SKSE Menu Framework reports.
Fixed hotkeys that stopped working for mods affected by the above. A mod that gives up during registration never installs its key listener either, so its hotkeys went quiet at the same time as its settings page vanished. Both come back together.
Fixed the mod list becoming unresponsive after a few reorders. Typing a new position rearranged the list while that same list was still being drawn, so the rows below it no longer matched what was on screen and further edits appeared to do nothing. A move is now applied once the list has finished drawing.
Fixed hotkeys belonging to other mods not firing during normal play. Key presses were only passed to mods while this framework's own menu was open. They are now passed on with the menu closed too, which is how SKSE Menu Framework behaves; a press still reaches the game unless a mod claims it, so nothing else changes.
Added diagnostics for anyone reporting a dead hotkey: the framework can now report whether it is telling mods that a window is blocking input, and name every window registered with it, so the cause can be identified instead of guessed.

Version 1.7.3
Restored the null checks that used to guard every hand-written wrapper. A mod that passes a null label or a null value pointer now gets a harmless no-op instead of crashing the game - 656 checks across 614 functions, generated over the built-in drawing functions rather than written by hand.
Left alone the pointers Dear ImGui treats as optional, on purpose: checking those would stop windows without a close button from drawing at all.

Version 1.7.2
Changed how this framework provides the drawing functions that mods written for SKSE Menu Framework call. It now exports every one of that framework's 1,420 entry points, up from 252, so a mod's page can no longer come up blank because of a name that was never added.
The wrappers are no longer written by hand a batch at a time. The generated cimgui matching the Dear ImGui this framework embeds is compiled in instead, so every name exists with the signature it is meant to have.
Fixed a style setting that quietly did nothing: a mod pushing the tab border size was told this ImGui had no such setting, when it does.
Fixed a mod being unable to change the interface style through the framework. Asking for the style handed back a copy, so anything written to it was discarded; it now hands back the real thing, as SKSE Menu Framework does.

Version 1.7.1
Fixed a mod's settings page coming up blank. A mod written for SKSE Menu Framework does not link against this framework - it looks up every drawing function it uses by name at runtime, so a name this framework did not export came back as nothing and that part of the page drew nothing at all, while the page itself still appeared in the list.
Added the thirty-eight missing entry points, chosen by scanning every SKSE plugin on the test machine that uses the framework rather than by guesswork: all one hundred and forty of them are now fully covered.

Version 1.7.0
Added a Save button to the menu list on the Framework Settings page, so an order you have dragged into place survives the game closing instead of reverting to the registration order.
Added custom menu art: a UI author can replace the frame, the background and the toggle switch with their own PNGs through the [Skin] keys in the INI.
Added a master switch for that art, off by default, as [Skin] bEnabled and as a toggle on the Framework Settings page - with it off nothing is loaded and the menu keeps its built-in look, whatever the paths say.
Drew a supplied frame as a nine-slice so its corners keep their size and only its edges stretch, and decided whether a background tiles or stretches from the image's own size.
Rejected a .dds by name in the log instead of failing with an unexplained decode error, because the texture loader reads PNG and not DDS.
Added two debug operations, skin and skinreload, so artwork can be reloaded from disk without restarting the game.

Version 1.6.8
The search box above the mod list is shown in your language. Its two strings were missing from every translation file since the box was added in 1.6.6, so it drew English whatever language you played in.
The Controls page line describing how the D-pad moves through a mod's sections is now in all eleven languages, not only the first five it shipped in.

Version 1.6.7
Fixed the D-pad and left stick dropping you out of a mod's page the moment you pressed left: in a mod with several sections, left now steps back one section and only leaves the page when you are already on the first one.
Right moves forward a section while you are on the row of section tabs, and still moves between a page's own controls below it.
The amf.menu tool gained the nav and focus ops, and its state now reports which section of a mod's page is open.

Version 1.6.6
Added a search box above the mod list. Once a load order registers thirty or more pages the list is longer than the pane, and typing two or three letters finds a mod faster than scrolling for it.
The search matches the name shown in the list, so a mod you have renamed is found by the name you gave it.

Version 1.6.5
Built the font atlas from every mod translation file of the active language in Interface/Translations, not only the framework's own file, so a translated mod page draws its kana, hangul, hanzi or Cyrillic without that mod touching fonts.
Added AMF_GetLanguage() to the C API so mods can follow the framework's Language setting - one setting drives every page.

Version 1.6.4
Added language support for the framework's own pages: the text is read from Interface/Translations/ApocryphaMenuFramework_<language>.txt in the game's language, with a Language combo on the Framework Settings page that switches live and an sLanguage override in the INI.
Shipped eleven languages - English, Japanese, Korean, Chinese, Russian, German, French, Spanish, Italian, Polish and Czech - and the installer now asks which to install.
Built the font atlas from the loaded translation's own characters and merged a system CJK or Hangul face where the chosen font lacks them, so Japanese, Korean, Chinese and Russian text draws correctly.
The amf.menu tool gained the language op.

Version 1.6.3
Changed the plugin's file name to !ApocryphaMenuFramework.dll so SKSE loads it before every mod that uses it; a mod that takes hold of the framework at its own load, such as Ammo Patcher, now finds it. Only the DLL and its PDB are renamed - the INI, the log and every setting keep their names, and a mod that looks the framework up by the old name still finds it.
Updating by hand: delete the old ApocryphaMenuFramework.dll (and .pdb) from SKSE\Plugins; if it is left behind it loads inert, the log says so and a notice asks for it to be removed. Mod Organizer users replacing the mod folder need do nothing.
Version 1.6.2
Fixed the section tabs on a mod's page: a mod with many sections keeps every tab label whole - the tab bar scrolls and a list button on its left opens any section by name - instead of squeezing the labels down to a few letters each.
Version 1.6.1
Changed the built-in Dear ImGui to the exact version SKSE Menu Framework uses, 1.90.8 docking, on both builds, so mods made for that framework meet the colours, styles and layouts their header expects.
Added fast exit: when the game exits, the process ends at once instead of running every DLL's and driver's shutdown, the phase where a closing game could get stuck beyond any kill. On by default; a toggle on the Settings page and bEnabled under [FastExit].
Version 1.6.0
Changed the log to name which mod asked for the framework on every line where a mod's lookup is answered, so a mod that reaches the framework but shows no page can be told apart from one that never reached it.
Version 1.5.9
Fixed most mods built for SKSE Menu Framework not appearing in this menu: their shared code first checks that a file named SKSEMenuFramework.dll exists and skips registration when it does not, and that check is now answered by this framework's own file.
Version 1.5.8
Added screen-wide drawing for mod pages, so a mod can show an overlay anywhere on the screen, such as a preview of where a HUD element will sit.
Version 1.5.7
Added a Skyrim 1.7.x build and an installer that asks which game version you have.
Changed the Address Library start-up check to say plainly which game versions this build supports and that Skyrim 1.7.x needs the other build.
Version 1.5.6
Added a clear start-up message when the Address Library database for your game version is missing, naming the file and the download to install.
Added cursor and click commands to the testing tool so menus can be driven without a keyboard or mouse.
Version 1.5.5
Fixed a mod asking this framework for a drawing call it does not provide crashing the game; the call now does nothing and the log names it so it can be added.
Added a log of every framework call each mod actually uses, so a missing one is found from the log rather than from a crash.
Version 1.5.4
Fixed mods built for SKSE Menu Framework not appearing in this menu: the framework is now made visible to each of them as it loads, so their settings pages register no matter how early they look for it.
Fixed the game crashing when a page from one of those mods was opened; the drawing calls their pages use are now all provided.
Version 1.5.3
Mods built for SKSE Menu Framework now find this framework: it answers to the SKSEMenuFramework module name in every SKSE plugin, so their settings pages appear here without any change on their side.
Added the window, HUD element, font and texture calls of the SKSE Menu Framework API, so a mod that draws its own window or HUD element through that API can do it here.
Menu launchers asking whether a menu is open are now also told about windows opened by other mods, not only this framework's own.
Version 1.5.2
Added a SKSE MENUS row to the game's own System tab, beside SAVE, LOAD and SETTINGS, which opens this framework sized to the journal panel around it.
The row is added to the menu as it opens rather than by replacing any game file, so it works alongside menu-artwork mods instead of fighting them for the same file.
Each way of opening the menu now remembers where you leave it, separately: move or resize either window and it keeps what you chose, with one button to put both back.
Keyboard or controller navigation is now detected from whatever you last used; the two settings that used to control it are gone.
Removed the AMF API Demo entry and its setting.
Version 1.5.1
Added support for menu launchers - mods that gather several menus behind one key can now open, close and query this framework, and can take over its toggle key for as long as they are installed.
Fixed a misleading "reported as incompatible" line this framework wrote to the SKSE log on every launch.
Version 1.5.0
Fixed navigation not being able to cross from the mod list into the options pane - only back the other way.
Fixed coming back from the options landing on wherever the list's cursor was last left, instead of on the entry whose page was open.
Fixed the knotwork theme's frame art landing on top of panel borders and covering the first line of text; it now draws just outside each panel.
The Vanilla and MO2 Skyrim themes are merged into one theme named Skyrim, the knotwork frame with silver and gold lines, and it is the default. Untarnished stays as the plain alternative.
Every theme now uses the same padding, so switching theme changes the colours but not the layout.
Added Detect input automatically, off by default: the menu follows whichever you last actually used, keyboard or controller, and switches on its own.
Version 1.4.8
Added per-mod menu personalization: rename any mod's entry from the Framework Settings page, and set the order of the list. It starts alphabetical, every entry shows its position number, and typing a new number moves it there while the rest re-flow. One button resets to alphabetical.
Added full controller navigation: the left stick moves through the list and across into the options with no button press. Press A to take hold of a slider and the right stick moves it. A opens a dropdown, B cancels, Start closes the menu.
Version 1.4.7
Fixed a crash when changing the Font or the Text size setting.
Version 1.4.6
Added developer tooling: an in-process screenshot capture and a reusable keybind-capture widget, used for testing and for the pictures on this page. No gameplay change.
Version 1.4.5
Internal cleanup; no visible change.
Version 1.4.4
The menu is a simple side list of your registered mods, plus the framework's own Settings, Controls and Help, with a content pane for the selected page - matching the shape of SKSE Menu Framework.
Version 1.4.3
First public release. An in-game menu framework, built on Dear ImGui, that mods written for the SKSE Menu Framework API register with unchanged.
Two selectable themes, a font picker listing your installed fonts plus any you drop in yourself, and a text-size slider.
Rebindable menu toggle key (default F1), with press-capture rebinding.
Per-save settings for registered mods, stored beside your save and restored when it loads.
Added a hang watchdog: if the game stops producing frames, the framework closes the process itself, which works even when Task Manager cannot.
Settings persist to a plain INI file, never through Windows' cached profile API.

REQUIREMENTS
------------
  * SKSE64
  * Address Library for SKSE Plugins

The .pdb debug symbols ship in this download so Crash Logger can resolve this framework's
stack frames; there is no separate symbols file to fetch.

BUILDING FROM SOURCE
--------------------
GPL-3.0-or-later, original work (not a fork of SKSE Menu Framework): https://github.com/ApocryphaRealm/ApocryphaMenuFramework.git
CommonLibSSE-NG via vcpkg; set VCPKG_ROOT, run configure.bat then build.bat. This build
corresponds to tag v1.5.2.

LANGUAGES (1.6.4)
-----------------
The framework's own pages are read from Data/Interface/Translations/ApocryphaMenuFramework_<language>.txt in the game's language (Skyrim's sLanguage). Shipped: English, Japanese, Korean, Chinese, Russian, German, French, Spanish, Italian, Polish, Czech. The Language combo on the Framework Settings page (or sLanguage= in the INI) forces one, live. A translation that lacks a line falls back to English.

LANGUAGES (1.6.4)
-----------------
The framework's own pages are read from Data/Interface/Translations/ApocryphaMenuFramework_<language>.txt in the game's language (Skyrim's sLanguage). Shipped: English, Japanese, Korean, Chinese, Russian, German, French, Spanish, Italian, Polish, Czech. The Language combo on the Framework Settings page (or sLanguage= in the INI) forces one, live. A translation that lacks a line falls back to English.
