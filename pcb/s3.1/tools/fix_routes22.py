#!/usr/bin/env python3
"""USB pair cleanup v2 (exact coords):
  rip DM east detour + DM_M north detour + DP_M far-east detour + junk
  new: DM parallel to DP on B x75.7; DM_M/DP_M B pair y21.15/y21.7
"""
import re, uuid

FN = "s3.1.kicad_pcb"
t = open(FN).read()
U = lambda: str(uuid.uuid4())
def pt(s): return round(float(s), 4)

DEL = {
 ("USB_DM_M", (58.3863,15.2959), (57.7168,14.6264)),
 ("USB_DM_M", (57.7168,14.6264), (55.2859,14.6264)),
 ("USB_DM_M", (55.2859,14.6263), (57.7167,14.6263)),
 ("USB_DM_M", (58.3863,18.05), (58.3863,15.2959)),
 ("USB_DM_M", (58.3863,18.05), (58.6,18.05)),
 ("USB_DM_M", (58.6,18.05), (59.05,18.5)),
 ("USB_DM_M", (56.5,18.05), (58.3863,18.05)),
 ("USB_DM_M", (53.7984,13.1388), (50.3312,13.1388)),
 ("USB_DM_M", (55.2859,14.6263), (53.7984,13.1388)),
 ("USB_DM_M", (49.9347,13.5353), (49.9347,13.8509)),
 ("USB_DM_M", (49.9347,13.8509), (43.0876,20.698)),
 ("USB_DM_M", (50.3312,13.1388), (49.9347,13.5353)),
 ("USB_DM_M", None, (55.2859,14.6263)),
 ("USB_DP_M", (64.4554,19.4054), (60.9488,22.912)),
 ("USB_DP_M", (69.4156,24.3656), (64.4554,19.4054)),
 ("USB_DP_M", (60.9488,22.912), (51.1889,22.912)),
 ("USB_DP_M", (69.4156,25.5125), (69.4156,24.3656)),
 ("USB_DP_M", (51.1889,22.912), (50.2486,21.9717)),
 ("USB_DP_M", (50.2486,21.9717), (50.2486,21.6757)),
 ("USB_DP_M", (64.4554,19.4054), (63.55,18.5)),
 ("USB_DP_M", (70.1625,25.5125), (70.5,25.85)),
 ("USB_DP_M", (69.4156,25.5125), (70.1625,25.5125)),
 ("USB_DP_M", None, (69.4156,25.5125)),
 ("USB_DP_M", None, (64.4554,19.4054)),
 ("USB_DP_M", None, (50.2486,21.6757)),
 ("USB_DM", (78.699,38.7135), (77.8625,39.55)),
 ("USB_DM", (60.95,18.5), (61.8636,19.4136)),
 ("USB_DM", (66.6977,18.7154), (62.5634,18.7154)),
 ("USB_DM", (78.699,38.7135), (78.699,30.7167)),
 ("USB_DM", (78.699,30.7167), (66.6977,18.7154)),
 ("USB_DM", (78.699,41.2408), (75.4516,44.4882)),
 ("USB_DM", (62.5634,18.7154), (61.8652,19.4136)),
 ("USB_DM", (78.699,38.7135), (78.699,41.2408)),
 ("USB_DM", None, (61.8652,19.4136)),
 ("USB_DM", None, (78.699,38.7135)),
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
 ("USB_DM","B.Cu",(75.4516,44.4882),(75.70,44.00),0.25),
 ("USB_DM","B.Cu",(75.70,44.00),(75.70,28.50),0.25),
 ("USB_DM","B.Cu",(75.70,28.50),(67.20,20.20),0.25),
 ("USB_DM","F.Cu",(67.20,20.20),(62.80,20.30),0.25),
 ("USB_DM","F.Cu",(62.80,20.30),(61.40,19.00),0.25),
 ("USB_DM","F.Cu",(61.40,19.00),(60.95,18.90),0.25),
 ("USB_DM_M","B.Cu",(43.0876,20.8024),(43.0876,18.70),0.25),
 ("USB_DM_M","B.Cu",(43.0876,18.70),(52.70,18.70),0.25),
 ("USB_DM_M","B.Cu",(52.70,18.70),(52.70,21.15),0.25),
 ("USB_DM_M","B.Cu",(52.70,21.15),(58.00,21.15),0.25),
 ("USB_DM_M","B.Cu",(58.00,21.15),(58.90,18.90),0.25),
 ("USB_DP_M","F.Cu",(50.0779,21.505),(52.40,21.40),0.25),
 ("USB_DP_M","B.Cu",(52.40,21.40),(52.40,21.70),0.25),
 ("USB_DP_M","B.Cu",(52.40,21.70),(58.00,21.70),0.25),
 ("USB_DP_M","B.Cu",(58.00,21.70),(63.00,21.70),0.25),
 ("USB_DP_M","B.Cu",(63.00,21.70),(63.00,19.40),0.25),
]
NEW_VIAS = [
 ("USB_DM",67.20,20.20),
 ("USB_DM_M",58.90,18.70),
 ("USB_DP_M",52.40,21.40),
 ("USB_DP_M",63.00,19.40),
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
