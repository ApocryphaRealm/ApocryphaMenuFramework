"""Offline check of AMF's own Font Awesome icons (include/AmfIcons.h).

For every icon constant: its UTF-8 bytes decode to the codepoint named in its comment, the codepoint is in
icons::kCodepoints (the list BuildFonts merges), and the shipped fa-solid-900.ttf has a glyph for it. Every
icons::kName used under source/ must be declared. Needs fontTools for the font part (skipped with a note without it).
Run from anywhere: py -3 tools/icon_check.py
"""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
header = (root / "include" / "AmfIcons.h").read_text(encoding="utf-8")
failures = []

consts = {}
for m in re.finditer(r'inline constexpr const char\* (k\w+) = "((?:\\x[0-9A-F]{2})+)";\s*//\s*U\+([0-9A-F]{4})', header):
    name, esc, cp = m.group(1), m.group(2), int(m.group(3), 16)
    decoded = bytes(int(h, 16) for h in re.findall(r"\\x([0-9A-F]{2})", esc)).decode("utf-8")
    if len(decoded) != 1 or ord(decoded) != cp:
        failures.append(f"{name}: bytes decode to {decoded!r}, comment says U+{cp:04X}")
    consts[name] = cp

listed = [int(x, 16) for x in re.findall(r"0x([0-9A-F]{4})", header.split("kCodepoints[]", 1)[1].split("};", 1)[0])]
for name, cp in consts.items():
    if cp not in listed:
        failures.append(f"{name} (U+{cp:04X}) is not in kCodepoints - BuildFonts would not merge it")
for cp in listed:
    if cp not in consts.values():
        failures.append(f"kCodepoints holds U+{cp:04X}, which no constant uses")
if len(set(listed)) != len(listed):
    failures.append("kCodepoints lists a codepoint twice")

used = set()
for src in list((root / "source").glob("*.cpp")) + list((root / "include").glob("*.h")):
    used.update(re.findall(r"icons::(k[A-Z]\w*)", src.read_text(encoding="utf-8", errors="replace")))
used.discard("kCodepoints")
for name in sorted(used - consts.keys()):
    failures.append(f"icons::{name} is used but not declared")

font = root / "dist" / "SKSE" / "Plugins" / "ApocryphaMenuFramework" / "icons" / "fa-solid-900.ttf"
try:
    from fontTools.ttLib import TTFont
    cmap = TTFont(str(font)).getBestCmap()
    for name, cp in consts.items():
        if cp not in cmap:
            failures.append(f"{name} (U+{cp:04X}) has no glyph in {font.name}")
except ImportError:
    print("note: fontTools not installed - the glyph check was skipped")

print(f"{len(consts)} icons, {len(listed)} merged codepoints, {len(used)} used in source")
for f in failures:
    print("FAIL", f)
print("PASS" if not failures else f"{len(failures)} failure(s)")
sys.exit(1 if failures else 0)
