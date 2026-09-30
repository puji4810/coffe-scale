#!/usr/bin/env python3
"""Validate candidate routes against all copper/pads/vias in the PCB.
Usage: python3 tools/route_check.py  (edit CANDIDATES list below)
"""
import re, math, sys

PCB = "s3.1.kicad_pcb"
CLEAR = 0.20          # required copper clearance
VIA_R = 0.35          # via pad radius (0.7 dia)
VIA_DRILL_R = 0.175   # hole edge

txt = open(PCB).read()

def blocks(tag):
    out = []; i = 0
    while True:
        j = txt.find("(" + tag, i)
        if j < 0: break
        d = 0; k = j
        while k < len(txt):
            if txt[k] == "(": d += 1
            elif txt[k] == ")":
                d -= 1
                if d == 0: break
            k += 1
        out.append(txt[j:k+1]); i = k + 1
    return out

# ---------- load obstacles ----------
segs = []   # (net, layer, x1,y1,x2,y2, halfw)
vias = []   # (net, x, y)
pads = []   # (net, layers, x0,y0,x1,y1)   axis-aligned bbox
for b in blocks("segment"):
    nm = re.search(r'\(net "([^"]+)"\)', b)
    ly = re.search(r'\(layer "([^"]+)"\)', b)
    at = re.search(r'\(start ([-\d.]+) ([-\d.]+)\)\s*\(end ([-\d.]+) ([-\d.]+)\)', b)
    wd = re.search(r'\(width ([-\d.]+)\)', b)
    if not (nm and ly and at): continue
    x1, y1, x2, y2 = map(float, at.groups())
    w = float(wd.group(1)) if wd else 0.25
    segs.append((nm.group(1), ly.group(1), x1, y1, x2, y2, w / 2))
for b in blocks("via"):
    nm = re.search(r'\(net "([^"]+)"\)', b)
    at = re.search(r'\(at ([-\d.]+) ([-\d.]+)\)', b)
    sz = re.search(r'\(size ([-\d.]+)\)', b)
    if nm and at:
        r = float(sz.group(1)) / 2 if sz else VIA_R
        vias.append((nm.group(1), float(at.group(1)), float(at.group(2)), r))
for b in blocks("footprint"):
    at = re.search(r'\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\)', b)
    if not at: continue
    fx, fy = float(at.group(1)), float(at.group(2))
    rot = float(at.group(3)) if at.group(3) else 0.0
    ref = re.search(r'property "Reference" "([^"]+)"', b)
    refn = ref.group(1) if ref else "?"
    for p in blocks("pad") if False else []: pass
    # iterate pad sub-blocks inside footprint
    for pm in re.finditer(r'\(pad "([^"]+)"\s+([a-z_]+)\s+([a-z_]+)\s+\(at ([-\d.]+) ([-\d.]+)(?: ([-\d.]+))?\).*?\(size ([-\d.]+) ([-\d.]+)\).*?\(layers ([^)]*)\).*?\(net "([^"]+)"\)', b, re.S):
        pname, ptype, shape = pm.group(1), pm.group(2), pm.group(3)
        px, py = float(pm.group(4)), float(pm.group(5))
        prot = float(pm.group(6)) if pm.group(6) else 0.0
        sx, sy = float(pm.group(7)), float(pm.group(8))
        layers_str = pm.group(9)
        net = pm.group(10)
        # rotate pad offset by footprint rotation
        rr = math.radians(rot)
        ax = fx + px * math.cos(rr) - py * math.sin(rr)
        ay = fy + px * math.sin(rr) + py * math.cos(rr)
        # pad own rotation = footprint rot + pad rot
        pr = rot + prot
        # axis-aligned bbox (rotate size)
        prr = math.radians(pr)
        ex = abs(sx * math.cos(prr) / 2) + abs(sy * math.sin(prr) / 2)
        ey = abs(sx * math.sin(prr) / 2) + abs(sy * math.cos(prr) / 2)
        on_f = "F.Cu" in layers_str
        on_b = "B.Cu" in layers_str
        pads.append((net, refn, pname, ax - ex, ay - ey, ax + ex, ay + ey, on_f, on_b))

def seg_seg_dist(ax, ay, bx, by, cx, cy, dx, dy):
    # min distance between two segments
    def pt_seg(px, py, x1, y1, x2, y2):
        vx, vy = x2 - x1, y2 - y1
        L2 = vx * vx + vy * vy
        if L2 == 0: return math.hypot(px - x1, py - y1)
        t = max(0, min(1, ((px - x1) * vx + (py - y1) * vy) / L2))
        return math.hypot(px - x1 - t * vx, py - y1 - t * vy)
    # check intersection
    def ccw(ax, ay, bx, by, cx, cy): return (cy - ay) * (bx - ax) > (by - ay) * (cx - ax)
    inter = ccw(ax, ay, cx, cy, dx, dy) != ccw(bx, by, cx, cy, dx, dy) and \
            ccw(ax, ay, bx, by, cx, cy) != ccw(ax, ay, bx, by, dx, dy)
    if inter: return 0.0
    return min(pt_seg(ax, ay, cx, cy, dx, dy), pt_seg(bx, by, cx, cy, dx, dy),
               pt_seg(cx, cy, ax, ay, bx, by), pt_seg(dx, dy, ax, ay, bx, by))

