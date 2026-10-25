#!/usr/bin/env python3
"""
null_check_audit.py - list every unchecked pointer use in Apocrypha Menu Framework (Skyrim), per function.

    py -3 tools/null_check_audit.py [--repo <AMF repo>] [--build <dir>] [--out <dir>] [--gate] [--baseline <json>]

    Needs LLVM's libclang.dll (LIBCLANG_PATH, else C:\\Program Files\\LLVM\\bin) and the Python bindings
    clang==20.1.5, and a configured build tree (build\\<preset>\\build.ninja) for the defines and include paths.
    The repo defaults to the one this file is in; the reports go to build\\null-check-audit\\ (git-ignored).
    tools/null_check_gate.py is the package gate's wrapper around it.

WHAT IT MEASURES (the definition of "null-checked" the AMF Nexus page rests on)
    A "pointer use" is any of:
      arrow      p->member, p->method()               (raw pointer, smart pointer, iterator, optional)
      star       *p  (including  auto& r = *p)
      index      p[i] on a raw pointer
      fncall     calling through a function pointer   (a consumer callback, a GetProcAddress result,
                                                       a hook trampoline)
      callable   calling an empty-able std::function
      string     handing a const char* to strlen/strcmp/... or to a std::string / string_view
                 constructor, which reads through it
    A use is CHECKED when the same expression is proven non-null on every path to it:
      - it sits inside  if (p) / if (auto* p = ...) / while (p) / p ? use : ... / p && use /
        !p || use / if (!p) {...} else { use }    (also != nullptr, != end(), has_value())
      - or an earlier statement in an enclosing block is  if (!p ...) return|continue|break|throw;
        or  if (!p) p = <fallback>;
      - or the expression is non-null by construction: this, &x, new, a string literal, an array,
        .c_str() / .data(), or a local initialised from one of those and never reassigned
      - or the line (or the declaration of the variable/field it names) carries the annotation
        // nonnull: <reason>        - a written, reviewable claim that it cannot be null
      - or the callee is listed in nonnull-contracts.json (with its reason)
    Anything else is UNCHECKED and is listed with file:line, function, kind and origin class:
      game        RE:: / SKSE:: / REL:: / Scaleform (GFx) objects, engine singletons, forms, menus
      other-mod   arguments of an exported entry point, consumer callbacks, GetProcAddress results,
                  FLICK / Prisma / SMF / OAR API structures
      d3d-win32   Direct3D / DXGI interfaces, HWND / HMODULE / HANDLE, Windows SDK return values
      internal    AMF's own objects, containers, lookups (find() iterators, optionals)

    Exported entry points get a second report: every pointer PARAMETER of every dllexport function
    (cimgui's 1,397 plus AMF's own) is CHECKED, NULLABLE-BY-API (passed only where Dear ImGui's own
    declaration gives that parameter a NULL default, so NULL is a legal value), UNUSED, or UNCHECKED.

LIMITS (stated so a zero is not over-read)
    - Path-insensitive to reassignment: a check followed by  p = Other();  then  p->x  is counted
      as checked. The fix pass is expected to read each function anyway.
    - A check performed by a helper is recognised only when the helper's name says so
      (Valid / Ok / Ready / Alive / NonNull / IsNull).
    - Null is the only fault it measures. A dangling (freed) pointer, a bad code address, or a
      wrong-type cast is not a null and no null check stops it (past-mistakes library entry 10).

Exit code: 0, or 1 with --gate when any UNCHECKED use or UNCHECKED export parameter remains
(or, with --baseline, when the count rose above the baseline's).
"""
import argparse
import ctypes
import gc
import json
import os
import re
import sys
import time
from collections import Counter, defaultdict

import clang.cindex as ci

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_REPO = os.path.dirname(HERE)
# LIBCLANG_PATH, when set, is the only candidate (so a missing library can be shown to fail the gate).
LIBCLANG_CANDIDATES = ([os.environ["LIBCLANG_PATH"]] if os.environ.get("LIBCLANG_PATH")
                       else [r"C:\Program Files\LLVM\bin\libclang.dll"])

# Files we own. Vendored third-party code is out of scope (it is not ours to claim about),
# cimgui.cpp is in scope but is measured by the export report (its derefs are its parameters).
VENDORED = re.compile(r"[\\/](nanosvg|OAR)[\\/]|[\\/]cimgui[\\/]imgui[\\/]|cimgui\.h$", re.I)
CIMGUI_CPP = re.compile(r"[\\/]cimgui[\\/]cimgui\.cpp$", re.I)

K = ci.CursorKind
FUNC_KINDS = {K.FUNCTION_DECL, K.CXX_METHOD, K.CONSTRUCTOR, K.DESTRUCTOR, K.CONVERSION_FUNCTION,
              K.FUNCTION_TEMPLATE}
NULL_KINDS = {K.CXX_NULL_PTR_LITERAL_EXPR, K.GNU_NULL_EXPR}
EXIT_KINDS = {K.RETURN_STMT, K.CONTINUE_STMT, K.BREAK_STMT, K.GOTO_STMT, K.CXX_THROW_EXPR}
NORETURN_CALLS = re.compile(r"^(report_and_fail|abort|terminate|exit|_exit|ExitProcess|TerminateProcess|"
                            r"__fastfail|stl_assert|unreachable)$")
HELPER_CHECK = re.compile(r"(valid|cstring|readable|ok$|ready|alive|nonnull|isnull|notnull|present|loaded|available)", re.I)
STRING_FUNCS = re.compile(r"^(strlen|strnlen|strcmp|strncmp|_stricmp|_strnicmp|stricmp|strcpy|strncpy|strcpy_s|"
                          r"strncpy_s|strcat|strcat_s|strstr|strchr|strrchr|wcslen|wcscmp|_wcsicmp|wcscpy_s|"
                          r"atoi|atof|strtol|strtoul|strtod|strtof|sscanf|sscanf_s|MultiByteToWideChar)$")

SDK_PATH = re.compile(r"Windows Kits", re.I)
GAME_TYPE = re.compile(r"\b(RE|SKSE|REL)::|\bGFx|\bGFX|Scaleform")
D3D_TYPE = re.compile(r"\b(ID3D\w*|IDXGI\w*|IUnknown|IWIC\w*|IFileDialog\w*|IShell\w*|HWND__|HINSTANCE__|"
                      r"HMODULE|HKEY__|HMONITOR__|_IMAGE_\w+|HANDLE|HWND|HINSTANCE|LPCWSTR|LPWSTR|LPSTR|LPCSTR)\b")
OTHERMOD_FILE = re.compile(r"(Flick|Prisma|SKSEMenuFramework|AMF[\\/]API|OpenAnimationReplacer|DevBenchAPI|"
                           r"McmBridge|Smf)", re.I)
OTHERMOD_NAME = re.compile(r"(GetProcAddress|GetMenuFrameworkFunction|Request\w*Interface|RequestFUCK)")
TRAMPOLINE_NAME = re.compile(r"^(_|orig|original|func$|_original|g_original|s_original)", re.I)
NONNULL_INIT_CALLS = re.compile(r"\.(c_str|data|begin|end)\(\)$|std::addressof|std::make_unique|std::make_shared")


def load_libclang():
    for p in LIBCLANG_CANDIDATES:
        if p and os.path.isfile(p):
            ci.Config.set_library_file(p)
            return p
    raise SystemExit("libclang.dll not found - install LLVM (winget install LLVM.LLVM) or set LIBCLANG_PATH")


def read_ninja_flags(build_dir):
    """DEFINES and INCLUDES of the ApocryphaMenuFramework target, from the repo's own build.ninja."""
    path = os.path.join(build_dir, "build.ninja")
    defines, incs = [], []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            s = line.strip()
            if s.startswith("DEFINES =") and not defines:
                defines = re.findall(r"-D(\S+)", s)
            elif s.startswith("FLAGS =") and "AMF_FLAGS_SEEN" not in defines:
                defines += [d for d in re.findall(r"(?<!\S)-D(\S+)", s) if d not in defines]
                defines.append("AMF_FLAGS_SEEN")
            elif s.startswith("INCLUDES =") and not incs:
                incs = re.findall(r'-(?:external:)?I"([^"]+)"', s)
    return [d for d in defines if d != "AMF_FLAGS_SEEN"], incs


