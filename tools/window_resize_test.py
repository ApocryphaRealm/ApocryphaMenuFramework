r"""In-game check of the framework window's resizing and moving (2.1.1). The owner, 2026-10-05: "Last I checked, I
couldn't resize the vertical length of the window, but I could resize the width ... you'll have to check."

Drives DevBench (amf.menu): opens the menu, then for each case presses the left button just inside an edge or corner
(op=mouse down), walks the cursor outward in steps (op=cursor), releases (op=mouse up), and reads the window's real rect
from op=state ("mainWindow"). Prints one line per case: the size before and after, and whether it changed the way it
should. Run with the game at a loaded save and nothing else driving it.

    python tools/window_resize_test.py [--settings]   (--settings: also flip Resize freely / Move the window from the page)
"""
import json
import sys
import time
import urllib.request

URL = "http://127.0.0.1:8920/api/tool/amf.menu"


def q(body):
    r = urllib.request.Request(URL, json.dumps(body).encode(), {"Content-Type": "application/json"})
    return json.loads(urllib.request.urlopen(r, timeout=30).read())


def rect():
    d = json.loads(q({"op": "state"})["raw"], strict=False)
    w = d["mainWindow"]
    return w["pos"][0], w["pos"][1], w["size"][0], w["size"][1]


def drag(x, y, dx, dy, steps=12):
    q({"op": "cursor", "x": x, "y": y})
    time.sleep(0.35)                                  # hovered for a frame first, as a real mouse would be
    q({"op": "mouse", "button": 0, "down": True})
    time.sleep(0.15)
    for i in range(1, steps + 1):
        q({"op": "cursor", "x": x + dx * i / steps, "y": y + dy * i / steps})
        time.sleep(0.04)
    time.sleep(0.15)
    q({"op": "mouse", "button": 0, "down": False})
    time.sleep(0.5)


def case(name, grab, dx, dy, expect):
    x0, y0, w0, h0 = rect()
    gx, gy = grab(x0, y0, w0, h0)
    drag(gx, gy, dx, dy)
    x1, y1, w1, h1 = rect()
    dw, dh = w1 - w0, h1 - h0
    ok = expect(dw, dh, x1 - x0, y1 - y0)
    print(f"{'PASS' if ok else 'FAIL'}  {name:34s} size {w0}x{h0} -> {w1}x{h1}  (dw {dw:+d}, dh {dh:+d}; moved {x1 - x0:+d},{y1 - y0:+d})")
    return ok


q({"op": "open"})
time.sleep(1.0)
q({"op": "select", "node": "settings"})
time.sleep(0.8)
inset = 3   # just inside the border: ImGui's edge zone straddles it
results = [
    case("bottom edge down (height)", lambda x, y, w, h: (x + w / 2, y + h - inset), 0, 120, lambda dw, dh, mx, my: dh > 40 and abs(dw) < 5),
    case("top edge up (height)", lambda x, y, w, h: (x + w / 2, y + inset), 0, -120, lambda dw, dh, mx, my: dh > 40 and abs(dw) < 5),
    case("right edge out (width)", lambda x, y, w, h: (x + w - inset, y + h / 2), 120, 0, lambda dw, dh, mx, my: dw > 40 and abs(dh) < 5),
    case("left edge out (width)", lambda x, y, w, h: (x + inset, y + h / 2), -120, 0, lambda dw, dh, mx, my: dw > 40 and abs(dh) < 5),
    case("bottom-right corner straight down", lambda x, y, w, h: (x + w - inset, y + h - inset), 0, 120, lambda dw, dh, mx, my: dh > 40),
    case("bottom-right corner diagonal", lambda x, y, w, h: (x + w - inset, y + h - inset), 120, 60, lambda dw, dh, mx, my: dw > 40 and dh > 20),
]
print(f"{sum(results)} of {len(results)} passed")
