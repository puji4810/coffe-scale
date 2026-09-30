#!/usr/bin/env python3
# fix_routes16.py — geometry-verified repairs after freerouting plateau
# All adds were validated by route_check.py against post-rip state.
import re, math, sys

PCB = "s3.1.kicad_pcb"
src = open(PCB).read()

# ---------- parse helpers ----------
def find_blocks(text, head):
    """return list of (start,end) char spans of balanced-paren blocks starting with head"""
    spans=[]
    i=0
    while True:
        j=text.find(head,i)
        if j<0: break
        if j>0 and (text[j-1].isalnum() or text[j-1]=='_'):
            i=j+1;continue
        depth=0;k=j
        while k<len(text):
            c=text[k]
            if c=='(':depth+=1
            elif c==')':
                depth-=1
                if depth==0:
                    spans.append((j,k+1));break
            k+=1
        i=j+1
    return spans

def num(s):
    m=re.search(r'(-?\d+\.?\d*)',s)
    return float(m.group(1)) if m else None

def pts_of(block):
    m=re.search(r'\(start\s+(-?[\d.]+)\s+(-?[\d.]+)\)\s*\(end\s+(-?[\d.]+)\s+(-?[\d.]+)\)',block)
    if not m:return None
    return tuple(round(float(v),3) for v in m.groups())

def net_of(block):
    m=re.search(r'\(net\s+"?([^"\s)]+)"?',block)
    return m.group(1) if m else None

def layer_of(block):
    m=re.search(r'\(layer\s+"?([^"\s)]+)"?',block)
    return m.group(1) if m else None

def via_xy(block):
    m=re.search(r'\(at\s+(-?[\d.]+)\s+(-?[\d.]+)\)',block)
    return (round(float(m.group(1)),3),round(float(m.group(2)),3)) if m else None

# ---------- delete list ----------
# nets to rip ENTIRELY (all unlocked segments + vias): IO38 IO39 IO41 IO42 IO17 TXD BTN_TARE-B? 
# TXD/BTN kept. Rip full nets: EXP_IO38 EXP_IO39 EXP_IO40? -> IO40 needed by SDA vert spacing, keep.
RIP_NETS = {"EXP_IO38","EXP_IO39","EXP_IO40","EXP_IO41","EXP_IO42","EXP_IO17","EXP_IO14"}

DEL_SEGS = {  # (net, layer, x1,y1,x2,y2) exact
 ("Net-(J5-CC2)","F.Cu",(47.6,26.55,47.6,29.3)),
 ("Net-(J5-CC2)","F.Cu",(47.6,29.3,47.78,30.1)),
 ("Net-(J5-CC2)","F.Cu",(47.78,30.1,47.78,30.955)),
 ("Net-(J5-CC2)","F.Cu",(48.85,25.3,47.6,26.55)),
 ("GND","F.Cu",(54.469,31.729,54.95,31.248)),
 ("GND","F.Cu",(54.95,31.248,54.95,30.5)),
 # SDA old south fanout (replaced by new x48.55 arm)
 ("SDA","B.Cu",(46.7,52.54,46.7,51.313)),
 ("SDA","B.Cu",(46.7,51.313,47.122,51.313)),
 ("SDA","B.Cu",(47.122,51.313,47.927,50.508)),
 ("SDA","B.Cu",(47.927,50.508,47.927,49.047)),
 ("SDA","B.Cu",(47.927,49.047,47.18,48.301)),
 ("SDA","B.Cu",(47.18,48.301,47.18,45.343)),
 ("SDA","B.Cu",(47.18,45.343,47.18,44.84)),
 ("SDA","B.Cu",(47.18,44.84,48.282,43.738)),
 ("SDA","B.Cu",(47.18,45.343,42.229,45.343)),
 ("SDA","B.Cu",(42.229,45.343,40.075,43.19)),
 # VCC F stubs caging INT1 pad12 escape
 ("VCC_3V3","F.Cu",(33.05,28.651,31.57,30.131)),
 ("VCC_3V3","F.Cu",(31.57,30.131,31.57,30.686)),
 ("VCC_3V3","F.Cu",(31.57,30.686,29.353,30.686)),
 ("VCC_3V3","F.Cu",(29.353,30.686,28.995,30.329)),
 ("VCC_3V3","F.Cu",(28.995,30.329,28.05,30.329)),
 ("VCC_3V3","F.Cu",(31.57,30.771,32.572,31.773)),
 ("VCC_3V3","F.Cu",(32.572,31.773,32.75,31.773)),
 # locked SCL orphan lane (force delete)
 ("SCL","F.Cu",(43.095,27.0,52.15,27.0)),
 ("SCL","F.Cu",(52.15,27.0,52.15,30.5)),
 ("SCL","F.Cu",(52.15,30.5,50.95,30.5)),
}
DEL_VIAS = {
 ("GND",45.5,26.5),("GND",31.9,10.0),("GND",54.469,31.729),("GND",49.0,9.5),
 ("GND",33.4805,33.2252),
}

