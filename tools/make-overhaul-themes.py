"""Draw the DRAFT themes that match the major Skyrim UI overhauls (themes-draft branch, for the owner's review).

The owner, 2026-10-11: "the art expansion by downloading all of the different UI overhauls and adding themes and assets for
each one", then "do all of the different UI themes at once, because each theme will need my review, and you may as well just
make an initial set for each theme that I can review later".

Each theme is a theme INI (themes/<id>.ini: palette + one part name per art kind) and its own part in every kind of the art
library (assets/<folder>/<id>.png, + .ini where the kind needs a cut), so "Theme's own" on Appearance > Art and picking the
theme's name there are the same look. The look of each one - palette, frame, control shapes - was read out of the overhaul's
own files (the evidence is in 4. plans\\amf-ui-themes\\<id>\\DESIGN.md and look.json, outside this repo).

ART: original, drawn here from shapes (no file of any overhaul is copied in); a theme takes its colours and its shape language
from the overhaul it matches. The geometry is the art library's: frames 78 x 78 with a 26 px corner (the knotwork's), controls
at the sizes tools/make-control-art.py writes, every part white where the colour roles tint it.

Run from the repo root:  python tools/make-overhaul-themes.py      (writes only the parts and INIs of the themes below)
"""
import importlib.util
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUG = os.path.join(ROOT, "dist", "SKSE", "Plugins", "ApocryphaMenuFramework")
ASSETS = os.path.join(PLUG, "assets")
THEME_DIR = os.path.join(PLUG, "themes")

# the control-art helpers (S, outline, to_rgba, contain, save, ini, grow, shrink, star ...) - same sizes, same supersampling
_spec = importlib.util.spec_from_file_location("control_art", os.path.join(ROOT, "tools", "make-control-art.py"))
ca = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ca)
K, S, IS = ca.K, ca.S, ca.IS
WHITE = (255, 255, 255, 255)
DARK = (14, 12, 10, 235)


def rgba(hex_rgb, a=255):
    h = hex_rgb.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), int(h[6:8], 16) if len(h) == 8 else a)


# ---------------------------------------------------------------------------------------------------------------------
# THE THEMES. Each names its shape language by option; the drawing below reads the options.
#   shape    control silhouette: square | rounded | round (pill-like) | cut (all corners) | notch (one corner cut)
#   frame    the window frame: list of lines (inset, width, colour, alpha) + corner style + ornament
#   tick     tick-box mark: check | cross | diamond | dot | square
#   arrow    chevron | triangle | thin
#   section  line under a heading: fade | rule | diamond | double | dots
#   knob     switch knob: circle | diamond | square | gem
#   cursor   (fill, outline, style)
#   bg       None, or a background tile maker name

THEMES = {}   # filled by register() calls at the end of this file


def register(tid, **kw):
    THEMES[tid] = kw


# ---------------------------------------------------------------------------------------------------------------------
# silhouettes

def shape_mask(t, w, h, r, top_only=False):
    m = ca.mask_canvas(w, h)
    d = ImageDraw.Draw(m)
    W, H, R = S(w), S(h), S(r)
    bot = H * 3 if top_only else H
    shape = t["shape"]
    if shape == "square":
        d.rectangle([0, 0, W - 1, bot], fill=255)
    elif shape == "rounded":
        d.rounded_rectangle([0, 0, W - 1, bot], radius=int(R * t.get("round", 0.5)), fill=255)
    elif shape == "round":
        d.rounded_rectangle([0, 0, W - 1, bot], radius=int(min(R * 1.2, H / 2 if not top_only else R)), fill=255)
    elif shape == "cut":
        c = R * t.get("cut", 0.55)
        b2 = 0 if top_only else c
        d.polygon([(c, 0), (W - c, 0), (W, c), (W, bot - b2), (W - b2, bot), (b2, bot), (0, bot - b2), (0, c)], fill=255)
    elif shape == "notch":
        c = R * 0.7
        d.polygon([(0, 0), (W - c, 0), (W, c), (W, bot), (0, bot)], fill=255)
    elif shape == "hex":      # pointed left and right ends, like a banner plate (a tab keeps only its top corners cut)
        if top_only:
            c = R * 0.6
            d.polygon([(c, 0), (W - c, 0), (W, c), (W, bot), (0, bot), (0, c)], fill=255)
        else:
            c = min(R * 0.9, H / 2)
            d.polygon([(c, 0), (W - c, 0), (W, H / 2), (W - c, H), (c, H), (0, H / 2)], fill=255)
    return m.crop((0, 0, W, H))


def shape_only(t, w, h, r, top_only=False):
    return ca.to_rgba(shape_mask(t, w, h, r, top_only), w, h)


def plate(t, w, h, r):
    """(main, edge) for a grab: the fill contained by its outline, plus the theme's grab mark."""
    m = shape_mask(t, w, h, r)
    edge = ca.outline(m, t.get("grab_line", 2.4))
    d = ImageDraw.Draw(edge)
    mark = t.get("grab_mark")
    W, H = S(w), S(h)
    if mark == "grip":        # three short grip lines across the middle
        for i in (-1, 0, 1):
            y = H / 2 + i * S(5)
            d.line([(W * 0.3, y), (W * 0.7, y)], fill=200, width=int(S(1.6)))
    elif mark == "dot":
        rr = S(3)
        d.ellipse([W / 2 - rr, H / 2 - rr, W / 2 + rr, H / 2 + rr], fill=220)
    elif mark == "diamond":
        rr = S(4.5)
        d.polygon([(W / 2, H / 2 - rr), (W / 2 + rr, H / 2), (W / 2, H / 2 + rr), (W / 2 - rr, H / 2)], fill=220)
    edge = ImageChops.multiply(edge, m)
    main, edge_img = ca.to_rgba(m, w, h), ca.to_rgba(edge, w, h)
    return ca.contain(main, edge_img), edge_img


# ---------------------------------------------------------------------------------------------------------------------
# the frame: 78 x 78, 26 px corner, drawn 8x and scaled down (tools/make-theme-art.py's geometry)

FS, FSIZE, FCORNER = 8, 78, 26


