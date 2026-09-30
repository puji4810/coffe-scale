#!/usr/bin/env python3
"""Per-net island audit on merged s3.1.kicad_pcb.
Builds a union-find over segments/vias/pads (same layer touching = joined;
vias and PTH pads join F<->B). Reports nets split into >1 island with the
island endpoint coordinates so gaps can be routed."""
import re, sys
from collections import defaultdict

PT = open(sys.argv[1] if len(sys.argv) > 1 else "s3.1.kicad_pcb").read()

def blocks(text, head):
    out = []
    for m in re.finditer(re.escape(head), text):
        d, i = 0, m.start()
        while i < len(text):
            if text[i] == '(': d += 1
            elif text[i] == ')':
                d -= 1
                if d == 0:
                    out.append((m.start(), i + 1, text[m.start():i + 1]))
                    break
            i += 1
    return out

items = []  # (net, kind, geom)  kind: seg/via/pad ; geom for distance/touch checks
for s, e, blk in blocks(PT, "(segment"):
    nmm = re.search(r'\(net "([^"]+)"\)', blk)
    st = re.search(r'\(start ([-\d.]+) ([-\d.]+)\)', blk)
    en = re.search(r'\(end ([-\d.]+) ([-\d.]+)\)', blk)
    lay = re.search(r'\(layer "([^"]+)"\)', blk)
    if nmm and st and en and lay:
        items.append((nmm.group(1), "seg",
                      (float(st.group(1)), float(st.group(2)),
                       float(en.group(1)), float(en.group(2)), lay.group(1))))
for s, e, blk in blocks(PT, "(via"):
    nmm = re.search(r'\(net "([^"]+)"\)', blk)
    a = re.search(r'\(at ([-\d.]+) ([-\d.]+)\)', blk)
    if nmm and a:
        items.append((nmm.group(1), "via", (float(a.group(1)), float(a.group(2)))))

# pads: footprint-local -> absolute. Only SMD on F.Cu / PTH on both.
for s, e, blk in blocks(PT, "(footprint"):
    fm = re.search(r'\(footprint "([^"]+)"', blk)
    am = re.search(r'\n\t\t\(at ([-\d.]+) ([-\d.]+)( ([-\d.]+))?\)', blk)
    ref = re.search(r'\(property "Reference" "([^"]+)"', blk)
    if not (fm and am):
        continue
    fx, fy, rot = float(am.group(1)), float(am.group(2)), float(am.group(4) or 0)
    import math
    cr, sr = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    for pm in re.finditer(r'\(pad "([^"]+)" (smd|thru_hole)[^\n]*\n(?:.*\n){0,6}?\t\t\t\)', blk):
        pa = re.search(r'\(at ([-\d.]+) ([-\d.]+)( [-\d.]+)?\)', pm.group(0))
        sz = re.search(r'\(size ([-\d.]+) ([-\d.]+)\)', pm.group(0))
        nm = re.search(r'\(net "([^"]+)"\)', pm.group(0))
        if not (pa and sz and nm):
            continue
        lx, ly = float(pa.group(1)), float(pa.group(2))
        ax = fx + lx * cr - ly * sr
        ay = fy + lx * sr + ly * cr
        w, h = float(sz.group(1)) / 2, float(sz.group(2)) / 2
        layers = "FB" if pm.group(2) == "thru_hole" else "F"
        items.append((nm.group(1), "pad", (ax, ay, w, h, layers,
                                           f"{ref.group(1)}.{pm.group(1)}" if ref else "?")))