def approx(a,b,tol=0.01):return abs(a-b)<=tol

out=[]
pos=0
for s,e in find_blocks(src,"(segment"):
    blk=src[s:e]
    net=net_of(blk);lay=layer_of(blk);p=pts_of(blk)
    kill=False
    if net in RIP_NETS: kill=True
    elif p:
        key=(p[0],p[1],p[2],p[3])
        for (n,l,q) in DEL_SEGS:
            if net==n and lay==l and all(approx(a,b) for a,b in zip(key,q)):
                kill=True;break
    if not kill:
        out.append(src[pos:e])
    pos=e
out.append(src[pos:])
src="".join(out)

out=[];pos=0
for s,e in find_blocks(src,"(via"):
    blk=src[s:e]
    net=net_of(blk);v=via_xy(blk)
    kill=False
    if net in RIP_NETS: kill=True
    elif v and (net,v[0],v[1]) in DEL_VIAS: kill=True
    if not kill:
        out.append(src[pos:e])
    pos=e
out.append(src[pos:])
src="".join(out)

# ---------- adds ----------
def seg(net,layer,x1,y1,x2,y2,w=0.25):
    return f'  (segment (start {x1} {y1}) (end {x2} {y2}) (width {w}) (layer "{layer}") (net "{net}") (tstamp {abs(hash((net,layer,x1,y1,x2,y2)))%10**10:010d}))\n'
def via(net,x,y):
    return f'  (via (at {x} {y}) (size 0.7) (drill 0.35) (layers "F.Cu" "B.Cu") (net "{net}") (tstamp {abs(hash((net,x,y)))%10**10:010d}))\n'

