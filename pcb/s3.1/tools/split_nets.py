#!/usr/bin/env python3
"""s3.1 power split surgery:
  AVDD -> VDD_ADC (merge, +F bridge)
  VBUS -> VIN5V (internal) / USB_VBUS (J5 island)
  delete bridge segs, move C15 to island east arm, add D4 + GND via + stubs
"""
import re, uuid

FN = "s3.1.kicad_pcb"
t = open(FN).read()
orig = t

def U():
    return str(uuid.uuid4())

def blocks(text, head):
    out = []
    for m in re.finditer(re.escape(head), text):
        d, i = 0, m.start()
        while i < len(text):
            if text[i] == '(': d += 1
            elif text[i] == ')':
                d -= 1
                if d == 0:
                    out.append((m.start(), i + 1))
                    break
            i += 1
    return out

# ---------- 1. AVDD -> VDD_ADC ----------
t = t.replace('(net "AVDD")', '(net "VDD_ADC")')

# ---------- 2/3. VBUS split ----------
ISLAND_PADS = {("J5", "A4"), ("J5", "A9"), ("J5", "B4"), ("J5", "B9"),
               ("U8", "5")}
ISLAND_SEGS = {
    ((79.0364, 40.8242), (79.8133, 40.8242)),
    ((79.0364, 40.8242), (79.0364, 42.0049)),
    ((79.8133, 40.8242), (80.1375, 40.5)),
    ((79.0364, 42.0049), (76.45, 44.5913)),
    ((76.45, 44.5913), (76.45, 45.955)),
    ((76.45, 45.955), (76.45, 46.5566)),
    ((75.4982, 47.5084), (76.45, 46.5566)),
    ((72.5018, 47.5084), (75.4982, 47.5084)),
    ((72.5018, 47.5084), (71.55, 46.5566)),
    ((71.55, 46.5566), (71.55, 45.955)),
    ((71.55, 44.1504), (71.55, 45.955)),
    ((71.3758, 43.9762), (71.55, 44.1504)),
    ((70.1691, 43.9762), (71.3758, 43.9762)),
    ((70.1691, 43.6191), (70.1691, 43.9762)),
    ((69.55, 43), (70.1691, 43.6191)),
}
DEL_SEGS = {
    ((68.55, 42), (69.55, 43)),          # VBUS bridge -> cut
    ((67.65, 42), (68.55, 42)),          # dead stub off D1.2
    ((71.45, 42.9), (73, 41.35)),        # GND stub to old C15.2
}

def pt(s):
    return round(float(s), 4)

# footprint map: ref -> block span
fps = []
for m in re.finditer(r'\(footprint "', t):
    d, i = 0, m.start()
    while i < len(t):
        if t[i] == '(': d += 1
        elif t[i] == ')':
            d -= 1
            if d == 0:
                fps.append((m.start(), i + 1)); break
        i += 1

edits = []  # (start, end, replacement)

for fs, fe in fps:
    blk = t[fs:fe]
    rm = re.search(r'property "Reference" "([^"]+)"', blk)
    ref = rm.group(1) if rm else '?'
    # pads
    for pm in re.finditer(r'\(pad "([^"]+)"', blk):
        pname = pm.group(1)
        ps, pe = fs + pm.start(), None
        d, i = 0, ps
        while i < len(t):
            if t[i] == '(': d += 1
            elif t[i] == ')':
                d -= 1
                if d == 0:
                    pe = i + 1; break
            i += 1
        pblk = t[ps:pe]
        if '(net "VBUS")' in pblk and (ref, pname) in ISLAND_PADS:
            edits.append((ps, pe, pblk.replace('(net "VBUS")', '(net "USB_VBUS")')))
    # move C15 (single edit: at + pad1 net inside same block)
    if ref == "C15":
        nb = blk.replace('(at 70.5 43', '(at 79.3 43.5', 1)
        nb = nb.replace('(net "VBUS")', '(net "USB_VBUS")')
        edits.append((fs, fe, nb))

