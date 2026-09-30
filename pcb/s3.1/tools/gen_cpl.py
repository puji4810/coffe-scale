#!/usr/bin/env python3
"""Regenerate scale-adc-s3-jlc-cpl.csv with PHYSICAL rotation, not instance rot.

The custom 0805 variants (SMD_0805_V/_VR/_V_OE/_V_OW) bake a 90-degree pad
turn inside the footprint, so the KiCad instance angle does not match the
mounted orientation. For 2-pin R/C parts we compute the pad-center axis from
the board's own pad geometry: long axis horizontal = 0 deg, vertical = 90 deg
(non-polar parts are 180-degree symmetric). Polar/multi-pin parts keep the
instance rotation; verify those in the JLC pick preview (pin-1 marks).
"""
import math, os, re

DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PCB = os.path.join(DIR, "s3.1.kicad_pcb")
OUT = os.path.join(DIR, "s3.1-jlc-cpl.csv")

txt = open(PCB).read()

def rot_pt(x, y, deg):
    a = math.radians(deg)
    return (x * math.cos(a) - y * math.sin(a),
            x * math.sin(a) + y * math.cos(a))

rows = []
for m in re.finditer(r'\(footprint "s3-1:([^"]+)"\n(?:\t\t[^\n]*\n)+?\t\)', txt):
    blk = m.group(0)
    ref = re.search(r'\(property "Reference" "([^"]+)"', blk).group(1)
    val = re.search(r'\(property "Value" "([^"]+)"', blk).group(1)
    at = re.search(r'\n\t\t\(at ([\d.\-]+) ([\d.\-]+)(?: ([\d.\-]+))?\)', blk)
    x, y, irot = float(at.group(1)), float(at.group(2)), float(at.group(3) or 0)
    if 'dnp' in blk or 'exclude_from_bom' in blk or 'exclude_from_pos_files' in blk:
        continue                                    # DNP (C23/C24), TPs
    if ref.startswith(('TP', 'MH')):
        continue                                    # test points, mounting holes
    pads = re.findall(r'\(pad "([^"]+)" (\w+) \w+\n\s*\(at ([\d.\-]+) ([\d.\-]+)',
                      blk)
    if not any(p[1] == 'smd' for p in pads):
        continue                                    # THT-only: J2/J6/J7/J8
    # physical rotation: for 2-pin passives use the pad-center axis on the board
    rot = irot
    smd = [(float(px), float(py)) for pn, ty, px, py in pads if ty == 'smd']
    if len(smd) == 2 and ref[0] in 'RC':
        (ax, ay), (bx, by) = (rot_pt(*smd[0], deg=irot), rot_pt(*smd[1], deg=irot))
        axis = math.degrees(math.atan2(by - ay, bx - ax)) % 180
        rot = 90.0 if 45 < axis < 135 else 0.0
    rows.append((ref, val, m.group(1), x, y, rot))

def key(r):
    p = re.match(r'([A-Z]+)(\d+)', r[0])
    return (p.group(1), int(p.group(2))) if p else (r[0], 0)
rows.sort(key=key)

with open(OUT, "w") as f:
    f.write("Designator,Comment,Footprint,Mid X,Mid Y,Rotation,Layer\n")
    for ref, val, fp, x, y, rot in rows:
        f.write(f'"{ref}","{val}","{fp}",{x:.4f},{-y:.4f},{rot:.1f},Top\n')
print(f"wrote {len(rows)} placements -> {OUT}")
