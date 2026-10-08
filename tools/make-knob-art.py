"""Build the switch knobs: dist/SKSE/Plugins/ApocryphaMenuFramework/assets/knobs/<name>.png (2.1.6).

The owner, 2026-10-08, looking at the switch art: "the art that you made for the switches only changed the outline shape of
the switch, but not the button or circle that's white that's on top of it, which should have a different shape as well",
then "Those knobs should be a separate thing and should be gold or match the frame art", and, once the section lines were
redrawn: "Let's use the art you built at the end of each one of those section lines or some variation of them for the
toggle knobs ... for some of them they just won't look right so you'll have to have something different but they're a good
place to start and they would match".

So a knob is its own kind of art (Switch knobs on Appearance > Art), one per theme, made from that theme's section-line end
piece and drawn in the colours of the theme's frame art (the knobs are tinted only by the Frame art colour, white = as drawn):

    veldun           the cut diamond with its rivets, bone and brass
    oblivion         the compass star in its ring, gold on dark
    oathvein         the blade point, made a short ridged blade, steel
    norden           the serifed bar does not read as a knob, so a rounded plate with Norden's corner ticks, silver
    skyrim           the knot from the knotwork frame's corner, mirrored four ways (Skyrim's section line ends in a
                     small square knot too)

Every shape has a dark outline, so a knob reads on any switch colour. Drawn 8x larger and scaled down.
Run from the repo root:  python tools/make-knob-art.py
"""
import math
import os

from PIL import Image, ImageDraw, ImageOps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "dist", "SKSE", "Plugins", "ApocryphaMenuFramework", "assets")
FRAMES = os.path.join(ASSETS, "frames")
KNOBS = os.path.join(ASSETS, "knobs")
SIZE = 128       # 2.1.6 sharpness pass: written at 128 (was 64), so a knob is always scaled down on screen
SS = 16          # one design pixel (the shapes were laid out on a 64 px knob) on the canvas
N = SIZE * 8     # drawn 8x larger than written, then scaled down
C = N / 2
DARK = (14, 12, 10, 240)
OUT = 3.2 * SS                       # outline width


def star(cx, cy, ro, ri, points, rot=-math.pi / 2):
    pts = []
    for i in range(points * 2):
        r = ro if i % 2 == 0 else ri
        a = rot + i * math.pi / points
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def grow(points, by):
    cx = sum(p[0] for p in points) / len(points)
    cy = sum(p[1] for p in points) / len(points)
    out = []
    for x, y in points:
        dx, dy = x - cx, y - cy
        n = math.hypot(dx, dy) or 1.0
        out.append((x + dx / n * by, y + dy / n * by))
    return out


def canvas():
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    return img, ImageDraw.Draw(img)


def done(img):
    return img.resize((SIZE, SIZE), Image.LANCZOS)


def veldun():
    img, d = canvas()
    bone, brass = (206, 196, 170, 255), (150, 128, 84, 255)
    r1, r2 = N * 0.47, N * 0.19
    outer = [(C - r1, C), (C, C - r1), (C + r1, C), (C, C + r1)]
    d.polygon(grow(outer, OUT), fill=DARK)
    d.polygon(outer, fill=bone)
    # a bevel: the lower half of the face a shade darker
    d.polygon([(C - r1 * 0.86, C + 1), (C + r1 * 0.86, C + 1), (C, C + r1 * 0.86)], fill=(186, 176, 150, 255))
    inner = [(C - r2, C), (C, C - r2), (C + r2, C), (C, C + r2)]
    d.polygon(grow(inner, OUT * 0.8), fill=DARK)
    d.polygon(inner, fill=(46, 40, 30, 255))
    for x, y in ((C, C - r1 * 0.58), (C, C + r1 * 0.58), (C - r1 * 0.58, C), (C + r1 * 0.58, C)):   # the rivets
        rr = N * 0.045
        d.ellipse([x - rr - OUT * 0.6, y - rr - OUT * 0.6, x + rr + OUT * 0.6, y + rr + OUT * 0.6], fill=DARK)
        d.ellipse([x - rr, y - rr, x + rr, y + rr], fill=brass)
    return done(img)


def oblivion():
    img, d = canvas()
    gold, deep = (222, 184, 92, 255), (96, 66, 30, 255)
    rr = N * 0.47
    d.ellipse([C - rr - OUT, C - rr - OUT, C + rr + OUT, C + rr + OUT], fill=DARK)
    d.ellipse([C - rr, C - rr, C + rr, C + rr], fill=gold)                       # the ring
    ri = rr - N * 0.07
    d.ellipse([C - ri, C - ri, C + ri, C + ri], fill=(58, 40, 22, 255))           # the dark field
    big = star(C, C, N * 0.40, N * 0.095, 4)
    d.polygon(grow(big, OUT * 0.7), fill=DARK)
    d.polygon(big, fill=gold)
    small = star(C, C, N * 0.24, N * 0.07, 4, rot=-math.pi / 4)                   # the half-winds behind
    d.polygon(small, fill=(176, 138, 64, 255))
    d.polygon(big, fill=gold)
    # each point lit on one side
    for i in range(4):
        a = -math.pi / 2 + i * math.pi / 2
        tip = (C + N * 0.40 * math.cos(a), C + N * 0.40 * math.sin(a))
        side = (C + N * 0.095 * math.cos(a + math.pi / 4), C + N * 0.095 * math.sin(a + math.pi / 4))
        d.polygon([(C, C), tip, side], fill=(246, 214, 132, 255))
    cd = N * 0.05
    d.ellipse([C - cd, C - cd, C + cd, C + cd], fill=deep)
    return done(img)


