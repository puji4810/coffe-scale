"""v0.3.2 wide plate support and a three-hole PCB carrier; frozen v031 base."""
import sys,json,math
from pathlib import Path
from dataclasses import dataclass,asdict
from itertools import combinations
from build123d import Color,Compound,Cone,Polygon,Pos,extrude,fillet,export_step,export_stl,import_step
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE/'base'))
from sealed_assembly import make_parts as inherited_parts,S,ring
from assembly import box,rounded,cyl,arm,volume_intersection,CENTER_BOTTOM
from dimensions import D

@dataclass(frozen=True)
class Layout:
    support_z: float=32
    support_t: float=9
    support_w: float=138
    support_d: float=84
    support_y: float=15
    pocket_depth: float=5
    spacer_t: float=3
    pcb_right: float=65
    pcb_front: float=-45
    pcb_z: float=11
    carrier_z: float=6
    @property
    def pan_mounts(self):return [(x,y) for x in (-59,59) for y in (-15,45)]
    def pcb_xy(self,x,y):return self.pcb_right-x,self.pcb_front+y
P=Layout()


def wide_plate():
    # Broad continuous web with flared end lands; no four independent arms.
    pts=[(-69,-22),(-61,-27),(-45,-12),(45,-12),(61,-27),(69,-22),
         (69,52),(61,57),(45,42),(-45,42),(-61,57),(-69,52)]
    profile=Polygon(*pts,align=None)
    profile=fillet(profile.vertices(),3)
    s=Pos(0,0,P.support_z)*extrude(profile,P.support_t)
    # Bottom pocket retains 4 mm of upper web and a solid sensor mounting land.
    s-=rounded(80,40,P.pocket_depth+1,5,-10,15,P.support_z-1)
    for dy in (-7.5,7.5):
        x,y=53,15+dy
        s-=cyl(3.3,11,x,y,31)
        s-=cyl(5.5,7.5,x,y,34.5)
    for x,y in P.pan_mounts:
        s-=cyl(2.2,11,x,y,31)
        # Recesses receive the cover's underside blind-thread bosses.
        s-=cyl(6.2,6,x,y,36)
    return s


def pan_cover():
    s=rounded(160,113,3,6,0,13.5,41)
    s+=ring(160,113,156,109,5,6,4,36,0,13.5)
    for x,y in P.pan_mounts:
        s+=cyl(6,5,x,y,36)
        s-=cyl(1.65,6,x,y,35)
        s-=Pos(x,y,41)*Cone(1.65,0,1,align=CENTER_BOTTOM)
    return s


