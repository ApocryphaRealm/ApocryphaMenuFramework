#!/usr/bin/env python3
r"""Draw the art and write the INIs for the Vel'dun, Oathvein, Norden and Norden - Black themes (AMF 1.9.8).

WHY
    The owner (2026-09-25): "Download velduun ui, oathvein ui, and make amf themes based on them."
    Then (same day): "Add a norden theme and norden black theme as well".
    A theme in this project means replacement art, not a colour tint (Skin.h), so each theme is a
    theme INI plus its own frame, background and toggle plate.

    The art is DRAWN HERE, from scratch, to match each UI's look - none of Nithog's files are copied
    into AMF. What was taken from the two mods is their colour values (Vel'dun's own ImGui style,
    Patches\Vel'dun flick\FUCKs\FUCK\styles\Vel'dun.ini, and colours sampled from Oathvein's preview
    screenshots) and the shape language: Vel'dun's bone-coloured double line with cut corners and
    small diamonds; Oathvein's thin grey line with crossed scratch strokes at the corners and its
    blood-red highlight.

GEOMETRY
    Frames are 78x78 with a 26px corner - the same geometry as AMF's built-in knotwork
    (KnotworkBorder.h kWidth/kHeight/kCorner), so a themed frame sits exactly where the knotwork
    does around every window and pane and the layout does not move when the theme changes.
    Edge slices are uniform along their run, because the nine-slice stretches them.

Usage:  python tools/make-theme-art.py            (writes into dist/SKSE/Plugins/ApocryphaMenuFramework/themes)
"""
import os
import random

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.normpath(os.path.join(HERE, "..", "dist", "SKSE", "Plugins", "ApocryphaMenuFramework", "themes"))
DATA_REL = "SKSE/Plugins/ApocryphaMenuFramework/themes"

SS = 8           # supersampling factor for smooth lines
SIZE = 78        # frame texture size (matches the built-in knotwork)
CORNER = 26      # nine-slice corner


def rgba(hex_rgb, a=255):
    h = hex_rgb.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), a)


def canvas(w, h):
    return Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))


def down(img, w, h):
    return img.resize((w, h), Image.LANCZOS)


def S(v):
    return v * SS


# ---------------------------------------------------------------------------------------------
# Vel'dun - bone lines on warm dark brown; cut corners with a small diamond on each cut.

def veldun_frame():
    img = canvas(SIZE, SIZE)
    d = ImageDraw.Draw(img)
    bone = rgba("D1C7AE", 236)
    faint = rgba("D1C7AE", 110)

    def chamfer(inset, cut, colour, width):
        a, b = inset, SIZE - inset
        pts = [(a + cut, a), (b - cut, a), (b, a + cut), (b, b - cut),
               (b - cut, b), (a + cut, b), (a, b - cut), (a, a + cut), (a + cut, a)]
        d.line([(S(x), S(y)) for x, y in pts], fill=colour, width=max(1, round(S(width))), joint="curve")

    chamfer(2.5, 10.0, bone, 2.2)   # outer line, cut corners
    chamfer(7.0, 10.5, faint, 1.3)  # inner line, echoing Vel'dun's doubled rules

    # A diamond sitting on each cut, pointing out along the diagonal.
    m = 2.5 + 5.0   # the midpoint of each cut
    for cx, cy in ((m, m), (SIZE - m, m), (m, SIZE - m), (SIZE - m, SIZE - m)):
        r = 5.0
        d.polygon([(S(cx), S(cy - r)), (S(cx + r), S(cy)), (S(cx), S(cy + r)), (S(cx - r), S(cy))],
                  fill=rgba("1D1A17", 255), outline=bone, width=round(S(1.6)))
        d.polygon([(S(cx), S(cy - 2.0)), (S(cx + 2.0), S(cy)), (S(cx), S(cy + 2.0)), (S(cx - 2.0), S(cy))], fill=bone)
    return down(img, SIZE, SIZE)


def veldun_toggle():
    w, h = 128, 64
    img = canvas(w, h)
    d = ImageDraw.Draw(img)
    cut = 14
    pts = [(cut, 2), (w - cut, 2), (w - 2, h / 2), (w - cut, h - 2), (cut, h - 2), (2, h / 2)]
    d.polygon([(S(x), S(y)) for x, y in pts], fill=(255, 255, 255, 255), outline=(150, 142, 124, 255), width=S(3))
    return down(img, w, h)


# ---------------------------------------------------------------------------------------------
# Oathvein - a thin grey line on charcoal; two crossed scratch strokes through each corner.

