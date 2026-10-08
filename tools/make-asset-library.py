"""Build AMF's art library: dist/SKSE/Plugins/ApocryphaMenuFramework/assets/<kind>/<name>.png (2.1.6).

The owner, 2026-10-08: "add an assets folder with subfolders for the customization section to choose from, and import the
oblivion theme and break down the themes into their art parts and put them into the subfolders for the theme to draw
from". Every theme's art is one PART per kind, filed under the kind and named after the theme it came from:

    assets/frames/<name>.png       nine-slice frame, transparent centre; <name>.ini beside it says how it is cut
    assets/backgrounds/<name>.png  a tile (<= 512 px both ways) or a full picture
    assets/toggles/<name>.png      the on/off switch track plate

Run once per change, from the repo root:  python tools/make-asset-library.py
  1. exports the two frames that lived only inside a DLL as C arrays - Skyrim's knotwork (include/KnotworkBorder.h) and
     the Oblivion map-edge (the Oblivion port's include/MapEdgeBorder.h) - to PNG, with their cut in the .ini;
  2. moves each theme's own folder (themes/<name>/frame.png, background.png, toggle.png) into the kind folders with
     `git mv`, so the art keeps its history;
  3. rewrites the theme INIs to name their parts (sFrameArt / sBackgroundArt / sToggleArt) instead of file paths.
Steps 2-3 are skipped once done (the old folders are gone), so running it again only re-exports step 1.
"""
import os
import re
import subprocess
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLUG = os.path.join(ROOT, "dist", "SKSE", "Plugins", "ApocryphaMenuFramework")
ASSETS = os.path.join(PLUG, "assets")
THEMES = os.path.join(PLUG, "themes")
OR_MAPEDGE = os.path.join(os.path.dirname(ROOT), "ApocryphaMenuFrameworkOR", "include", "MapEdgeBorder.h")


def c_array(header, name="kRGBA"):
    text = open(header, encoding="utf-8").read()
    consts = {k: int(v) for k, v in re.findall(r"constexpr int (\w+) = (\d+);", text)}
    body = text[text.index(name):]
    body = body[body.index("{") + 1: body.index("};")]
    data = bytes(int(x) for x in re.findall(r"\d+", body))
    return consts, data


def write_frame(name, header, ini_lines, note):
    consts, data = c_array(header)
    w, h = consts["kWidth"], consts["kHeight"]
    assert len(data) == w * h * 4, (name, len(data), w * h * 4)
    Image.frombytes("RGBA", (w, h), data).save(os.path.join(ASSETS, "frames", name + ".png"))
    with open(os.path.join(ASSETS, "frames", name + ".ini"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write("; " + note + "\n[Frame]\n" + "\n".join(ini_lines) + "\n")
    print("exported", name, w, "x", h)


def git(*args):
    subprocess.run(["git", *args], cwd=ROOT, check=True)


def main():
    for kind in ("frames", "backgrounds", "toggles"):
        os.makedirs(os.path.join(ASSETS, kind), exist_ok=True)

    # 1. the frames that existed only as C arrays
    write_frame("skyrim-knotwork", os.path.join(ROOT, "include", "KnotworkBorder.h"),
                ["uCorner=26", "; corners: a highlighted item gets the frame's line and its four corner knots, not the solid edge bands",
                 "sHighlight=corners"],
                "Skyrim theme frame: Nordic knotwork corners joined by thin lines (from the MO2 Skyrim stylesheet's panel frame).")
    if os.path.exists(OR_MAPEDGE):
        write_frame("oblivion-map-edge", OR_MAPEDGE,
                    ["uCorner=52", "; drawn at 26 px on a 1080p screen (the texture is twice the design size, so it stays sharp at 4K)",
                     "uDrawCorner=26", "; the stitched edge repeats every 16 px instead of stretching", "bTileEdges=1",
                     "sHighlight=corners"],
                    "Oblivion theme frame: an embroidered map's edge in gold and brown - original art drawn from shapes for AMF.")
    else:
        sys.exit("Oblivion port's MapEdgeBorder.h not found at " + OR_MAPEDGE)

    # 2-3. each theme folder -> parts, theme INI -> part names
    for ini in sorted(f for f in os.listdir(THEMES) if f.endswith(".ini")):
        p = os.path.join(THEMES, ini)
        text = open(p, encoding="utf-8").read()
        m = re.search(r"^sSkinPlates=.*?/themes/([\w-]+)\s*$", text, flags=re.M)
        if not m:
            continue
        part = m.group(1)
        corner = re.search(r"^uSkinFrameCorner=(\d+)", text, flags=re.M)
        src = os.path.join(THEMES, part)
        if os.path.isdir(src):
            for file, kind in (("frame.png", "frames"), ("background.png", "backgrounds"), ("toggle.png", "toggles")):
                if os.path.exists(os.path.join(src, file)):
                    git("mv", os.path.relpath(os.path.join(src, file), ROOT),
                        os.path.relpath(os.path.join(ASSETS, kind, part + ".png"), ROOT))
            with open(os.path.join(ASSETS, "frames", part + ".ini"), "w", encoding="utf-8", newline="\r\n") as f:
                f.write("; Frame drawn for AMF's %s theme.\n[Frame]\nuCorner=%s\n; whole: a highlighted item gets the "
                        "complete frame (thin lines with open bands)\nsHighlight=whole\n" % (part, corner.group(1) if corner else "26"))
        new = re.sub(r"^sSkinFrame=.*\n?", "", text, flags=re.M)
        new = re.sub(r"^uSkinFrameCorner=.*\n?", "", new, flags=re.M)
        new = re.sub(r"^sSkinBackground=.*\n?", "", new, flags=re.M)
        new = re.sub(r"^sSkinPlates=.*$",
                     "; 2.1.6: the theme's art parts, by name, from assets/frames, assets/backgrounds and assets/toggles\n"
                     "sFrameArt=%s\nsBackgroundArt=%s\nsToggleArt=%s" % (part, part, part), new, flags=re.M)
        open(p, "w", encoding="utf-8", newline="").write(new)
        print("theme", ini, "-> parts", part)


if __name__ == "__main__":
    main()
