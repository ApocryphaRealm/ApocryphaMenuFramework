"""Draw the control kinds' parts for AMF's art library (2.1.6).

The owner, 2026-10-08: "I want more than just those three kinds. I want like the scroll bar. I want the box shapes because
the boxes are different from the frame. And I want sliders ... the toggles ... the lines that appear in a mod menu as a
horizontal line under a section header ... the on-screen keyboard ... and anything else that you can think of", with five
shapes of each kind, the default one of them.

The default of every control kind is the built-in shape (Dear ImGui's own, no picture). The other four are drawn here, one
per theme's shape language, so a kind's five are all different shapes and a theme's parts look like one family:

    skyrim    the knotwork frame itself, in its own grey strands on black (2.1.6, the owner: "any of its art ... should follow
              its Nordic knotwork design and color"): the frame nine-sliced down to each plate, the corner knot as the tick,
              strands for the lines and arrows. A pick for other themes; the Skyrim theme keeps its own built-in controls.
    norden    rounded corners, bright L-ticks on the corners          (Norden's corner ticks)
    oathvein  square, one corner cut on the slant, a scratch across it (Oathvein's crossed scratches)
    veldun    every corner cut off, an inner second line              (Vel'dun's cut corners)
    oblivion  the Oblivion map-edge frame itself (2.1.6, the owner chose it over the scooped-corner shapes): compass-rose
              corners, the gold rope repeated along each side, the stitch round the field; its slider and scroll grabs are
              "a single line of the frame art with its ending circles art at either end" (bThreeSlice)

Kinds and files (assets/<folder>/<theme>.png, at FOUR times their 1080p size - 2.1.6, the owner: "make sure that everything
is nice and sharp looking, as they're all pretty tiny": at twice the size a part was still drawn larger than its art at a
big text size on a 4K screen, so it was stretched and soft; now it is always scaled down, and the game mipmaps it):
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
K = 2            # the parts are written K times their design size (the sizes below are the design size, 2x the 1080p one)
SS = 8 * K       # and drawn SS times larger than the design size, so 8x the written size, then scaled down
THEMES = ("skyrim", "norden", "oathvein", "veldun", "oblivion")
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
    if theme == "skyrim":     # a plain plate - the knotwork frame is its edge (knot_plate)
        d.rectangle([0, 0, W - 1, bot], fill=255)
    elif theme == "norden":
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
    """The mask (drawn at the design size w x h, SS times larger) written out at K times the design size."""
    img = Image.new("RGBA", mask.size, (0, 0, 0, 0))
    img.paste(col, (0, 0), mask)
    return img.resize((w * K, h * K), Image.LANCZOS)


def decor(theme, d, w, h, r, top_only=False):
    """Each theme's mark on the edge layer, drawn white into a mask."""
    W, H, R = S(w), S(h), S(r)
    t = S(2.2)
    if theme == "skyrim":           # the knot plate carries its own edge
        pass
    elif theme == "norden":           # bright L-ticks on the corners
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


KNOT_FRAME = os.path.join(ASSETS, "frames", "skyrim-knotwork.png")
KNOT_CORNER = 26        # the knotwork frame's corner, in its own pixels


def knot_edge(w, h, r, top_only=False):
    """The knotwork frame nine-sliced to w x h with r-pixel corners, in its own grey and black; the middle left empty."""
    src = Image.open(KNOT_FRAME).convert("RGBA")
    sw, sh = src.size
    c = KNOT_CORNER
    H = h * 2 if top_only else h          # a tab: the top half of a frame twice as tall
    out = Image.new("RGBA", (w, H), (0, 0, 0, 0))

    def put(box, dst):
        piece = src.crop(box).resize((max(1, int(dst[2] - dst[0])), max(1, int(dst[3] - dst[1]))), Image.LANCZOS)
        out.alpha_composite(piece, (int(dst[0]), int(dst[1])))
    xs = [(0, c, 0, r), (c, sw - c, r, w - r), (sw - c, sw, w - r, w)]
    ys = [(0, c, 0, r), (c, sh - c, r, H - r), (sh - c, sh, H - r, H)]
    for i, (sx0, sx1, dx0, dx1) in enumerate(xs):
        for j, (sy0, sy1, dy0, dy1) in enumerate(ys):
            if i == 1 and j == 1:
                continue                   # no fill in the middle - the plate's own colour shows there
            put((sx0, sy0, sx1, sy1), (dx0, dy0, dx1, dy1))
    return out.crop((0, 0, w, h))


