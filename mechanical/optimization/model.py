"""Paired optimization of v031 and v032, sharing PCB/base/retention hardware.

Exports are separate from the original versions. CAD checks are not FEA.
"""
import sys,json,argparse,importlib.util
from pathlib import Path
from itertools import combinations
from dataclasses import asdict
from build123d import Color,Compound,Cone,RegularPolygon,Pos,extrude,export_step,export_stl,import_step
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE/'baseline'))
from sealed_assembly import make_parts as make_v031,S,ring
from assembly import box,rounded,cyl,arm,volume_intersection,CENTER_BOTTOM
from integrated_pan import integrated_pan,load_adapter
from dimensions import D
# Freeze the reference implementation, but use the current preserved v031 base
# already imported above. Neither original source tree is changed.
spec=importlib.util.spec_from_file_location('paired_reference',HERE/'v032_reference/model.py')
REF=importlib.util.module_from_spec(spec);sys.modules[spec.name]=REF;spec.loader.exec_module(REF)
CAPTURES=[(x,y) for x in (-71,71) for y in (-9,53)]
BOSS_BOTTOM=36
RECESS_FLOOR=35.8
LIFT_GAP=1.5
SIDE_GAP=.4


def make_parts(variant,snapshot):
    # Reuse v032's three-hole carrier and the full current component snapshot.
    parts,groups,materials=REF.make_parts(snapshot)
    def drop(n):
        for d in (parts,groups,materials):d.pop(n,None)
    def add(n,s,g,m,c):
        s.label=n;s.color=Color(c);parts[n]=s;groups[n]=g;materials[n]=m
    if variant=='v031':
        for n in ('wide_support','pan_cover','moving_spacer'):drop(n)
        original,og,om=make_v031(json.loads((HERE/'references/v031-pcb-snapshot.json').read_text()))
        for n in ['load_adapter']+[f'moving_M6_{i}' for i in (1,2)]+[f'pan_M4_{i}' for i in range(1,5)]:
            add(n,original[n],og[n],om[n],'#91a6b5' if n=='load_adapter' else '#56616c')
        pan=original['integrated_pan']
        # Broaden the original centre rib 8 -> 12 mm, plus two short cross ribs.
        pan+=box(102,12,8,-12,15,33)
        for x in (-32,8):pan+=box(4,88,6,x,13,35)
        pan_name='integrated_pan'
    else:
        pan=parts['pan_cover'];pan_name='pan_cover'
        support=parts['wide_support']
        # One Z bearing datum: top plane. Boss ends have 0.2 mm axial relief.
        for x,y in REF.P.pan_mounts:
            support-=cyl(6.2,6.2,x,y,RECESS_FLOOR)
        add('wide_support',support,'moving','6061-T6; upper-plane bearing datum, recesses 0.2 mm deeper','#acbbc5')

    shell=parts['printed_shell']
    # Retention pins are shoulder pins seated against MOVING bosses, never
    # tightened against fixed lugs. Normal travel remains contact-free.
    for i,(x,y) in enumerate(CAPTURES,1):
        pan+=cyl(2.3,5,x,y,BOSS_BOTTOM)
        pan-=cyl(1.25,5.5,x,y,35)
        pan-=Pos(x,y,40.5)*Cone(1.25,0,.75,align=CENTER_BOTTOM)
        if variant=='v032':
            support=parts['wide_support']-cyl(2.5,11,x,y,31)
            add('wide_support',support,'moving','6061-T6; upper-plane bearing datum, boss relief 0.2 mm','#acbbc5')
        shell+=arm((x,y),(76 if x>0 else -76,y),8,2,25)
        shell+=box(2,8,4,77 if x>0 else -77,y,21)
        shell-=cyl(2+SIDE_GAP,4,x,y,24)
        pin=cyl(3,3,x,y,20.5)+cyl(2,12.5,x,y,23.5)+cyl(1.2,3.5,x,y,36)
        add(f'capture_pin_{i}',pin,'moving','custom shoulder pin: dia4x12.5 shoulder, M3x3.5 thread, dia6x3 head; retention strength TBD','#d0a65e')
    add(pan_name,pan,'moving','6061-T6; integral retention bosses, uninterrupted top surface','#bacbd8')
    add('printed_shell',shell,'fixed','printed shell with fixed retention lugs; prototype strength/sealing TBD','#303d50')

    # One-piece pedestal removes the fixed-end spacer interface, with unchanged
    # sensor height. This is a machined 7 mm-stock base, not a 4 mm sheet alone.
    frame=parts['lower_frame']+parts['fixed_spacer'];drop('fixed_spacer')
    for i,(x,y) in enumerate(D.feet,1):
        frame-=cyl(4.15,1.5,x,y,0)
        post=cyl(4,5,x,y,-3.5)+cyl(6,1.5,x,y,-5)
        post-=cyl(1.25,4.5,x,y,-3)
        post-=Pos(x,y,-3.75)*Cone(0,1.25,.75,align=CENTER_BOTTOM)
        add(f'foot_spacer_{i}',post,'fixed','metal foot spacer, top seat Z1.5; M3 engagement3.5, drill-tip floor1.25','#acbdc8')
        add(f'foot_{i}',cyl(6,3,x,y,-8),'fixed','3 mm rubber foot; bottom Z-8, seal and shoulder remain fixed','#263544')
        # Existing M3x6 screws seat at Z4 and end at Z-2: 3.5 mm engagement.
    add('lower_frame',frame,'fixed','6061-T6 one-piece 7 mm-stock base with 4 mm rails and integral 3 mm pedestal','#9aaebd')

    # Lockable downward stops. Keep provisional 0.8 mm operating gap; do not
    # infer a safe overload setting from CAD. Flatter noses spread contact.
    for i,(x,y) in enumerate(D.stops,1):
        post=cyl(3.5,23.5,x,y,4)+cyl(1.5,4,x,y,0)
        post-=cyl(1.65,7.5,x,y,21)
        tip=cyl(1.5,17,x,y,22)+cyl(2.5,1.2,x,y,39)
        nut=Pos(x,y,27.5)*extrude(RegularPolygon(7/(3**.5),6),2)
        nut-=cyl(1.65,4,x,y,26.5)
        add(f'stop_post_{i}',post,'fixed','threaded metal stop post, integral lower stud, blind upper bore','#d7853f')
        add(f'stop_tip_{i}',tip,'fixed','M4 adjustment spindle envelope with dia5 contact nose; gap must be calibrated','#dc9d63')
        add(f'stop_locknut_{i}',nut,'fixed','M4 thin locknut envelope AF7x2; supplier dimensions TBD','#d0a65e')
    return parts,groups,materials


