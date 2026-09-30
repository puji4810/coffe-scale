#!/usr/bin/env python3
"""Surgical route fixes on the merged s3.1.kicad_pcb.

Removes freerouting orphan fragments/vias that cause shorts, then adds
hand-planned segments+vias to close the remaining unconnected nets.
Run after autoroute merge; then `kicad-cli pcb drc --refill-zones`.
"""
import re, sys, uuid

PCB = "s3.1.kicad_pcb"
pt = open(PCB).read()

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

# ---------- 1. deletions ----------
DEL_SEGS = [  # (net, x1, y1, x2, y2) - CC2 orphan run crossing BUZZ/SDA/vias
    ("Net-(J5-CC2)", 48.85, 25.3, 47.6, 26.55),
    ("Net-(J5-CC2)", 47.6, 26.55, 47.6, 29.3),
    ("Net-(J5-CC2)", 47.6, 29.3, 47.78, 30.1),
    ("Net-(J5-CC2)", 47.78, 30.1, 47.78, 30.955),
]
DEL_VIAS = [  # (net, x, y)
    ("LCD_DC", 42.0, 11.0),     # orphan via -> U4.41 clearance violation
    ("GND", 47.5, 27.8),        # stitch via inside CC2/BUZZ/SDA corridor
    ("GND", 49.0, 26.0),        # ditto
]

def approx(a, b, t=0.02):
    return abs(a - b) < t

removed = []
for s, e, blk in blocks(pt, "(segment"):
    nm = re.search(r'\(net "([^"]+)"\)', blk).group(1)
    st = re.search(r'\(start ([-\d.]+) ([-\d.]+)\)', blk)
    en = re.search(r'\(end ([-\d.]+) ([-\d.]+)\)', blk)
    for net, x1, y1, x2, y2 in DEL_SEGS:
        if nm == net and all(approx(float(g), v) for g, v in
                             zip(st.groups() + en.groups(), (x1, y1, x2, y2))):
            removed.append((s, e, f"seg {net} {x1},{y1}-{x2},{y2}"))
            break
for s, e, blk in blocks(pt, "(via"):
    nmm = re.search(r'\(net "([^"]+)"\)', blk)
    a = re.search(r'\(at ([-\d.]+) ([-\d.]+)\)', blk)
    if not (nmm and a):
        continue
    nm = nmm.group(1)
    for net, x, y in DEL_VIAS:
        if nm == net and approx(float(a.group(1)), x) and approx(float(a.group(2)), y):
            removed.append((s, e, f"via {net} {x},{y}"))
            break

for s, e, desc in sorted(removed, reverse=True):
    pt = pt[:s] + pt[e:]
    print("DEL", desc)

# ---------- 2. additions ----------
def seg(net, layer, x1, y1, x2, y2, w=0.3):
    return (f'\t(segment\n\t\t(start {x1} {y1})\n\t\t(end {x2} {y2})\n'
            f'\t\t(width {w})\n\t\t(layer "{layer}")\n\t\t(net "{net}")\n'
            f'\t\t(uuid "{uuid.uuid4()}")\n\t)\n')

def via(net, x, y):
    return (f'\t(via\n\t\t(at {x} {y})\n\t\t(size 0.7)\n\t\t(drill 0.35)\n'
            f'\t\t(layers "F.Cu" "B.Cu")\n\t\t(free yes)\n\t\t(net "{net}")\n'
            f'\t\t(uuid "{uuid.uuid4()}")\n\t)\n')

adds = []

def chain(net, layer, *pts, w=0.3):
    for i in range(len(pts) - 1):
        adds.append(seg(net, layer, *pts[i], *pts[i + 1], w=w))

# CC2: bridge 0.12mm gap at J5 pad stub (orphan fragments deleted above)
chain("Net-(J5-CC2)", "F.Cu", (75.75, 44.9), (75.75, 45.1))

# SCL: TP13 stub -> F.Cu only (B.Cu is analog no-track zone x<27)
chain("SCL", "F.Cu", (17.72, 7.31), (16.5, 8.6), (16.5, 20.0),
      (19.7, 20.0), (19.7, 21.0), (21.2, 21.365))

