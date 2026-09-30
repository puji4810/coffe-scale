#!/usr/bin/env python3
"""restore original DM route (only corridor that clears the B-layer diagonal jungle);
remove fix24's failed attempts."""
import re, uuid
FN = "s3.1.kicad_pcb"
t = open(FN).read()
U = lambda: str(uuid.uuid4())
def pt(s): return round(float(s), 4)

DEL = {
 ("USB_DM",(74.0639,44.4882),(72.2,44.49)),
 ("USB_DM",(72.2,44.49),(72.2,28.6)),
 ("USB_DM",(72.2,28.6),(62.0,19.4)),
 ("USB_DM",(62.0,19.4),(61.4,19.0)),
 ("USB_DM",None,(62.0,19.4)),
 ("USB_DM",(75.4516,44.4882),(77.2,44.49)),
 ("USB_DM",(77.2,44.49),(77.2,39.0)),
 ("USB_DM",(77.2,39.0),(78.699,39.0)),
 ("USB_DM",(78.699,39.0),(78.699,38.7135)),
 ("USB_DM",(78.699,38.7135),(77.8625,39.55)),
 ("USB_DM",None,(78.699,38.7135)),
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
    if nm != 'USB_DM': continue
    sm = re.search(r'\(start ([\d.-]+) ([\d.-]+)\)\s*\(end ([\d.-]+) ([\d.-]+)\)', blk)
    if sm:
        a, b = (pt(sm.group(1)), pt(sm.group(2))), (pt(sm.group(3)), pt(sm.group(4)))
        if (nm, a, b) in DEL or (nm, b, a) in DEL:
            edits.append((m.start(), i+1, ""))
    else:
        am = re.search(r'\(at ([\d.-]+) ([\d.-]+)\)', blk)
        if am and (nm, None, (pt(am.group(1)), pt(am.group(2)))) in DEL:
            edits.append((m.start(), i+1, ""))
for s, e, r in sorted(edits, key=lambda x: -x[0]):
    t = t[:s] + r + t[e:]
print(f"deleted {len(edits)}")

NEW_SEGS = [
 ("USB_DM","B.Cu",(75.4516,44.4882),(78.699,41.2408),0.25),
 ("USB_DM","B.Cu",(78.699,41.2408),(78.699,38.7135),0.25),
 ("USB_DM","B.Cu",(78.699,38.7135),(78.699,30.7167),0.25),
 ("USB_DM","B.Cu",(78.699,30.7167),(66.6977,18.7154),0.25),
 ("USB_DM","B.Cu",(66.6977,18.7154),(62.5634,18.7154),0.25),
 ("USB_DM","B.Cu",(62.5634,18.7154),(61.8652,19.4136),0.25),
 ("USB_DM","F.Cu",(78.699,38.7135),(77.8625,39.55),0.25),
 ("USB_DM","F.Cu",(61.8636,19.4136),(60.95,18.5),0.25),
]
NEW_VIAS = [
 ("USB_DM",78.699,38.7135),
 ("USB_DM",61.8652,19.4136),
]
new=[]
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
t=t.rstrip();assert t.endswith(')')
t=t[:-1]+'\n\t'+'\n\t'.join(new)+'\n)\n'
open(FN,'w').write(t)
print(f"added {len(new)}")
