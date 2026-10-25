#!/usr/bin/env python3
"""
null_check_audit_test.py - proves tools/null_check_audit.py still detects what it must, after the 2.3.0 changes.

    py -3 tools/null_check_audit_test.py        (exit 0 = all cases pass)

Builds a tiny fixture repo in a temp folder (include/PCH.h, source/fixture.cpp, a build.ninja holding only the
include path) and runs the audit on it. Each case is a function whose expected status is fixed:

  unguarded std::function call         -> UNCHECKED  (callable detection is kept)
  amf::Call(fn)() on a std::function   -> not a read (Caller<std::function<...>> is not itself a std::function;
                                                      its operator() tests the callee first)
  if (!p) {return} else {*p}           -> checked    (the else is found after multibyte UTF-8 text in the file -
                                                      libclang offsets are bytes, the old slice was of decoded text)
  bare *p after the same UTF-8 text    -> UNCHECKED  (a read is still found there)
  p->x inside if (p)                   -> checked
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
AUDIT = os.path.join(HERE, "null_check_audit.py")

PCH = "#pragma once\n#include <functional>\n"

FIXTURE = r'''#include "PCH.h"
// Multibyte UTF-8 before the code under test: é ü ö — “quotes” — 日本語 (each shifts byte offsets from text offsets)
// ÄÖÜ äöü ß ñ ç ø å é è ê ë – — … ™ © ®
namespace amf::detail
{
	template <class F>
	struct Caller
	{
		const F& fn;
		template <class... A>
		void operator()(A&&... a) const
		{
			if (!fn) { return; }
			fn(static_cast<A&&>(a)...);
		}
	};
}
namespace amf
{
	template <class F>
	detail::Caller<F> Call(const F& f) { return detail::Caller<F>{ f }; }
}
struct S { int x; };

void case_raw_function(const std::function<void()>& fn) { fn(); }
void case_amf_call(const std::function<void()>& fn) { amf::Call(fn)(); }
int case_else_guard(int* p)
{
	if (!p) { return 0; }
	else { return *p; }
}
int case_bare_after_utf8(int* p) { return *p; }
int case_if_guard(S* p)
{
	if (p) { return p->x; }
	return 0;
}
'''

EXPECT = {
    # function -> (expected UNCHECKED count, expected checked count or None to skip)
    "case_raw_function": (1, None),
    "case_amf_call": (0, None),
    "case_else_guard": (0, 1),
    "case_bare_after_utf8": (1, None),
    "case_if_guard": (0, 1),
}


def main():
    tmp = tempfile.mkdtemp(prefix="amf-null-audit-test-")
    try:
        for d in ("include", "source", os.path.join("build", "x")):
            os.makedirs(os.path.join(tmp, d))
        with open(os.path.join(tmp, "include", "PCH.h"), "w", encoding="utf-8") as f:
            f.write(PCH)
        with open(os.path.join(tmp, "source", "fixture.cpp"), "w", encoding="utf-8", newline="\n") as f:
            f.write(FIXTURE)
        with open(os.path.join(tmp, "build", "x", "build.ninja"), "w", encoding="utf-8") as f:
            f.write('DEFINES = -DAMF_TEST\nINCLUDES = -I"%s"\n' % os.path.join(tmp, "include"))
        out = os.path.join(tmp, "out")
        r = subprocess.run([sys.executable, AUDIT, "--repo", tmp, "--out", out, "--free-floor-mb", "1024",
                            "--max-wait-s", "60"], capture_output=True, text=True)
        if r.returncode not in (0, 1):
            print(r.stdout[-2000:], r.stderr[-2000:])
            print("FAIL: audit exited %d" % r.returncode)
            return 1
        with open(os.path.join(out, "uses.json"), encoding="utf-8") as f:
            uses = json.load(f)
        rows = uses if isinstance(uses, list) else uses.get("uses", [])
        fails = 0
        for fn, (want_un, want_ck) in EXPECT.items():
            mine = [u for u in rows if str(u.get("function", "")).endswith(fn)]
            un = sum(1 for u in mine if u.get("status") == "UNCHECKED")
            ck = sum(1 for u in mine if u.get("status") == "checked")
            ok = un == want_un and (want_ck is None or ck == want_ck)
            fails += not ok
            print("%s  %-22s unchecked %d (want %d)  checked %d%s" % ("ok  " if ok else "FAIL", fn, un, want_un, ck,
                  "" if want_ck is None else " (want %d)" % want_ck))
        print("PASS: %d of %d cases" % (len(EXPECT), len(EXPECT)) if not fails else
              "FAIL: %d of %d cases" % (fails, len(EXPECT)))
        return 1 if fails else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