def oathvein():
    img, d = canvas()
    steel, shade = (204, 206, 210, 255), (150, 152, 158, 255)
    hw, hh = N * 0.48, N * 0.30
    blade = [(C - hw, C), (C - hw * 0.45, C - hh), (C + hw * 0.45, C - hh), (C + hw, C), (C + hw * 0.45, C + hh), (C - hw * 0.45, C + hh)]
    d.polygon(grow(blade, OUT), fill=DARK)
    d.polygon(blade, fill=steel)
    d.polygon([(C - hw, C), (C + hw, C), (C + hw * 0.45, C + hh), (C - hw * 0.45, C + hh)], fill=shade)   # the lower bevel
    d.line([(C - hw * 0.8, C), (C + hw * 0.8, C)], fill=DARK, width=int(SS * 2.2))                         # the ridge
    d.line([(C - hw * 0.25, C - hh * 0.75), (C + hw * 0.05, C + hh * 0.75)], fill=(150, 40, 46, 255), width=int(SS * 2))  # Oathvein's scratch
    return done(img)


def norden():
    img, d = canvas()
    silver, bright = (214, 218, 222, 255), (250, 252, 255, 255)
    m = N * 0.06
    d.rounded_rectangle([m - OUT, m - OUT, N - m + OUT, N - m + OUT], radius=N * 0.2 + OUT, fill=DARK)
    d.rounded_rectangle([m, m, N - m, N - m], radius=N * 0.2, fill=silver)
    d.rounded_rectangle([m + N * 0.08, m + N * 0.5, N - m - N * 0.08, N - m - N * 0.05], radius=N * 0.12, fill=(196, 200, 206, 255))
    t, L = N * 0.055, N * 0.2                       # Norden's corner ticks
    for sx, sy in ((0, 0), (1, 0), (0, 1), (1, 1)):
        x0 = m + N * 0.13 if sx == 0 else N - m - N * 0.13
        y0 = m + N * 0.13 if sy == 0 else N - m - N * 0.13
        dx = 1 if sx == 0 else -1
        dy = 1 if sy == 0 else -1
        for rect in ([x0, y0, x0 + dx * L, y0 + dy * t], [x0, y0, x0 + dx * t, y0 + dy * L]):
            x_a, x_b = sorted((rect[0], rect[2]))
            y_a, y_b = sorted((rect[1], rect[3]))
            d.rectangle([x_a - OUT * 0.5, y_a - OUT * 0.5, x_b + OUT * 0.5, y_b + OUT * 0.5], fill=DARK)
            d.rectangle([x_a, y_a, x_b, y_b], fill=bright)
    return done(img)


def skyrim_knotwork():
    """The knotwork frame's corner knot, mirrored four ways, over a dark disc."""
    cut = 26
    piece = Image.open(os.path.join(FRAMES, "skyrim-knotwork.png")).convert("RGBA").crop((0, 0, cut, cut))
    w, h = piece.size
    knot = Image.new("RGBA", (w * 2, h * 2), (0, 0, 0, 0))
    knot.alpha_composite(piece, (0, 0))
    knot.alpha_composite(ImageOps.mirror(piece), (w, 0))
    knot.alpha_composite(ImageOps.flip(piece), (0, h))
    knot.alpha_composite(ImageOps.flip(ImageOps.mirror(piece)), (w, h))
    img, d = canvas()
    mm = N * 0.1
    d.ellipse([mm, mm, N - mm, N - mm], fill=(12, 10, 8, 170))
    img = done(img)
    img.alpha_composite(knot.resize((SIZE, SIZE), Image.LANCZOS))
    return img


def main():
    os.makedirs(KNOBS, exist_ok=True)
    for old in os.listdir(KNOBS):          # the first set was named after the frames
        if old.endswith(".png"):
            os.remove(os.path.join(KNOBS, old))
    for name, fn in (("veldun", veldun), ("oblivion", oblivion), ("oathvein", oathvein), ("norden", norden),
                     ("skyrim", skyrim_knotwork)):
        fn().save(os.path.join(KNOBS, name + ".png"))
        print("knobs/" + name + ".png")


if __name__ == "__main__":
    main()