class Source:
    """Cached file text, for slicing expression text and reading annotations."""
    def __init__(self):
        self.cache = {}

    def text(self, path):
        if path not in self.cache:
            try:
                with open(path, "rb") as f:
                    self.cache[path] = f.read().decode("utf-8", errors="replace")
            except OSError:
                self.cache[path] = ""
        return self.cache[path]

    def raw(self, path):
        key = ("raw", path)
        if key not in self.cache:
            try:
                with open(path, "rb") as f:
                    self.cache[key] = f.read()
            except OSError:
                self.cache[key] = b""
        return self.cache[key]

    def between(self, path, start, end):
        """Text between two libclang BYTE offsets (a decoded-text slice drifts after any multibyte UTF-8)."""
        return self.raw(path)[start:end].decode("utf-8", errors="replace") if path else ""

    def lines(self, path):
        key = ("lines", path)
        if key not in self.cache:
            self.cache[key] = self.text(path).split("\n")
        return self.cache[key]

    def slice(self, cur):
        e = cur.extent
        if not e.start.file or not e.end.file or e.start.file.name != e.end.file.name:
            return ""
        # offsets are byte offsets: slice the bytes, then decode (2.3.0: decoded-text slicing misread UTF-8 files)
        return self.between(e.start.file.name, e.start.offset, e.end.offset)

    def annotated(self, path, line):
        ls = self.lines(path)
        if 1 <= line <= len(ls):
            if "nonnull:" in ls[line - 1]:
                return True
            if line >= 2 and re.match(r"\s*//.*nonnull:", ls[line - 2]):
                return True
        return False


SRC = Source()


def strip(c):
    """Peel implicit casts / parens / unexposed wrappers that have exactly one child."""
    seen = 0
    while c is not None and c.kind in (K.UNEXPOSED_EXPR, K.PAREN_EXPR, K.CSTYLE_CAST_EXPR,
                                       K.CXX_STATIC_CAST_EXPR, K.CXX_FUNCTIONAL_CAST_EXPR,
                                       K.CXX_CONST_CAST_EXPR, K.CXX_REINTERPRET_CAST_EXPR,
                                       K.CXX_DYNAMIC_CAST_EXPR) and seen < 20:
        ch = [x for x in c.get_children() if x.kind != K.TYPE_REF and x.kind != K.TEMPLATE_REF
              and x.kind != K.NAMESPACE_REF]
        if len(ch) != 1:
            break
        c = ch[0]
        seen += 1
    return c


def kids(c):
    return list(c.get_children())


def key(c):
    c = strip(c)
    if c is None:
        return ""
    if c.kind == K.DECL_REF_EXPR:
        r = c.referenced
        return "v%s" % r.hash if r is not None else c.spelling
    if c.kind == K.MEMBER_REF_EXPR:
        ch = kids(c)
        base = key(ch[0]) if ch else "this"
        return base + "." + (c.spelling or "")
    if c.kind == K.CXX_THIS_EXPR:
        return "this"
    if c.kind == K.CALL_EXPR and (c.spelling in ("operator->", "operator*", "get", "operator!") or
                                  (c.spelling or "").startswith("operator bool") or
                                  ((c.spelling or "").startswith("operator ") and (c.spelling or "").endswith("*"))):
        ch = kids(c)
        if ch:
            f = strip(ch[0])
            if f is not None and f.kind == K.MEMBER_REF_EXPR:
                mk = kids(f)
                return key(mk[0]) if mk else "this"
            return key(ch[0])
    if c.kind == K.CALL_EXPR:
        ch = kids(c)
        r = c.referenced
        head = ("f%s" % r.hash) if r is not None else c.spelling
        if ch and strip(ch[0]) is not None and strip(ch[0]).kind == K.MEMBER_REF_EXPR:
            mk = kids(strip(ch[0]))
            head = (key(mk[0]) if mk else "this") + "." + head
            args = ch[1:]
        else:
            args = [x for x in ch if not (strip(x) is not None and strip(x).kind == K.DECL_REF_EXPR
                                          and strip(x).referenced is not None and r is not None
                                          and strip(x).referenced == r)]
        return "c:" + head + "(" + ",".join(key(x) for x in args) + ")"
    if c.kind == K.UNARY_OPERATOR:
        ch = kids(c)
        return "u" + unop(c) + "(" + (key(ch[0]) if ch else "") + ")"
    t = re.sub(r"\s+", "", SRC.slice(c))
    if not t:
        t = "%s#%s" % (c.kind.name, c.hash)   # collapsed macro extent: unique, never matches by accident
    return "t:" + t


def is_null_literal(c, other=None):
    c = strip(c)
    if c is None:
        return False
    if c.kind in NULL_KINDS:
        return True
    if c.kind == K.INTEGER_LITERAL:
        s = SRC.slice(c).strip()
        if s in ("0", "NULL"):
            return True
        # inside a macro the text is unavailable; an integer literal compared with a pointer can only be 0
        if not s and other is not None and strip(other) is not None and \
                pointer_like(strip(other).type) in ("raw", "fnptr"):
            return True
    return False


def is_end_like(c):
    c = strip(c)
    if c is None:
        return False
    if c.kind == K.CALL_EXPR and c.spelling in ("end", "cend", "rend", "crend"):
        return True
    if c.kind in (K.DECL_REF_EXPR, K.MEMBER_REF_EXPR) and c.spelling == "npos":
        return True
    return False


def binop(c):
    """Return the operator spelling of a BINARY_OPERATOR cursor."""
    try:
        bo = c.binary_operator
        name = bo.name
        return {"LAnd": "&&", "LOr": "||", "EQ": "==", "NE": "!=", "Assign": "=", "LT": "<", "GE": ">=",
                "GT": ">", "LE": "<="}.get(name, name)
    except Exception:
        ch = kids(c)
        if len(ch) != 2:
            return ""
        f = c.extent.start.file.name if c.extent.start.file else ""
        mid = SRC.between(f, ch[0].extent.end.offset, ch[1].extent.start.offset).strip()
        return mid


def _bare(t):
    return re.sub(r"\b(const|volatile)\b|\s+", "", t.get_canonical().spelling)


def unop(c):
    """Operator of a UNARY_OPERATOR cursor; from the types when inside a macro (no usable text)."""
    s = SRC.slice(c).lstrip()
    if s[:1] in ("!", "*", "&", "-", "~", "+"):
        return {"!": "!", "*": "*", "&": "&"}.get(s[:1], s[:2])
    ch = kids(c)
    if not ch:
        return "?"
    sub = strip(ch[0])
    ct = c.type.get_canonical()
    st = sub.type.get_canonical() if sub is not None else None
    if st is not None and st.kind == ci.TypeKind.POINTER and _bare(st.get_pointee()) == _bare(ct):
        return "*"
    if ct.kind == ci.TypeKind.POINTER and st is not None and _bare(ct.get_pointee()) == _bare(st):
        return "&"
    if ct.kind == ci.TypeKind.BOOL:
        return "!"
    return "?"


def com_out_keys(call):
    """Keys of the out-pointers (&K, IID_PPV_ARGS(&K), ComPtr operator&) handed to an HRESULT call."""
    out = set()
    for n in call.walk_preorder():
        if n.kind == K.UNARY_OPERATOR and unop(n) == "&":
            ch = kids(n)
            if ch:
                out.add(key(ch[0]))
        elif n.kind == K.CALL_EXPR and n.spelling in ("operator&", "GetAddressOf", "ReleaseAndGetAddressOf"):
            ch = kids(n)
            if ch:
                sub = strip(ch[0])
                if sub is not None and sub.kind == K.MEMBER_REF_EXPR:
                    mk = kids(sub)
                    if mk:
                        out.add(key(mk[0]))
                else:
                    out.add(key(ch[0]))
    return out


def hresult_test(c):
    """For  (HRESULT)(call) < 0  (FAILED) return ('failed', call); for  >= 0  (SUCCEEDED) ('succeeded', call)."""
    if c.kind != K.BINARY_OPERATOR:
        return None, None
    op = binop(c)
    ch = kids(c)
    if len(ch) != 2 or strip(ch[1]) is None or strip(ch[1]).kind != K.INTEGER_LITERAL:
        return None, None
    lhs = strip(ch[0])
    if lhs is None or "HRESULT" not in (ch[0].type.spelling + lhs.type.spelling) and \
            lhs.type.get_canonical().spelling != "long":
        return None, None
    if lhs.kind != K.CALL_EXPR:
        return None, None
    if op in ("<", "LT"):
        return "failed", lhs
    if op in (">=", "GE"):
        return "succeeded", lhs
    return None, None


