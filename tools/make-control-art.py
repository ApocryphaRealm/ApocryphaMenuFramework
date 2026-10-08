"""Draw the control kinds' parts for AMF's art library (2.1.6).

The owner, 2026-10-08: "I want more than just those three kinds. I want like the scroll bar. I want the box shapes because
the boxes are different from the frame. And I want sliders ... the toggles ... the lines that appear in a mod menu as a
horizontal line under a section header ... the on-screen keyboard ... and anything else that you can think of", with five
shapes of each kind, the default one of them.

The default of every control kind is the built-in shape (Dear ImGui's own, no picture). The other four are drawn here, one
per theme's shape language, so a kind's five are all different shapes and a theme's parts look like one family:

    norden    rounded corners, bright L-ticks on the corners          (Norden's corner ticks)
    oathvein  square, one corner cut on the slant, a scratch across it (Oathvein's crossed scratches)
    veldun    every corner cut off, an inner second line              (Vel'dun's cut corners)
    oblivion  corners scooped inward like a scroll, brass studs       (the Oblivion port's scroll shapes)

Kinds and files (assets/<folder>/<theme>.png, at twice their 1080p size so they stay sharp at 4K):
    boxes/       fields: <n>.png (the shape, white, tinted with the field's colour) + <n>-edge.png (outline, line colour)
    buttons/     the same family with a double outline
    tickboxes/   a square + <n>-mark.png (the tick: check, crossed blades, diamond, compass star)
    sliders/     the grab
    scrollbars/  the grab + <n>-track.png / <n>-track-edge.png
    tabs/        the shape on top only
    sections/    a strip with end caps (uCorner = the cap) - the line under a section heading
    arrows/      pointing right: chevron, blade, diamond, arrowhead
    cursors/     pointers in their own colours (not tinted), with the hot spot in the .ini

Original art drawn from shapes; no game or mod files. Run from the repo root:  python tools/make-control-art.py
"""
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "dist", "SKSE", "Plugins", "ApocryphaMenuFramework", "assets")
SS = 8
THEMES = ("norden", "oathvein", "veldun", "oblivion")
WHITE = (255, 255, 255, 255)


# ---------------------------------------------------------------------------------------------------------------------
# helpers - all coordinates in the part's own pixels (the 2x size); drawing happens SS times larger and is scaled down

def S(v):
    return v * SS


def IS(v):
    return int(round(S(v)))


def mask_canvas(w, h):
    return Image.new("L", (IS(w), IS(h)), 0)


def shape_mask(theme, w, h, r, top_only=False):
    """The theme's shape filling w x h, corner size r."""
    m = mask_canvas(w, h)
    d = ImageDraw.Draw(m)
    W, H = S(w), S(h)
    R = S(r)
    bot = H * 3 if top_only else H   # top-only shapes run off the bottom (tabs)
    if theme == "norden":
        d.rounded_rectangle([0, 0, W - 1, bot], radius=int(R * 0.6), fill=255)
    elif theme == "oathvein":
        d.polygon([(0, 0), (W - R, 0), (W, R), (W, bot), (0, bot)], fill=255)
    elif theme == "veldun":
        c = R * 0.7
        pts = [(c, 0), (W - c, 0), (W, c), (W, bot - (0 if top_only else c)), (W - (0 if top_only else c), bot),
               ((0 if top_only else c), bot), (0, bot - (0 if top_only else c)), (0, c)]
        d.polygon(pts, fill=255)
    elif theme == "oblivion":
        d.rectangle([0, 0, W - 1, bot], fill=255)
        q = R * 0.75
        corners = [(0, 0), (W, 0)] + ([] if top_only else [(0, H), (W, H)])
        for cx, cy in corners:
            d.ellipse([cx - q, cy - q, cx + q, cy + q], fill=0)
    return m.crop((0, 0, W, H))


