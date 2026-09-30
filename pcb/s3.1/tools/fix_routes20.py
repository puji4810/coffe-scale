#!/usr/bin/env python3
# fix_routes20.py — 删 CC2 孤儿链；BAT 横线改道 y38.35 避开 VBUS；
# SCL 补回 U1.13/U3.1/U2.1 连接（x19.5 西通道）。
import re

F = 's3.1.kicad_pcb'
t = open(F).read()

DEL = [
    # CC2 orphan chain (real pads already connected via stub 75.75,43.25-45.955)
    ("Net-(J5-CC2)","seg",(48.85,25.3,47.6,26.55)),
    ("Net-(J5-CC2)","seg",(47.6,26.55,47.6,29.3)),
    ("Net-(J5-CC2)","seg",(47.6,29.3,47.78,30.1)),
    ("Net-(J5-CC2)","seg",(47.78,30.1,47.78,30.955)),
    # BAT horiz + east diag start (0.5mm track 0.45mm under VBUS 0.5mm)
    ("BAT","seg",(33.1671,38.6074,56.4426,38.6074)),
    ("BAT","seg",(56.4426,38.6074,59.05,36.0)),
]

ADDS = [
    ("BAT","F.Cu",[(33.092,38.6825),(35.2,38.35),(56.0,38.35),(59.05,36.0)],0.5),
    # SCL west re-connect: U1.13 via -> x19.5 -> main island end (30.5216,31.9983)
    ("SCL","B.Cu",[(20.7,21.365),(19.5,21.365),(19.5,33.0),(30.52,33.0),(30.5216,31.9983)],0.25),
    # U3.1 stub via -> x19.5
    ("SCL","B.Cu",[(20.3,29.3),(19.5,29.3)],0.25),
    # U2.1 pad -> via -> B chain end
    ("SCL","F.Cu",[(30.4,32.5),(30.8,32.3),(31.15,32.25)],0.25),
    ("SCL","B.Cu",[(30.4,32.5),(30.7,32.0),(30.5216,31.9983)],0.25),
]
VIA_ADDS = [("SCL",30.4,32.5)]

def parse_spans(t, tag):
    out=[]; i=0
    while True:
        s=t.find('('+tag, i)
        if s<0: break
        d=0; e=s
        while e<len(t):
            if t[e]=='(': d+=1
            elif t[e]==')':
                d-=1
                if d==0: e+=1; break
            e+=1
        out.append((s,e,t[s:e])); i=e
    return out

def net_of(b):
    m=re.search(r'\(net "([^"]+)"\)',b)
    return m.group(1) if m else None

def coords_of(b):
    s=re.search(r'\(start ([-\d.]+) ([-\d.]+)\)',b)
    e=re.search(r'\(end ([-\d.]+) ([-\d.]+)\)',b)
    return (float(s.group(1)),float(s.group(2)),float(e.group(1)),float(e.group(2))) if s and e else None

def pt_of(b):
    a=re.search(r'\(at ([-\d.]+) ([-\d.]+)\)',b)
    return (float(a.group(1)),float(a.group(2))) if a else None

def match_del(net,kind,co,n,co2,tol=0.35):
    if n!=net or co2 is None: return False
    if kind=="via":
        return abs(co2[0]-co[0])<tol and abs(co2[1]-co[1])<tol
    a = all(abs(co2[i]-co[i])<tol for i in range(4))
    r = (abs(co2[0]-co[2])<tol and abs(co2[1]-co[3])<tol and
         abs(co2[2]-co[0])<tol and abs(co2[3]-co[1])<tol)
    return a or r

removes=[]; hit=set()
for st,en,b in parse_spans(t,"segment"):
    n=net_of(b); co=coords_of(b)
    for i,(net,kind,c) in enumerate(DEL):
        if kind=="seg" and match_del(net,kind,c,n,co):
            removes.append((st,en,f"{n} seg {co}")); hit.add(i); break
for st,en,b in parse_spans(t,"via"):
    n=net_of(b); co=pt_of(b)
    if co is None: continue
    for i,(net,kind,c) in enumerate(DEL):
        if kind=="via" and match_del(net,kind,c,n,co):
            removes.append((st,en,f"{n} via {co}")); hit.add(i); break

removes.sort(reverse=True)
print(f"removing {len(removes)} items")
for st,en,label in removes:
    t=t[:st]+t[en:]
for i,(net,kind,c) in enumerate(DEL):
    if i not in hit: print("  MISS:",net,kind,c)

def seg_block(net,layer,x1,y1,x2,y2,w):
    return (f'\t(segment\n\t\t(start {x1} {y1})\n\t\t(end {x2} {y2})\n'
            f'\t\t(width {w})\n\t\t(layer "{layer}")\n\t\t(net "{net}")\n\t)\n')
def via_block(net,x,y):
    return (f'\t(via\n\t\t(at {x} {y})\n\t\t(size 0.7)\n\t\t(drill 0.35)\n'
            f'\t\t(layers "F.Cu" "B.Cu")\n\t\t(net "{net}")\n\t)\n')

add_txt=""; nseg=0
for net,layer,pts,w in ADDS:
    for i in range(len(pts)-1):
        add_txt+=seg_block(net,layer,pts[i][0],pts[i][1],pts[i+1][0],pts[i+1][1],w); nseg+=1
for net,x,y in VIA_ADDS:
    add_txt+=via_block(net,x,y); nseg+=1

pos=t.rfind(')')
t=t[:pos]+add_txt+t[pos:]
open(F,'w').write(t)
print(f"adds: {nseg}")