def frame(t):
    if "frame_maker" in t:
        return t["frame_maker"]()
    f = t["frame"]
    img = Image.new("RGBA", (FSIZE * FS, FSIZE * FS), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    P = lambda v: v * FS
    corner = f.get("corner", "square")
    for inset, width, col, alpha in f["lines"]:
        a, b = inset, FSIZE - inset
        colour = rgba(col, alpha)
        wpx = max(1, round(P(width)))
        if corner == "square":
            d.rectangle([P(a), P(a), P(b), P(b)], outline=colour, width=wpx)
        elif corner == "rounded":
            d.rounded_rectangle([P(a), P(a), P(b), P(b)], radius=P(f.get("radius", 8)), outline=colour, width=wpx)
        elif corner == "cut":
            c = f.get("cut", 8)
            pts = [(a + c, a), (b - c, a), (b, a + c), (b, b - c), (b - c, b), (a + c, b), (a, b - c), (a, a + c), (a + c, a)]
            d.line([(P(x), P(y)) for x, y in pts], fill=colour, width=wpx, joint="curve")
    orn = f.get("ornament")
    oc = rgba(f.get("ornament_colour", f["lines"][0][2]), f.get("ornament_alpha", 245))
    a, b = f["lines"][0][0], FSIZE - f["lines"][0][0]
    corners = ((1, 1, a, a), (-1, 1, b, a), (1, -1, a, b), (-1, -1, b, b))
    if orn == "brackets":      # heavier L-brackets over each corner
        arm, th = f.get("arm", 12), f.get("thick", 2.4)
        for sx, sy, cx, cy in corners:
            d.line([(P(cx), P(cy + sy * arm)), (P(cx), P(cy)), (P(cx + sx * arm), P(cy))], fill=oc, width=round(P(th)), joint="curve")
    elif orn == "diamond":     # a small diamond on each corner
        r = f.get("orn_r", 4.0)
        for sx, sy, cx, cy in corners:
            x, y = cx + sx * f.get("orn_in", 0), cy + sy * f.get("orn_in", 0)
            d.polygon([(P(x), P(y - r)), (P(x + r), P(y)), (P(x), P(y + r)), (P(x - r), P(y))], fill=oc)
    elif orn == "dot":
        r = f.get("orn_r", 2.6)
        for sx, sy, cx, cy in corners:
            x, y = cx + sx * f.get("orn_in", 0), cy + sy * f.get("orn_in", 0)
            d.ellipse([P(x - r), P(y - r), P(x + r), P(y + r)], fill=oc)
    elif orn == "knotdiamond":   # a small interlaced diamond on each cut corner: an outline diamond crossed by a smaller one
        r = f.get("orn_r", 5.0)
        m = a + f.get("cut", 8) / 2.0
        for cx_, cy_ in ((m, m), (FSIZE - m, m), (m, FSIZE - m), (FSIZE - m, FSIZE - m)):
            outer = [(P(cx_), P(cy_ - r)), (P(cx_ + r), P(cy_)), (P(cx_), P(cy_ + r)), (P(cx_ - r), P(cy_))]
            d.polygon(outer, fill=(0, 0, 0, 200), outline=oc, width=round(P(1.3)))
            ri = r * 0.45
            d.line([(P(cx_ - ri), P(cy_ - ri)), (P(cx_ + ri), P(cy_ + ri))], fill=oc, width=round(P(1.1)))
            d.line([(P(cx_ + ri), P(cy_ - ri)), (P(cx_ - ri), P(cy_ + ri))], fill=oc, width=round(P(1.1)))
    elif orn == "hatch":       # a small triangle of diagonal stripes tucked into the top-left and bottom-right inner corners
        inner = f["lines"][-1][0] + 2.5
        size = f.get("hatch", 12)
        for sx, sy, cx, cy in ((1, 1, inner, inner), (-1, -1, FSIZE - inner, FSIZE - inner)):
            tri = Image.new("L", img.size, 0)
            ImageDraw.Draw(tri).polygon([(P(cx), P(cy)), (P(cx + sx * size), P(cy)), (P(cx), P(cy + sy * size))], fill=255)
            stripes = Image.new("L", img.size, 0)
            sd = ImageDraw.Draw(stripes)
            for k in range(-int(size), int(size) * 2, 3):
                sd.line([(P(cx + sx * k), P(cy)), (P(cx), P(cy + sy * k))], fill=255, width=round(P(1.0)))
            layer = Image.new("RGBA", img.size, oc)
            img.paste(layer, (0, 0), ImageChops.multiply(tri, stripes))
    elif orn == "claw":        # a curved talon sweeping out of each corner along the top and bottom edges
        for sx, sy, cx, cy in corners:
            pts = []
            for i in range(25):
                u = i / 24.0
                x = cx + sx * (2 + u * 20)
                y = cy + sy * (2 + 6 * (1 - u) ** 2)   # thick at the corner, thinning as it runs along the edge
                pts.append((P(x), P(y)))
            for i, (x, y) in enumerate(pts[:-1]):
                wv = (1 - i / 24.0) * f.get("thick", 2.6) + 0.5
                d.line([pts[i], pts[i + 1]], fill=oc, width=max(1, round(P(wv))))
            r = 2.2
            d.ellipse([P(cx - r), P(cy - r), P(cx + r), P(cy + r)], fill=oc)
    return img.resize((FSIZE, FSIZE), Image.LANCZOS)


# ---------------------------------------------------------------------------------------------------------------------
# marks

def tick_mark(t, n=48):
    m = ca.mask_canvas(n, n)
    d = ImageDraw.Draw(m)
    N = S(n)
    kind = t["tick"]
    if kind == "check":
        d.line([(N * 0.18, N * 0.52), (N * 0.42, N * 0.76), (N * 0.84, N * 0.24)], fill=255, width=int(N * 0.13), joint="curve")
    elif kind == "cross":
        for a_, b_ in (((N * 0.22, N * 0.22), (N * 0.78, N * 0.78)), ((N * 0.78, N * 0.22), (N * 0.22, N * 0.78))):
            d.line([a_, b_], fill=255, width=int(N * 0.12))
    elif kind == "diamond":
        c = N / 2
        d.polygon([(c, N * 0.16), (N * 0.84, c), (c, N * 0.84), (N * 0.16, c)], fill=255)
    elif kind == "dot":
        d.ellipse([N * 0.26, N * 0.26, N * 0.74, N * 0.74], fill=255)
    elif kind == "square":
        d.rectangle([N * 0.24, N * 0.24, N * 0.76, N * 0.76], fill=255)
    return ca.to_rgba(m, n, n)


def arrow(t, n=64):
    m = ca.mask_canvas(n, n)
    d = ImageDraw.Draw(m)
    N = S(n)
    kind = t["arrow"]
    if kind == "chevron":
        d.line([(N * 0.32, N * 0.16), (N * 0.7, N * 0.5), (N * 0.32, N * 0.84)], fill=255, width=int(N * 0.14), joint="curve")
    elif kind == "thin":
        d.line([(N * 0.36, N * 0.18), (N * 0.68, N * 0.5), (N * 0.36, N * 0.82)], fill=255, width=int(N * 0.08), joint="curve")
    elif kind == "triangle":
        d.polygon([(N * 0.3, N * 0.18), (N * 0.78, N * 0.5), (N * 0.3, N * 0.82)], fill=255)
    return ca.to_rgba(m, n, n)


def section(t, w=1536, h=96, cap=96):
    """The line under a section heading, white (tinted with the line colour) with a dark outline so it reads on any ground."""
    W, H, C = S(w), S(h), S(cap)
    cy = H / 2
    light = Image.new("L", (W, H), 0)
    dark = Image.new("L", (W, H), 0)
    dl, dd = ImageDraw.Draw(light), ImageDraw.Draw(dark)
    o = S(2.0)
    kind = t["section"]
    outline = t.get("section_outline", True)

    def rail(y, thick, x0, x1, shade=255):
        if outline:
            dd.rounded_rectangle([x0 - o, y - thick / 2 - o, x1 + o, y + thick / 2 + o], radius=thick / 2 + o, fill=255)
        dl.rounded_rectangle([x0, y - thick / 2, x1, y + thick / 2], radius=thick / 2, fill=shade)

    def diamond(x, r, inner=None):
        pts = [(x - r, cy), (x, cy - r), (x + r, cy), (x, cy + r)]
        if outline:
            dd.polygon(ca.grow(pts, o), fill=255)
        dl.polygon(pts, fill=255)
        if inner:
            ip = [(x - inner, cy), (x, cy - inner), (x + inner, cy), (x, cy + inner)]
            dl.polygon(ip, fill=0)

    if kind == "fade":        # one thin line, brightest in the middle, fading out at both ends
        steps = 64
        for i in range(steps):
            u0, u1 = i / steps, (i + 1) / steps
            fade = 1.0 - abs((u0 + u1) / 2 - 0.5) * 2
            shade = int(255 * max(0.0, min(1.0, fade * 1.6)))
            dl.rectangle([C * 0.3 + (W - C * 0.6) * u0, cy - S(3), C * 0.3 + (W - C * 0.6) * u1, cy + S(3)], fill=shade)
    elif kind == "rule":      # a plain rule with square ends
        rail(cy, S(6), C * 0.3, W - C * 0.3)
    elif kind == "diamond":   # a rule with a diamond at each end
        rail(cy, S(5), C * 0.6, W - C * 0.6)
        for x in (C * 0.4, W - C * 0.4):
            diamond(x, S(16), inner=S(6))
    elif kind == "double":    # two thin rules
        for dy in (-S(6), S(6)):
            rail(cy + dy, S(3.5), C * 0.3, W - C * 0.3, shade=255 if dy < 0 else 190)
    elif kind == "flourish":  # a rule that thins toward each end, curling into a scroll with a dot at its heart
        steps = 48
        x0, x1 = C * 1.1, W - C * 1.1
        for i in range(steps):
            u0, u1 = i / steps, (i + 1) / steps
            th = S(2.2) + S(3.2) * (1 - abs((u0 + u1) / 2 - 0.5) * 2)
            xa, xb = x0 + (x1 - x0) * u0, x0 + (x1 - x0) * u1
            if outline:
                dd.rectangle([xa, cy - th / 2 - o, xb, cy + th / 2 + o], fill=255)
            dl.rectangle([xa, cy - th / 2, xb, cy + th / 2], fill=255)
        for side in (-1, 1):
            cx = C * 0.62 if side < 0 else W - C * 0.62
            pts = []
            for i in range(60):          # an outward spiral that ends where the rule begins
                a = i / 59.0 * math.pi * 1.6
                rr = S(4) + S(16) * i / 59.0
                pts.append((cx - side * rr * math.cos(a), cy - rr * math.sin(a)))
            pts.append((C * 1.1 if side < 0 else W - C * 1.1, cy))
            if outline:
                dd.line(pts, fill=255, width=int(S(3.4) + o * 2), joint="curve")
            dl.line(pts, fill=255, width=int(S(3.4)), joint="curve")
            r = S(4.5)
            dl.ellipse([cx - r, cy - r, cx + r, cy + r], fill=255)
    elif kind == "rope":      # two strands twisted round each other, with a small diamond at each end
        x0, x1 = C * 0.75, W - C * 0.75
        period = S(26)
        for phase in (0.0, math.pi):
            pts = []
            n = int((x1 - x0) / S(2))
            for i in range(n + 1):
                x = x0 + (x1 - x0) * i / n
                pts.append((x, cy + S(6) * math.sin((x - x0) / period * 2 * math.pi + phase)))
            if outline:
                dd.line(pts, fill=255, width=int(S(3.2) + o * 2), joint="curve")
            dl.line(pts, fill=255 if phase == 0 else 215, width=int(S(3.2)), joint="curve")
        for x in (C * 0.45, W - C * 0.45):
            diamond(x, S(12), inner=S(4))
    elif kind == "dots":      # a rule ending in a dot each side
        rail(cy, S(4.5), C * 0.55, W - C * 0.55)
        for x in (C * 0.35, W - C * 0.35):
            r = S(9)
            if outline:
                dd.ellipse([x - r - o, cy - r - o, x + r + o, cy + r + o], fill=255)
            dl.ellipse([x - r, cy - r, x + r, cy + r], fill=255)
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    img.paste((14, 12, 10, 200), (0, 0), dark)
    img.paste((255, 255, 255, 255), (0, 0), light)
    return img.resize((w, h), Image.LANCZOS)


def knob(t, size=128):
    """The switch knob in the theme's own colours (tinted only by the Frame art colour, white = as drawn), dark-outlined."""
    N = size * 8
    C = N / 2
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    face, shade = rgba(t["knob_colour"]), rgba(t.get("knob_shade", t["knob_colour"]))
    OUT = 3.2 * 16
    kind = t["knob"]
    if kind == "circle":
        r = N * 0.44
        d.ellipse([C - r - OUT, C - r - OUT, C + r + OUT, C + r + OUT], fill=DARK)
        d.ellipse([C - r, C - r, C + r, C + r], fill=face)
        d.chord([C - r, C - r, C + r, C + r], 20, 160, fill=shade)
    elif kind == "diamond":
        r = N * 0.47
        pts = [(C - r, C), (C, C - r), (C + r, C), (C, C + r)]
        d.polygon(ca.grow(pts, OUT), fill=DARK)
        d.polygon(pts, fill=face)
        d.polygon([(C - r * 0.86, C + 1), (C + r * 0.86, C + 1), (C, C + r * 0.86)], fill=shade)
    elif kind == "square":
        m = N * 0.08
        d.rectangle([m - OUT, m - OUT, N - m + OUT, N - m + OUT], fill=DARK)
        d.rectangle([m, m, N - m, N - m], fill=face)
        d.rectangle([m, C, N - m, N - m], fill=shade)
    elif kind == "gem":       # a cut-cornered plate with a bevel
        m, c = N * 0.08, N * 0.22
        pts = [(m + c, m), (N - m - c, m), (N - m, m + c), (N - m, N - m - c), (N - m - c, N - m), (m + c, N - m), (m, N - m - c), (m, m + c)]
        d.polygon(ca.grow(pts, OUT), fill=DARK)
        d.polygon(pts, fill=face)
        d.polygon([(m, C), (N - m, C), (N - m, N - m - c), (N - m - c, N - m), (m + c, N - m), (m, N - m - c)], fill=shade)
    return img.resize((size, size), Image.LANCZOS)


def dilate(mask, r):
    """An L mask grown by r written pixels (dilated at twice the written size, so it stays cheap), soft-edged."""
    w, h = mask.size
    f = S(1) // K // 2                      # supersampled pixels per half written pixel
    small = mask.resize((w // f, h // f), Image.LANCZOS)
    small = small.filter(ImageFilter.MaxFilter(int(round(r * 2)) * 2 + 1))
    return small.resize((w, h), Image.LANCZOS)


def paint(img, mask, col):
    """Lay col over img wherever mask is set."""
    layer = Image.new("RGBA", img.size, col)
    layer.putalpha(ImageChops.multiply(mask, Image.new("L", img.size, col[3])))
    return Image.alpha_composite(img, layer)


def written(pt, n=64):
    """A supersampled canvas point in the written art's pixels (the cursor .ini's hot spot)."""
    return (int(round(pt[0] / S(n) * n * K)), int(round(pt[1] / S(n) * n * K)))


def bold_cursor(t, n=64):
    """A broad arrow with a dark outline and a light outer rim, so it reads on dark panels and light ones alike
    (Dear Diary - Dark, the owner in game 2026-10-12: "I want a more visually distinct cursor")."""
    fill, line, rim = rgba(t["cursor"][0]), rgba(t["cursor"][1]), rgba(t["cursor"][3])
    N = S(n)
    pts = [(N * 0.1, N * 0.08), (N * 0.1, N * 0.84), (N * 0.29, N * 0.67), (N * 0.42, N * 0.94), (N * 0.56, N * 0.88),
           (N * 0.43, N * 0.61), (N * 0.68, N * 0.61)]
    m = Image.new("L", (N, N), 0)
    ImageDraw.Draw(m).polygon(pts, fill=255)
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    img = paint(img, dilate(m, 4.2), rim)
    img = paint(img, dilate(m, 2.6), line)
    img = paint(img, m, fill)
    shade = Image.new("L", (N, N), 0)          # a darker lower-right half of the face, for depth
    ImageDraw.Draw(shade).polygon([(N * 0.1, N * 0.84), (N * 0.29, N * 0.67), (N * 0.42, N * 0.94), (N * 0.56, N * 0.88),
                                   (N * 0.43, N * 0.61), (N * 0.68, N * 0.61), (N * 0.3, N * 0.5)], fill=255)
    img = paint(img, ImageChops.multiply(shade, ImageChops.multiply(m, Image.new("L", (N, N), 70))), rim)
    return img.resize((n * K, n * K), Image.LANCZOS), written(pts[0], n)


def blade_cursor(t, n=64):
    """A sword pointing up and to the left, the point at the hot spot: a silver blade with a bright ridge, a gold guard and
    pommel, a dark grip, all in a dark outline so it reads on any backdrop (Edge, the owner 2026-10-12: "a cursor for Edge
    UI that looks more like a blade, since it's called Edge UI")."""
    blade, line, gold, gold_dk = rgba(t["cursor"][0]), rgba(t["cursor"][1]), rgba(t["cursor"][3]), rgba(t["cursor"][4])
    N = S(n)
    r2 = math.sqrt(0.5)
    T = (N * 0.06, N * 0.06)

    def at(a, b):                     # a along the blade (toward the hilt), b across it, both in canvas fractions
        return (T[0] + (a * r2 + b * r2) * N, T[1] + (a * r2 - b * r2) * N)

    L, w, tip = 0.62, 0.05, 0.16
    blade_pts = [at(0, 0), at(tip, w), at(L, w), at(L, -w), at(tip, -w)]
    guard_pts = [at(L - 0.02, 0.19), at(L + 0.035, 0.19), at(L + 0.035, -0.19), at(L - 0.02, -0.19)]
    grip_pts = [at(L + 0.035, 0.032), at(L + 0.2, 0.032), at(L + 0.2, -0.032), at(L + 0.035, -0.032)]
    pc, pr = L + 0.245, 0.05
    pommel_pts = [at(pc - pr, 0), at(pc, pr), at(pc + pr, 0), at(pc, -pr)]

    def mask(*polys):
        m = Image.new("L", (N, N), 0)
        d = ImageDraw.Draw(m)
        for p in polys:
            d.polygon(p, fill=255)
        return m

    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    img = paint(img, dilate(mask(blade_pts, guard_pts, grip_pts, pommel_pts), 2.4), line)
    img = paint(img, mask(blade_pts), blade)
    img = paint(img, mask([at(0, 0), at(tip, w), at(L, w), at(L, 0), at(tip * 0.5, 0)]), (176, 180, 188, 255))  # far edge, shaded
    ridge = Image.new("L", (N, N), 0)
    ImageDraw.Draw(ridge).line([at(0.05, 0), at(L - 0.01, 0)], fill=255, width=int(S(1.3)))
    img = paint(img, ridge, (255, 255, 255, 255))
    img = paint(img, mask(guard_pts), gold)
    img = paint(img, mask([at(L + 0.008, 0.19), at(L + 0.035, 0.19), at(L + 0.035, -0.19), at(L + 0.008, -0.19)]), gold_dk)
    img = paint(img, mask(grip_pts), (58, 52, 44, 255))
    wraps = Image.new("L", (N, N), 0)
    dw = ImageDraw.Draw(wraps)
    for a in (L + 0.08, L + 0.125, L + 0.17):
        dw.line([at(a, 0.032), at(a + 0.02, -0.032)], fill=255, width=int(S(1.1)))
    img = paint(img, wraps, gold_dk)
    img = paint(img, mask(pommel_pts), gold)
    return img.resize((n * K, n * K), Image.LANCZOS), written(T, n)


def quill_cursor(t, n=64):
    """A white feather quill writing up and to the left, the nib at the hot spot, in a dark ink outline so the white reads
    on pale paper (Dear Diary, the owner 2026-10-12: "I want Dear Diary theme to have a white feather quill for the
    pointer")."""
    vane, line, nib, ink = rgba(t["cursor"][0]), rgba(t["cursor"][1]), rgba(t["cursor"][3]), rgba(t["cursor"][4])
    N = S(n)
    r2 = math.sqrt(0.5)
    T = (N * 0.06, N * 0.06)

    def at(a, b):                     # a along the quill (toward the tail), b across it, in canvas fractions
        return (T[0] + (a * r2 + b * r2) * N, T[1] + (a * r2 - b * r2) * N)

    a0, a1 = 0.27, 1.0
    steps = 40
    side1 = [at(a0 + (a1 - a0) * i / steps, 0.15 * math.sin(math.pi * i / steps) ** 0.75 + 0.012) for i in range(steps + 1)]
    side2 = [at(a0 + (a1 - a0) * i / steps, -0.085 * math.sin(math.pi * i / steps) ** 0.8 - 0.012) for i in range(steps + 1)]
    vane_pts = side1 + side2[::-1]
    nib_pts = [at(0, 0), at(0.15, 0.03), at(0.15, -0.03)]
    barrel_pts = [at(0.14, 0.024), at(0.3, 0.02), at(0.3, -0.02), at(0.14, -0.024)]

    def mask(*polys):
        m = Image.new("L", (N, N), 0)
        d = ImageDraw.Draw(m)
        for p in polys:
            d.polygon(p, fill=255)
        return m

    vm = mask(vane_pts)
    cuts = Image.new("L", (N, N), 0)        # two barb notches in the wide side, one in the narrow side
    cd = ImageDraw.Draw(cuts)
    for a, b, s in ((0.55, 0.16, 1), (0.78, 0.13, 1), (0.66, -0.095, -1)):
        cd.polygon([at(a, b * 1.15), at(a + 0.1, b * 1.15), at(a - 0.02, b * 0.45)], fill=255)
    vm = ImageChops.subtract(vm, cuts)
    whole = ImageChops.lighter(vm, mask(nib_pts, barrel_pts))

    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    img = paint(img, dilate(whole, 2.3), line)
    img = paint(img, vm, vane)
    shade = Image.new("L", (N, N), 0)       # the narrow side a touch greyer, for the fold of the feather
    ImageDraw.Draw(shade).polygon([at(a0, 0)] + side2 + [at(a1, 0)], fill=255)
    img = paint(img, ImageChops.multiply(ImageChops.multiply(shade, vm), Image.new("L", (N, N), 255)), (222, 216, 204, 255))
    barbs = Image.new("L", (N, N), 0)       # fine barb lines angled back from the shaft
    bd = ImageDraw.Draw(barbs)
    for i in range(7):
        a = 0.36 + i * 0.085
        bd.line([at(a, 0), at(a + 0.07, 0.12 * math.sin(math.pi * (a - a0) / (a1 - a0)) ** 0.75)], fill=255, width=int(S(0.7)))
    img = paint(img, ImageChops.multiply(barbs, vm), (196, 188, 174, 255))
    shaft = Image.new("L", (N, N), 0)
    ImageDraw.Draw(shaft).line([at(0.14, 0), at(0.97, 0)], fill=255, width=int(S(1.4)))
    img = paint(img, mask(barrel_pts), (236, 230, 214, 255))
    img = paint(img, shaft, (176, 160, 136, 255))
    img = paint(img, mask(nib_pts), nib)
    slit = Image.new("L", (N, N), 0)
    ImageDraw.Draw(slit).line([at(0.015, 0), at(0.1, 0)], fill=255, width=int(S(0.9)))
    img = paint(img, slit, ink)
    return img.resize((n * K, n * K), Image.LANCZOS), written(T, n)


def cursor(t, n=64):
    fill, line = rgba(t["cursor"][0]), rgba(t["cursor"][1])
    style = t["cursor"][2]
    if style == "quill":
        return quill_cursor(t, n)
    if style == "bold":
        return bold_cursor(t, n)
    if style == "blade":
        return blade_cursor(t, n)
    N = S(n)
    img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if style == "slim":
        pts = [(N * 0.06, N * 0.04), (N * 0.06, N * 0.74), (N * 0.24, N * 0.58), (N * 0.36, N * 0.88), (N * 0.46, N * 0.84),
               (N * 0.34, N * 0.54), (N * 0.58, N * 0.54)]
    else:    # "arrow": a broad classic pointer
        pts = [(N * 0.06, N * 0.04), (N * 0.06, N * 0.8), (N * 0.25, N * 0.63), (N * 0.38, N * 0.93), (N * 0.51, N * 0.87),
               (N * 0.38, N * 0.58), (N * 0.63, N * 0.58)]
    d.polygon(pts, fill=line)
    d.polygon(ca.shrink(pts, S(3.2)), fill=fill)
    return img.resize((n * K, n * K), Image.LANCZOS), (4 * K, 3 * K)


def hover_frame(gold, glow, dark, line=4.5, inset=3.5, bracket=15, gem=4.6):
    """A highlight (hover) frame for a theme whose window frame is too dark to read round a control: a bright line in
    the theme's warm gold over a dark backing, a soft glow outside it, a heavier bracket and a small gem at each corner.
    78 x 78 with a 26 px corner, like the window frames (drawn 8x and scaled down)."""
    F, E = FSIZE * FS, FS
    g, gl, dk = rgba(gold), rgba(glow), rgba(dark)
    a0, a1 = inset * E, (inset + line) * E      # the line's outer and inner edges
    mid = (a0 + a1) / 2

    def ring(o, i):
        m = Image.new("L", (F, F), 0)
        d = ImageDraw.Draw(m)
        d.rectangle([o, o, F - 1 - o, F - 1 - o], fill=255)
        d.rectangle([i, i, F - 1 - i, F - 1 - i], fill=0)
        return m

    img = Image.new("RGBA", (F, F), (0, 0, 0, 0))
    halo = ring(0.5 * E, a1 + 1.5 * E).filter(ImageFilter.GaussianBlur(1.4 * E))
    img = paint(img, ImageChops.multiply(halo, Image.new("L", (F, F), 110)), gl)
    img = paint(img, ring(a0 - 1.1 * E, a1 + 1.1 * E), dk)
    img = paint(img, ring(a0, a1), g)
    # a brighter core down the middle of the line
    img = paint(img, ring(mid - 0.6 * E, mid + 0.6 * E), (255, 230, 168, 255))
    # corner brackets and gems
    br = Image.new("L", (F, F), 0)
    bd = ImageDraw.Draw(br)
    w2 = (line + 1.6) * E
    for cx, sx in ((0, 1), (F - 1, -1)):
        for cy, sy in ((0, 1), (F - 1, -1)):
            x0, y0 = cx + sx * (a0 - 0.8 * E), cy + sy * (a0 - 0.8 * E)
            x1, y1 = x0 + sx * bracket * E, y0 + sy * w2
            bd.rectangle([min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)], fill=255)
            x1, y1 = x0 + sx * w2, y0 + sy * bracket * E
            bd.rectangle([min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)], fill=255)
    img = paint(img, dilate_px(br, 1.0 * E), dk)
    img = paint(img, br, g)
    gems = Image.new("L", (F, F), 0)
    gd = ImageDraw.Draw(gems)
    for cx in (mid, F - 1 - mid):
        for cy in (mid, F - 1 - mid):
            r = gem * E
            gd.polygon([(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)], fill=255)
    img = paint(img, dilate_px(gems, 1.0 * E), dk)
    img = paint(img, gems, (255, 230, 168, 255))
    return img.resize((FSIZE, FSIZE), Image.LANCZOS)


def dilate_px(mask, px):
    """An L mask grown by px of its own pixels (a polygon-sized MaxFilter at a quarter scale)."""
    w, h = mask.size
    small = mask.resize((w // 4, h // 4), Image.LANCZOS)
    small = small.filter(ImageFilter.MaxFilter(max(3, int(round(px / 4)) * 2 + 1)))
    return small.resize((w, h), Image.LANCZOS)


def toggle_track(t):
    """The switch track: the theme's silhouette at 128 x 64, white (the switch's on / off colour tints it)."""
    tt = dict(t)
    tt["shape"] = t.get("toggle_shape", t["shape"])
    return shape_only(tt, 128, 64, 22)


# ---------------------------------------------------------------------------------------------------------------------
# backgrounds

def paper_tile(t, seed=7):
    """A 256 px paper-grain tile: fibres and blotches, mostly translucent, over the theme's own panel colour."""
    import random
    rnd = random.Random(seed)
    light, dark = rgba(t["bg_light"]), rgba(t["bg_dark"])
    img = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    px = img.load()
    for y in range(256):
        for x in range(256):
            v = rnd.random()
            if v < t.get("bg_density", 0.22):
                c = light if rnd.random() < 0.5 else dark
                px[x, y] = (c[0], c[1], c[2], int(18 + rnd.random() * 26))
    img = img.filter(ImageFilter.GaussianBlur(0.6))
    # soft blotches that wrap at the edges, so the tile repeats without seams
    blot = Image.new("L", (256, 256), 0)
    bd = ImageDraw.Draw(blot)
    for _ in range(14):
        cx, cy, r = rnd.random() * 256, rnd.random() * 256, 18 + rnd.random() * 40
        for ox in (-256, 0, 256):
            for oy in (-256, 0, 256):
                bd.ellipse([cx - r + ox, cy - r + oy, cx + r + ox, cy + r + oy], fill=int(10 + rnd.random() * 14))
    blot = blot.filter(ImageFilter.GaussianBlur(14))
    layer = Image.new("RGBA", (256, 256), dark[:3] + (0,))
    layer.putalpha(blot)
    img.alpha_composite(layer)
    return img


BG_MAKERS = {"paper": paper_tile}


# ---------------------------------------------------------------------------------------------------------------------
# THE OVERHAUL'S OWN ART, where its author allows asset use with credit (the theme's "art" entry names the source PNGs,
# exported from the overhaul's SWFs into 4. plans\amf-ui-themes\<id>\art-candidates\; the files used are copied into
# tools\overhaul-art\<id>\ beside this script, so the build does not depend on a folder outside the repo)

ART_SRC = os.path.join(ROOT, "tools", "overhaul-art")


def src(tid, name):
    return Image.open(os.path.join(ART_SRC, tid, name)).convert("RGBA")


def mirror_tile(img, size=256):
    """A seamless tile from a picture: its centre size/2 square mirrored four ways (a tile <= 512 px, as the library wants)."""
    from PIL import ImageOps
    h = size // 2
    cx, cy = img.width // 2, img.height // 2
    q = img.crop((cx - h, cy - h, cx + h, cy + h))
    out = Image.new("RGBA", (size * 2, size * 2), (0, 0, 0, 0))
    out.paste(q, (0, 0)); out.paste(ImageOps.mirror(q), (size, 0))
    out.paste(ImageOps.flip(q), (0, size)); out.paste(ImageOps.flip(ImageOps.mirror(q)), (size, size))
    return out.resize((size, size), Image.LANCZOS) if size * 2 > 512 else out


def frame_from_panel(img, corner_src, band_src, inset=0):
    """A 78 x 78 nine-slice frame cut from a framed panel: its four corners (corner_src px), its edges squeezed between
    them, and everything further in than band_src px from the outer edge made transparent (the panel's own field)."""
    w, h = img.size
    c = corner_src
    out = Image.new("RGBA", (FSIZE, FSIZE), (0, 0, 0, 0))
    scale = FCORNER / c
    band = band_src * scale

    def piece(box, size):
        return img.crop(box).resize(size, Image.LANCZOS)
    i = inset
    out.paste(piece((i, i, i + c, i + c), (FCORNER, FCORNER)), (0, 0))
    out.paste(piece((w - i - c, i, w - i, i + c), (FCORNER, FCORNER)), (FSIZE - FCORNER, 0))
    out.paste(piece((i, h - i - c, i + c, h - i), (FCORNER, FCORNER)), (0, FSIZE - FCORNER))
    out.paste(piece((w - i - c, h - i - c, w - i, h - i), (FCORNER, FCORNER)), (FSIZE - FCORNER, FSIZE - FCORNER))
    mid = FSIZE - 2 * FCORNER
    out.paste(piece((i + c, i, w - i - c, i + c), (mid, FCORNER)), (FCORNER, 0))
    out.paste(piece((i + c, h - i - c, w - i - c, h - i), (mid, FCORNER)), (FCORNER, FSIZE - FCORNER))
    out.paste(piece((i, i + c, i + c, h - i - c), (FCORNER, mid)), (0, FCORNER))
    out.paste(piece((w - i - c, i + c, w - i, h - i - c), (FCORNER, mid)), (FSIZE - FCORNER, FCORNER))
    px = out.load()
    for y in range(FSIZE):
        for x in range(FSIZE):
            dist = min(x, y, FSIZE - 1 - x, FSIZE - 1 - y)
            r_, g_, b_, a_ = px[x, y]
            if dist > band:
                px[x, y] = (r_, g_, b_, 0)
            elif dist > band - 1.5:
                px[x, y] = (r_, g_, b_, int(a_ * max(0.0, (band - dist) / 1.5)))
    return out


def frame_from_corner(corner, edge_box, band_src):
    """A 78 x 78 frame from one top-left corner piece: mirrored into the four corners, its top and left edge strips
    (edge_box = (x0, x1) of a run of plain edge in the corner piece's top band) stretched along the sides."""
    from PIL import ImageOps
    c = corner.width
    scale = FCORNER / c
    tl = corner.resize((FCORNER, FCORNER), Image.LANCZOS)
    out = Image.new("RGBA", (FSIZE, FSIZE), (0, 0, 0, 0))
    out.paste(tl, (0, 0)); out.paste(ImageOps.mirror(tl), (FSIZE - FCORNER, 0))
    out.paste(ImageOps.flip(tl), (0, FSIZE - FCORNER)); out.paste(ImageOps.flip(ImageOps.mirror(tl)), (FSIZE - FCORNER, FSIZE - FCORNER))
    mid = FSIZE - 2 * FCORNER
    x0, x1 = edge_box
    top = corner.crop((x0, 0, x1, c)).resize((mid, FCORNER), Image.LANCZOS)
    out.paste(top, (FCORNER, 0)); out.paste(ImageOps.flip(top), (FCORNER, FSIZE - FCORNER))
    left = top.rotate(90, expand=True).transpose(Image.FLIP_TOP_BOTTOM)
    left = left.resize((FCORNER, mid), Image.LANCZOS)
    out.paste(left, (0, FCORNER)); out.paste(ImageOps.mirror(left), (FSIZE - FCORNER, FCORNER))
    band = band_src * scale
    px = out.load()
    for y in range(FSIZE):
        for x in range(FSIZE):
            if min(x, y, FSIZE - 1 - x, FSIZE - 1 - y) > band:
                px[x, y] = (0, 0, 0, 0)
    return out


def white_mask(img, invert=False):
    """Art in its own colours -> white with the art's darkness (or brightness) as alpha, so the colour roles tint it."""
    gray = img.convert("L")
    lum = ImageChops.invert(gray) if invert else gray
    a = ImageChops.multiply(img.split()[3], lum)
    out = Image.new("RGBA", img.size, (255, 255, 255, 0))
    out.putalpha(a)
    return out


# ---------------------------------------------------------------------------------------------------------------------

def write_theme_ini(tid, t):
    p = t["palette"]
    lines = ["; Apocrypha Menu Framework theme - %s (DRAFT for review)." % t["name"]]
    lines += ["; " + x for x in t["about"]]
    lines += ["[Theme]", "sName=%s" % t["name"]]
    for key in ("sBackground", "sFrame", "sBorder", "sText", "sTextDim", "sAccent", "sTextHeader", "sTextHelp"):
        lines.append("%s=%s" % (key, p[key]))
    lines += ["fBorderThickness=1.0", "bKnotwork=0", "; the theme's own art parts, by name - its namesake in every kind"]
    lines.append("sFrameArt=%s" % tid)
    if t.get("highlight"):
        lines.append("; the hover / highlight frame: its own, brighter than the window frame")
        lines.append("sHighlightArt=%s-hover" % tid)
    if t.get("bg"):
        lines.append("sBackgroundArt=%s" % tid)
    else:
        lines.append("; no background art: the plain panel colour")
    for key in ("sToggleArt", "sKnobArt", "sBoxArt", "sButtonArt", "sTickBoxArt", "sSliderArt", "sScrollbarArt", "sSectionArt",
                "sTabArt", "sArrowArt", "sCursorArt", "sSliderTrackArt", "sScrollTrackArt"):
        lines.append("%s=%s" % (key, tid))
    with open(os.path.join(THEME_DIR, tid + ".ini"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")


def draw(tid, t):
    note = "drawn for AMF's %s theme (tools/make-overhaul-themes.py) - original art from shapes" % t["name"]
    r = t.get("r", 16)

    frame(t).save(os.path.join(ASSETS, "frames", tid + ".png"))
    with open(os.path.join(ASSETS, "frames", tid + ".ini"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write("; Frame drawn for AMF's %s theme.\n[Frame]\nuCorner=26\n" % t["name"] +
                "; whole: a highlighted item gets the complete frame (thin lines with open bands)\nsHighlight=whole\n" +
                "; how far the art reaches in from its outer edge, in texture pixels - a framed pane keeps its scroll bar inside it\n" +
                "uBand=%d\n" % t["frame"].get("band", 6))

    hover = os.path.join(ASSETS, "frames", tid + "-hover")
    if t.get("highlight"):
        t["highlight"]().save(hover + ".png")
        with open(hover + ".ini", "w", encoding="utf-8", newline="\r\n") as f:
            f.write("; Hover / highlight frame drawn for AMF's %s theme (tools/make-overhaul-themes.py) - original art.\n" % t["name"] +
                    "[Frame]\nuCorner=26\nsHighlight=whole\nuBand=9\n")
    else:
        for ext in (".png", ".ini"):
            if os.path.exists(hover + ext):
                os.remove(hover + ext)

    if t.get("bg"):
        maker = t["bg"] if callable(t["bg"]) else (lambda: BG_MAKERS[t["bg"]](t))
        maker().save(os.path.join(ASSETS, "backgrounds", tid + ".png"))

    for folder in ("boxes", "buttons"):
        ca.save(shape_only(t, 64, 64, r), folder, tid)
        ca.ini(folder, tid, ["uCorner=%d" % (16 * K), "uDrawCorner=8"], note)

    ca.save(shape_only(t, 48, 48, r * 0.75), "tickboxes", tid)
    ca.save(tick_mark(t), "tickboxes", tid + "-mark")
    ca.ini("tickboxes", tid, ["uCorner=%d" % (12 * K), "uDrawCorner=6"], note)

    m_, e_ = plate(t, 24, 48, min(8, r))
    ca.save(m_, "sliders", tid); ca.save(e_, "sliders", tid + "-edge")
    ca.ini("sliders", tid, ["uCorner=%d" % (8 * K), "uDrawCorner=4"], note)

    m_, e_ = plate(t, 24, 64, min(10, r))
    ca.save(m_, "scrollbars", tid); ca.save(e_, "scrollbars", tid + "-edge")
    ca.ini("scrollbars", tid, ["uCorner=%d" % (10 * K), "uDrawCorner=5"], note)

    tm, te = plate(t, 24, 64, min(10, r))
    groove = Image.new("RGBA", (24 * K, 64 * K), (0, 0, 0, 0))
    groove.paste(tm.resize((10 * K, 64 * K), Image.LANCZOS), (7 * K, 0))
    groove_edge = Image.new("RGBA", (24 * K, 64 * K), (0, 0, 0, 0))
    groove_edge.paste(te.resize((10 * K, 64 * K), Image.LANCZOS), (7 * K, 0))
    ca.save(groove, "scrolltracks", tid); ca.save(groove_edge, "scrolltracks", tid + "-edge")
    ca.ini("scrolltracks", tid, ["uCorner=%d" % (10 * K), "uDrawCorner=5"], note)

    track_mask = shape_mask(t, 128, 32, min(10, r))
    ca.save(shape_only(t, 128, 32, min(10, r)), "slidertracks", tid)
    ca.save(ca.to_rgba(ca.outline(track_mask, 1.2), 128, 32), "slidertracks", tid + "-edge")
    ca.ini("slidertracks", tid, ["uCorner=%d" % (10 * K), "uDrawCorner=5"], note)

    tab_mask = shape_mask(t, 96, 48, r, top_only=True)
    tab_line = ca.outline(tab_mask, 1.2)
    open_bottom = Image.new("L", tab_mask.size, 255)
    ImageDraw.Draw(open_bottom).rectangle([0, tab_mask.size[1] - S(3), tab_mask.size[0], tab_mask.size[1]], fill=0)
    ca.save(shape_only(t, 96, 48, r, top_only=True), "tabs", tid)
    ca.save(ca.to_rgba(ImageChops.multiply(tab_line, open_bottom), 96, 48), "tabs", tid + "-edge")
    ca.ini("tabs", tid, ["uCorner=%d" % (16 * K), "uDrawCorner=8"], note)

    ca.save(section(t), "sections", tid)
    ca.ini("sections", tid, ["; the end caps: 96 px in the art (as tall as the strip), 10 px on a 1080p screen at text size 1",
                             "uCorner=96", "uDrawCorner=10"], note)

    ca.save(arrow(t), "arrows", tid)
    ca.save(toggle_track(t), "toggles", tid)
    edge = os.path.join(ASSETS, "toggles", tid + "-edge.png")
    if os.path.exists(edge):
        os.remove(edge)
    knob(t).save(os.path.join(ASSETS, "knobs", tid + ".png"))

    img, hot = cursor(t)
    ca.save(img, "cursors", tid)
    ch = t.get("cursor_h", 24)
    ca.ini("cursors", tid, ["; %d px tall on a 1080p screen; the hot spot is the tip, in the art's own (written) pixels" % ch,
                            "uDrawCorner=%d" % ch, "uHotX=%d" % hot[0], "uHotY=%d" % hot[1]], note)
    write_theme_ini(tid, t)
    print("drew", tid)


def main():
    for tid, t in THEMES.items():
        draw(tid, t)


# ---------------------------------------------------------------------------------------------------------------------
# THEME DEFINITIONS (palettes and evidence: 4. plans\amf-ui-themes\<id>\look.json / DESIGN.md)
def frame_corners_only(corner):
    """A 78 x 78 frame that is only its four corner ornaments (no line between them): one top-left piece mirrored."""
    from PIL import ImageOps
    tl = corner.resize((FCORNER, FCORNER), Image.LANCZOS)
    out = Image.new("RGBA", (FSIZE, FSIZE), (0, 0, 0, 0))
    out.paste(tl, (0, 0)); out.paste(ImageOps.mirror(tl), (FSIZE - FCORNER, 0))
    out.paste(ImageOps.flip(tl), (0, FSIZE - FCORNER)); out.paste(ImageOps.flip(ImageOps.mirror(tl)), (FSIZE - FCORNER, FSIZE - FCORNER))
    return out


def frame_from_strip(strip, band):
    """A 78 x 78 frame from a vertical run of an overhaul's border (strip: its outer edge on the LEFT), band px wide on
    screen: the strip down the left, turned for the other three sides, the corners mitred on the diagonal."""
    left = strip.resize((band, FSIZE), Image.LANCZOS)
    out = Image.new("RGBA", (FSIZE, FSIZE), (0, 0, 0, 0))
    sides = {
        "left": (left, (0, 0)),
        "right": (left.transpose(Image.FLIP_LEFT_RIGHT), (FSIZE - band, 0)),
        "top": (left.transpose(Image.TRANSPOSE), (0, 0)),
        "bottom": (left.transpose(Image.TRANSPOSE).transpose(Image.FLIP_TOP_BOTTOM), (0, FSIZE - band)),
    }
    for name, (img, pos) in sides.items():
        layer = Image.new("RGBA", (FSIZE, FSIZE), (0, 0, 0, 0))
        layer.paste(img, pos)
        mask = Image.new("L", (FSIZE, FSIZE), 0)
        md = ImageDraw.Draw(mask)
        F, c = FSIZE, FSIZE / 2
        tri = {"left": [(0, 0), (c, c), (0, F)], "right": [(F, 0), (c, c), (F, F)],
               "top": [(0, 0), (c, c), (F, 0)], "bottom": [(0, F), (c, c), (F, F)]}[name]
        md.polygon(tri, fill=255)
        out = Image.composite(layer, out, mask)
    return out


def tone(img, mul):
    """Multiply a picture's colour channels (matches a tile to the colour the overhaul shows it at in game)."""
    r, g, b, a = img.split()
    r = r.point(lambda v: int(v * mul[0])); g = g.point(lambda v: int(v * mul[1])); b = b.point(lambda v: int(v * mul[2]))
    return Image.merge("RGBA", (r, g, b, a))


# Edge - Edge UI's look (Nexus 130983). LOOK ONLY, OWN ART: asset use is allowed with credit, but the archive mixes the
# author's own art with other authors' (some needing their explicit permission) and does not say which piece is whose.
register("edge", name="Edge",
         about=["A dark, slightly see-through charcoal panel framed by a thin muted-gold double line with a hatched triangle",
                "in two corners, white text, gold headings and highlights, flat square controls and a plain grey rule under",
                "headings - drawn for AMF from shapes. Evidence: 4. plans/amf-ui-themes/edge-ui/DESIGN.md (outside the repo)."],
         palette=dict(sBackground="#1D1D1DF4", sFrame="#B1A06F", sBorder="#BFBFBF", sText="#FFFFFF", sTextDim="#8A887A",
                      sAccent="#F6DB85", sTextHeader="#AAA07A", sTextHelp="#D2D2D2"),
         shape="rounded", round=0.2, r=10,
         frame=dict(lines=[(3, 2.0, "B1A06F", 245), (6, 1.0, "6A644D", 235)], corner="rounded", radius=4, ornament="hatch",
                    hatch=13, ornament_colour="B1A06F", band=8),
         tick="check", arrow="chevron", section="rule", knob="square", knob_colour="#F6DB85", knob_shade="#C9AE5E",
         # the pointer is a sword (the owner, 2026-10-12: "a cursor for Edge UI that looks more like a blade"): silver blade,
         # Edge's warm gold #F6DB85 guard and pommel, dark outline
         cursor=("#DCDDE2", "#121212", "blade", "#F6DB85", "#B39A55"), cursor_h=32, grab_mark=None, section_outline=False)

# Nordic - NORDIC UI's look (Nexus 49881); its author: "Permissions are open to use my art as you wish". ITS OWN ART: the
# Greek-key corner ornament (configpanel.swf shape 130), credited. The controls are drawn here in its shape language.
register("nordic", name="Nordic",
         about=["A translucent black slab with no outline, only a Greek-key ornament in each corner (NORDIC UI's own corner art,",
                "by outobugi, used with credit under its open permissions), pointed banner-plate controls, diamond knobs and a",
                "twisted-rope section line. Evidence: 4. plans/amf-ui-themes/nordic-ui/DESIGN.md (outside the repo)."],
         palette=dict(sBackground="#000000F2", sFrame="#DEDEDE", sBorder="#999999", sText="#FFFFFF", sTextDim="#7A7A7A",
                      sAccent="#E4D2B3", sTextHeader="#DDDDDD", sTextHelp="#999999"),
         shape="hex", r=16, frame=dict(lines=[(3, 1.2, "DEDEDE", 0)], band=8),
         frame_maker=lambda: frame_corners_only(src("nordic", "frame-corner-meander.png")),
         tick="cross", arrow="chevron", section="rope", knob="diamond", knob_colour="#DEDEDE", knob_shade="#B4B4B4",
         cursor=("#F2F2F2", "#1A1A1A", "slim"), grab_mark="grip")

# Dear Diary - Dear Diary's paper look (Nexus 23010); asset use allowed with credit. ITS OWN ART: the oxblood leather
# border of its MCM card (configpanel_BG.swf bitmap 15) as the frame and its paper as the background, credited.
register("dear-diary", name="Dear Diary",
         about=["Paper bound in oxblood leather: Dear Diary's own leather border and paper (by uranreactor, used with credit",
                "under its asset permissions), dark ink text, dark-red accents, a scrolled section line and softly rounded",
                "controls. Evidence: 4. plans/amf-ui-themes/dear-diary/DESIGN.md (outside the repo)."],
         palette=dict(sBackground="#9B9B96", sFrame="#421A1A", sBorder="#313130", sText="#272726", sTextDim="#4E4E4B",
                      sAccent="#640000", sTextHeader="#1A1A1A", sTextHelp="#3E3A36"),
         shape="rounded", round=0.4, r=12, frame=dict(lines=[(3, 1.2, "421A1A", 0)], band=10),
         frame_maker=lambda: frame_from_strip(src("dear-diary", "leather-framed-panel.png").crop((3, 150, 16, 400)), 9),
         bg=lambda: tone(mirror_tile(src("dear-diary", "paper-texture.png"), 256), (0.876, 0.934, 0.943)),
         tick="check", arrow="triangle", section="flourish", knob="circle", knob_colour="#7A2A22", knob_shade="#4E1612",
         # the pointer is a white feather quill, nib at the hot spot (the owner, 2026-10-12), in a dark ink outline with an
         # iron nib and an oxblood ink slit
         cursor=("#FAF8F2", "#2A1612", "quill", "#3A2E28", "#640000"), cursor_h=32, grab_mark="grip", section_outline=False)

# Dear Diary - Dark - Dear Diary Dark Mode's look (Nexus 60837); asset use allowed with credit. ITS OWN ART: the frame's
# filigree corner and the near-black grunge of its MCM panel (configpanel_BG.swf bitmap 15), credited.
register("dear-diary-dark", name="Dear Diary - Dark",
         about=["Near-black grunge with a thin bronze line and a filigree bracket in each corner (Dear Diary Dark Mode's own",
                "panel art, by uranreactor, used with credit under its asset permissions), warm cream text, a double bronze",
                "rail under headings and square bronze-and-red controls.",
                "Evidence: 4. plans/amf-ui-themes/dear-diary-dark/DESIGN.md (outside the repo)."],
         palette=dict(sBackground="#060606", sFrame="#7D5B3D", sBorder="#92755C", sText="#FFCC99", sTextDim="#9A7A5C",
                      sAccent="#B21601", sTextHeader="#FFCC99", sTextHelp="#D29968"),
         shape="square", r=10, frame=dict(lines=[(3, 1.2, "7D5B3D", 0)], band=9),
         frame_maker=lambda: frame_from_panel(src("dear-diary-dark", "mcm-panel-bg-full.png").crop((24, 71, 266, 543)), 30, 8),
         bg=lambda: mirror_tile(src("dear-diary-dark", "mcm-panel-bg-full.png").crop((300, 90, 740, 530)), 256),
         tick="check", arrow="triangle", section="double", knob="square", knob_colour="#8A2A10", knob_shade="#521301",
         # In game (the owner, 2026-10-12): the window frame's dark bronze (#28180A-#7D5B3D) round a hovered control "is not
         # visually distinguishable" on these near-black panels, "I want it to be the golden color", and "a more visually
         # distinct cursor". So: its own hover frame in a bright gold (#F2C25A: the overhaul's glowing arrow glyph #F8C080
         # taken toward gold, core #FFE6A8 near its button text #FFDB9E) and a broad pointer drawn 32 px tall with a dark outline and a gold rim.
         highlight=lambda: hover_frame("#F2C25A", "#E0A848", "#140A04"),
         cursor=("#FFDB9E", "#140A04", "bold", "#F8C080"), cursor_h=32, grab_mark="grip")


if __name__ == "__main__":
    main()
