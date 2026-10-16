#!/usr/bin/env python3
r"""Off-game check that a player's User.ini survives AMF's load-and-save round trip (2.2.1).

WHY: the 2.2.1 test build (branch next, before main's 2.2.0 commits were merged in) still read and wrote 2.1.9's
[IniMenus] sOff, while 2.2.0 had moved to sOn. Settings::Save() rewrites User.ini from what Load() read, so the first
settings save on that build silently dropped the player's [IniMenus] sOn - the owner's backpocketforcontroller.ini page
choice was lost (in-game test, 2026-10-16). Nothing checked that the keys Load() reads and the keys Save() writes are the
same keys, held in the same field, nor that a key the last release wrote is still both.

WHAT IT CHECKS, on source/Settings.cpp (the code that runs, read as text - it is not compiled here):
  * the round trip of a User.ini: every key of the sample below (and of any User.ini given on the command line) in a
    section Settings.cpp owns is READ by Load() into a field and WRITTEN by ScalarBlock() (what Save() writes) from the
    SAME field - so its value comes back out unchanged (Save leaves a key out only when it equals the shipped default,
    and the sample's values differ from it);
  * every key the last release (git tag v2.2.0) wrote is still read and written from one field (a retired key must be
    listed in RETIRED below with the reason);
  * every key read is written and every key written is read.
Sections written whole by other code (Bindings.cpp, Personalization.cpp: [Bindings], [MenuAlias], [MenuOrder] ...) are
listed as such and not judged here.

Usage:  py -3 tools/user_ini_roundtrip_check.py [User.ini ...] [--source <Settings.cpp>] [--release <tag>]
Prints one line per check and "user_ini_roundtrip_check: N of N passed"; exits 1 on any failure.
"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))

# The owner's case from the 2.2.1 test, plus a few ordinary keys of other sections.
SAMPLE = """; ApocryphaRealm Menu Framework - YOUR settings
[Input]
uToggleKey=59
[Menu]
bPauseGame=0
[IniMenus]
bEnabled=1
sOn=skse/plugins/backpocketforcontroller.ini,data/foo.ini
[MCM]
bImportNewMenus=0
"""

# keys written through a local variable built from a field (the field is what Load() fills back)
WRITTEN_VIA = {
    "ListFilter.sWords": "listFilters",   # a_v.listFilters joined as "+word;-word;0word", parsed back the same way by Load()
}

# key -> why it may be missing from this tree although the release wrote it
RETIRED = {}

passed = failed = 0


def check(ok, what):
    global passed, failed
    if ok:
        passed += 1
    else:
        failed += 1
    print(("PASS " if ok else "FAIL ") + what)


def scalar_block(src):
    start = src.find("std::string ScalarBlock(")
    if start < 0:
        raise SystemExit("user_ini_roundtrip_check: no ScalarBlock() in Settings.cpp - the writer has moved")
    end = src.find("return file.str()", start)
    return src[start:end if end > 0 else len(src)]


def written_keys(src):
    """section.key -> the field it is written from (a_v.<field>), from ScalarBlock()."""
    body = scalar_block(src)
    out = {}
    section = None
    for m in re.finditer(r'\[(\w+)\]\\n|"(\w+)=" << ([^;]+?)(?:<< "\\n"|;)', body):
        if m.group(1):
            section = m.group(1)
            continue
        key, expr = m.group(2), m.group(3)
        field = re.search(r"a_v\.([\w.]+)", expr)
        if section:
            k = "%s.%s" % (section, key)
            out[k] = WRITTEN_VIA.get(k, field.group(1) if field else expr.strip())
    return out


def read_keys(src):
    """section.key -> the field Load() stores it in (g_values.<field>)."""
    load = src[src.find("void Load()"):]
    out = {}
    for m in re.finditer(r'Read\w+\(\s*entries,\s*"(\w+\.\w+)",\s*g_values\.([\w.]+)', load):
        out[m.group(1)] = m.group(2)
    for m in re.finditer(r'entries\.find\("(\w+\.\w+)"\);[^}]*?g_values\.([\w.]+?)(?:\.clear\(\)|\s*=)', load, re.S):
        out.setdefault(m.group(1), m.group(2))
    return out


def other_block_sections():
    secs = set()
    for name in ("Bindings.cpp", "Personalization.cpp"):
        p = os.path.join(REPO, "source", name)
        if os.path.isfile(p):
            secs |= set(re.findall(r'\[(\w+)\]', open(p, encoding="utf-8", errors="ignore").read()))
    return secs


def dynamic_prefixes(src):
    """Sections written with a computed name, e.g. "[Colors." << theme << "]" - judged by their own code, not here."""
    return set(re.findall(r'"\[(\w+)\."', src))


def ini_keys(text):
    keys, section = [], None
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith(";") or line.startswith("#"):
            continue
        m = re.match(r"\[([^\]]+)\]$", line)
        if m:
            section = m.group(1)
            continue
        if "=" in line and section:
            keys.append(("%s.%s" % (section, line.split("=", 1)[0].strip()), line.split("=", 1)[1].strip()))
    return keys


def main(argv):
    src_path = os.path.join(REPO, "source", "Settings.cpp")
    release = "v2.2.0"
    inis = []
    i = 0
    while i < len(argv):
        if argv[i] == "--source":
            src_path = argv[i + 1]; i += 2; continue
        if argv[i] == "--release":
            release = argv[i + 1]; i += 2; continue
        inis.append(argv[i]); i += 1
    src = open(src_path, encoding="utf-8", errors="ignore").read()
    W, R = written_keys(src), read_keys(src)
    others = other_block_sections()
    dynamic = dynamic_prefixes(src)
    check(len(W) > 20 and len(R) > 20, "Settings.cpp read: %d keys written by ScalarBlock(), %d read by Load()" % (len(W), len(R)))

    def round_trip(name, text):
        for key, value in ini_keys(text):
            section = key.rsplit(".", 1)[0]
            if section in others or section.split(".", 1)[0] in dynamic and "." in section:
                continue
            ok = key in R and key in W and R[key] == W[key]
            check(ok, "%s: %s=%s survives load and save (read into %s, written from %s)" %
                  (name, key, value, R.get(key, "NOT READ"), W.get(key, "NOT WRITTEN")))

    round_trip("sample User.ini", SAMPLE)
    for p in inis:
        round_trip(os.path.basename(p), open(p, encoding="utf-8-sig", errors="ignore").read())

    try:
        rel_src = subprocess.run(["git", "-C", REPO, "show", "%s:source/Settings.cpp" % release],
                                 capture_output=True, text=True, encoding="utf-8", errors="ignore", check=True).stdout
        rel_w = written_keys(rel_src)
        for key in sorted(rel_w):
            if key in RETIRED:
                check(True, "%s key %s retired: %s" % (release, key, RETIRED[key]))
                continue
            check(key in R and key in W and R[key] == W[key], "%s wrote %s - this tree still reads and writes it" % (release, key))
    except subprocess.CalledProcessError:
        check(False, "release tag %s not found in the repo" % release)

    check(sorted(set(R) - set(W)) == [], "every key read is written (read only: %s)" % sorted(set(R) - set(W)))
    check(sorted(set(W) - set(R)) == [], "every key written is read (written only: %s)" % sorted(set(W) - set(R)))
    print("user_ini_roundtrip_check: %d of %d passed" % (passed, passed + failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
