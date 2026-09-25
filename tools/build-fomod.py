#!/usr/bin/env python3
r"""Assemble the FOMOD installer package for Apocrypha Menu Framework.

WHY THIS SCRIPT EXISTS
    Because the package was assembled BY HAND, and on 2026-09-16 the owner found that the 1.8.7
    upload on Nexus had shipped without a FOMOD at all. Every release from 1.5.7 to 1.8.4 had one;
    1.8.7 was a plain folder. Nothing checked, so nothing complained.

    What that cost a player: the FOMOD's first question is "Which Skyrim do you have?", and it is
    what hands a 1.7.x player the 1.7 build instead of the SE/AE one. A plain package has exactly
    one DLL in it. The 1.8.7 upload carried the SE/AE build, so every 1.7.x user got a binary that
    cannot work for them - and the package also quietly lost NOTICE.md, THIRD_PARTY_NOTICES.md and
    CUSTOM-MENU-ART.md on the way, the first two of which are licence obligations.

    So the packaging is a script now, and the script REFUSES rather than shipping something wrong.

WHAT IT REFUSES TO DO
    * ship if either build line is missing - that is the exact failure above;
    * ship if the two build lines are byte-identical, which means one was copied over the other
      and the "which Skyrim" question would be a lie;
    * ship if any file the installer's ModuleConfig.xml names as a source folder came out empty;
    * ship if a document the licence requires is absent.

Usage:  python tools/build-fomod.py [output folder]
        Default output is "<repo>/../../7. current test builds/Apocrypha Menu Framework <ver> FOMOD".
"""
import os
import re
import shutil
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))

DLL = "!ApocryphaMenuFramework.dll"
PDB = "!ApocryphaMenuFramework.pdb"

# The two build lines, and the folder each becomes in the installer. These names are not free -
# ModuleConfig.xml names them as its source folders, and the check at the bottom proves they match.
LINES = [
    ("build/relwithdebinfo-se-only", "Line-SE-AE16"),
    ("build/relwithdebinfo-17", "Line-17"),
]

LANGUAGES = ["Chinese", "Czech", "French", "German", "Italian", "Japanese",
             "Korean", "Polish", "Russian", "Spanish"]

# Everything a licence or the page promises. Absent means the build stops; these are the files that
# went missing from 1.8.7 without anyone noticing.
COMMON_DOCS = [
    ("NOTICE.md", "NOTICE.md"),
    ("THIRD_PARTY_NOTICES.md", "THIRD_PARTY_NOTICES.md"),
    ("LICENSE", "LICENSE-ApocryphaMenuFramework-GPL-3.0.txt"),
    ("docs/CUSTOM-MENU-ART.md", "CUSTOM-MENU-ART.md"),
]


# Written per build so the version and the source tag are never stale - the copy shipped with 1.8.4
# still said 1.8.4 because it was copied by hand.
LINE17_NOTICE = """Apocrypha Menu Framework {ver} - Skyrim 1.7.x build line

This DLL statically links CommonLibSSE-NG 7.2.0 (https://github.com/alandtse/CommonLibSSE-NG, commit
7a60f4de794095d7b0f8928d1b930a52e9a7da83), GPL-3.0-or-later WITH Modding Exception AND GPL-3.0 Linking Exception
(CommonLibSSE-NG-EXCEPTIONS.md in this folder).

Apocrypha Menu Framework as a whole - both build lines and its source - is Copyright (C) 2026 ApocryphaRealm and licensed
GPL-3.0-or-later (LICENSE-GPL-3.0.txt here; LICENSE-ApocryphaMenuFramework-GPL-3.0.txt and NOTICE.md with the common files).
Corresponding source: https://github.com/ApocryphaRealm/ApocryphaMenuFramework (tag v{ver}). Third-party notices: THIRD_PARTY_NOTICES.md.
"""


def fail(msg):
    raise SystemExit("build-fomod: " + msg)


def version():
    """Read the version from CMakeLists.txt - the location version-gate.ps1 stamps."""
    text = open(os.path.join(REPO, "CMakeLists.txt"), encoding="utf-8").read()
    m = re.search(r"project\s*\([^)]*?VERSION\s+(\d+\.\d+\.\d+)", text, re.S | re.I)
    if not m:
        fail("no project(... VERSION x.y.z) in CMakeLists.txt - the version has moved")
    return m.group(1)


def copy_into(src, dest_dir, dest_name=None):
    if not os.path.isfile(src):
        fail("missing file the package needs: %s" % src)
    os.makedirs(dest_dir, exist_ok=True)
    shutil.copy2(src, os.path.join(dest_dir, dest_name or os.path.basename(src)))