# EN: U4 side F.Cu -> via -> B.Cu fragments joined (x27.36 stays E of keepout)
adds.append(via("EN", 28.25, 25.71))
chain("EN", "F.Cu", (28.25, 25.71), (28.25, 14.75), (33.5, 14.75))
chain("EN", "B.Cu", (27.36, 26.6), (27.36, 41.07))

# VCC_3V3: three island joins
adds.append(via("VCC_3V3", 28.05, 32.75))
chain("VCC_3V3", "B.Cu", (28.05, 32.75), (28.07, 39.92))
chain("VCC_3V3", "F.Cu", (8.03, 6.1), (8.03, 4.7), (31.9, 4.7), (31.9, 10))
chain("VCC_3V3", "F.Cu", (49.05, 28), (49.05, 29.3), (33.05, 29.3), (33.05, 29.7841))

# LCD_MOSI: stub -> B.Cu hop under U4 E-pads -> F.Cu -> J7.4 PTH pad
adds.append(via("LCD_MOSI", 39.92, 8.22))
chain("LCD_MOSI", "B.Cu", (39.92, 8.22), (48.0, 8.22), (49.0, 10.2), (55.5, 10.2))
adds.append(via("LCD_MOSI", 55.5, 10.2))
chain("LCD_MOSI", "F.Cu", (55.5, 10.2), (55.5, 7.3), (69.8, 7.3), (69.8, 4.0))

# EXP_IO39: U4 stub -> B.Cu x52.0/x47.0 lane -> F.Cu -> R25.1
adds.append(via("EXP_IO39", 52.4, 15.9))
chain("EXP_IO39", "F.Cu", (52.7046, 15.9), (52.4, 15.9))
chain("EXP_IO39", "B.Cu", (52.4, 15.9), (52.0, 16.4), (52.0, 29.9),
      (47.0, 29.9), (47.0, 43.9))
adds.append(via("EXP_IO39", 47.0, 43.9))
chain("EXP_IO39", "F.Cu", (47.0, 43.9), (44.8, 43.9), (44.8, 43.8),
      (36.5, 43.8), (36.5, 45.55))

# EXP_IO40: U4 stub -> B.Cu x52.6/x46.4/x44.0 lane -> existing via (39.97,44.66)
adds.append(via("EXP_IO40", 52.2, 14.63))
chain("EXP_IO40", "F.Cu", (52.4279, 14.63), (52.2, 14.63))
chain("EXP_IO40", "B.Cu", (52.2, 14.63), (52.6, 15.3), (52.6, 29.0),
      (46.4, 29.0), (46.4, 32.4), (44.0, 32.4), (44.0, 42.5),
      (39.97, 42.5), (39.97, 44.66))

# U5-PROG: F<->B layer jump missing via at shared point
adds.append(via("Net-(U5-PROG)", 64.7241, 34.1741))

# VSYS: U7 stub F.Cu -> via E of keepout -> B.Cu long-haul -> island
adds.append(via("VSYS", 28.9, 13.5))
chain("VSYS", "F.Cu", (25.5, 12.15), (25.5, 13.5), (28.9, 13.5))
chain("VSYS", "B.Cu", (28.9, 13.5), (28.9, 9.5), (50.0, 9.5), (50.0, 18.9),
      (54.0, 18.9), (54.0, 30.5), (52.6, 30.5), (52.6, 31.4),
      (51.93, 31.4), (51.93, 31.85))

# GND: tie stitch via (42.9,17) into U4.41 EP pad (S edge y15.41)
chain("GND", "F.Cu", (42.9, 17.0), (42.9, 15.3))

# GND: replacement stitch vias in open area (was 47.5,27.8 / 49,26)
adds.append(via("GND", 46.5, 34.8))
adds.append(via("GND", 50.5, 34.8))

# insert before final closing paren
end = pt.rstrip()
assert end.endswith(")")
pt = end[:-1] + "".join(adds) + ")\n"
open(PCB, "w").write(pt)
print(f"ADDED {len(adds)} items")
