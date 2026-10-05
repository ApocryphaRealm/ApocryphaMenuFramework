"""Dry run of the MCM auto-sort on a list of names, with the same steps the C++ takes (McmLoader.cpp CategoryOf):
judge each name whole AND with camelCase split ("TrueHUD" -> "True HUD"), drop the word MCM and the " (MCM)" suffix, score every rule, highest group wins.
    python tools/try_mcm_categories.py "TrueHUD" "moreHUD" ...
With no arguments it runs a built-in list of Njordlinger's MCM names."""
import os
import re
import sys

RULES = []
for line in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "source", "McmCategoryRules.inc"), encoding="utf-8"):
    m = re.match(r'\{ R"\((.*)\)", "([A-Za-z]+)", ([0-9.]+)f \}', line)
    if m:
        RULES.append((re.compile(m.group(1), re.I), m.group(2), float(m.group(3))))


def prepare(name, split):
    name = re.sub(r"\s*\(MCM\)\s*$", "", name)
    if split:
        name = re.sub(r"([a-z0-9])([A-Z])", r"\1 \2", name)      # TrueHUD -> True HUD, SmoothCam -> Smooth Cam
        name = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1 \2", name)   # HUDWidgets -> HUD Widgets
    name = re.sub(r"[_\-.]+", " ", name)
    name = re.sub(r"\bmcm\b", " ", name, flags=re.I)
    return re.sub(r"\s+", " ", name).strip()


def category(*names):
    parts = []
    for n in names:
        if n:
            whole, split = prepare(n, False), prepare(n, True)
            parts += [whole] if whole == split else [whole, split]   # "SkyUI" whole, "Sky UI" split
    text = " | ".join(parts)
    score = {}
    for rx, group, weight in RULES:
        if rx.search(text):
            score[group] = score.get(group, 0.0) + weight
    return (max(score.items(), key=lambda kv: kv[1])[0] if score else "Other"), text


NAMES = sys.argv[1:] or ["TrueHUD", "moreHUD", "SmoothCam", "Precision", "Floating Subtitles", "Better Third Person Selection",
    "Campfire", "Lanterns of Skyrim II", "Wyrmstooth", "Pick Up Radius", "Equipment Manager", "QuickLoot IE", "SKY UI",
    "Dismembering Framework", "Accuracy - Localized Damage", "Simple Offence Suppression", "Vivid Routines", "FEC",
    "True Directional Movement", "Faster Loadscreens", "Knockback", "Paragon Perks", "Poisoner's Aid", "Simplest Horses",
    "Timing is Everything", "Valhalla Combat", "Immersive Death Cycle", "Follower Stats", "Atlas Map Markers", "Custom Skills Menu",
    "Jaxonz Renamer", "Hotkey Conflict Manager", "Followers Ride Horses", "Collectibles Helper", "Extended Encounters",
    "Dragonborn ReVoiced", "Wet and Cold", "Immersive Diseases", "C.O.I.N.", "Sanguine Symphony", "FSMP", "CBBE 3BA",
    "T.N.G.", "RaceMenuHH", "Smart Auto-Loot", "No Fast Travel", "VioLens", "Honed Metal", "EVG CLAMBER", "Dynamic Crafting"]
for n in NAMES:
    c, text = category(n)
    print(f"{c:14} <- {n}   [{text}]")