def main(out_root=None):
    ver = version()
    if out_root is None:
        out_root = os.path.normpath(os.path.join(
            REPO, "..", "..", "7. current test builds",
            "Apocrypha Menu Framework %s FOMOD" % ver))

    if os.path.isdir(out_root):
        shutil.rmtree(out_root)

    # ---- the two build lines, checked against each other before anything is copied
    built = {}
    for rel, folder in LINES:
        dll = os.path.join(REPO, rel, DLL)
        if not os.path.isfile(dll):
            fail("build line '%s' has no %s - build it before packaging, or the installer's "
                 "'which Skyrim do you have?' question cannot be honoured" % (folder, DLL))
        built[folder] = open(dll, "rb").read()
    a, b = list(built.values())
    if a == b:
        fail("the two build lines are byte-identical - one has been copied over the other, and "
             "the installer would offer a choice that makes no difference")

    for rel, folder in LINES:
        dest = os.path.join(out_root, folder, "SKSE", "Plugins")
        copy_into(os.path.join(REPO, rel, DLL), dest)
        pdb = os.path.join(REPO, rel, PDB)
        if os.path.isfile(pdb):                      # rule 43 - ship the symbols
            copy_into(pdb, dest)

    # The 1.7 line links CommonLibSSE-NG 7.x, which is GPL-3.0-or-later with a modding exception,
    # so that folder carries its own licence set. These were hand-placed in the package every
    # release until now, which is precisely why they were easy to lose.
    line17 = os.path.join(out_root, "Line-17")
    for name in ("CommonLibSSE-NG-EXCEPTIONS.md", "LICENSE-GPL-3.0.txt"):
        copy_into(os.path.join(REPO, "fomod", "line-17", name), line17)
    with open(os.path.join(line17, "NOTICE.txt"), "w", encoding="utf-8") as f:
        f.write(LINE17_NOTICE.format(ver=ver))

    # ---- Common: what every install gets
    common = os.path.join(out_root, "Common")
    for src, name in COMMON_DOCS:
        copy_into(os.path.join(REPO, src), common, name)
    copy_into(os.path.join(REPO, "dist", "README.txt"), common, "README.txt")

    # The ini has lived in two places across the repo's life. Take whichever exists rather than
    # assuming, but name both if neither does - a silent fallback is how the last package lost
    # files in the first place.
    ini = None
    for cand in (os.path.join(REPO, "dist", "SKSE", "Plugins", "ApocryphaMenuFramework.ini"),
                 os.path.join(REPO, "dist", "ApocryphaMenuFramework.ini")):
        if os.path.isfile(cand):
            ini = cand
            break
    if ini is None:
        fail("no ApocryphaMenuFramework.ini in dist/SKSE/Plugins/ or dist/")
    copy_into(ini, os.path.join(common, "SKSE", "Plugins"))

    # The shipped themes (1.9.8: Vel'dun and Oathvein) - each an INI plus the art it names. A theme
    # whose art did not come along would show up in the picker and draw nothing, so every file an
    # INI points at must be in the package or the build stops.
    themes_src = os.path.join(REPO, "dist", "SKSE", "Plugins", "ApocryphaMenuFramework", "themes")
    themes_dst = os.path.join(common, "SKSE", "Plugins", "ApocryphaMenuFramework", "themes")
    if not os.path.isdir(themes_src) or not any(f.endswith(".ini") for f in os.listdir(themes_src)):
        fail("no theme INIs in dist/SKSE/Plugins/ApocryphaMenuFramework/themes (run tools/make-theme-art.py)")
    shutil.copytree(themes_src, themes_dst, dirs_exist_ok=True)
    for name in os.listdir(themes_dst):
        if not name.endswith(".ini"):
            continue
        for line in open(os.path.join(themes_dst, name), encoding="utf-8"):
            key, _, val = line.strip().partition("=")
            if key in ("sSkinFrame", "sSkinBackground") and val:
                rel = val.replace("SKSE/Plugins/ApocryphaMenuFramework/themes/", "", 1)
                if not os.path.isfile(os.path.join(themes_dst, rel.replace("/", os.sep))):
                    fail("theme %s names %s, which is not in the package" % (name, val))

    # English is not optional - it is the fallback every other language falls back TO.
    trans = os.path.join(REPO, "dist", "Interface", "Translations")
    copy_into(os.path.join(trans, "ApocryphaMenuFramework_english.txt"),
              os.path.join(common, "Interface", "Translations"))

    # ---- Languages: one folder per language, plus All
    for lang in LANGUAGES:
        src = os.path.join(trans, "ApocryphaMenuFramework_%s.txt" % lang.lower())
        copy_into(src, os.path.join(out_root, "Languages", lang, "Interface", "Translations"))
        copy_into(src, os.path.join(out_root, "Languages", "All", "Interface", "Translations"))

    # ---- fomod: the installer itself
    fomod = os.path.join(out_root, "fomod")
    copy_into(os.path.join(REPO, "fomod", "ModuleConfig.xml"), fomod)
    with open(os.path.join(fomod, "info.xml"), "w", encoding="utf-8") as f:
        f.write('<?xml version="1.0" encoding="UTF-8"?>\n<fomod>\n'
                '\t<Name>Apocrypha Menu Framework</Name>\n'
                '\t<Author>ApocryphaRealm</Author>\n'
                '\t<Version>%s</Version>\n'
                '\t<Website>https://github.com/ApocryphaRealm/ApocryphaMenuFramework</Website>\n'
                '\t<Description>Original ImGui menu framework for SKSE mods; a drop-in stand-in '
                'for SKSE Menu Framework. Pick the build for your game version.</Description>\n'
                '</fomod>\n' % ver)

    # ---- and the check that matters: every folder the installer names must exist and hold files
    root = ET.parse(os.path.join(fomod, "ModuleConfig.xml")).getroot()
    sources = sorted({e.get("source") for e in root.iter("folder") if e.get("source")})
    for s in sources:
        p = os.path.join(out_root, s.replace("/", os.sep))
        if not os.path.isdir(p) or not any(
                os.path.isfile(os.path.join(dp, f)) for dp, _, fs in os.walk(p) for f in fs):
            fail("ModuleConfig.xml installs from '%s', but the package has nothing there" % s)

    total = sum(len(fs) for _, _, fs in os.walk(out_root))
    print("built %s" % out_root)
    print("  version ............... %s" % ver)
    print("  build lines ........... %s" % ", ".join(f for _, f in LINES))
    print("  languages ............. %d, plus All" % len(LANGUAGES))
    print("  installer sources ..... %d, every one of them present" % len(sources))
    print("  files ................. %d" % total)
    return out_root


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else None)