def contain(main, edge, open_bottom=False):
    """2.1.6 (the owner: "The frame should go around the color, not on top of it" - the colours "leak out of the frame"):
    the fill is cut to what lies INSIDE its outline - the area reachable from the middle without crossing the outline -
    and tucked a pixel under the outline's inner side, so no colour shows outside the frame or between its strands."""
    w, h = main.size
    grow_by = 2 * K + 1   # a design pixel, at the written size
    wall = edge.split()[3].point(lambda v: 255 if v > 40 else 0).filter(ImageFilter.MaxFilter(grow_by))   # gaps in a line closed
    if open_bottom:   # a tab is open along the bottom: close it so the fill reaches the bottom edge
        ImageDraw.Draw(wall).line([(0, h - 1), (w, h - 1)], fill=0)
    flood = wall.copy()
    ImageDraw.floodfill(flood, (w // 2, h // 2), 128, thresh=0)
    inside = flood.point(lambda v: 255 if v == 128 else 0).filter(ImageFilter.MaxFilter(grow_by))
    r, g, bl, a = main.split()
    return Image.merge("RGBA", (r, g, bl, ImageChops.multiply(a, inside)))


MAP_FRAME = os.path.join(ASSETS, "frames", "oblivion-map-edge.png")
MAP_CORNER = 52         # the map-edge frame's corner, in its own pixels


def _tile(piece, w, h, horizontal):
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    if horizontal:
        p = piece.resize((max(1, int(round(piece.width * h / piece.height))), h), Image.LANCZOS)
        for x in range(0, w, p.width):
            out.alpha_composite(p.crop((0, 0, min(p.width, w - x), h)), (x, 0))
    else:
        p = piece.resize((w, max(1, int(round(piece.height * w / piece.width)))), Image.LANCZOS)
        for y in range(0, h, p.height):
            out.alpha_composite(p.crop((0, 0, w, min(p.height, h - y))), (0, y))
    return out


def map_edge(w, h, c, top_only=False):
    """The Oblivion map-edge frame at w x h with c-pixel corners: compass-rose corners, the rope repeated along the sides,
    the middle left empty - in its own colours."""
    src = Image.open(MAP_FRAME).convert("RGBA")
    sw, sh = src.size
    fc = MAP_CORNER
    H = h * 2 if top_only else h
    out = Image.new("RGBA", (w, H), (0, 0, 0, 0))
    out.alpha_composite(_tile(src.crop((fc, 0, sw - fc, fc)), w - 2 * c, c, True), (c, 0))
    out.alpha_composite(_tile(src.crop((fc, sh - fc, sw - fc, sh)), w - 2 * c, c, True), (c, H - c))
    out.alpha_composite(_tile(src.crop((0, fc, fc, sh - fc)), c, H - 2 * c, False), (0, c))
    out.alpha_composite(_tile(src.crop((sw - fc, fc, sw, sh - fc)), c, H - 2 * c, False), (w - c, c))
    for box, at in (((0, 0, fc, fc), (0, 0)), ((sw - fc, 0, sw, fc), (w - c, 0)), ((0, sh - fc, fc, sh), (0, H - c)),
                    ((sw - fc, sh - fc, sw, sh), (w - c, H - c))):
        out.alpha_composite(src.crop(box).resize((c, c), Image.LANCZOS), at)
    return out.crop((0, 0, w, h))


def map_medallion(n):
    """The compass rose from the map-edge frame's corner, cut out round, n x n."""
    src = Image.open(MAP_FRAME).convert("RGBA").crop((0, 0, MAP_CORNER, MAP_CORNER))
    cx, cy, r = 23, 23, 17
    big = src.crop((cx - r, cy - r, cx + r + 1, cy + r + 1)).resize((n * 8, n * 8), Image.LANCZOS)
    mask = Image.new("L", big.size, 0)
    ImageDraw.Draw(mask).ellipse([0, 0, big.width - 1, big.height - 1], fill=255)
    out = Image.new("RGBA", big.size, (0, 0, 0, 0))
    out.paste(big, (0, 0), mask)
    return out.resize((n, n), Image.LANCZOS)


def map_grab(w, h):
    """An Oblivion grab: one line of the frame's rope down the middle with a compass rose at each end. (main, edge); the
    main is empty - the grab is the art itself (bThreeSlice: the caps are w tall)."""
    src = Image.open(MAP_FRAME).convert("RGBA")
    rope = src.crop((9, MAP_CORNER, 25, src.height - MAP_CORNER))          # the left side's rope band, edging and all
    edge = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    rw = max(2, int(w * 0.5))
    edge.alpha_composite(_tile(rope, rw, h - w, False), ((w - rw) // 2, w // 2))
    med = map_medallion(w)
    edge.alpha_composite(med, (0, 0))
    edge.alpha_composite(med, (0, h - w))
    return Image.new("RGBA", (w, h), (0, 0, 0, 0)), edge


def plate(theme, w, h, r, top_only=False, double=False):
    """(main, edge) RGBA images for a nine-sliced control - the fill contained by the outline. w, h, r are the design size;
    the images are K times it."""
    if theme == "oblivion":
        e_ = map_edge(w * K, h * K, r * K, top_only)
        m_ = Image.new("RGBA", (w * K, h * K), (255, 255, 255, 255))
        return contain(m_, e_, open_bottom=top_only), e_
    m_, e_ = plate_raw(theme, w, h, r, top_only, double)
    return contain(m_, e_, open_bottom=top_only), e_


def plate_raw(theme, w, h, r, top_only=False, double=False):
    if theme == "skyrim":
        m = shape_mask(theme, w, h, r, top_only)
        return to_rgba(m, w, h), knot_edge(w * K, h * K, r * K, top_only)
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

def knot_medallion(n):
    """The knotwork frame's corner knot mirrored four ways into one square knot, n x n, in its own colours."""
    src = Image.open(KNOT_FRAME).convert("RGBA").crop((0, 0, KNOT_CORNER, KNOT_CORNER))
    w, h = src.size
    from PIL import ImageOps
    knot = Image.new("RGBA", (w * 2, h * 2), (0, 0, 0, 0))
    knot.alpha_composite(src, (0, 0))
    knot.alpha_composite(ImageOps.mirror(src), (w, 0))
    knot.alpha_composite(ImageOps.flip(src), (0, h))
    knot.alpha_composite(ImageOps.flip(ImageOps.mirror(src)), (w, h))
    return knot.resize((n, n), Image.LANCZOS)


def tick_mark(theme, n=48):
    m = mask_canvas(n, n)
    d = ImageDraw.Draw(m)
    N = S(n)
    if theme == "skyrim":      # the knotwork's corner knot, mirrored four ways, in its own colours
        return knot_medallion(n * K)
    elif theme == "norden":      # a check
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
    if theme == "skyrim":      # two strands bent into a chevron, grey on black like the knotwork's
        img = Image.new("RGBA", (N, N), (0, 0, 0, 0))
        di = ImageDraw.Draw(img)
        for off in (0.0, N * 0.16):
            pts = [(N * 0.2 + off, N * 0.14), (N * 0.58 + off, N * 0.5), (N * 0.2 + off, N * 0.86)]
            di.line(pts, fill=(0, 0, 0, 255), width=int(N * 0.14), joint="curve")
            di.line(pts, fill=(153, 153, 153, 255), width=int(N * 0.07), joint="curve")
        return img.resize((n * K, n * K), Image.LANCZOS)
    elif theme == "norden":      # a chevron
        d.line([(N * 0.3, N * 0.14), (N * 0.72, N * 0.5), (N * 0.3, N * 0.86)], fill=255, width=int(N * 0.16), joint="curve")
    elif theme == "oathvein":  # a long thin blade
        d.polygon([(N * 0.08, N * 0.43), (N * 0.42, N * 0.33), (N * 0.96, N * 0.5), (N * 0.42, N * 0.67), (N * 0.08, N * 0.57)], fill=255)
    elif theme == "veldun":    # a diamond with a stem
        d.polygon([(N * 0.42, N * 0.18), (N * 0.94, N * 0.5), (N * 0.42, N * 0.82), (N * 0.6, N * 0.5)], fill=255)
        d.rectangle([N * 0.06, N * 0.44, N * 0.5, N * 0.56], fill=255)
    elif theme == "oblivion":  # an arrowhead with a notched back, like a fletched arrow's
        d.polygon([(N * 0.94, N * 0.5), (N * 0.18, N * 0.1), (N * 0.38, N * 0.5), (N * 0.18, N * 0.9)], fill=255)
    return to_rgba(m, n, n)


def section(theme, w=1536, h=96, cap=96):
    """The line under a section heading: a rail with an end cap at each side (2.1.6, redrawn after the owner's screenshot:
    the first version "looks weird. It's not sharp or detailed"). Drawn at three times the old size so it is always
    scaled DOWN on screen (never up, which blurs), with a dark outline round every shape so it reads on parchment as well
    as on black, and finer detail in the caps. The rail is white (tinted with the line colour); the outline stays dark."""
    W, H, C = S(w), S(h), S(cap)
    cy = H / 2
    light = Image.new("L", (W, H), 0)     # the shapes, tinted
    dark = Image.new("L", (W, H), 0)      # their outline and the rails' shadow, kept dark
    dl, dd = ImageDraw.Draw(light), ImageDraw.Draw(dark)
    o = S(2.6)                            # outline width

    def rail(y, thick, x0, x1, shade=255):
        dd.rounded_rectangle([x0 - o, y - thick / 2 - o, x1 + o, y + thick / 2 + o], radius=thick / 2 + o, fill=255)
        dl.rounded_rectangle([x0, y - thick / 2, x1, y + thick / 2], radius=thick / 2, fill=shade)

    def poly(points, inner_cut=None):
        dd.polygon(grow(points, o), fill=255)
        dl.polygon(points, fill=255)
        if inner_cut:
            dl.polygon(inner_cut, fill=0)
            dd.polygon(inner_cut, fill=255)

    def dot(x, y, r):
        dd.ellipse([x - r - o, y - r - o, x + r + o, y + r + o], fill=255)
        dl.ellipse([x - r, y - r, x + r, y + r], fill=255)

    if theme == "skyrim":      # the knotwork's double strand, grey on black, with the corner knot at each end
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        di = ImageDraw.Draw(img)
        cyp = h / 2
        for dy in (-h * 0.09, h * 0.09):
            di.line([(cap * 0.75, cyp + dy), (w - cap * 0.75, cyp + dy)], fill=(0, 0, 0, 255), width=max(3, int(h * 0.12)))
            di.line([(cap * 0.75, cyp + dy), (w - cap * 0.75, cyp + dy)], fill=(153, 153, 153, 255), width=max(1, int(h * 0.05)))
        knot = knot_medallion(int(cap * 0.62))
        for x in (cap * 0.42, w - cap * 0.42):
            img.alpha_composite(knot, (int(x - knot.width / 2), int(cyp - knot.height / 2)))
        return img
    elif theme == "norden":      # one rail, an upright bar at each end with small serifs pointing in
        rail(cy, S(7), C * 0.45, W - C * 0.45)
        for x, s in ((C * 0.4, 1), (W - C * 0.4, -1)):
            poly([(x - S(3), cy - S(20)), (x + S(3), cy - S(20)), (x + S(3), cy + S(20)), (x - S(3), cy + S(20))])
            poly([(x, cy - S(20)), (x + s * S(11), cy - S(20)), (x + s * S(11), cy - S(15.5)), (x, cy - S(15.5))])
            poly([(x, cy + S(20)), (x + s * S(11), cy + S(20)), (x + s * S(11), cy + S(15.5)), (x, cy + S(15.5))])
    elif theme == "oathvein":  # a blade stroke that thins to points, a ridge down its middle
        poly([(S(6), cy), (C, cy - S(7)), (W - C, cy - S(7)), (W - S(6), cy), (W - C, cy + S(7)), (C, cy + S(7))])
        dd.line([(C * 0.9, cy), (W - C * 0.9, cy)], fill=255, width=int(S(2)))
    elif theme == "veldun":    # two rails with rivets, a diamond at each end cut with a smaller diamond
        for dy in (-S(7), S(7)):
            rail(cy + dy, S(4.5), C * 0.72, W - C * 0.72)
        for x in (C * 0.8, W - C * 0.8):
            dot(x, cy - S(7), S(3.5)); dot(x, cy + S(7), S(3.5))
        for x in (C * 0.42, W - C * 0.42):
            r1, r2 = S(24), S(9)
            poly([(x - r1, cy), (x, cy - r1), (x + r1, cy), (x, cy + r1)],
                 inner_cut=[(x - r2, cy), (x, cy - r2), (x + r2, cy), (x, cy + r2)])
    elif theme == "oblivion":  # a heavy rail and a thin one, a compass star in a ring at each end
        rail(cy - S(4), S(7), C * 0.8, W - C * 0.8)
        rail(cy + S(9), S(3.5), C * 0.8, W - C * 0.8, shade=200)
        for x in (C * 0.42, W - C * 0.42):
            ring_r = S(20)
            dd.ellipse([x - ring_r - o, cy - ring_r - o, x + ring_r + o, cy + ring_r + o], fill=255)
            dl.ellipse([x - ring_r, cy - ring_r, x + ring_r, cy + ring_r], fill=255)
            dl.ellipse([x - ring_r + S(4), cy - ring_r + S(4), x + ring_r - S(4), cy + ring_r - S(4)], fill=0)
            dd.ellipse([x - ring_r + S(4), cy - ring_r + S(4), x + ring_r - S(4), cy + ring_r - S(4)], fill=0)
            poly(star(x, cy, S(27), S(6.5), 4))
            dot(x, cy, S(3.5))
    # compose: dark outline first, the tinted white over it
    img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    img.paste((14, 12, 10, 235), (0, 0), dark)
    img.paste((255, 255, 255, 255), (0, 0), light)
    return img.resize((w, h), Image.LANCZOS)


def grow(points, by):
    """A polygon pushed out from its centre by about a_by (for an outline under it)."""
    cx = sum(p[0] for p in points) / len(points)
    cy = sum(p[1] for p in points) / len(points)
    out = []
    for x, y in points:
        dx, dy = x - cx, y - cy
        n = math.hypot(dx, dy) or 1.0
        out.append((x + dx / n * by, y + dy / n * by))
    return out


def cursor(theme, n=64):
    """Pointers in their own colours, the tip at the hot spot."""
    img = Image.new("RGBA", (S(n), S(n)), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    N = S(n)
    if theme == "skyrim":      # a classic silver arrow with a double outline, like the knotwork's twin strands
        pts = [(N * 0.06, N * 0.04), (N * 0.06, N * 0.8), (N * 0.24, N * 0.64), (N * 0.36, N * 0.94), (N * 0.5, N * 0.88),
               (N * 0.38, N * 0.6), (N * 0.62, N * 0.6)]
        d.polygon(pts, fill=(16, 16, 18, 255))
        d.polygon(shrink(pts, S(3)), fill=(153, 153, 153, 255))
        d.polygon(shrink(pts, S(6.5)), fill=(0, 0, 0, 255))
        d.polygon(shrink(pts, S(8.5)), fill=(153, 153, 153, 255))
        hot = (4, 3)
    elif theme == "norden":      # a sleek silver arrow, black outline
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
    return img.resize((n * K, n * K), Image.LANCZOS), (hot[0] * K, hot[1] * K)


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

def split_toggles():
    """2.1.6 (the owner: the switches' "color leaks out from the green and red coloring"): a switch track is a white fill,
    tinted with the switch's on / off colour, and its outline as <name>-edge.png, drawn in its own colours - so the
    green and red stay inside. The themes' own tracks were one picture; split once, by colour: near-white is fill."""
    folder = os.path.join(ASSETS, "toggles")
    for f in os.listdir(folder):
        if not f.endswith(".png") or f.endswith("-edge.png"):
            continue
        name = f[:-4]
        if os.path.exists(os.path.join(folder, name + "-edge.png")):
            continue
        im = Image.open(os.path.join(folder, f)).convert("RGBA")
        fill = Image.new("RGBA", im.size, (0, 0, 0, 0))
        edge = Image.new("RGBA", im.size, (0, 0, 0, 0))
        src, pf, pe = im.load(), fill.load(), edge.load()
        for y in range(im.size[1]):
            for x in range(im.size[0]):
                r, g, bb, a = src[x, y]
                if a == 0:
                    continue
                whiteness = min(r, g, bb) / 255.0
                if whiteness > 0.92:
                    pf[x, y] = (255, 255, 255, a)
                else:
                    pe[x, y] = (r, g, bb, a)
                    pf[x, y] = (255, 255, 255, int(a * max(0.0, (whiteness - 0.6) / 0.32)))   # the fill runs under the line's inner edge
        fill.save(os.path.join(folder, f))
        edge.save(os.path.join(folder, name + "-edge.png"))
        print("split toggles/" + f)
    for f in os.listdir(folder):   # the fill inside the outline, for every track
        if f.endswith(".png") and not f.endswith("-edge.png"):
            ep = os.path.join(folder, f[:-4] + "-edge.png")
            if os.path.exists(ep):
                contain(Image.open(os.path.join(folder, f)).convert("RGBA"), Image.open(ep).convert("RGBA")).save(os.path.join(folder, f))


def _theme_toggles():
    """Norden's, Oathvein's and Vel'dun's own switch tracks (tools/make-theme-art.py), at K times their size."""
    import importlib.util
    spec = importlib.util.spec_from_file_location("theme_art", os.path.join(ROOT, "tools", "make-theme-art.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return {tid: (lambda f=t["toggle"]: f(K)) for tid, t in mod.THEMES.items() if "toggle" in t}


THEME_TOGGLES = {}


def main():
    THEME_TOGGLES.update(_theme_toggles())
    note = "drawn for AMF's %s look (tools/make-control-art.py) - original art from shapes"
    for t in THEMES:
        n = note % t
        main_, edge = plate(t, 64, 64, 16)
        save(main_, "boxes", t); save(edge, "boxes", t + "-edge")
        ini("boxes", t, ["uCorner=%d" % (16 * K), "uDrawCorner=8"], n)

        main_, edge = plate(t, 64, 64, 16, double=True)
        save(main_, "buttons", t); save(edge, "buttons", t + "-edge")
        ini("buttons", t, ["uCorner=%d" % (16 * K), "uDrawCorner=8"], n)

        main_, edge = plate(t, 48, 48, 12)
        save(main_, "tickboxes", t); save(edge, "tickboxes", t + "-edge"); save(tick_mark(t), "tickboxes", t + "-mark")
        ini("tickboxes", t, ["uCorner=%d" % (12 * K), "uDrawCorner=6"], n)

        main_, edge = plate(t, 24, 48, 8)
        save(main_, "sliders", t); save(edge, "sliders", t + "-edge")
        ini("sliders", t, ["uCorner=%d" % (8 * K), "uDrawCorner=4"], n)

        main_, edge = plate(t, 24, 64, 10)
        save(main_, "scrollbars", t); save(edge, "scrollbars", t + "-edge")
        ini("scrollbars", t, ["uCorner=%d" % (10 * K), "uDrawCorner=5"], n)

        # 2.1.6 (the owner: the track and the grab "separately" - for sliders and for the scroll bar): the scroll bar's
        # track is a kind of its own, the same shape narrowed to a groove down the middle
        tm, te = plate(t, 24, 64, 10)
        groove = Image.new("RGBA", (24 * K, 64 * K), (0, 0, 0, 0))
        groove.paste(tm.resize((10 * K, 64 * K), Image.LANCZOS), (7 * K, 0))
        groove_edge = Image.new("RGBA", (24 * K, 64 * K), (0, 0, 0, 0))
        groove_edge.paste(te.resize((10 * K, 64 * K), Image.LANCZOS), (7 * K, 0))
        save(groove, "scrolltracks", t); save(groove_edge, "scrolltracks", t + "-edge")
        ini("scrolltracks", t, ["uCorner=%d" % (10 * K), "uDrawCorner=5"], n)

        # a slider's track: the family's plate, long and low, behind the grab
        main_, edge = plate(t, 128, 32, 10)
        save(main_, "slidertracks", t); save(edge, "slidertracks", t + "-edge")
        ini("slidertracks", t, ["uCorner=%d" % (10 * K), "uDrawCorner=5"], n)

        main_, edge = plate(t, 96, 48, 16, top_only=True)
        save(main_, "tabs", t); save(edge, "tabs", t + "-edge")
        ini("tabs", t, ["uCorner=%d" % (16 * K), "uDrawCorner=8"], n)

        save(section(t), "sections", t)
        ini("sections", t, ["; the end caps: 96 px in the art (as tall as the strip), 10 px on a 1080p screen at text size 1 - the",
                            "; art is always scaled down, so it stays sharp; the strip is never drawn taller than the row gap allows",
                            "uCorner=96", "uDrawCorner=10"], n)

        save(arrow(t), "arrows", t)

        if t == "oblivion":   # the switch track: the Oblivion plate, like its boxes and tabs (replaces oblivion-scroll)
            m_, e_ = plate(t, 128, 64, 22)      # the map-edge frame round the switch, like the other Oblivion plates
            save(m_, "toggles", t); save(e_, "toggles", t + "-edge")
        if t == "skyrim":   # the switch track: a white plate (the switch colour tints it) and the knotwork as its own layer
            m_, e_ = plate(t, 128, 64, 22)
            save(m_, "toggles", t); save(e_, "toggles", t + "-edge")
        if t in THEME_TOGGLES:   # the three themes' own switch tracks, redrawn at K times their size and split again
            save(THEME_TOGGLES[t](), "toggles", t)
            edge_path = os.path.join(ASSETS, "toggles", t + "-edge.png")
            if os.path.exists(edge_path):
                os.remove(edge_path)
        split_toggles()

        img, hot = cursor(t)
        save(img, "cursors", t)
        ini("cursors", t, ["; 24 px tall on a 1080p screen; the hot spot is the tip, in the art's own (written) pixels",
                           "uDrawCorner=24", "uHotX=%d" % hot[0], "uHotY=%d" % hot[1]], n)
        if t == "oblivion":   # the grabs: a line of rope with a compass rose at each end
            for folder, (gw, gh) in (("sliders", (24, 72)), ("scrollbars", (24, 96))):
                m_, e_ = map_grab(gw * K, gh * K)
                save(m_, folder, t); save(e_, folder, t + "-edge")
                ini(folder, t, ["; a line of the map edge's rope with a compass rose at each end (the owner: \"a single line of the",
                                "; frame art with its ending circles art at either end\"): a cap the art's width tall at each end",
                                "uCorner=%d" % (12 * K), "uDrawCorner=6", "bThreeSlice=1", "bTileEdges=1", "bOwnColours=1"], n)
        if t in ("skyrim", "oblivion"):   # their edges are art in its own colours (and the map edge's rope repeats)
            for folder in ("boxes", "buttons", "tickboxes", "tabs", "slidertracks", "scrolltracks") + (("sliders", "scrollbars") if t == "skyrim" else ()):
                path = os.path.join(ASSETS, folder, t + ".ini")
                text = open(path, encoding="utf-8").read()
                add = ["bOwnColours=1"] + (["bTileEdges=1"] if t == "oblivion" else [])
                for line in add:
                    if line.split("=")[0] not in text:
                        text = text.rstrip("\n") + "\n" + line + "\n"
                open(path, "w", encoding="utf-8", newline="\r\n").write(text)
        print("drew", t)


if __name__ == "__main__":
    main()
