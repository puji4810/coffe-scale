"""LC7012 v0.4.1: centred equal-radius spider, actual s3.1 PCB, 103450/TFT.
All units mm. Sensor/electronics are installation envelopes, not vendor solids.
"""
from pathlib import Path
from itertools import combinations
import json, math
from build123d import (Align, Box, Cylinder, Pos, Rot, RectangleRounded, extrude,
                       Color, Compound, export_step, export_stl, import_step)
HERE=Path(__file__).resolve().parent
OUT=HERE.parent/'exports/v041'
CB=(Align.CENTER,Align.CENTER,Align.MIN)
CY=19.0
TOUCH_W,TOUCH_D=15.0,11.0
TOUCH_X=(-49.0,49.0)
GLASS_T=1.1
UPPER_SHIFT=1.5
MOUNTS=[(x,CY+y) for x in (-57,57) for y in (-33,33)]
FEET=[(x,y) for x in (-71,71) for y in (-61,61)]
STOPS=[(x,CY+y) for x in (-68,68) for y in (-23,23)]

def box(w,d,h,x=0,y=0,z=0): return Pos(x,y,z)*Box(w,d,h,align=CB)
def cyl(r,h,x=0,y=0,z=0): return Pos(x,y,z)*Cylinder(r,h,align=CB)
def rounded(w,d,h,r=3,x=0,y=0,z=0): return Pos(x,y,z)*extrude(RectangleRounded(w,d,r),h)
def ring(w,d,t,h,r=4,x=0,y=0,z=0):
    return rounded(w,d,h,r,x,y,z)-rounded(w-2*t,d-2*t,h+2,max(.5,r-t),x,y,z-1)
def arm(a,b,w,h,z):
    dx,dy=b[0]-a[0],b[1]-a[1]
    return Pos((a[0]+b[0])/2,(a[1]+b[1])/2,z)*Rot(0,0,math.degrees(math.atan2(dy,dx)))*Box(math.hypot(dx,dy)+w,w,h,align=CB)
def pcb_xy(x,y): return x-70,-10-y

