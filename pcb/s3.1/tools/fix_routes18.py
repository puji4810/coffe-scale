#!/usr/bin/env python3
"""fix_routes18: rip congestion-causing copper, apply verified-clean joins.

Strategy: freerouting repack left ~20 nets forming interlocking walls. Remove
the wall segments (they get repacked later), apply all geometry-verified joins
for the remaining true islands, shrink analog keepout x26->x19.
"""
import re, math, sys

PCB = "s3.1.kicad_pcb"
t = open(PCB).read()

def parse_spans(text, tag):
    """Yield (start,end,body) of top-level (tag ...) blocks."""
    out = []
    i = 0
    pat = f"({tag}"
    while True:
        j = text.find(pat, i)
        if j < 0: break
        # ensure it's a block start at line start-ish
        depth = 0; k = j
        while k < len(text):
            c = text[k]
            if c == '(': depth += 1
            elif c == ')':
                depth -= 1
                if depth == 0:
                    out.append((j, k+1, text[j:k+1])); i = k+1; break
            k += 1
        else: break
    return out

def net_of(b):
    m = re.search(r'\(net "([^"]+)"\)', b)
    return m.group(1) if m else None

def coords_of(b):
    s = re.search(r'\(start ([-\d.]+) ([-\d.]+)\)', b)
    e = re.search(r'\(end ([-\d.]+) ([-\d.]+)\)', b)
    return tuple(map(float, s.groups())) + tuple(map(float, e.groups()))

def pt_of(b):
    a = re.search(r'\(at ([-\d.]+) ([-\d.]+)\)', b)
    return (float(a.group(1)), float(a.group(2))) if a else None