def outline(mask, width):
    """The band of a mask within `width` part-pixels of its edge."""
    k = int(S(width)) | 1
    # padded with empty space first: MinFilter treats pixels past the image's border as copies of the border, so a shape
    # that touches the canvas edge would get no line along that side
    pad = k
    big = Image.new("L", (mask.size[0] + pad * 2, mask.size[1] + pad * 2), 0)
    big.paste(mask, (pad, pad))
    inner = big.filter(ImageFilter.MinFilter(k)).crop((pad, pad, pad + mask.size[0], pad + mask.size[1]))
    return ImageChops.subtract(mask, inner)


def to_rgba(mask, w, h, col=WHITE):
    img = Image.new("RGBA", mask.size, (0, 0, 0, 0))
    img.paste(col, (0, 0), mask)
    return img.resize((w, h), Image.LANCZOS)


def decor(theme, d, w, h, r, top_only=False):
    """Each theme's mark on the edge layer, drawn white into a mask."""
    W, H, R = S(w), S(h), S(r)
    t = S(2.2)
    if theme == "norden":           # bright L-ticks on the corners
        L = R * 0.9
        corners = [(0, 0, 1, 1), (W, 0, -1, 1)] + ([] if top_only else [(0, H, 1, -1), (W, H, -1, -1)])
        for cx, cy, sx, sy in corners:
            x0 = cx + sx * S(1.5)
            y0 = cy + sy * S(1.5)
            d.line([(x0, y0 + sy * L), (x0, y0), (x0 + sx * L, y0)], fill=255, width=int(t * 1.4))
    elif theme == "oathvein":       # a scratch across the cut corner
        for i in range(2):
            o = S(3 + i * 4)
            d.line([(W - R - o, S(2)), (W - S(2), R + o)], fill=200 - i * 80, width=int(t * 0.8))
    elif theme == "veldun":         # an inner second line
        c = R * 0.7
        i = S(4)
        bot = H * 3 if top_only else H - i
        pts = [(c + i * 0.4, i), (W - c - i * 0.4, i), (W - i, c + i * 0.4), (W - i, bot), (i, bot), (i, c + i * 0.4)]
        if not top_only:
            pts = [(c + i * 0.4, i), (W - c - i * 0.4, i), (W - i, c + i * 0.4), (W - i, H - c - i * 0.4), (W - c - i * 0.4, H - i),
                   (c + i * 0.4, H - i), (i, H - c - i * 0.4), (i, c + i * 0.4)]
        d.line(pts + [pts[0]], fill=170, width=int(t * 0.7))
    elif theme == "oblivion":       # brass studs where the corners scoop in
        q = R * 0.75
        rr = S(2.2)
        corners = [(q * 0.72, q * 0.72), (W - q * 0.72, q * 0.72)] + ([] if top_only else [(q * 0.72, H - q * 0.72), (W - q * 0.72, H - q * 0.72)])
        for cx, cy in corners:
            d.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=255)


def plate(theme, w, h, r, top_only=False, double=False):
    """(main, edge) RGBA images for a nine-sliced control."""
    m = shape_mask(theme, w, h, r, top_only)
    edge = outline(m, 2.6)
    if top_only:   # no line along the open bottom
        cut = Image.new("L", m.size, 255)
        ImageDraw.Draw(cut).rectangle([0, m.size[1] - S(3), m.size[0], m.size[1]], fill=0)
        edge = ImageChops.multiply(edge, cut)
    d = ImageDraw.Draw(edge)
    decor(theme, d, w, h, r, top_only)
    if double:     # buttons: a second outline inside the first
        inner = m.filter(ImageFilter.MinFilter(int(S(5)) | 1))
        edge = ImageChops.lighter(edge, ImageChops.multiply(outline(inner, 1.6), Image.new("L", m.size, 190)))
    edge = ImageChops.multiply(edge, m)   # nothing outside the shape
    return to_rgba(m, w, h), to_rgba(edge, w, h)


def save(img, folder, name):
    os.makedirs(os.path.join(ASSETS, folder), exist_ok=True)
    img.save(os.path.join(ASSETS, folder, name + ".png"))