def operands(call):
    """Operands of an overloaded operator call, without the callee reference libclang lists among them."""
    out = []
    for x in kids(call):
        sx = strip(x)
        if sx is not None and sx.kind == K.DECL_REF_EXPR and (sx.spelling or "").startswith("operator"):
            continue
        out.append(x)
    return out


def pos_tests(c):
    """Keys known non-null when expression c is TRUE."""
    c = strip(c)
    if c is None:
        return set()
    kind_, call = hresult_test(c)
    if kind_ == "succeeded":
        return com_out_keys(call)
    if c.kind == K.VAR_DECL:
        return {"v%s" % c.hash}
    if c.kind == K.BINARY_OPERATOR:
        op = binop(c)
        ch = kids(c)
        if len(ch) == 2:
            a, b = ch
            if op == "&&":
                return pos_tests(a) | pos_tests(b)
            if op == "||":
                return pos_tests(a) & pos_tests(b)
            if op == "!=":
                if is_null_literal(b, a) or is_end_like(b):
                    return {key(a)}
                if is_null_literal(a, b) or is_end_like(a):
                    return {key(b)}
            return set()
    if c.kind == K.UNARY_OPERATOR and unop(c) == "!":
        ch = kids(c)
        return neg_tests(ch[0]) if ch else set()
    if c.kind == K.CALL_EXPR:
        name = c.spelling or ""
        ch = kids(c)
        if name in ("has_value", "operator bool") and ch:
            return {key(ch[0])} | {key(c)}
        if name in ("operator!=",) and len(operands(c)) == 2:
            a, b = operands(c)
            if is_end_like(b) or is_null_literal(b):
                return {key(a)}
            if is_end_like(a) or is_null_literal(a):
                return {key(b)}
        if HELPER_CHECK.search(name) and ch:
            out = set()
            for x in ch[1:] if ch and strip(ch[0]).kind in (K.DECL_REF_EXPR, K.MEMBER_REF_EXPR) and \
                    strip(ch[0]).spelling == name else ch:
                out.add(key(x))
            return out | {key(c)}
    # Any other expression used as a condition: the expression itself is non-null when true.
    return {key(c)}


def neg_tests(c):
    """Keys known non-null when expression c is FALSE."""
    c = strip(c)
    if c is None:
        return set()
    kind_, call = hresult_test(c)
    if kind_ == "failed":
        return com_out_keys(call)
    if c.kind == K.UNARY_OPERATOR and unop(c) == "!":
        ch = kids(c)
        return pos_tests(ch[0]) if ch else set()
    if c.kind == K.BINARY_OPERATOR:
        op = binop(c)
        ch = kids(c)
        if len(ch) == 2:
            a, b = ch
            if op == "||":
                return neg_tests(a) | neg_tests(b)
            if op == "&&":
                return neg_tests(a) & neg_tests(b)
            if op == "==":
                if is_null_literal(b, a) or is_end_like(b):
                    return {key(a)}
                if is_null_literal(a, b) or is_end_like(a):
                    return {key(b)}
    if c.kind == K.CALL_EXPR:
        name = c.spelling or ""
        ch = kids(c)
        if name == "operator==" and len(operands(c)) == 2:
            a, b = operands(c)
            if is_end_like(b) or is_null_literal(b):
                return {key(a)}
            if is_end_like(a) or is_null_literal(a):
                return {key(b)}
        if name in ("empty",) and ch:
            return set()
    return set()


def stmt_exits(s):
    """True when statement s always leaves the enclosing block (return/continue/break/throw/noreturn)."""
    s = strip(s) if s.kind != K.COMPOUND_STMT else s
    if s is None:
        return False
    if s.kind in EXIT_KINDS:
        return True
    if s.kind == K.COMPOUND_STMT:
        ch = kids(s)
        return bool(ch) and stmt_exits(ch[-1])
    if s.kind == K.CALL_EXPR and NORETURN_CALLS.match(s.spelling or ""):
        return True
    if s.kind == K.IF_STMT:
        parts = if_parts(s)
        return parts["else"] is not None and stmt_exits(parts["then"]) and stmt_exits(parts["else"])
    return False


def assigns_keys(s, keys):
    """Keys from `keys` that statement s assigns (the  if (!p) p = fallback;  substitution form)."""
    out = set()
    stack = [s]
    while stack:
        n = stack.pop()
        if n.kind == K.BINARY_OPERATOR and binop(n) == "=":
            ch = kids(n)
            if ch:
                kk = key(ch[0])
                if kk in keys:
                    out.add(kk)
        stack.extend(kids(n))
    return out


_IF_CACHE = {}


def if_parts(s):
    h = s.hash
    if h in _IF_CACHE:
        return _IF_CACHE[h]
    ch = kids(s)
    then_ = else_ = None
    conds = []
    if len(ch) >= 2:
        # an else exists when the text between the 2nd-last child's end and the last child's start is 'else'
        f = s.extent.start.file.name if s.extent.start.file else ""
        between = SRC.between(f, ch[-2].extent.end.offset, ch[-1].extent.start.offset)
        if re.search(r"\belse\b", between) and len(ch) >= 3:
            then_, else_, conds = ch[-2], ch[-1], ch[:-2]
        else:
            then_, conds = ch[-1], ch[:-1]
    elif ch:
        then_ = ch[-1]
    # an init-statement (DECL_STMT) is not a test; a bare VAR_DECL is the condition variable
    conds = [c for c in conds if c.kind != K.DECL_STMT]
    r = {"conds": conds, "then": then_, "else": else_}
    _IF_CACHE[h] = r
    return r


def contains(outer, inner):
    a, b = outer.extent, inner.extent
    if not a.start.file or not b.start.file or a.start.file.name != b.start.file.name:
        return False
    return a.start.offset <= b.start.offset and b.end.offset <= a.end.offset


class FuncCtx:
    def __init__(self, fn, exported, file):
        self.fn = fn
        self.exported = exported
        self.file = file
        self.assigned = set()       # keys assigned anywhere in the function
        self.nonnull_locals = set()  # locals initialised non-null and never reassigned
        self.param_keys = {}
        self.seh_ranges = []


def guarded(node, k, parents):
    """Is key k proven non-null at `node`, given its ancestor chain (innermost last)?"""
    chain = parents + [node]
    for i in range(len(chain) - 1, 0, -1):
        child, par = chain[i], chain[i - 1]
        pk = par.kind
        if pk == K.IF_STMT:
            p = if_parts(par)
            if p["then"] is not None and child == p["then"]:
                if any(k in pos_tests(c) for c in p["conds"]):
                    return True
            if p["else"] is not None and child == p["else"]:
                if any(k in neg_tests(c) for c in p["conds"]):
                    return True
        elif pk in (K.WHILE_STMT, K.FOR_STMT):
            ch = kids(par)
            body = ch[-1] if ch else None
            if body is not None and child == body:
                for c in ch[:-1]:
                    if c.kind != K.DECL_STMT and k in pos_tests(c):
                        return True
        elif pk == K.CONDITIONAL_OPERATOR:
            ch = kids(par)
            if len(ch) == 3:
                if child == ch[1] and k in pos_tests(ch[0]):
                    return True
                if child == ch[2] and k in neg_tests(ch[0]):
                    return True
        elif pk == K.BINARY_OPERATOR:
            op = binop(par)
            ch = kids(par)
            if len(ch) == 2 and child == ch[1]:
                if op == "&&" and k in pos_tests(ch[0]):
                    return True
                if op == "||" and k in neg_tests(ch[0]):
                    return True
        elif pk == K.COMPOUND_STMT:
            # early exits / substitutions in earlier sibling statements
            for s in par.get_children():
                if s == child:
                    break
                if s.kind == K.IF_STMT:
                    p = if_parts(s)
                    negs = set()
                    for c in p["conds"]:
                        negs |= neg_tests(c)
                    if k in negs and p["else"] is None and p["then"] is not None:
                        if stmt_exits(p["then"]) or k in assigns_keys(p["then"], {k}):
                            return True
        elif pk in FUNC_KINDS:
            break
    return False