# ---------- DELETE list: (net, kind, approx coords) ----------
# kind 'seg': match segment endpoints within tol; 'via': match via at within tol
DEL = [
  # CC2 orphan F chain (net has only J5.B5+R9.1, already stub-connected)
  ("Net-(J5-CC2)","seg",(48.85,25.3,47.6,26.55)),
  ("Net-(J5-CC2)","seg",(47.6,26.55,47.6,29.3)),
  ("Net-(J5-CC2)","seg",(47.6,29.3,47.78,30.1)),
  ("Net-(J5-CC2)","seg",(47.78,30.1,47.78,30.955)),
  # GND dead-end via + stub chain
  ("GND","via",(45.5,26.5)),
  ("GND","via",(54.469,31.729)),
  ("GND","seg",(54.469,31.729,54.95,31.248)),
  ("GND","seg",(54.95,31.248,54.95,30.5)),
  ("GND","seg",(54.95,30.5,54.56,30.11)),
  # GND vias/stubs blocking SCL U3 hop
  ("GND","via",(22.3,33.6)),
  ("GND","seg",(22.57,32.0,22.3,32.0)),
  ("GND","seg",(22.3,32.0,22.3,33.6)),
  ("GND","via",(21.4,33.4)),
  ("GND","via",(62.5,29.5)),
  ("GND","seg",(62.66,31.8,62.86,32.0)),
  ("GND","seg",(63.19,31.68,62.86,32.0)),
  # SDA B.Cu wall pieces (vert x26.65 + diag + staircase top)
  ("SDA","seg",(26.65,31.46,26.65,24.15)),
  ("SDA","seg",(26.65,24.15,26.11,23.6)),
  ("SDA","seg",(29.82,34.62,26.65,31.46)),
  ("SDA","seg",(26.11,23.6,20.84,23.6)),
  ("SDA","seg",(20.84,23.6,20.09,22.85)),
  ("SDA","seg",(20.09,22.85,20.09,20.7)),
  ("SDA","seg",(20.09,20.7,20.7,20.09)),
  # DRDY B.Cu wall
  ("DRDY","seg",(27.32,31.15,27.32,23.15)),
  ("DRDY","seg",(27.32,31.15,29.5,33.33)),
  ("DRDY","via",(29.5,33.33)),
  ("DRDY","seg",(30.6,36.76,28.58,34.74)),
  ("DRDY","seg",(28.58,34.74,28.58,34.37)),
  ("DRDY","via",(28.58,34.37)),
  # VSYS walls
  ("VSYS","seg",(25.47,13.1,36.46,24.08)),
  ("VSYS","seg",(36.46,24.08,36.46,30.61)),
  ("VSYS","seg",(36.38,33.18,36.38,39.65)),
  ("VSYS","seg",(62.82,40.47,41.23,40.47)),
  ("VSYS","seg",(39.88,40.47,37.2,40.47)),
  # RXD walls
  ("RXD","seg",(24.74,13.96,35.69,24.91)),
  ("RXD","seg",(35.69,24.91,35.69,30.8)),
  ("RXD","seg",(35.69,30.8,36.11,31.22)),
  # EN walls
  ("EN","seg",(34.77,26.33,34.77,30.28)),
  ("EN","seg",(31.97,23.53,34.77,26.33)),
  ("EN","seg",(36.33,48.49,26.65,38.81)),
  # INT1 B diag + F chain + via
  ("INT1","seg",(29.92,31.8,33.75,27.97)),
  ("INT1","via",(29.77,31.8)),
  # IO15 walls
  ("EXP_IO15","seg",(34.54,15.79,34.54,19.47)),
  ("EXP_IO15","seg",(34.54,19.47,37.09,22.03)),
  ("EXP_IO15","seg",(37.09,30.6,39.88,33.39)),
  ("EXP_IO15","seg",(45.93,42.24,49.24,45.55)),
  ("EXP_IO15","seg",(39.91,42.24,45.93,42.24)),
  # IO14 vert
  ("EXP_IO14","seg",(46.66,24.83,46.66,44.3)),
  # IO17 vert + F tail + via
  ("EXP_IO17","seg",(50.7,28.66,50.7,44.47)),
  ("EXP_IO17","seg",(51.78,45.55,50.7,44.47)),
  ("EXP_IO17","via",(50.7,44.47)),
  # IO1 vert
  ("EXP_IO1","seg",(51.26,27.63,51.26,43.91)),
  # IO42 via + tail
  ("EXP_IO42","via",(45.06,44.65)),
  ("EXP_IO42","seg",(45.06,44.65,44.16,45.55)),
  ("EXP_IO42","seg",(40.47,29.0,43.64,29.0)),
  ("EXP_IO42","via",(43.64,29.0)),
  # IO38 via
  ("EXP_IO38","via",(43.27,42.99)),
  # TXD vert + diag + horiz + via
  ("TXD","seg",(50.51,45.35,50.51,51.27)),
  ("TXD","seg",(50.51,45.35,46.72,41.56)),
  ("TXD","seg",(46.72,41.56,41.42,41.56)),
  ("TXD","seg",(41.42,41.56,40.56,40.7)),
  ("TXD","via",(40.56,40.7)),
  # PROG tail + via
  ("Net-(U5-PROG)","seg",(66.0,31.05,66.36,31.41)),
  ("Net-(U5-PROG)","seg",(65.14,31.05,66.0,31.05)),
  ("Net-(U5-PROG)","via",(66.36,31.41)),
  # CHRG_STAT tail
  ("CHRG_STAT","seg",(60.95,30.29,60.95,29.6)),
  ("CHRG_STAT","seg",(59.5,32.34,58.78,33.06)),
  # Q2-G fanout
  ("Net-(Q2-G)","seg",(53.05,28.56,53.05,30.5)),
  ("Net-(Q2-G)","seg",(52.95,26.0,53.51,26.0)),
  ("Net-(Q2-G)","seg",(53.51,26.0,54.56,27.05)),
  # DP_M stub + via
  ("USB_DP_M","via",(69.42,25.51)),
  ("USB_DP_M","seg",(70.16,25.51,70.5,25.85)),
  # VCC B vert + diag (island AB keeps via31.83)
  ("VCC_3V3","seg",(30.01,20.99,30.01,25.78)),
  ("VCC_3V3","seg",(30.01,25.78,31.83,27.6)),
  # VBAT_SENSE vert + diag chain
  ("VBAT_SENSE","seg",(25.95,29.76,25.95,35.7)),
  ("VBAT_SENSE","seg",(31.43,24.28,25.95,29.76)),
  ("VBAT_SENSE","seg",(34.05,24.28,31.43,24.28)),
  ("VBAT_SENSE","seg",(34.34,24.58,34.05,24.28)),
  ("VBAT_SENSE","seg",(40.55,24.58,34.34,24.58)),
  # BUZZ horiz (cages IO47)
  ("BUZZ","seg",(49.6,26.0,51.05,26.0)),
  ("BUZZ","seg",(48.18,24.58,49.6,26.0)),
  # IO1 vert
  ("EXP_IO1","seg",(49.16,21.79,49.16,25.53)),
]