# segments + vias on VBUS
for m in re.finditer(r'\(segment', t):
    d, i = 0, m.start()
    while i < len(t):
        if t[i] == '(': d += 1
        elif t[i] == ')':
            d -= 1
            if d == 0:
                break
            if d == 0: break
        i += 1
    blk = t[m.start():i + 1]
    sm = re.search(r'\(start ([\d.-]+) ([\d.-]+)\)\s*\(end ([\d.-]+) ([\d.-]+)\)', blk)
    a, b = (pt(sm.group(1)), pt(sm.group(2))), (pt(sm.group(3)), pt(sm.group(4)))
    if (a, b) in DEL_SEGS or (b, a) in DEL_SEGS:
        edits.append((m.start(), i + 1, ""))
    elif '(net "VBUS")' in blk and ((a, b) in ISLAND_SEGS or (b, a) in ISLAND_SEGS):
        edits.append((m.start(), i + 1, blk.replace('(net "VBUS")', '(net "USB_VBUS")')))

# apply edits (desc order); assert every splice keeps paren balance
for s, e, r in sorted(edits, key=lambda x: -x[0]):
    removed = t[s:e]
    if removed.count('(') != removed.count(')'):
        print("UNBALANCED REMOVED:", repr(removed[:120])); raise SystemExit(1)
    if r.count('(') != r.count(')'):
        print("UNBALANCED INSERT:", repr(r[:120])); raise SystemExit(1)
    # overlapping edits check
    t = t[:s] + r + t[e:]

# remaining VBUS -> VIN5V
n_left = t.count('(net "VBUS")')
t = t.replace('(net "VBUS")', '(net "VIN5V")')
print(f"VBUS->VIN5V remaining: {n_left}; island segs+pads done: {len(edits)} edits")

# silk text
t = t.replace('(gr_text "VBUS"', '(gr_text "VIN5V"')

# ---------- append new items ----------
new = []
new.append(f'''(segment
		(start 17.5 14.1)
		(end 17.5 14.9)
		(width 0.5)
		(layer "F.Cu")
		(net "VDD_ADC")
		(uuid "{U()}")
	)''')
new.append(f'''(segment
		(start 79.04 42.0)
		(end 78.6 42.9)
		(width 0.25)
		(layer "F.Cu")
		(net "USB_VBUS")
		(uuid "{U()}")
	)''')
new.append(f'''(via
		(at 80.45 43.9)
		(size 0.7)
		(drill 0.35)
		(layers "F.Cu" "B.Cu")
		(net "GND")
		(uuid "{U()}")
	)''')

