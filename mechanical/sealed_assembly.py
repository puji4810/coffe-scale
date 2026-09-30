"""v0.3.1: preserve v0.3 layout, add pan glass, serviceable bottom and USB seals.

Seal solids represent nominal compressed envelopes, not compression simulation.
The USB cassette is a packaging proposal, not a selected supplier STEP.
"""
import argparse
from dataclasses import dataclass, asdict
import json
from pathlib import Path
from build123d import Color, Compound, Cone, Pos, Rot, export_step, export_stl, import_step
from assembly import make_parts as base_parts, validate as base_validate
from assembly import box, rounded, cyl, arm, volume_intersection, CENTER_BOTTOM
from dimensions import D

ROOT=Path(__file__).resolve().parent

@dataclass(frozen=True)
class Sealing:
    glass_w: float=159
    glass_d: float=112
    glass_t: float=1.1
    glass_bond_t: float=.2
    bottom_top: float=-1
    bottom_t: float=2.5
    gasket_free_t: float=1
    gasket_compressed_t: float=.8
    foot_flange_t: float=.5
    usb_y: float=-51
    usb_z: float=16.5

    @property
    def bottom_z(self): return self.bottom_top-self.bottom_t
    @property
    def foot_top(self): return self.bottom_z-self.foot_flange_t
    @property
    def total_height(self): return D.pan_top+self.glass_bond_t+self.glass_t-self.foot_top+D.foot_h
    @property
    def lid_mounts(self): return [(x,y) for x in (-72.5,72.5) for y in (-30,30)]

S=Sealing()

def ring(w,d,iw,id,h,r,ir,z,x=0,y=0):
    return rounded(w,d,h,r,x,y,z)-rounded(iw,id,h+2,ir,x,y,z-1)

def yz(w,h,t,r,x,y,z):
    return Pos(x,y,z)*Rot(0,90,0)*rounded(h,w,t,r)

def xcyl(r,h,x,y,z):
    return Pos(x,y,z)*Rot(0,90,0)*cyl(r,h)

def xcone(r1,r2,h,x,y,z):
    return Pos(x,y,z)*Rot(0,90,0)*Cone(r1,r2,h,align=CENTER_BOTTOM)


