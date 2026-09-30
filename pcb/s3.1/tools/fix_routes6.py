#!/usr/bin/env python3
"""Round 6: undo bad round-5 adds; apply verified pieces only.

Deferred (need rip-up repack): LCD_MOSI, VSYS, EXP_IO39.
"""
import re, uuid

PCB = 's3.1.kicad_pcb'

DEL_SEGS = [
    # round-5 EN pieces that collided (keep the B horiz 30.68->26.15)
    ("EN", 26.15, 44.38, 26.15, 14.5),
    ("EN", 26.15, 14.5, 28.5, 14.5),
    ("EN", 28.5, 14.5, 28.5, 12.2),
    ("EN", 28.5, 12.2, 34.5, 12.2),
    ("EN", 32.7353, 46.4436, 55.5, 46.4436),
    ("EN", 55.5, 46.4436, 55.5, 46.0),
    # round-5 VSYS pieces (blocked by SDA via/diag + IO38/VBUS squeeze)
    ("VSYS", 38.1, 26.0, 39.5, 26.0),
    ("VSYS", 39.5, 26.0, 39.5, 39.95),
    ("VSYS", 39.5, 39.95, 51.9901, 39.9535),
    # round-5 MOSI north lane (TXD/RXD/BOOT fences)
    ("LCD_MOSI", 37.3267, 10.82, 37.33, 5.3),
    ("LCD_MOSI", 37.33, 5.3, 58.0, 5.3),
    ("LCD_MOSI", 58.0, 5.3, 58.0, 0.8),
    ("LCD_MOSI", 58.0, 0.8, 70.0, 0.8),
    ("LCD_MOSI", 70.0, 0.8, 70.0, 2.7),
    # round-5 VCC thread (endpoint grazed J10.2 pad)
    ("VCC_3V3", 36.54, 53.9, 35.27, 53.9),
    ("VCC_3V3", 35.27, 53.9, 35.27, 50.6),
    ("VCC_3V3", 35.27, 50.6, 35.3133, 50.5467),
    # round-5 GND stub (crossed by IO41 y15.87 horiz)
    ("GND", 42.9, 17.0, 42.9, 15.3),
    # SDA F dead-end island (B.Cu route is continuous; F spurs only dangle)
    ("SDA", 41.8, 23.5, 41.8, 24.6),
    ("SDA", 41.8, 24.6, 41.2, 25.2),
    ("SDA", 41.2, 25.2, 40.6, 25.2),
    ("SDA", 41.825, 24.5767, 41.232, 25.1697),
    ("SDA", 41.232, 25.1697, 40.5522, 25.1697),
]
DEL_VIAS = [
    ("EN", 26.15, 14.5, 1),
    ("VSYS", 39.5, 26.0, 1),
    ("VSYS", 39.5, 39.95, 1),
    ("LCD_MOSI", 37.3267, 10.82, 1),
    ("SCL", 28.8777, 28.6596, 1),
    ("SDA", 40.58, 25.18, 1),
    ("GND", 42.9, 17.0, 1),        # dangling; pad41 stitched via new via below
]

ADDS = []
def seg(net, layer, x1, y1, x2, y2, w=0.25):
    ADDS.append(("seg", net, layer, x1, y1, x2, y2, w))
def via(net, x, y):
    ADDS.append(("via", net, None, x, y, 0.7, 0.35, None))

# SCL: relocate the F<->B transition via to (28.7,28.5) (clear of C7.1/SDA vert)
via("SCL", 28.7, 28.5)
seg("SCL", "F.Cu", 28.8777, 28.6596, 28.7, 28.5)
seg("SCL", "B.Cu", 28.7, 28.5, 28.8777, 28.5)

# EN north link: B.Cu x26.15 south of stub fence -> via -> F.Cu x25 -> y16.2 -> x34.9
seg("EN", "B.Cu", 26.15, 44.38, 26.15, 23.15)
seg("EN", "B.Cu", 26.15, 23.15, 25.0, 23.15)
via("EN", 25.0, 23.15)
seg("EN", "F.Cu", 25.0, 23.15, 25.0, 16.2)
seg("EN", "F.Cu", 25.0, 16.2, 34.9, 16.2)
seg("EN", "F.Cu", 34.9, 16.2, 34.9, 15.0)
# EN south link: via(30.68,44.38) -> B.Cu x30.68 -> y51.3 lane between J10 rows -> pad20
seg("EN", "B.Cu", 30.6751, 44.3834, 30.68, 51.3)
seg("EN", "B.Cu", 30.68, 51.3, 56.86, 51.3)
seg("EN", "B.Cu", 56.86, 51.3, 56.86, 52.54)