# D4 footprint (SOD-123, F.Cu) at 68.1,43.4: pad1 cathode west=VIN5V, pad2 anode east=USB_VBUS
new.append(f'''(footprint "s3-1:D_SOD-123"
		(layer "F.Cu")
		(uuid "{U()}")
		(at 68.1 43.4)
		(descr "SOD-123")
		(tags "SOD-123")
		(property "Reference" "D4"
			(at 0 0 0)
			(layer "F.SilkS")
			(hide yes)
			(uuid "{U()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(property "Value" "B5819W"
			(at 0 2.1 0)
			(layer "F.Fab")
			(uuid "{U()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(property "Datasheet" ""
			(at 0 0 0)
			(unlocked yes)
			(layer "F.Fab")
			(hide yes)
			(uuid "{U()}")
		)
		(property "Description" "USB_VBUS->VIN5V source isolation"
			(at 0 0 0)
			(unlocked yes)
			(layer "F.Fab")
			(hide yes)
			(uuid "{U()}")
		)
		(attr smd)
		(duplicate_pad_numbers_are_jumpers no)
		(fp_line
			(start -2.36 -1)
			(end -2.36 1)
			(stroke
				(width 0.12)
				(type solid)
			)
			(layer "F.SilkS")
			(uuid "{U()}")
		)
		(fp_line
			(start -2.36 -1)
			(end 1.65 -1)
			(stroke
				(width 0.12)
				(type solid)
			)
			(layer "F.SilkS")
			(uuid "{U()}")
		)
		(fp_line
			(start -2.36 1)
			(end 1.65 1)
			(stroke
				(width 0.12)
				(type solid)
			)
			(layer "F.SilkS")
			(uuid "{U()}")
		)
		(fp_line
			(start -2.35 -1.15)
			(end -2.35 1.15)
			(stroke
				(width 0.05)
				(type solid)
			)
			(layer "F.CrtYd")
			(uuid "{U()}")
		)
		(fp_line
			(start -2.35 -1.15)
			(end 2.35 -1.15)
			(stroke
				(width 0.05)
				(type solid)
			)
			(layer "F.CrtYd")
			(uuid "{U()}")
		)
		(fp_line
			(start 2.35 -1.15)
			(end 2.35 1.15)
			(stroke
				(width 0.05)
				(type solid)
			)
			(layer "F.CrtYd")
			(uuid "{U()}")
		)
		(fp_line
			(start 2.35 1.15)
			(end -2.35 1.15)
			(stroke
				(width 0.05)
				(type solid)
			)
			(layer "F.CrtYd")
			(uuid "{U()}")
		)
		(fp_line
			(start -1.4 -0.9)
			(end 1.4 -0.9)
			(stroke
				(width 0.1)
				(type solid)
			)
			(layer "F.Fab")
			(uuid "{U()}")
		)
		(fp_line
			(start -1.4 0.9)
			(end -1.4 -0.9)
			(stroke
				(width 0.1)
				(type solid)
			)
			(layer "F.Fab")
			(uuid "{U()}")
		)
		(fp_line
			(start -0.75 0)
			(end -0.35 0)
			(stroke
				(width 0.1)
				(type solid)
			)
			(layer "F.Fab")
			(uuid "{U()}")
		)
		(fp_line
			(start -0.35 0)
			(end -0.35 -0.55)
			(stroke
				(width 0.1)
				(type solid)
			)
			(layer "F.Fab")
			(uuid "{U()}")
		)
		(fp_line
			(start -0.35 0)
			(end -0.35 0.55)
			(stroke
				(width 0.1)
				(type solid)
			)
			(layer "F.Fab")
			(uuid "{U()}")
		)
		(fp_line
			(start -0.35 0)
			(end 0.25 -0.4)
			(stroke
				(width 0.1)
				(type solid)
			)
			(layer "F.Fab")
			(uuid "{U()}")
		)
		(fp_line
			(start 0.25 -0.4)
			(end 0.25 0.4)
			(stroke
				(width 0.1)
				(type solid)
			)
			(layer "F.Fab")
			(uuid "{U()}")
		)
		(fp_line
			(start 0.25 0)
			(end 0.75 0)
			(stroke
				(width 0.1)
				(type solid)
			)
			(layer "F.Fab")
			(uuid "{U()}")
		)
		(fp_line
			(start 0.25 0.4)
			(end -0.35 0)
			(stroke
				(width 0.1)
				(type solid)
			)
			(layer "F.Fab")
			(uuid "{U()}")
		)
		(fp_line
			(start 1.4 -0.9)
			(end 1.4 0.9)
			(stroke
				(width 0.1)
				(type solid)
			)
			(layer "F.Fab")
			(uuid "{U()}")
		)
		(fp_line
			(start 1.4 0.9)
			(end -1.4 0.9)
			(stroke
				(width 0.1)
				(type solid)
			)
			(layer "F.Fab")
			(uuid "{U()}")
		)
		(fp_text user "${{REFERENCE}}"
			(at 0 -2 0)
			(layer "F.Fab")
			(uuid "{U()}")
			(effects
				(font
					(size 1 1)
					(thickness 0.15)
				)
			)
		)
		(pad "1" smd roundrect
			(at -1.65 0)
			(size 0.9 1.2)
			(layers "F.Cu" "F.Mask" "F.Paste")
			(roundrect_rratio 0.25)
			(net "VIN5V")
			(uuid "{U()}")
		)
		(pad "2" smd roundrect
			(at 1.65 0)
			(size 0.9 1.2)
			(layers "F.Cu" "F.Mask" "F.Paste")
			(roundrect_rratio 0.25)
			(net "USB_VBUS")
			(uuid "{U()}")
		)
		(embedded_fonts no)
	)''')

# insert before final closing paren of the kicad_pcb root
t = t.rstrip()
assert t.endswith(')')
t = t[:-1] + '\n\t' + '\n\t'.join(new) + '\n)\n'
open(FN, 'w').write(t)
print("surgery complete")
