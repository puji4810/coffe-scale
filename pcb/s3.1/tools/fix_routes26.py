#!/usr/bin/env python3
"""cleanup: delete dead-end stubs + one copy of each BUZZ duplicate"""
import re
FN = "s3.1.kicad_pcb"
t = open(FN).read()
def pt(s): return round(float(s), 4)

# (net, a, b) — delete every match
DEL_ALL = {
 ("USB_VBUS",(69.55,43.0),(70.169,43.619)),
 ("SCL",(52.15,30.5),(50.95,30.5)),
 ("SCL",(42.87,24.802),(43.095,24.802)),
 ("SCL",(37.81,48.64),(37.37,48.2)),
 ("SCL",(37.37,48.2),(35.9,48.2)),
 ("SCL",(35.9,48.2),(30.0,48.2)),
 ("SCL",(30.0,48.2),(30.0,51.27)),
 ("SCL",(46.854,22.271),(47.333,22.75)),
 ("SCL",(20.419,21.365),(20.7,21.365)),
 ("SCL",(20.7,21.365),(26.8,21.365)),
 ("VCC_3V3",(29.694,38.085),(28.506,38.085)),
 ("VCC_3V3",(30.014,20.99),(25.913,16.889)),
 ("VCC_3V3",(25.913,16.889),(24.76,16.89)),
 ("VCC_3V3",(24.76,16.89),(16.34,8.46)),
 ("VCC_3V3",(16.34,8.46),(11.13,8.46)),
 ("VCC_3V3",(11.13,8.46),(10.43,9.16)),
 ("VCC_3V3",(29.509,48.023),(29.509,51.444)),
 ("EXP_IO1",(55.59,49.6),(55.59,53.3)),
 ("XIO15",(49.24,50.9),(49.24,51.27)),
}
# BUZZ duplicates — delete ONLY the first occurrence of each
DEL_ONCE = {
 ("BUZZ",(48.85,25.3),(47.6,26.55)),
 ("BUZZ",(47.6,26.55),(47.6,29.3)),
 ("BUZZ",(47.6,29.3),(47.78,30.1)),
 ("BUZZ",(47.78,30.1),(47.78,30.955)),
}

edits = []
seen_once = set()
for m in re.finditer(r'\((segment|via)', t):
    if m.group(1) != 'segment': continue
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
    if not sm: continue
    a, b = (pt(sm.group(1)), pt(sm.group(2))), (pt(sm.group(3)), pt(sm.group(4)))
    if (nm, a, b) in DEL_ALL or (nm, b, a) in DEL_ALL:
        edits.append((m.start(), i+1, ""))
    elif (nm, a, b) in DEL_ONCE or (nm, b, a) in DEL_ONCE:
        key = (nm, frozenset((a, b)))
        if key not in seen_once:
            seen_once.add(key)
            edits.append((m.start(), i+1, ""))

for s, e, r in sorted(edits, key=lambda x: -x[0]):
    t = t[:s] + r + t[e:]
print(f"deleted {len(edits)} segs (DEL_ONCE hit {len(seen_once)}/4)")
open(FN,'w').write(t)
