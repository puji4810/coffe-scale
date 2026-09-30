"""Geometry-based support comparison and local section calculation.

No finite-element analysis. I ratios compare isolated sections, not assemblies.
"""
import json
from pathlib import Path
from build123d import Color, Pos, export_step, export_stl
from upper_support import ribbed_support, flat_reference, cylinder
from dimensions import D

ROOT = Path(__file__).resolve().parent


def section_properties(rectangles):
    # Each rectangle is (width, height, bottom z), in mm.
    area = sum(w*h for w,h,z in rectangles)
    centroid = sum(w*h*(z+h/2) for w,h,z in rectangles)/area
    inertia = sum(w*h**3/12+w*h*(z+h/2-centroid)**2 for w,h,z in rectangles)
    return {'area_mm2': area, 'centroid_z_mm': centroid, 'I_mm4': inertia}


def main():
    out = ROOT/'exports/upper-support'
    out.mkdir(parents=True, exist_ok=True)
    variants = {'flat_3mm_reference': flat_reference(),
                'flat_6mm_comparison': flat_reference(6),
                'ribbed_10mm': ribbed_support()}
    metrics = {}
    for name, shape in variants.items():
        assert shape.is_valid and len(shape.solids()) == 1
        shape.label = name
        shape.color = Color('#5b9aa2')
        export_step(shape, out/f'{name}.step')
        export_stl(shape, out/f'{name}.stl', tolerance=.05, angular_tolerance=.1)
        metrics[name] = {'volume_mm3': round(shape.volume, 3),
                         'estimated_mass_g': round(shape.volume*.0027, 2)}
    support = variants['ribbed_10mm']
    # Check M3 blind bores are closed at their bottom and retain a full-height
    # annular land, rather than accidentally opening into an underside pocket.
    for x,y in D.plate_mounts:
        land = cylinder(3, D.upper_m3_pilot_depth, x, y, D.plate_z-D.upper_m3_pilot_depth)
        land -= cylinder(1.26, D.upper_m3_pilot_depth+2, x, y, D.plate_z-D.upper_m3_pilot_depth-1)
        assert abs((land & support).volume-land.volume) < 1e-5, (x,y,'thin M3 wall')
        floor = cylinder(1.2, 1, x, y, D.spider_z+.1)
        assert abs((floor & support).volume-floor.volume) < 1e-5, (x,y,'open M3 bore')
    # Allen-key access to the M6 socket heads before fitting the platter.
    for dy in (-D.cell_hole_pitch/2,D.cell_hole_pitch/2):
        key = cylinder(3, 25, D.cell_hole_span/2, D.cell_y+dy, D.plate_z-.5)
        hit = key & support
        assert hit is None or hit.volume < 1e-5
    # Restrict the effective flange to 13 mm for a stated local comparison.
    # This chosen strip width is not an effective-width analysis of the plate.
    sections = {
        'flat_3mm_13mm_wide': section_properties([(13,3,0)]),
        'flat_6mm_13mm_wide': section_properties([(13,6,0)]),
        'ribbed_chosen_13mm_flange_strip': section_properties([
            (13,D.upper_skin,D.upper_depth-D.upper_skin),
            (D.upper_center_rib,D.upper_depth-D.upper_skin,0)])}
    baseline = sections['flat_3mm_13mm_wide']['I_mm4']
    for data in sections.values():
        data['I_ratio_to_flat_3mm'] = data['I_mm4']/baseline
    metrics['scope'] = ('Mass uses density 2.70 g/cm3. I values are geometric section '
                        'comparisons under equal E and length; NOT assembly stiffness '
                        'ratios, torsional predictions, FEA or weighing error estimates.')
    metrics['sections'] = sections
    metrics['checks'] = {'M3_full_depth_land': 'PASS', 'M3_blind_bore_floor': 'PASS',
                         'M6_tool_access_before_platter': 'PASS'}
    metrics['dimensions_mm'] = {'width':D.upper_w,'depth':D.upper_d,'height':D.upper_depth,
                                'roof':D.upper_skin,'perimeter_wall':D.upper_wall,
                                'central_rib':D.upper_center_rib,
                                'pocket_depth':D.upper_depth-D.upper_skin,
                                'pocket_corner_radius':D.upper_pocket_r}
    (out/'comparison.json').write_text(json.dumps(metrics, indent=2)+'\n')
    print(json.dumps(metrics, indent=2))


if __name__ == '__main__':
    main()
