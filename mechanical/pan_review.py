"""v0.3 checks: continuous roof, blind mounting, overlap and mass comparison."""
import json
from pathlib import Path
from build123d import Color, export_step, export_stl
from integrated_pan import integrated_pan,load_adapter,rounded,cylinder,dry_rim
from dimensions import D
from assembly import make_parts,volume_intersection

ROOT=Path(__file__).resolve().parent


def main():
    out=ROOT/'exports/v03'
    out.mkdir(parents=True,exist_ok=True)
    pan=integrated_pan()
    adapter=load_adapter()
    # The entire 3 mm roof must contain solid material: no screws or windows.
    roof=rounded(D.pan_w,D.pan_plan_depth,D.pan_skin,6,0,D.pan_y,D.pan_roof_z)
    assert abs((pan & roof).volume-roof.volume)<1e-5
    for x,y in D.pan_mounts:
        land=cylinder(3.5,6,x,y,D.pan_z)
        land-=cylinder(1.66,8,x,y,D.pan_z-1)
        assert abs((land & pan).volume-land.volume)<1e-5,(x,y)
    snapshot=json.loads((ROOT/'references/pcb-snapshot.json').read_text())
    parts,_,_=make_parts(snapshot)
    rim=dry_rim()
    assert abs(volume_intersection(rim,parts['printed_shell'])-rim.volume)<1e-5
    film=parts['front_cover_film']
    assert film.is_valid and len(film.solids())==1
    expected_film=rounded(152,28,D.front_film_t,3,0,-54,
                          D.panel_z+3+D.front_adhesive_t)
    assert abs(volume_intersection(film,expected_film)-expected_film.volume)<1e-5
    # Straight 5 mm tool approach to each M4 head in the loose sensor/pan
    # subassembly. PCB and chassis must be fitted later, not on this path.
    for x,y in D.pan_mounts:
        tool=cylinder(2.5,D.cell_z+D.cell_h-4+10,x,y,-10)
        for name in ('LC1330_ENVELOPE','load_adapter','integrated_pan'):
            assert volume_intersection(tool,parts[name])<1e-5,(x,y,name)
    for name,shape in [('integrated_pan',pan),('load_adapter',adapter)]:
        assert shape.is_valid and len(shape.solids())==1
        shape.label=name;shape.color=Color('#bacbd8')
        export_step(shape,out/f'{name}.step')
        export_stl(shape,out/f'{name}.stl',tolerance=.05,angular_tolerance=.1)
    previous=json.loads((ROOT/'references/v02-mass-baseline.json').read_text())
    old_al=sum(previous['volumes_mm3'].values())*.0027
    data={
        'version':'v0.3',
        'checks':{'entire_3mm_top_skin_solid':'PASS','M4_full_depth_bearing_land':'PASS',
                  'continuous_fixed_upstand_preserved':'PASS','continuous_front_overlay':'PASS',
                  'M4_tool_approach_before_PCB_and_chassis':'PASS'},
        'pan_mm':[D.pan_w,D.pan_plan_depth,D.pan_depth],
        'overall_mm':[D.width,D.depth,D.total_height],
        'pan_mass_g':round(pan.volume*.0027,2),
        'adapter_mass_g':round(adapter.volume*.0027,2),
        'combined_aluminium_mass_g':round((pan.volume+adapter.volume)*.0027,2),
        'v02_three_aluminium_parts_mass_g':round(old_al,2),
        'mass_change_g':round((pan.volume+adapter.volume)*.0027-old_al,2),
        'nominal_M6_sensor_engagement_mm':12-D.adapter_t,
        'nominal_M4_pan_engagement_mm':10-D.adapter_t,
        'M6_head_well_remaining_roof_mm':D.pan_depth-6.5,
        'M4_straight_pilot_remaining_roof_mm':D.pan_depth-D.pan_m4_pilot_depth,
        'nominal_lap':{'radial_gap_mm':D.rim_radial_gap,'overlap_mm':D.rim_top-(D.pan_roof_z-D.pan_skirt_h),
                       'downward_opening_mm':D.underside_gap,'roof_gap_mm':D.rim_roof_gap,
                       'roof_gap_after_stop_travel_mm':D.rim_roof_gap-D.stop_gap},
        'scope':'CAD fit/coverage checks only. No fluid simulation, waterproof rating, material strength or weighing accuracy validation.'}
    (out/'pan-review.json').write_text(json.dumps(data,indent=2)+'\n')
    print(json.dumps(data,indent=2))


if __name__=='__main__':
    main()
