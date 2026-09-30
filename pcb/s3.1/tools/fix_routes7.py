#!/usr/bin/env python3
"""Round 7: fix round-6 leftovers; EN north via B.Cu west strip; restore SDA pad18."""
import re, uuid

PCB = 's3.1.kicad_pcb'

DEL_SEGS = [
    # round-6 EN south lane on B.Cu (hit every J10 escape stub)
    ("EN", 30.6751, 44.3834, 30.68, 51.3),
    ("EN", 30.68, 51.3, 56.86, 51.3),
    ("EN", 56.86, 51.3, 56.86, 52.54),
    # round-6 EN F.Cu east approach (crossed IO15 diag; replaced by all-B route)
    ("EN", 25.0, 23.15, 25.0, 16.2),
    ("EN", 25.0, 16.2, 34.9, 16.2),
    ("EN", 34.9, 16.2, 34.9, 15.0),
    # round-5 C14.2 stubs (grazed IO15 y14.63 horiz)
    ("EN", 34.8951, 13.3549, 34.9, 15.0),
    ("EN", 34.9, 15.0, 33.9, 15.0),
    # round-6 SCL via stubs (via relocated again)
    ("SCL", 28.8777, 28.6596, 28.7, 28.5),
    ("SCL", 28.7, 28.5, 28.8777, 28.5),
]
DEL_VIAS = [
    ("EN", 25.0, 23.15, 1),
    ("SCL", 28.7, 28.5, 1),
]

ADDS = []
def seg(net, layer, x1, y1, x2, y2, w=0.25):
    ADDS.append(("seg", net, layer, x1, y1, x2, y2, w))
def via(net, x, y):
    ADDS.append(("via", net, None, x, y, 0.7, 0.35, None))

# SCL F<->B transition via at (28.75,30.9): clear of C7.1 pad (x<=28.5,y<=29.95),
# VCC B vert x28.05 (gap 0.225), SDA B vert x29.5 (gap 0.275)
via("SCL", 28.75, 30.9)
seg("SCL", "F.Cu", 28.8777, 30.0095, 28.75, 30.9)
seg("SCL", "B.Cu", 28.75, 30.9, 28.8777, 30.9)

# EN north link (all B.Cu): via(30.68,44.38) -> x26.15 -> y23.15 -> west x20.1
# -> north to y8.81 -> east to island-A B.Cu piece (32.45,8.81)
seg("EN", "B.Cu", 26.15, 44.38, 26.15, 23.15)
seg("EN", "B.Cu", 26.15, 23.15, 20.1, 23.15)
seg("EN", "B.Cu", 20.1, 23.15, 20.1, 8.81)
seg("EN", "B.Cu", 20.1, 8.81, 32.45, 8.81)
# C14.2 pad hookup avoiding IO15 y14.63 horiz (stop short, diagonal into pad edge)
seg("EN", "F.Cu", 34.8951, 13.3549, 34.9, 14.3)
seg("EN", "F.Cu", 34.9, 14.3, 34.4, 14.6)

# SDA: restore pad18 link (deleted in r6 by mistake) + via to B.Cu stub
seg("SDA", "F.Cu", 41.825, 23.5, 41.8, 24.6)
seg("SDA", "F.Cu", 41.8, 24.6, 41.2, 25.2)
seg("SDA", "F.Cu", 41.2, 25.2, 40.6, 25.2)
via("SDA", 40.7, 25.3)

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
