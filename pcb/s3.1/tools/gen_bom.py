#!/usr/bin/env python3
"""Generate s3.1 BOM.csv / BOM-jlc.csv from the PCB's placed parts.

Groups parts by (Value, Footprint), preserves the LCSC codes carried over
from the scale-adc-s3 BOM. New parts (D4, R24-R31 series R, C27-C29 ch2 caps,
J10/J11 headers) get the shared LCSC code of their value/footprint group;
R24-R31 100R/0805=C17408; D4 B5819WS=C64886 (1A) — verified 2026-09-18.
THT connectors, test points and mounting holes are excluded (hand-solder).
"""
import os, re

DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PCB = os.path.join(DIR, "s3.1.kicad_pcb")
txt = open(PCB).read()

# ref -> (value, footprint, dnp)
parts = []
for m in re.finditer(r'\(footprint "s3-1:([^"]+)"', txt):
    d, i = 0, m.start()
    while i < len(txt):
        if txt[i] == '(': d += 1
        elif txt[i] == ')':
            d -= 1
            if d == 0: break
        i += 1
    fb = txt[m.start():i+1]
    ref = re.search(r'property "Reference" "([^"]+)"', fb).group(1)
    val = re.search(r'property "Value" "([^"]+)"', fb).group(1)
    parts.append((ref, val, m.group(1), 'dnp' in fb.lower()))

# per-group annotation: (description, lcsc, note, dnp)
INFO = {
 'NAU7802SGI': ('24-bit bridge-sensor ADC (Nuvoton)', 'C5180029', 'extended', ''),
 'LIS2DW12': ('3-axis accelerometer (ST) level/vibration', 'C189624', 'extended', ''),
 'TMP102': ('I2C temperature sensor (TI)', 'C99269', 'extended', ''),
 'ESP32-S3-WROOM-1-N16R8': ('Wi-Fi/BLE5 module, 16MB flash + 8MB octal PSRAM', 'C2913202', 'extended', ''),
 'TP4057-42': ('Li-ion charger ~370mA (R10=2k7)', 'C12044', '', ''),
 'XC6220B331MR-G': ('Main 3.3V LDO 1A high-PSRR', 'C86534', 'extended', ''),
 'HT7533-1': ('ADC rail LDO VDD_ADC 3.3V 100mA', 'C14289', '', ''),
 'USBLC6-2SC6': ('USB ESD protection diode array', 'C7519', '', ''),
 'AO3401A': ('P-FET load switch D=BAT S=VSYS G=VIN5V', 'C15127', '', ''),
 '2N7002': ('Buzzer low-side switch', 'C8545', '', ''),
 'TYPE-C-31-M-12': ('USB-C 16-pin receptacle USB 2.0', 'C165948', 'extended', ''),
 'MLT-8530': ('Passive magnetic buzzer 8.5mm SMD', 'C94599', 'extended', ''),
}
# diodes: D1/D4 populated, D3 DNP
D_INFO = {
 'D1': ('VIN5V->VSYS Schottky load sharing', 'C64885', '', ''),
 'D2': ('Buzzer flyback diode', 'C81598', '', ''),
 'D3': ('EXP_5V->VIN5V injection (machine build)', 'C64885', '', 'DNP'),
 'D4': ('USB_VBUS->VIN5V Schottky isolation, SOD-323, 1A', 'C64886', '', ''),
}
R_INFO = {
 '1k': ('RFI/series resistor', 'C17513', '', ''),
 '4k7': ('I2C SDA pull-up to VCC_3V3', 'C17673', '', ''),
 '10k': ('pull-up/down', 'C17414', '', ''),
 '5k1': ('USB-C CC1 pulldown', 'C27834', '', ''),
 '2k7': ('TP4057 PROG resistor ~370mA', 'C17530', '', ''),
 '100k': ('Q1 gate pulldown VIN5V-GND', 'C149504', '', ''),
 '22R': ('USB D+/D- series resistor', 'C17561', '', ''),
 '100R': ('EXP signal series resistor (J10)', 'C17408', '', ''),
}
C_INFO = {
 '100n': ('decoupling', 'C49678', '', ''),
 '1u': ('bulk decoupling', 'C28323', '', ''),
 '1n': ('common-mode filter cap to GND', 'C46653', '', ''),
 '4u7': ('LIS2DW12 VDD_IO decoupling', 'C1779', '', ''),
 '10u': ('rail bulk cap', 'C15850', '', ''),
 'DNP': ('USB D+/D- cap to GND (optional)', 'C49678', '', 'DNP'),
}

groups = {}
skip_prefix = ('TP', 'MH')
skip_fp = ('PinHeader', 'JST_PH', 'MountingHole')
for ref, val, fp, dnp in parts:
    if ref.startswith(skip_prefix) or any(fp.startswith(s) for s in skip_fp):
        continue
    if val in ('B5819W', 'B5819WS', '1N4148W'):
        info = D_INFO[ref]
    elif ref[0] == 'R':
        info = R_INFO[val]
    elif ref[0] == 'C':
        info = C_INFO[val]
    else:
        info = INFO[val]
    desc, lcsc, note, defdnp = info
    isdnp = 'DNP' if (dnp or defdnp) else ''
    key = (val, fp, lcsc, isdnp, desc)
    groups.setdefault(key, {'refs': [], 'desc': desc, 'note': note, 'dnp': isdnp})
    groups[key]['refs'].append(ref)

def kref(r):
    m = re.match(r'([A-Z]+)(\d+)', r)
    return (m.group(1), int(m.group(2))) if m else (r, 0)

rows = []
for (val, fp, lcsc, _, _), g in ((k, v) for k, v in groups.items()):
    refs = sorted(g['refs'], key=kref)
    first = refs[0]
    order = {'U':0,'Q':1,'D':2,'J':3,'BZ':4,'R':5,'C':6}
    okey = (order.get(re.match(r'[A-Z]+', first).group(0), 9), kref(first)[1])
    rows.append((okey, ','.join(refs), len(refs), val, fp, g['desc'], g['dnp'], lcsc, g['note']))
rows.sort()

hdr = 'Reference,Qty,Value,Footprint,Description,DNP,LCSC,Note'
for name, quote in (('BOM.csv', False), ('BOM-jlc.csv', True)):
    with open(os.path.join(DIR, name), 'w') as f:
        f.write(hdr + '\n')
        for _, refs, qty, val, fp, desc, dnp, lcsc, note in rows:
            cells = [refs, str(qty), val, fp, desc, dnp, lcsc, note]
            if quote:
                cells = [f'"{c}"' if c else '' for c in cells]
            f.write(','.join(cells) + '\n')
    print('wrote', name, len(rows), 'lines')