def hits(a,b):return REF.interference(a,b)


def validate(variant,parts,groups,materials,snapshot):
    pan_name='integrated_pan' if variant=='v031' else 'pan_cover'
    pan=parts[pan_name]
    invalid=[n for n,s in parts.items() if not s.is_valid or s.volume<=0]
    split=[n for n,s in parts.items() if len(s.solids())!=1]
    collisions=[]
    for a,b in combinations(parts,2):
        v=hits(parts[a],parts[b])
        if v>1e-5:collisions.append([a,b,round(v,5)])
    # Sensor compliance is absent. Omit only intended load-end mating contact.
    interface='load_adapter' if variant=='v031' else 'moving_spacer'
    scenarios=[('down_half',(0,0,-.4)),('down_stop',(0,0,-.8)),
               ('up_half',(0,0,.75)),('up_stop',(0,0,1.5)),
               ('left_stop',(-.4,0,0)),('right_stop',(.4,0,0)),
               ('front_stop',(0,-.4,0)),('rear_stop',(0,.4,0))]
    moving=[n for n in parts if groups[n]=='moving']
    fixed=[n for n in parts if groups[n] in ('fixed','sensor')]
    travel=[]
    for label,xyz in scenarios:
        for a in moving:
            moved=Pos(*xyz)*parts[a]
            for b in fixed:
                if a==interface and b=='LC1330_ENVELOPE':continue
                v=hits(moved,parts[b])
                if v>1e-5:travel.append([label,a,b,round(v,5)])
    down={n:round(pan.distance_to(s),5) for n,s in parts.items() if n.startswith('stop_tip_')}
    lift={n:round((Pos(0,0,LIFT_GAP)*s).distance_to(parts['printed_shell']),5) for n,s in parts.items() if n.startswith('capture_pin_')}
    normal_capture={n:round(s.distance_to(parts['printed_shell']),5) for n,s in parts.items() if n.startswith('capture_pin_')}
    assert all(abs(v-.8)<1e-4 for v in down.values()),down
    assert all(v<1e-4 for v in lift.values()),lift
    assert all(abs(v-SIDE_GAP)<1e-4 for v in normal_capture.values()),normal_capture
    assert pan.distance_to(parts['printed_shell'])>=.69
    # Pins must be installable vertically before the chassis/electronics enter
    # the shell. This catches lower shell bosses blocking the assembly route.
    for x,y in CAPTURES:
        approach=cyl(3,30.5,x,y,-10)
        assert hits(approach,parts['printed_shell'])<1e-5,(x,y,'pin approach')
    # Local retention drill tips preserve at least 2 mm of continuous top skin.
    skin=rounded(160,113,2,6,0,13.5,42)
    assert abs(volume_intersection(pan,skin)-skin.volume)<1e-5
    gasket=ring(158,138,154,134,.8,5,3,-1)
    for dz,n in ((0,'bottom_perimeter_gasket'),(-.81,'bottom_cover'),(.81,'printed_shell')):
        sample=Pos(0,0,dz)*gasket
        assert abs(volume_intersection(sample,parts[n])-sample.volume)<1e-5
    rf=box(48,36.3,33.1,1,-43.6,11+snapshot['thickness']-15)
    metals=['lower_frame',pan_name]+(['load_adapter'] if variant=='v031' else ['wide_support','moving_spacer'])
    rf_hits=[n for n in metals if hits(rf,parts[n])>1e-5]
    al=sum(parts[n].volume*.0027 for n in metals if groups[n]=='moving')
    steel=sum(s.volume*.00785 for n,s in parts.items() if groups[n]=='moving' and n not in metals and n not in ('pan_glass','pan_glass_bond'))
    total=al+steel+parts['pan_glass'].volume*.0025+parts['pan_glass_bond'].volume*.0011
    boss_gap=None
    if variant=='v032':
        measured=[]
        for x,y in REF.P.pan_mounts:
            # Compare the actual boss underside annulus with the recess floor.
            probe=cyl(5.9,.01,x,y,35.99)-cyl(2.3,.03,x,y,35.98)
            measured.append(round(probe.distance_to(parts['wide_support'])+.01,5))
        boss_gap=measured
        assert all(abs(v-.2)<1e-4 for v in measured),measured
    return {'version':variant+'-optimized','status':'PASS' if not (invalid or split or collisions or travel or rf_hits) else 'FAIL',
            'invalid_solids':invalid,'disconnected_parts':split,'collisions_mm3':collisions,
            'travel_scenarios':scenarios,'travel_collisions_mm3':travel,'sensor_mating_exception':[interface,'LC1330_ENVELOPE'],
            'downward_stop_gaps_mm':down,'capture_normal_minimum_gap_mm':normal_capture,'capture_uplift_at_stop_mm':lift,
            'v032_boss_axial_gaps_mm':boss_gap,'foot_M3_nominal_engagement_mm':3.5,'foot_drill_tip_bottom_wall_mm':1.25,
            'continuous_top_2mm':'PASS','bottom_seal_and_lands':'PASS','antenna_metal_hits':rf_hits,
            'capture_installation_before_chassis':'PASS',
            'pcb_sha256':snapshot['sha256'],'pcb_mounts':[{**h,'assembly_xy':REF.P.pcb_xy(*h['at'])} for h in snapshot['mounting_holes']],
            'pcb_under_board_gap_mm':4,'moving_aluminium_g':round(al,2),'moving_total_estimate_g':round(total,2),
            'part_count':len(parts),'body_mm':[160,140,53.3],'USB_cap_overall_width_mm':164,
            'scope':'Nominal rigid CAD checks only. Travel settings are not sensor safety limits. Retention strength, tolerances, thermal drift and weighing accuracy require prototype tests.',
            'parts':{n:{'group':groups[n],'material':materials[n],'volume_mm3':round(s.volume,2)} for n,s in parts.items()}}


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--variant',choices=['v031','v032'],required=True);args=parser.parse_args()
    snap=json.loads((HERE/'references/pcb-snapshot.json').read_text())
    parts,groups,materials=make_parts(args.variant,snap)
    out=HERE.parent/'exports'/(args.variant+'-optimized');out.mkdir(parents=True,exist_ok=True)
    report=validate(args.variant,parts,groups,materials,snap)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    if report['status']!='PASS':raise RuntimeError(json.dumps({k:v for k,v in report.items() if k!='parts'},indent=2))
    assembly=Compound(label=args.variant+'_OPTIMIZED_CONCEPT',children=list(parts.values()))
    export_step(assembly,out/'assembly.step');back=import_step(out/'assembly.step')
    assert back.is_valid and len(back.solids())==len(parts)
    assert abs(back.volume-sum(s.volume for s in parts.values()))<1
    (out/'step-roundtrip.json').write_text(json.dumps({'valid':back.is_valid,'solids':len(back.solids()),'volume_mm3':back.volume},indent=2)+'\n')
    names=['lower_frame','pcb_carrier','PCB_ENVELOPE','printed_shell','foot_spacer_1','stop_post_1','stop_tip_1','stop_locknut_1','capture_pin_1']
    names+=['integrated_pan','load_adapter'] if args.variant=='v031' else ['wide_support','pan_cover','moving_spacer']
    for n in names:
        export_step(parts[n],out/f'{n}.step');export_stl(parts[n],out/f'{n}.stl',tolerance=.05,angular_tolerance=.1)
    (out/'parameters.json').write_text(json.dumps({'variant':args.variant,'pcb_layout':asdict(REF.P),'base':asdict(D),'sealing':asdict(S),
      'captures':CAPTURES,'lift_gap':LIFT_GAP,'side_gap':SIDE_GAP,'v032_recess_floor':RECESS_FLOOR},indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='parts'},indent=2))

if __name__=='__main__':main()
