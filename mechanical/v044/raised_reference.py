"""LC7012 v0.4.3: lowered fixed display fascia, retaining v0.4.2 adjusters.

The frozen base_model is v0.4.1. All edits below use its FINAL coordinates.
Threads are clearance/core envelopes, not manufacturing thread geometry.
"""
from dataclasses import dataclass, asdict
from itertools import combinations, product
from pathlib import Path
import json, math
from build123d import Pos, Rot, Color, Compound, RegularPolygon, extrude, export_step, export_stl, import_step
import base_model as base

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / 'exports/v043'
CY, MOUNTS, STOPS = base.CY, base.MOUNTS, base.STOPS
box, cyl, rounded, hits = base.box, base.cyl, base.rounded, base.hits
PIN_XY = [(8., CY + 6), (-8., CY - 6)]
ALLOWED_FOILS = (.02, .05, .1)
SHIM_RANGE = (.2, .4)
GAP_RANGE = (.4, .8)
DISPLAY_DROP = 4.0


@dataclass(frozen=True)
class Settings:
    fixed_shims: tuple = (.1, .1, .1)
    moving_shims: tuple = (.1, .1, .1)
    stop_gaps: tuple = (.6, .6, .6, .6)

    def check(self):
        for stack in (self.fixed_shims, self.moving_shims):
            if not stack or any(t not in ALLOWED_FOILS for t in stack):
                raise ValueError('Use full-face 0.02/0.05/0.1 mm foils')
            if not SHIM_RANGE[0]-1e-9 <= sum(stack) <= SHIM_RANGE[1]+1e-9:
                raise ValueError('Validated shim stack range is 0.20..0.40 mm per end')
        if len(self.stop_gaps) != 4 or any(not GAP_RANGE[0] <= g <= GAP_RANGE[1] for g in self.stop_gaps):
            raise ValueError('Four individual stop gaps must be within 0.4..0.8 mm')


def hexagon(af, height, x, y, z):
    return Pos(x, y, z) * extrude(RegularPolygon(af/math.sqrt(3), 6), height)


def shim(t, x, z, tab_side):
    s = box(12, 12, t, x, CY, z)
    s += box(3, 4, t, x + tab_side*7.5, CY, z)
    for y in (CY-3.5, CY+3.5):
        s -= cyl(1.7, t+2, x, y, z-1)
    return s


