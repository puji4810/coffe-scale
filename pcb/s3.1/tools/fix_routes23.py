#!/usr/bin/env python3
"""USB pair fix v3:
  - remove fix22's colliding adds (16 segs + 4 vias)
  - restore pre-existing clean DP_M/DM_M routes (minus DM_M duplicate seg)
  - keep new DM J5-side route: B col x72.2 parallel to DP + U8.1 via old via
"""
import re, uuid

FN = "s3.1.kicad_pcb"
t = open(FN).read()
U = lambda: str(uuid.uuid4())
def pt(s): return round(float(s), 4)

DEL = {
 # fix22 adds to remove
 ("USB_DM",(75.4516,44.4882),(75.7,44.0)),
 ("USB_DM",(75.7,44.0),(75.7,28.5)),
 ("USB_DM",(75.7,28.5),(67.2,20.2)),
 ("USB_DM",(67.2,20.2),(62.8,20.3)),
 ("USB_DM",(62.8,20.3),(61.4,19.0)),
 ("USB_DM",(61.4,19.0),(60.95,18.9)),
 ("USB_DM",None,(67.2,20.2)),
 ("USB_DM_M",(43.0876,20.8024),(43.0876,18.7)),
 ("USB_DM_M",(43.0876,18.7),(52.7,18.7)),
 ("USB_DM_M",(52.7,18.7),(52.7,21.15)),
 ("USB_DM_M",(52.7,21.15),(58.0,21.15)),
 ("USB_DM_M",(58.0,21.15),(58.9,18.9)),
 ("USB_DM_M",None,(58.9,18.7)),
 ("USB_DP_M",(50.0779,21.505),(52.4,21.4)),
 ("USB_DP_M",(52.4,21.4),(52.4,21.7)),
 ("USB_DP_M",(52.4,21.7),(58.0,21.7)),
 ("USB_DP_M",(58.0,21.7),(63.0,21.7)),
 ("USB_DP_M",(63.0,21.7),(63.0,19.4)),
 ("USB_DP_M",None,(52.4,21.4)),
 ("USB_DP_M",None,(63.0,19.4)),
}

edits = []
for m in re.finditer(r'\((segment|via)', t):
    d, i = 0, m.start()
    while i < len(t):
        if t[i] == '(': d += 1
        elif t[i] == ')':
            d -= 1
            if d == 0: break
        i += 1
    blk = t[m.start():i+1]
    nm_m = re.search(r'\(net "([^"]+)"', blk) or re.search(r'\(net (\d+)\)', blk)
    nm = nm_m.group(1) if nm_m else '?'
    sm = re.search(r'\(start ([\d.-]+) ([\d.-]+)\)\s*\(end ([\d.-]+) ([\d.-]+)\)', blk)
    if sm:
        a, b = (pt(sm.group(1)), pt(sm.group(2))), (pt(sm.group(3)), pt(sm.group(4)))
        if (nm, a, b) in DEL or (nm, b, a) in DEL:
            edits.append((m.start(), i+1, ""))
    else:
        am = re.search(r'\(at ([\d.-]+) ([\d.-]+)\)', blk)
        if am and (nm, None, (pt(am.group(1)), pt(am.group(2)))) in DEL:
            edits.append((m.start(), i+1, ""))

hit = set()
for s, e, r in edits:
    blk = t[s:e]
    nm_m = re.search(r'\(net "([^"]+)"', blk) or re.search(r'\(net (\d+)\)', blk)
    nm = nm_m.group(1) if nm_m else '?'
    sm = re.search(r'\(start ([\d.-]+) ([\d.-]+)\)\s*\(end ([\d.-]+) ([\d.-]+)\)', blk)
    am = re.search(r'\(at ([\d.-]+) ([\d.-]+)\)', blk)
    if sm: hit.add((nm,(pt(sm.group(1)),pt(sm.group(2))),(pt(sm.group(3)),pt(sm.group(4)))))
    elif am: hit.add((nm,None,(pt(am.group(1)),pt(am.group(2)))))
miss = [k for k in DEL if k not in hit and (k[0],k[2],k[1]) not in hit]

for s, e, r in sorted(edits, key=lambda x: -x[0]):
    t = t[:s] + r + t[e:]
print(f"deleted {len(edits)} items; MISS: {miss}")

