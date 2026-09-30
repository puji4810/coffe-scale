"""Compare exported nominal geometry, not measured assembly stiffness."""
import json
from pathlib import Path
from build123d import import_step
from model import HERE,box

def section(s):
 t=.1;q=s & box(t,200,100,0,0,0);a=q.volume/t
 return {'A_mm2':a,'I_y_mm4':q.matrix_of_inertia[1][1]/t-a*t*t/12}

def main():
 root=HERE.parent/'exports'
 p31=import_step(root/'v031-optimized/integrated_pan.step')
 p32=import_step(root/'v032-optimized/pan_cover.step')
 w32=import_step(root/'v032-optimized/wide_support.step')
 s31=section(p31);s32=section(p32+w32)
 reports={v:json.loads((root/f'{v}-optimized/validation.json').read_text()) for v in ('v031','v032')}
 data={'variants':{v:{'moving_aluminium_g':r['moving_aluminium_g'],'moving_total_estimate_g':r['moving_total_estimate_g'],'CAD_status':r['status']} for v,r in reports.items()},
       'local_section_plane':'X=0, vertical bending about Y, excludes glass',
       'v031_optimized_monolithic_section':s31,'v032_optimized_perfect_composite_section':s32,
       'ideal_local_section_ratio_v032_over_v031':s32['I_y_mm4']/s31['I_y_mm4'],
       'scope':'Local section geometry only. v032 requires perfect shear transfer/contact to achieve composite section behavior. This does not establish whole-assembly stiffness or weighing accuracy.'}
 out=root/'optimization';out.mkdir(exist_ok=True)
 (out/'comparison.json').write_text(json.dumps(data,indent=2)+'\n')
 print(json.dumps(data,indent=2))
if __name__=='__main__':main()