def make_parts(snapshot, cfg=Settings()):
    cfg.check()
    p, groups, materials = base.make_parts(snapshot)

    def add(name, shape, group, material, color):
        shape.label = name
        shape.color = Color(color)
        p[name], groups[name], materials[name] = shape, group, material

    def replace(name, shape):
        add(name, shape, groups[name], materials[name], tuple(p[name].color))

    df = sum(cfg.fixed_shims)-.3
    du = sum(cfg.moving_shims)-.3
    dm = df+du
    # Remove 0.3 mm from fixed seating pad; retain its original 12 x 12 footprint.
    replace('lower_frame', p['lower_frame']-box(12, 12, .3, -58, CY, 5.7))
    replace('LC7012_ENVELOPE', Pos(0, 0, df)*p['LC7012_ENVELOPE'])
    for name in list(p):
        if groups[name] == 'moving':
            replace(name, Pos(0, 0, dm)*p[name])
    spacer = box(12, 12, 2.2, 0, CY, 28+df)
    for y in (CY-3.5, CY+3.5):
        spacer -= cyl(1.7, 4, 0, y, 27+df)
    replace('moving_spacer', spacer)
    materials['moving_spacer'] = '6061 aluminium; 2.2 mm solid spacer; foils above'
    for label, stack, x, z, side, group in (
        ('fixed', cfg.fixed_shims, -58, 5.7, -1, 'fixed'),
        ('moving', cfg.moving_shims, 0, 30.2+df, 1, 'moving')):
        for i, t in enumerate(stack, 1):
            add(f'{label}_shim_{i}', shim(t, x, z, side), group,
                f'stainless shim foil {t:g} mm; full seating face; deburred', '#d49666')
            z += t

    # Round/relieved locating pins on the hub, clear of central M3 screws.
    # Pan bosses have 0.2 mm axial clearance: they locate, not an extra seat.
    spider, pan = p['equal_arm_spider'], p['pan_cover']
    for i, (x, y) in enumerate(PIN_XY, 1):
        spider -= cyl(2.85, 3, x, y, 34.3+dm)
        spider -= cyl(1, 4, x, y, 31+dm)
        pan += cyl(2.75, 2, x, y, 34.5+dm)
        pan -= cyl(1.01, 2.3, x, y, 34.5+dm)
        pin = cyl(1, 5, x, y, 31.3+dm)
        if i == 2:
            head = cyl(1, 2, x, y, 34.3+dm)
            relief = Pos(x,y,34.3+dm)*Rot(0,0,math.degrees(math.atan2(12,16)))*box(1.6,2.2,2)
            pin = cyl(1,3,x,y,31.3+dm)+(head & relief)
        add(f'locating_pin_{i}', pin, 'moving',
            'steel D2 x 5; round' if i == 1 else 'steel D2 x 5; upper 2 mm relieved to 1.6 along pin baseline', '#79aebe')
    replace('equal_arm_spider', spider)

    # Integral metal towers transfer stop loads to the frame. Socket/locknut
    # service is entirely from below; bottom cover is removed during adjustment.
    frame, cover, shell = p['lower_frame'], p['bottom_cover'], p['printed_shell']
    for i, ((x, y), gap) in enumerate(zip(STOPS, cfg.stop_gaps), 1):
        for old in (f'stop_post_{i}', f'stop_tip_{i}'):
            del p[old], groups[old], materials[old]
        frame += cyl(6.5, 4, x, y, 1)+cyl(4, 22, x, y, 5)
        frame -= cyl(5.7, 2.1, x, y, .9)  # socket counterbore, seat at Z=3
        frame -= cyl(2.2, 18.1, x, y, .9) # clearance bore below tapped region
        frame -= cyl(1.65, 8.1, x, y, 19) # M4 tapped region, minor envelope
        # Closed outer cover, with an internal pocket for the adjusting screw.
        cover -= cyl(5.9, 1.2, x, y, -.9)
        pad_z = 35.5+dm
        pan += cyl(5, 1, x, y, pad_z)
        pan += box(10, 4, 1, 73 if x > 0 else -73, y, pad_z)
        # Horizontal feeler access below the continuous pan skin. The fixed
        # splash lip receives a matching local relief, with no bridging seal.
        pan -= box(6, 8, 1.4, 79 if x > 0 else -79, y, 34.1+dm)
        shell -= box(8, 8, 2, 75 if x > 0 else -75, y, 33.7)
        tip_z = pad_z-gap
        screw_z = tip_z-35
        screw = cyl(1.5, 34, x, y, screw_z)+cyl(2, 1, x, y, tip_z-1)
        screw -= hexagon(2, 2.1, x, y, screw_z-.1)
        add(f'stop_tip_{i}', screw, 'fixed', 'M4 x 35 flat-point socket screw; simplified thread core; 2 AF socket', '#d6ad75')
        nut = hexagon(7, 2.2, x, y, .8)-cyl(1.65, 3, x, y, .4)
        add(f'stop_locknut_{i}', nut, 'fixed', 'M4 thin nut envelope AF7 x 2.2; verify purchased hardware', '#be9461')
    replace('lower_frame', frame)
    materials['lower_frame'] = '6061 aluminium; integral M4 stop towers; bottom socket counterbores'
    replace('bottom_cover', cover)
    replace('printed_shell', shell)
    replace('pan_cover', pan)
    # Lower only the fixed front fascia. Rebuild the upper front shell as a
    # translated region; retain lower sidewalls and bottom-cover screw mounts.
    front_upper = box(170, 40, 50, 0, -53, 26.5)
    old_shell = p['printed_shell']
    lowered = Pos(0, 0, -DISPLAY_DROP) * (old_shell & front_upper)
    replace('printed_shell', (old_shell - front_upper) + lowered)
    for name in list(p):
        if name.startswith(('fascia_', 'TFT_', 'TTP223_')):
            replace(name, Pos(0, 0, -DISPLAY_DROP) * p[name])
    return p, groups, materials


def clashes(p, groups):
    result = []
    for a, b in combinations(p, 2):
        if groups[a] == groups[b] == 'component':
            continue
        v = hits(p[a], p[b])
        if v > 1e-5:
            result.append([a, b, round(v, 5)])
    return result


