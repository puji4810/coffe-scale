#!/usr/bin/env python3
# fix_routes21.py — 整网拆除 EXP_IO 逃逸森林 + TXD/RXD/EN/SDA/INT1（交 repack 重布）；
# 拆 VCC/VSYS/SCL 三根挡路竖墙（局部，repack 重连）；
# 落 BAT B.Cu y42.1 大通道（接自身 x61.1 竖线）+ SCL U4 缺口补线。
import re

F = 's3.1.kicad_pcb'
t = open(F).read()

RIP_NETS = {'EXP_IO1','EXP_IO2','EXP_IO14','EXP_IO15','EXP_IO17','EXP_IO38','EXP_IO39',
            'EXP_IO40','EXP_IO41','EXP_IO42','EXP_IO47','TXD','RXD','EN','SDA','INT1'}

# 局部拆除：仅精确坐标段
PIECES = [
    ("VCC_3V3", 32.81, 38.01, 32.81, 44.99),
    ("SCL",     35.9, 34.8573, 35.9, 48.2),
    # BAT 旧件（y38.35/38.68 系列，含我此前的错误新增）
    ("BAT", 22.15, 37.6, 23.2325, 38.6825),
    ("BAT", 23.2325, 38.6825, 33.85, 38.6825),
    ("BAT", 33.85, 38.6825, 35.5, 38.35),
    ("BAT", 35.5, 38.35, 56.0, 38.35),
    ("BAT", 35.2, 38.35, 56.0, 38.35),
    ("BAT", 56.0, 38.35, 59.05, 36.0),
]

ADDS = [
    ("BAT","F.Cu",[(22.15,37.6),(22.6,38.3)],0.5),
    ("BAT","B.Cu",[(22.6,38.3),(22.6,42.1),(61.0964,42.1)],0.5),
    ("SCL","F.Cu",[(46.8538,22.2707),(43.1,22.32)],0.25),
    ("SCL","B.Cu",[(43.1,22.32),(47.3328,22.7497)],0.25),
]
VIA_ADDS = [("BAT",22.6,38.3),("SCL",43.1,22.32)]

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

def piece_match(n, co, tol=0.15):
    for net,x1,y1,x2,y2 in PIECES:
        if n != net: continue
        a = abs(co[0]-x1)<tol and abs(co[1]-y1)<tol and abs(co[2]-x2)<tol and abs(co[3]-y2)<tol
        r = abs(co[0]-x2)<tol and abs(co[1]-y2)<tol and abs(co[2]-x1)<tol and abs(co[3]-y1)<tol
        if a or r: return True
    return False

removes=[]; n_rip=0; n_piece=0
for st,en,b in parse_spans(t,"segment"):
    n=net_of(b); co=coords_of(b)
    if n in RIP_NETS:
        removes.append((st,en,f"{n}")); n_rip+=1; continue
    if co and piece_match(n,co):
        removes.append((st,en,f"{n} pc")); n_piece+=1
for st,en,b in parse_spans(t,"via"):
    n=net_of(b)
    if n in RIP_NETS:
        removes.append((st,en,f"{n} via")); n_rip+=1

removes.sort(reverse=True)
print(f"rip nets: {n_rip}, pieces: {n_piece}, total removed: {len(removes)}")
for st,en,label in removes:
    t=t[:st]+t[en:]

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
