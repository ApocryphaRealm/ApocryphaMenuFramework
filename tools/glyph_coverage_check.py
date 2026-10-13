"""Runs tools\\glyph_coverage_check.bat (the off-game glyph coverage check, 2.2.0) and passes its exit code on.

package-gate.ps1's repo-script-passes rows run a Python script; this is the bridge to the compiled check, which builds real
ImGui 1.90.8 atlases with include\\GlyphCore.h for all eleven languages and fails on any character without a glyph.
"""
import os
import subprocess
import sys

here = os.path.dirname(os.path.abspath(__file__))
proc = subprocess.run(["cmd", "/c", os.path.join(here, "glyph_coverage_check.bat")], capture_output=True, text=True,
                      encoding="utf-8", errors="replace")
sys.stdout.write(proc.stdout)
sys.stderr.write(proc.stderr)
sys.exit(proc.returncode)
