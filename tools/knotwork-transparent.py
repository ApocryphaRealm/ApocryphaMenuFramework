"""Take the black out of the middle of the Skyrim knotwork frame (2.1.6).

The owner, 2026-10-08: the Skyrim frames "have a black interior, which is incorrect" - then, to be exact: "I don't want
the frame transparent. I just don't want it to have a background attached to the middle of the frame. Just like how the
Oblivion frame has a color, the Skyrim frame has one too. It won't look right if it doesn't have any black underneath it.
I just don't want it to be protruding so harshly."

So the frame keeps its black backing under the knots and the black outline round every strand (1 px), and loses only
the solid black that filled the middle and the inner half of each side. The menu's
Background colour then shows inside the frame; the frame itself looks as before.

Writes assets/frames/skyrim-knotwork.png and the built-in copy, include/KnotworkBorder.h. Run from the repo root on the
original frame (git checkout both first if it was run before); then re-run make-control-art.py and make-knob-art.py,
which cut their Skyrim parts from this frame.
"""
import os
from PIL import Image, ImageFilter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PNG = os.path.join(ROOT, "dist", "SKSE", "Plugins", "ApocryphaMenuFramework", "assets", "frames", "skyrim-knotwork.png")
HEADER = os.path.join(ROOT, "include", "KnotworkBorder.h")
DARK = 60          # brightness below which a pixel is backing, not strand


def main():
    im = Image.open(PNG).convert("RGBA")
    px = im.load()
    w, h = im.size
    # a strand is any bright pixel; the black kept is its outline (within 1 px) and the solid backing under the four knots
    strand = Image.new("L", (w, h), 0)
    sp = strand.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a > 0 and (r + g + b) / 3.0 >= DARK:
                sp[x, y] = 255
    near = strand.filter(ImageFilter.MaxFilter(3)).load()
    KNOT = 18                              # the knot block in each corner, in the frame's own pixels
    cleared = 0
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0 or sp[x, y]:
                continue
            in_knot = (x < KNOT or x >= w - KNOT) and (y < KNOT or y >= h - KNOT)
            if near[x, y] or in_knot:
                continue
            px[x, y] = (r, g, b, 0)
            cleared += 1
    im.save(PNG)

    data = im.tobytes()
    text = open(HEADER, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in text else "\n"
    start = text.index("kRGBA[78*78*4] = {") + len("kRGBA[78*78*4] = {")
    end = text.index("};", start)
    rows = ["\t" + ",".join(str(v) for v in data[i:i + 80]) + "," for i in range(0, len(data), 80)]
    text = text[:start] + nl + nl.join(rows) + nl + "\t" + text[end:]
    marker = "// 2.1.6: no black in the middle"
    if marker not in text:
        text = text.replace("// art changes. Do not hand-edit.", "// art changes. Do not hand-edit." + nl + marker +
                            " (tools/knotwork-transparent.py) - the owner: \"I just don't want it to have" + nl +
                            "// a background attached to the middle of the frame\"; the backing under the knots and strands stays.", 1)
    open(HEADER, "w", encoding="utf-8", newline="").write(text)
    print("cleared", cleared, "middle pixels of", w * h)


if __name__ == "__main__":
    main()
