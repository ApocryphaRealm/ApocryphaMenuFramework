# Custom menu art for Apocrypha Menu Framework

<!-- DOC-VERSION: 1.0.0 | 2026-09-09 -->

How to make the framework's menu wear your interface's artwork instead of its built-in look. Written
for a UI author who has never seen this codebase.

Nothing here needs the framework rebuilt, and nothing here is a code change on your side: you ship
PNGs and four INI keys. Every piece is optional and independent - supply only a frame and everything
else stays as it was.

---

## What you ship

### 1. The frame - one square PNG

A 32-bit RGBA PNG with a **transparent centre**, drawn as a **nine-slice** around the menu window:
the four corners are drawn at their own size and never stretch, the four edges stretch along their
run, and the middle is left transparent so the menu shows through.

**192 x 192 with 64-pixel corners is a good default.** That means the outer 64px of each side is
corner ornament and the 64px band between them is the stretched edge.

```
   +----------+----------+----------+
   |  corner  |   edge   |  corner  |    corner = 64px, from uFrameCorner
   |  64x64   | stretches|  64x64   |
   +----------+----------+----------+
   |   edge   |transparent|  edge   |    the centre is never drawn
   | stretches|  (unused) |stretches|
   +----------+----------+----------+
   |  corner  |   edge   |  corner  |
   +----------+----------+----------+
              192 x 192
```

The corner size is an INI key rather than a fixed number because only you know where your ornament
stops. If you set it too large for the image the framework clamps it and says so in the log rather
than drawing corrupt art.

Your frame **replaces** the built-in knotwork - it is not drawn on top of it - and it is drawn
whichever theme is selected, because shipping frame art is itself the request for a frame.

### 2. The background - one PNG, tiled or full-screen

Either works, and **you do not tell the framework which**: it decides from the image's own size.

* **512 x 512 or smaller on both sides** - treated as a **tile** and repeated at its own pixel size,
  clipped to the window edge. A 256 x 256 seamless texture is the usual choice.
* **Anything larger** - **stretched** to fill the window. A 1920 x 1080 backdrop is the usual choice.

It is drawn inside the window, behind everything the menu then draws.

### 3. Control plates - optional, up to three small PNGs

Put any of these in one folder and point `sPlates` at it. Supply only the ones you want changed;
a missing file simply leaves that control alone.

| File | What it restyles |
|---|---|
| `toggle.png` | the on/off switch track |
| `slider.png` | the slider grab |
| `tab.png` | the section tab bar |

**`toggle.png` is live now.** The plate carries the shape and the framework still tints it green for
on and red for off, so the switch stays readable as a switch whatever the art does, and the knob
still draws over it.

**`slider.png` and `tab.png` are read and reported but not yet drawn.** Those two controls are Dear
ImGui built-ins rather than the framework's own widgets, so restyling them means replacing the
widgets outright - a bigger change that would land on every mod's settings page at once, and it is
not worth doing carelessly. Ship the files if you have them; they will start drawing when that work
lands, and `afs`/`amf.process op=skin` will already tell you they loaded.

---

## PNG only. Not DDS.

The framework decodes textures through WIC, which reads **PNG, JPG, BMP and TIFF** and does **not
read DDS at all** - so a `.dds` cannot work here even though it is Skyrim's own texture format.
Export 32-bit RGBA PNG.

If you point a key at a `.dds` the log says so by name rather than leaving you guessing:

```
skin: frame "Data\Interface\YourMod\frame.dds" is a .dds. AMF decodes PNG (WIC), not DDS -
export it as a 32-bit RGBA PNG instead.
```

---

## The INI

In `Data/SKSE/Plugins/ApocryphaMenuFramework.ini`:

```ini
[Skin]
sFrame=Interface/YourMod/frame.png
uFrameCorner=64
sBackground=Interface/YourMod/background.png
sPlates=Interface/YourMod/plates
```

Paths are relative to the game's `Data` folder, which is how you already think about your own mod's
files. An absolute path also works, which is handy while you are still moving a work-in-progress
file around outside the mod folder.

All four keys are optional. Leave one empty and that piece keeps the built-in look.

---

## Seeing your art, and iterating on it

The framework writes what actually loaded to
`Documents\My Games\Skyrim Special Edition\SKSE\ApocryphaMenuFramework.log`:

```
skin: frame loaded from "Data\Interface\YourMod\frame.png" (192x192)
skin: background loaded from "Data\Interface\YourMod\background.png" (256x256)
skin: toggle loaded from "Data\Interface\YourMod\plates\toggle.png" (64x32)
```

**You do not have to restart the game to see a change.** Save the PNG, then ask the framework to
reload its art. Through DevBench:

```
amf.process  op=skinreload      reloads every skin texture from disk and reports what loaded
amf.process  op=skin            reports what is loaded right now, without reloading
```

Both return the same structure, so "my art is not showing" is answered by asking the framework
rather than by guessing at paths:

```json
{"frame":{"what":"frame","path":"Data\\Interface\\YourMod\\frame.png","loaded":true,"w":192,"h":192,"error":""},
 "frameCorner":64,
 "background":{"what":"background",...,"loaded":false,"error":"file not found"},
 "backgroundTiles":true,
 "plates":[{"what":"toggle",...},{"what":"slider",...},{"what":"tab",...}]}
```

An image that fails to load is skipped and the built-in art draws in its place - a bad path or a
corrupt file can never leave the menu unusable or invisible.

---

## Things worth knowing before you draw

* **Leave the frame's centre fully transparent.** Any pixels there are drawn over the menu content.
* **Corners never stretch, edges always do.** Put detail in the corners; keep the edge band
  something that survives being stretched to any length - a repeating line, a bevel, a gradient.
* **The frame is drawn slightly outside the window rect** (a few pixels), so it reads as a frame
  *around* the panel rather than smudging into the panel's own 1px border.
* **A tiled background must be seamless**, since it repeats at its own pixel size.
* The menu is drawn at the player's resolution with a scale factor derived from it, so art is scaled
  up on a 4K display. Author the frame at a size that still looks right enlarged - 192px corners
  hold up better than 32px ones.