NEW_SEGS = [
 # --- DM_M restore (B north corridor -> F col x58.39 -> pads) ---
 ("USB_DM_M","B.Cu",(43.0876,20.8024),(49.9347,13.8509),0.25),
 ("USB_DM_M","B.Cu",(49.9347,13.8509),(49.9347,13.5353),0.25),
 ("USB_DM_M","B.Cu",(49.9347,13.5353),(50.3312,13.1388),0.25),
 ("USB_DM_M","B.Cu",(50.3312,13.1388),(53.7984,13.1388),0.25),
 ("USB_DM_M","B.Cu",(53.7984,13.1388),(55.2859,14.6263),0.25),
 ("USB_DM_M","F.Cu",(55.2859,14.6263),(57.7168,14.6264),0.25),
 ("USB_DM_M","F.Cu",(57.7168,14.6264),(58.3863,15.2959),0.25),
 ("USB_DM_M","F.Cu",(58.3863,15.2959),(58.3863,18.05),0.25),
 ("USB_DM_M","F.Cu",(58.3863,18.05),(58.6,18.05),0.25),
 ("USB_DM_M","F.Cu",(58.6,18.05),(59.05,18.5),0.25),
 ("USB_DM_M","F.Cu",(56.5,18.05),(58.3863,18.05),0.25),
 # --- DP_M restore (B lane y22.91 + C23 branch) ---
 ("USB_DP_M","B.Cu",(64.4554,19.4054),(60.9488,22.912),0.25),
 ("USB_DP_M","B.Cu",(69.4156,24.3656),(64.4554,19.4054),0.25),
 ("USB_DP_M","B.Cu",(60.9488,22.912),(51.1889,22.912),0.25),
 ("USB_DP_M","B.Cu",(69.4156,25.5125),(69.4156,24.3656),0.25),
 ("USB_DP_M","B.Cu",(51.1889,22.912),(50.2486,21.9717),0.25),
 ("USB_DP_M","B.Cu",(50.2486,21.9717),(50.2486,21.6757),0.25),
 ("USB_DP_M","F.Cu",(64.4554,19.4054),(63.55,18.5),0.25),
 ("USB_DP_M","F.Cu",(69.4156,25.5125),(70.1625,25.5125),0.25),
 ("USB_DP_M","F.Cu",(70.1625,25.5125),(70.5,25.85),0.25),
 # --- NEW DM J5-side: B col x72.2 parallel to DP, U8.1 via old via ---
 ("USB_DM","B.Cu",(74.0639,44.4882),(72.2,44.49),0.25),
 ("USB_DM","B.Cu",(72.2,44.49),(72.2,28.6),0.25),
 ("USB_DM","B.Cu",(72.2,28.6),(65.9,20.6),0.25),
 ("USB_DM","F.Cu",(65.9,20.6),(65.9,20.2),0.25),
 ("USB_DM","F.Cu",(65.9,20.2),(62.8,20.2),0.25),
 ("USB_DM","F.Cu",(62.8,20.2),(61.4,19.0),0.25),
 ("USB_DM","F.Cu",(61.4,19.0),(60.95,18.9),0.25),
 ("USB_DM","B.Cu",(75.4516,44.4882),(75.7,44.49),0.25),
 ("USB_DM","B.Cu",(75.7,44.49),(75.7,39.0),0.25),
 ("USB_DM","B.Cu",(75.7,39.0),(78.699,39.0),0.25),
 ("USB_DM","B.Cu",(78.699,39.0),(78.699,38.7135),0.25),
 ("USB_DM","F.Cu",(78.699,38.7135),(77.8625,39.55),0.25),
]
NEW_VIAS = [
 ("USB_DM",65.9,20.6),
 ("USB_DM",78.699,38.7135),
 ("USB_DM_M",55.2859,14.6263),
 ("USB_DP_M",50.2486,21.6757),
 ("USB_DP_M",64.4554,19.4054),
 ("USB_DP_M",69.4156,25.5125),
]

new = []
for net,ly,(x1,y1),(x2,y2),w in NEW_SEGS:
    new.append(f'''(segment
		(start {x1} {y1})
		(end {x2} {y2})
		(width {w})
		(layer "{ly}")
		(net "{net}")
		(uuid "{U()}")
	)''')
for net,x,y in NEW_VIAS:
    new.append(f'''(via
		(at {x} {y})
		(size 0.7)
		(drill 0.35)
		(layers "F.Cu" "B.Cu")
		(net "{net}")
		(uuid "{U()}")
	)''')

t = t.rstrip()
assert t.endswith(')')
t = t[:-1] + '\n\t' + '\n\t'.join(new) + '\n)\n'
open(FN,'w').write(t)
print(f"added {len(new)} items")
