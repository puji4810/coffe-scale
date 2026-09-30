#!/usr/bin/env python3
"""Round-3: undo round-2 collisions, reroute MOSI with jogs, re-place TP13."""
import re, uuid

FN = "s3.1.kicad_pcb"
pt = open(FN).read()

def blocks(text, head):
    out = []
    for m in re.finditer(re.escape(head), text):
        d, i = 0, m.start()
        while i < len(text):
            if text[i] == '(':
                d += 1
            elif text[i] == ')':
                d -= 1
                if d == 0:
                    out.append((m.start(), i + 1, text[m.start():i + 1]))
                    break
            i += 1
    return out

def ap(a, b, t=0.03):
    return abs(a - b) < t

# ---- deletions ----
DEL_VIAS = [
    ("BTN_TARE", 62.0, 10.2),   # redundant: J8.2 PTH already joins F+B
    ("BTN_MODE", 64.0, 10.2),   # redundant: J8.3 PTH
    ("GND", 70.0, 25.0),        # hits USB_DP_M via / C23.1 hole
    ("GND", 58.0, 45.5),        # hits RXD B.Cu
]
DEL_SEGS = [
    ("LCD_MOSI", 39.92, 8.22, 39.0, 8.22),
    ("LCD_MOSI", 39.0, 8.22, 39.0, 5.25),
    ("LCD_MOSI", 39.0, 5.25, 68.0, 5.25),
    ("LCD_MOSI", 68.0, 5.25, 68.0, 3.5),
    ("LCD_MOSI", 68.0, 3.5, 69.3, 3.5),
    ("SCL", 20.05, 30.6, 20.9, 30.95),
]
removed = []
for s, e, blk in blocks(pt, "(segment"):
    nmm = re.search(r'\(net "([^"]+)"\)', blk)
    st = re.search(r'\(start ([-\d.]+) ([-\d.]+)\)', blk)
    en = re.search(r'\(end ([-\d.]+) ([-\d.]+)\)', blk)
    if not (nmm and st and en):
        continue
    for net, x1, y1, x2, y2 in DEL_SEGS:
        if nmm.group(1) == net and all(ap(float(g), v) for g, v in
                                       zip(st.groups() + en.groups(), (x1, y1, x2, y2))):
            removed.append((s, e, f"seg {net} {x1},{y1}-{x2},{y2}"))
            break
for s, e, blk in blocks(pt, "(via"):
    nmm = re.search(r'\(net "([^"]+)"\)', blk)
    a = re.search(r'\(at ([-\d.]+) ([-\d.]+)\)', blk)
    if not (nmm and a):
        continue
    for net, x, y in DEL_VIAS:
        if nmm.group(1) == net and ap(float(a.group(1)), x) and ap(float(a.group(2)), y):
            removed.append((s, e, f"via {net} {x},{y}"))
            break
for s, e, d in sorted(removed, reverse=True):
    pt = pt[:s] + pt[e:]
    print("DEL", d)

# ---- TP13 (19.5,30.6) -> (20.3,29.3): clears SIG_N channel x19.0 & U3 courtyard ----
for s, e, blk in blocks(pt, "(footprint"):
    if 'Reference" "TP13"' in blk:
        nb = re.sub(r'(\n\t\t\(at )19\.5 30\.6( [-\d.]+)?\)', r'\g<1>20.3 29.3\g<2>)', blk, count=1)
        if nb != blk:
            pt = pt[:s] + nb + pt[e:]
            print("MOVED TP13 -> (20.3,29.3)")

def seg(net, layer, x1, y1, x2, y2, w=0.25):
    return (f'\t(segment\n\t\t(start {x1} {y1})\n\t\t(end {x2} {y2})\n'
            f'\t\t(width {w})\n\t\t(layer "{layer}")\n\t\t(net "{net}")\n'
            f'\t\t(uuid "{uuid.uuid4()}")\n\t)\n')

adds = [
    # SCL: TP13 pad (20.3,29.3) -> U3 pad1 (20.74-21.36,30.85-31.15) + SCL stub (21.43,31)
    seg("SCL", "F.Cu", 20.80, 29.60, 21.10, 30.90),
    seg("SCL", "F.Cu", 21.10, 30.90, 21.45, 30.95),
    # LCD_MOSI on B.Cu: via(39.92,8.22) -> x39 down -> y6.1 lane (above BOOT diag, below TXD)
    #   jog around LCD_BL via(60.91,6.61) via south-west detour, then to J7.4
    seg("LCD_MOSI", "B.Cu", 39.92, 8.22, 39.0, 8.22),
    seg("LCD_MOSI", "B.Cu", 39.0, 8.22, 39.0, 6.10),
    seg("LCD_MOSI", "B.Cu", 39.0, 6.10, 60.3, 6.10),
    seg("LCD_MOSI", "B.Cu", 60.3, 6.10, 58.7, 6.10),
    seg("LCD_MOSI", "B.Cu", 58.7, 6.10, 58.7, 7.60),
    seg("LCD_MOSI", "B.Cu", 58.7, 7.60, 61.6, 7.60),
    seg("LCD_MOSI", "B.Cu", 61.6, 7.60, 61.6, 6.10),
    seg("LCD_MOSI", "B.Cu", 61.6, 6.10, 71.5, 6.10),
    seg("LCD_MOSI", "B.Cu", 71.5, 6.10, 71.5, 3.50),
    seg("LCD_MOSI", "B.Cu", 71.5, 3.50, 70.3, 3.50),
]

end = pt.rstrip()
pt = end[:-1] + "".join(adds) + ")\n"
open(FN, "w").write(pt)
print(f"ADDED {len(adds)} items")