def match_del(net, kind, co, b_net, b_co, tol=0.35):
    if b_net != net: return False
    if kind == "via":
        return abs(b_co[0]-co[0])<tol and abs(b_co[1]-co[1])<tol
    # segment: match both endpoints either order
    a = (abs(b_co[0]-co[0])<tol and abs(b_co[1]-co[1])<tol and
         abs(b_co[2]-co[2])<tol and abs(b_co[3]-co[3])<tol)
    r = (abs(b_co[0]-co[2])<tol and abs(b_co[1]-co[3])<tol and
         abs(b_co[2]-co[0])<tol and abs(b_co[3]-co[1])<tol)
    return a or r

removes = []
hit_idx = set()
for st,en,b in parse_spans(t,"segment"):
    n = net_of(b); co = coords_of(b)
    for i,(net,kind,c) in enumerate(DEL):
        if kind=="seg" and match_del(net,kind,c,n,co):
            removes.append((st,en,f"{n} seg {co}")); hit_idx.add(i); break
for st,en,b in parse_spans(t,"via"):
    n = net_of(b); co = pt_of(b)
    if co is None: continue
    for i,(net,kind,c) in enumerate(DEL):
        if kind=="via" and match_del(net,kind,c,n,co):
            removes.append((st,en,f"{n} via {co}")); hit_idx.add(i); break

removes.sort(reverse=True)
print(f"removing {len(removes)} items")
for st,en,label in removes:
    t = t[:st] + t[en:]
for i,(net,kind,c) in enumerate(DEL):
    if i not in hit_idx:
        print("  MISS:",net,kind,c)

# ---------- UNLOCK SCL F lane (walls IO47/VCC) ----------
def unlock(t, net, co, tol=0.35):
    for st,en,b in parse_spans(t,"segment"):
        n = net_of(b); bc = coords_of(b)
        if n==net and match_del(net,"seg",co,n,bc,tol):
            nb = b.replace("(locked)","")
            nb = re.sub(r'\(locked yes\)','',nb)
            return t[:st]+nb+t[en:]
    return t

for co in [(43.09,27.0,52.15,27.0),(52.15,27.0,52.15,30.5),(52.15,30.5,50.95,30.5)]:
    t = unlock(t,"SCL",co)

# ---------- ADDS ----------
def seg(net,layer,x1,y1,x2,y2,w=0.25,locked=False):
    lk = " (locked)" if locked else ""
    return (f'\t(segment\n\t\t(start {x1} {y1})\n\t\t(end {x2} {y2})\n'
            f'\t\t(width {w})\n\t\t(layer "{layer}")\n\t\t(net "{net}"){lk}\n\t\t(tstamp {abs(hash((net,x1,y1,x2,y2)))%10**10:010d})\n\t)\n')
def via(net,x,y,locked=False):
    lk = " (locked)" if locked else ""
    return (f'\t(via\n\t\t(at {x} {y})\n\t\t(size 0.7)\n\t\t(drill 0.35)\n'
            f'\t\t(layers "F.Cu" "B.Cu")\n\t\t(net "{net}"){lk}\n'
            f'\t\t(tstamp {abs(hash((net,x,y)))%10**10:010d})\n\t)\n')

ADDS = []
# SCL: U2.1 pad join via via(30.4,32.5)
ADDS.append(("SCL","seg",(30.4,32.5,30.8,32.3),"F.Cu"))
ADDS.append(("SCL","seg",(30.8,32.3,31.15,32.25),"F.Cu"))
ADDS.append(("SCL","via",(30.4,32.5),None))
ADDS.append(("SCL","seg",(30.4,32.5,30.7,32.0),"B.Cu"))
ADDS.append(("SCL","seg",(30.7,32.0,30.5216,31.9983),"B.Cu"))
# SCL: island B -> main (B.Cu corridor via x28.4/x37.7)
for p in [(26.8,21.365,28.4,21.365),(28.4,21.365,28.4,26.4),(28.4,26.4,37.7,26.4),
          (37.7,26.4,37.7,29.5),(37.7,29.5,32.6428,30.0387)]:
    ADDS.append(("SCL","seg",p,"B.Cu"))
# SCL: U3.1 hop
ADDS.append(("SCL","via",(20.3,29.3),None))
for p in [(20.3,29.3,19.4,29.3),(19.4,29.3,19.4,34.0),(19.4,34.0,24.5,34.0),
          (24.5,34.0,24.5,26.4),(24.5,26.4,28.4,26.4)]:
    ADDS.append(("SCL","seg",p,"B.Cu"))
