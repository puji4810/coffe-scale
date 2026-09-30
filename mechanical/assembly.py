"""Build and verify the concept assembly. Run from any directory.

Normal builds use the stored PCB snapshot; --refresh-pcb is explicitly read-only
against the live PCB file. --frame butterfly exports the alternate lower frame.
"""
import argparse
from dataclasses import asdict
from itertools import combinations
import json
import math
from pathlib import Path

from build123d import (Align, Box, Color, Compound, Cone, Cylinder, Pos, Rot,
                      RectangleRounded, extrude, export_step, export_stl)
from dimensions import D
from pcb_snapshot import capture
from integrated_pan import integrated_pan, load_adapter, dry_rim, transition_ledge

ROOT = Path(__file__).resolve().parent
CENTER_BOTTOM = (Align.CENTER, Align.CENTER, Align.MIN)


def box(w, d, h, x=0, y=0, z=0):
    return Pos(x, y, z) * Box(w, d, h, align=CENTER_BOTTOM)


def rounded(w, d, h, r=3, x=0, y=0, z=0):
    return Pos(x, y, z) * extrude(RectangleRounded(w, d, r), h)


def cyl(r, h, x=0, y=0, z=0):
    return Pos(x, y, z) * Cylinder(r, h, align=CENTER_BOTTOM)


def arm(a, b, width, thickness, z):
    dx, dy = b[0]-a[0], b[1]-a[1]
    return (Pos((a[0]+b[0])/2, (a[1]+b[1])/2, z)
            * Rot(0, 0, math.degrees(math.atan2(dy, dx)))
            * Box(math.hypot(dx, dy)+width, width, thickness, align=CENTER_BOTTOM))


def lc_holes(solid, x, z, height):
    for dy in (-D.cell_hole_pitch/2, D.cell_hole_pitch/2):
        solid -= cyl(D.cell_clearance_d/2, height+2, x, D.cell_y+dy, z-1)
    return solid


def lower_frame(kind):
    left = -D.cell_hole_span/2
    if kind == 'rectangle':
        f = rounded(D.width-8, D.depth-8, D.frame_t, 4)
        # Windows leave a transverse load-bearing spine and perimeter rails.
        for ya, yb in [(-54, 0), (30, 54)]:
            f -= rounded(128, yb-ya, D.frame_t+2, 4, y=(ya+yb)/2, z=-1)
    else:
        f = box(26, 32, D.frame_t, left, D.cell_y)
        for xy in D.feet:
            f += arm((left, D.cell_y), xy, 16, D.frame_t, 0)
        # Tie rails keep the four branches linked; sensor fixed end is the hub.
        for x in (-68, 68):
            f += box(16, 120, D.frame_t, x, 0)
        f &= rounded(D.width-8, D.depth-8, D.frame_t, 4)
    # Preserve a metal-free volume below the forward-facing antenna.
    f -= box(48, 45, 50, 1, -47.5, -1)
    # Carrier ears need supported seats even over the lightening windows.
    for x, y in D.tray_posts:
        anchor = (-68 if x < 0 else 68, y)
        f += arm(anchor, (x, y), 8, D.frame_t, 0)
        f -= cyl(1.7, D.frame_t+2, x, y, -1)
    # Sensor fixed-end land, mechanically connected to the spine/branches.
    f += box(24, 30, D.frame_t, left, D.cell_y)
    f = lc_holes(f, left, 0, D.frame_t)
    for dy in (-D.cell_hole_pitch/2, D.cell_hole_pitch/2):
        f -= Pos(left, D.cell_y+dy, 0) * Cone(6.3, 3.3, 3, align=CENTER_BOTTOM)
    for x, y in D.feet:
        f -= cyl(1.7, D.frame_t+2, x, y, -1)
    for x, y in D.shell_mounts:
        f -= cyl(1.7, D.frame_t+2, x, y, -1)
    for x, y in D.stops:
        f -= cyl(1.65, D.frame_t+2, x, y, -1)  # M4 pilot/tap placeholder
    return f