def oathvein_frame():
    img = canvas(SIZE, SIZE)
    d = ImageDraw.Draw(img)
    grey = rgba("A6A8A7", 230)
    inset = 3.0
    a, b = inset, SIZE - inset
    d.rectangle([S(a), S(a), S(b), S(b)], outline=grey, width=round(S(1.6)))

    def scratch(x0, y0, x1, y1, alpha):
        # A stroke that fades toward both ends, like a blade mark rather than a ruled line.
        steps = 24
        for i in range(steps):
            t0, t1 = i / steps, (i + 1) / steps
            mid = (t0 + t1) / 2
            fade = 1.0 - abs(mid - 0.5) * 1.3
            col = rgba("CFD1D0", int(alpha * max(0.0, fade)))
            d.line([(S(x0 + (x1 - x0) * t0), S(y0 + (y1 - y0) * t0)),
                    (S(x0 + (x1 - x0) * t1), S(y0 + (y1 - y0) * t1))], fill=col, width=round(S(1.5)))

    # Each corner: a narrow X - one shallow and one steep stroke crossing just inside the corner,
    # the way Oathvein marks its panel corners - kept inside the corner slice, never the edges.
    for sx, sy, cx, cy in ((1, 1, a, a), (-1, 1, b, a), (1, -1, a, b), (-1, -1, b, b)):
        ox, oy = cx + sx * 7.0, cy + sy * 7.0       # the crossing point
        for dx, dy in ((0.87, 0.5), (0.5, 0.87)):   # 30 and 60 degrees
            scratch(ox - sx * dx * 9.0, oy - sy * dy * 9.0, ox + sx * dx * 17.5, oy + sy * dy * 17.5, 255)
    return down(img, SIZE, SIZE)


def oathvein_toggle():
    w, h = 128, 64
    img = canvas(w, h)
    d = ImageDraw.Draw(img)
    d.rectangle([S(2), S(2), S(w - 2), S(h - 2)], fill=(255, 255, 255, 255), outline=(120, 120, 120, 255), width=S(3))
    # one diagonal notch across the right end - Oathvein's slash, as a darker cut in the plate
    d.line([(S(w - 30), S(h - 3)), (S(w - 14), S(3))], fill=(150, 150, 150, 255), width=S(2))
    return down(img, w, h)


# ---------------------------------------------------------------------------------------------
# Norden - a thin slate line on grey (or black, for Norden - Black) with bright L-shaped ticks at
# the corners, the way Norden UI marks its item card and lists. Both Norden themes share this art.

def norden_frame():
    img = canvas(SIZE, SIZE)
    d = ImageDraw.Draw(img)
    slate = rgba("7D878C", 235)
    tick = rgba("D0D2D4", 250)
    inset = 3.0
    a, b = inset, SIZE - inset
    d.rectangle([S(a), S(a), S(b), S(b)], outline=slate, width=round(S(1.3)))
    arm, th = 11.0, 2.2
    for sx, sy, cx, cy in ((1, 1, a, a), (-1, 1, b, a), (1, -1, a, b), (-1, -1, b, b)):
        # the two arms of the L, laid on the line and running along each edge from the corner
        x0, x1 = sorted((cx, cx + sx * arm))
        y0, y1 = sorted((cy - sy * th / 2, cy + sy * th / 2))
        d.rectangle([S(x0 - (th / 2 if sx > 0 else -th / 2)), S(y0), S(x1), S(y1)], fill=tick)
        x0, x1 = sorted((cx - sx * th / 2, cx + sx * th / 2))
        y0, y1 = sorted((cy, cy + sy * arm))
        d.rectangle([S(x0), S(y0), S(x1), S(y1)], fill=tick)
    return down(img, SIZE, SIZE)


def norden_toggle():
    w, h = 128, 64
    img = canvas(w, h)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([S(2), S(6), S(w - 2), S(h - 6)], radius=S(10), fill=(255, 255, 255, 255),
                        outline=(140, 146, 150, 255), width=S(3))
    return down(img, w, h)


# ---------------------------------------------------------------------------------------------
# Backgrounds: a 256px tile of fine grain, mostly transparent, drawn OVER the theme's own panel
# colour - so the INI's sBackground still decides how dark and how opaque the panel is, and the
# tile only adds texture. Per-pixel noise has no structure, so the tile repeats without seams.

def grain_tile(seed, light, dark, density):
    rnd = random.Random(seed)
    img = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    px = img.load()
    for y in range(256):
        for x in range(256):
            r = rnd.random()
            if r < density:
                px[x, y] = light + (rnd.randint(6, 16),)
            elif r < density * 2:
                px[x, y] = dark + (rnd.randint(10, 26),)
    return img


