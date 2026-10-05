r"""Watch the framework window's real rect while the OWNER resizes and moves it by hand (2.1.1). Read-only: it sends no
input - the owner, 2026-10-05: "Whenever you launch the game, I'll be there to try and resize it, and you can take the
readings."

Polls DevBench op=state four times a second and prints a line whenever the window's position or size changes, with
what changed (width, height, both, moved); on Ctrl+C (or after --seconds) prints the ranges reached. A height change
that never shows while the owner drags a top or bottom edge is the fault he saw.

    python tools/window_watch.py [--seconds 300] [--log <file>]
"""
import json
import sys
import time
import urllib.request

URL = "http://127.0.0.1:8920/api/tool/amf.menu"
seconds = float(sys.argv[sys.argv.index("--seconds") + 1]) if "--seconds" in sys.argv else 300.0
log = open(sys.argv[sys.argv.index("--log") + 1], "a", encoding="utf-8") if "--log" in sys.argv else None


def out(line):
    print(line, flush=True)
    if log:
        log.write(line + "\n")
        log.flush()


def state():
    r = urllib.request.Request(URL, json.dumps({"op": "state"}).encode(), {"Content-Type": "application/json"})
    d = json.loads(json.loads(urllib.request.urlopen(r, timeout=10).read())["raw"], strict=False)
    w = d.get("mainWindow", {"pos": [0, 0], "size": [0, 0]})
    return d.get("visible"), tuple(w["pos"]), tuple(w["size"]), d.get("cursor", {})


last = None
seen_w, seen_h = set(), set()
end = time.time() + seconds
out(f"watching the framework window for {seconds:.0f}s - resize and move it by hand")
try:
    while time.time() < end:
        try:
            vis, pos, size, cur = state()
        except Exception as e:   # the game busy or loading: wait and go on
            time.sleep(1.0)
            continue
        if vis and size[0] > 0:
            seen_w.add(size[0]); seen_h.add(size[1])
            if last and (pos, size) != last:
                (px, py), (pw, ph) = last
                what = []
                if size[0] != pw: what.append(f"width {pw}->{size[0]}")
                if size[1] != ph: what.append(f"height {ph}->{size[1]}")
                if pos != (px, py) and not what: what.append(f"moved to {pos}")
                out(time.strftime("%H:%M:%S") + "  " + ", ".join(what) + f"   (pos {pos}, cursor {cur.get('x')},{cur.get('y')})")
            last = (pos, size)
        time.sleep(0.25)
except KeyboardInterrupt:
    pass
if seen_w:
    out(f"width ranged {min(seen_w)}..{max(seen_w)}, height ranged {min(seen_h)}..{max(seen_h)}"
        + ("  <- HEIGHT NEVER CHANGED" if len(seen_h) == 1 else ""))
else:
    out("the window was never seen open")