def make_parts(snapshot):
    baseline=json.loads((HERE/'references/v031-pcb-snapshot.json').read_text())
    parts,groups,materials=inherited_parts(baseline,'rectangle')
    def drop(n):
        for d in (parts,groups,materials):d.pop(n,None)
    def add(n,s,g,m,c):
        s.label=n;s.color=Color(c);parts[n]=s;groups[n]=g;materials[n]=m
    for n in ['integrated_pan','load_adapter']+[f'pan_M4_{i}' for i in range(1,5)]:drop(n)
    add('wide_support',wide_plate(),'moving','6061-T6; continuous 9 mm plate with 5 mm underside pocket','#acbbc5')
    add('pan_cover',pan_cover(),'moving','6061-T6; 3 mm roof, skirt and four integral blind-thread bosses','#bacbd8')
    spacer=box(24,30,3,53,15,29)
    for i,dy in enumerate((-7.5,7.5),1):
        y=15+dy
        spacer-=cyl(3.3,5,53,y,28)
        screw=cyl(5,6,53,y,34.5)+cyl(2.9,12,53,y,22.5)
        add(f'moving_M6_{i}',screw,'moving','M6x12 socket head; nominal cell engagement 6.5 mm','#56616c')
    add('moving_spacer',spacer,'moving','aluminium 24x30x3, loading end only','#91a6b5')
    for i,(x,y) in enumerate(P.pan_mounts,1):
        screw=cyl(3.5,4,x,y,28)+cyl(1.6,8,x,y,32)
        add(f'pan_M4_{i}',screw,'moving','M4x8 from below; 4 mm cover boss engagement','#56616c')

    for c in baseline['components']:drop(c['reference']+'_ENVELOPE')
    for n in list(parts):
        if n.startswith('pcb_M2_') or n.startswith('tray_post_'):drop(n)
    drop('PCB_ENVELOPE');drop('pcb_carrier')
    bw,bd=snapshot['width'],snapshot['depth'];assert (bw,bd)==(75,40)
    px,py=P.pcb_xy(bw/2,bd/2)
    mounts=[(h['reference'],*P.pcb_xy(*h['at']),h['drill_mm']) for h in snapshot['mounting_holes']]
    board=box(bw,bd,snapshot['thickness'],px,py,P.pcb_z)
    tray=box(bw+4,bd+4,1,px,py,P.carrier_z)
    chassis=parts['lower_frame']
    for i,(x,y) in enumerate(D.tray_posts,1):
        tray+=arm((px-bw/2 if x<0 else px+bw/2,y),(x,y),8,1,P.carrier_z)
        tray-=cyl(1.7,3,x,y,P.carrier_z-1)
        post=cyl(2.8,2,x,y,4)-cyl(1.7,4,x,y,3)
        add(f'carrier_spacer_{i}',post,'fixed','nylon M3 spacer, 2 mm','#729980')
        chassis+=cyl(1.7,4,x,y,0)
        chassis-=cyl(1.25,6,x,y,-1)
        add(f'carrier_M3_{i}',cyl(1.2,6,x,y,1)+cyl(2.75,3,x,y,7),
            'fixed','M3x6, chassis blind-side engagement 3 mm; thread simplified','#657583')
    for i,(ref,x,y,drill) in enumerate(mounts,1):
        board-=cyl(drill/2,4,x,y,P.pcb_z-1)
        tray+=cyl(2.7,5,x,y,6)
        tray-=cyl(1.6,5,x,y,7)
        tray-=cyl(1.1,7,x,y,5)
        insert=cyl(1.6,4,x,y,7)-cyl(.8,6,x,y,6)
        add(f'pcb_insert_{ref}',insert,'fixed','M2 brass insert target OD3.2x4; actual insert/press-fit TBD','#bd9452')
        seat=P.pcb_z+snapshot['thickness']
        add(f'pcb_M2_{ref}',cyl(.75,6,x,y,seat-6)+cyl(1.8,1.4,x,y,seat),
            'fixed','M2x6; board 1.6 mm, nominal 4 mm insert engagement','#657583')
    tray-=box(30,5,5,1,-46.5,5)
    add('lower_frame',chassis,'fixed','6061-T6; retained rectangle base, four carrier holes now M3 pilots','#9aaebd')
    add('pcb_carrier',tray,'fixed','printed carrier; three M2 insert bosses, 4 mm board underside clearance','#477663')
    add('PCB_ENVELOPE',board,'fixed','actual PCB outline and three NPTH holes, read-only snapshot','#227d55')
    for c in snapshot['components']:
        x0,y0,x1,y1=c['bbox'];x,y=P.pcb_xy((x0+x1)/2,(y0+y1)/2)
        z=P.pcb_z+snapshot['thickness'] if c['side']=='F' else P.pcb_z-c['height_envelope']
        if x1-x0<1e-6 or y1-y0<1e-6:continue
        add(c['reference']+'_ENVELOPE',box(x1-x0,y1-y0,c['height_envelope'],x,y,z),
            'fixed','component Fab rectangle; height assumed, not vendor STEP','#c8d0d0')
    return parts,groups,materials


def overlaps(a,b):
    aa,bb=a.bounding_box(),b.bounding_box()
    return all(min(getattr(aa.max,k),getattr(bb.max,k))-max(getattr(aa.min,k),getattr(bb.min,k))>1e-6 for k in 'XYZ')

def interference(a,b):return volume_intersection(a,b) if overlaps(a,b) else 0


