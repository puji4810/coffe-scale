"""v0.3: unpierced aluminium pan with integral ribs and a downward skirt.

The LC1330's existing M6 threads are not changed. A small adapter is mounted
first, then M4 screws from below clamp it to blind threads in the pan.
"""
from build123d import Align, Box, Cylinder, Polygon, Pos, RectangleRounded, extrude, fillet
from dimensions import D

BOTTOM=(Align.CENTER,Align.CENTER,Align.MIN)


def block(w,d,h,x,y,z):
    return Pos(x,y,z)*Box(w,d,h,align=BOTTOM)


def cylinder(r,h,x,y,z):
    return Pos(x,y,z)*Cylinder(r,h,align=BOTTOM)


def rounded(w,d,h,r,x,y,z):
    return Pos(x,y,z)*extrude(RectangleRounded(w,d,r),h)


def pan_pockets():
    # Full-depth loading land spans adapter footprint X=39..67, Y=-13..43.
    xl,xr=-63,63
    yf,yr=-31,57
    hub_left=D.cell_hole_span/2-D.adapter_w/2
    front,rear=D.cell_y-D.adapter_d/2,D.cell_y+D.adapter_d/2
    profiles=[
        [(xl+8,yf),(xr-8,yf),(xr,yf+8),(xr,front),(hub_left,front),
         (hub_left,11),(xl,11),(xl,yf+8)],
        [(xl,19),(hub_left,19),(hub_left,rear),(xr,rear),(xr,yr-8),
         (xr-8,yr),(xl+8,yr),(xl,yr-8)],
    ]
    result=[]
    for pts in profiles:
        p=Polygon(*pts,align=None)
        result.append(fillet(p.vertices(),3))
    return result


def integrated_pan():
    core=rounded(134,96,D.pan_depth,6,0,13,D.pan_z)
    for profile in pan_pockets():
        core-=Pos(0,0,D.pan_z-1)*extrude(profile,D.pan_depth-D.pan_skin+1)
    # The uninterrupted upper surface is one piece with the core and skirt.
    roof=rounded(D.pan_w,D.pan_plan_depth,D.pan_skin,6,0,D.pan_y,D.pan_roof_z)
    skirt=rounded(D.pan_w,D.pan_plan_depth,D.pan_skirt_h,6,0,D.pan_y,D.pan_roof_z-D.pan_skirt_h)
    skirt-=rounded(D.pan_w-2*D.pan_skirt_wall,D.pan_plan_depth-2*D.pan_skirt_wall,
                   D.pan_skirt_h+2,4,0,D.pan_y,D.pan_roof_z-D.pan_skirt_h-1)
    pan=core+roof+skirt
    # Blind head wells accommodate the M6 screws already installed in adapter.
    for dy in (-D.cell_hole_pitch/2,D.cell_hole_pitch/2):
        pan-=cylinder(5.5,7.5,D.cell_hole_span/2,D.cell_y+dy,D.pan_z-1)
    # Upward M4 pilot bores; machining drawing includes drill-tip allowance.
    for x,y in D.pan_mounts:
        pan-=cylinder(1.65,D.pan_m4_pilot_depth+1,x,y,D.pan_z-1)
    return pan


def load_adapter():
    z=D.cell_z+D.cell_h
    adapter=rounded(D.adapter_w,D.adapter_d,D.adapter_t,2,D.cell_hole_span/2,D.cell_y,z)
    for dy in (-D.cell_hole_pitch/2,D.cell_hole_pitch/2):
        adapter-=cylinder(D.cell_clearance_d/2,D.adapter_t+2,D.cell_hole_span/2,D.cell_y+dy,z-1)
    for x,y in D.pan_mounts:
        adapter-=cylinder(2.2,D.adapter_t+2,x,y,z-1)
    return adapter


def dry_rim():
    outer=rounded(D.rim_outer_w,D.rim_outer_d,D.rim_top-D.wall_top,4,0,D.pan_y,D.wall_top)
    inner=rounded(D.rim_outer_w-2*D.rim_wall,D.rim_outer_d-2*D.rim_wall,
                  D.rim_top-D.wall_top+2,2,0,D.pan_y,D.wall_top-1)
    return outer-inner


def transition_ledge():
    s=rounded(D.pan_w,D.pan_plan_depth,2,6,0,D.pan_y,D.wall_top-2)
    s-=rounded(D.rim_outer_w-2*D.rim_wall,D.rim_outer_d-2*D.rim_wall,
               4,2,0,D.pan_y,D.wall_top-3)
    return s