def pointer_like(t):
    """Classify a type: 'raw', 'fnptr', 'smart', 'iter', 'optional', 'callable' or None."""
    if t is None:
        return None
    ct = t.get_canonical()
    if ct.kind == ci.TypeKind.POINTER:
        pt = ct.get_pointee()
        if pt.kind in (ci.TypeKind.FUNCTIONPROTO, ci.TypeKind.FUNCTIONNOPROTO):
            return "fnptr"
        return "raw"
    if ct.kind == ci.TypeKind.MEMBERPOINTER:
        return None
    s = ct.spelling
    if re.search(r"(unique_ptr|shared_ptr|weak_ptr|NiPointer|BSTSmartPointer|GPtr|ComPtr|hkRefPtr|"
                 r"BSTSharedPtr|NiTPointer)<", s):
        return "smart"
    if re.search(r"std::(__\d+::)?optional<", s):
        return "optional"
    # the type itself, not a template merely holding one: amf::Call's amf::detail::Caller<std::function<...>>
    # tests the callee before calling it (2.3.0)
    if re.match(r"(const )?(volatile )?std::(__\d+::)?function<", s):
        return "callable"
    if re.search(r"(_Tree_|_List_|_Hash|_Vector_|_String_)\w*iterator|::iterator\b|::const_iterator\b", s):
        return "iter"
    return None


def decl_file(c):
    try:
        d = c.get_definition() or c
        return d.location.file.name if d.location.file else ""
    except Exception:
        return ""


def callee_of(call):
    r = call.referenced
    return r


def classify(base, ctx, kind):
    """Origin class of the pointer expression `base`."""
    b = strip(base)
    # the root call or variable initialiser decides first
    origin_call = None
    if b is not None and b.kind == K.CALL_EXPR:
        origin_call = b
    elif b is not None and b.kind == K.DECL_REF_EXPR and b.referenced is not None:
        ref = b.referenced
        if ref.kind == K.PARM_DECL and ctx.exported:
            return "other-mod"
        if ref.kind == K.VAR_DECL:
            for ch in ref.walk_preorder():
                if ch.kind == K.CALL_EXPR and ch != ref:
                    origin_call = ch
                    break
    elif b is not None and b.kind == K.MEMBER_REF_EXPR:
        fr = b.referenced
        if fr is not None:
            f = decl_file(fr)
            parent = fr.semantic_parent
            pname = parent.type.spelling if parent is not None and parent.type else ""
            if OTHERMOD_FILE.search(f) or OTHERMOD_FILE.search(pname):
                return "other-mod"
            if SDK_PATH.search(f):
                return "d3d-win32"
            if GAME_TYPE.search(pname) or GAME_TYPE.search(fr.type.spelling or ""):
                return "game"
    if origin_call is not None:
        r = origin_call.referenced
        name = origin_call.spelling or ""
        qn = ""
        f = ""
        if r is not None:
            f = decl_file(r)
            p = r.semantic_parent
            parts = []
            while p is not None and p.kind not in (K.TRANSLATION_UNIT,):
                if p.spelling:
                    parts.append(p.spelling)
                p = p.semantic_parent
            qn = "::".join(reversed(parts))
        if OTHERMOD_NAME.search(name) or OTHERMOD_FILE.search(f):
            return "other-mod"
        if SDK_PATH.search(f) or D3D_TYPE.search(qn):
            return "d3d-win32"
        if re.match(r"(RE|SKSE|REL)\b", qn) or GAME_TYPE.search(qn):
            return "game"
    if kind == "fncall":
        nm = b.spelling if b is not None else ""
        t = b.type.spelling if b is not None else ""
        if TRAMPOLINE_NAME.search(nm or "") and not OTHERMOD_FILE.search(t):
            return "game"
        if re.search(r"PFN_|WINAPI|__stdcall", t):
            return "d3d-win32"
        return "other-mod"
    tspell = b.type.get_canonical().spelling if b is not None else ""
    if GAME_TYPE.search(tspell):
        return "game"
    if D3D_TYPE.search(tspell) or D3D_TYPE.search(b.type.spelling if b is not None else ""):
        return "d3d-win32"
    try:
        pt = b.type.get_canonical().get_pointee()
        decl = pt.get_declaration()
        f = decl.location.file.name if decl is not None and decl.location.file else ""
        if SDK_PATH.search(f):
            return "d3d-win32"
        if OTHERMOD_FILE.search(f):
            return "other-mod"
    except Exception:
        pass
    return "internal"


def nonnull_by_construction(b, ctx):
    b = strip(b)
    if b is None:
        return True
    if b.kind in (K.CXX_THIS_EXPR, K.STRING_LITERAL, K.CXX_NEW_EXPR):
        return True
    if b.kind == K.UNARY_OPERATOR and unop(b) == "&":
        return True
    if b.kind == K.BINARY_OPERATOR and binop(b) in ("+", "-", "Add", "Sub"):
        ch = kids(b)
        for x in ch:
            if pointer_like(strip(x).type) == "raw":
                return nonnull_by_construction(x, ctx)
    if b.kind in (K.CALL_EXPR, K.ARRAY_SUBSCRIPT_EXPR) and (b.spelling or "") in ("operator[]", "at", "front", "back", "") \
            and annotated_root(b):
        return True
    if b.kind == K.MEMBER_REF_EXPR and b.referenced is not None and \
            any(p.search(qualified(b.referenced)) for p in CONTRACTS.fields):
        return True
    if b.kind == K.CONDITIONAL_OPERATOR:
        ch = kids(b)
        if len(ch) == 3:
            t_ok = nonnull_by_construction(ch[1], ctx) or key(ch[1]) in pos_tests(ch[0])
            f_ok = nonnull_by_construction(ch[2], ctx) or key(ch[2]) in neg_tests(ch[0])
            return t_ok and f_ok
    if b.kind == K.DECL_REF_EXPR:
        r = b.referenced
        if r is not None and r.kind == K.VAR_DECL and r.semantic_parent is not None and \
                r.semantic_parent.kind in (K.NAMESPACE, K.TRANSLATION_UNIT, K.CLASS_DECL, K.STRUCT_DECL) and \
                (r.type.get_canonical().is_const_qualified() or "constexpr" in SRC.slice(r)[:200]):
            init = [x for x in kids(r) if x.kind not in (K.TYPE_REF, K.NAMESPACE_REF, K.TEMPLATE_REF)]
            if init and nonnull_by_construction(init[-1], ctx):
                return True
        if r is not None:
            ct = r.type.get_canonical().kind
            if ct in (ci.TypeKind.CONSTANTARRAY, ci.TypeKind.INCOMPLETEARRAY, ci.TypeKind.FUNCTIONPROTO):
                return True
            if r.kind == K.FUNCTION_DECL or r.kind == K.CXX_METHOD:
                return True
            if "v%s" % r.hash in ctx.nonnull_locals:
                return True
            if r.location.file and SRC.annotated(r.location.file.name, r.location.line):
                return True
    if b.kind == K.MEMBER_REF_EXPR:
        r = b.referenced
        if r is not None and r.type.get_canonical().kind in (ci.TypeKind.CONSTANTARRAY,):
            return True
        if r is not None and r.location.file and SRC.annotated(r.location.file.name, r.location.line):
            return True
    if b.kind == K.CALL_EXPR:
        if (b.spelling or "") in ("c_str", "data", "begin", "end", "addressof", "make_unique", "make_shared",
                                  "operator new"):
            return True
        if b.referenced is not None and CONTRACTS.match(qualified(b.referenced), b):
            return True
    return False


class Contracts:
    def __init__(self, path):
        self.entries = []
        self.fields = []
        if os.path.isfile(path):
            with open(path, encoding="utf-8") as f:
                data = json.load(f)
            for e in data.get("nonnull", []):
                self.entries.append(re.compile(e["callee"]))
            for e in data.get("nonnull_fields", []):
                self.fields.append(re.compile(e["field"]))

    def match(self, qname, call):
        return any(p.search(qname) for p in self.entries)


CONTRACTS = Contracts(os.path.join(HERE, "nonnull-contracts.json"))


def collect_function_facts(fn, ctx):
    """Assigned keys and never-reassigned non-null locals."""
    inits = {}
    for n in fn.walk_preorder():
        if n.kind == K.BINARY_OPERATOR and binop(n) == "=":
            ch = kids(n)
            if ch:
                ctx.assigned.add(key(ch[0]))
        elif n.kind == K.VAR_DECL:
            ch = [x for x in kids(n) if x.kind not in (K.TYPE_REF, K.NAMESPACE_REF, K.TEMPLATE_REF)]
            if ch:
                inits["v%s" % n.hash] = ch[-1]
        elif n.kind == K.SEH_TRY_STMT:
            ch = kids(n)
            if ch:
                ctx.seh_ranges.append(ch[0].extent)
    for k_, init in inits.items():
        if k_ in ctx.assigned:
            continue
        if nonnull_by_construction(init, ctx):
            ctx.nonnull_locals.add(k_)


