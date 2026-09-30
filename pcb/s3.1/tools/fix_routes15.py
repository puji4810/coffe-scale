#!/usr/bin/env python3
# fix_routes15: SCL pad19 link + vert reroute; SDA south join; RXD y5.9 lane; MOSI inside-ring F.Cu
import re

PCB = "s3.1.kicad_pcb"
txt = open(PCB).read()

DEL_VIAS = [
    ("LCD_MOSI", 39.92, 8.2248),  # B side abandoned; F route now continues east
]

DEL_SEGS = [
    # SCL vert crossing VSYS horiz y26.02
    ("SCL", "F.Cu", 43.095, 24.8021, 43.095, 27.0),
    # SCL B vert crossing SDA diag
    ("SCL", "B.Cu", 32.8052, 32.0, 32.8052, 36.0415),
    ("SCL", "B.Cu", 32.8052, 36.0415, 34.6473, 34.1994),
]

ADD_VIAS = [
    ("SDA", 31.22, 33.6),     # join west F vert -> middle B piece
    ("SDA", 40.7, 25.92),     # pad18 stub -> x40.08 vert top
    ("RXD", 28.95, 6.3),
]

ADDS = [
    # SCL: pad19 -> stub -> y24.8 east -> x45.2 down -> y27 lane -> R4.2
    ("SCL", "F.Cu", 43.095, 23.5, 43.095, 24.8021, 0.25),
    ("SCL", "F.Cu", 42.8696, 24.8021, 45.2, 24.8, 0.25),
    ("SCL", "F.Cu", 45.2, 24.8, 45.2, 27.0, 0.25),
    ("SCL", "F.Cu", 45.2, 27.0, 52.15, 27.0, 0.25),
    # SCL B: reroute vert west of SDA diag
    ("SCL", "B.Cu", 30.5216, 31.9983, 30.52, 36.0415, 0.25),
    ("SCL", "B.Cu", 30.52, 36.0415, 34.6473, 34.1994, 0.25),
    # SDA: west island F vert (22.575,31) -> via -> middle B piece (31.22,33.6)
    ("SDA", "F.Cu", 22.575, 31.0, 31.22, 33.6, 0.25),
    # SDA: pad18 stub -> via(40.7,25.92) -> x40.08 vert top
    ("SDA", "F.Cu", 41.825, 24.5767, 40.7, 25.92, 0.25),
    ("SDA", "B.Cu", 40.7, 25.92, 40.0755, 25.9245, 0.25),
    # RXD: west chain -> via(28.95,6.3) -> B.Cu y5.9 lane -> via(48.37,10.19)
    ("RXD", "F.Cu", 28.7948, 6.4313, 28.95, 6.3, 0.25),
    ("RXD", "B.Cu", 28.95, 6.3, 29.6, 5.9, 0.25),
    ("RXD", "B.Cu", 29.6, 5.9, 44.0, 5.9, 0.25),
    ("RXD", "B.Cu", 44.0, 5.9, 48.3726, 10.1884, 0.25),
    # LCD_MOSI: F.Cu inside U4 pad ring -> east -> down to J7.4
    ("LCD_MOSI", "F.Cu", 39.9219, 8.2248, 54.4, 8.2248, 0.25),
    ("LCD_MOSI", "F.Cu", 54.4, 8.2248, 54.4, 13.0, 0.25),
    ("LCD_MOSI", "F.Cu", 54.4, 13.0, 58.3, 13.0, 0.25),
    ("LCD_MOSI", "F.Cu", 58.3, 13.0, 58.3, 8.9, 0.25),
    ("LCD_MOSI", "F.Cu", 58.3, 8.9, 60.8, 8.9, 0.25),
    ("LCD_MOSI", "F.Cu", 60.8, 8.9, 60.8, 5.2, 0.25),
    ("LCD_MOSI", "F.Cu", 60.8, 5.2, 70.0, 5.2, 0.25),
    ("LCD_MOSI", "F.Cu", 70.0, 5.2, 70.0, 3.5, 0.25),
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