# VCC_3V3: J10.12 stub via -> between-row thread -> main island vert (extended 0.4)
seg("VCC_3V3", "B.Cu", 36.54, 53.9, 35.27, 53.9, 0.5)
seg("VCC_3V3", "B.Cu", 35.27, 53.9, 35.27, 50.95, 0.25)
seg("VCC_3V3", "B.Cu", 35.27, 50.95, 35.3133, 50.95, 0.25)
seg("VCC_3V3", "B.Cu", 35.3133, 50.95, 35.3133, 50.5467, 0.25)

# GND: stitch pad41 from SW via B.Cu (avoids IO41 y15.87 horiz)
via("GND", 40.5, 15.0)
seg("GND", "B.Cu", 40.5, 15.0, 41.9, 14.3, 0.3)

def block_end(txt, j):
    d = 0; k = j
    while k < len(txt):
        if txt[k] == '(': d += 1
        elif txt[k] == ')':
            d -= 1
            if d == 0: return k
        k += 1
    return -1

txt = open(PCB).read()

def seg_key(b):
    m = re.search(r'\(start ([-\d.]+) ([-\d.]+)\)\s*\(end ([-\d.]+) ([-\d.]+)\)', b)
    n = re.search(r'\(net "([^"]+)"\)', b)
    if not m: return None
    return (n.group(1) if n else '?',) + tuple(float(v) for v in m.groups())

def via_key(b):
    m = re.search(r'\(at ([-\d.]+) ([-\d.]+)\)', b)
    n = re.search(r'\(net "([^"]+)"\)', b)
    if not m: return None
    return (n.group(1) if n else '?', float(m.group(1)), float(m.group(2)))

rem = []
for m in re.finditer(r'\(segment\b', txt):
    e = block_end(txt, m.start())
    k = seg_key(txt[m.start():e+1])
    if k: rem.append((m.start(), e+1, k))
def match_seg(k, spec):
    n,x1,y1,x2,y2 = spec
    if k[0] != n: return False
    a = abs(k[1]-x1)+abs(k[2]-y1)+abs(k[3]-x2)+abs(k[4]-y2)
    b = abs(k[1]-x2)+abs(k[2]-y2)+abs(k[3]-x1)+abs(k[4]-y1)
    return min(a,b) < 0.1
kill = set()
for spec in DEL_SEGS:
    hit = [r for r in rem if match_seg(r[2], spec)]
    for r in hit: kill.add((r[0], r[1]))
    print('DEL seg', spec, '->', len(hit))
via_marks = {}
for m in re.finditer(r'\(via\b', txt):
    e = block_end(txt, m.start())
    k = via_key(txt[m.start():e+1])
    if k: via_marks[(m.start(), e+1)] = k
for n,x,y,cnt in DEL_VIAS:
    cands = [(s,e,k) for (s,e),k in via_marks.items()
             if k[0]==n and abs(k[1]-x)<0.05 and abs(k[2]-y)<0.05 and (s,e) not in kill]
    cands.sort(key=lambda c: abs(c[2][1]-x)+abs(c[2][2]-y))
    for s,e,k in cands[:cnt]: kill.add((s,e))
    print('DEL via', n, x, y, '->', len(cands[:cnt]), 'of', len(cands))

out = []; last = 0
for s,e in sorted(kill):
    out.append(txt[last:s]); last = e
out.append(txt[last:])
txt = ''.join(out)

add_txt = ''
for a in ADDS:
    if a[0] == 'seg':
        _,net,layer,x1,y1,x2,y2,w = a
        add_txt += (f'\n\t(segment\n\t\t(start {x1} {y1})\n\t\t(end {x2} {y2})\n'
                    f'\t\t(width {w})\n\t\t(layer "{layer}")\n\t\t(net "{net}")\n'
                    f'\t\t(uuid "{uuid.uuid4()}")\n\t)')
    else:
        _,net,_,x,y,sz,dr,_ = a
        add_txt += (f'\n\t(via\n\t\t(at {x} {y})\n\t\t(size {sz})\n\t\t(drill {dr})\n'
                    f'\t\t(layers "F.Cu" "B.Cu")\n\t\t(net "{net}")\n'
                    f'\t\t(uuid "{uuid.uuid4()}")\n\t)')
txt = txt.rstrip()
assert txt.endswith(')')
txt = txt[:-1] + add_txt + '\n)\n'
open(PCB,'w').write(txt)
print('ADDED', len(ADDS), 'items;', len(kill), 'removed')
