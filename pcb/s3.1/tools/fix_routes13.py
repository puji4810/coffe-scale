#!/usr/bin/env python3
# fix_routes13: verified routes against full copper map
import re

PCB = "s3.1.kicad_pcb"
txt = open(PCB).read()

DEL_VIAS = [
    ("SDA", 39.0, 27.0),
    ("SCL", 42.8696, 24.8021),
]

DEL_SEGS = [
    # r12 SDA F lane (hit C13 pads / VBAT via / IO42 via)
    ("SDA", "F.Cu", 22.575, 27.0, 41.825, 27.0),
    ("SDA", "F.Cu", 41.825, 27.0, 41.825, 24.5767),
    ("SDA", "B.Cu", 39.0, 27.0, 40.0755, 25.9245),
    # r12 RXD F lane (hit C12 pads)
    ("RXD", "F.Cu", 28.7948, 6.4313, 44.5, 6.4313),
    ("RXD", "F.Cu", 44.5, 6.4313, 48.3726, 10.1884),
    # r10 RXD B.Cu vert (hit LCD_BL / CHRG_STAT)
    ("RXD", "B.Cu", 55.65, 51.05, 55.65, 10.2),
    ("RXD", "B.Cu", 55.65, 10.2, 48.3726, 10.1884),
    ("RXD", "B.Cu", 54.7, 52.54, 55.65, 51.05),
    # r11 MOSI east section (hit LCD_CS / LCD_BL / J8)
    ("LCD_MOSI", "B.Cu", 56.5, 10.85, 56.3, 10.55),
    ("LCD_MOSI", "B.Cu", 56.3, 10.55, 56.3, 9.0),
    ("LCD_MOSI", "B.Cu", 56.3, 9.0, 61.0, 9.0),
    ("LCD_MOSI", "B.Cu", 61.0, 9.0, 61.0, 4.85),
    ("LCD_MOSI", "B.Cu", 61.0, 4.85, 70.0, 4.85),
    ("LCD_MOSI", "B.Cu", 70.0, 4.85, 70.0, 3.5),
    # r9 SCL B.Cu vert+horiz (crossed INT1 vert)
    ("SCL", "B.Cu", 42.8696, 24.8021, 42.87, 30.0),
    ("SCL", "B.Cu", 42.87, 30.0, 50.5099, 30.0),
    ("SCL", "B.Cu", 50.5099, 30.0, 50.5099, 31.8773),
]

ADD_VIAS = [
    ("SDA", 41.3, 25.92),
    ("RXD", 29.0, 6.0),
    ("LCD_MOSI", 56.5, 10.85),
]

ADDS = [
    # SDA: west island B chain -> y20.8 lane -> x41.3 vert -> join x40.08 vert top
    ("SDA", "B.Cu", 26.8, 20.095, 26.8, 20.8, 0.25),
    ("SDA", "B.Cu", 26.8, 20.8, 41.3, 20.8, 0.25),
    ("SDA", "B.Cu", 41.3, 20.8, 41.3, 25.92, 0.25),
    ("SDA", "B.Cu", 41.3, 25.92, 40.0755, 25.9245, 0.25),
    # SDA: U4.18 stub -> via(41.3,25.92)
    ("SDA", "F.Cu", 41.825, 24.5767, 41.3, 25.92, 0.25),
    # SCL: pad19 stub -> F.Cu y26.5 lane -> R4.2
    ("SCL", "F.Cu", 42.8696, 24.8021, 43.095, 24.8021, 0.25),
    ("SCL", "F.Cu", 43.095, 24.8021, 43.095, 26.5, 0.25),
    ("SCL", "F.Cu", 43.095, 26.5, 51.85, 26.5, 0.25),
    ("SCL", "F.Cu", 51.85, 26.5, 51.85, 30.5, 0.25),
    ("SCL", "F.Cu", 51.85, 30.5, 50.95, 30.5, 0.25),
    # RXD: west chain -> via(29,6) -> B.Cu y6.0 lane -> via(48.37,10.19)
    ("RXD", "F.Cu", 28.7948, 6.4313, 29.0, 6.0, 0.25),
    ("RXD", "B.Cu", 29.0, 6.0, 44.0, 6.0, 0.25),
    ("RXD", "B.Cu", 44.0, 6.0, 48.3726, 10.1884, 0.25),
    # LCD_MOSI: via at (56.5,10.85) -> F.Cu x57.7 up -> J7.4
    ("LCD_MOSI", "F.Cu", 56.5, 10.85, 57.7, 10.85, 0.25),
    ("LCD_MOSI", "F.Cu", 57.7, 10.85, 57.7, 5.2, 0.25),
    ("LCD_MOSI", "F.Cu", 57.7, 5.2, 70.0, 5.2, 0.25),
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