THEMES = {
    "veldun": {
        "ini": """; Apocrypha Menu Framework theme - Vel'dun.
; Made to sit beside Vel'dun UI (Nithog, Nexus 176230): its bone line colour, warm dark panel and tan
; headings, taken from Vel'dun UI's own ImGui style. The art in veldun\\ was drawn for AMF; no files
; from Vel'dun UI are included, and Vel'dun UI is not required.
[Theme]
sName=Vel'dun
sBackground=#1D1A17
sFrame=#D1C7AE
sBorder=#D1C7AE
sText=#E4DDCC
sTextDim=#A39C8A
sAccent=#AAA07A
fBorderThickness=1.0
bKnotwork=0
sSkinFrame={rel}/veldun/frame.png
uSkinFrameCorner=26
sSkinBackground={rel}/veldun/background.png
sSkinPlates={rel}/veldun
""",
        "frame": veldun_frame,
        "toggle": veldun_toggle,
        "grain": (0x7EA1, (0xD1, 0xC7, 0xAE), (0x0A, 0x08, 0x06), 0.035),
    },
    "oathvein": {
        "ini": """; Apocrypha Menu Framework theme - Oathvein.
; Made to sit beside Oathvein UI (Nithog, Nexus 160916): its charcoal panels, thin grey lines and
; blood-red highlight, colours sampled from Oathvein UI's own screens. The art in oathvein\\ was drawn
; for AMF; no files from Oathvein UI are included, and Oathvein UI is not required.
[Theme]
sName=Oathvein
sBackground=#191919
sFrame=#A6A8A7
sBorder=#A6A8A7
sText=#E8E8E8
sTextDim=#8C8C8C
sAccent=#8E2A2E
fBorderThickness=1.0
bKnotwork=0
sSkinFrame={rel}/oathvein/frame.png
uSkinFrameCorner=26
sSkinBackground={rel}/oathvein/background.png
sSkinPlates={rel}/oathvein
""",
        "frame": oathvein_frame,
        "toggle": oathvein_toggle,
        "grain": (0x0A77, (0xB8, 0xBA, 0xB9), (0x00, 0x00, 0x00), 0.04),
    },
    "norden": {
        "ini": """; Apocrypha Menu Framework theme - Norden.
; Made to sit beside Norden UI (Nithog, Nexus 166086): its #333333 panels, silver-grey lines and
; highlights and bright corner ticks, colours taken from Norden UI's own menus. The art in norden/ was
; drawn for AMF; no files from Norden UI are included, and Norden UI is not required.
[Theme]
sName=Norden
sBackground=#333333
sFrame=#BBBDBF
sBorder=#8E9396
sText=#E5E5E5
sTextDim=#999999
sAccent=#BBBDBF
fBorderThickness=1.0
bKnotwork=0
sSkinFrame={rel}/norden/frame.png
uSkinFrameCorner=26
sSkinBackground={rel}/norden/background.png
sSkinPlates={rel}/norden
""",
        "frame": norden_frame,
        "toggle": norden_toggle,
        "grain": (0x40DE, (0xE5, 0xE5, 0xE5), (0x00, 0x00, 0x00), 0.03),
    },
    "norden-black": {
        "ini": """; Apocrypha Menu Framework theme - Norden - Black.
; Norden with its panel grey taken to black, the same rule as our Norden UI - Black recolour: the
; #333333 panel becomes black, lines, text and highlights stay Norden's. Uses the Norden art in norden/.
[Theme]
sName=Norden - Black
sBackground=#000000
sFrame=#BBBDBF
sBorder=#8E9396
sText=#E5E5E5
sTextDim=#999999
sAccent=#BBBDBF
fBorderThickness=1.0
bKnotwork=0
sSkinFrame={rel}/norden/frame.png
uSkinFrameCorner=26
sSkinBackground={rel}/norden/background.png
sSkinPlates={rel}/norden
""",
    },
}


def main():
    for tid, t in THEMES.items():
        if "frame" in t:   # a theme without art of its own points at another theme's folder
            folder = os.path.join(OUT, tid)
            os.makedirs(folder, exist_ok=True)
            t["frame"]().save(os.path.join(folder, "frame.png"))
            t["toggle"]().save(os.path.join(folder, "toggle.png"))
            grain_tile(*t["grain"]).save(os.path.join(folder, "background.png"))
        with open(os.path.join(OUT, tid + ".ini"), "w", encoding="utf-8", newline="\r\n") as f:
            f.write(t["ini"].format(rel=DATA_REL))
        print("wrote", tid)


if __name__ == "__main__":
    main()
