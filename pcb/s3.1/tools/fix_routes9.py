#!/usr/bin/env python3
# fix_routes9: post-repack surgical joins + dangling-via cleanup
import re

PCB = "s3.1.kicad_pcb"
txt = open(PCB).read()

DEL_VIAS = [
    ("GND", 31.9, 10.0),          # shorts R5.1 VCC pad
    ("Net-(U5-PROG)", 64.7241, 34.1741),  # duplicate co-located via
    ("INT1", 45.5, 26.5),         # dangling
    ("EXP_IO40", 42.9, 17.0),     # dangling
    ("GND", 40.5, 15.0),          # dangling, pad41 has own thermal vias
]

DEL_SEGS = [
    # VBUS dangling B stub at via (66.61,41.95)
    ("VBUS", "B.Cu", 66.6063, 41.9527, 65.857, 41.9527),
    # CC2 crossing chain (crosses VCC horiz + BUZZ) - rerouted below
    ("Net-(J5-CC2)", "F.Cu", 48.85, 25.3, 47.6, 26.55),
    ("Net-(J5-CC2)", "F.Cu", 47.6, 26.55, 47.6, 29.3),
    ("Net-(J5-CC2)", "F.Cu", 47.6, 29.3, 47.78, 30.1),
    ("Net-(J5-CC2)", "F.Cu", 47.78, 30.1, 47.78, 30.955),
]

ADDS = [
    # ==== SCL: 3 islands -> 1 ====
    # [2] TP13/U3.1 -> [8] via U1.13 pad chain (F.Cu)
    ("SCL", "F.Cu", 21.425, 31.0, 21.9, 31.0, 0.25),
    ("SCL", "F.Cu", 21.9, 31.0, 21.9, 21.365, 0.25),
    ("SCL", "F.Cu", 21.9, 21.365, 21.2, 21.365, 0.25),
    # [8] -> [19]: B.Cu x32.81 vert
    ("SCL", "B.Cu", 30.5216, 31.9983, 32.8052, 32.0, 0.25),
    ("SCL", "B.Cu", 32.8052, 32.0, 32.8052, 36.0415, 0.25),
    # U4.19 via(42.87,24.8) -> x50.51 vert (island [19])
    ("SCL", "B.Cu", 42.8696, 24.8021, 42.87, 30.0, 0.25),
    ("SCL", "B.Cu", 42.87, 30.0, 50.5099, 30.0, 0.25),
    ("SCL", "B.Cu", 50.5099, 30.0, 50.5099, 31.8773, 0.25),
    # ==== SDA ====
    # [4] U4.18 stub via -> [18] vert x40.08 top
    ("SDA", "B.Cu", 42.8947, 24.9792, 40.0755, 25.9245, 0.25),
    # [2] U2.4 via -> [18] at y33.6
    ("SDA", "B.Cu", 31.2188, 34.4721, 31.22, 33.6, 0.25),
    ("SDA", "B.Cu", 31.22, 33.6, 40.0755, 33.6, 0.25),
    # [12] west -> [18]: north B.Cu lane y6.7 then x36.5 down to y25.92
    ("SDA", "B.Cu", 27.8246, 10.1848, 27.82, 6.7, 0.25),
    ("SDA", "B.Cu", 27.82, 6.7, 36.5, 6.7, 0.25),
    ("SDA", "B.Cu", 36.5, 6.7, 36.5, 25.92, 0.25),
    ("SDA", "B.Cu", 36.5, 25.92, 40.0755, 25.92, 0.25),
    # ==== RXD ====
    ("RXD", "B.Cu", 48.3726, 10.1884, 44.1484, 6.1162, 0.25),
    ("RXD", "B.Cu", 54.2, 52.54, 54.2, 10.2, 0.25),
    ("RXD", "B.Cu", 54.2, 10.2, 48.3726, 10.1884, 0.25),
    # ==== LCD_MOSI: F.Cu north lane to J7.4 ====
    ("LCD_MOSI", "F.Cu", 39.9219, 8.2248, 39.92, 5.5, 0.25),
    ("LCD_MOSI", "F.Cu", 39.92, 5.5, 70.0, 5.5, 0.25),
    ("LCD_MOSI", "F.Cu", 70.0, 5.5, 70.0, 3.5, 0.25),
    # ==== CC2 reroute: pad (48.85,25.3) -> stub (47.78,30.955) -> J5 pad ====
    ("Net-(J5-CC2)", "F.Cu", 48.85, 25.3, 48.85, 26.8, 0.25),
    ("Net-(J5-CC2)", "F.Cu", 48.85, 26.8, 49.6, 27.55, 0.25),
    ("Net-(J5-CC2)", "F.Cu", 49.6, 27.55, 49.6, 30.1, 0.25),
    ("Net-(J5-CC2)", "F.Cu", 49.6, 30.1, 47.78, 30.1, 0.25),
    ("Net-(J5-CC2)", "F.Cu", 47.78, 30.1, 47.78, 44.0, 0.25),
    ("Net-(J5-CC2)", "F.Cu", 47.78, 44.0, 75.75, 44.0, 0.25),
    ("Net-(J5-CC2)", "F.Cu", 75.75, 44.0, 75.75, 43.25, 0.25),
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
for (s, e) in parse_blocks(txt, "segment"):
    b = txt[s:e]
    nm = re.search(r'\(net "([^"]+)"\)', b)
    ly = re.search(r'\(layer "([^"]+)"\)', b)
    at = re.search(r'\(start ([-\d.]+) ([-\d.]+)\)\s*\(end ([-\d.]+) ([-\d.]+)\)', b)
    if not (nm and ly and at):
        continue
    n, l = nm.group(1), ly.group(1)
    x1, y1, x2, y2 = map(float, at.groups())
    key = (n, l, frozenset([(round(x1, 4), round(y1, 4)), (round(x2, 4), round(y2, 4))]))
    if key in delset:
        spans.append((s, e))

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
for (net, layer, x1, y1, x2, y2, w) in ADDS:
    add_lines.append(
        f'  (segment (start {x1} {y1}) (end {x2} {y2}) (width {w}) '
        f'(layer "{layer}") (net "{net}"))'
    )
idx = txt.rstrip().rfind(")")
txt = txt[:idx] + "\n" + "\n".join(add_lines) + "\n" + txt[idx:]

open(PCB, "w").write(txt)
print(f"removed {len(spans)} items ({removed_vias} vias); added {len(add_lines)} segments")
