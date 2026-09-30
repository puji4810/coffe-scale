"""Read one KiCad file atomically into memory; never write to the PCB tree.

Dimensions come from Edge.Cuts centre lines, not the stroked bounding box.
Footprint rectangles are conservative 2D envelopes, NOT component STEP models.
"""
from datetime import datetime, timezone
from hashlib import sha256
from pathlib import Path
import json
import math
import sexpdata

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT.parent / 'pcb/scale-adc-s3/scale-adc-s3.kicad_pcb'


def children(node, key):
    return [x for x in node if isinstance(x, list) and x and str(x[0]) == key]


def one(node, key):
    return children(node, key)[0]


def capture():
    raw = SOURCE.read_bytes()
    tree = sexpdata.loads(raw.decode())
    outlines = [g for g in children(tree, 'gr_rect')
                if one(g, 'layer')[1] == 'Edge.Cuts']
    if len(outlines) != 1:
        raise ValueError('PCB outline changed: adapt snapshot reader for non-rectangular board')
    outline = outlines[0]
    x0, y0 = one(outline, 'start')[1:3]
    x1, y1 = one(outline, 'end')[1:3]
    items = []
    # Z envelopes need physical/STEP confirmation, especially mated connectors.
    heights = {'U4': 3.1, 'U1': 2.0, 'J2': 8.5, 'J5': 3.2,
               'J6': 12, 'J7': 12, 'J8': 12, 'BZ1': 4}
    refs = set(heights)
    mounts = []
    for f in children(tree, 'footprint'):
        props = {p[1]: p[2] for p in children(f, 'property')}
        ref = props.get('Reference', '')
        at = one(f, 'at')[1:]
        if 'MountingHole' in str(f[1]):
            mounts.append({'reference': ref, 'at': at})
        if ref not in refs:
            continue
        # Use fabrication geometry, fall back to courtyard. Exclude text.
        points = []
        for layer in ('F.Fab', 'F.CrtYd'):
            for kind in ('fp_line', 'fp_rect'):
                for g in children(f, kind):
                    if one(g, 'layer')[1] == layer:
                        points += [one(g, 'start')[1:3], one(g, 'end')[1:3]]
            if points:
                break
        if not points:
            points = [[-4, -4], [4, 4]]
        angle = math.radians(at[2] if len(at) > 2 else 0)
        transformed = [(at[0] + x * math.cos(angle) + y * math.sin(angle),
                        at[1] - x * math.sin(angle) + y * math.cos(angle))
                       for x, y in points]
        pads = []
        for p in children(f, 'pad'):
            nets = children(p, 'net')
            if nets:
                pads.append({'pin': str(p[1]), 'net': str(nets[0][-1])})
        items.append({'reference': ref, 'value': props.get('Value'), 'at': at,
                      'bbox': [min(x for x, y in transformed), min(y for x, y in transformed),
                               max(x for x, y in transformed), max(y for x, y in transformed)],
                      'height_envelope': heights[ref], 'pads': pads})
    snapshot = {'source': str(SOURCE.relative_to(ROOT.parent)),
                'sha256': sha256(raw).hexdigest(),
                'captured_utc': datetime.now(timezone.utc).isoformat(),
                'outline': [x0, y0, x1, y1], 'width': x1-x0, 'depth': y1-y0,
                'thickness': one(one(tree, 'general'), 'thickness')[1],
                'mounting_holes': mounts, 'components': items}
    dest = ROOT / 'references/pcb-snapshot.json'
    dest.parent.mkdir(exist_ok=True)
    dest.write_text(json.dumps(snapshot, indent=2) + '\n')
    return snapshot


if __name__ == '__main__':
    print(json.dumps(capture(), indent=2))
