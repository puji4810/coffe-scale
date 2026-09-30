#!/usr/bin/env python3
# fix_routes10: cleanup orphans + final joins (round 10)
import re

PCB = "s3.1.kicad_pcb"
txt = open(PCB).read()

DEL_VIAS = [
    ("SDA", 42.8947, 24.9792),        # co-located with SCL via target
    ("LCD_DC", 42.0, 11.0),           # dangling, blocks MOSI B.Cu lane
    ("VBUS", 28.7976, 38.2233),       # dangling via (F side connects anyway via seg)
]

DEL_SEGS = [
    # CC2 orphan chain (R9.1-J5.B5 already joined by their own stub)
    ("Net-(J5-CC2)", "F.Cu", 48.85, 25.3, 47.6, 26.55),
    ("Net-(J5-CC2)", "F.Cu", 47.6, 26.55, 47.6, 29.3),
    ("Net-(J5-CC2)", "F.Cu", 47.6, 29.3, 47.78, 30.1),
    ("Net-(J5-CC2)", "F.Cu", 47.78, 30.1, 47.78, 30.955),
    # my r9 CC2 adds - all dead
    ("Net-(J5-CC2)", "F.Cu", 48.85, 25.3, 48.85, 26.8),
    ("Net-(J5-CC2)", "F.Cu", 48.85, 26.8, 49.6, 27.55),
    ("Net-(J5-CC2)", "F.Cu", 49.6, 27.55, 49.6, 30.1),
    ("Net-(J5-CC2)", "F.Cu", 49.6, 30.1, 47.78, 30.1),
    ("Net-(J5-CC2)", "F.Cu", 47.78, 30.1, 47.78, 44.0),
    ("Net-(J5-CC2)", "F.Cu", 47.78, 44.0, 75.75, 44.0),
    ("Net-(J5-CC2)", "F.Cu", 75.75, 44.0, 75.75, 43.25),
    # my r9 SDA west adds (cross VSYS/BOOT)
    ("SDA", "B.Cu", 27.8246, 10.1848, 27.82, 6.7),
    ("SDA", "B.Cu", 27.82, 6.7, 36.5, 6.7),
    ("SDA", "B.Cu", 36.5, 6.7, 36.5, 25.92),
    ("SDA", "B.Cu", 36.5, 25.92, 40.0755, 25.92),
    # my r9 SDA via link (via being deleted)
    ("SDA", "B.Cu", 42.8947, 24.9792, 40.0755, 25.9245),
    # my r9 RXD adds (J10.9 pad collision)
    ("RXD", "B.Cu", 54.2, 52.54, 54.2, 10.2),
    ("RXD", "B.Cu", 54.2, 10.2, 48.3726, 10.1884),
    # my r9 MOSI adds (U4 pad row collision)
    ("LCD_MOSI", "F.Cu", 39.9219, 8.2248, 39.92, 5.5),
    ("LCD_MOSI", "F.Cu", 39.92, 5.5, 70.0, 5.5),
    ("LCD_MOSI", "F.Cu", 70.0, 5.5, 70.0, 3.5),
    # SDA dangling spur at (49.93,26.98)
    ("SDA", "F.Cu", 50.95, 28.0, 49.9263, 26.9763),
    # VBUS dead-end spur off J11 pad
    ("VBUS", "F.Cu", 9.2312, 4.1983, 7.0191, 6.4104),
    ("VBUS", "F.Cu", 7.0191, 6.4104, 7.0191, 8.9809),
    ("VBUS", "F.Cu", 7.0191, 8.9809, 6.0, 10.0),
    ("VBUS", "F.Cu", 6.0, 10.0, 1.7535, 14.2465),
    ("VBUS", "B.Cu", 28.7976, 38.2233, 29.2703, 37.7506),
    # SDA F stub tail to deleted via
    ("SDA", "F.Cu", 41.825, 24.5767, 42.489, 24.5767),
    ("SDA", "F.Cu", 42.489, 24.5767, 42.8947, 24.9792),
]

ADD_VIAS = [
    ("SCL", 42.8696, 24.8021),        # restore F<->B link for U4.19
    ("Net-(U5-PROG)", 64.7241, 34.1741),  # restore single via
    ("SDA", 41.4, 25.0),              # SDA [4]->[18] bridge
    ("LCD_MOSI", 39.9219, 8.2248),    # stub end -> B.Cu
    ("GND", 55.9, 7.0),               # U4.40 GND stub link
    ("GND", 30.0, 45.0),              # F.Cu zone island stitch
    ("GND", 62.0, 47.0),              # F.Cu zone island stitch
]