def check_route(net, layer, pts, width=0.25, verbose=True):
    """pts = [(x,y),...] polyline. Returns list of problems."""
    hw = width / 2
    probs = []
    for i in range(len(pts) - 1):
        x1, y1 = pts[i]; x2, y2 = pts[i + 1]
        # vs segments (same layer only)
        for (n, l, a, b, c, d, ohw) in segs:
            if l != layer or n == net: continue
            dist = seg_seg_dist(x1, y1, x2, y2, a, b, c, d)
            if dist < hw + ohw + CLEAR - 1e-9:
                probs.append(f"{layer} seg ({x1:.2f},{y1:.2f})-({x2:.2f},{y2:.2f}) vs {n} ({a:.2f},{b:.2f})-({c:.2f},{d:.2f}): gap {dist - hw - ohw:.3f}")
        # vs vias
        for (n, vx, vy, vr) in vias:
            if n == net: continue
            dist = seg_seg_dist(x1, y1, x2, y2, vx, vy, vx, vy)
            if dist < hw + vr + CLEAR - 1e-9:
                probs.append(f"{layer} seg ({x1:.2f},{y1:.2f})-({x2:.2f},{y2:.2f}) vs via {n} ({vx:.2f},{vy:.2f}): gap {dist - hw - vr:.3f}")
        # vs pads
        for (n, ref, pn, x0, y0, x3, y3, onf, onb) in pads:
            if n == net: continue
            if layer == "F.Cu" and not onf: continue
            if layer == "B.Cu" and not onb: continue
            # segment vs pad bbox: min distance (capsule vs rect)
            corners = [(x0, y0), (x3, y0), (x3, y3), (x0, y3)]
            edges = [(x0, y0, x3, y0), (x3, y0, x3, y3), (x3, y3, x0, y3), (x0, y3, x0, y0)]
            dist = min(seg_seg_dist(x1, y1, x2, y2, e0, e1, e2, e3) for e0, e1, e2, e3 in edges)
            # inside check
            if x0 <= (x1 + x2) / 2 <= x3 and y0 <= (y1 + y2) / 2 <= y3: dist = 0.0
            if dist < hw + CLEAR - 1e-9:
                probs.append(f"{layer} seg ({x1:.2f},{y1:.2f})-({x2:.2f},{y2:.2f}) vs pad {ref}.{pn} {n} ({x0:.2f},{y0:.2f})-({x3:.2f},{y3:.2f}): gap {dist - hw:.3f}")
    return probs

def check_via(net, x, y):
    probs = []
    for (n, l, a, b, c, d, hw) in segs:
        if n == net: continue
        dist = seg_seg_dist(x, y, x, y, a, b, c, d)
        if dist < VIA_R + hw + CLEAR - 1e-9:
            probs.append(f"via {net}({x:.2f},{y:.2f}) vs {n} {l} ({a:.2f},{b:.2f})-({c:.2f},{d:.2f}): gap {dist - VIA_R - hw:.3f}")
    for (n, vx, vy, vr) in vias:
        if n == net: continue
        dist = math.hypot(x - vx, y - vy)
        if dist < VIA_R + vr + CLEAR - 1e-9:
            probs.append(f"via {net}({x:.2f},{y:.2f}) vs via {n}({vx:.2f},{vy:.2f}): gap {dist - VIA_R - vr:.3f}")
    for (n, ref, pn, x0, y0, x3, y3, onf, onb) in pads:
        if n == net: continue
        # via circle vs pad rect
        dx = max(x0 - x, 0, x - x3); dy = max(y0 - y, 0, y - y3)
        dist = math.hypot(dx, dy)
        if dist < VIA_R + CLEAR - 1e-9:
            probs.append(f"via {net}({x:.2f},{y:.2f}) vs pad {ref}.{pn} {n} ({x0:.2f},{y0:.2f})-({x3:.2f},{y3:.2f}): gap {dist - VIA_R:.3f}")
    return probs

if __name__ == "__main__":
    print(f"loaded {len(segs)} segs, {len(vias)} vias, {len(pads)} pads")
    CANDIDATES = [
        # ("NET", "LAYER", [(x,y),...], width)
    ]
    bad = 0
    for (net, layer, pts, w) in CANDIDATES:
        ps = check_route(net, layer, pts, w)
        if ps:
            bad += 1
            print(f"\n=== {net} {layer} {pts[0]}..{pts[-1]}")
            for p in ps: print("   ", p)
    if not bad: print("all clean")