def travel_check(p, groups, transform):
    result = []
    for a in p:
        if groups[a] != 'moving':
            continue
        moved = transform*p[a]
        for b in p:
            if groups[b] == 'moving':
                continue
            # Only interfaces attached to the deforming sensor end are exempt.
            # Sensor is a solid envelope, not a deformed flexure model.
            if b == 'LC7012_ENVELOPE' and (a == 'moving_spacer' or a.startswith(('moving_M3_', 'moving_shim_'))):
                continue
            v = hits(moved, p[b])
            if v > 1e-5:
                result.append([a, b, round(v, 5)])
    return result


def validate(p, groups, materials, snapshot, cfg=Settings(), detailed=True):
    df, du = sum(cfg.fixed_shims)-.3, sum(cfg.moving_shims)-.3
    dm = df+du
    invalid = [n for n,s in p.items() if not s.is_valid or len(s.solids()) != 1 or s.volume <= 0]
    collisions = clashes(p, groups)
    gaps = [p['pan_cover'].distance_to(p[f'stop_tip_{i}']) for i in range(1,5)]
    contacts = {
        'fixed_seat_to_first_shim': p['lower_frame'].distance_to(p['fixed_shim_1']),
        'fixed_last_shim_to_sensor': p[f'fixed_shim_{len(cfg.fixed_shims)}'].distance_to(p['LC7012_ENVELOPE']),
        'sensor_to_upper_spacer': p['LC7012_ENVELOPE'].distance_to(p['moving_spacer']),
        'upper_spacer_to_first_shim': p['moving_spacer'].distance_to(p['moving_shim_1']),
        'upper_last_shim_to_spider': p[f'moving_shim_{len(cfg.moving_shims)}'].distance_to(p['equal_arm_spider']),
        'spider_to_pan': p['equal_arm_spider'].distance_to(p['pan_cover']),
    }
    for prefix, stack in (('fixed',cfg.fixed_shims), ('moving',cfg.moving_shims)):
        for i in range(1,len(stack)):
            contacts[f'{prefix}_foil_{i}_to_{i+1}'] = p[f'{prefix}_shim_{i}'].distance_to(p[f'{prefix}_shim_{i+1}'])
    tools = []
    for i,(x,y) in enumerate(STOPS,1):
        # Socket OD11 approaches nut from below. Cover is removed; the central
        # screw passes inside the socket. Driver AF2 enters modelled hex recess.
        socket = cyl(5.5, 23, x,y,-20)-cyl(4.15,24,x,y,-20.5)
        bottom = p[f'stop_tip_{i}'].bounding_box().min.Z
        driver = hexagon(1.95,bottom+21.8,x,y,-20)
        blade_z = 35.5+dm-cfg.stop_gaps[i-1]/2-.05
        blade = box(20,4,.1,76 if x>0 else -76,y,blade_z)
        for label,tool,excluded in (
            ('nut_socket',socket,{'bottom_cover',f'stop_locknut_{i}'}),
            ('hex_driver',driver,{'bottom_cover'}),
            ('feeler_blade',blade,set())):
            for n in p:
                if n in excluded:
                    continue
                v = hits(tool,p[n])
                if v > 1e-5:
                    tools.append([i,label,n,round(v,5)])
    antenna = box(48,20,33.1,-25,-3.25,-3.4)
    metals = [n for n in p if n in ('lower_frame','equal_arm_spider','moving_spacer','pan_cover','LC7012_ENVELOPE','BATTERY_103450') or 'shim_' in n or n.startswith(('locating_pin_','stop_'))]
    rf = [n for n in metals if hits(antenna,p[n]) > 1e-5]
    skin = rounded(160,102,1.5,6,0,CY,37+dm)
    skin_ok = abs(skin.volume-hits(skin,p['pan_cover'])) < 1e-4
    # Only evaluate new pocket footprints; preexisting foot/fastener holes remain.
    pocket_floors = [hits(cyl(5.9,.9,x,y,-1.8),p['bottom_cover']) for x,y in STOPS]
    cover_ok = all(abs(v-math.pi*5.9**2*.9) < 1e-4 for v in pocket_floors)
    nut_seats = [p['lower_frame'].distance_to(p[f'stop_locknut_{i}']) for i in range(1,5)]
    screw_engagement = [min(27,p[f'stop_tip_{i}'].bounding_box().max.Z-1)-19 for i in range(1,5)]
    screw_protrusion = [.8-p[f'stop_tip_{i}'].bounding_box().min.Z for i in range(1,5)]
    moving_end_engagement = 4.8-du
    fixed_end_engagement = 6-df
    travel = []
    rf_travel = []
    cases = [('down_half',Pos(0,0,-min(gaps)/2)),('down_to_first_stop',Pos(0,0,-min(gaps))),
             ('up_to_capture',Pos(0,0,1-dm))]
    if detailed:
        # A bounded geometric tilt probe, not an inferred physical deflection.
        pivot = Pos(0,CY,30.5+dm)
        for ax,ay in product((-.1,.1),repeat=2):
            cases.append((f'tilt_{ax}_{ay}_down_0.1',Pos(0,0,-.1)*pivot*Rot(ax,ay,0)*Pos(0,-CY,-30.5-dm)))
    for label,tf in cases:
        travel.extend([[label,*row] for row in travel_check(p,groups,tf)])
        rf_travel.extend([[label,n] for n in metals if groups[n]=='moving' and hits(antenna,tf*p[n])>1e-5])
    pin_gaps = [p[f'locating_pin_{i}'].distance_to(p['pan_cover']) for i in (1,2)]
    touch = [p[f'TTP223_{i}_ADHESIVE'].distance_to(p['fascia_glass']) for i in (1,2)]
    fascia_seat_gap = p['fascia_glass_bond'].distance_to(p['printed_shell'])
    display_header_gap = p['TFT_HEADER_KEEPOUT'].distance_to(p['PCB_J6'])
    front_height = p['fascia_glass'].bounding_box().max.Z+4
    step_height = p['pan_glass'].bounding_box().max.Z-p['fascia_glass'].bounding_box().max.Z
    actual_height = max(s.bounding_box().max.Z for n,s in p.items() if groups[n]!='keepout')+4
    checks = [not invalid,not collisions,not tools,not rf,not rf_travel,not travel,skin_ok,cover_ok,
              all(abs(a-b)<1e-4 for a,b in zip(gaps,cfg.stop_gaps)),
              all(v<1e-4 for v in contacts.values()),all(v<1e-4 for v in nut_seats),
              all(v>0 for v in screw_protrusion),all(v>=8-1e-5 for v in screw_engagement),
              all(abs(v-.01)<1e-4 for v in pin_gaps),all(v<1e-4 for v in touch),
              fascia_seat_gap<1e-4,display_header_gap>=.9-1e-5,
              abs(front_height-(43.8-DISPLAY_DROP))<1e-4,
              abs(step_height-(DISPLAY_DROP+dm))<1e-4,
              abs(actual_height-(43.8+dm))<1e-4]
    return {'version':'v0.4.3 LC7012','status':'PASS' if all(checks) else 'FAIL',
            'settings':asdict(cfg),'part_count':len(p),'invalid_or_split_solids':invalid,
            'collisions_mm3':collisions,'tool_collisions_mm3':tools,'travel_collisions_mm3':travel,
            'antenna_hits':rf,'antenna_travel_hits':rf_travel,'down_stop_gaps_mm':gaps,
            'interface_gaps_mm':contacts,'nut_seat_gaps_mm':nut_seats,'pin_radial_clearances_mm':pin_gaps,
            'continuous_pan_skin_1_5mm':skin_ok,'cover_pocket_floor_0_9mm':cover_ok,
            'stop_thread_engagement_mm':screw_engagement,'screw_below_nut_mm':screw_protrusion,
            'sensor_screw_engagement_mm':{'fixed':fixed_end_engagement,'moving':moving_end_engagement},
            'height_with_feet_mm':round(max(39.8-DISPLAY_DROP,39.8+dm)+4,3),
            'moving_glass_vs_fixed_glass_mm':round(DISPLAY_DROP+dm,3),'pcb_sha256':snapshot['sha256'],
            'front_glass_height_with_feet_mm':round(front_height,3),
            'fascia_bond_to_shell_gap_mm':fascia_seat_gap,
            'TFT_header_keepout_to_J6_gap_mm':display_header_gap,
            'scope':'Nominal rigid CAD, sampled shim/gap range and +/-0.1 degree tilt probes. No FEA, thread strength, tolerance stack, shock protection or weighing accuracy certification. Sensor interfaces use undeformed solid-envelope exceptions.',
            'parts':{n:{'group':groups[n],'material':materials[n],'volume_mm3':round(s.volume,4)} for n,s in p.items()}}


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    snapshot = json.loads((HERE/'references/pcb-snapshot.json').read_text())
    cfg = Settings()
    p,g,m = make_parts(snapshot,cfg)
    report = validate(p,g,m,snapshot,cfg)
    (OUT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='parts'},indent=2),flush=True)
    if report['status']!='PASS':
        raise RuntimeError('Default geometry failed; see validation.json')
    reports = []
    for ft,ut in product(SHIM_RANGE,repeat=2):
        case = Settings((.1,)*round(ft/.1),(.1,)*round(ut/.1),(.4,.8,.4,.8))
        cp,cg,cm = make_parts(snapshot,case)
        cr = validate(cp,cg,cm,snapshot,case)
        cr.pop('parts')
        reports.append(cr)
        print('Range',ft,ut,cr['status'],flush=True)
    # Reverse gaps ensures both extremes are checked at each physical corner.
    for ft,ut in product(SHIM_RANGE,repeat=2):
        case = Settings((.1,)*round(ft/.1),(.1,)*round(ut/.1),(.8,.4,.8,.4))
        cp,cg,cm = make_parts(snapshot,case)
        cr = validate(cp,cg,cm,snapshot,case)
        cr.pop('parts')
        reports.append(cr)
        print('Range reversed',ft,ut,cr['status'],flush=True)
    (OUT/'adjustment-validation.json').write_text(json.dumps(reports,indent=2)+'\n')
    if any(r['status']!='PASS' for r in reports):
        raise RuntimeError('Adjustment envelope failed')
    physical = {n:s for n,s in p.items() if g[n]!='keepout'}
    assembly = Compound(label='LC7012_v043_STEPPED_CONCEPT',children=list(physical.values()))
    export_step(assembly,OUT/'assembly.step')
    back = import_step(OUT/'assembly.step')
    delta = abs(back.volume-sum(s.volume for s in physical.values()))
    assert back.is_valid and len(back.solids())==len(physical) and delta<.1
    (OUT/'step-roundtrip.json').write_text(json.dumps({'valid':back.is_valid,'solids':len(back.solids()),'volume_delta_mm3':delta},indent=2)+'\n')
    for n,s in physical.items():
        if g[n]=='component':continue
        export_step(s,OUT/f'{n}.step')
        export_stl(s,OUT/f'{n}.stl',tolerance=.03,angular_tolerance=.1)
    for t in ALLOWED_FOILS:
        for name,x,side in (('fixed',-58,-1),('moving',0,1)):
            export_step(shim(t,x,0,side),OUT/f'{name}_shim_{t:g}mm.step')
    export_step(rounded(159,34,base.GLASS_T,3.5,0,-51.5,38.7-DISPLAY_DROP),OUT/'fascia_glass_blank.step')
    params = {'default':asdict(cfg),'shim_total_range_mm':SHIM_RANGE,'foil_sizes_mm':ALLOWED_FOILS,
              'stop_gap_range_mm':GAP_RANGE,'stop_xy':STOPS,'locating_pin_xy':PIN_XY,
              'stop_screw':'M4x35, flat point, AF2 socket; simplified model',
              'locknut_envelope_mm':{'across_flats':7,'height':2.2},'socket_tool_outer_diameter_mm':11,
              'base_seat_mm':.7,'upper_spacer_mm':2.2,'pan_size_mm':[160,102,2],
              'nominal_overall_mm':[160,140,43.8],'tilt_probe_deg':.1,
              'display_drop_mm':DISPLAY_DROP,'front_glass_height_with_feet_mm':43.8-DISPLAY_DROP,
              'nominal_stop_gap_is_not_calibrated':True}
    (OUT/'parameters.json').write_text(json.dumps(params,indent=2)+'\n')
    print(OUT/'assembly.step',flush=True)


if __name__=='__main__':
    main()
