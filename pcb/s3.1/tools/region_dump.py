#!/usr/bin/env python3
"""Dump all copper objects (segments, vias, pads) inside a bbox, both layers.
Usage: region_dump.py x1 y1 x2 y2 [pcbfile]"""
import re, sys, math
from collections import defaultdict

x1, y1, x2, y2 = map(float, sys.argv[1:5])
fn = sys.argv[5] if len(sys.argv) > 5 else "s3.1.kicad_pcb"
PT = open(fn).read()

def blocks(text, head):
    out = []
    for m in re.finditer(re.escape(head), text):
        d, i = 0, m.start()
        while i < len(text):
            if text[i] == '(': d += 1
            elif text[i] == ')':
                d -= 1
                if d == 0:
                    out.append((m.start(), i + 1, text[m.start():i + 1]))
                    break
            i += 1
    return out

def inbox(x, y):
    return x1 - 0.6 <= x <= x2 + 0.6 and y1 - 0.6 <= y <= y2 + 0.6

def seg_inbox(a, b, c, d):
    # rough: either endpoint in box, or segment crosses box centerlines
    if inbox(a, b) or inbox(c, d):
        return True
    if min(a, c) <= x2 and max(a, c) >= x1 and min(b, d) <= y2 and max(b, d) >= y1:
        # segment bbox overlaps -> check line-box intersect (cheap: sample)
        for t in [i / 20 for i in range(21)]:
            if inbox(a + t * (c - a), b + t * (d - b)):
                return True
    return False

segs = []
for s, e, blk in blocks(PT, "(segment"):
    nmm = re.search(r'\(net "([^"]+)"\)', blk)
    st = re.search(r'\(start ([-\d.]+) ([-\d.]+)\)', blk)
    en = re.search(r'\(end ([-\d.]+) ([-\d.]+)\)', blk)
    lay = re.search(r'\(layer "([^"]+)"\)', blk)
    wd = re.search(r'\(width ([-\d.]+)\)', blk)
    if nmm and st and en and lay:
        a, b, c, d = map(float, st.groups() + en.groups())
        if seg_inbox(a, b, c, d):
            segs.append((lay.group(1), nmm.group(1), a, b, c, d, float(wd.group(1)) if wd else 0.3))
for lay, net, a, b, c, d, w in sorted(segs):
    print(f"{lay[:1]} {net:20s} ({a:6.2f},{b:6.2f})->({c:6.2f},{d:6.2f}) w{w}")

for s, e, blk in blocks(PT, "(via"):
    nmm = re.search(r'\(net "([^"]+)"\)', blk)
    a = re.search(r'\(at ([-\d.]+) ([-\d.]+)\)', blk)
    if nmm and a and inbox(float(a.group(1)), float(a.group(2))):
        print(f"V {nmm.group(1):20s} ({float(a.group(1)):6.2f},{float(a.group(2)):6.2f})")

for s, e, blk in blocks(PT, "(footprint"):
    am = re.search(r'\n\t\t\(at ([-\d.]+) ([-\d.]+)( ([-\d.]+))?\)', blk)
    ref = re.search(r'\(property "Reference" "([^"]+)"', blk)
    if not am:
        continue
    fx, fy, rot = float(am.group(1)), float(am.group(2)), float(am.group(4) or 0)
    cr, sr = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    for pm in re.finditer(r'\(pad "([^"]+)" (smd|thru_hole)[^\n]*\n(?:.*\n){0,8}?\t\t\t\)', blk):
        pa = re.search(r'\(at ([-\d.]+) ([-\d.]+)', pm.group(0))
        nm = re.search(r'\(net "([^"]+)"\)', pm.group(0))
        if not (pa and nm):
            continue
        lx, ly = float(pa.group(1)), float(pa.group(2))
        ax = fx + lx * cr - ly * sr
        ay = fy + lx * sr + ly * cr
        if inbox(ax, ay):
            print(f"P {nm.group(1):20s} {ref.group(1) if ref else '?'}.{pm.group(1)} ({ax:6.2f},{ay:6.2f}) {pm.group(2)}")
