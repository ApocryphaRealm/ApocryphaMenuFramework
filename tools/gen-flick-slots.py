"""Generate include/FlickSlots.inc - the order and C types of FLICK's 259 API slots, in AMF's own type names.

FLICK host plan section 3 / 13 (4. plans\\amf-flick\\PLAN.md): AMF answers FLICK's C table, so it must declare the slots in
exactly FLICK's order with exactly their parameter types - x64 passes a float in an XMM register and an int in a general
one, so a wrong type is a wrong value with no error. This script reads ONLY the declarations in the public API header's
`struct FUCK_Interface` (interface fact, needed to interoperate) and writes them as an X-macro list:

    FLICK_SLOT(<return type>, <name>, (<parameter types>))

with FLICK's own types renamed to AMF's (FUCK::ITool* -> flick::Tool*, ImFont* -> void* (AMF hands out font tokens),
FLICK's enums -> int, the SimpleIni reference -> void*). No function bodies, widget code or styles are read or copied.
The header is not vendored into AMF: point this at the plans folder's copy when regenerating.

2.1.8: the same generator also writes the slot list of an OLD header (FLICK's table before 2026-06-08 was ordered
differently while still saying version 1 - 4. plans\\amf-flick\\ABI-HISTORY.md). --out names the file and --macro the
X-macro and count names, so the old list sits beside the current one:

Run from the repo root:
    python tools/gen-flick-slots.py "..\\..\\4. plans\\amf-flick\\api\\FUCK_API.h"
    python tools/gen-flick-slots.py "..\\..\\4. plans\\amf-flick\\api-history\\FUCK_API-2026-06-05-53665d6.h" ^
        --out include\\FlickSlotsOld.inc --macro FLICK_OLD_SLOT
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "include", "FlickSlots.inc")

TYPE_MAP = [
    (r"\bFUCK::ITool\s*\*", "flick::Tool*"),
    (r"\bFUCK::IWindow\s*\*", "flick::Window*"),
    (r"\bFUCK::ManagedHotkey\s*\*", "flick::Hotkey*"),
    (r"\bFUCK_SIMPLEINI_TYPE\s*&", "void*"),
    (r"\bCSimpleIniA\s*&", "void*"),
    (r"\bImFont\s*\*", "void*"),
    (r"\bFUCK::(Font|Overlay|Modifier|ItemFlags|InputDevice)\b", "int"),
    (r"\bFUCK::BindResult\b", "int"),
    # an old header (before FLICK put its types in namespace FUCK, 2026-05): the same types without the namespace
    (r"(?<![\w:])ITool\s*\*", "flick::Tool*"),
    (r"(?<![\w:])IWindow\s*\*", "flick::Window*"),
    (r"(?<![\w:])ManagedHotkey\s*\*", "flick::Hotkey*"),
    (r"(?<![\w:])(FUCK_Font|FUCK_Overlay|Modifier|ItemFlags|InputDevice|BindResult)\b", "int"),
    (r"\bImGuiCol\b", "int"),
    (r"\bImGuiStyleVar\b", "int"),
    (r"\bImGuiKey\b", "int"),
]


def parse(text):
    """The slots of a header's struct FUCK_Interface: [(section, return type, name, parameter types)], and its version."""
    m = re.search(r"struct FUCK_Interface\s*\{(.*?)\n\};", text, re.S)
    if not m:
        sys.exit("struct FUCK_Interface not found")
    body = m.group(1)
    version = re.search(r"#define FUCK_API_VERSION (\d+)", text).group(1)
    slots = []
    section = ""
    # a vendored copy may be clang-formatted with one declaration over several lines: join them first
    lines, buf = [], ""
    for raw in body.split("\n"):
        s = raw.strip()
        if not buf and (not s or s.startswith("//")):
            lines.append(s)
            continue
        buf = (buf + " " + s.split("//")[0].strip()).strip()
        if buf.endswith(";"):
            lines.append(re.sub(r"\(\s+", "(", re.sub(r"\s+\)", ")", buf)))
            buf = ""
    for line in lines:
        if line.startswith("//"):
            section = line[2:].strip()
            continue
        line = line.split("//")[0].strip()   # a trailing comment on a slot line
        if not line or line.startswith("unsigned int version"):
            continue
        sm = re.match(r"(.+?)\(\*(\w+)\)\((.*)\);$", line)
        if not sm:
            sys.exit("unparsed slot line: " + line)
        ret, name, params = sm.group(1).strip(), sm.group(2), sm.group(3).strip()
        for pat, rep in TYPE_MAP:
            ret = re.sub(pat, rep, ret)
            params = re.sub(pat, rep, params)
        # parameter names out, so the list is types only (a function-pointer parameter keeps its own parameter list)
        params = re.sub(r"\(\*\w+\)", "(*)", params)
        parts, depth, cur = [], 0, ""
        for ch in params:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            if ch == "," and depth == 0:
                parts.append(cur.strip()); cur = ""
            else:
                cur += ch
        if cur.strip():
            parts.append(cur.strip())
        clean = []
        for p in parts:
            if "(*)" in p:
                clean.append(p)
                continue
            p = re.sub(r"\s*=\s*[^,]+$", "", p)               # a default argument
            p = re.sub(r"\s*\[\d*\]$", "*", p)               # float[3] -> float*
            pm = re.match(r"^(.*[\*&])\s*(\w+)$", p)          # "const char* pluginName" -> "const char*"
            if pm:
                p = pm.group(1)
            else:
                vm = re.match(r"^((?:const\s+)?(?:unsigned\s+)?[\w:]+)\s+(\w+)$", p)   # "bool opening" -> "bool"
                if vm and vm.group(2) not in ("int", "char", "long", "short"):
                    p = vm.group(1)
            clean.append(p)
        slots.append((section, ret, name, ", ".join(clean)))
    return slots, version


def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument("header")
    a.add_argument("--out", default=OUT, help="the .inc to write (default include/FlickSlots.inc)")
    a.add_argument("--macro", default="FLICK_SLOT", help="the X-macro name; its count is <macro>_COUNT")
    a = a.parse_args()
    text = open(a.header, encoding="utf-8").read()
    slots, version = parse(text)
    out = a.out if os.path.isabs(a.out) else os.path.join(ROOT, a.out)
    current = a.macro == "FLICK_SLOT"
    with open(out, "w", encoding="utf-8", newline="\r\n") as f:
        f.write("// GENERATED by tools/gen-flick-slots.py from the FLICK API header (version %s) - do not edit.\n" % version)
        f.write("// Source: %s\n" % os.path.basename(a.header))
        f.write("// %s(return type, name, (parameter types)) in the C table's order; %d slots.\n" % (a.macro, len(slots)))
        if current:
            f.write("#define FLICK_API_VERSION_ANSWERED %s\n" % version)
        f.write("#define %s_COUNT %d\n" % (a.macro, len(slots)))
        last = None
        for section, ret, name, params in slots:
            if section != last:
                f.write("// %s\n" % section)
                last = section
            f.write("%s(%s, %s, (%s))\n" % (a.macro, ret, name, params))
    print("wrote", out, len(slots), "slots, version", version)


if __name__ == "__main__":
    main()