def make_parts(snapshot, frame='rectangle'):
    parts = {}
    groups = {}
    materials = {}

    def add(name, shape, group, material, color):
        shape.label = name
        shape.color = Color(color)
        parts[name] = shape
        groups[name] = group
        materials[name] = material

    add('lower_frame', lower_frame(frame), 'fixed', '6061-T6 aluminium', '#9aaebd')
    left, right = -D.cell_hole_span/2, D.cell_hole_span/2
    add('fixed_spacer', lc_holes(box(24, 30, D.spacer_t, left, D.cell_y, D.frame_t),
                               left, D.frame_t, D.spacer_t), 'fixed', 'aluminium', '#c6d5df')
    # Sensor is an envelope; no invented internal flexure/stress geometry.
    cell = box(D.cell_l, D.cell_w, D.cell_h, 0, D.cell_y, D.cell_z)
    for x in (left, right):
        cell = lc_holes(cell, x, D.cell_z, D.cell_h)
    add('LC1330_ENVELOPE', cell, 'sensor', 'vendor part; M6 holes simplified', '#d5ad54')
    add('load_adapter', load_adapter(), 'moving', '6061-T6; LC1330 M6 to underside M4 attachment', '#91a6b5')
    add('integrated_pan', integrated_pan(), 'moving', '6061-T6 monolithic pan; unpierced roof and skirt', '#bacbd8')
    # Install M6 into the original sensor threads before fitting the pan.
    for i, dy in enumerate((-D.cell_hole_pitch/2, D.cell_hole_pitch/2), 1):
        y = D.cell_y+dy
        fixed = Pos(left, y, 0) * Cone(6, 3, 3, align=CENTER_BOTTOM)
        fixed += cyl(2.9, 13, left, y, 3)
        seat_z = D.pan_z
        moving = cyl(5, 6, right, y, seat_z)
        moving += cyl(2.9, 12, right, y, seat_z-12)
        add(f'fixed_M6_{i}', fixed, 'fixed', 'M6x16 countersunk envelope; verify engagement', '#56616c')
        add(f'moving_M6_{i}', moving, 'moving', 'M6x12 socket head; nominal sensor engagement 8 mm', '#56616c')
    for i, (x, y) in enumerate(D.pan_mounts, 1):
        seat_z=D.cell_z+D.cell_h
        screw=cyl(3.5,4,x,y,seat_z-4)+cyl(1.6,10,x,y,seat_z)
        add(f'pan_M4_{i}',screw,'moving','M4x10 from below; nominal pan engagement 6 mm; thread core simplified','#56616c')

    shell = rounded(D.width, D.depth, D.wall_top, 6)
    shell -= rounded(D.width-2*D.wall, D.depth-2*D.wall, D.wall_top+2, 4, z=-1)
    # Fixed front display deck, separated horizontally from moving platter.
    panel = box(D.width-4, 33, 3, y=-53.5, z=D.panel_z)
    panel -= rounded(D.screen_active_w+1, D.screen_active_d+1, 5, 2,
                     D.screen_x, D.screen_y, D.panel_z-1)
    for x in (-64, 48):
        panel -= cyl(4.5, 5, x, -56, D.panel_z-1)  # fixed-side button apertures
    shell += panel
    shell += transition_ledge()
    shell += dry_rim()
    # Outward drainage notches remain outside the continuous inner upstand.
    for x in (-79,79):
        for y in (-25,50):
            shell-=box(6,4,2,x,y,D.wall_top-2)
    for x in (-45,45):
        shell-=box(4,4,2,x,69,D.wall_top-2)
    # Side-wall panel USB extension aperture: connector not yet selected.
    shell -= box(6, 13, 7, 79, -54, 13)
    # Screen shelf touches only the display, never the moving platter.
    shelf = box(44, 26, 2, D.screen_x, D.screen_y, D.screen_z-2)
    shelf -= box(30, 15, 4, D.screen_x, D.screen_y, D.screen_z-3)
    shell += shelf
    # Shelf-to-front-wall webs, outside display module footprint.
    for x in (D.screen_x-21, D.screen_x+21):
        shell += box(2, 15, 7, x, -61, D.screen_z-2)
    for x, y in D.shell_mounts:
        boss = cyl(5.5, 8, x, y, 4) - cyl(1.25, 6, x, y, 4)
        shell += boss
    # The overlap ledge is locally recessed so the adhesive/film sit flush.
    shell-=rounded(152,28,1,3,0,-54,D.panel_z+3)
    add('printed_shell', shell, 'fixed', 'PETG / ASA; M3 pilot bosses need print-fit check', '#303d50')
    # Continuous fixed-side cover, bonded only to the fixed front deck.
    bond_z=D.panel_z+3
    bond=rounded(152,28,D.front_adhesive_t,3,0,-54,bond_z)
    bond-=rounded(148,24,D.front_adhesive_t+2,2,0,-54,bond_z-1)
    add('front_cover_bond',bond,'fixed','closed perimeter adhesive ring, material/process TBD','#445164')
    film=rounded(152,28,D.front_film_t,3,0,-54,bond_z+D.front_adhesive_t)
    add('front_cover_film',film,'fixed','continuous PC overlay; membrane key action and adhesive require prototype','#456070')
    parts['front_cover_film'].color=Color(.25,.4,.45,.4)

    add('screen_ENVELOPE', box(D.screen_w, D.screen_d, D.screen_h,
                             D.screen_x, D.screen_y, D.screen_z),
        'fixed', '1.47 inch ST7789 module; connector envelope TBD', '#157e9d')
    add('battery_ENVELOPE', rounded(D.battery_w, D.battery_d, D.battery_h, 2,
                                  D.battery_x, D.battery_y, 8),
        'fixed', '1S pouch battery envelope; exact SKU TBD', '#b59cbd')
    # Battery shelf and upstands; removable retention strap is not modelled.
    bat_tray = box(D.battery_w+4, D.battery_d+4, 2, D.battery_x, D.battery_y, 6)
    bat_tray += box(2, D.battery_d+4, 8, D.battery_x-D.battery_w/2-1, D.battery_y, 6)
    # A support wall transfers battery tray weight to fixed aluminium rail.
    bat_tray += box(4, 32, 2, -64, D.battery_y, 4)
    add('battery_tray', bat_tray, 'fixed', 'PETG / ASA', '#635270')

    bw, bd = snapshot['width'], snapshot['depth']
    if (bw, bd) != (75, 40) or snapshot['outline'][:2] != [0, 0]:
        raise ValueError('PCB envelope changed; revise tray/placement before rebuilding')
    px, py = D.pcb_xy(bw/2, bd/2)
    pcb_mounts = [D.pcb_xy(m['at'][0], m['at'][1]) for m in snapshot['mounting_holes']]
    # Open tray with 1 mm edge seats for alignment; the board is screwed down
    # at the M2 NPTH positions (2.2 mm drill) read from the PCB snapshot.
    tray = box(bw+4, bd+4, 1, px, py, 8)
    for x in (px-bw/2, px+bw/2):
        tray += box(2, bd, 2, x, py, 9)
    for x, y in D.tray_posts:
        tray += arm((px-bw/2 if x < 0 else px+bw/2, y), (x, y), 8, 1, 8)
        tray -= cyl(1.7, 3, x, y, 7)
        post = cyl(2.8, 4, x, y, 4) - cyl(1.7, 6, x, y, 3)
        add(f'tray_post_{x}_{y}', post, 'fixed', 'nylon M3 spacer', '#729980')
    for x, y in pcb_mounts:
        # Boss stands the board off the 1 mm floor; pilot opens through it.
        tray += cyl(2.5, D.pcb_z-9, x, y, 9)
        tray -= cyl(.8, D.pcb_z-6.5, x, y, 7)
    # RF notch in tray also avoids supporting antenna overhang.
    tray -= box(30, 5, 4, 1, -46.5, 8)
    add('pcb_carrier', tray, 'fixed', 'PETG / ASA; edge seats require underside check', '#477663')
    board = box(bw, bd, snapshot['thickness'], px, py, D.pcb_z)
    for x, y in pcb_mounts:
        board -= cyl(1.1, snapshot['thickness']+2, x, y, D.pcb_z-1)
    add('PCB_ENVELOPE', board, 'fixed', 'FR4 outline from read-only KiCad snapshot', '#227d55')
    for i, (x, y) in enumerate(pcb_mounts, 1):
        screw = cyl(2.1, 1.4, x, y, D.pcb_z+snapshot['thickness']) + cyl(.75, 6, x, y, 7)
        add(f'pcb_M2_{i}', screw, 'fixed', 'M2x8 self-tapping envelope; thread core simplified', '#657583')
    for c in snapshot['components']:
        x0, y0, x1, y1 = c['bbox']
        x, y = D.pcb_xy((x0+x1)/2, (y0+y1)/2)
        add(c['reference']+'_ENVELOPE', box(x1-x0, y1-y0, c['height_envelope'], x, y,
                                         D.pcb_z+snapshot['thickness']),
            'fixed', 'approximate component + mated-connector envelope', '#c8d0d0')

    for i, (x, y) in enumerate(D.feet, 1):
        add(f'foot_{i}', cyl(6, D.foot_h, x, y, -D.foot_h), 'fixed', 'rubber', '#263544')
    for i, (x, y) in enumerate(D.stops, 1):
        # Tall fixed metal posts + an adjustable M4 screw nose below platter.
        stop_top = D.pan_roof_z-D.stop_gap
        post_top = D.pan_roof_z-11
        post = cyl(3.5, post_top-4, x, y, 4) - cyl(1.65, post_top-2, x, y, 3)
        tip = cyl(1.5, stop_top-(post_top-3), x, y, post_top-3)
        add(f'stop_post_{i}', post, 'fixed', 'metal threaded post; locknut required', '#d7853f')
        add(f'stop_tip_{i}', tip, 'fixed', 'adjustable M4 screw, thread simplified', '#dc9d63')
    return parts, groups, materials