ADDS=[
 # --- SDA: pad18 escape -> join x40.075 vert ---
 via("SDA",41.825,23.5),
 seg("SDA","B.Cu",41.825,23.5,41.825,42.7),
 seg("SDA","B.Cu",41.825,42.7,40.075,42.7),
 # --- SDA: J10.16 new fanout ---
 seg("SDA","B.Cu",48.282,43.738,48.282,44.5),
 seg("SDA","B.Cu",48.282,44.5,48.55,44.5),
 seg("SDA","B.Cu",48.55,44.5,47.97,45.6),
 seg("SDA","B.Cu",47.97,45.6,47.97,51.4),
 seg("SDA","B.Cu",47.97,51.4,46.7,51.4),
 seg("SDA","B.Cu",46.7,51.4,46.7,52.54),
 # --- SDA: layer-jump vias ---
 via("SDA",32.247,30.491),via("SDA",39.086,32.3),
 via("SDA",50.119,28.831),via("SDA",28.715,31.006),
 # --- SCL: pad19 escape via B.Cu x42.85 corridor ---
 via("SCL",43.095,23.5),
 seg("SCL","B.Cu",43.095,23.5,43.095,24.6),
 seg("SCL","B.Cu",43.095,24.6,42.85,24.6),
 seg("SCL","B.Cu",42.85,24.6,42.85,44.5),
 seg("SCL","B.Cu",42.85,44.5,45.75,44.5),
 seg("SCL","B.Cu",45.75,44.5,45.75,49.22),
 seg("SCL","B.Cu",45.75,49.22,45.43,49.22),
 # --- SCL: U3 west escape, north corridor y21 ---
 seg("SCL","F.Cu",20.3,29.3,19.5,29.3),
 seg("SCL","F.Cu",19.5,29.3,19.5,21.0),
 via("SCL",19.5,21.0),
 seg("SCL","B.Cu",19.5,21.0,28.4,21.0),
 seg("SCL","B.Cu",28.4,21.0,28.4,30.5),
 seg("SCL","B.Cu",28.4,30.5,30.2,33.6),
 seg("SCL","B.Cu",30.2,33.6,27.652,32.944),
 # --- INT1: B.Cu south loop + x46.5 hop over VCC horiz ---
 via("INT1",31.5,31.3),
 seg("INT1","F.Cu",31.75,32.25,31.75,31.9),
 seg("INT1","F.Cu",31.75,31.9,31.5,31.3),
 seg("INT1","B.Cu",31.5,31.3,33.5,31.0),
 seg("INT1","B.Cu",33.5,31.0,33.5,54.0),
 seg("INT1","B.Cu",33.5,54.0,50.3,54.0),
 seg("INT1","B.Cu",50.3,54.0,50.3,29.6),
 seg("INT1","B.Cu",50.3,29.6,46.5,29.6),
 seg("INT1","B.Cu",46.5,29.6,46.5,27.8),
 via("INT1",46.5,27.8),
 seg("INT1","F.Cu",46.5,27.8,45.635,27.8),
 seg("INT1","F.Cu",45.635,27.8,45.635,25.116),
 # --- RXD: link locked lane -> via(50.361,9.495) ---
 seg("RXD","B.Cu",48.373,10.188,48.373,9.6),
 seg("RXD","B.Cu",48.373,9.6,50.361,9.6),
 seg("RXD","B.Cu",50.361,9.6,50.361,9.495),
 # --- LCD_MOSI: B.Cu south lane -> J7.4 pad-gap entry ---
 seg("LCD_MOSI","F.Cu",49.664,6.313,49.7,7.0),
 seg("LCD_MOSI","F.Cu",49.7,7.0,49.7,1.5),
 via("LCD_MOSI",49.7,1.5),
 seg("LCD_MOSI","B.Cu",49.7,1.5,58.5,1.5),
 seg("LCD_MOSI","B.Cu",58.5,1.5,59.2,0.8),
 seg("LCD_MOSI","B.Cu",59.2,0.8,61.3,0.8),
 seg("LCD_MOSI","B.Cu",61.3,0.8,62.0,1.5),
 seg("LCD_MOSI","B.Cu",62.0,1.5,69.0,1.5),
 via("LCD_MOSI",69.0,1.5),
 seg("LCD_MOSI","F.Cu",69.0,1.5,69.0,3.5),
 seg("LCD_MOSI","F.Cu",69.0,3.5,70.0,3.5),
 # --- VCC: islandA link (vert x34.2 dodges GND via) ---
 seg("VCC_3V3","F.Cu",32.75,32.75,34.2,32.75),
 seg("VCC_3V3","F.Cu",34.2,32.75,34.2,36.5),
 via("VCC_3V3",34.2,36.5),
 seg("VCC_3V3","B.Cu",34.2,36.5,36.6,38.6),
 seg("VCC_3V3","B.Cu",36.6,38.6,38.158,38.114),
 # --- VCC: islandB (49.05,28.65) -> east stub, south loop around IO1/IO47 vias ---
 via("VCC_3V3",49.05,28.65),
 seg("VCC_3V3","B.Cu",49.05,28.65,51.0,30.8),
 seg("VCC_3V3","B.Cu",51.0,30.8,53.4,32.8),
 seg("VCC_3V3","B.Cu",53.4,32.8,53.4,40.7),
 seg("VCC_3V3","B.Cu",53.4,40.7,58.3,40.7),
 seg("VCC_3V3","B.Cu",58.3,40.7,58.3,27.2),
 via("VCC_3V3",58.3,27.2),
 seg("VCC_3V3","F.Cu",58.3,27.2,58.002,24.497),
 # --- GND stitch vias (fill zone islands) ---
 via("GND",75.0,15.0),via("GND",70.0,30.0),via("GND",80.0,30.0),
 via("GND",84.0,20.0),via("GND",64.0,50.0),via("GND",80.0,10.0),via("GND",86.0,40.0),
]

end=src.rstrip()
assert end.endswith(")"),"pcb tail broken"
src=end[:-1]+"\n"+"".join(ADDS)+")\n"
open(PCB,"w").write(src)
print("done")