# union-find with touch predicates
def touch(a, b):
    (na, ka, ga), (nb, kb, gb) = a, b
    if na != nb:
        return False
    def layerok(la, lb):
        # both must share a layer (via/pth are on both)
        A = {"F.Cu", "B.Cu"} if la in ("FB", "V") else {la}
        B = {"F.Cu", "B.Cu"} if lb in ("FB", "V") else {lb}
        return bool(A & B)
    def pt_seg(p, s):
        # point to segment distance
        (x1, y1, x2, y2), (px, py) = s, p
        dx, dy = x2 - x1, y2 - y1
        L2 = dx * dx + dy * dy
        t = 0 if L2 == 0 else max(0, min(1, ((px - x1) * dx + (py - y1) * dy) / L2))
        cx, cy = x1 + t * dx, y1 + t * dy
        return (px - cx) ** 2 + (py - cy) ** 2 < 0.09
    if ka == "seg" and kb == "seg":
        (x1, y1, x2, y2, la), (u1, v1, u2, v2, lb) = ga, gb
        if not layerok(la, lb):
            return False
        for p in ((x1, y1), (x2, y2)):
            if pt_seg(p, (u1, v1, u2, v2)):
                return True
        for p in ((u1, v1), (u2, v2)):
            if pt_seg(p, (x1, y1, x2, y2)):
                return True
        return False
    if ka == "seg" and kb != "seg":
        return touch(b, a)
    if kb == "seg":
        (x, y) = ga[:2] if ka == "via" else ga[:2]
        if ka == "pad":
            (px, py, w, h, lays, _) = ga
            if not layerok(lays, gb[4]):
                return False
            (x1, y1, x2, y2) = gb[:4]
            # seg endpoint or body inside pad bbox
            for qx, qy in ((x1, y1), (x2, y2)):
                if px - w - .05 <= qx <= px + w + .05 and py - h - .05 <= qy <= py + h + .05:
                    return True
            mx = min(max((x1 + x2) / 2, px - w), px + w)
            my = min(max((y1 + y2) / 2, py - h), py + h)
            return pt_seg((mx, my), (x1, y1, x2, y2))
        # via point to segment
        if not layerok("V", gb[4]):
            return False
        return pt_seg((ga[0], ga[1]), gb[:4]) or \
            (abs(ga[0] - gb[0]) < 0.5 and abs(ga[1] - gb[1]) < 0.5) or \
            (abs(ga[0] - gb[2]) < 0.5 and abs(ga[1] - gb[3]) < 0.5)
    if ka == "via" and kb == "via":
        return (ga[0] - gb[0]) ** 2 + (ga[1] - gb[1]) ** 2 < 0.51
    if ka == "via" and kb == "pad":
        (x, y), (px, py, w, h, lays, _) = ga, gb
        return layerok("V", lays) and px - w - .36 <= x <= px + w + .36 and py - h - .36 <= y <= py + h + .36
    if ka == "pad" and kb == "via":
        return touch(b, a)
    if ka == "pad" and kb == "pad":
        (ax, ay, aw, ah, la, _), (bx, by, bw, bh, lb, _) = ga, gb
        return layerok(la, lb) and abs(ax - bx) < aw + bw + .05 and abs(ay - by) < ah + bh + .05
    return False

n = len(items)
par = list(range(n))
def find(x):
    while par[x] != x:
        par[x] = par[par[x]]
        x = par[x]
    return x
def union(a, b):
    ra, rb = find(a), find(b)
    if ra != rb:
        par[ra] = rb

by_net = defaultdict(list)
for i, it in enumerate(items):
    by_net[it[0]].append(i)

for net, idxs in sorted(by_net.items()):
    # O(k^2) within net only
    for a in range(len(idxs)):
        for b in range(a + 1, len(idxs)):
            if touch(items[idxs[a]], items[idxs[b]]):
                union(idxs[a], idxs[b])
    islands = defaultdict(list)
    for i in idxs:
        islands[find(i)].append(i)
    if len(islands) > 1:
        print(f"\n=== {net}: {len(islands)} islands")
        for root, mem in sorted(islands.items(), key=lambda kv: -len(kv[1])):
            pts = []
            for i in mem:
                g = items[i][2]
                if items[i][1] == "seg":
                    pts.append(f"seg{g[4][:1]}({g[0]:.1f},{g[1]:.1f})-({g[2]:.1f},{g[3]:.1f})")
                elif items[i][1] == "via":
                    pts.append(f"via({g[0]:.1f},{g[1]:.1f})")
                else:
                    pts.append(f"pad {g[5]}({g[0]:.1f},{g[1]:.1f})")
            print(f"   [{len(mem)}] " + " ".join(pts[:8]) + (" ..." if len(pts) > 8 else ""))