def in_seh(node, ctx):
    o = node.extent.start.offset
    return any(r.start.offset <= o <= r.end.offset for r in ctx.seh_ranges)


def is_exported(fn):
    for ch in fn.get_children():
        if ch.kind == K.DLLEXPORT_ATTR:
            return True
    return False


def owned(path, repo):
    p = os.path.normcase(os.path.abspath(path))
    r = os.path.normcase(os.path.abspath(repo))
    if not p.startswith(r):
        return False
    rel = p[len(r):]
    if not (rel.startswith(os.sep + "source") or rel.startswith(os.sep + "include")):
        return False
    return not VENDORED.search(p)


def walk_uses(fn, ctx, repo, out, seen):
    """Depth-first walk keeping the ancestor chain; record every pointer use."""
    stack = [(fn, [])]
    fname = fn.spelling
    while stack:
        node, parents = stack.pop()
        loc = node.location
        rec = None
        try:
            nk = node.kind
        except ValueError:
            continue
        if nk == K.LAMBDA_EXPR and parents:
            pass
        if nk == K.MEMBER_REF_EXPR:
            ch = kids(node)
            if ch:
                b = ch[0]
                sb = strip(b)
                bt = sb.type.get_canonical() if sb is not None else None
                is_arrow = (bt is not None and bt.kind == ci.TypeKind.POINTER) or \
                    (sb is not None and sb.kind == K.CALL_EXPR and sb.spelling == "operator->")
                if is_arrow:
                    if sb is not None and sb.kind == K.CALL_EXPR and sb.spelling == "operator->":
                        inner = kids(sb)
                        base = inner[0] if inner else sb
                        pl = pointer_like(strip(base).type) if inner else None
                        if pl in ("smart", "optional", "iter"):
                            rec = ("arrow", base, pl)
                    else:
                        pl = pointer_like(sb.type) if sb is not None else None
                        if pl in ("raw", "smart", "iter", "optional"):
                            rec = ("arrow", b, pl)
        elif nk == K.UNARY_OPERATOR and unop(node) == "*":
            ch = kids(node)
            if ch:
                pl = pointer_like(strip(ch[0]).type)
                if pl in ("raw", "fnptr"):
                    rec = ("star", ch[0], pl)
        elif nk == K.ARRAY_SUBSCRIPT_EXPR:
            ch = kids(node)
            if ch:
                pl = pointer_like(strip(ch[0]).type)
                if pl == "raw":
                    rec = ("index", ch[0], pl)
        elif nk == K.CALL_EXPR:
            ch = kids(node)
            name = node.spelling or ""
            if name == "operator*" and len(ch) == 1:
                pl = pointer_like(strip(ch[0]).type)
                if pl in ("smart", "iter", "optional"):
                    rec = ("star", ch[0], pl)
            elif name == "value" and ch:
                callee = strip(ch[0])
                if callee is not None and callee.kind == K.MEMBER_REF_EXPR:
                    obj = kids(callee)
                    if obj and pointer_like(strip(obj[0]).type) == "optional":
                        rec = ("star", obj[0], "optional")
            elif name == "operator()" and ch:
                if pointer_like(strip(ch[0]).type) == "callable":
                    rec = ("callable", ch[0], "callable")
            elif ch and node.referenced is not None and node.referenced.kind in (K.VAR_DECL, K.FIELD_DECL, K.PARM_DECL):
                callee = strip(ch[0])
                if callee is not None and pointer_like(callee.type) == "fnptr":
                    rec = ("fncall", callee, "fnptr")
            elif ch and STRING_FUNCS.match(name):
                for a in ch[1:]:
                    sa = strip(a)
                    if sa is not None and sa.kind not in (K.STRING_LITERAL,) and \
                            pointer_like(sa.type) == "raw" and "char" in sa.type.get_canonical().spelling:
                        rec = ("string", a, "raw")
                        break
            elif node.referenced is not None and node.referenced.kind == K.CONSTRUCTOR and \
                    re.search(r"basic_string(_view)?$", node.referenced.semantic_parent.spelling or "") and len(ch) == 1:
                sa = strip(ch[0])
                if sa is not None and sa.kind != K.STRING_LITERAL and pointer_like(sa.type) == "raw" and \
                        "char" in sa.type.get_canonical().spelling:
                    rec = ("string", ch[0], "raw")
        if nk == K.VAR_DECL:
            note_alias(node, ctx, parents)
        if nk == K.CXX_FOR_RANGE_STMT:
            ch = kids(node)
            if len(ch) >= 2 and ch[0].kind == K.VAR_DECL and annotated_root(ch[1]):
                ctx.nonnull_locals.add("v%s" % ch[0].hash)
        if rec is not None and implicit_or_unevaluated(rec[1], parents):
            rec = None
        if rec is not None and rec[2] == "iter" and not from_lookup(rec[1]):
            rec = None
        if rec is not None and loc.file and owned(loc.file.name, repo):
            kind, base, pl = rec
            k = key(base)
            sig = (loc.file.name, loc.line, loc.column, kind, k)
            if sig not in seen:
                seen.add(sig)
                if nonnull_by_construction(base, ctx):
                    status = "nonnull"
                elif SRC.annotated(loc.file.name, loc.line):
                    status = "annotated"
                elif guarded(node, k, parents):
                    status = "checked"
                else:
                    status = "UNCHECKED"
                out.append({
                    "file": os.path.relpath(loc.file.name, repo),
                    "line": loc.line, "col": loc.column,
                    "function": qualified(ctx.fn),
                    "kind": kind, "ptr": pl,
                    "expr": re.sub(r"\s+", " ", SRC.slice(strip(base)) or "")[:120],
                    "class": classify(base, ctx, kind),
                    "status": status,
                    "seh": in_seh(node, ctx),
                })
        for c in reversed(kids(node)):
            if c.kind in FUNC_KINDS and c != fn:
                continue
            stack.append((c, parents + [node]))


LOOKUP_CALLS = re.compile(r"^(find|find_if|find_if_not|lower_bound|upper_bound|equal_range|search|find_first_of|"
                          r"find_end|adjacent_find|max_element|min_element)$")


def from_lookup(base):
    """An iterator is a nullable lookup result only when a find-style call produced it."""
    b = strip(base)
    if b is None:
        return False
    if b.kind == K.CALL_EXPR:
        return bool(LOOKUP_CALLS.match(b.spelling or ""))
    if b.kind == K.DECL_REF_EXPR and b.referenced is not None and b.referenced.kind == K.VAR_DECL:
        for n in b.referenced.walk_preorder():
            if n.kind == K.CALL_EXPR and LOOKUP_CALLS.match(n.spelling or ""):
                return True
    return False


def annotated_root(expr):
    """The declaration an expression is rooted in (g_mods, a.b.c -> c's field) carries // nonnull:."""
    e = strip(expr)
    while e is not None:
        if e.kind in (K.DECL_REF_EXPR, K.MEMBER_REF_EXPR):
            r = e.referenced
            if r is not None and r.location.file and SRC.annotated(r.location.file.name, r.location.line):
                return True
            if e.kind == K.MEMBER_REF_EXPR:
                mk = kids(e)
                e = strip(mk[0]) if mk else None
                continue
            return False
        if e.kind == K.CALL_EXPR:
            ch = kids(e)
            if not ch:
                return False
            f = strip(ch[0])
            if f is not None and f.kind == K.MEMBER_REF_EXPR:   # x.front(), x.at(i)
                mk = kids(f)
                e = strip(mk[0]) if mk else None
            else:                                               # x[i] (operator[]: object is the first operand)
                ops = operands(e)
                e = strip(ops[0]) if ops else None
            continue
        if e.kind == K.ARRAY_SUBSCRIPT_EXPR:
            ch = kids(e)
            e = strip(ch[0]) if ch else None
            continue
        return False
    return False


