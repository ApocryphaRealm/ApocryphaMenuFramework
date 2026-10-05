"""Generate source/McmCategoryRules.inc - AMF's MCM auto-sort rules - from MO2 Modlist Manager's name table.

The owner, 2026-10-05: "an auto sort function which sorted the imported menus into categories, sort of like our mod
manager plugin but built into AMF. And it can just sort by name, it doesn't have to be perfect." So the rules are not
invented here: they are MO2 Modlist Manager's TEXT_SIGNALS (our own plugin, GPL-3.0 like AMF; repo
6. current wip mods\\MO2ModlistManager, MO2ModlistManager.py) - (pattern, leaf, weight) - read straight from its source
and written out as C++. Its fine-grained leaves are folded into the few separators an MCM list wants (LEAF_TO_GROUP).

std::regex (ECMAScript) has no lookbehind, so (?<! ...) guards are dropped: a pattern may then match a little more
widely than in MO2, which is fine for "it doesn't have to be perfect". Re-run this after Modlist Manager's table changes:
    python tools/gen_mcm_categories.py
"""
import os
import re
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
MO2 = os.path.join(os.path.dirname(REPO), "MO2ModlistManager", "MO2ModlistManager.py")
OUT = os.path.join(REPO, "source", "McmCategoryRules.inc")

src = open(MO2, encoding="utf-8").read()
start = src.index("TEXT_SIGNALS = [(re.compile(p, re.I), c, w) for p, c, w in (")
end = src.index("\n)]\n", start)
body = src[src.index("(", src.index(" in (", start) + 3): end + 2]  # the tuple of (pattern, leaf, weight)
consts = {"DEFINITIVE": 3.6, "NAMED_ROW": 6.0, "NAME_DEF": 3.61, "SHAPE_CAT": "Shape"}
rows = eval(body, {}, consts)

# Modlist Manager's leaves folded into MCM separators; (prefix match on the leaf, group key). First match wins.
LEAF_TO_GROUP = [
    ("User Interface", "Interface"), ("UI Overhaul", "Interface"), ("Icon Overhauls", "Interface"), ("Maps", "Interface"),
    ("Dialogue", "Interface"),
    ("Improved Controls", "Controls"),
    ("Camera", "Camera"),
    ("Gameplay - Combat", "Combat"), ("Animation - Combat", "Combat"),
    ("Animation", "Animation"),
    ("Magic", "MagicSkills"), ("Class, Perks", "MagicSkills"), ("Shouts", "MagicSkills"),
    ("Body", "Characters"), ("Face", "Characters"), ("Hair", "Characters"), ("Races", "Characters"), ("Physics", "Characters"),
    ("Player", "Characters"), ("Shape", "Characters"), ("Equipment Positioning", "Characters"),
    ("NPC", "NpcsCreatures"), ("Creatures", "NpcsCreatures"),
    ("Audio", "Audio"),
    ("Quests", "QuestsPlaces"), ("Player homes", "QuestsPlaces"), ("Dungeons", "QuestsPlaces"), ("Locations - New", "QuestsPlaces"),
    ("Guilds", "QuestsPlaces"),
    ("Environment", "WorldVisuals"), ("Lighting", "WorldVisuals"), ("Visual Effects", "WorldVisuals"), ("Presets", "WorldVisuals"),
    ("Mesh", "WorldVisuals"), ("Models and Textures", "WorldVisuals"), ("PBR", "WorldVisuals"), ("Collision", "WorldVisuals"),
    ("Buildings", "WorldVisuals"), ("Location Overhauls", "WorldVisuals"), ("Cubemaps", "WorldVisuals"),
    ("Items and Objects", "WorldVisuals"),
    ("Gameplay", "Gameplay"), ("Immersion", "Gameplay"), ("Alchemy", "Gameplay"), ("Crafting", "Gameplay"),
    ("Enchanting", "Gameplay"), ("Overhauls", "Gameplay"), ("Cheats", "Gameplay"), ("Clothing", "Gameplay"),
    ("New ", "Gameplay"), ("Armour", "Gameplay"), ("Weapons", "Gameplay"),
    ("Frameworks", "Utility"), ("Utilities", "Utility"), ("Bug Fixes", "Utility"), ("Essential Engine Fixes", "Utility"),
    ("Debugging", "Utility"), ("Script Fixes", "Utility"), ("SKSE", "Utility"), ("Mesh and Texture Fixes", "Utility"),
    ("Unofficial", "Utility"), ("Performance", "Utility"), ("Patches", "Utility"), ("Alternate Start", "Utility"),
    ("Save Games", "Utility"),
]