def make_parts(snapshot):
    parts={}; groups={}; materials={}
    def add(n,s,g,m,c):
        s=Compound(children=list(s)) if isinstance(s,list) else s
        s.label=n;s.color=Color(c);parts[n]=s;groups[n]=g;materials[n]=m
    # Base perimeter and lateral spine: no metal under the antenna keepout.
    frame=ring(150,130,8,4,5,z=2)+box(142,20,4,0,CY,2)
    frame+=box(12,12,3,-58,CY,6)
    for yy in (CY-3.5,CY+3.5):
        frame-=cyl(1.7,10,-58,yy,1)
        frame-=cyl(3,3,-58,yy,2)
    # Feet, downward travel adjusters and PCB tray attach to the frame.
    for x,y in FEET: frame-=cyl(1.25,6,x,y,1)
    for x,y in STOPS:
        frame+=cyl(5,4,x,y,2);frame+=arm((x,y),(71 if x>0 else -71,y),8,4,2)
        frame-=cyl(1.65,5,x,y,2)
    carrier_mounts=[(-61,-64),(11,-64),(-61,-11),(11,-11)]
    for x,y in carrier_mounts:
        frame+=arm((x,y),(-71 if x<0 else 0,-61 if y<-30 else CY),7,4,2)
        frame-=cyl(1.25,4,x,y,2)
    # Antenna clearance through lower frame (15 mm around radiating end).
    frame-=box(48,20,7,-25,-3.25,1)
    # Final bores after all ribs have been united.
    for x,y in FEET:frame-=cyl(1.25,6,x,y,1)
    for x,y in STOPS:frame-=cyl(1.65,6,x,y,1)
    # Base-to-shell M3 fasteners, in fixed-side tabs.
    for x,y in [(-71,30),(71,30)]:frame-=cyl(1.7,5,x,y,1.5)
    frame=frame & box(152,134,12,0,0,0)
    add('lower_frame',frame,'fixed','6061 aluminium','#91a7b5')
    for i,(x,y) in enumerate(FEET,1):
        foot=cyl(6,4,x,y,-4)+cyl(3.5,2,x,y,0)
        foot-=cyl(1.25,2,x,y,0)
        add(f'foot_{i}',foot,'fixed','rubber foot with threaded insert envelope','#283443')
        add(f'foot_M3_{i}',cyl(1.2,6,x,y,0)+cyl(2.7,2,x,y,6),'fixed','M3 screw simplified thread core','#697b89')
    # Lower frame, electronics mounting stack and sensor fixed seat: -1 mm.
    frame=parts['lower_frame']-box(14,14,3,-58,CY,7)
    add('lower_frame',frame,'fixed','6061 aluminium; fixed pedestal now 1 mm','#91a7b5')
    sensor=box(70,12,22,-29,CY,7)
    for x in (-58,0):
        for y in (CY-3.5,CY+3.5):sensor-=cyl(1.3,24,x,y,6)
    add('LC7012_ENVELOPE',sensor,'sensor','70x12x22;4xM3;58x7; installation envelope','#bfced5')
    for i,y in enumerate((CY-3.5,CY+3.5),1):
        add(f'fixed_M3_{i}',cyl(2.75,3,-58,y,2)+cyl(1.2,8,-58,y,5),'fixed','M3x8;6 mm nominal engagement','#586776')
    # Everything below this line is authored at FINAL z; lower parts move at end.
    parts['LC7012_ENVELOPE']=Pos(0,0,-1)*parts['LC7012_ENVELOPE']
    for n in ('fixed_M3_1','fixed_M3_2'):parts[n]=Pos(0,0,-1)*parts[n]
    spacer=box(12,12,2.5,0,CY,28)
    for y in (CY-3.5,CY+3.5):spacer-=cyl(1.7,5,0,y,27)
    add('moving_spacer',spacer,'moving','6061 aluminium;2.5 mm nominal upper beam gap','#c3aa79')
    spider=rounded(24,22,6,4,0,CY,29)
    # Identical tapered arms: 28 mm at root,12 mm at distal end,6 mm thick.
    # Rounded distal ends avoid collision with fixed peripheral rain lip.
    from build123d import Polygon
    for x,y in MOUNTS:
        dx,dy=x,y-CY;l=math.hypot(dx,dy);nx,ny=-dy/l,dx/l
        polygon=[(nx*14,CY+ny*14),(-nx*14,CY-ny*14),
                 (x-nx*6,y-ny*6),(x+nx*6,y+ny*6)]
        spider+=Pos(0,0,29)*extrude(Polygon(*polygon,align=None),6)
        spider+=cyl(6,6,x,y,29)
    for x,y in MOUNTS:
        spider-=cyl(1.7,8,x,y,28)
        spider-=cyl(4.25,4.3,x,y,30.8)
    for y in (CY-3.5,CY+3.5):
        spider-=cyl(1.7,8,0,y,28);spider-=cyl(3,3.3,0,y,31.7)
    add('equal_arm_spider',spider,'moving','6061 aluminium;four identical tapered28-to12x6 arms;stiffness unverified','#dfaa57')
    for i,y in enumerate((CY-3.5,CY+3.5),1):
        add(f'moving_M3_{i}',cyl(1.2,10,0,y,21.7)+cyl(2.75,3,0,y,31.7),'moving','M3x10;4.8 mm nominal sensor engagement;head clearance0.3','#586776')
    pan=rounded(160,102,2,6,0,CY,35)+ring(160,102,2,5,6,0,CY,30)
    for x,y in MOUNTS:
        pan+=cyl(4,4,x,y,31);pan-=cyl(1.25,4.5,x,y,31)
    add('pan_cover',pan,'moving','6061 aluminium;2 mm skin,1.5 mm over blind holes','#bfd1dc')
    for i,(x,y) in enumerate(MOUNTS,1):
        add(f'pan_M3_{i}',cyl(2.75,3,x,y,26)+cyl(1.2,6,x,y,29),'moving','M3x6 from below;4 mm engagement','#586776')
    add('pan_glass_bond',rounded(158,100,.2,5,0,CY,37),'moving','adhesive only on moving pan','#6f8593')
    add('pan_glass',rounded(159,101,1.1,5.5,0,CY,37.2),'moving','glass','#253640')
    # Fixed fascia and moving pan use separate glass plates with a 3 mm glass-to-glass plan gap.
    shell=ring(160,140,2,27.5,6,z=1)
    shell+=box(160,2,13.5,0,-69,25)
    shell+=box(2,35,11.5,-79,-50.5,27)
    shell+=box(2,35,11.5,79,-50.5,27)
    shell+=box(156,35,4,0,-50.5,34.5)
    shell+=box(6,86,2,-75,CY,26.5)
    shell+=box(6,86,2,75,CY,26.5)
    shell+=ring(152,94,2,6,4,0,CY,28.5)
    # Recess TFT and two touch modules; touch face meets glass through 0.2 mm adhesive.
    shell-=box(38,31,5,0,-52.5,31.5)
    shell-=rounded(37.08,20.19,5,1,0,-52.5,35.5)
    for x in (-17,17):
        shell+=box(3,3,2,x,-37,32.5)+box(3,2,4,x,-35,32.5)
    for x in TOUCH_X:
        shell-=box(TOUCH_W+.8,TOUCH_D+.8,8,x,-52.5,31.5)
        shell-=box(TOUCH_W+.8,6,8,x,-60,31.5)
    for x,y in [(-71,30),(71,30)]:
        shell+=arm((x,y),(-76 if x<0 else 76,y),7,4,5)
        shell-=cyl(1.25,4,x,y,5)
    for i,(x,y) in enumerate([(x,CY+d) for x in (-71,71) for d in (-15,15)],1):
        shell+=arm((x,y),(-75 if x<0 else 75,y),8,2,24.5)
        shell-=cyl(1.9,12,x,y,23.5)
        pan=parts['pan_cover']+cyl(2.5,4,x,y,31)
        pan-=cyl(1.25,4.5,x,y,31)
        add('pan_cover',pan,'moving','6061 aluminium;1.5 mm minimum continuous top','#bfd1dc')
        pin=cyl(2.75,2,x,y,20)+cyl(1.5,9,x,y,22)+cyl(1.2,4,x,y,31)
        add(f'capture_pin_{i}',pin,'moving','shoulder pin;1 mm lift and0.4 mm radial gap','#c8a355')
    shell-=box(6,12,7,79,-30,14)
    add('printed_shell',shell,'fixed','PETG/ASA concept shell; touch recesses','#344354')
    for i,(x,y) in enumerate([(-71,30),(71,30)],1):
        add(f'shell_M3_{i}',cyl(2.75,3,x,y,-1)+cyl(1.2,7,x,y,2),'fixed','M3x7 nominal envelope','#697b89')
    # Full-width fixed tempered glass. Black layer is INSULATING ink, window unprinted.
    add('fascia_glass_bond',ring(158,33,1,.2,3,0,-51.5,37),'fixed','0.2 mm perimeter adhesive','#718797')
    add('fascia_glass',rounded(159,34,GLASS_T,3.5,0,-51.5,37.2),'fixed','tempered cover glass;independent of moving weighing glass','#12202b')
    # Render mask and transparent aperture as disjoint solids of one glass part.
    glass=parts['fascia_glass'];window=rounded(35,18,GLASS_T,1,0,-52.5,37.2)
    add('fascia_glass',glass-window,'fixed','black-backed glass outside display window; export glass_blank as cutting outline','#17222c')
    add('fascia_clear_window',window,'fixed','same uncut glass; optical material split for visualization','#7bbac9')
    parts['fascia_clear_window'].color=Color(.45,.75,.85,.4)
    add('TFT_PCB_ENVELOPE',box(37,30,1.3,0,-52.5,33),'fixed','37x30 module;1.3 mm PCB stack assumed','#377967')
    add('TFT_PANEL_ENVELOPE',box(36.28,19.39,1.46,0,-52.5,34.3),'fixed','user dimensions;centred placement provisional','#101b29')
    add('TFT_HEADER_KEEPOUT',box(22,4,6,0,-64.5,27),'keepout','max6 mm connector/wire projection;straight tall Dupont excluded','#c28f72')
    for i,x in enumerate(TOUCH_X,1):
        add(f'TTP223_{i}_PCB_ENVELOPE',box(TOUCH_W,TOUCH_D,1,x,-52.5,36),'fixed','15x11x1 assumed PCB;electrode face upward','#357d67')
        add(f'TTP223_{i}_COMPONENT_ENVELOPE',box(TOUCH_W-2,TOUCH_D-2,2.5,x,-52.5,33.5),'fixed','downward IC/passives envelope;verify actual board','#566372')
        add(f'TTP223_{i}_ADHESIVE',box(TOUCH_W,TOUCH_D,.2,x,-52.5,37),'fixed','thin insulating adhesive;no foam/air gap over electrode','#94947b')
        add(f'TTP223_{i}_WIRE_KEEPOUT',box(TOUCH_W,4,4,x,-60,32),'keepout','soldered leads toward front;no vertical pin header','#c28f72')
    board=box(snapshot['width'],snapshot['depth'],snapshot['thickness'],-25,-37.5,11)
    carrier=rounded(94,59,2,3,-25,-37.5,6)
    for h in snapshot['mounting_holes']:
        x,y=pcb_xy(*h['at']);board-=cyl(h['drill_mm']/2,3,x,y,10.5)
        carrier+=cyl(2.5,3,x,y,8);carrier-=cyl(1.6,6,x,y,6)
        insert=cyl(1.55,4,x,y,7)-cyl(.85,5,x,y,6.5)
        add(h['reference']+'_M2_insert',insert,'fixed','M2 heat-set insert envelope OD3.1x4;fit supplier TBD','#c8a355')
        screw=cyl(.8,6,x,y,6.6)+cyl(1.65,1.5,x,y,12.6)
        add(h['reference']+'_M2_screw',screw,'fixed','M2x6 small-head envelope; screw head OD3.3 required near TP3','#697b89')
    for x,y in carrier_mounts:carrier-=cyl(1.7,3,x,y,5.5)
    for x,y in FEET:carrier-=cyl(3.1,4,x,y,5)
    add('pcb_carrier',carrier,'fixed','printed carrier;3 mm underside solder clearance','#699aab')
    add('PCB_ENVELOPE',board,'fixed','s3.1 actual outline and 4 NPTH holes','#247b65')
    for i,(x,y) in enumerate(carrier_mounts,1):
        add(f'carrier_M3_{i}',cyl(1.2,5,x,y,3)+cyl(2.75,2,x,y,8),'fixed','M3x5;3 mm nominal engagement','#697b89')
    for c in snapshot['components']:
        x0,y0,x1,y1=c['bbox'];x,y=pcb_xy((x0+x1)/2,(y0+y1)/2)
        z=12.6 if c['side']=='F' else 11-c['height_envelope']
        add('PCB_'+c['reference'],box(x1-x0,y1-y0,c['height_envelope'],x,y,z),'component','conservative fabrication-outline component envelope','#4c6470')
    # Battery side bay, positive wiring exit towards J6.
    tray=rounded(39,55,2,3,49,-39,6)+ring(39,55,1.5,9,3,49,-39,8)
    tray-=box(5,8,7,30,-59,10)
    for y in (-50,-25):tray+=arm((49,y),(71.5,y),6,2,6)
    for x,y in FEET:tray-=cyl(3.1,4,x,y,5)
    for i,y in enumerate((-50,-25),1):
        tray-=cyl(1.7,3,71.5,y,5.5)
        frame=parts['lower_frame']-cyl(1.25,4,71.5,y,2)
        add('lower_frame',frame,'fixed','6061 aluminium','#91a7b5')
        add(f'battery_tray_M3_{i}',cyl(1.2,5,71.5,y,3)+cyl(2.75,2,71.5,y,8),'fixed','M3x5 tray screw','#697b89')
    add('battery_tray',tray,'fixed','printed; retained with adhesive/strap;clearance1 mm per side','#699aab')
    add('battery_pad',box(34,50,.5,49,-39,8),'fixed','removable battery adhesive','#b6ae84')
    add('BATTERY_103450',box(34,50,10,49,-39,8.5),'fixed','user 103450;34x50x10;50 mm lead','#bdbdc2')
    # Shorter adjusters: final pan underside36.5, nose top35.9.
    for i,(x,y) in enumerate(STOPS,1):
        post=cyl(3.5,23.5,x,y,5)+cyl(1.5,4,x,y,1)
        post-=cyl(1.65,9,x,y,20.5)
        add(f'stop_post_{i}',post,'fixed','M4 blind-tapped adjustable post','#b58450')
        add(f'stop_tip_{i}',cyl(1.5,10,x,y,24.9)+cyl(2.5,1,x,y,34.9),'fixed','flat nose;gap0.6 provisional','#d6ad75')
    # Bottom gasket/cover remain entirely fixed; no gasket crosses load path.
    cover=rounded(160,140,2,6,z=-.8)
    for x,y in FEET:cover-=cyl(6.3,4,x,y,-1.5)
    for x,y in [(-71,30),(71,30)]:cover-=cyl(3.1,4,x,y,-1.5)
    # Local cover screws into shell corner pads (separate from chassis screws).
    for i,(x,y) in enumerate([(-76,-42),(76,-42),(-76,51),(76,51)],1):
        shell=parts['printed_shell']+cyl(2.8,6,x,y,1)
        shell-=cyl(1.25,6,x,y,1)
        frame=parts['lower_frame']-cyl(3.1,5,x,y,1.5)
        add('lower_frame',frame,'fixed','6061 aluminium','#91a7b5')
        add('printed_shell',shell,'fixed','PETG/ASA concept shell','#344354')
        cover-=cyl(1.7,3,x,y,-1)
        add(f'cover_M3_{i}',cyl(2.75,1.5,x,y,-2.3)+cyl(1.2,6,x,y,-.8),'fixed','M3x6;3.2 mm engagement','#697b89')
    add('bottom_cover',cover,'fixed','printed bottom cover','#3b4c5d')
    add('bottom_gasket',ring(159,139,1,.8,5.5,z=1.2),'fixed','perimeter silicone gasket compressed envelope','#9ab2ba')
    add('USB_PANEL_ENVELOPE',box(4,11,6,79,-30,15.5),'fixed','panel extension body placeholder; selected connector TBD','#7c9aab')
    for n in parts:
        if (groups[n]=='moving' and n!='moving_spacer') or n.startswith(('fascia_','TFT_','TTP223_')):
            parts[n]=Pos(0,0,UPPER_SHIFT)*parts[n]
    lower_names={'lower_frame','pcb_carrier','PCB_ENVELOPE','battery_tray','battery_pad',
                 'BATTERY_103450','bottom_cover','bottom_gasket','USB_PANEL_ENVELOPE'}
    for n in parts:
        if n in lower_names or n.startswith(('PCB_','MH','carrier_M3_','battery_tray_M3_','shell_M3_','cover_M3_','foot_M3_')):
            parts[n]=Pos(0,0,-1)*parts[n]
    # Keep floor datum z=-4; underside hardware stays at least0.7 mm above floor.
    for i,(x,y) in enumerate(FEET,1):
        foot=cyl(6,3,x,y,-4)+cyl(3.5,2,x,y,-1)
        foot-=cyl(1.25,2,x,y,-1)
        add(f'foot_{i}',foot,'fixed','3 mm rubber plus2 mm shoulder','#283443')
    return parts,groups,materials