def note_alias(var, ctx, parents):
    """auto* b = static_cast<T*>(a); with a already proven non-null here, and b never reassigned => b is non-null."""
    vk = "v%s" % var.hash
    if vk in ctx.assigned or vk in ctx.nonnull_locals:
        return
    init = [x for x in kids(var) if x.kind not in (K.TYPE_REF, K.NAMESPACE_REF, K.TEMPLATE_REF)]
    if not init or pointer_like(var.type) not in ("raw", "smart", "fnptr", "callable"):
        return
    src = strip(init[-1])
    if src is None or src.kind in (K.CALL_EXPR,) and src.spelling not in ("operator->", "get") and \
            not nonnull_by_construction(src, ctx):
        return
    k0 = key(src)
    if nonnull_by_construction(src, ctx) or guarded(var, k0, parents):
        ctx.nonnull_locals.add(vk)


def implicit_or_unevaluated(base, parents):
    """Compiler-generated derefs (range-for's *__begin) and operands that are never evaluated
    (sizeof, __uuidof inside IID_PPV_ARGS, decltype) are not pointer uses."""
    b = strip(base)
    if b is not None and b.kind == K.DECL_REF_EXPR and (b.spelling or "").startswith("__"):
        return True
    for a in parents[-8:]:
        if a.kind == K.CXX_UNARY_EXPR:      # sizeof / alignof
            return True
        if a.kind == K.UNEXPOSED_EXPR and "GUID" in (a.type.spelling or ""):
            return True
    return False


def qualified(fn):
    parts = []
    p = fn
    while p is not None and p.kind != K.TRANSLATION_UNIT:
        if p.spelling:
            parts.append(p.spelling)
        p = p.semantic_parent
    return "::".join(reversed(parts))


# ---------------------------------------------------------------- export parameters
def param_default_is_null(parm):
    s = SRC.slice(parm)
    return bool(re.search(r"=\s*(NULL|nullptr|0|0u|\(\w+\)0)\s*$", s))


def export_params(fn, ctx, repo):
    rows = []
    params = [c for c in fn.get_children() if c.kind == K.PARM_DECL]
    body = [c for c in fn.get_children() if c.kind == K.COMPOUND_STMT]
    if not body:
        return rows
    body = body[0]
    # collect uses of each param with their ancestor chain
    uses = defaultdict(list)
    stack = [(body, [fn])]
    while stack:
        n, par = stack.pop()
        if n.kind == K.DECL_REF_EXPR and n.referenced is not None and n.referenced.kind == K.PARM_DECL:
            uses[n.referenced.hash].append((n, par))
        for c in kids(n):
            stack.append((c, par + [n]))
    for p in params:
        pl = pointer_like(p.type)
        if pl not in ("raw", "fnptr") or "va_list" in p.type.spelling:
            continue
        k = "v%s" % p.hash
        plist = uses.get(p.hash, [])
        if not plist:
            rows.append(row_for(fn, p, "UNUSED", None, repo, pl))
            continue
        bad = None
        api_nullable = False
        forwards = []
        for n, par in plist:
            # a use that IS the null test itself (or the substitution p = fallback) is not a use
            if is_test_position(n, par) or is_assign_target(n, par):
                continue
            if guarded(n, k, par):
                continue
            how, call, idx = use_kind(n, par)
            if how == "deref":
                bad = n
                break
            if how != "arg":
                continue        # stored, compared, returned, cast for a log line: not a read through it
            if passed_to_null_default(n, par):
                api_nullable = True
                continue
            callee = call.referenced
            if callee is not None and LOG_CALLEE.search(qualified(callee)):
                continue
            # the definition may be in another file (not visible from this one): the declaration says whose it is
            cdef = callee.get_definition() if callee is not None else None
            cdecl = cdef or callee
            dfile = cdecl.location.file.name if cdecl is not None and cdecl.location.file else ""
            if callee is not None and dfile and (owned(dfile, repo) or CIMGUI_CPP.search(dfile)):
                forwards.append((callee.get_usr(), idx))   # resolved after every file is read
                continue
            bad = n
            break
        if bad is not None:
            r = row_for(fn, p, "UNCHECKED", bad, repo, pl)
        elif forwards:
            r = row_for(fn, p, "PENDING", None, repo, pl)
            r["forwards"] = forwards
        elif api_nullable:
            r = row_for(fn, p, "NULLABLE-BY-API", None, repo, pl)
        else:
            r = row_for(fn, p, "CHECKED", None, repo, pl)
        r["usr"] = fn.get_usr()
        r["index"] = params.index(p)
        rows.append(r)
    return rows


LOG_CALLEE = re.compile(r"^(spdlog|fmt|logger|std::format|std::vformat)\b|::(log|debug|info|warn|error|trace|critical)$")


def use_kind(n, par):
    """How a parameter reference is used: 'deref' (p->, *p, p[i], p()), 'arg' (handed to a call), or 'other'."""
    cur = n
    for pr in reversed(par):
        if pr.kind in (K.UNEXPOSED_EXPR, K.PAREN_EXPR, K.CSTYLE_CAST_EXPR, K.CXX_STATIC_CAST_EXPR,
                       K.CXX_REINTERPRET_CAST_EXPR, K.CXX_CONST_CAST_EXPR, K.CXX_FUNCTIONAL_CAST_EXPR):
            cur = pr
            continue
        if pr.kind == K.MEMBER_REF_EXPR:
            return ("deref", None, None) if kids(pr) and kids(pr)[0] == cur else ("other", None, None)
        if pr.kind == K.UNARY_OPERATOR and unop(pr) == "*":
            return ("deref", None, None)
        if pr.kind == K.ARRAY_SUBSCRIPT_EXPR:
            return ("deref", None, None) if kids(pr) and kids(pr)[0] == cur else ("other", None, None)
        if pr.kind == K.CALL_EXPR:
            ch = kids(pr)
            if ch and ch[0] == cur and pr.referenced is not None and \
                    pr.referenced.kind in (K.PARM_DECL, K.VAR_DECL, K.FIELD_DECL):
                return ("deref", None, None)        # calling through the function pointer
            if pr.referenced is not None and pr.referenced.kind == K.CONSTRUCTOR:
                # std::string(p) / string_view(p) read through it; other constructors just store it
                if re.search(r"basic_string(_view)?$", pr.referenced.semantic_parent.spelling or ""):
                    return ("deref", None, None)
                return ("other", None, None)
            pos = None
            for j, a in enumerate(ch):
                if a == cur:
                    pos = j
                    break
            if pos is None or pos == 0:
                return ("other", None, None)
            return ("arg", pr, pos - 1)
        return ("other", None, None)
    return ("other", None, None)


def resolve_forwards(rows_all):
    """PENDING parameters take the status of the parameters they are forwarded to (any function we define)."""
    by_key = {}
    for r in rows_all:
        by_key[(r.get("usr"), r.get("index"))] = r

    def status_of(key_, depth):
        r = by_key.get(key_)
        if r is None:
            return "UNCHECKED"        # target not analysed (no body in source\, or a template parameter): not proven
        if r["status"] != "PENDING":
            return r["status"]
        if depth > 6:
            return "UNCHECKED"
        sts = [status_of(f, depth + 1) for f in r.get("forwards", [])]
        return "UNCHECKED" if "UNCHECKED" in sts else "CHECKED"

    for r in rows_all:
        if r["status"] == "PENDING":
            st = status_of((r["usr"], r["index"]), 0)
            r["status"] = st
            r["via"] = "forwarded"


def is_assign_target(n, par):
    """The DECL_REF is the left side of  p = ...  (the substitution half of a guard), not a use."""
    if not par:
        return False
    pr = par[-1]
    if pr.kind == K.BINARY_OPERATOR and binop(pr) == "=":
        ch = kids(pr)
        return bool(ch) and ch[0] == n
    return False


def is_test_position(n, par):
    """The DECL_REF is (part of) a null test: if (!p), p == nullptr, p ? :, p && ..."""
    chain = par + [n]
    for i in range(len(chain) - 1, 0, -1):
        c, pr = chain[i], chain[i - 1]
        if pr.kind == K.UNARY_OPERATOR and unop(pr) == "!":
            return True
        if pr.kind == K.BINARY_OPERATOR and binop(pr) in ("==", "!=", "&&", "||"):
            ch = kids(pr)
            if binop(pr) in ("&&", "||"):
                return True
            if len(ch) == 2 and (is_null_literal(ch[0], ch[1]) or is_null_literal(ch[1], ch[0])):
                return True
            return False
        if pr.kind in (K.IF_STMT, K.WHILE_STMT):
            p = if_parts(pr) if pr.kind == K.IF_STMT else {"conds": kids(pr)[:-1]}
            return any(c == x for x in p["conds"])
        if pr.kind == K.CONDITIONAL_OPERATOR:
            ch = kids(pr)
            return bool(ch) and c == ch[0]
        if pr.kind in (K.UNEXPOSED_EXPR, K.PAREN_EXPR):
            continue
        return False
    return False