def ini(folder, name, lines, note):
    with open(os.path.join(ASSETS, folder, name + ".ini"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write("; " + note + "\n[Part]\n" + "\n".join(lines) + "\n")


# ---------------------------------------------------------------------------------------------------------------------
# marks, arrows, cursors, section lines

def tick_mark(theme, n=48):
    m = mask_canvas(n, n)
    d = ImageDraw.Draw(m)
    N = S(n)
    if theme == "norden":      # a check
        d.line([(N * 0.18, N * 0.52), (N * 0.42, N * 0.76), (N * 0.84, N * 0.24)], fill=255, width=int(N * 0.14), joint="curve")
    elif theme == "oathvein":  # crossed blades
        for a, b in (((N * 0.2, N * 0.2), (N * 0.8, N * 0.8)), ((N * 0.8, N * 0.2), (N * 0.2, N * 0.8))):
            d.polygon(blade(a, b, N * 0.09), fill=255)
    elif theme == "veldun":    # a diamond
        c = N / 2
        d.polygon([(c, N * 0.14), (N * 0.86, c), (c, N * 0.86), (N * 0.14, c)], fill=255)
    elif theme == "oblivion":  # a four-point compass star
        d.polygon(star(N / 2, N / 2, N * 0.40, N * 0.12, 4), fill=255)
    return to_rgba(m, n, n)


def blade(a, b, half):
    """A long thin diamond from a to b - a blade stroke."""
    (x0, y0), (x1, y1) = a, b
    dx, dy = x1 - x0, y1 - y0
    L = math.hypot(dx, dy)
    nx, ny = -dy / L * half, dx / L * half
    mx, my = x0 + dx * 0.5, y0 + dy * 0.5
    return [(x0, y0), (mx + nx, my + ny), (x1, y1), (mx - nx, my - ny)]


def star(cx, cy, ro, ri, points, rot=-math.pi / 2):
    pts = []
    for i in range(points * 2):
        r = ro if i % 2 == 0 else ri
        a = rot + i * math.pi / points
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def arrow(theme, n=64):
    m = mask_canvas(n, n)
    d = ImageDraw.Draw(m)
    N = S(n)
    if theme == "norden":      # a chevron
        d.line([(N * 0.3, N * 0.14), (N * 0.72, N * 0.5), (N * 0.3, N * 0.86)], fill=255, width=int(N * 0.16), joint="curve")
    elif theme == "oathvein":  # a long thin blade
        d.polygon([(N * 0.08, N * 0.43), (N * 0.42, N * 0.33), (N * 0.96, N * 0.5), (N * 0.42, N * 0.67), (N * 0.08, N * 0.57)], fill=255)
    elif theme == "veldun":    # a diamond with a stem
        d.polygon([(N * 0.42, N * 0.18), (N * 0.94, N * 0.5), (N * 0.42, N * 0.82), (N * 0.6, N * 0.5)], fill=255)
        d.rectangle([N * 0.06, N * 0.44, N * 0.5, N * 0.56], fill=255)
    elif theme == "oblivion":  # an arrowhead with a notched back, like a fletched arrow's
        d.polygon([(N * 0.94, N * 0.5), (N * 0.18, N * 0.1), (N * 0.38, N * 0.5), (N * 0.18, N * 0.9)], fill=255)
    return to_rgba(m, n, n)


def section(theme, w=512, h=32, cap=64):
    m = mask_canvas(w, h)
    d = ImageDraw.Draw(m)
    W, H, C = S(w), S(h), S(cap)
    cy = H / 2
    t = S(2.4)
    if theme == "norden":      # a line with bright upright ticks at both ends
        d.line([(S(6), cy), (W - S(6), cy)], fill=200, width=int(t))
        for x in (S(6), W - S(6)):
            d.line([(x, cy - S(9)), (x, cy + S(9))], fill=255, width=int(t * 1.4))
    elif theme == "oathvein":  # a blade stroke that thins to nothing at both ends
        d.polygon([(S(2), cy), (C, cy - S(2.2)), (W - C, cy - S(2.2)), (W - S(2), cy), (W - C, cy + S(2.2)), (C, cy + S(2.2))], fill=255)
    elif theme == "veldun":    # a double line ending in diamonds
        for dy in (-S(3.5), S(3.5)):
            d.line([(C * 0.55, cy + dy), (W - C * 0.55, cy + dy)], fill=210, width=int(t * 0.7))
        for x in (C * 0.32, W - C * 0.32):
            d.polygon([(x - S(10), cy), (x, cy - S(10)), (x + S(10), cy), (x, cy + S(10))], fill=255)
    elif theme == "oblivion":  # a heavy line and a thin one, with a compass star at each end
        d.line([(C * 0.7, cy - S(2)), (W - C * 0.7, cy - S(2))], fill=255, width=int(t * 1.2))
        d.line([(C * 0.7, cy + S(4)), (W - C * 0.7, cy + S(4))], fill=170, width=int(t * 0.6))
        for x in (C * 0.38, W - C * 0.38):
            d.polygon(star(x, cy, S(13), S(4), 4), fill=255)
    return to_rgba(m, w, h)


def cursor(theme, n=64):
    """Pointers in their own colours, the tip at the hot spot."""
    img = Image.new("RGBA", (S(n), S(n)), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    N = S(n)
    if theme == "norden":      # a sleek silver arrow, black outline
        pts = [(N * 0.06, N * 0.04), (N * 0.06, N * 0.78), (N * 0.26, N * 0.6), (N * 0.4, N * 0.92), (N * 0.52, N * 0.86),
               (N * 0.38, N * 0.56), (N * 0.64, N * 0.56)]
        d.polygon(pts, fill=(20, 20, 22, 255))
        inner = shrink(pts, S(3.2))
        d.polygon(inner, fill=(225, 228, 232, 255))
        hot = (4, 3)
    elif theme == "oathvein":  # a dagger pointing to the upper left, a red line along the blade
        tip = (N * 0.05, N * 0.05)
        d.polygon([tip, (N * 0.5, N * 0.38), (N * 0.38, N * 0.5)], fill=(18, 18, 18, 255))
        d.polygon(shrink([tip, (N * 0.5, N * 0.38), (N * 0.38, N * 0.5)], S(2.6)), fill=(200, 202, 204, 255))
        d.line([(N * 0.1, N * 0.1), (N * 0.4, N * 0.4)], fill=(150, 40, 46, 255), width=IS(2))
        d.line([(N * 0.32, N * 0.58), (N * 0.58, N * 0.32)], fill=(18, 18, 18, 255), width=IS(7))       # guard
        d.line([(N * 0.33, N * 0.57), (N * 0.57, N * 0.33)], fill=(120, 122, 124, 255), width=IS(3.5))
        d.line([(N * 0.48, N * 0.48), (N * 0.8, N * 0.8)], fill=(18, 18, 18, 255), width=IS(7))          # grip
        d.line([(N * 0.49, N * 0.49), (N * 0.79, N * 0.79)], fill=(90, 62, 44, 255), width=IS(3.5))
        hot = (3, 3)
    elif theme == "veldun":    # an angular bone-coloured pointer with a cut tail
        pts = [(N * 0.06, N * 0.04), (N * 0.62, N * 0.4), (N * 0.38, N * 0.46), (N * 0.52, N * 0.8), (N * 0.4, N * 0.86),
               (N * 0.26, N * 0.52), (N * 0.06, N * 0.68)]
        d.polygon(pts, fill=(28, 24, 20, 255))
        d.polygon(shrink(pts, S(3.2)), fill=(214, 204, 178, 255))
        hot = (4, 3)
    else:                      # oblivion: a quill pen, nib at the hot spot
        tip = (N * 0.06, N * 0.06)
        d.polygon([tip, (N * 0.22, N * 0.16), (N * 0.16, N * 0.22)], fill=(40, 30, 20, 255))                 # nib
        vane = [(N * 0.16, N * 0.16), (N * 0.5, N * 0.24), (N * 0.9, N * 0.66), (N * 0.62, N * 0.62), (N * 0.24, N * 0.5)]
        d.polygon(vane, fill=(60, 42, 26, 255))
        d.polygon(shrink(vane, S(2.6)), fill=(236, 226, 200, 255))
        d.line([(N * 0.18, N * 0.18), (N * 0.86, N * 0.86)], fill=(150, 112, 52, 255), width=IS(2.4))       # shaft
        hot = (4, 4)
    return img.resize((n, n), Image.LANCZOS), hot


def shrink(pts, by):
    """Move every point of a polygon toward its centroid by `by` (a cheap inset for an outline)."""
    cx = sum(p[0] for p in pts) / len(pts)
    cy = sum(p[1] for p in pts) / len(pts)
    out = []
    for x, y in pts:
        dx, dy = x - cx, y - cy
        L = math.hypot(dx, dy) or 1.0
        out.append((x - dx / L * by, y - dy / L * by))
    return out


# ---------------------------------------------------------------------------------------------------------------------

def main():
    note = "drawn for AMF's %s look (tools/make-control-art.py) - original art from shapes"
    for t in THEMES:
        n = note % t
        main_, edge = plate(t, 64, 64, 16)
        save(main_, "boxes", t); save(edge, "boxes", t + "-edge")
        ini("boxes", t, ["uCorner=16", "uDrawCorner=8"], n)

        main_, edge = plate(t, 64, 64, 16, double=True)
        save(main_, "buttons", t); save(edge, "buttons", t + "-edge")
        ini("buttons", t, ["uCorner=16", "uDrawCorner=8"], n)

        main_, edge = plate(t, 48, 48, 12)
        save(main_, "tickboxes", t); save(edge, "tickboxes", t + "-edge"); save(tick_mark(t), "tickboxes", t + "-mark")
        ini("tickboxes", t, ["uCorner=12", "uDrawCorner=6"], n)

        main_, edge = plate(t, 24, 48, 8)
        save(main_, "sliders", t); save(edge, "sliders", t + "-edge")
        ini("sliders", t, ["uCorner=8", "uDrawCorner=4"], n)

        main_, edge = plate(t, 24, 64, 10)
        save(main_, "scrollbars", t); save(edge, "scrollbars", t + "-edge")
        tm, te = plate(t, 24, 64, 10)
        # the track: the same shape, narrowed to a groove down the middle
        groove = Image.new("RGBA", (24, 64), (0, 0, 0, 0))
        groove.paste(tm.resize((10, 64), Image.LANCZOS), (7, 0))
        groove_edge = Image.new("RGBA", (24, 64), (0, 0, 0, 0))
        groove_edge.paste(te.resize((10, 64), Image.LANCZOS), (7, 0))
        save(groove, "scrollbars", t + "-track"); save(groove_edge, "scrollbars", t + "-track-edge")
        ini("scrollbars", t, ["uCorner=10", "uDrawCorner=5"], n)

        main_, edge = plate(t, 96, 48, 16, top_only=True)
        save(main_, "tabs", t); save(edge, "tabs", t + "-edge")
        ini("tabs", t, ["uCorner=16", "uDrawCorner=8"], n)

        save(section(t), "sections", t)
        ini("sections", t, ["; the end caps: 64 px in the art, 32 px on a 1080p screen (the strip is drawn 16 px tall there)",
                            "uCorner=64", "uDrawCorner=32"], n)

        save(arrow(t), "arrows", t)

        img, hot = cursor(t)
        save(img, "cursors", t)
        ini("cursors", t, ["; 24 px tall on a 1080p screen; the hot spot is the tip, in the art's own pixels",
                           "uDrawCorner=24", "uHotX=%d" % hot[0], "uHotY=%d" % hot[1]], n)
        print("drew", t)


if __name__ == "__main__":
    main()
