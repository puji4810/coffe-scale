#!/usr/bin/env python3
# fix_routes14: dodge VSYS/IO17 for SDA; R17 for SCL; EN via for RXD; drop MOSI dead-end
import re

PCB = "s3.1.kicad_pcb"
txt = open(PCB).read()

DEL_VIAS = [
    ("RXD", 29.0, 6.0),
    ("LCD_MOSI", 56.5, 10.85),
]

DEL_SEGS = [
    # r13 SDA y20.8 lane (VSYS via + IO17 vert)
    ("SDA", "B.Cu", 26.8, 20.095, 26.8, 20.8),
    ("SDA", "B.Cu", 26.8, 20.8, 41.3, 20.8),
    ("SDA", "B.Cu", 41.3, 20.8, 41.3, 25.92),
    # r13 SCL y26.5 lane (R17 pad + VSYS razor)
    ("SCL", "F.Cu", 43.095, 24.8021, 43.095, 26.5),
    ("SCL", "F.Cu", 43.095, 26.5, 51.85, 26.5),
    ("SCL", "F.Cu", 51.85, 26.5, 51.85, 30.5),
    ("SCL", "F.Cu", 51.85, 30.5, 50.95, 30.5),
    # r13 RXD via + B lane (EN F via clash)
    ("RXD", "F.Cu", 28.7948, 6.4313, 29.0, 6.0),
    ("RXD", "B.Cu", 29.0, 6.0, 44.0, 6.0),
    ("RXD", "B.Cu", 44.0, 6.0, 48.3726, 10.1884),
    # MOSI dead-end lane (east approach sealed by IO1/BL/BTN_MODE/J8)
    ("LCD_MOSI", "B.Cu", 39.92, 8.2248, 39.92, 10.85),
    ("LCD_MOSI", "B.Cu", 39.92, 10.85, 56.5, 10.85),
    ("LCD_MOSI", "F.Cu", 56.5, 10.85, 57.7, 10.85),
    ("LCD_MOSI", "F.Cu", 57.7, 10.85, 57.7, 5.2),
    ("LCD_MOSI", "F.Cu", 57.7, 5.2, 70.0, 5.2),
    ("LCD_MOSI", "F.Cu", 70.0, 5.2, 70.0, 3.5),
]

ADD_VIAS = [
    ("RXD", 28.5, 6.2),
]

ADDS = [
    # SDA: west B chain (26.8,20.095) -> y19.2 lane north of IO17 vert -> x41.3 down -> x40.08 top
    ("SDA", "B.Cu", 26.8, 20.095, 26.8, 19.2, 0.25),
    ("SDA", "B.Cu", 26.8, 19.2, 41.3, 19.2, 0.25),
    ("SDA", "B.Cu", 41.3, 19.2, 41.3, 25.92, 0.25),
    # SCL: pad19 stub -> y27.0 lane -> x52.15 down -> R4.2
    ("SCL", "F.Cu", 43.095, 24.8021, 43.095, 27.0, 0.25),
    ("SCL", "F.Cu", 43.095, 27.0, 52.15, 27.0, 0.25),
    ("SCL", "F.Cu", 52.15, 27.0, 52.15, 30.5, 0.25),
    ("SCL", "F.Cu", 52.15, 30.5, 50.95, 30.5, 0.25),
    # RXD: via at (28.5,6.2) -> B.Cu y6.2 lane -> via(48.37,10.19)
    ("RXD", "F.Cu", 28.7948, 6.4313, 28.5, 6.2, 0.25),
    ("RXD", "B.Cu", 28.5, 6.2, 44.0, 6.2, 0.25),
    ("RXD", "B.Cu", 44.0, 6.2, 48.3726, 10.1884, 0.25),
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
