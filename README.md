# Apocrypha Menu Framework 1.4.3

An original SKSE menu framework that embeds Dear ImGui and exposes an API compatible with the
public consumer header of SKSE Menu Framework - so mods written for that API register with this
framework unchanged. Skyrim SE 1.5.97 and AE 1.6.x from one DLL (CommonLibSSE-NG, Address Library).

Licence: MIT (see `LICENSE`). Not a fork: contains no SKSE Menu Framework code.


> **Scope note (2026-08-30):** this repository is **AMF**, the SKSE-Menu-Framework replacement released as
> 1.4.3. The comprehensive build line - game-menu takeover, HUD widgets, capture, the theme standard - is the
> **Apocrypha Framework Overhaul (AFO)** and continues in its own repository from here; commits after
> v1.4.3 in this history are the AFO seed and are not part of the AMF release.

## What it does

- **One in-game menu for every registered mod**, shaped like the game's own menu: top tabs
  (Quests / General / Stats / System), a side list per tab, a content pane. Each mod's settings
  pages live under System -> Mod menus; a mod with several pages gets tabs.
- **Themes and fonts, separately.** A theme sets the colours (Vanilla, Untarnished, MO2 Skyrim
  with its knotwork frame); the font picker lists the Windows faces present plus any `.ttf`/`.otf`
  dropped into `Data\SKSE\Plugins\ApocryphaMenuFramework\fonts`. Text is rasterised from a real
  TrueType face at native size, never scaled from a bitmap.
- **Keyboard and controller navigation.** D-pad and left stick move the selection, START closes
  the menu; the toggle key is rebindable from System -> Controls (default F1).
- **Per-save persistence for registered mods** - a plain-text `<save>.amf-state.ini` beside each
  save, restored when that save loads, scoped exactly like the co-save.
- **Toggles render as sliding switches**, sliders can be nudged with the arrow keys, and
  explanatory text is never drawn in the near-invisible disabled grey.
- **Hang watchdog.** If the renderer produces no frame for 120 s (configurable, `[Watchdog]`), the
  framework terminates the game process itself - the only thing that closes a kernel-wedged
  Skyrim, since Task Manager cannot.
- **DevBench driving tools** (optional, only if DevBench is installed): `amf.menu` opens, selects
  and reads the menu headlessly; `amf.mainmenu` drives the vanilla start menu; `amf.process`
  reports frame health and can force-exit the game on demand.

## Requirements

- SKSE64 and Address Library for SKSE Plugins. Nothing else.
- Optional: DevBench (for the driving tools), the AMF MO2 plugin and DLL Load Order Shim (load-order
  control when a mod needs a specific DLL order).

## Settings

`Data\SKSE\Plugins\ApocryphaMenuFramework.ini` - toggle key, controller mode, text scale, window
preset, log level (`uLogLevel=0` = trace, the shipped default), watchdog window, API demo page.
Everything is editable from System -> Settings in game and written back to the INI.

## For mod authors

Vendor the public SKSE Menu Framework consumer header as usual; the framework resolves either
`ApocryphaMenuFramework` or the stock name. Register your menu and pages; the framework draws
them. Log file: `Documents\My Games\Skyrim Special Edition\SKSE\ApocryphaMenuFramework.log`.

## Building

`build.bat` (finds MSVC via `find-msvc.bat`, configures with the CMake presets, builds Release).
vcpkg supplies CommonLibSSE-NG, Dear ImGui (dx11 + win32 bindings) and DirectXTK. Package with
the project's `package-mod.ps1`; the `.pdb` ships as a sibling "Debug Symbols" package.

Repository: https://github.com/ApocryphaRealm/ApocryphaMenuFramework - every version is a tag.