def make_parts(snapshot,frame='rectangle'):
    parts,groups,materials=base_parts(snapshot,frame)
    def add(n,s,g,m,c):
        s.label=n;s.color=Color(c)
        parts[n]=s;groups[n]=g;materials[n]=m
    shell=parts['printed_shell']
    chassis=parts['lower_frame']
    # Replace the old mounting bosses, which conflict with top access to feet.
    for x,y in D.shell_mounts:
        shell-=cyl(5.6,8.2,x,y,3.9)
        chassis+=cyl(1.7,D.frame_t,x,y,0)
    # Bottom perimeter flange and captive blind screw bosses, fixed side only.
    flange=ring(160,140,152.4,132.4,3,6,3,-1)
    shell+=flange
    gasket=ring(158,138,154,134,S.gasket_compressed_t,5,3,S.bottom_top)
    shell-=gasket
    for x,y in S.lid_mounts:
        boss=cyl(4,10,x,y,-1)-cyl(1.25,8,x,y,-1)
        # Join the bridge first so each boolean union stays a single solid.
        shell+=arm((x,y),(77 if x>0 else -77,y),5,3,4)
        shell+=boss
        shell-=cyl(1.25,8,x,y,-1)
        # Keep the original aluminium skeleton, relieving only these new bosses.
        chassis-=cyl(4.15,6,x,y,-1)
    # Close v0.3's unselected side aperture, then form a reinforced cassette seat.
    shell+=box(2,13,7,79,-54,13)
    shell+=yz(26,15,4,1,76,S.usb_y,S.usb_z)
    for dy in (-10.5,10.5):
        shell+=xcyl(2.5,6,74,S.usb_y+dy,S.usb_z)
        shell-=xcyl(.8,5,76,S.usb_y+dy,S.usb_z)
    shell-=yz(13,7,10,1,74,S.usb_y,S.usb_z)
    # Outer drainage reliefs are strictly outside the bottom perimeter gasket.
    for x in (-80,80):
        for y in (-20,20): shell-=box(2,4,.5,x,y,-1)
    for x in (-45,45): shell-=box(4,2,.5,x,70,-1)
    add('printed_shell',shell,'fixed','PETG/ASA; sealed flange and blind mounting bosses; finish sealing lands','#303d50')
    add('lower_frame',chassis,'fixed','6061-T6; four local boss reliefs added','#9aaebd')

    add('pan_glass_bond',rounded(S.glass_w,S.glass_d,S.glass_bond_t,5.5,0,D.pan_y,D.pan_top),
        'moving','continuous compliant adhesive; glass-to-pan only, grade/process TBD','#6c7c85')
    glass=rounded(S.glass_w,S.glass_d,S.glass_t,5.5,0,D.pan_y,D.pan_top+S.glass_bond_t)
    add('pan_glass',glass,'moving','1.1 mm cover glass, polished edges; strength/heat process TBD','#354e59')
    parts['pan_glass'].color=Color(.18,.29,.33,.7)

    lid=rounded(160,140,S.bottom_t,6,z=S.bottom_z)
    # A 0.3 mm outer cosmetic/drain gap; it never crosses the inner seal.
    lid-=ring(160,140,158.8,138.8,.31,6,5.4,S.bottom_top-.3)
    add('bottom_perimeter_gasket',gasket,'fixed','closed silicone gasket, 1 mm free / 0.8 mm assembled assumption','#43a8bd')
    for i,(x,y) in enumerate(S.lid_mounts,1):
        lid-=cyl(1.7,S.bottom_t+2,x,y,S.bottom_z-1)
        seal=cyl(3,.5,x,y,S.bottom_z)-cyl(1.8,1.5,x,y,S.bottom_z-.5)
        lid-=seal
        add(f'lid_screw_seal_{i}',seal,'fixed','compressed annular face seal; grade TBD','#43a8bd')
        screw=cyl(3.5,2,x,y,S.bottom_z-2)+cyl(1.2,10,x,y,S.bottom_z)
        add(f'lid_M3_{i}',screw,'fixed','M3x10 flanged head envelope; blind pilot and head seal','#657583')
    for i,(x,y) in enumerate(D.feet,1):
        # Load goes from aluminium frame to metal foot spacer, then rubber foot.
        # The bottom sheet does not set the sensor's primary support stiffness.
        lid-=cyl(4.2,S.bottom_t+2,x,y,S.bottom_z-1)
        seal=cyl(5.6,.5,x,y,S.bottom_z)-cyl(4.4,1.5,x,y,S.bottom_z-.5)
        lid-=seal
        post=cyl(4,-S.bottom_z,x,y,S.bottom_z)+cyl(6,S.foot_flange_t,x,y,S.foot_top)
        post-=cyl(1.25,3,x,y,-3)
        add(f'foot_spacer_{i}',post,'fixed','metal shoulder spacer, blind M3; supports frame directly','#acbdc8')
        add(f'foot_seal_{i}',seal,'fixed','compressed annular face seal above spacer flange','#43a8bd')
        add(f'foot_{i}',cyl(6,D.foot_h,x,y,S.foot_top-D.foot_h),'fixed','rubber pad bonded to metal shoulder','#263544')
        screw=cyl(1.2,6,x,y,-2)+cyl(2.75,3,x,y,4)
        add(f'foot_M3_{i}',screw,'fixed','M3x6 socket head, simplified thread core','#657583')
    # A sealed service hatch provides bottom tool access to the two fixed M6
    # bolts after the foot spacers have attached the bottom sheet to the frame.
    hx,hy=-D.cell_hole_span/2,D.cell_y
    lid-=rounded(14,31,S.bottom_t+2,2,hx,hy,S.bottom_z-1)
    hatch_seal=ring(18,35,14,31,.4,3,2,S.bottom_z,hx,hy)
    lid-=hatch_seal
    hatch=rounded(26,43,1.5,3,hx,hy,S.bottom_z-1.5)
    for i,(dx,dy) in enumerate([(x,y) for x in (-10.5,10.5) for y in (-18.5,18.5)],1):
        x,y=hx+dx,hy+dy
        lid-=cyl(.8,1.8,x,y,S.bottom_z)
        hatch-=cyl(1.1,3,x,y,S.bottom_z-2)
        screw=cyl(.75,3,x,y,S.bottom_z-1.5)+cyl(1.9,1.5,x,y,S.bottom_z-3)
        add(f'service_M2_{i}',screw,'fixed','M2x3 hatch screw; blind lid hole outside hatch seal','#657583')
    add('service_hatch',hatch,'fixed','removable fixed-M6 tool access cover','#506276')
    add('service_hatch_gasket',hatch_seal,'fixed','closed compressed face gasket, material TBD','#43a8bd')
    add('bottom_cover',lid,'fixed','printed polymer; sealing faces finished; 2.5 mm nominal','#39485b')

    # USB extension cassette: compact potted body, flange seal and removable cap.
    # This is the assembled packaging target; dimensions require a real supplier.
    usb_gasket=yz(18,13,.5,2,80,S.usb_y,S.usb_z)-yz(14,9,2,1,79.5,S.usb_y,S.usb_z)
    add('usb_panel_gasket',usb_gasket,'fixed','compressed face gasket; supplier-dependent','#43a8bd')
    module=yz(12.5,6.5,8.5,.8,72,S.usb_y,S.usb_z)+yz(26,15,2,1.5,80.5,S.usb_y,S.usb_z)
    module-=yz(9,3.4,6,1.2,78,S.usb_y,S.usb_z)
    cap_seal=yz(16,9,.2,1.5,82.3,S.usb_y,S.usb_z)-yz(12,5,1.2,.8,81.8,S.usb_y,S.usb_z)
    module-=cap_seal
    for i,dy in enumerate((-10.5,10.5),1):
        y=S.usb_y+dy
        module-=xcyl(1.1,4,79.5,y,S.usb_z)
        module-=xcone(1.1,2.1,1.2,81.3,y,S.usb_z)
        screw=xcyl(.75,4.8,76.5,y,S.usb_z)+xcone(1,2,1.2,81.3,y,S.usb_z)
        add(f'usb_M2_{i}',screw,'fixed','M2x6 countersunk envelope; screws outside panel seal','#657583')
    add('usb_cassette_ENVELOPE',module,'fixed','potted USB-C data+power extension cassette; custom envelope, not vendor model','#788d9b')
    cap=yz(18,11,1.5,2,82.5,S.usb_y,S.usb_z)+yz(9,3.4,1.5,1.2,81,S.usb_y,S.usb_z)+cap_seal
    add('usb_sealing_cap',cap,'fixed','removable silicone cap, closed compressed state; retention fit TBD','#263544')
    return parts,groups,materials


