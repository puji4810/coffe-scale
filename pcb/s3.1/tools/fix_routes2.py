#!/usr/bin/env python3
"""Round-2 manual route repairs on merged s3.1.kicad_pcb.
Only adds items verified collision-free against full region dumps.
Run AFTER fix_routes.py deletions (already applied)."""
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

def ap(a, b, t=0.02):
    return abs(a - b) < t

# ---- deletions: TP13 old stub segments (pad moves south) ----
DEL_SEGS = [
    ("SCL", 18.0, 10.0, 17.3187, 9.3187),
    ("SCL", 17.3187, 9.3187, 17.3187, 7.7058),
    ("SCL", 17.3187, 7.7058, 17.7177, 7.3068),
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
for s, e, d in sorted(removed, reverse=True):
    pt = pt[:s] + pt[e:]
    print("DEL", d)

# ---- TP13 footprint relocate (18,10) -> (19.5,30.6) ----
for s, e, blk in blocks(pt, "(footprint"):
    if 'Reference" "TP13"' in blk:
        nb = re.sub(r'(\n\t\t\(at )18 10( [-\d.]+)?\)', r'\g<1>19.5 30.6\g<2>)', blk, count=1)
        if nb != blk:
            pt = pt[:s] + nb + pt[e:]
            print("MOVED TP13 -> (19.5,30.6)")
        else:
            m = re.search(r'\(at ([-\d.]+) ([-\d.]+)', blk)
            print("TP13 at:", m.groups() if m else "?")

def seg(net, layer, x1, y1, x2, y2, w=0.25):
    return (f'\t(segment\n\t\t(start {x1} {y1})\n\t\t(end {x2} {y2})\n'
            f'\t\t(width {w})\n\t\t(layer "{layer}")\n\t\t(net "{net}")\n'
            f'\t\t(uuid "{uuid.uuid4()}")\n\t)\n')

def via(net, x, y):
    return (f'\t(via\n\t\t(at {x} {y})\n\t\t(size 0.7)\n\t\t(drill 0.35)\n'
            f'\t\t(layers "F.Cu" "B.Cu")\n\t\t(free yes)\n\t\t(net "{net}")\n'
            f'\t\t(uuid "{uuid.uuid4()}")\n\t)\n')

adds = [
    # SIG2 taps: bridge 0.8mm gap stub->channel
    seg("SIG2_N", "F.Cu", 14.50, 21.40, 15.30, 21.36),
    seg("SIG2_P", "F.Cu", 14.50, 22.70, 15.30, 22.64),
    # BTN layer joins (same point, different layer)
    via("BTN_TARE", 62.0, 10.2),
    via("BTN_MODE", 64.0, 10.2),
    # EN orphan -> J10-side island (B.Cu vertical, east of keepout)
    seg("EN", "B.Cu", 27.36, 26.60, 27.36, 41.07),
    # GND: stitch via (42.9,17) into U4 pad41 edge (stops at pad edge, y<16.83)
    seg("GND", "F.Cu", 42.9, 17.0, 42.9, 16.80, 0.30),
    # SCL: TP13 relocated stub into U3 pad1 (SCL)
    seg("SCL", "F.Cu", 20.05, 30.60, 20.90, 30.95),
    # LCD_MOSI: via down to B.Cu, north lane to J7.4 PTH
    via("LCD_MOSI", 39.92, 8.22),
    seg("LCD_MOSI", "B.Cu", 39.92, 8.22, 39.00, 8.22),
    seg("LCD_MOSI", "B.Cu", 39.00, 8.22, 39.00, 5.25),
    seg("LCD_MOSI", "B.Cu", 39.00, 5.25, 68.00, 5.25),
    seg("LCD_MOSI", "B.Cu", 68.00, 5.25, 68.00, 3.50),
    seg("LCD_MOSI", "B.Cu", 68.00, 3.50, 69.30, 3.50),
    # GND stitching candidates (contiguous pour areas)
    via("GND", 70.0, 25.0),
    via("GND", 58.0, 45.5),
    via("GND", 24.0, 47.0),
]

end = pt.rstrip()
pt = end[:-1] + "".join(adds) + ")\n"
open(FN, "w").write(pt)
print(f"ADDED {len(adds)} items")
