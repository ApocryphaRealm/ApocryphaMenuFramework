# Apocrypha Menu Framework 1.4.4

An original SKSE menu framework that embeds Dear ImGui and exposes an API compatible with the
public consumer header of SKSE Menu Framework - so mods written for that API register with this
framework unchanged. Skyrim SE 1.5.97 and AE 1.6.x from one DLL (CommonLibSSE-NG, Address Library).

Licence: GPL-3.0-or-later (see `LICENSE` and `NOTICE.md`). Not a fork: contains no SKSE Menu Framework code.


> **Scope note (2026-08-30):** this repository is **AMF**, the SKSE-Menu-Framework replacement released as
> 1.4.3. The comprehensive build line - game-menu takeover, HUD widgets, capture, the theme standard - is the
> **Apocrypha Framework Overhaul (AFO)** and continues in its own repository from here; commits after
> v1.4.3 in this history are the AFO seed and are not part of the AMF release.

## What it does

- **One in-game menu for every registered mod**, in the SKSE Menu Framework shape: a side list of
  the registered mods (plus the framework's own Settings / Controls / Help entries) and a content
  pane for the selected mod's pages; a mod with several pages gets tabs. No game-menu takeover -
  that is the separate Apocrypha Framework Overhaul project.
- **Themes and fonts, separately.** A theme sets the colours (Skyrim, Untarnished
  with its knotwork frame); the font picker lists the Windows faces present plus any `.ttf`/`.otf`
  dropped into `Data\SKSE\Plugins\ApocryphaMenuFramework\fonts`. Text is rasterised from a real
  TrueType face at native size, never scaled from a bitmap.
- **Keyboard and controller navigation.** D-pad and left stick move the selection, START closes
  the menu; the toggle key is rebindable from Framework -> Controls (default F1).
- **Per-save persistence for registered mods** - a plain-text `<save>.amf-state.ini` beside each
  save, restored when that save loads, scoped exactly like the co-save.
- **Keep the camera awake.** With the game left running behind the menu, the idle camera never starts circling
  the character while it is open, and one already circling stops as the menu opens (Settings -> General, on by
  default, `[Menu] bKeepCameraAwake`).
- **Toggles render as sliding switches**, sliders can be nudged with the arrow keys, and
  explanatory text is never drawn in the near-invisible disabled grey.
- **Hang watchdog.** If the renderer produces no frame for 120 s (configurable, `[Watchdog]`), the
  framework terminates the game process itself - the only thing that closes a kernel-wedged
  Skyrim, since Task Manager cannot.
- **INI files as pages.** Every `.ini` directly in `Data\SKSE\Plugins` and in `Data` gets a page "<name> (INI)" (a tab
  per section, a control per key) unless its mod has a page of its own; a change writes only that value, keeping a
  `.amf-backup` of the file. Each page has its own switch under Settings -> Converted menus -> INI, and starts off:
  turn on the INI files you want and only those appear in the Mods list.
- **TOML files as pages** (2.2.1). Every `.toml` directly in `Data\SKSE\Plugins` and in its folders can be a page:
  "<name> (TOML)", a tab per table (a folder's files: a tab per file), a control per value (switch, number box with the
  wheel, text box; arrays and inline tables shown only), the file's comments as help. A change writes only that value
  (comments, order, quoting, byte-order mark and line endings kept; `.amf-backup` of the file). Game Settings Override's
  folder is the "Game Settings Override" page: controls by the game setting's own type, the game's current value beside
  each, flags for unknown keys, wrong types and keys a later file wins, and the game's value set at once. Each page starts
  off: turn it on under Settings -> Converted menus -> TOML.
- **Changes in game, where a mod says how** (2.2.1). Each INI and TOML page says how its changes take effect:
  - reloaded in game, through the reload the mod declares (its Mod Function Menu Config entry, or Game Settings
    Override's `ReloadConfig`), after each save and from the page's Reload button;
  - applied live, for the game's own INI settings, which are set in the engine at once;
  - or the next time the game starts.

  Nothing undeclared is ever called.
- **Other menus.** Open Animation Replacer, Community Shaders, Immersive Equipment Displays, OptiScaler and ENB get a
  row under Framework -> Other menus with a button that opens their own menu; AMF closes as it does, and a mod's own key
  pressed while AMF is open closes AMF too. "AMF holds this key" (off by default, per mod) writes a key no keyboard has
  (F13 and up, one nothing in these mods' configs is bound to) into that mod's own config, with
  `<config>.amf-key-backup` kept while it holds it, so its menu opens from AMF and its real key is free; turning it off
  writes the original back. Takes effect next launch. OAR, Community Shaders and IED get their key pressed into the
  game's own input events; OptiScaler and ENB by Windows. A config a mod manager puts back at launch (Root Builder) is
  noticed and the switch says so. ENB is found from its files only. **Opens directly** where a mod has a published call
  for it and its build is on AMF's checked list (today Open Animation Replacer 3.2.1, interface V3): no key is pressed,
  so that mod's own key can be unbound entirely. Every other mod, or a build AMF has not checked, uses its key (AMF's held
  key, else its own); Community Shaders, IED and OptiScaler have no such call, and ENB's licence rules it out. Each row
  says which: "opens directly" or "uses its key: X".
- **Settings presets** (2.3.0). Settings -> General -> Presets keeps the settings of every mod in this menu - INI and
  TOML pages, Prisma and MCM Helper menus, and SkyUI menus written in a mod's script - in one named file. Save several,
  load one any time: AMF first shows what would change (mod, setting, now -> preset, a switch per mod), skips mods that
  are not installed, and keeps your values from before as a `Backup <date time>` preset. Your presets are
  `Data\SKSE\Plugins\ApocryphaMenuFramework\RememberedSettings\<name>.json` (the same files Remembered settings uses);
  a modlist can ship read-only presets in `Data\SKSE\Plugins\ApocryphaMenuFramework\Presets\`. FLICK pages keep their
  own values and are not in presets.
- **Nothing while loading.** Until the main menu has opened, and while any loading screen is up, AMF does not open, takes
  no input and queues no SKSE task; if it is open when a load starts, it closes.
- **Converted menus.** Mod Function Menu entries run in AMF itself (Settings -> Converted menus -> MFM):
  Mod Function Menu is not needed, and mods that list it as a requirement only need their own files. If it is
  installed as well, it takes the key first: on F1 (both menus' default) only Mod Function Menu opens - uninstall it, or
  give it another key in `ccld_ModFunctionMenu.toml`.
- **DevBench driving tools** (optional, only if DevBench is installed): `amf.menu` opens, selects
  and reads the menu headlessly (`op=systemrow` reports the journal's own category list and whether
  the framework's row is live in it); `amf.mainmenu` drives the vanilla start menu; `amf.process`
  reports frame health and can force-exit the game on demand.

## Requirements

- SKSE64 and Address Library for SKSE Plugins. Nothing else.
- The installer also adds `!ApocryphaMenuFramework-SMFName.dll` (built from `stub/`) when SKSE Menu Framework is not installed: it only declares the SKSE plugin name `SKSEMenuFramework` at 3.14.0, so Papyrus `SKSE.GetPluginVersion("SKSEMenuFramework")` checks pass (2.2.1).
- Optional: DevBench (for the driving tools), the AMF MO2 plugin and DLL Load Order Shim (load-order
  control when a mod needs a specific DLL order).

## Settings

`Data\SKSE\Plugins\ApocryphaMenuFramework.ini` - toggle key, controller mode, text scale, window
preset, log level (`uLogLevel=0` = trace, the shipped default), watchdog window, API demo page.
Everything is editable from Framework -> Settings in game and written back to the INI.

## For mod authors

Vendor the public SKSE Menu Framework consumer header as usual; the framework resolves either
`ApocryphaMenuFramework` or the stock name. Register your menu and pages; the framework draws
them. Log file: `Documents\My Games\Skyrim Special Edition\SKSE\ApocryphaMenuFramework.log`.

## Building

`build.bat` (finds MSVC via `find-msvc.bat`, configures with the CMake presets, builds Release).
vcpkg supplies CommonLibSSE-NG, Dear ImGui (dx11 + win32 bindings) and DirectXTK. Package with
the project's `package-mod.ps1`; the `.pdb` ships as a sibling "Debug Symbols" package.

Repository: https://github.com/ApocryphaRealm/ApocryphaMenuFramework - every version is a tag.

## Languages (1.6.4)

The framework's own text - its Settings, Controls and Help pages and the window chrome - is read from
`Data/Interface/Translations/ApocryphaMenuFramework_<language>.txt` in the game's language (Skyrim's
`sLanguage`), the same file format SkyUI uses. Shipped: English, Japanese, Korean, Chinese, Russian, German,
French, Spanish, Italian, Polish and Czech. The Language combo on the Framework Settings page (or
`sLanguage=` in the INI) forces one; switching is live. A translation that lacks a line falls back to
English. The font atlas holds Latin Extended and Cyrillic always, the full set of the language's script (the whole CJK block for
Chinese and Japanese, every Hangul syllable for Korean) where it fits, every character of the loaded translations, and every
character a page draws that it lacked (added at the next frame); a Windows CJK, Hangul or Segoe UI face is merged into any
face, a picked one included, that lacks a character it must draw. The atlas is fitted to the D3D11 limit (2.2.0). Each mod's own page is that mod's to translate - a mod that ships its own `<Mod>_<language>.txt` beside ours follows the same Language setting (`AMF_GetLanguage()` in the C API) and its characters go into the atlas automatically (1.6.5).
Corrections from native speakers are welcome - the files are plain text.

## Credits

- **shazdeh2** - Virtual Keyboard (Nexus 158716). The menus and SkyUI hooks for game text boxes were mapped from
  shazdeh2's Virtual Keyboard, whose permissions allow modification and asset use; AMF's universal keyboard (2.3.0) is
  built on AMF's own keyboard in C++, and none of that mod's files are shipped or needed.
- **ccld** - Mod Function Menu (MIT). AMF shows Mod Function Menu's entries as one of its converted menus; that support
  is a clean reimplementation from Mod Function Menu's description and public SDK header, with none of its code.
- **ccld** - Game Settings Override (GPL-3.0). AMF reads and writes Game Settings Override's documented file format and
  calls its exported `ReloadConfig`; none of its code is used.

## Licence

GPL-3.0-or-later - see `LICENSE` and `NOTICE.md`; components under other licences, with their notices, are in
`THIRD_PARTY_NOTICES.md`. Versions before 2026-09-13 carried an MIT licence in error: the Skyrim 1.7.x build links
CommonLibSSE-NG 7.2.0, which is GPL-3.0-or-later.
