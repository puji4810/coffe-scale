#!/usr/bin/env python3
# fix_routes19.py — 撤销 fix18 碰撞新增；SCL 用验证过的路径落地；
# 拆掉 VCC 撞 XIO40 焊盘的 B.Cu 链；其余断网交给 freerouting repack。
import re, sys

F = 's3.1.kicad_pcb'
t = open(F).read()

# ---------- DELETE (net, kind, coords) ----------
DEL = [
    # my fix18 SCL adds that collide with EN via / IO15 vert / IO41 vert
    ("SCL","seg",(28.4,26.4,37.7,26.4)),
    ("SCL","seg",(37.7,26.4,37.7,29.5)),
    ("SCL","seg",(37.7,29.5,32.6428,30.0387)),
    ("SCL","seg",(35.2421,40.9533,42.89,48.6012)),
    ("SCL","seg",(42.89,48.6012,42.89,49.0288)),
    ("SCL","seg",(42.89,49.0288,42.89,50.7982)),
    ("SCL","seg",(42.89,50.7982,43.3618,51.27)),
    # my fix18 VCC B corridor (crosses ~8 nets)
    ("VCC_3V3","seg",(31.83,27.6,46.0,24.5)),
    ("VCC_3V3","seg",(46.0,24.5,47.2,24.0)),
    ("VCC_3V3","seg",(47.2,24.0,58.5,22.5)),
    ("VCC_3V3","seg",(58.5,22.5,58.5,24.0)),
    ("VCC_3V3","via",(58.5,24.0)),
    # my fix18 VCC-E F route (shorts IO47/BUZZ_SW/C25.2/CHRG)
    ("VCC_3V3","seg",(58.5,24.0,55.9,24.0)),
    ("VCC_3V3","seg",(55.9,24.0,55.9,28.5)),
    ("VCC_3V3","seg",(55.9,28.5,67.3,28.5)),
    ("VCC_3V3","seg",(67.3,28.5,67.3,32.4)),
    ("VCC_3V3","seg",(67.3,32.4,58.55,32.4)),
    ("VCC_3V3","seg",(58.55,32.4,58.55,31.8)),
    # my fix18 IO47 adds (short BUZZ pad + VCC)
    ("EXP_IO47","seg",(48.5,25.0,48.5,25.9)),
    ("EXP_IO47","seg",(48.5,25.9,53.8,25.9)),
    ("EXP_IO47","seg",(53.8,25.9,55.8,27.5)),
    ("EXP_IO47","via",(55.8,27.5)),
    ("EXP_IO47","seg",(55.8,27.5,56.0835,31.2152)),
    # my fix18 XIO15 adds (cross IO38 F / IO39 via / IO40 pad / R29.1)
    ("XIO15","seg",(53.05,40.9,39.3,40.9)),
    ("XIO15","seg",(39.3,40.9,39.3,46.4)),
    ("XIO15","seg",(39.3,46.4,48.2,46.4)),
    ("XIO15","seg",(48.2,46.4,48.2,46.9)),
    ("XIO15","seg",(48.2,46.9,49.2,46.9)),
    # VCC B chain shorting XIO40 J10 pad (PTH barrel at 39.08,47.45)
    ("VCC_3V3","seg",(36.54,53.9,38.3,53.9)),
    ("VCC_3V3","seg",(38.3,53.9,38.3,48.3)),
    ("VCC_3V3","seg",(38.3,48.3,35.3133,47.4964)),
]

# ---------- ADD: (net, layer, [pts], width) ----------
ADDS = [
    ("SCL","B.Cu",[(28.4,26.4),(32.6428,30.0387)],0.25),
    ("SCL","B.Cu",[(35.2421,40.9533),(35.9,41.6),(35.9,48.2),(30.0,48.2),(30.0,51.27),(43.3618,51.27)],0.25),
]
VIA_ADDS = []

# ---------- helpers ----------
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

# ---------- append ----------
def seg_block(net,layer,x1,y1,x2,y2,w):
    return (f'\t(segment\n\t\t(start {x1} {y1})\n\t\t(end {x2} {y2})\n'
            f'\t\t(width {w})\n\t\t(layer "{layer}")\n\t\t(net "{net}")\n\t)\n')
def via_block(net,x,y):
    return (f'\t(via\n\t\t(at {x} {y})\n\t\t(size 0.7)\n\t\t(drill 0.35)\n'
            f'\t\t(layers "F.Cu" "B.Cu")\n\t\t(net "{net}")\n\t)\n')

add_txt=""
nseg=0
for net,layer,pts,w in ADDS:
    for i in range(len(pts)-1):
        add_txt+=seg_block(net,layer,pts[i][0],pts[i][1],pts[i+1][0],pts[i+1][1],w); nseg+=1
for net,x,y in VIA_ADDS:
    add_txt+=via_block(net,x,y); nseg+=1

pos=t.rfind(')')
t=t[:pos]+add_txt+t[pos:]
open(F,'w').write(t)
print(f"adds: {nseg}")