def validate(parts,groups,materials,snapshot):
    invalid=[n for n,s in parts.items() if not s.is_valid or s.volume<=0]
    split=[n for n,s in parts.items() if len(s.solids())!=1]
    collisions=[]
    for a,b in combinations(parts,2):
        v=interference(parts[a],parts[b])
        if v>1e-5:collisions.append([a,b,round(v,5)])
    travel_hits=[]
    for dz in (.4,.8):
        for a in parts:
            if groups[a]!='moving':continue
            shifted=Pos(0,0,-dz)*parts[a]
            for b in parts:
                if groups[b] not in ('fixed','sensor'):continue
                # This spacer is bolted to the sensor's moving end. The sensor
                # is only a rigid envelope, so that mating interface must move
                # with it in reality; all other sensor-envelope contacts remain checked.
                if a=='moving_spacer' and b=='LC1330_ENVELOPE':continue
                v=interference(shifted,parts[b])
                if v>1e-5:travel_hits.append([dz,a,b,round(v,5)])
    rf=box(48,36.3,33.1,1,-43.6,P.pcb_z+snapshot['thickness']-15)
    rf_hits=[n for n in ('lower_frame','fixed_spacer','wide_support','pan_cover','moving_spacer') if interference(rf,parts[n])>1e-5]
    tools=[]
    for x,y in P.pan_mounts:
        tool=cyl(2.5,38,x,y,-10)
        for n in ('LC1330_ENVELOPE','wide_support','pan_cover'):
            if interference(tool,parts[n])>1e-5:tools.append([x,y,n])
    # The top 2 mm remains continuous including drill-tip allowance.
    skin=rounded(160,113,2,6,0,13.5,42)
    assert abs(volume_intersection(skin,parts['pan_cover'])-skin.volume)<1e-5
    expected=ring(158,138,154,134,.8,5,3,-1)
    assert abs(volume_intersection(expected,parts['bottom_perimeter_gasket'])-expected.volume)<1e-5
    for dz,n in ((-.81,'bottom_cover'),(.81,'printed_shell')):
        sample=Pos(0,0,dz)*expected
        assert abs(volume_intersection(sample,parts[n])-sample.volume)<1e-5
    gaps={n:round(parts['pan_cover'].distance_to(s),4) for n,s in parts.items() if n.startswith('stop_tip_')}
    assert all(abs(v-.8)<1e-4 for v in gaps.values()),gaps
    assert (Pos(0,0,-.8)*parts['wide_support']).distance_to(parts['LC1330_ENVELOPE'])>=2
    mounts=[{'reference':h['reference'],'native_xy':h['at'],'assembly_xy':P.pcb_xy(*h['at']),'drill_mm':h['drill_mm']} for h in snapshot['mounting_holes']]
    moving_al=sum(parts[n].volume*.0027 for n in ('wide_support','pan_cover','moving_spacer'))
    baseline=json.loads((HERE/'references/v031-validation.json').read_text())
    old_al=sum(baseline['parts'][n]['volume_mm3']*.0027 for n in ('integrated_pan','load_adapter'))
    report={'version':'0.3.2','status':'PASS' if not (invalid or split or collisions or travel_hits or rf_hits or tools) else 'FAIL',
            'invalid_solids':invalid,'disconnected_parts':split,'all_part_collisions_mm3':collisions,
            'rigid_downward_sweep_mm':[.4,.8],'rigid_downward_sweep_collisions_mm3':travel_hits,'antenna_metal_keepout_hits':rf_hits,
            'sweep_excluded_mating_interface':['moving_spacer','LC1330_ENVELOPE'],
            'underside_M4_tool_access_before_electronics_hits':tools,
            'continuous_top_2mm_skin':'PASS','bottom_seal_and_lands':'PASS','stop_to_cover_mm':gaps,
            'support_to_cell_mm':round(parts['wide_support'].distance_to(parts['LC1330_ENVELOPE']),4),
            'support_to_cell_at_stop_mm':round((Pos(0,0,-.8)*parts['wide_support']).distance_to(parts['LC1330_ENVELOPE']),4),
            'pcb_mounts':mounts,'pcb_box_mm':[75,40,1.6],'pcb_lower_surface_z':11,
            'pcb_to_floor_mm':4,'pcb_source_sha256':snapshot['sha256'],'part_count':len(parts),
            'body_mm':[160,140,53.3],'with_USB_cap_mm':[164,140,53.3],
            'wide_support_nominal_mm':[138,84,9],'mount_span_mm':[118,60],
            'moving_aluminium_g':round(moving_al,2),'v031_moving_aluminium_g':round(old_al,2),
            'scope':'CAD fit and nominal rigid translation only; no stiffness or weighing accuracy proof. Reference image has no thickness or material. All electronics heights are provisional.',
            'parts':{n:{'material':materials[n],'group':groups[n],'volume_mm3':round(s.volume,2)} for n,s in parts.items()}}
    return report


def main():
    snapshot=json.loads((HERE/'references/pcb-snapshot.json').read_text())
    parts,groups,materials=make_parts(snapshot)
    out=HERE.parent/'exports/v032';out.mkdir(parents=True,exist_ok=True)
    report=validate(parts,groups,materials,snapshot)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    if report['status']!='PASS':raise RuntimeError(json.dumps({k:v for k,v in report.items() if k!='parts'},indent=2))
    assembly=Compound(label='coffee_scale_v032_CONCEPT',children=list(parts.values()))
    export_step(assembly,out/'assembly.step');back=import_step(out/'assembly.step')
    assert back.is_valid and len(back.solids())==len(parts)
    assert abs(back.volume-sum(s.volume for s in parts.values()))<1
    (out/'step-roundtrip.json').write_text(json.dumps({'valid':back.is_valid,'solids':len(back.solids()),'volume_mm3':back.volume},indent=2)+'\n')
    for n in ('wide_support','pan_cover','moving_spacer','pcb_carrier','PCB_ENVELOPE','lower_frame'):
        export_step(parts[n],out/f'{n}.step');export_stl(parts[n],out/f'{n}.stl',tolerance=.05,angular_tolerance=.1)
    (out/'parameters.json').write_text(json.dumps({'v032':asdict(P),'inherited':asdict(D),'sealing':asdict(S)},indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='parts'},indent=2))

if __name__=='__main__':main()
