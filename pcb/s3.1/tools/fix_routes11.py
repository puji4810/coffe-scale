#!/usr/bin/env python3
# fix_routes11: fix r10 collisions - SDA U4-pad-row reroute, MOSI J8 dodge
import re

PCB = "s3.1.kicad_pcb"
txt = open(PCB).read()

DEL_VIAS = [
    ("SDA", 41.4, 25.0),
]

DEL_SEGS = [
    # SDA stub tail to removed via (42.89,24.98)
    ("SDA", "F.Cu", 41.825, 24.5767, 42.4922, 24.5767),
    ("SDA", "F.Cu", 42.4922, 24.5767, 42.8947, 24.9792),
    # r10 SDA adds crossing U4 pad rows -> replaced by y24.13 lane
    ("SDA", "F.Cu", 36.8, 20.3, 36.8, 23.5),
    ("SDA", "F.Cu", 36.8, 23.5, 41.825, 23.5),
    ("SDA", "F.Cu", 41.825, 24.5767, 41.4, 25.0),
    ("SDA", "B.Cu", 41.4, 25.0, 40.0755, 25.92),
    # r10 MOSI adds hitting J8 PTH pads -> replaced
    ("LCD_MOSI", "B.Cu", 56.5, 10.85, 57.4, 10.1),
    ("LCD_MOSI", "B.Cu", 57.4, 10.1, 63.7, 10.1),
    ("LCD_MOSI", "B.Cu", 63.7, 10.1, 63.7, 4.85),
    ("LCD_MOSI", "B.Cu", 63.7, 4.85, 70.0, 4.85),
    ("LCD_MOSI", "B.Cu", 70.0, 4.85, 70.0, 3.5),
]

ADD_VIAS = [
    ("SDA", 41.4, 24.13),
]

ADDS = [
    # SDA: [12] -> U4.18 stub via lane between pad rows (y24.13)
    ("SDA", "F.Cu", 36.8, 20.3, 36.8, 24.13, 0.25),
    ("SDA", "F.Cu", 36.8, 24.13, 41.4, 24.13, 0.25),
    ("SDA", "F.Cu", 41.4, 24.13, 41.825, 24.5767, 0.25),
    # SDA: via -> B.Cu -> east vert x40.08 top end
    ("SDA", "B.Cu", 41.4, 24.13, 40.0755, 25.9245, 0.25),
    # LCD_MOSI: north of J8 pad band via corridor x56.5 -> x61 -> J7.4
    ("LCD_MOSI", "B.Cu", 56.5, 10.85, 56.5, 9.0, 0.25),
    ("LCD_MOSI", "B.Cu", 56.5, 9.0, 61.0, 9.0, 0.25),
    ("LCD_MOSI", "B.Cu", 61.0, 9.0, 61.0, 4.85, 0.25),
    ("LCD_MOSI", "B.Cu", 61.0, 4.85, 70.0, 4.85, 0.25),
    ("LCD_MOSI", "B.Cu", 70.0, 4.85, 70.0, 3.5, 0.25),
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
    print("  MISS:", k)

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
