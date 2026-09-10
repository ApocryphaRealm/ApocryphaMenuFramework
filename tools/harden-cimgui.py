#!/usr/bin/env python3
"""
harden-cimgui.py - add null guards to the vendored cimgui, mechanically and reproducibly.

    python tools/harden-cimgui.py

Reads   extern/cimgui-1.90.8dock/cimgui.cpp   (pristine upstream, never edited by hand)
Writes  source/cimgui/cimgui.cpp              (what actually compiles)

WHY
    A mod written for SKSE Menu Framework calls into this framework through raw function pointers
    it resolved by name. Nothing validates what it passes. The wrappers this framework used to
    hand-write checked every pointer and string before use, "because the stock header calls
    whatever it got back"; cimgui's generated wrappers do not, so vendoring cimgui for full export
    parity traded a consumer's blank page for a consumer's crash.

    SMF has the same exposure - it is raw cimgui too - so this is not a regression against it. But
    a crash in our DLL is reported against our DLL, and a guard is cheap. The owner asked for the
    checks back on 2026-09-10; this is how they come back without hand-editing 1,397 functions or
    losing the ability to re-fetch a newer cimgui.

WHAT IS GUARDED, AND WHAT DELIBERATELY IS NOT
    1. `Type* self` as the first parameter - cimgui's method wrappers. Calling a method through a
       null self is a certain crash and never meaningful. Guarded: return a default.

    2. A `pOut` first parameter - cimgui's way of returning a struct by value. Guarded: return
       without writing, exactly as this framework's own igGetCursorScreenPos always did.

    3. Non-const pointers to primitives (bool*, int*, float*, ...) ANYWHERE in the signature -
       these are the values a widget writes through, and ImGui dereferences them unconditionally.
       Guarded: return a default.

       EXCEPT any whose name begins with `p_`. That is ImGui's own convention for an OPTIONAL
       pointer, and getting this wrong would be worse than the crash: igBegin(name, p_open, flags)
       is called with p_open = NULL by every window that has no close button, and guarding it would
       silently stop those windows drawing. Only four such names exist in this cimgui - p_open,
       p_selected, p_visible, p_dst_size - and all four are left alone.

    4. A `const char*` FIRST parameter - the label, str_id, fmt or name a widget is keyed on.
       Substituted with "" rather than returning, so the call still happens, which is what the
       hand-written wrappers did.

       Only the first parameter. Later `const char*` parameters are left alone on purpose, because
       for several of them NULL is meaningful rather than a mistake - igTextUnformatted's text_end,
       igLogToFile's filename - and substituting "" would change behaviour instead of preventing a
       crash.

RE-VENDORING A NEWER CIMGUI
    Drop the new cimgui.cpp/cimgui.h into extern/<tag>/, point SOURCE below at it, and run this
    again. The generated file is never edited by hand, so there is no patch to re-apply and no
    merge to get wrong.
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(HERE, "extern", "cimgui-1.90.8dock", "cimgui.cpp")
TARGET = os.path.join(HERE, "source", "cimgui", "cimgui.cpp")

PRIMITIVE = r"(?:bool|int|float|double|size_t|unsigned\s+int|ImU32|ImU64|ImS32|ImGuiID|ImWchar)"

# ImGui's own convention: a pointer parameter named p_* is optional and NULL is a valid, meaningful
# argument. Guarding one of these would stop real windows drawing.
OPTIONAL_PREFIX = "p_"


def default_return(ret: str) -> str:
    ret = ret.strip()
    if ret == "void":
        return "return;"
    if ret.endswith("*"):
        return "return nullptr;"
    if ret == "bool":
        return "return false;"
    if ret == "float":
        return "return 0.0f;"
    if ret == "double":
        return "return 0.0;"
    if ret in ("int", "size_t", "unsigned int", "ImU32", "ImU64", "ImS32", "ImGuiID", "ImWchar",
               "short", "unsigned short", "char", "unsigned char"):
        return "return 0;"
    return "return {};"        # aggregate returned by value


def split_args(args: str):
    """Top-level comma split - cimgui signatures have no templates, so this is enough."""
    out, depth, cur = [], 0, ""
    for ch in args:
        if ch in "([": depth += 1
        elif ch in ")]": depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip()); cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def guards_for(ret: str, args_text: str):
    """The guard lines for one function, in the order they should appear."""
    args = split_args(args_text)
    if not args or args == ["void"]:
        return []
    lines, seen = [], set()
    ret_default = default_return(ret)

    first = args[0]
    m = re.match(r"^(?:const\s+)?[A-Za-z_]\w*\s*\*\s*(self)$", first)
    if m:
        lines.append(f"if (!self) {{ {ret_default} }}")
        seen.add("self")
    m = re.match(r"^[A-Za-z_]\w*\s*\*\s*(pOut)$", first)
    if m:
        lines.append(f"if (!pOut) {{ {ret_default} }}")
        seen.add("pOut")
    m = re.match(r"^const\s+char\s*\*\s*([A-Za-z_]\w*)$", first)
    if m:
        name = m.group(1)
        lines.append(f'if (!{name}) {{ {name} = ""; }}')
        seen.add(name)

    for a in args:
        m = re.match(r"^" + PRIMITIVE + r"\s*\*\s*([A-Za-z_]\w*)(?:\[\d*\])?$", a)
        if not m:
            continue
        name = m.group(1)
        if name in seen or name.startswith(OPTIONAL_PREFIX):
            continue
        seen.add(name)
        lines.append(f"if (!{name}) {{ {ret_default} }}")
    return lines


def main() -> int:
    if not os.path.exists(SOURCE):
        raise SystemExit(f"pristine cimgui not found at {SOURCE}")
    src = io.open(SOURCE, encoding="utf-8", errors="replace").read()

    # cimgui's generator emits every definition as: CIMGUI_API <ret> <name>(<args>)\n{
    fn = re.compile(r"^(CIMGUI_API\s+(.+?)\s+([A-Za-z_]\w*)\(([^\n]*)\))\s*\n\{", re.M)

    out, pos = [], 0
    counted = {"self": 0, "pOut": 0, "string": 0, "outparam": 0}
    touched = 0
    for m in fn.finditer(src):
        ret, name, args = m.group(2).strip(), m.group(3), m.group(4).strip()
        lines = guards_for(ret, args)
        out.append(src[pos:m.end()])
        pos = m.end()
        if lines:
            touched += 1
            for L in lines:
                if L.startswith("if (!self)"): counted["self"] += 1
                elif L.startswith("if (!pOut)"): counted["pOut"] += 1
                elif '= ""' in L: counted["string"] += 1
                else: counted["outparam"] += 1
            # the marker goes on EVERY guard line, so grepping for it is an exact audit of what
            # this script changed - nothing has to be inferred from a line's position
            marker = "   // AMF null guard"
            out.append("".join("\n    " + L + marker for L in lines))
    out.append(src[pos:])

    banner = (
        "// ============================================================================================\n"
        "// GENERATED - do not edit. Produced by tools/harden-cimgui.py from\n"
        "// extern/cimgui-1.90.8dock/cimgui.cpp (upstream, unmodified).\n"
        "//\n"
        "// The only difference from upstream is a null guard at the top of functions that would\n"
        "// otherwise dereference a pointer a consumer passed as null. Consumers reach these through\n"
        "// raw function pointers they resolved by name, so nothing else validates what they pass.\n"
        "// The generator's docstring explains exactly what is guarded and what is deliberately not -\n"
        "// in particular that ImGui's optional p_* pointers are left alone, because guarding p_open\n"
        "// would stop every window without a close button from drawing.\n"
        "// ============================================================================================\n"
    )
    text = banner + "".join(out)
    os.makedirs(os.path.dirname(TARGET), exist_ok=True)
    io.open(TARGET, "w", encoding="utf-8", newline="\n").write(text)

    total = sum(counted.values())
    print(f"generated {os.path.relpath(TARGET, HERE)}")
    print(f"  {touched} of the functions gained a guard, {total} guards in total:")
    print(f"     self pointer      {counted['self']}")
    print(f"     pOut out-struct   {counted['pOut']}")
    print(f"     label/str_id/fmt  {counted['string']}  (substituted with \"\", not returned)")
    print(f"     value out-params  {counted['outparam']}  (p_* optional pointers excluded)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