ADDS = [
    # SDA: F.Cu tap off vert x22.575 -> U4.18 pad (joins [12] to pad/[4])
    ("SDA", "F.Cu", 22.575, 20.3, 36.8, 20.3, 0.25),
    ("SDA", "F.Cu", 36.8, 20.3, 36.8, 23.5, 0.25),
    ("SDA", "F.Cu", 36.8, 23.5, 41.825, 23.5, 0.25),
    # SDA: stub -> via(41.4,25.0) -> B.Cu -> vert x40.08 top
    ("SDA", "F.Cu", 41.825, 24.5767, 41.4, 25.0, 0.25),
    ("SDA", "B.Cu", 41.4, 25.0, 40.0755, 25.92, 0.25),
    # RXD: [4] via -> south, west at y16.3, down x32.3, into [6] stub end
    ("RXD", "B.Cu", 48.3726, 10.1884, 48.37, 16.3, 0.25),
    ("RXD", "B.Cu", 48.37, 16.3, 32.3, 16.3, 0.25),
    ("RXD", "B.Cu", 32.3, 16.3, 32.3, 5.95, 0.25),
    ("RXD", "B.Cu", 32.3, 5.95, 31.7101, 5.7356, 0.25),
    # RXD: J10.19 pad -> vert x55.65 -> west to [4] via
    ("RXD", "B.Cu", 54.7, 52.54, 55.65, 51.05, 0.25),
    ("RXD", "B.Cu", 55.65, 51.05, 55.65, 10.2, 0.25),
    ("RXD", "B.Cu", 55.65, 10.2, 48.3726, 10.1884, 0.25),
    # LCD_MOSI: via at stub end -> B.Cu north lane -> J7.4 (PTH)
    ("LCD_MOSI", "B.Cu", 39.92, 8.2248, 39.92, 10.85, 0.25),
    ("LCD_MOSI", "B.Cu", 39.92, 10.85, 56.5, 10.85, 0.25),
    ("LCD_MOSI", "B.Cu", 56.5, 10.85, 57.4, 10.1, 0.25),
    ("LCD_MOSI", "B.Cu", 57.4, 10.1, 63.7, 10.1, 0.25),
    ("LCD_MOSI", "B.Cu", 63.7, 10.1, 63.7, 4.85, 0.25),
    ("LCD_MOSI", "B.Cu", 63.7, 4.85, 70.0, 4.85, 0.25),
    ("LCD_MOSI", "B.Cu", 70.0, 4.85, 70.0, 3.5, 0.25),
    # GND stub extension to new via
    ("GND", "F.Cu", 55.3554, 6.3, 55.9, 7.0, 0.25),
]

def parse_blocks(txt, tag):
    out = []
    i = 0
    while True:
        j = txt.find("(" + tag, i)
        if j < 0:
            break
        d = 0
        k = j
        while k < len(txt):
            if txt[k] == "(":
                d += 1
            elif txt[k] == ")":
                d -= 1
                if d == 0:
                    break
            k += 1
        out.append((j, k + 1))
        i = k + 1
    return out

delset = set()
for n, l, x1, y1, x2, y2 in DEL_SEGS:
    delset.add((n, l, frozenset([(round(x1, 4), round(y1, 4)), (round(x2, 4), round(y2, 4))])))
delvias = {(n, round(x, 4), round(y, 4)) for n, x, y in DEL_VIAS}

spans = []
missed = []
seen = set()
for (s, e) in parse_blocks(txt, "segment"):
    b = txt[s:e]
    nm = re.search(r'\(net "([^"]+)"\)', b)
    ly = re.search(r'\(layer "([^"]+)"\)', b)
    at = re.search(r'\(start ([-\d.]+) ([-\d.]+)\)\s*\(end ([-\d.]+) ([-\d.]+)\)', b)
    if not (nm and ly and at):
        continue
    key = (nm.group(1), ly.group(1), frozenset([(round(float(at.group(1)), 4), round(float(at.group(2)), 4)), (round(float(at.group(3)), 4), round(float(at.group(4)), 4))]))
    if key in delset:
        spans.append((s, e)); seen.add(key)
for k in delset - seen:
    missed.append(("SEG", k))

removed_vias = 0
for (s, e) in parse_blocks(txt, "via"):
    b = txt[s:e]
    nm = re.search(r'\(net "([^"]+)"\)', b)
    at = re.search(r'\(at ([-\d.]+) ([-\d.]+)\)', b)
    if not (nm and at):
        continue
    key = (nm.group(1), round(float(at.group(1)), 4), round(float(at.group(2)), 4))
    if key in delvias:
        spans.append((s, e))
        removed_vias += 1

spans.sort(reverse=True)
for (s, e) in spans:
    while s > 0 and txt[s - 1] in " \t":
        s -= 1
    if s > 0 and txt[s - 1] == "\n":
        s -= 1
    txt = txt[:s] + txt[e:]

add_lines = []
for (net, x, y) in ADD_VIAS:
    add_lines.append(
        f'  (via (at {x} {y}) (size 0.7) (drill 0.35) '
        f'(layers "F.Cu" "B.Cu") (net "{net}"))'
    )
for (net, layer, x1, y1, x2, y2, w) in ADDS:
    add_lines.append(
        f'  (segment (start {x1} {y1}) (end {x2} {y2}) (width {w}) '
        f'(layer "{layer}") (net "{net}"))'
    )
idx = txt.rstrip().rfind(")")
txt = txt[:idx] + "\n" + "\n".join(add_lines) + "\n" + txt[idx:]

open(PCB, "w").write(txt)
print(f"removed {len(spans)} items ({removed_vias} vias); added {len(add_lines)} items")
for m in missed:
    print("  MISS:", m)