# Dear ImGui parameters that take NULL although imgui.h gives them no default: one list, tools/imgui-null-ok.json, read
# here and by tools/harden-cimgui.py (which leaves exactly these unguarded), so the audit and the generator cannot disagree.
def _load_null_ok():
    with open(os.path.join(HERE, "imgui-null-ok.json"), encoding="utf-8") as f:
        d = json.load(f)
    return {e["param"] for e in d["names"]}, {(e["callee"], e["param"]) for e in d["params"]}


IMGUI_NULL_OK, IMGUI_NULL_OK_AT = _load_null_ok()


def passed_to_null_default(n, par):
    """Is this use a direct argument to a call whose parameter at that slot defaults to NULL?"""
    chain = par + [n]
    arg = n
    for i in range(len(chain) - 2, -1, -1):
        pr = chain[i]
        if pr.kind in (K.UNEXPOSED_EXPR, K.PAREN_EXPR, K.CSTYLE_CAST_EXPR, K.CXX_STATIC_CAST_EXPR,
                       K.CXX_REINTERPRET_CAST_EXPR, K.CXX_CONST_CAST_EXPR):
            arg = pr
            continue
        if pr.kind == K.CALL_EXPR:
            callee = pr.referenced
            if callee is None:
                return False
            # libclang's get_arguments() cursors do not compare equal to get_children() ones; use positions
            ch = kids(pr)
            pos = None
            for j, a in enumerate(ch):
                if a == arg:
                    pos = j
                    break
            if pos is None or pos == 0:
                return False
            idx = pos - 1
            cparams = [c for c in callee.get_children() if c.kind == K.PARM_DECL]
            if idx < len(cparams) and param_default_is_null(cparams[idx]):
                return True
            # Dear ImGui parameters that take NULL although they have no default (read in imgui.cpp / imgui_widgets.cpp
            # 1.90.8; tools/imgui-null-ok.json gives the line for each). NOT every p_*: p_data is dereferenced by
            # DragScalar / SliderScalar / InputScalar, and Selectable(label, bool* p_selected) reads *p_selected.
            if idx < len(cparams):
                pn = cparams[idx].spelling or ""
                if pn in IMGUI_NULL_OK or (qualified(callee), pn) in IMGUI_NULL_OK_AT:
                    return True
            return False
        return False
    return False


def row_for(fn, p, status, use, repo, pl):
    loc = (use or p).location
    return {
        "file": os.path.relpath(fn.location.file.name, repo),
        "line": loc.line,
        "function": fn.spelling,
        "param": p.spelling,
        "type": p.type.spelling,
        "ptr": pl,
        "status": status,
    }


# ---------------------------------------------------------------- memory discipline
# The first full run kept every file's AST alive through cursor caches and reached ~15 GB (2026-10-25).
# Now: one TU at a time, caches cleared per file, only cursors in source\ / include\ are visited,
# the process stops itself above RSS_CAP_MB, and it waits before a file while the machine is short.
RSS_CAP_MB = int(os.environ.get("AMF_AUDIT_RSS_CAP_MB", "2048"))
# The free-memory floor and how long the run may wait for it, in total. A manual run waits up to an hour for 6 GB
# (--free-floor-mb / --max-wait-s, or AMF_AUDIT_FREE_FLOOR_MB). The package gate's wrapper (tools/null_check_gate.py)
# passes about 2.5 GB and 2 minutes: the audit peaks near 1.15 GB, and a gate that waited an hour stalled a
# pre-finalize run for 33 minutes (2026-10-25). When the wait runs out the audit exits 4 and the gate FAILS.
FREE_FLOOR_MB = int(os.environ.get("AMF_AUDIT_FREE_FLOOR_MB", "6144"))
MAX_WAIT_S = 3600
_waited_s = 0


class _PMC(ctypes.Structure):
    _fields_ = [("cb", ctypes.c_ulong), ("PageFaultCount", ctypes.c_ulong), ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t), ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t), ("PrivateUsage", ctypes.c_size_t)]


class _MSX(ctypes.Structure):
    _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong), ("ullTotalPhys", ctypes.c_ulonglong),
                ("ullAvailPhys", ctypes.c_ulonglong), ("ullTotalPageFile", ctypes.c_ulonglong),
                ("ullAvailPageFile", ctypes.c_ulonglong), ("ullTotalVirtual", ctypes.c_ulonglong),
                ("ullAvailVirtual", ctypes.c_ulonglong), ("ullAvailExtendedVirtual", ctypes.c_ulonglong)]


def rss_mb():
    try:
        pmc = _PMC()
        pmc.cb = ctypes.sizeof(_PMC)
        k32 = ctypes.windll.kernel32
        k32.GetCurrentProcess.restype = ctypes.c_void_p
        fn = k32.K32GetProcessMemoryInfo
        fn.argtypes = [ctypes.c_void_p, ctypes.POINTER(_PMC), ctypes.c_ulong]
        fn.restype = ctypes.c_int
        if not fn(k32.GetCurrentProcess(), ctypes.byref(pmc), pmc.cb):
            return 0
        return int(max(pmc.WorkingSetSize, pmc.PrivateUsage) / (1024 * 1024))
    except Exception:
        return 0


def free_mb():
    try:
        m = _MSX()
        m.dwLength = ctypes.sizeof(_MSX)
        ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m))
        return int(m.ullAvailPhys / (1024 * 1024))
    except Exception:
        return 1 << 30


def check_rss():
    r = rss_mb()
    if r > RSS_CAP_MB:
        print("STOPPED: the audit's own memory reached %d MB (cap %d MB) - nothing was written for this run" % (r, RSS_CAP_MB),
              flush=True)
        sys.exit(3)


def wait_for_memory():
    """Wait while free memory is under FREE_FLOOR_MB; MAX_WAIT_S is the budget for the WHOLE run, not per file."""
    global _waited_s
    while free_mb() < FREE_FLOOR_MB:
        if _waited_s >= MAX_WAIT_S:
            print("STOPPED: not enough free memory to run the null-check audit (%d MB free, floor %d MB, waited %d s); "
                  "rerun when memory frees" % (free_mb(), FREE_FLOOR_MB, _waited_s), flush=True)
            sys.exit(4)
        if _waited_s % 60 == 0:
            print("  waiting: %d MB free, floor %d MB (waited %d of %d s)" % (free_mb(), FREE_FLOOR_MB, _waited_s, MAX_WAIT_S),
                  flush=True)
        step = min(10, MAX_WAIT_S - _waited_s)
        time.sleep(step)
        _waited_s += step


CONTAINER_KINDS = {K.NAMESPACE, K.CLASS_DECL, K.STRUCT_DECL, K.CLASS_TEMPLATE, K.LINKAGE_SPEC, K.UNEXPOSED_DECL,
                   K.UNION_DECL, K.CLASS_TEMPLATE_PARTIAL_SPECIALIZATION}


def owned_functions(root, repo):
    """Function definitions located in source\\ or include\\ (and cimgui.cpp); system, SDK, vcpkg and extern
    headers are never descended into."""
    stack = list(root.get_children())
    while stack:
        c = stack.pop()
        f = c.location.file
        if not f:
            continue
        name = f.name
        if not (owned(name, repo) or CIMGUI_CPP.search(name)):
            continue
        if c.kind in FUNC_KINDS:
            if c.is_definition():
                yield c
            continue
        if c.kind in CONTAINER_KINDS:
            stack.extend(c.get_children())


