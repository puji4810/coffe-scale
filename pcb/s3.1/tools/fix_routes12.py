#!/usr/bin/env python3
# fix_routes12: RXD -> F.Cu direct lane; SDA -> y27 lane; MOSI corner dodge
import re

PCB = "s3.1.kicad_pcb"
txt = open(PCB).read()

DEL_VIAS = [
    ("GND", 42.0, 11.0),            # stitches pour; blocks MOSI lane, nets elsewhere
    ("RXD", 28.7948, 6.4313),       # B-side stubs deleted -> via obsolete
    ("SDA", 41.4, 24.13),           # replaced by y27 route
]

DEL_SEGS = [
    # r10 RXD B.Cu detour (crossed EN/VCC)
    ("RXD", "B.Cu", 48.3726, 10.1884, 48.37, 16.3),
    ("RXD", "B.Cu", 48.37, 16.3, 32.3, 16.3),
    ("RXD", "B.Cu", 32.3, 16.3, 32.3, 5.95),
    ("RXD", "B.Cu", 32.3, 5.95, 31.7101, 5.7356),
    ("RXD", "B.Cu", 48.3726, 10.1884, 44.1484, 6.1162),
    # RXD dead-end B stubs off via(28.79,6.43)
    ("RXD", "B.Cu", 29.0387, 6.1874, 28.7948, 6.4313),
    ("RXD", "B.Cu", 30.6979, 6.1874, 29.0387, 6.1874),
    ("RXD", "B.Cu", 31.1496, 5.7356, 30.6979, 6.1873),
    ("RXD", "B.Cu", 31.7101, 5.7356, 31.1496, 5.7356),
    # r10/r11 SDA adds superseded by y27 lane
    ("SDA", "F.Cu", 22.575, 20.3, 36.8, 20.3),
    ("SDA", "F.Cu", 36.8, 20.3, 36.8, 24.13),
    ("SDA", "F.Cu", 36.8, 24.13, 41.4, 24.13),
    ("SDA", "F.Cu", 41.4, 24.13, 41.825, 24.5767),
    ("SDA", "B.Cu", 41.4, 24.13, 40.0755, 25.9245),
    # SCL dead-end spur
    ("SCL", "B.Cu", 32.8052, 37.9849, 32.8052, 36.0415),
    # r11 MOSI corner (BTN_MODE clearance)
    ("LCD_MOSI", "B.Cu", 56.5, 10.85, 56.5, 9.0),
    ("LCD_MOSI", "B.Cu", 56.5, 9.0, 61.0, 9.0),
]

ADD_VIAS = [
    ("SDA", 39.0, 27.0),
]

ADDS = [
    # RXD: F.Cu lane under U4 north pad row -> via(48.37,10.19)
    ("RXD", "F.Cu", 28.7948, 6.4313, 44.5, 6.4313, 0.25),
    ("RXD", "F.Cu", 44.5, 6.4313, 48.3726, 10.1884, 0.25),
    # SDA: [12] vert x22.575 -> y27 lane -> up to U4.18 stub; via -> B.Cu x40.08
    ("SDA", "F.Cu", 22.575, 27.0, 41.825, 27.0, 0.25),
    ("SDA", "F.Cu", 41.825, 27.0, 41.825, 24.5767, 0.25),
    ("SDA", "B.Cu", 39.0, 27.0, 40.0755, 25.9245, 0.25),
    # LCD_MOSI: corner moved west to clear BTN_MODE stub
    ("LCD_MOSI", "B.Cu", 56.5, 10.85, 56.3, 10.55, 0.25),
    ("LCD_MOSI", "B.Cu", 56.3, 10.55, 56.3, 9.0, 0.25),
    ("LCD_MOSI", "B.Cu", 56.3, 9.0, 61.0, 9.0, 0.25),
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
