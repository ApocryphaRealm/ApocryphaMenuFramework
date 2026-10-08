"""Draw the art library's own parts (AMF 2.1.6) into dist/SKSE/Plugins/ApocryphaMenuFramework/assets.

The owner, 2026-10-08: "i want 5 art options for each kind that all have different shapes, include the default as one of
those 5". The library then holds, per kind:

    frames       skyrim-knotwork (the default), norden, oathvein, veldun, oblivion-map-edge   - made by make-asset-library.py
    backgrounds  plain (the default: no picture, the theme's colour), grain, parchment, weave, lattice
    toggles      rounded (the default: the built-in switch, no picture), norden, oathvein, veldun, oblivion-scroll

This script draws the ones that are not a theme's own art from 1.9.8: the four background tiles and the Oblivion scroll
switch. The three themes' grain tiles were the same speckle three times (3-4% noise, colours a shade apart), so they are
one part, grain (Norden's), which every one of those themes names.

Everything is original art drawn from shapes; no game or mod files are used. The tiles are drawn in neutral light and dark
at low strength so they sit on any theme's background colour and take the Frame art tint (Appearance > Colours).
Run from the repo root:  python tools/make-art-parts.py
"""
import os
import random

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "dist", "SKSE", "Plugins", "ApocryphaMenuFramework", "assets")
TILE = 256   # <= skin::kTileThreshold, so it tiles at its own size
SS = 8       # supersampling for smooth edges on the switch plate


def wrap_noise(seed, cells):
    """Smooth value noise that tiles: a random grid repeated 3x3, upsampled, centre cut out."""
    rnd = random.Random(seed)
    small = Image.new("L", (cells, cells))
    small.putdata([rnd.randint(0, 255) for _ in range(cells * cells)])
    big = Image.new("L", (cells * 3, cells * 3))
    for i in range(3):
        for j in range(3):
            big.paste(small, (i * cells, j * cells))
    big = big.resize((TILE * 3, TILE * 3), Image.BICUBIC).filter(ImageFilter.GaussianBlur(TILE / cells / 4))
    return big.crop((TILE, TILE, TILE * 2, TILE * 2))


def parchment():
    # Aged paper: broad soft blotches (two octaves of noise) darkening and lightening the ground, plus a fine fibre speckle.
    broad = wrap_noise(0x0B11, 6)
    fine = wrap_noise(0x0B12, 24)
    rnd = random.Random(0x0B13)
    img = Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0))
    px = img.load()
    b, f = broad.load(), fine.load()
    for y in range(TILE):
        for x in range(TILE):
            v = (b[x, y] * 0.7 + f[x, y] * 0.3) / 255.0 - 0.5   # -0.5 .. 0.5
            if rnd.random() < 0.02:
                px[x, y] = (60, 44, 28, rnd.randint(18, 34))      # a fibre fleck
            elif v < 0:
                px[x, y] = (70, 52, 34, int(min(1.0, -v * 2.2) * 34))   # stained
            else:
                px[x, y] = (255, 250, 238, int(min(1.0, v * 2.2) * 16))  # faded lighter
    return img


def weave():
    # Linen: threads over and under in 4 px bands, a lit top edge and a shaded gap on every thread.
    img = Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0))
    px = img.load()
    rnd = random.Random(0x3EA7)
    for y in range(TILE):
        for x in range(TILE):
            horiz = ((x // 4) + (y // 4)) % 2 == 0   # which thread is on top in this cell
            along = y % 4 if horiz else x % 4
            if along == 0:
                px[x, y] = (0, 0, 0, 30 + rnd.randint(0, 6))           # the gap between threads
            elif along == 1:
                px[x, y] = (255, 255, 255, 14 + rnd.randint(0, 4))     # the thread's lit edge
            else:
                px[x, y] = (255, 255, 255, rnd.randint(0, 5))          # the thread's body
    return img


def lattice():
    # Diamond lattice: thin raised lines on 32 px diagonals, lit on one side and shaded on the other; a small stud
    # where two lines cross.
    img = Image.new("RGBA", (TILE, TILE), (0, 0, 0, 0))
    px = img.load()
    P = 32
    for y in range(TILE):
        for x in range(TILE):
            a, b = (x + y) % P, (x - y) % P
            if a == 0 or b == 0:
                px[x, y] = (255, 255, 255, 30)
            elif a == 1 or b == 1:
                px[x, y] = (0, 0, 0, 36)
    d = ImageDraw.Draw(img)
    for cy in range(0, TILE + P, P):
        for cx in range(0, TILE + P, P):
            for ox, oy in ((0, 0), (P // 2, P // 2)):
                x, y = (cx + ox) % TILE, (cy + oy) % TILE
                if (x + y) % P == 0 and (x - y) % P == 0:
                    d.ellipse([x - 2, y - 2, x + 2, y + 2], fill=(255, 255, 255, 40))
    return img


def oblivion_scroll():
    # A scroll cartouche: a bar whose two ends curve inward like a rolled sheet, outlined in brown-gold. White fill so
    # the Switch on / off colour shows through the tint, as on the other plates.
    w, h = 128, 64
    W, H = w * SS, h * SS
    mask = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(mask)
    top, bot = 4 * SS, (h - 4) * SS
    d.rectangle([2 * SS, top, W - 2 * SS, bot], fill=255)
    r = 15 * SS                               # the inward curve at each end
    d.ellipse([2 * SS - r, H / 2 - r, 2 * SS + r, H / 2 + r], fill=0)
    d.ellipse([W - 2 * SS - r, H / 2 - r, W - 2 * SS + r, H / 2 + r], fill=0)
    inner = mask.filter(ImageFilter.MinFilter(3 * SS + 1))
    band = ImageChops.subtract(mask, inner)
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    img.paste((255, 255, 255, 255), (0, 0), inner)
    img.paste((150, 112, 52, 255), (0, 0), band)
    return img.resize((w, h), Image.LANCZOS)


def main():
    for kind in ("backgrounds", "toggles"):
        os.makedirs(os.path.join(ASSETS, kind), exist_ok=True)
    parchment().save(os.path.join(ASSETS, "backgrounds", "parchment.png"))
    weave().save(os.path.join(ASSETS, "backgrounds", "weave.png"))
    lattice().save(os.path.join(ASSETS, "backgrounds", "lattice.png"))
    oblivion_scroll().save(os.path.join(ASSETS, "toggles", "oblivion-scroll.png"))
    print("drew parchment, weave, lattice, oblivion-scroll")


if __name__ == "__main__":
    main()
