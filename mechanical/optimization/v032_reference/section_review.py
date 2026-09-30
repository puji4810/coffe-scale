"""Exact local CAD section properties, NOT FEA or assembled stiffness bounds.

A 0.1 mm slice at X=0 is uniform along X. Its centroidal volume inertia about
Y, divided by thickness, minus A*t^2/12 gives integral (Z-Zc)^2 dA in mm^4.
No glass, bolt preload, friction, contact opening or torsion is represented.
"""
import json
from model import HERE,wide_plate,pan_cover,box
from integrated_pan import integrated_pan


def section(s):
    t=.1;q=s & box(t,200,100,0,0,0)
    area=q.volume/t
    return {'area_mm2':area,'centroid_z_mm':q.center().Z,
            'I_y_mm4':q.matrix_of_inertia[1][1]/t-area*t*t/12}


def main():
    old=integrated_pan();support=wide_plate();cover=pan_cover()
    a,b,c,d=section(old),section(support),section(cover),section(support+cover)
    result={'section_plane':'X=0; vertical bending along X about Y',
            'v031_integrated':a,'v032_support_only':b,'v032_cover_only':c,
            'v032_perfect_composite':d,
            'sum_individual_centroidal_I_mm4':b['I_y_mm4']+c['I_y_mm4'],
            'ideal_composite_I_ratio':d['I_y_mm4']/a['I_y_mm4'],
            'scope':'Local geometric section comparison only. Perfect composite assumes no relative slip or opening. Sum of individual I is an idealized reference, not an assembly lower bound. Neither predicts corner deflection, torsional stiffness or weighing error.'}
    out=HERE.parent/'exports/v032/section-review.json'
    out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':main()