# VCC: island B<->C vert
for p in [(28.05,29.5,27.9,29.5),(27.9,29.5,27.9,35.3),(27.9,35.3,28.05,35.3)]:
    ADDS.append(("VCC_3V3","seg",p,"F.Cu"))
# VCC: J10.12 island -> C
for p in [(36.54,53.9,38.3,53.9),(38.3,53.9,38.3,48.3),(38.3,48.3,35.3133,47.4964)]:
    ADDS.append(("VCC_3V3","seg",p,"B.Cu"))
# VCC: AB->A via B corridor + via(58.5,24)
for p in [(31.83,27.6,46.0,24.5),(46.0,24.5,47.2,24.0),(47.2,24.0,58.5,22.5),(58.5,22.5,58.5,24.0)]:
    ADDS.append(("VCC_3V3","seg",p,"B.Cu"))
ADDS.append(("VCC_3V3","via",(58.5,24.0),None))
# VCC: E stub join
for p in [(58.5,24.0,55.9,24.0),(55.9,24.0,55.9,28.5),(55.9,28.5,67.3,28.5),
          (67.3,28.5,67.3,32.4),(67.3,32.4,58.55,32.4),(58.55,32.4,58.55,31.8)]:
    ADDS.append(("VCC_3V3","seg",p,"F.Cu"))
# VCC: E2 (AB<->E)
for p in [(49.05,30.5,49.05,32.6),(49.05,32.6,58.55,32.6)]:
    ADDS.append(("VCC_3V3","seg",p,"F.Cu"))
# VCC: F east cluster
for p in [(67.56,28.54,67.9,28.9),(67.9,28.9,69.0,30.8),(69.0,30.8,69.0,31.2),
          (69.0,31.2,72.2,31.2),(72.2,31.2,72.2,21.0),(72.2,21.0,67.85,21.0),(67.85,21.0,67.85,22.0)]:
    ADDS.append(("VCC_3V3","seg",p,"F.Cu"))
# IO47: F west leg + via(55.8,27.5) onto B chain
for p in [(49.7385,24.8702,48.5,25.0),(48.5,25.0,48.5,25.9),(48.5,25.9,53.8,25.9),(53.8,25.9,55.8,27.5)]:
    ADDS.append(("EXP_IO47","seg",p,"F.Cu"))
ADDS.append(("EXP_IO47","via",(55.8,27.5),None))
ADDS.append(("EXP_IO47","seg",(55.8,27.5,56.0835,31.2152),"B.Cu"))
# IO41: B chain -> J10.4 stub (gap x42.52-43.26)
for p in [(45.43,48.3051,42.89,48.3051),(42.89,48.3051,42.89,50.5059)]:
    ADDS.append(("EXP_IO41","seg",p,"B.Cu"))
# XIO15: deep corridor
for p in [(53.05,46.8,53.05,40.9),(53.05,40.9,39.3,40.9),(39.3,40.9,39.3,46.4),
          (39.3,46.4,48.2,46.4),(48.2,46.4,48.2,46.9),(48.2,46.9,49.2,46.9)]:
    ADDS.append(("XIO15","seg",p,"F.Cu"))
# SIG jogs
ADDS.append(("SIG_N","seg",(13.95,18.6,13.45,18.05),"F.Cu"))
ADDS.append(("SIG2_N","seg",(15.3,21.365,14.5,21.4),"F.Cu"))

add_txt = ""
for net,kind,co,layer in ADDS:
    if kind=="via": add_txt += via(net,co[0],co[1])
    else: add_txt += seg(net,layer,*co)

# insert before final closing paren
idx = t.rfind(")")
t = t[:idx] + add_txt + t[idx:]

# ---------- keepout shrink x26->x19 ----------
# find the B.Cu keepout zone polygon pts and rewrite x=26.0 -> 19.0 for y>24 area
def shrink_keepout(t):
    for st,en,b in parse_spans(t,"zone"):
        if "keepout" not in b or 'B.Cu' not in b: continue
        if '(tracks (not_allowed))' not in b: continue
        # only the analog one (has pts with x<=0.5)
        if "(xy 0.5 " not in b and "(xy 0 " not in b: continue
        nb = b.replace("(xy 26.0 ","(xy 19.0 ")
        nb = nb.replace("(xy 26 ","(xy 19 ")
        return t[:st]+nb+t[en:]
    return t
t = shrink_keepout(t)

open(PCB,"w").write(t)
print("adds:",len(ADDS))
