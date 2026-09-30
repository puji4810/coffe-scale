#!/usr/bin/env python3
"""Post-merge cleanup round 5: remove colliding freerouting artifacts and
manually close the remaining unconnected nets."""
import re, sys, uuid

PCB = 's3.1.kicad_pcb'

# ---- deletions ----
DEL_SEGS = [  # (net, x1, y1, x2, y2)
    ("Net-(J5-CC2)", 48.85, 25.3, 47.6, 26.55),
    ("Net-(J5-CC2)", 47.6, 26.55, 47.6, 29.3),
    ("Net-(J5-CC2)", 47.6, 29.3, 47.78, 30.1),
    ("Net-(J5-CC2)", 47.78, 30.1, 47.78, 30.955),
]
DEL_VIAS = [  # (net, x, y, max_count)
    ("GND", 45.5, 26.5, 1),          # shorts INT1 F + EXP_IO40 B
    ("GND", 44.0, 25.0, 1),          # isolated stitch via
    ("GND", 31.9, 10.0, 1),          # collides R5.1 pad
    ("GND", 24.0, 47.0, 1),          # co-located duplicate
    ("Net-(U5-PROG)", 64.7241, 34.1741, 1),  # co-located duplicate
    ("LCD_DC", 42.0, 11.0, 1),       # dangling via, clears U4.41 pad
    ("SCL", 26.8, 21.365, 1),        # redundant: B.Cu segs meet here anyway
    ("SDA", 26.8, 20.095, 1),        # redundant
]

# ---- additions ----
ADDS = []  # (kind, net, layer, points...)
def seg(net, layer, x1, y1, x2, y2, w=0.25):
    ADDS.append(("seg", net, layer, x1, y1, x2, y2, w))
def via(net, x, y):
    ADDS.append(("via", net, None, x, y, 0.7, 0.35, None))

# SCL / SDA layer joins (B.Cu and F.Cu tracks share endpoints)
via("SCL", 28.8777, 28.6596)
via("SDA", 30.6367, 33.8866)
via("SDA", 40.58, 25.18)

# EN: C14.2 pad hookup
seg("EN", "F.Cu", 34.8951, 13.3549, 34.9, 15.0)
seg("EN", "F.Cu", 34.9, 15.0, 33.9, 15.0)
# EN: orphan-via island -> J10.20 stub island (F.Cu south corridor)
seg("EN", "F.Cu", 32.7353, 46.4436, 55.5, 46.4436)
seg("EN", "F.Cu", 55.5, 46.4436, 55.5, 46.0)
# EN: orphan via -> B.Cu west corridor -> F.Cu -> main island vert x34.5
seg("EN", "B.Cu", 30.6751, 44.3834, 26.15, 44.38)
seg("EN", "B.Cu", 26.15, 44.38, 26.15, 14.5)
via("EN", 26.15, 14.5)
seg("EN", "F.Cu", 26.15, 14.5, 28.5, 14.5)
seg("EN", "F.Cu", 28.5, 14.5, 28.5, 12.2)
seg("EN", "F.Cu", 28.5, 12.2, 34.5, 12.2)

# VSYS: west island F tip (38.1,26) -> via -> B.Cu vert x39.5 -> via -> F.Cu -> east island horiz
via("VSYS", 39.5, 26.0)
seg("VSYS", "F.Cu", 38.1, 26.0, 39.5, 26.0, 0.5)
seg("VSYS", "B.Cu", 39.5, 26.0, 39.5, 39.95, 0.5)
via("VSYS", 39.5, 39.95)
seg("VSYS", "F.Cu", 39.5, 39.95, 51.9901, 39.9535, 0.5)

# VCC_3V3: J10.12 stub via -> B.Cu thread between J10 pins -> main island vert x35.31
seg("VCC_3V3", "B.Cu", 36.54, 53.9, 35.27, 53.9, 0.5)
seg("VCC_3V3", "B.Cu", 35.27, 53.9, 35.27, 50.6, 0.25)
seg("VCC_3V3", "B.Cu", 35.27, 50.6, 35.3133, 50.5467, 0.5)

# LCD_MOSI: stub via -> B.Cu north-edge hugger -> J7.4 pad top
via("LCD_MOSI", 37.3267, 10.82)
seg("LCD_MOSI", "B.Cu", 37.3267, 10.82, 37.33, 5.3)
seg("LCD_MOSI", "B.Cu", 37.33, 5.3, 58.0, 5.3)
seg("LCD_MOSI", "B.Cu", 58.0, 5.3, 58.0, 0.8)
seg("LCD_MOSI", "B.Cu", 58.0, 0.8, 70.0, 0.8)
seg("LCD_MOSI", "B.Cu", 70.0, 0.8, 70.0, 2.7)

# GND: stitch via(42.9,17) to U4.41 PTH pad (B.Cu annular)
seg("GND", "B.Cu", 42.9, 17.0, 42.9, 15.3, 0.3)

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

# delete segs (match either direction, tol 0.05)
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
# delete vias (count-limited, nearest first)
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

out = []
last = 0
for s,e in sorted(kill):
    out.append(txt[last:s]); last = e
out.append(txt[last:])
txt = ''.join(out)

# append additions before the final closing paren
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