def hits(a,b):
    aa,bb=a.bounding_box(),b.bounding_box()
    if any(getattr(aa.max,k)<=getattr(bb.min,k)+1e-6 or getattr(bb.max,k)<=getattr(aa.min,k)+1e-6 for k in ('X','Y','Z')):return 0
    common=a&b
    return 0 if common is None else common.volume

def validate(parts,groups,materials,snapshot):
    invalid=[n for n,s in parts.items() if not s.is_valid or len(s.solids())!=1 or s.volume<=0]
    collisions=[]
    # Component-to-component bounding boxes are not actual body meshes.
    for a,b in combinations(parts,2):
        if groups[a]==groups[b]=='component':continue
        # Thread cores and contact faces are explicitly modelled without overlap.
        v=hits(parts[a],parts[b])
        if v>1e-5:collisions.append([a,b,round(v,5)])
    travel=[]
    moving=[n for n in parts if groups[n]=='moving']
    for dz in (-.3,-.6,1.0):
        for a in moving:
            for b in parts:
                if groups[b]=='moving':continue
                if b=='LC7012_ENVELOPE' and (a=='moving_spacer' or a.startswith('moving_M3_')):continue
                v=hits(Pos(0,0,dz)*parts[a],parts[b])
                if v>1e-5:travel.append([dz,a,b,round(v,5)])
    # The entire symmetric spider is invariant under 180-degree rotation.
    rotated=Pos(0,CY,0)*Rot(0,0,180)*Pos(0,-CY,0)*parts['equal_arm_spider']
    symmetry=abs(parts['equal_arm_spider'].volume-hits(parts['equal_arm_spider'],rotated))
    lever=[math.hypot(x,y-CY) for x,y in MOUNTS]
    lift_gaps=[parts[f'capture_pin_{i}'].distance_to(parts['printed_shell']) for i in range(1,5)]
    lift_contact=[(Pos(0,0,1)*parts[f'capture_pin_{i}']).distance_to(parts['printed_shell']) for i in range(1,5)]
    gaps=[parts['pan_cover'].distance_to(parts[f'stop_tip_{i}']) for i in range(1,5)]
    antenna=box(48,20,33.1,-25,-3.25,-3.4)
    metals=['lower_frame','equal_arm_spider','moving_spacer','pan_cover','LC7012_ENVELOPE','BATTERY_103450']
    rf_hits=[n for n in metals if hits(antenna,parts[n])>1e-5]
    j6=next(c for c in snapshot['components'] if c['reference']=='J6')
    jx,jy=pcb_xy(*j6['at'][:2])
    wire_points=[(32,-59,20),(26,-59,22),(jx,jy,22)]
    wire_length=sum(math.dist(a,b) for a,b in zip(wire_points,wire_points[1:]))
    skin=rounded(160,102,1.5,6,0,CY,37)
    skin_ok=abs(skin.volume-hits(skin,parts['pan_cover']))<1e-4
    passed=not(invalid or collisions or travel or rf_hits) and symmetry<1e-4 and skin_ok and all(abs(v-.6)<1e-4 for v in gaps) and all(abs(v-.4)<1e-4 for v in lift_gaps) and all(v<1e-4 for v in lift_contact)
    report={'version':'v0.4.1 LC7012','status':'PASS' if passed else 'FAIL','invalid_or_split_solids':invalid,'collisions_mm3':collisions,'travel_collisions_mm3':travel,'part_count':len(parts),'pcb_sha256':snapshot['sha256'],'pcb_holes':[{**h,'assembly_xy':pcb_xy(*h['at'])} for h in snapshot['mounting_holes']], 'arm_radii_mm':lever,'spider_180deg_symmetry_difference_mm3':symmetry,'load_group_and_pan_center_xy':[0,CY],'centre_payload_external_eccentric_moment_Nm':0,'down_stop_gap_mm':gaps,'capture_minimum_gap_mm':lift_gaps,'capture_contact_at_1mm_lift':lift_contact,'antenna_metal_or_battery_hits':rf_hits,'continuous_top_1_5mm':skin_ok,'battery_lead_available_mm':50,'battery_lead_proposed_route_mm':round(wire_length,2),'battery_lead_route_points':wire_points,'battery_lead_remaining_mm':round(50-wire_length,2),'lead_scope':'Routing estimate only; pouch lead exit/connector datum and bends require sample confirmation. Cable not modelled as solid.','nominal_body_with_feet_mm':[160,140,43.8],'moving_aluminium_mass_g':round(sum(parts[n].volume*.0027 for n in ('equal_arm_spider','moving_spacer','pan_cover')),2),'scope':'Nominal CAD only; no FEA, tilt travel, harness solid collision, waterproof or weighing-accuracy certification. Vendor sensor solid and TFT header location not supplied.','component_pair_check':'Excluded only component-component envelopes; every component checked against structure and moving travel.','sensor_travel_exception':'Only moving_spacer/moving_M3 vs deforming sensor envelope.','parts':{n:{'group':groups[n],'material':materials[n],'volume_mm3':round(s.volume,3)} for n,s in parts.items()}}


    rf_travel=[n for n in metals if groups[n]=='moving' and hits(antenna,Pos(0,0,-.6)*parts[n])>1e-5]
    touch_contact=[]
    for i in (1,2):
        board=parts[f'TTP223_{i}_PCB_ENVELOPE'];adhesive=parts[f'TTP223_{i}_ADHESIVE']
        touch_contact.append({'module':i,'board_to_adhesive_mm':board.distance_to(adhesive),
                              'adhesive_to_glass_mm':adhesive.distance_to(parts['fascia_glass'])})
    actual_height=max(s.bounding_box().max.Z for n,s in parts.items() if groups[n]!='keepout')-min(s.bounding_box().min.Z for n,s in parts.items() if groups[n]!='keepout')
    expected_height=max(38.3,37.2+GLASS_T)+UPPER_SHIFT+4
    report.update({'antenna_down_travel_hits':rf_travel,'touch_face_contacts':touch_contact,
                   'actual_overall_height_mm':round(actual_height,4),'height_reduction_mm':round(50.3-actual_height,4),
                   'pcb_underside_clearance_mm':3,'sensor_under_beam_gap_mm':1,
                   'sensor_over_beam_gap_mm':2.5,'upper_gap_after_down_travel_mm':1.9,
                   'touch_module_assumed_mm':[TOUCH_W,TOUCH_D,3.5],'front_glass_mm':[159,34,GLASS_T],
                   'TFT_max_underside_connector_projection_mm':6,'stiffness_status':'Unverified: arms8->6,pan3->2; tapered arms are not proof of equal stiffness to v04.'})
    contact_ok=all(c['board_to_adhesive_mm']<1e-5 and c['adhesive_to_glass_mm']<1e-5 for c in touch_contact)
    if rf_travel or not contact_ok or abs(actual_height-expected_height)>1e-4:report['status']='FAIL'
    return report


