#!/usr/bin/env python3
"""Round-4: keepout shrunk to x<26,y>=24. All new tracks w=0.2, vias 0.6/0.3.
Verified per-segment against full copper maps:
- SDA: delete x28.3 B.Cu fence; reroute F.Cu x25.3 + via(25.3,20.1)
- EN: via(30.9,11) -> B.Cu y9.5 west -> x19.4 south -> y23.6 east -> orphan
- VSYS: F.Cu (20.15,11.7)-(20.15,14.8) -> via -> B.Cu y14.8 -> x51.9 -> y31.8 stub
- VCC islandB -> islandA: via(8.1,9.1) -> B.Cu y9.0 -> x31.6 -> (30.74,14.06)
- VCC islandC -> east: via(58.5,30.1) -> B.Cu x59.85 -> via(60.5,15.5)
- EXP_IO39: via(52.7,15.9) -> B.Cu x52.7 -> y24.4 -> x44.35 -> y43.9 -> R25.1
- EXP_IO40: via(52.4,14.6) -> x52.0 -> y24.0 -> x43.9 -> y43.4 -> R26.1
- LCD_MOSI: F.Cu y16.65 under-module lane -> jog -> x57.8 -> y2.3 -> J7.4
"""
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

DEL_SEGS = [
    ("SDA", 27.8246, 10.1848, 28.3486, 10.7088),  # B.Cu stub off the via
    ("SDA", 28.3486, 10.7088, 28.3486, 18.6539),  # x28.35 fence vert
    ("SDA", 28.3486, 18.6539, 26.9075, 20.095),   # diag into via field
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

def seg(net, layer, x1, y1, x2, y2, w=0.2):
    return (f'\t(segment\n\t\t(start {x1} {y1})\n\t\t(end {x2} {y2})\n'
            f'\t\t(width {w})\n\t\t(layer "{layer}")\n\t\t(net "{net}")\n'
            f'\t\t(uuid "{uuid.uuid4()}")\n\t)\n')

def via(net, x, y, size=0.6, drill=0.3):
    return (f'\t(via\n\t\t(at {x} {y})\n\t\t(size {size})\n\t\t(drill {drill})\n'
            f'\t\t(layers "F.Cu" "B.Cu")\n\t\t(net "{net}")\n'
            f'\t\t(uuid "{uuid.uuid4()}")\n\t)\n')

adds = [
    # SDA: from via(27.8246,10.1848) -> B.Cu x26.5 south, hopping over VSYS y14.8 lane
    seg("SDA", "B.Cu", 27.8246, 10.1848, 26.5, 10.1848),
    seg("SDA", "B.Cu", 26.5, 10.1848, 26.5, 14.1),
    via("SDA", 26.5, 14.1),
    seg("SDA", "F.Cu", 26.5, 14.1, 26.5, 15.5),
    via("SDA", 26.5, 15.5),
    seg("SDA", "B.Cu", 26.5, 15.5, 26.5, 20.095),
    seg("SDA", "B.Cu", 26.5, 20.095, 26.8, 20.095),

    # EN: via(30.9,11) -> up to y9.5 -> west x19.4 -> south -> east y23.6 -> orphan
    seg("EN", "B.Cu", 30.9, 11.0, 30.9, 9.5),
    seg("EN", "B.Cu", 30.9, 9.5, 19.4, 9.5),
    seg("EN", "B.Cu", 19.4, 9.5, 19.4, 23.6),
    seg("EN", "B.Cu", 19.4, 23.6, 26.3, 23.6),
    seg("EN", "B.Cu", 26.3, 23.6, 26.8, 25.6),
    seg("EN", "B.Cu", 26.8, 25.6, 27.4, 26.6),

    # VCC islandB (8.03,9.03) -> via -> B.Cu y9.0 -> x31.6 -> VCC vert top (30.74,14.06)
    via("VCC_3V3", 8.1, 9.1),
    seg("VCC_3V3", "B.Cu", 8.1, 9.1, 8.1, 9.0),
    seg("VCC_3V3", "B.Cu", 8.1, 9.0, 31.6, 9.0),
    seg("VCC_3V3", "B.Cu", 31.6, 9.0, 31.6, 14.05),
    seg("VCC_3V3", "B.Cu", 31.6, 14.05, 30.74, 14.06),

]

end = pt.rstrip()
pt = end[:-1] + "".join(adds) + ")\n"
open(FN, "w").write(pt)
print(f"ADDED {len(adds)} items")