def volume_intersection(a, b):
    intersection = a & b
    return 0.0 if intersection is None else intersection.volume


def validate(parts, groups, materials, snapshot):
    invalid = [n for n, s in parts.items() if not s.is_valid or s.volume <= 0]
    split = [n for n in ('lower_frame', 'integrated_pan', 'load_adapter', 'printed_shell', 'pcb_carrier', 'battery_tray')
             if len(parts[n].solids()) != 1]
    # All separate solids must be disjoint; mating surfaces may touch.
    # This catches fixed electronics/tray conflicts as well as weighing bypasses.
    collisions = []
    for a, b in combinations(parts, 2):
        v = volume_intersection(parts[a], parts[b])
        if v > 1e-5:
            collisions.append([a, b, round(v, 5)])
    gaps = {n: round(parts['integrated_pan'].distance_to(parts[n]), 4)
            for n in parts if n.startswith('stop_tip_')}
    shell_gap = parts['integrated_pan'].distance_to(parts['printed_shell'])
    # Conservative antenna box from U4 top 6.3 mm, expanded 15 mm in 3D.
    # This only validates chassis/plate metal, not PCB RF design or RF performance.
    rf = box(48, 36.3, 33.1, 1, -43.6, D.pcb_z+snapshot['thickness']-15)
    rf_hits = []
    for n in ('lower_frame', 'fixed_spacer', 'integrated_pan', 'load_adapter'):
        if volume_intersection(rf, parts[n]) > 1e-5:
            rf_hits.append(n)
    travel_collisions = []
    # Rigid downward sweep is a fit check, not simulated load-cell deformation.
    for travel in (D.stop_gap/2, D.stop_gap):
        for a in parts:
            if groups[a] != 'moving':
                continue
            shifted = Pos(0, 0, -travel) * parts[a]
            for b in parts:
                if groups[b] != 'fixed':
                    continue
                v = volume_intersection(shifted, parts[b])
                if v > 1e-5:
                    travel_collisions.append([travel, a, b, round(v, 5)])
    upper_gap = parts['integrated_pan'].distance_to(parts['LC1330_ENVELOPE'])
    upper_at_stop = (Pos(0, 0, -D.stop_gap)*parts['integrated_pan']).distance_to(parts['LC1330_ENVELOPE'])
    head_gaps = {n: round(parts[n].distance_to(parts['integrated_pan']), 4)
                 for n in parts if n.startswith('moving_M6_')}
    report = {'status': 'PASS' if not (invalid or split or collisions or rf_hits or travel_collisions) else 'FAIL',
              'scope': 'Concept geometry only; no FEA, tolerance stack, RF test or overload calibration',
              'invalid_solids': invalid, 'disconnected_parts': split,
              'all_part_collisions_mm3': collisions,
              'antenna_metal_keepout_hits': rf_hits,
              'rigid_downward_sweep_mm': [D.stop_gap/2, D.stop_gap],
              'rigid_downward_sweep_collisions_mm3': travel_collisions,
              'upper_support_to_sensor_mm': round(upper_gap, 4),
              'upper_support_to_sensor_at_stop_mm': round(upper_at_stop, 4),
              'moving_M6_head_to_plate_mm': head_gaps,
              'skirt_to_fixed_rim_radial_mm': D.rim_radial_gap,
              'skirt_overlap_mm': D.rim_top-(D.pan_roof_z-D.pan_skirt_h),
              'skirt_to_fixed_ledge_vertical_mm': D.underside_gap,
              'roof_to_fixed_rim_mm': D.rim_roof_gap,
              'plate_to_shell_minimum_mm': round(shell_gap, 4),
              'stop_to_plate_gaps_mm': gaps,
              'pcb_mounts_native_mm': {m['reference']: m['at'][:2]
                                      for m in snapshot['mounting_holes']},
              'pcb_mounts_mech_mm': {m['reference']: [round(v, 2) for v in D.pcb_xy(m['at'][0], m['at'][1])]
                                     for m in snapshot['mounting_holes']},
              'overall_mm': [D.width, D.depth, D.total_height],
              'pan_mm': [D.pan_w, D.pan_plan_depth, D.pan_depth],
              'pcb_source_sha256': snapshot['sha256'],
              'part_count': len(parts),
              'parts': {n: {'material': materials[n], 'group': groups[n],
                            'volume_mm3': round(s.volume, 2)} for n, s in parts.items()}}
    assert shell_gap >= 1.9, f'Insufficient plate-shell gap: {shell_gap}'
    assert all(abs(g-D.stop_gap) < 1e-3 for g in gaps.values()), gaps
    assert upper_at_stop >= 2, f'Insufficient sensor clearance at stop: {upper_at_stop}'
    assert all(g >= 0.49 for g in head_gaps.values()), head_gaps
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--refresh-pcb', action='store_true')
    parser.add_argument('--frame', choices=['rectangle', 'butterfly'], default='rectangle')
    args = parser.parse_args()
    path = ROOT / 'references/pcb-snapshot.json'
    snapshot = capture() if args.refresh_pcb or not path.exists() else json.loads(path.read_text())
    parts, groups, materials = make_parts(snapshot, args.frame)
    dest = ROOT / 'exports' / 'v03' / args.frame
    dest.mkdir(parents=True, exist_ok=True)
    report = validate(parts, groups, materials, snapshot)
    (dest/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
    if report['status'] != 'PASS':
        raise RuntimeError(json.dumps({k:v for k,v in report.items() if k != 'parts'}, indent=2))
    assembly = Compound(label='coffee_scale_CONCEPT', children=list(parts.values()))
    export_step(assembly, dest/'assembly.step')
    # Always replace the round-trip report with the newly generated assembly.
    from build123d import import_step
    readback = import_step(dest/'assembly.step')
    assert readback.is_valid and len(readback.solids()) == len(parts)
    assert abs(readback.volume-sum(s.volume for s in parts.values())) < 1
    (dest/'step-roundtrip.json').write_text(json.dumps({
        'valid': readback.is_valid, 'solids': len(readback.solids()),
        'volume_mm3': round(readback.volume, 2)}, indent=2)+'\n')
    for name in ('lower_frame', 'integrated_pan', 'load_adapter', 'fixed_spacer',
                 'printed_shell', 'front_cover_film', 'pcb_carrier', 'battery_tray'):
        export_step(parts[name], dest/f'{name}.step')
        export_stl(parts[name], dest/f'{name}.stl', tolerance=0.05, angular_tolerance=0.1)
    (dest/'parameters.json').write_text(json.dumps(asdict(D), indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k != 'parts'}, indent=2))


if __name__ == '__main__':
    main()