# AMF's OWN rules, after Modlist Manager's (the owner, 2026-10-05: "any mods that land in the other category should be
# categorized by you and teach the categorizer where it's supposed to go based on its name"). Each was a menu the sort
# put under Other in Njordlinger (82 MCM menus, 25 under Other), placed by hand by what the mod is - its install name,
# files and MCM config - and taught here by its name. Weight 8.0, above a Modlist Manager named row (6.0), so a taught
# name wins. Names are matched as the sort prepares them: "(MCM)" and the word MCM dropped, dots, dashes and
# underscores turned into spaces ("C.O.I.N." is read as "C O I N"). Add a row whenever a menu lands under Other.
AMF_RULES = [
    (r"\bC[\s.]?O[\s.]?I[\s.]?N\b|Coins of Interesting Nature", "Gameplay", "C.O.I.N. - coin economy"),
    (r"Tag\s*(&|and)\s*Track", "Interface", "CS' Tag & Track - item tagging and tracking"),
    (r"\bClamber\b", "Animation", "EVG Clamber - climbing"),
    (r"Equipment\s*Manager", "Controls", "Outfit Wheeler - Dynamic Equipment Manager"),
    (r"\bFEC\b|Frozen\s*Electrocuted\s*Combustion", "WorldVisuals", "FEC - death effects"),
    (r"Favou?r\s*Jobs", "QuestsPlaces", "Favor Jobs Overhaul"),
    (r"Honed\s*Metal", "Gameplay", "Honed Metal - smithing services"),
    (r"\bIni\s*Editor\b", "Utility", "Custom INI Editor"),
    (r"Renamer", "Utility", "Jaxonz Renamer"),
    (r"Jewel+e?ry\s*Limiter", "Gameplay", "Jewelry Limiter"),
    (r"Knock\s*back", "Combat", "Knockback"),
    (r"\bRecorder\b", "Utility", "MCM Recorder"),
    (r"\bSWL\b|Wearable\s*Lanterns", "Gameplay", "New SWL Toggle - Simple Wearable Lanterns key"),
    (r"No\s*Fast\s*Travel", "Gameplay", "No Fast Travel"),
    (r"Photo\s*Mode", "Camera", "Photo Mode"),
    (r"Poisoner", "Gameplay", "Poisoner's Aid"),
    (r"\bPrecision\b", "Combat", "Precision - melee hit collision"),
    (r"Pumping\s*Iron", "Characters", "Pumping Iron - muscle growth"),
    (r"Sanguine\s*Symphony|Blood\s*pool", "WorldVisuals", "Sanguine Symphony - blood pools"),
    (r"Offen[cs]e\s*Suppression|Friendly\s*Fire", "Combat", "Simple Offence Suppression - friendly fire"),
    (r"Strange\s*Runes", "MagicSkills", "Strange Runes - spell runes"),
    (r"\bT[\s.]?N[\s.]?G\b|New\s*Gentleman", "Characters", "T.N.G. - The New Gentleman"),
    (r"Timing\s*is\s*Everything", "QuestsPlaces", "Timing is Everything - quest start levels"),
    (r"Wet\s*(and|&)\s*Cold", "WorldVisuals", "Wet and Cold - weather effects"),
    (r"Wet\s*Function", "WorldVisuals", "WetFunction Redux - wet skin"),
    (r"\bASG\b|Grass", "WorldVisuals", "ASG Multithreaded - grass"),
    (r"Dynamic\s*Looting", "Animation", "Dynamic Looting - loot animations"),
    (r"\bI[\s.]?C[\s.]?O[\s.]?W\b", "Gameplay", "I.C.O.W."),
    (r"\bP[\s.]?W[\s.]?E[\s.]?R\b", "Gameplay", "P.W.E.R"),
    (r"Order\s*Squad", "NpcsCreatures", "Swiftly Order Squad - followers"),
    (r"\bUBG\b", "WorldVisuals", "UBG Redone"),
]
AMF_WEIGHT = 8.0


def group_of(leaf):
    for prefix, group in LEAF_TO_GROUP:
        if leaf.startswith(prefix):
            return group
    raise SystemExit(f"no MCM group for Modlist Manager leaf {leaf!r} - add it to LEAF_TO_GROUP")


def strip_lookbehind(p):
    # (?<!...) and (?<=...) groups, which may hold nested parentheses-free alternatives in this table
    out, i = [], 0
    while i < len(p):
        if p.startswith("(?<!", i) or p.startswith("(?<=", i):
            depth, j = 0, i
            while j < len(p):
                if p[j] == "\\":
                    j += 2
                    continue
                if p[j] == "(":
                    depth += 1
                elif p[j] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            i = j + 1
            continue
        out.append(p[i])
        i += 1
    return "".join(out)


def cpp_raw(s):
    assert ')"' not in s
    return 'R"(' + s + ')"'


commit = subprocess.run(["git", "-C", os.path.dirname(MO2), "log", "-1", "--format=%h"], capture_output=True, text=True).stdout.strip()
lines = [
    "// GENERATED by tools/gen_mcm_categories.py - do not edit by hand; re-run the script instead.",
    f"// Source: MO2 Modlist Manager's TEXT_SIGNALS (our own plugin, GPL-3.0), MO2ModlistManager.py @ {commit}.",
    "// Each row: a name pattern (std::regex, ECMAScript, case-insensitive; lookbehinds dropped), the MCM group it votes",
    "// for, and its weight. The highest summed group wins; no vote at all is \"Other\".",
]
for pattern, leaf, weight in rows:
    p = strip_lookbehind(pattern)
    lines.append(f'{{ {cpp_raw(p)}, "{group_of(leaf)}", {float(weight)}f }},  // {leaf}')
lines.append("// AMF's own rules, taught from menus the sort put under Other (tools/gen_mcm_categories.py AMF_RULES).")
for pattern, group, note in AMF_RULES:
    lines.append(f'{{ {cpp_raw(pattern)}, "{group}", {AMF_WEIGHT}f }},  // AMF: {note}')
open(OUT, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
print(f"{len(rows)} Modlist Manager rules + {len(AMF_RULES)} AMF rules written to {OUT}")