def main():
    snapshot=json.loads((HERE/'references/pcb-snapshot.json').read_text())
    parts,groups,materials=make_parts(snapshot);OUT.mkdir(parents=True,exist_ok=True)
    report=validate(parts,groups,materials,snapshot)
    (OUT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='parts'},indent=2),flush=True)
    if report['status']!='PASS':raise RuntimeError('See validation.json')
    # Keepout volumes are useful for clearance checks but are not hardware.
    physical={n:s for n,s in parts.items() if groups[n]!='keepout'}
    assembly=Compound(label='LC7012_v041_SLIM_CONCEPT',children=list(physical.values()))
    export_step(assembly,OUT/'assembly.step');back=import_step(OUT/'assembly.step')
    assert back.is_valid and len(back.solids())==len(physical)
    delta=abs(back.volume-sum(s.volume for s in physical.values()));assert delta<.1
    (OUT/'step-roundtrip.json').write_text(json.dumps({'valid':back.is_valid,'solids':len(back.solids()),'volume_delta_mm3':delta},indent=2)+'\n')
    for n in ['equal_arm_spider','moving_spacer','pan_cover','lower_frame','printed_shell','pcb_carrier','battery_tray','bottom_cover','PCB_ENVELOPE','LC7012_ENVELOPE','TFT_PCB_ENVELOPE','BATTERY_103450','TTP223_1_PCB_ENVELOPE']:
        export_step(parts[n],OUT/f'{n}.step');export_stl(parts[n],OUT/f'{n}.stl',tolerance=.05,angular_tolerance=.1)
    export_step(rounded(159,34,GLASS_T,3.5,0,-51.5,37.2+UPPER_SHIFT),OUT/'fascia_glass_blank.step')
    (OUT/'parameters.json').write_text(json.dumps({'pan_center':[0,CY],'pan_size':[160,102,2],'pan_mounts':MOUNTS,'sensor_center':[-29,CY,17],'sensor_size':[70,12,22],'sensor_hole_group_pitch':58,'sensor_same_end_pitch':7,'sensor_thread':'M3','pcb_transform':'X=KiCad_X-70;Y=-10-KiCad_Y;Z=10','display_landscape_overall':[37,30,2.76],'display_panel':[36.28,19.39,1.46],'battery':[34,50,10],'battery_lead':50,'touch_module_assumed_mm':[TOUCH_W,TOUCH_D,3.5],'fixed_glass_mm':[159,34,GLASS_T],'source_snapshot':'mechanical/v041/references/pcb-snapshot.json'},indent=2)+'\n')
if __name__=='__main__':main()