# ---------------------------------------------------------------- driver
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=DEFAULT_REPO)
    ap.add_argument("--build", default=None, help="build dir holding build.ninja (default: newest under build\\)")
    ap.add_argument("--out", default=None, help="report folder (default: <repo>\\build\\null-check-audit)")
    ap.add_argument("--gate", action="store_true")
    ap.add_argument("--baseline", default=None)
    ap.add_argument("--only", default=None, help="regex of source files to parse (debugging)")
    ap.add_argument("--free-floor-mb", type=int, default=None,
                    help="wait while free RAM is under this (default AMF_AUDIT_FREE_FLOOR_MB, else 6144)")
    ap.add_argument("--max-wait-s", type=int, default=None,
                    help="total seconds the run may wait for memory before it stops with exit 4 (default 3600)")
    a = ap.parse_args()
    global FREE_FLOOR_MB, MAX_WAIT_S
    if a.free_floor_mb is not None:
        FREE_FLOOR_MB = a.free_floor_mb
    if a.max_wait_s is not None:
        MAX_WAIT_S = max(0, a.max_wait_s)

    lib = load_libclang()
    repo = os.path.abspath(a.repo)
    if a.out is None:
        a.out = os.path.join(repo, "build", "null-check-audit")
    build = a.build
    if build is None:
        cands = []
        bdir = os.path.join(repo, "build")
        for d in os.listdir(bdir) if os.path.isdir(bdir) else []:
            p = os.path.join(bdir, d, "build.ninja")
            if os.path.isfile(p):
                cands.append((os.path.getmtime(p), os.path.join(bdir, d)))
        if not cands:
            raise SystemExit("no configured build dir with build.ninja under %s\\build" % repo)
        build = sorted(cands)[-1][1]
    defines, incs = read_ninja_flags(build)
    args = ["-x", "c++", "--target=x86_64-pc-windows-msvc", "-std=c++2c", "-fms-compatibility",
            "-fms-extensions", "-Wno-everything", "-DNDEBUG"]
    args += ["-D" + d for d in defines]
    for i in incs:
        if os.path.isdir(i):
            args += ["-I" + i]
    os.makedirs(a.out, exist_ok=True)
    idx = ci.Index.create()
    t0 = time.time()
    pch_src = os.path.join(repo, "include", "PCH.h")
    pch_ast = os.path.join(os.environ.get("TEMP", a.out), "amf-null-audit-pch-%d.ast" % os.getpid())   # ~130 MB, removed below
    wait_for_memory()   # before the PCH too, so a short machine is caught before the first parse
    tu = idx.parse(pch_src, args=args)
    errs = [d for d in tu.diagnostics if d.severity >= 3]
    if errs:
        print("PCH parse errors:", *errs[:5], sep="\n  ")
    tu.save(pch_ast)

    sources = []
    for root, _, files in os.walk(os.path.join(repo, "source")):
        for fn in files:
            if fn.endswith(".cpp"):
                sources.append(os.path.join(root, fn))
    if a.only:
        sources = [s for s in sources if re.search(a.only, s)]
    del tu
    idx = None
    gc.collect()
    uses, exports, funcs, all_params = [], [], {}, []
    seen = set()
    parse_errors = {}
    for src in sorted(sources):
        wait_for_memory()
        check_rss()
        # one Index and one TranslationUnit per file; nothing that holds a cursor outlives the file
        idx = ci.Index.create()
        tu = idx.parse(src, args=args + ["-include-pch", pch_ast],
                       options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES * 0)
        e = [str(d) for d in tu.diagnostics if d.severity >= 3]
        if e:
            parse_errors[os.path.relpath(src, repo)] = e[:5]
        for c in owned_functions(tu.cursor, repo):
            check_rss()
            fid = (c.location.file.name, c.location.line, c.spelling)
            if fid in funcs:
                continue
            exported = is_exported(c)
            ctx = FuncCtx(c, exported, c.location.file.name)
            funcs[fid] = {"file": os.path.relpath(c.location.file.name, repo), "line": c.location.line,
                          "function": qualified(c), "exported": exported,
                          "cimgui": bool(CIMGUI_CPP.search(c.location.file.name))}
            prow = export_params(c, ctx, repo)
            for r in prow:
                r["exported"] = exported
            all_params.extend(prow)
            if CIMGUI_CPP.search(c.location.file.name):
                continue
            collect_function_facts(c, ctx)
            walk_uses(c, ctx, repo, uses, seen)
            ctx = None
        # free the file's AST: drop every cache that holds cursors, then the TU and the Index
        _IF_CACHE.clear()
        c = None
        del tu
        idx = None
        gc.collect()
        with open(os.path.join(a.out, "progress.log"), "a", encoding="utf-8") as pf:
            pf.write("%s %.1fs uses=%d params=%d rss=%dMB\n" % (os.path.relpath(src, repo), time.time() - t0,
                                                                len(uses), len(all_params), rss_mb()))
        print("  %-40s %6.1fs  uses=%d params=%d rss=%dMB" % (os.path.relpath(src, repo), time.time() - t0,
                                                              len(uses), len(all_params), rss_mb()), flush=True)

    try:
        os.remove(pch_ast)
    except OSError:
        pass
    resolve_forwards(all_params)
    exports = [r for r in all_params if r.get("exported")]
    unchecked = [u for u in uses if u["status"] == "UNCHECKED"]
    exp_unchecked = [e for e in exports if e["status"] == "UNCHECKED"]
    by_class = Counter(u["class"] for u in unchecked)
    by_file = Counter(u["file"] for u in unchecked)
    by_kind = Counter(u["kind"] for u in unchecked)
    by_status = Counter(u["status"] for u in uses)
    fn_with_unchecked = len({(u["file"], u["function"]) for u in unchecked})
    fn_with_uses = len({(u["file"], u["function"]) for u in uses})
    exp_by_status = Counter(e["status"] for e in exports)
    exp_fns = [f for f in funcs.values() if f["exported"]]
    exp_fns_unchecked = len({(e["file"], e["function"]) for e in exp_unchecked})
    summary = {
        "generated": time.strftime("%Y-%m-%d %H:%M"),
        "repo": repo, "build_flags_from": build, "libclang": lib,
        "functions_defined": len(funcs),
        "functions_defined_non_cimgui": sum(1 for f in funcs.values() if not f["cimgui"]),
        "functions_with_pointer_uses": fn_with_uses,
        "functions_with_unchecked_uses": fn_with_unchecked,
        "pointer_uses": len(uses),
        "uses_by_status": dict(by_status),
        "unchecked_by_class": dict(by_class),
        "unchecked_by_kind": dict(by_kind),
        "unchecked_by_file": dict(by_file.most_common()),
        "unchecked_in_seh": sum(1 for u in unchecked if u["seh"]),
        "exported_functions": len(exp_fns),
        "exported_cimgui": sum(1 for f in exp_fns if f["cimgui"]),
        "export_pointer_params": len(exports),
        "export_params_by_status": dict(exp_by_status),
        "exported_functions_with_unchecked_param": exp_fns_unchecked,
        "parse_errors": parse_errors,
        "total_unchecked": len(unchecked) + len(exp_unchecked),
    }
    with open(os.path.join(a.out, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)
    with open(os.path.join(a.out, "uses.json"), "w", encoding="utf-8") as f:
        json.dump(uses, f, indent=1)
    with open(os.path.join(a.out, "exports.json"), "w", encoding="utf-8") as f:
        json.dump(exports, f, indent=1)
    with open(os.path.join(a.out, "params-all.json"), "w", encoding="utf-8") as f:
        json.dump(all_params, f, indent=1)
    with open(os.path.join(a.out, "unchecked.txt"), "w", encoding="utf-8") as f:
        for u in sorted(unchecked, key=lambda u: (u["class"], u["file"], u["line"])):
            f.write("%-10s %s:%d  %-8s %-40s  %s%s\n" % (u["class"], u["file"], u["line"], u["kind"],
                                                       u["function"][-40:], u["expr"],
                                                       "  [SEH]" if u["seh"] else ""))
        f.write("\n# exported entry points - unchecked pointer parameters\n")
        for e in sorted(exp_unchecked, key=lambda e: (e["file"], e["line"])):
            f.write("export     %s:%d  %s(%s %s)\n" % (e["file"], e["line"], e["function"], e["type"], e["param"]))
    print(json.dumps({k: v for k, v in summary.items() if k not in ("unchecked_by_file", "parse_errors")}, indent=1))
    if parse_errors:
        print("parse errors in %d files (see summary.json)" % len(parse_errors))
    if a.gate:
        total = summary["total_unchecked"]
        if a.baseline and os.path.isfile(a.baseline):
            with open(a.baseline, encoding="utf-8") as f:
                base = json.load(f).get("total_unchecked", 0)
            if total > base:
                print("GATE FAIL: unchecked pointer uses rose from %d to %d" % (base, total))
                return 1
            print("GATE PASS (baseline %d, now %d)" % (base, total))
            return 0
        if total:
            print("GATE FAIL: %d unchecked pointer uses / export parameters" % total)
            return 1
        print("GATE PASS: zero unchecked")
    return 0


if __name__ == "__main__":
    sys.exit(main())
