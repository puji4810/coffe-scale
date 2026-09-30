#!/usr/bin/env python3
"""cleanup round2: delete remaining dead stubs with exact coords"""
import re
FN = "s3.1.kicad_pcb"
t = open(FN).read()
def pt(s): return round(float(s), 4)

DEL_ALL = {
 ("USB_VBUS",(69.55,43.0),(70.1691,43.6191)),
 ("SCL",(42.8696,24.8021),(43.095,24.8021)),
 ("SCL",(37.3669,48.2),(35.9,48.2)),
 ("SCL",(46.8538,22.2707),(47.3328,22.7497)),
 ("VCC_3V3",(29.6941,38.0846),(28.5057,38.0846)),
 ("VCC_3V3",(30.0138,20.9896),(25.9131,16.8889)),
 ("VCC_3V3",(25.9131,16.8889),(24.7637,16.8889)),
 ("VCC_3V3",(24.7637,16.8889),(16.3354,8.4606)),
 ("VCC_3V3",(16.3354,8.4606),(11.1317,8.4606)),
 ("VCC_3V3",(11.1317,8.4606),(10.4288,9.1635)),
 ("VCC_3V3",(29.5087,51.4435),(29.5087,48.0225)),
 ("BUZZ",(48.85,25.3),(47.6,26.55)),
 ("BUZZ",(47.6,26.55),(47.6,29.3)),
 ("BUZZ",(47.6,29.3),(47.78,30.1)),
 ("BUZZ",(47.78,30.1),(47.78,30.955)),
}

edits, hit = [], set()
for m in re.finditer(r'\(segment', t):
    d, i = 0, m.start()
    while i < len(t):
        if t[i] == '(': d += 1
        elif t[i] == ')':
            d -= 1
            if d == 0: break
        i += 1
    blk = t[m.start():i+1]
    nm_m = re.search(r'\(net "([^"]+)"', blk)
    nm = nm_m.group(1) if nm_m else '?'
    sm = re.search(r'\(start ([\d.-]+) ([\d.-]+)\)\s*\(end ([\d.-]+) ([\d.-]+)\)', blk)
    a, b = (pt(sm.group(1)), pt(sm.group(2))), (pt(sm.group(3)), pt(sm.group(4)))
    if (nm, a, b) in DEL_ALL or (nm, b, a) in DEL_ALL:
        edits.append((m.start(), i+1))
        hit.add((nm, frozenset((a, b))))

miss = DEL_ALL - hit
for s, e in sorted(edits, key=lambda x: -x[0]):
    t = t[:s] + t[e:]
print(f"deleted {len(edits)} segs, missed: {miss if miss else 'none'}")
open(FN,'w').write(t)
