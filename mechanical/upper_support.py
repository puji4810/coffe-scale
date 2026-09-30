"""CNC-machined upper support; one solid with underside pockets.

The original 3 mm spider remains a comparison reference. All functions use
assembly coordinates. Pocket radii describe cutter access, not a stress proof.
"""
from build123d import Align, Box, Cone, Cylinder, Pos, Rot, Polygon, RectangleRounded, extrude, fillet
from dimensions import D
import math

BOTTOM = (Align.CENTER, Align.CENTER, Align.MIN)


def block(w, d, h, x, y, z):
    return Pos(x, y, z) * Box(w, d, h, align=BOTTOM)


def cylinder(r, h, x, y, z):
    return Pos(x, y, z) * Cylinder(r, h, align=BOTTOM)


def pocket_profiles():
    """Two rounded, connected pocket outlines with no zero-width separator fins."""
    xl = -D.upper_w/2 + D.upper_wall
    xr = D.upper_w/2 - D.upper_wall
    yf = D.upper_y - D.upper_d/2 + D.upper_wall
    yr = D.upper_y + D.upper_d/2 - D.upper_wall
    hub_left = D.cell_hole_span/2 - 12
    c = D.upper_boss_corner
    front = D.cell_y-D.upper_center_rib/2
    rear = D.cell_y+D.upper_center_rib/2
    hub_front, hub_rear = D.cell_y-19, D.cell_y+19
    vertices = [
        [(xl+c,yf),(xr-c,yf),(xr,yf+c),(xr,hub_front),
         (hub_left,hub_front),(hub_left,front),(xl,front),(xl,yf+c)],
        [(xl,rear),(hub_left,rear),(hub_left,hub_rear),(xr,hub_rear),
         (xr,yr-c),(xr-c,yr),(xl+c,yr),(xl,yr-c)],
    ]
    result = []
    for points in vertices:
        profile = Polygon(*points, align=None)
        result.append(fillet(profile.vertices(), D.upper_pocket_r))
    return result


def ribbed_support():
    if D.upper_depth < D.upper_m6_counterbore_depth + 3:
        raise ValueError('M6 head seat requires at least 3 mm of aluminium')
    if D.upper_m3_pilot_depth >= D.upper_depth:
        raise ValueError('M3 holes must remain blind')
    solid = Pos(0, D.upper_y, D.spider_z) * extrude(
        RectangleRounded(D.upper_w, D.upper_d, 6), D.upper_depth)
    for profile in pocket_profiles():
        tool = Pos(0, 0, D.spider_z-1) * extrude(profile, D.upper_depth-D.upper_skin+1)
        solid -= tool
    for dy in (-D.cell_hole_pitch/2, D.cell_hole_pitch/2):
        x, y = D.cell_hole_span/2, D.cell_y+dy
        solid -= cylinder(D.cell_clearance_d/2, D.upper_depth+2, x, y, D.spider_z-1)
        solid -= cylinder(D.upper_m6_counterbore_d/2,
                          D.upper_m6_counterbore_depth+1, x, y,
                          D.plate_z-D.upper_m6_counterbore_depth)
    for x, y in D.plate_mounts:
        # Ø2.5 pilot 8 mm deep; drawing calls out M3x0.5, 6 mm effective thread.
        # Helical threads and drill-tip cone are omitted from interference CAD.
        solid -= cylinder(1.25, D.upper_m3_pilot_depth+1, x, y,
                          D.plate_z-D.upper_m3_pilot_depth)
    return solid


def flat_reference(thickness=3):
    """Exact geometry of v0.1 upper spider, with its original mounting face Z."""
    x0 = D.cell_hole_span/2
    s = block(24, 30, thickness, x0, D.cell_y, D.spider_z)
    for x, y in D.plate_mounts:
        dx, dy = x-x0, y-D.cell_y
        s += (Pos((x+x0)/2, (y+D.cell_y)/2, D.spider_z)
              * Rot(0, 0, math.degrees(math.atan2(dy, dx)))
              * Box(math.hypot(dx, dy)+13, 13, thickness, align=BOTTOM))
        s += cylinder(9, thickness, x, y, D.spider_z)
    for dy in (-D.cell_hole_pitch/2, D.cell_hole_pitch/2):
        x, y = x0, D.cell_y+dy
        s -= cylinder(D.cell_clearance_d/2, thickness+2, x, y, D.spider_z-1)
        s -= Pos(x, y, D.spider_z+thickness-3) * Cone(3.3, 6.3, 3, align=BOTTOM)
    for x, y in D.plate_mounts:
        s -= cylinder(1.7, thickness+2, x, y, D.spider_z-1)
    return s
