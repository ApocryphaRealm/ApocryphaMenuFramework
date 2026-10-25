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

       EXCEPT any whose name begins with `p_` - those are decided by shape 5 below, from what ImGui
       accepts: igBegin(name, p_open, flags) is called with p_open = NULL by every window that has no
       close button, and guarding it would silently stop those windows drawing, but Selectable's
       p_selected and SliderScalar's p_data are read unconditionally.

    4. A `const char*` FIRST parameter - the label, str_id, fmt or name a widget is keyed on.
       Substituted with "" rather than returning, so the call still happens, which is what the
       hand-written wrappers did.
       EXCEPT (2.3.0) when it is the BEGIN of a [begin, end) range (str_begin/str_end, text/text_end): "" with the
       caller's end is not a range - igImStrchrRange(NULL, end) would walk from a literal to an unrelated address - so
       the function returns its default. A push (igPushID_StrStr) gets an EMPTY range instead ("" to ""), because a
       push that does not happen makes the consumer's matching Pop take someone else's entry off the stack.

    5. (2.3.0) EVERY OTHER POINTER PARAMETER that Dear ImGui reads - so that "every pointer AMF reads is
       null-checked" is true of the consumer surface, as tools/null_check_audit.py measures it. Until 2.3.0 the
       four shapes above were all, and 564 exported pointer parameters went unguarded (12 of them p_data, which
       DragScalar / SliderScalar / InputScalar dereference). A parameter is guarded when the wrapper reads
       through it or hands it to a Dear ImGui slot that does not take NULL. A slot takes NULL when
         - its declaration in imgui.h / imgui_internal.h gives it a NULL default (read from vcpkg's headers in a
           configured build tree, exactly as the audit reads them through libclang), or
         - tools/imgui-null-ok.json lists it, with the line in imgui.cpp that handles the NULL (p_open,
           FocusWindow's window, PushFont's font, MenuItem's p_selected, ...). The audit reads the same file,
           so the two cannot disagree.
       What the guard does:
         - `self` anywhere (also after a pOut), any other object pointer, a function pointer, a writable
           buffer: return the function's default value;
         - a `const char*` with a string default in imgui.h ("%.3f", "%d", "%.0f deg"): substitute it;
         - the end of a [begin, end) range whose begin is already proven: end = begin + strlen(begin), which is
           what ImGui does itself wherever it accepts a NULL end;
         - a range's begin, a mode / file name / font data, a text ImGui reads at given offsets: return;
         - any other `const char*` (a format, a label, a hint): substitute "", so the call still happens.
       Later `const char*` parameters where NULL is meaningful - igTextUnformatted's text_end,
       igLogToFile's filename - have a NULL default or a list entry, so they stay unguarded.

THE PROOF THAT THE GUARDS CHANGE NOTHING ELSE
    Every guard line ends in "// AMF null guard". Generate a copy without them and compare:
        python tools/harden-cimgui.py --no-guards --out <scratch>\\cimgui-unguarded.cpp
    source/cimgui/cimgui.cpp with its marked lines removed must equal that copy line for line, and the copy
    differs from upstream only in the rewritten bodies described below (text fields, headers, SameLine,
    Checkbox, sliders, drags, tooltips, text). `--out <path>` writes anywhere without touching the compiled copy.

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


# ------------------------------------------------------------------ Dear ImGui's own declarations (2.3.0)
# The guards below the first four shapes are decided from what Dear ImGui itself accepts, read from the imgui.h and
# imgui_internal.h the build compiles against (vcpkg's, under build\<preset>\vcpkg_installed\<triplet>\include) - the
# same declarations tools/null_check_audit.py reads through libclang - plus tools/imgui-null-ok.json, the one list both
# tools read of parameters ImGui takes as NULL without saying so in a default.
NULL_DEFAULT = re.compile(r"=\s*(NULL|nullptr|0|0u|\(\w+\)0)\s*$")      # the audit's param_default_is_null, verbatim


def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
    return re.sub(r'//[^\n"]*$', "", text, flags=re.M)


def _block_end(text: str, i: int) -> int:
    """text[i] == '{'; the index just past its matching '}'."""
    d = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            d += 1
        elif text[j] == "}":
            d -= 1
            if d == 0:
                return j + 1
    return len(text)


def param_name(a: str) -> str:
    a = re.sub(r"\s*=.*$", "", a).strip()
    m = re.search(r"\(\s*[A-Z_]*\s*\*\s*(\w+)\s*\)", a)        # int (IMGUI_CDECL *compare_func)(...)
    if m:
        return m.group(1)
    m = re.search(r"(\w+)\s*(\[[^\]]*\])?$", a)
    return m.group(1) if m else a


class ImGuiHeaders:
    """Every function declaration in imgui.h + imgui_internal.h, by (scope, name): scope is a struct name, "ImGui", or
    "" for a free function. A declaration's parameters keep their default text."""

    DECL = re.compile(r"\b(\w+)\s*\(([^;{}()]*(?:\([^()]*\)[^;{}()]*)*)\)\s*(?:const)?\s*"
                      r"(?:IM_FMTARGS\(\d\)|IM_FMTLIST\(\d\))?\s*[;{]")

    def __init__(self, include_dir: str):
        self.decls = {}
        self.pointer_typedefs, self.fnptr_typedefs = set(), set()
        for fn in ("imgui.h", "imgui_internal.h"):
            text = _strip_comments(io.open(os.path.join(include_dir, fn), encoding="utf-8").read())
            spans = []
            for m in re.finditer(r"\bnamespace\s+ImGui\s*\{", text):
                spans.append((m.end() - 1, _block_end(text, m.end() - 1), "ImGui"))
            for m in re.finditer(r"^\s*struct\s+(?:IMGUI_API\s+)?(\w+)\b[^;{()]*\{", text, re.M):
                spans.append((m.end() - 1, _block_end(text, m.end() - 1), m.group(1)))
            for m in re.finditer(r"typedef\s+[^;]*?\(\s*[A-Z_]*\s*\*\s*(\w+)\s*\)\s*\([^;]*\);", text):
                self.fnptr_typedefs.add(m.group(1))
            for m in re.finditer(r"typedef\s+[\w\s]+\*\s*(\w+)\s*;", text):
                self.pointer_typedefs.add(m.group(1))
            for m in self.DECL.finditer(text):
                name = m.group(1)
                if name in ("if", "for", "while", "switch", "return", "sizeof", "IM_ASSERT", "IM_MSVC_RUNTIME_CHECKS_OFF"):
                    continue
                inner = [s for s in spans if s[0] < m.start() < s[1]]
                scope = min(inner, key=lambda s: s[1] - s[0])[2] if inner else ""
                self.decls.setdefault((scope, name), []).append(split_args(m.group(2)))

    def lookup(self, scope: str, name: str, arg_names):
        """The declaration a call with these arguments reaches: same arity, then the most parameter names in common."""
        cands = self.decls.get((scope, name)) or ([] if scope else self.decls.get(("ImGui", name), []))
        best, best_score = None, -1
        for params in cands:
            if params == ["void"]:
                params = []
            required = sum(1 for p in params if "=" not in p)
            if not (required <= len(arg_names) <= len(params)):
                continue
            score = sum(1 for i, a in enumerate(arg_names) if a and param_name(params[i]) == a)
            if score > best_score:
                best, best_score = params, score
        return best


def find_imgui_include() -> str:
    """vcpkg's ImGui headers in a configured build tree; every copy found must be the same file."""
    found = []
    bdir = os.path.join(HERE, "build")
    for preset in sorted(os.listdir(bdir)) if os.path.isdir(bdir) else []:
        inst = os.path.join(bdir, preset, "vcpkg_installed")
        for trip in sorted(os.listdir(inst)) if os.path.isdir(inst) else []:
            inc = os.path.join(inst, trip, "include")
            if os.path.isfile(os.path.join(inc, "imgui.h")) and os.path.isfile(os.path.join(inc, "imgui_internal.h")):
                found.append(inc)
    if not found:
        raise SystemExit("no imgui.h under build\\<preset>\\vcpkg_installed\\<triplet>\\include - run configure.bat or "
                         "configure17.bat first (the guards are decided from Dear ImGui's own declarations)")
    first = [io.open(os.path.join(found[0], f), "rb").read() for f in ("imgui.h", "imgui_internal.h")]
    for inc in found[1:]:
        if [io.open(os.path.join(inc, f), "rb").read() for f in ("imgui.h", "imgui_internal.h")] != first:
            raise SystemExit("the build trees hold different imgui.h / imgui_internal.h (%s vs %s)" % (found[0], inc))
    ver = re.search(rb'#define\s+IMGUI_VERSION\s+"([^"]+)"', first[0])
    if not ver or ver.group(1) != b"1.90.8":
        raise SystemExit("vcpkg's imgui.h is %s, this cimgui is generated for 1.90.8" % (ver.group(1) if ver else "unversioned"))
    return found[0]


def load_null_ok():
    with io.open(os.path.join(HERE, "tools", "imgui-null-ok.json"), encoding="utf-8") as f:
        d = __import__("json").load(f)
    return {e["param"] for e in d["names"]}, {(e["callee"], e["param"]) for e in d["params"]}


CALL = re.compile(r"(ImGui::|self->|\b)([A-Za-z_]\w*)\s*\(")
CAST = re.compile(r"^\(\s*(?:const\s+)?[\w:]+(?:\s*\*)*\s*\)\s*")
NOT_CALLS = {"if", "return", "sizeof", "IM_PLACEMENT_NEW", "IM_NEW", "IM_DELETE", "IM_ALLOC", "IM_FREE"}


def calls_in(body: str):
    """Every call in a wrapper body: (scope, name, [argument texts])."""
    out = []
    for m in CALL.finditer(body):
        if m.group(2) in NOT_CALLS:
            continue
        j = k = m.end()
        d = 1
        while k < len(body) and d:
            if body[k] == "(":
                d += 1
            elif body[k] == ")":
                d -= 1
            k += 1
        out.append((m.group(1), m.group(2), split_args(body[j:k - 1])))
    return out


def bare_arg(a: str) -> str:
    """An argument's parameter name once casts and parentheses are taken off ("" when it is an expression)."""
    a = a.strip()
    while True:
        b = CAST.sub("", a)
        if b.startswith("(") and b.endswith(")"):
            b = b[1:-1].strip()
        if b == a:
            break
        a = b
    return a if re.fullmatch(r"[A-Za-z_]\w*", a) else ""


def param_type(a: str) -> str:
    a = re.sub(r"\s*=.*$", "", a).strip()
    m = re.search(r"\(\s*\*\s*\w+\s*\)", a)
    if m:
        return a[:m.start()].strip() + "(*)" + a[m.end():]
    m = re.match(r"^(.*?)(\w+)\s*(\[[^\]]*\])?$", a)
    return (m.group(1).strip() + ("*" if m.group(3) else "")) if m else a


# Non-first strings that must never become "": a mode for fopen (the CRT's invalid-parameter handler ends the process on
# an empty mode), a file or font to load, and texts ImGui reads at offsets it was given (a base the line index points
# into, the atlas's rectangle drawn from a string, a position inside a caller's text that ImGui walks back from).
RETURN_ON_NULL_STRINGS = {"mode", "filename", "compressed_font_data_base85", "dst_name", "in_str", "base", "in_text_curr"}
# Functions whose "nothing found" is -1, not 0.
RETURN_OVERRIDE = {"igTypingSelectFindMatch": "return -1;", "igTypingSelectFindBestLeadingMatch": "return -1;",
                   "igTypingSelectFindNextSingleCharMatch": "return -1;"}


def new_guards(name, ret, params, body, hdr, null_ok_names, null_ok_at, already):
    """The 2.3.0 guards: every pointer parameter Dear ImGui reads, not only the four first-parameter shapes."""
    ret_default = RETURN_OVERRIDE.get(name, default_return(ret))
    names = [param_name(p) for p in params]
    types = {param_name(p): param_type(p) for p in params}
    self_type = re.sub(r"\b(const|struct)\b|\*", "", types.get("self", "")).strip()
    calls = calls_in(body)
    out = []
    for i, p in enumerate(params):
        n, t = names[i], types[names[i]]
        if n in already or "va_list" in t:
            continue
        is_fnptr = "(*)" in t or t in hdr.fnptr_typedefs
        is_ptr = is_fnptr or "*" in t or t in hdr.pointer_typedefs or re.sub(r"^const\s+", "", t) in hdr.pointer_typedefs
        if not is_ptr:
            continue
        P = re.escape(n)
        derefs = bool(re.search(r"\b" + P + r"\s*->|(^|[=(,;{\s])\*\s*" + P + r"\b|\b" + P + r"\s*\[|\b" + P + r"\s*\(", body))
        needs, dflt = derefs, None
        for scope, cname, args in calls:
            arg_names = [bare_arg(a) for a in args]
            if n not in arg_names:
                continue
            idx = arg_names.index(n)
            qscope = "ImGui" if scope == "ImGui::" else (self_type if scope == "self->" else "")
            decl = hdr.lookup(qscope, cname, arg_names)
            if decl is None:
                if not qscope and (cname, cname) in hdr.decls:
                    continue            # a constructor stores what it is given (the audit counts no read either)
                needs = True            # va_start, a macro, a function ImGui does not declare: not proven
                continue
            cp = decl[idx]
            cpn = param_name(cp)
            if qscope:
                qual = qscope + "::" + cname
            else:
                qual = cname if ("", cname) in hdr.decls else "ImGui::" + cname
            if NULL_DEFAULT.search(cp) or cpn in null_ok_names or (qual, cpn) in null_ok_at:
                continue                # Dear ImGui takes NULL here - a guard would change what the call does
            needs = True
            m = re.search(r'=\s*("(?:[^"\\]|\\.)*")\s*$', cp)
            if m and dflt is None:
                dflt = m.group(1)
        if not needs:
            continue
        flat = re.sub(r"\s+", " ", t)
        if n == "self":
            out.append((f"if (!self) {{ {ret_default} }}", "self"))
        elif flat in ("const char*", "const char *"):
            begin = next((b for b in (n[:-4], n[:-4] + "_begin") if n.endswith("_end") and b in names[:i]), None)
            if dflt is not None:
                out.append((f"if (!{n}) {{ {n} = {dflt}; }}", "string-default"))
            elif (n.endswith("_end") and begin and re.sub(r"\s+", "", types[begin]) == "constchar*"
                  and (begin in already or any(L.startswith(f"if (!{begin})") for L, _ in out))):
                # the begin is already proven non-null above, so strlen(begin) reads a real string
                out.append((f"if (!{n}) {{ {n} = {begin} + strlen({begin}); }}", "string-end"))
            elif (n.endswith("_end") or n in RETURN_ON_NULL_STRINGS or (n + "_end") in names
                  or (n.endswith("_begin") and (n[:-6] + "_end") in names)):
                out.append((f"if (!{n}) {{ {ret_default} }}", "pointer"))
            else:
                out.append((f'if (!{n}) {{ {n} = ""; }}', "string-empty"))
        else:
            out.append((f"if (!{n}) {{ {ret_default} }}", "fnptr" if is_fnptr else "pointer"))
    return out


def guards_for(ret: str, args_text: str, fname: str = ""):
    """The guard lines for one function, in the order they should appear."""
    args = split_args(args_text)
    if not args or args == ["void"]:
        return [], set()
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
        names = [param_name(a) for a in args]
        end = next((e for e in (name + "_end", name[:-6] + "_end" if name.endswith("_begin") else None)
                    if e and e in names[1:]), None)
        if end is None:
            lines.append(f'if (!{name}) {{ {name} = ""; }}')
        elif fname.startswith("igPush"):
            # 2.3.0: a push must still push, or the consumer's matching Pop takes someone else's entry off the stack -
            # so a NULL begin becomes an EMPTY range ("" to ""), never "" to the caller's end, which is no range at all
            lines.append(f'if (!{name}) {{ {name} = ""; {end} = {name}; }}')
        else:
            # 2.3.0: the begin of a [begin, end) range. "" with the caller's end is not a range (igImStrchrRange(NULL, end)
            # would walk from a literal to an unrelated address), so a NULL begin returns the function's default
            lines.append(f"if (!{name}) {{ {ret_default} }}")
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
    return lines, seen


def main() -> int:
    if not os.path.exists(SOURCE):
        raise SystemExit(f"pristine cimgui not found at {SOURCE}")
    src = io.open(SOURCE, encoding="utf-8", errors="replace").read()

    # cimgui's generator emits every definition as: CIMGUI_API <ret> <name>(<args>)\n{
    fn = re.compile(r"^(CIMGUI_API\s+(.+?)\s+([A-Za-z_]\w*)\(([^\n]*)\))\s*\n\{", re.M)

    out, pos = [], 0
    counted = {"self": 0, "pOut": 0, "string": 0, "outparam": 0}
    touched = 0
    # 1.8.9: the on-screen keyboard has to know which ImGui items are TEXT FIELDS, and the only
    # place every consumer's text field passes through is these four exports. Their bodies are
    # rewritten to note the item's id after the call (see Keyboard.h). Nothing else changes.
    TEXT_FIELDS = {"igInputText", "igInputTextMultiline", "igInputTextWithHint", "igInputTextEx"}
    noted = 0
    # 2.0.6 (C0kAdam, the dMenu NG author): every collapsing-header and tree-node export. A FRAMED
    # header widens its frame to the left by IM_TRUNC(window->WindowPadding.x * 0.5f - 1) (TreeNodeBehavior,
    # imgui_widgets.cpp 1.90.8), and AMF's windows carry a 34 px padding where stock ImGui has 8, so the
    # frame's left edge - GetItemRectMin().x - moved 13 px further from the arrow than it does under SKSE
    # Menu Framework, and dMenu's icon, placed from that edge, landed on the arrow. The body is wrapped in
    # amf_BeginConsumerHeader()/amf_EndConsumerHeader() (ConsumerSurface.cpp), which give the call the
    # stock padding and put AMF's back. TreeNodeBehavior reads WindowPadding for nothing else.
    HEADER_PREFIXES = ("igCollapsingHeader_", "igTreeNode_", "igTreeNodeV_", "igTreeNodeEx_", "igTreeNodeExV_")
    HEADER_NAMES = {"igTreeNodeBehavior"}
    wrapped = []
    # 2.1.6 (the owner's screenshots of Show Player In Inventory: its sliders and its button sat on top of their own
    # labels): a consumer's SameLine(<pixels>) - "Offset X" then SameLine(130.0f) - is a column laid out for SKSE Menu
    # Framework's small text. At a big text size the label is wider than the column and the next item lands on it.
    # The export goes through amf_ConsumerSameLine (ConsumerSurface.cpp), which keeps the column when the label fits
    # and otherwise starts the next item just after the label, as SameLine() would.
    sameline = 0
    # 2.1.9 (the owner, on Simple Timed Block's SKSE Menu Framework page: stock tick boxes, and in Norden - Black invisible,
    # black on black): rule 32 holds on a consumer's page too. The Checkbox export goes through amf_ConsumerCheckbox
    # (ConsumerSurface.cpp), which draws AMF's on/off switch with the same label, value and result.
    checkbox = 0
    # 2.2.0 (the owner, testing another mod: the mouse wheel did not move a slider after clicking it): every
    # single-value slider export calls amf_ConsumerSliderWheel (ConsumerSurface.cpp) after ImGui's own slider - one wheel
    # notch over the slider moves it one unit of the digit its format shows, clamped, and the export returns true.
    # name -> the (data type, value pointer, min pointer, max pointer, format) the helper is given.
    SLIDERS = {
        "igSliderFloat":   ("ImGuiDataType_Float", "v", "&v_min", "&v_max", "format"),
        "igSliderInt":     ("ImGuiDataType_S32", "v", "&v_min", "&v_max", "format"),
        "igSliderScalar":  ("data_type", "p_data", "p_min", "p_max", "format"),
        "igVSliderFloat":  ("ImGuiDataType_Float", "v", "&v_min", "&v_max", "format"),
        "igVSliderInt":    ("ImGuiDataType_S32", "v", "&v_min", "&v_max", "format"),
        "igVSliderScalar": ("data_type", "p_data", "p_min", "p_max", "format"),
    }
    sliders = 0
    # 2.3.0 ONE WAY for sliders (4. plans\amf-null-checks\ONE-WAY-SLIDERS.md): the three horizontal single-value slider
    # exports read the value before ImGui's slider and, after it, hold a keyboard / D-pad nudge to one unit of the shown
    # digit (amf_ConsumerSliderNav, ConsumerSurface.cpp) - set from the value before the call, so a mod's own precise
    # wrapper around the export still makes one step. The vertical sliders (Up / Down) and igSliderAngle keep ImGui's step.
    NAV_SLIDERS = {"igSliderFloat", "igSliderInt", "igSliderScalar"}
    nav_sliders = 0
    # 2.3.0 (combined run 2026-10-25 1515: Left on a mod's slider sent the highlight back to the mod list): a widget that
    # Left/Right adjust tells the side-navigation it holds the highlight (navside::NoteArrowItem, NavSide.h), so Left does
    # not leave the page. The single-value sliders above already note it through the wheel helper; these are the rest -
    # every drag and the multi-value sliders - which call amf_NoteArrowItem (ConsumerSurface.cpp) after ImGui's own widget.
    ARROW_ITEMS = {"igDragFloat", "igDragFloat2", "igDragFloat3", "igDragFloat4", "igDragFloatRange2", "igDragInt",
                   "igDragInt2", "igDragInt3", "igDragInt4", "igDragIntRange2", "igDragScalar", "igDragScalarN",
                   "igSliderFloat2", "igSliderFloat3", "igSliderFloat4", "igSliderInt2", "igSliderInt3", "igSliderInt4",
                   "igSliderScalarN"}
    arrow_items = 0
    # 2.2.2 (BanMarwein: Skyrim Performance Solution's long description ran off the screen on one line): a mod's tooltips
    # are wrapped to the window it draws in. The seven tooltip exports call consumer_tooltip:: (ConsumerTooltip.h) in
    # place of the same-named ImGui function; Dear ImGui never wraps a tooltip.
    TOOLTIPS = {"igBeginTooltip": "BeginTooltip", "igEndTooltip": "EndTooltip", "igBeginItemTooltip": "BeginItemTooltip",
                "igSetTooltip": "SetTooltipV", "igSetTooltipV": "SetTooltipV", "igSetItemTooltip": "SetItemTooltipV",
                "igSetItemTooltipV": "SetItemTooltipV"}
    tooltips = 0
    # 2.2.4 (the menu render matrix: Compass Navigation Overhaul's INI path line was cut mid-word at the window's edge from
    # text size 2.00): a mod's plain text on its AMF page wraps inside the page instead of running past its edge. The seven
    # text exports run between amf_BeginConsumerText() and amf_EndConsumerText() (ConsumerSurface.cpp), which turn wrapping
    # on only on a mod's page, only when the mod set no wrap position of its own, and never in a window that scrolls
    # sideways. Text that fits draws exactly as before.
    TEXTS = {"igTextUnformatted", "igText", "igTextV", "igTextColored", "igTextColoredV", "igTextDisabled", "igTextDisabledV"}
    texts = 0
    hdr = ImGuiHeaders(find_imgui_include())
    null_ok_names, null_ok_at = load_null_ok()
    for m in fn.finditer(src):
        ret, name, args = m.group(2).strip(), m.group(3), m.group(4).strip()
        lines, seen = guards_for(ret, args, name)
        kinds = []
        for L in lines:
            if L.startswith("if (!self)"): kinds.append("self")
            elif L.startswith("if (!pOut)"): kinds.append("pOut")
            elif '= ""' in L: kinds.append("string")
            elif re.match(r"^const\s+char\s*\*", split_args(args)[0]) and \
                    L.startswith("if (!" + param_name(split_args(args)[0]) + ")"):
                kinds.append("range-begin")
            else: kinds.append("outparam")
        # 2.3.0: every other pointer parameter Dear ImGui reads, decided from the upstream body's own calls
        upstream_body = src[m.end():src.index("\n}", m.end())]
        for L, kind in new_guards(name, ret, split_args(args), upstream_body, hdr, null_ok_names, null_ok_at, seen):
            lines.append(L)
            kinds.append(kind)
        out.append(src[pos:m.end()])
        pos = m.end()
        # The guards come FIRST. Until 2.0.6 they were appended after a rewritten body below, which put
        # the text fields' label guard after their `return` - unreachable, so a null label still crashed.
        if "--no-guards" in sys.argv:   # the identity proof's other half (docstring, THE PROOF)
            lines = []
        if lines:
            touched += 1
            for k in kinds:
                counted[k] = counted.get(k, 0) + 1
            # the marker goes on EVERY guard line, so grepping for it is an exact audit of what
            # this script changed - nothing has to be inferred from a line's position
            marker = "   // AMF null guard"
            out.append("".join("\n    " + L + marker for L in lines))
        if name == "igSameLine":
            body_end = src.index("\n}", pos)
            assert src[pos:body_end].strip() == "return ImGui::SameLine(offset_from_start_x,spacing);", src[pos:body_end]
            out.append("\n    return amf_ConsumerSameLine(offset_from_start_x, spacing);   // AMF: never on top of the label (2.1.6)")
            pos = body_end
            sameline += 1
        elif name == "igCheckbox":
            body_end = src.index("\n}", pos)
            assert src[pos:body_end].strip() == "return ImGui::Checkbox(label,v);", src[pos:body_end]
            out.append("\n    return amf_ConsumerCheckbox(label, v);   // AMF: on/off as AMF's switch (rule 32)")
            pos = body_end
            checkbox += 1
        elif name in SLIDERS or name == "igSliderAngle":
            body_end = src.index("\n}", pos)
            body = src[pos:body_end]
            assert body.strip().startswith("return ImGui::"), name
            call = body.strip()[len("return "):].rstrip(";")
            if name == "igSliderAngle":
                wheel = "amf_ConsumerSliderAngleWheel(v_rad, v_degrees_min, v_degrees_max, format)"
            else:
                wheel = "amf_ConsumerSliderWheel(" + ", ".join(SLIDERS[name]) + ")"
            if name in NAV_SLIDERS:
                # 2.3.0 ONE WAY for sliders: a keyboard / D-pad nudge is one unit of the shown digit (amf_ConsumerSliderNav)
                t, v, lo, hi, fmt = SLIDERS[name]
                out.append("\n    const double amf_b = amf_ConsumerSliderValue(" + t + ", " + v + ");   // AMF: the value before (2.3.0)\n"
                           "    const bool amf_r = " + call + ";\n"
                           "    const bool amf_n = amf_ConsumerSliderNav(" + ", ".join((t, v, "amf_b", lo, hi, fmt, "flags")) +
                           ");   // AMF: one unit per nudge (2.3.0)\n"
                           "    const bool amf_w = " + wheel + ";   // AMF: the wheel nudges a slider (2.2.0)\n"
                           "    return amf_r || amf_n || amf_w;")
                nav_sliders += 1
            else:
                out.append("\n    const bool amf_r = " + call + ";\n"
                           "    const bool amf_w = " + wheel + ";   // AMF: the wheel nudges a slider (2.2.0)\n"
                           "    return amf_r || amf_w;")
            pos = body_end
            sliders += 1
        elif name in ARROW_ITEMS:
            body_end = src.index("\n}", pos)
            body = src[pos:body_end]
            assert body.strip().startswith("return ImGui::"), name
            call = body.strip()[len("return "):].rstrip(";")
            out.append("\n    const bool amf_r = " + call + ";\n"
                       "    amf_NoteArrowItem();   // AMF: Left/Right adjust it, so Left stays on the page (2.3.0)\n"
                       "    return amf_r;")
            pos = body_end
            arrow_items += 1
        elif name in TEXTS:
            body_end = src.index("\n}", pos)
            body = src[pos:body_end]
            stripped = body.strip()
            if stripped.startswith("return ImGui::"):
                # void igTextUnformatted(text, text_end) { return ImGui::TextUnformatted(text,text_end); }
                call = stripped[len("return "):].rstrip(";")
                out.append("\n    const bool amf_t = amf_BeginConsumerText();   // AMF: wraps inside the page (2.2.4)\n"
                           "    " + call + ";\n"
                           "    amf_EndConsumerText(amf_t);")
            else:
                # the variadic shape: va_start; ImGui::...V(...); va_end;
                call_line = re.search(r"^(\s*)ImGui::Text[A-Za-z]*V\([^\n]*;$", body, re.M)
                assert call_line, name
                indent = call_line.group(1)
                body = (body[:call_line.start()] +
                        indent + "const bool amf_t = amf_BeginConsumerText();   // AMF: wraps inside the page (2.2.4)\n" +
                        call_line.group(0) + "\n" +
                        indent + "amf_EndConsumerText(amf_t);" +
                        body[call_line.end():])
                out.append(body)
            pos = body_end
            texts += 1
        elif name in TOOLTIPS:
            body_end = src.index("\n}", pos)
            body = src[pos:body_end]
            target = "ImGui::" + TOOLTIPS[name] + "("
            assert body.count(target) == 1, (name, body)
            out.append(body.replace(target, "consumer_tooltip::" + TOOLTIPS[name] + "(") + "   // AMF: wrapped (2.2.2)")
            pos = body_end
            tooltips += 1
        elif name in TEXT_FIELDS:
            body_end = src.index("\n}", pos)
            body = src[pos:body_end]
            assert body.strip().startswith("return ImGui::"), name
            call = body.strip()[len("return "):].rstrip(";")
            out.append("\n    const bool amf_r = " + call + ";   // AMF keyboard: note the text field\n"
                       "    amf_NoteTextField(ImGui::GetItemID());\n    return amf_r;")
            pos = body_end
            noted += 1
        elif name.startswith(HEADER_PREFIXES) or name in HEADER_NAMES:
            body_end = src.index("\n}", pos)
            body = src[pos:body_end]
            stripped = body.strip()
            if stripped.startswith("return ImGui::"):
                # bool igTreeNode_Str(label) { return ImGui::TreeNode(label); }
                call = stripped[len("return "):].rstrip(";")
                out.append("\n    amf_BeginConsumerHeader();   // AMF: stock header widening (2.0.6)\n"
                           "    const bool amf_r = " + call + ";\n"
                           "    amf_EndConsumerHeader();\n    return amf_r;")
            else:
                # the variadic shape: va_start; bool ret = ImGui::...V(...); va_end; return ret;
                call_line = re.search(r"^(\s*)bool ret = ImGui::[^\n]*;$", body, re.M)
                assert call_line, name
                indent = call_line.group(1)
                body = (body[:call_line.start()] +
                        indent + "amf_BeginConsumerHeader();   // AMF: stock header widening (2.0.6)\n" +
                        call_line.group(0) + "\n" +
                        indent + "amf_EndConsumerHeader();" +
                        body[call_line.end():])
                out.append(body)
            pos = body_end
            wrapped.append(name)
    out.append(src[pos:])

    banner = (
        "// ============================================================================================\n"
        "// GENERATED - do not edit. Produced by tools/harden-cimgui.py from\n"
        "// extern/cimgui-1.90.8dock/cimgui.cpp (upstream, unmodified).\n"
        "//\n"
        "// The differences from upstream: a null guard at the top of functions that would otherwise\n"
        "// dereference a pointer a consumer passed as null (consumers reach these through raw function\n"
        "// pointers they resolved by name, so nothing else validates what they pass); the four text-field\n"
        "// exports note their item for the on-screen keyboard; and the collapsing-header / tree-node\n"
        "// exports run with ImGui's stock window padding so a framed header's edge sits where it does\n"
        "// under SKSE Menu Framework (2.0.6); SameLine never puts an item on top of the one before it (2.1.6);\n"
        "// Checkbox draws AMF's on/off switch, as every boolean in the menu does (rule 32, 2.1.9); and the\n"
        "// single-value slider exports take a mouse-wheel notch as one step of the shown digit (2.2.0); the tooltip\n"
        "// exports wrap a mod's tooltip to the window it draws in (ConsumerTooltip.h, 2.2.2); the plain text exports wrap\n"
        "// a mod's text inside its AMF page (2.2.4); the drag and multi-value slider exports note the highlight so Left\n"
        "// adjusts them instead of leaving the page (2.3.0); the horizontal single-value slider exports hold a keyboard /\n"
        "// D-pad nudge to one unit of the shown digit (2.3.0).\n"
        "// The generator's docstring explains exactly what is guarded and what is deliberately not. Since 2.3.0 every\n"
        "// pointer parameter Dear ImGui reads is guarded; one that ImGui takes as NULL (a NULL default in imgui.h, or\n"
        "// tools/imgui-null-ok.json - p_open, FocusWindow's window, PushFont's font) is left alone, because guarding\n"
        "// p_open would stop every window without a close button from drawing.\n"
        "// ============================================================================================\n"
    )
    # 1.8.9: the text-field hook the four rewritten exports call (defined in Keyboard.cpp).
    assert noted == 4, noted
    # 2.0.6: the header-widening pair the wrapped tree-node exports call (defined in ConsumerSurface.cpp).
    assert len(wrapped) == 13, wrapped
    # 2.1.6: the column-overlap guard the SameLine export calls (defined in ConsumerSurface.cpp).
    assert sameline == 1, sameline
    # 2.1.9: the switch the Checkbox export draws (defined in ConsumerSurface.cpp).
    assert checkbox == 1, checkbox
    # 2.2.0: the wheel the seven single-value slider exports call (defined in ConsumerSurface.cpp).
    assert sliders == 7, sliders
    # 2.3.0: three of them also hold a keyboard / D-pad nudge to one unit (amf_ConsumerSliderNav, ConsumerSurface.cpp).
    assert nav_sliders == 3, nav_sliders
    # 2.3.0: the twelve drags and seven multi-value sliders note the highlight (amf_NoteArrowItem, ConsumerSurface.cpp).
    assert arrow_items == 19, arrow_items
    # 2.2.2: the seven tooltip exports (ConsumerTooltip.h, included after cimgui.h below).
    assert tooltips == 7, tooltips
    # 2.2.4: the seven text exports (amf_BeginConsumerText / amf_EndConsumerText, ConsumerSurface.cpp).
    assert texts == 7, texts
    text = (banner + "void amf_NoteTextField(unsigned int a_itemId);\n"
            "void amf_BeginConsumerHeader();\nvoid amf_EndConsumerHeader();\n"
            "void amf_ConsumerSameLine(float a_offsetFromStartX, float a_spacing);\n"
            "bool amf_ConsumerCheckbox(const char* a_label, bool* a_value);\n"
            "bool amf_ConsumerSliderWheel(int a_type, void* a_data, const void* a_min, const void* a_max, const char* a_format);\n"
            "bool amf_ConsumerSliderAngleWheel(float* a_rad, float a_minDegrees, float a_maxDegrees, const char* a_format);\n"
            "double amf_ConsumerSliderValue(int a_type, const void* a_data);\n"
            "bool amf_ConsumerSliderNav(int a_type, void* a_data, double a_before, const void* a_min, const void* a_max, const char* a_format, int a_flags);\n"
            "void amf_NoteArrowItem();\n"
            "bool amf_BeginConsumerText();\nvoid amf_EndConsumerText(bool a_pushed);\n"
            + "".join(out))
    assert text.count('#include "cimgui.h"\n') == 1
    text = text.replace('#include "cimgui.h"\n', '#include "cimgui.h"\n#include "ConsumerTooltip.h"   // AMF: wrapped tooltips (2.2.2)\n', 1)
    target = TARGET
    if "--out" in sys.argv:             # a dry run somewhere else (the compiled copy is left alone)
        target = os.path.abspath(sys.argv[sys.argv.index("--out") + 1])
    os.makedirs(os.path.dirname(target), exist_ok=True)
    io.open(target, "w", encoding="utf-8", newline="\n").write(text)

    total = sum(counted.values())
    print(f"generated {target}")
    print(f"  {touched} of the functions gained a guard, {total} guards in total:")
    print(f"     self pointer      {counted['self']}")
    print(f"     pOut out-struct   {counted['pOut']}")
    print(f"     label/str_id/fmt  {counted['string']}  (substituted with \"\", not returned)")
    print(f"     value out-params  {counted['outparam']}  (p_* names excluded from this shape)")
    print(f"     range begin first {counted.get('range-begin', 0)}  (returned - a NULL begin with the caller's end is no range)")
    print(f"   2.3.0 - every other pointer Dear ImGui reads (imgui.h defaults + tools/imgui-null-ok.json decide):")
    for k, label in (("pointer", "pointer -> return"), ("fnptr", "function pointer"), ("string-default", "string -> imgui.h default"),
                     ("string-empty", 'string -> ""'), ("string-end", "range end -> begin + strlen")):
        print(f"     {label:<30}{counted.get(k, 0)}")
    print(f"  {noted} text-field exports note their item; {len(wrapped)} header exports get the stock widening:")
    print("     " + ", ".join(wrapped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
