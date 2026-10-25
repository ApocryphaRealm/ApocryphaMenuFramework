#!/usr/bin/env python3
"""
null_check_gate.py - the package gate's null-check row: every pointer AMF reads is null-checked.

    python tools/null_check_gate.py            (no arguments; exit 0 = PASS, 1 = FAIL)

Runs tools/null_check_audit.py on the repo this file is in, with --gate, and with
--baseline tools/null-check-baseline.json while that file exists: the count of unchecked pointer reads
and unchecked exported pointer parameters may only fall. Once the count reaches zero the baseline file is
removed and any unchecked read fails.

FAILS CLOSED. A gate that cannot measure must not pass, so it prints FAIL and exits 1 when:
  - the clang Python bindings (clang==20.1.5) cannot be imported - after first re-running itself under
    `py -3`, because the package gate calls plain `python`, which may be a different interpreter;
  - libclang.dll is missing or will not load (LIBCLANG_PATH, else C:\\Program Files\\LLVM\\bin\\libclang.dll);
  - the build tree it reads defines and include paths from (build\\relwithdebinfo-17\\build.ninja, made by
    configure17.bat) is not configured;
  - the audit stops itself (memory cap, or an hour without enough free memory), reports parse errors
    (a file it could not read is a file it did not count), or ends without a GATE PASS line.

The build tree is pinned to the 1.7 line's because the baseline was measured with its flags; the two lines
compile the same sources.
"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
AUDIT = os.path.join(HERE, "null_check_audit.py")
BASELINE = os.path.join(HERE, "null-check-baseline.json")
BUILD = os.path.join(REPO, "build", "relwithdebinfo-17")
OUT = os.path.join(REPO, "build", "null-check-audit")
CLANG_VERSION = "20.1.5"
REEXEC = "AMF_NULL_GATE_REEXEC"


def fail(msg):
    print("FAIL: null-check gate: " + msg, flush=True)
    return 1


def reexec_under_py3(why):
    """Run this script again under the Python launcher's py -3, once."""
    if os.environ.get(REEXEC):
        return None
    py = shutil.which("py")
    if not py:
        return None
    print("null-check gate: %s under %s - re-running under py -3" % (why, sys.executable), flush=True)
    env = dict(os.environ)
    env[REEXEC] = "1"
    return subprocess.run([py, "-3", os.path.abspath(__file__)], env=env).returncode


def bindings_problem():
    try:
        import clang.cindex  # noqa: F401
    except Exception as e:
        return "the clang Python bindings are not importable (%s: %s)" % (type(e).__name__, e)
    try:
        from importlib.metadata import version
        v = version("clang")
    except Exception:
        v = None
    if v != CLANG_VERSION:
        return "the clang Python bindings are %s, the audit needs clang==%s" % (v or "of unknown version", CLANG_VERSION)
    return None


def libclang_problem():
    import clang.cindex as ci
    cands = ([os.environ["LIBCLANG_PATH"]] if os.environ.get("LIBCLANG_PATH")
             else [r"C:\Program Files\LLVM\bin\libclang.dll"])
    lib = next((p for p in cands if p and os.path.isfile(p)), None)
    if not lib:
        return "libclang.dll not found (looked at %s) - install LLVM or set LIBCLANG_PATH" % ", ".join(cands)
    try:
        ci.Config.set_library_file(lib)
        ci.Index.create()
    except Exception as e:
        return "libclang.dll at %s will not load (%s: %s)" % (lib, type(e).__name__, e)
    return None


def main():
    p = bindings_problem()
    if p:
        rc = reexec_under_py3(p)
        if rc is not None:
            return 0 if rc == 0 else 1
        return fail(p + " - pip install clang==%s for the python that runs the gate" % CLANG_VERSION)
    p = libclang_problem()
    if p:
        return fail(p)
    if not os.path.isfile(AUDIT):
        return fail("tools/null_check_audit.py is missing")
    if not os.path.isfile(os.path.join(BUILD, "build.ninja")):
        return fail("no configured build tree at build\\relwithdebinfo-17 (build.ninja missing) - run configure17.bat")

    cmd = [sys.executable, AUDIT, "--repo", REPO, "--build", BUILD, "--out", OUT, "--gate"]
    if os.path.isfile(BASELINE):
        try:
            with open(BASELINE, encoding="utf-8") as f:
                base = int(json.load(f)["total_unchecked"])
        except Exception as e:
            return fail("tools/null-check-baseline.json does not hold a total_unchecked number (%s)" % e)
        cmd += ["--baseline", BASELINE]
        print("null-check gate: baseline %d unchecked (tools/null-check-baseline.json); the count may only fall" % base,
              flush=True)
    else:
        base = None
        print("null-check gate: no baseline file - any unchecked pointer read fails", flush=True)

    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    out = proc.stdout or ""
    tail = [l for l in out.splitlines() if l.strip()][-6:]
    for l in tail:
        print("  " + l)
    gate_line = next((l for l in reversed(out.splitlines()) if l.startswith("GATE ")), "")
    if proc.returncode != 0:
        if gate_line.startswith("GATE FAIL"):
            return fail(gate_line[len("GATE FAIL: "):] + " - the new reads are in build\\null-check-audit\\unchecked.txt")
        return fail("the audit exited %d without a verdict: %s" % (proc.returncode, tail[-1] if tail else "(no output)"))
    try:
        with open(os.path.join(OUT, "summary.json"), encoding="utf-8") as f:
            summary = json.load(f)
    except Exception as e:
        return fail("the audit wrote no readable summary.json (%s)" % e)
    if summary.get("parse_errors"):
        return fail("libclang could not parse %d source file(s): %s - an unread file is an uncounted file"
                    % (len(summary["parse_errors"]), ", ".join(sorted(summary["parse_errors"]))[:300]))
    if not gate_line.startswith("GATE PASS"):
        return fail("the audit ended without a GATE PASS line")
    total = summary.get("total_unchecked")
    if base is not None and isinstance(total, int) and total < base:
        print("null-check gate: the count fell from %d to %d - lower total_unchecked in tools/null-check-baseline.json"
              % (base, total), flush=True)
    print("PASS: null-check gate: %s unchecked pointer reads%s" % (total, "" if base is None else " (baseline %d)" % base),
          flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