def validate(parts,groups,materials,snapshot):
    report=base_validate(parts,groups,materials,snapshot)
    expected=ring(158,138,154,134,.8,5,3,-1)
    gasket=parts['bottom_perimeter_gasket']
    lid=parts['bottom_cover']
    # No opening or screw interrupts the nominal closed gasket.
    assert abs(volume_intersection(expected,gasket)-expected.volume)<1e-5
    assert len(gasket.solids())==1
    # Continuous lid bearing land below the ring, and continuous shell above it.
    lower=Pos(0,0,-.81)*expected
    upper=Pos(0,0,.81)*expected
    assert abs(volume_intersection(lower,lid)-lower.volume)<1e-5
    assert abs(volume_intersection(upper,parts['printed_shell'])-upper.volume)<1e-5
    for dy in (-D.cell_hole_pitch/2,D.cell_hole_pitch/2):
        tool=cyl(6.05,10,-D.cell_hole_span/2,D.cell_y+dy,-10)
        assert volume_intersection(tool,lid)<1e-5
    glass=parts['pan_glass']
    assert len(glass.solids())==1
    split=[n for n,s in parts.items() if len(s.solids())!=1]
    assert not split,split
    report.update({
        'version':'v0.3.1',
        'overall_mm':[164,140,round(S.total_height,3)],
        'body_plan_mm':[160,140],
        'USB_closed_cap_protrusion_mm':4,
        'glass_mm':[S.glass_w,S.glass_d,S.glass_t],
        'glass_mass_g_at_density_2_5':round(glass.volume*.0025,2),
        'bottom_continuous_gasket_and_bearing_lands':'PASS',
        'all_parts_single_solid':'PASS',
        'fixed_M6_tool_access_with_service_hatch_removed':'PASS',
        'bottom_outer_relief_mm':.3,
        'moving_mass_estimate_g':round(sum(s.volume*(.0027 if n in ('integrated_pan','load_adapter') else
            .0025 if n=='pan_glass' else .0011 if n=='pan_glass_bond' else .00785)
            for n,s in parts.items() if groups[n]=='moving'),2),
        'scope':'Nominal CAD fit only. Seals are compressed envelopes; no leakage, seal pressure, FEA, thermal or weighing validation. USB/cable supplier and routing not selected.'})
    return report


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frame',choices=['rectangle','butterfly'],default='rectangle')
    args=parser.parse_args()
    snapshot=json.loads((ROOT/'references/pcb-snapshot.json').read_text())
    parts,groups,materials=make_parts(snapshot,args.frame)
    out=ROOT/'exports/v031'/args.frame;out.mkdir(parents=True,exist_ok=True)
    report=validate(parts,groups,materials,snapshot)
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    if report['status']!='PASS':
        raise RuntimeError(json.dumps({k:v for k,v in report.items() if k!='parts'},indent=2))
    assembly=Compound(label='coffee_scale_v031_CONCEPT',children=list(parts.values()))
    export_step(assembly,out/'assembly.step')
    readback=import_step(out/'assembly.step')
    assert readback.is_valid and len(readback.solids())==len(parts)
    assert abs(readback.volume-sum(s.volume for s in parts.values()))<1
    (out/'step-roundtrip.json').write_text(json.dumps({'valid':readback.is_valid,'solids':len(readback.solids()),'volume_mm3':round(readback.volume,2)},indent=2)+'\n')
    for n in ('pan_glass','pan_glass_bond','printed_shell','bottom_cover','bottom_perimeter_gasket',
              'lower_frame','fixed_spacer','integrated_pan','load_adapter','pcb_carrier',
              'foot_spacer_1','foot_seal_1','lid_screw_seal_1',
              'service_hatch','service_hatch_gasket',
              'usb_panel_gasket','usb_cassette_ENVELOPE','usb_sealing_cap'):
        export_step(parts[n],out/f'{n}.step')
        export_stl(parts[n],out/f'{n}.stl',tolerance=.05,angular_tolerance=.1)
    (out/'parameters.json').write_text(json.dumps({'v03':asdict(D),'sealing':asdict(S)},indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='parts'},indent=2))

if __name__=='__main__': main()
