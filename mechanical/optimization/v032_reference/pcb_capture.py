"""Capture live PCB without writing either the PCB or v0.3.1 snapshots."""
import sys,json,math,hashlib
from pathlib import Path
from datetime import datetime,timezone
import sexpdata
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE/'base'))
from pcb_snapshot import children,one
SOURCE=HERE.parents[1]/'pcb/scale-adc-s3/scale-adc-s3.kicad_pcb'

def capture():
 raw=SOURCE.read_bytes();tree=sexpdata.loads(raw.decode())
 outlines=[g for g in children(tree,'gr_rect') if one(g,'layer')[1]=='Edge.Cuts']
 assert len(outlines)==1,'Review changed PCB outline'
 x0,y0=one(outlines[0],'start')[1:3];x1,y1=one(outlines[0],'end')[1:3]
 items=[];holes=[]
 heights={'U4':3.1,'U1':2,'J2':8.5,'J5':3.2,'J6':12,'J7':12,'J8':12,'BZ1':4}
 for f in children(tree,'footprint'):
  props={p[1]:p[2] for p in children(f,'property')};ref=props.get('Reference','')
  at=one(f,'at')[1:];angle=math.radians(at[2] if len(at)>2 else 0)
  def transform(x,y): return [at[0]+x*math.cos(angle)+y*math.sin(angle),at[1]-x*math.sin(angle)+y*math.cos(angle)]
  if 'MountingHole' in str(f[1]):
   for p in children(f,'pad'):
    if str(p[2])=='np_thru_hole':
     assert len(one(p,'drill'))==2,'Slotted mounting hole needs review'
     holes.append({'reference':ref,'at':transform(*one(p,'at')[1:3]),'drill_mm':one(p,'drill')[1],'plated':False})
   continue
  points=[]
  side='B' if one(f,'layer')[1]=='B.Cu' else 'F'
  for layer in (side+'.Fab',side+'.CrtYd'):
   for kind in ('fp_line','fp_rect'):
    for g in children(f,kind):
     if one(g,'layer')[1]==layer:points.extend([one(g,'start')[1:3],one(g,'end')[1:3]])
   for g in children(f,'fp_circle'):
    if one(g,'layer')[1]==layer:
     cx,cy=one(g,'center')[1:3];ex,ey=one(g,'end')[1:3];rr=math.hypot(ex-cx,ey-cy)
     points.extend([[cx-rr,cy-rr],[cx+rr,cy+rr]])
   if points:break
  if not points:continue
  pts=[transform(*p) for p in points]
  height=heights.get(ref, .8 if ref.startswith('R') else 1.5 if ref.startswith(('C','D','Q')) else 2)
  items.append({'reference':ref,'value':props.get('Value'),'footprint':str(f[1]),'at':at,'side':side,
                'bbox':[min(p[0] for p in pts),min(p[1] for p in pts),max(p[0] for p in pts),max(p[1] for p in pts)],
                'height_envelope':height,'height_basis':'assumed, not a vendor STEP'})
 data={'source':str(SOURCE.relative_to(HERE.parents[1])),'sha256':hashlib.sha256(raw).hexdigest(),
       'captured_utc':datetime.now(timezone.utc).isoformat(),'outline':[x0,y0,x1,y1],'width':x1-x0,'depth':y1-y0,
       'thickness':one(one(tree,'general'),'thickness')[1],'mounting_holes':holes,'components':items}
 assert len(holes)==3 and all(abs(h['drill_mm']-2.2)<1e-6 for h in holes)
 (HERE/'references/pcb-snapshot.json').write_text(json.dumps(data,indent=2)+'\n')
 return data

if __name__=='__main__':
 d=capture();print(json.dumps({k:v for k,v in d.items() if k!='components'},indent=2));print('Components:',len(d['components']))
