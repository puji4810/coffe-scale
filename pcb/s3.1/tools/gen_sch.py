#!/usr/bin/env python3
"""Generate s3.1.kicad_sch on the 1.27mm grid. Rev B: standalone main board."""
G = 1.27
def mm(n):  # grid units -> mm string
    v = round(n * G, 2)
    return f"{v:g}"

uid_n = 0
def U():
    global uid_n
    uid_n += 1
    return f"b2a10000-0000-4000-8000-{uid_n:012x}"

out = []
def w(s): out.append(s)

w('(kicad_sch')
w('\t(version 20260306)')
w('\t(generator "eeschema")')
w('\t(generator_version "10.0")')
w('\t(uuid "b2a10000-0000-4000-8000-000000000001")')
w('\t(paper "A3")')
w('\t(title_block')
w('\t\t(title "CoffeeScale Control Board s3.1 (ESP32-S3)")')
w('\t\t(date "2026-09-17")')
w('\t\t(rev "D")')
w('\t\t(company "Coffee Scale")')
w('\t\t(comment 1 "Rev D — dual-use control board: coffee scale OR auto-brewer brain")')
w('\t\t(comment 2 "USB-C charge + Li-ion on J6, D1/Q1 load-sharing, XC6220 3V3(1A) + HT7533 VDD_ADC")')
w('\t\t(comment 3 "NAU7802 ch1=load cell J2, ch2=J11 (PT1000/2nd sensor); J10 2x10 expansion; UART0 TPs, BOOT=IO0")')
w('\t)')

# ---------------- lib_symbols (embedded copies) ----------------
w('\t(lib_symbols')
symfile = open('/home/puji/coffee-scale/pcb/s3.1/s3.1.kicad_sym').read()
i = symfile.find('(symbol "')
while i >= 0 and i < len(symfile):
    d = 0; k = i
    while True:
        if symfile[k] == '(': d += 1
        elif symfile[k] == ')':
            d -= 1
            if d == 0: break
        k += 1
    blk = symfile[i:k+1]
    blk = blk.replace('(symbol "', '(symbol "s3-1:', 1)
    for ln in blk.split('\n'):
        w('\t\t' + ln)
    i = symfile.find('(symbol "', k+1)
w('\t)')

def text(s, x, y):
    w(f'\t(text "{s}"')
    w('\t\t(exclude_from_sim no)')
    w(f'\t\t(at {mm(x)} {mm(y)} 0)')
    w('\t\t(effects')
    w('\t\t\t(font')
    w('\t\t\t\t(size 1.27 1.27)')
    w('\t\t\t)')
    w('\t\t)')
    w(f'\t\t(uuid "{U()}")')
    w('\t)')

def wire(x1, y1, x2, y2):
    w('\t(wire')
    w('\t\t(pts')
    w(f'\t\t\t(xy {mm(x1)} {mm(y1)}) (xy {mm(x2)} {mm(y2)})')
    w('\t\t)')
    w('\t\t(stroke')
    w('\t\t\t(width 0)')
    w('\t\t\t(type solid)')
    w('\t\t)')
    w(f'\t\t(uuid "{U()}")')
    w('\t)')

def noconn(x, y):
    w('\t(no_connect')
    w(f'\t\t(at {mm(x)} {mm(y)})')
    w(f'\t\t(uuid "{U()}")')
    w('\t)')

def label(name, x, y, rot=0):
    w(f'\t(global_label "{name}"')
    w('\t\t(shape passive)')
    w(f'\t\t(at {mm(x)} {mm(y)} {rot})')
    w('\t\t(effects')
    w('\t\t\t(font')
    w('\t\t\t\t(size 1.27 1.27)')
    w('\t\t\t)')
    w('\t\t)')
    w(f'\t\t(uuid "{U()}")')
    w('\t\t(property "Intersheetrefs" "${INTERSHEET_REFS}"')
    w(f'\t\t\t(at {mm(x)} {mm(y)} {rot})')
    w('\t\t\t(hide yes)')
    w('\t\t\t(show_name no)')
    w('\t\t\t(do_not_autoplace no)')
    w('\t\t\t(effects')
    w('\t\t\t\t(font')
    w('\t\t\t\t\t(size 1.27 1.27)')
    w('\t\t\t\t)')
    w('\t\t\t)')
    w('\t\t)')
    w('\t)')

SYMS = []

def sym(lib, ref, val, fp, desc, x, y, rot=0, npins=1, pins=None, dnp=False,
        bom=True, board=True, datasheet='', refdx=0, refdy=-8, valdx=0, valdy=8):
    w('\t(symbol')
    w(f'\t\t(lib_id "s3-1:{lib}")')
    w(f'\t\t(at {mm(x)} {mm(y)} {rot})')
    w('\t\t(unit 1)')
    w('\t\t(body_style 1)')
    w('\t\t(exclude_from_sim no)')
    w(f'\t\t(in_bom {"yes" if bom else "no"})')
    w(f'\t\t(on_board {"yes" if board else "no"})')
    w(f'\t\t(in_pos_files {"yes" if board else "no"})')
    w(f'\t\t(dnp {"yes" if dnp else "no"})')
    _su = U()
    w(f'\t\t(uuid "{_su}")')
    if board:
        SYMS.append((_su, ref, val, fp))
    for prop, pval, dx, dy, hide in (
            ("Reference", ref, refdx, refdy, False),
            ("Value", val, valdx, valdy, False),
            ("Footprint", fp, 0, 0, True),
            ("Datasheet", datasheet, 0, 0, True),
            ("Description", desc, 0, 0, True)):
        w(f'\t\t(property "{prop}" "{pval}"')
        w(f'\t\t\t(at {mm(x+dx)} {mm(y+dy)} 0)')
        if hide:
            w('\t\t\t(hide yes)')
        w('\t\t\t(show_name no)')
        w('\t\t\t(do_not_autoplace no)')
        w('\t\t\t(effects')
        w('\t\t\t\t(font')
        w('\t\t\t\t\t(size 1.27 1.27)')
        w('\t\t\t\t)')
        w('\t\t\t)')
        w('\t\t)')
    for p in (pins if pins is not None else [str(i) for i in range(1, npins + 1)]):
        w(f'\t\t(pin "{p}"')
        w(f'\t\t\t(uuid "{U()}")')
        w('\t\t)')
    w('\t\t(instances')
    w('\t\t\t(project "s3.1"')
    w('\t\t\t\t(path "/b2a10000-0000-4000-8000-000000000001"')
    w(f'\t\t\t\t\t(reference "{ref}")')
    w('\t\t\t\t\t(unit 1)')
    w('\t\t\t\t)')
    w('\t\t\t)')
    w('\t\t)')
    w('\t)')

pwr_n = 0
def pwr3v3(x, y):
    global pwr_n
    pwr_n += 1
    sym('PWR_3V3', f'#PWR{pwr_n:02d}', 'VCC_3V3', '', '', x, y,
        board=False, refdy=-6, valdy=6)

def gnd(x, y):
    global pwr_n
    pwr_n += 1
    sym('PWR_GND', f'#PWR{pwr_n:02d}', 'GND', '', '', x, y,
        board=False, refdy=6, valdy=-6)

def flag(x, y):
    global pwr_n
    pwr_n += 1
    sym('PWR_FLAG', f'#PWR{pwr_n:02d}', 'PWR_FLAG', '', '', x, y,
        board=False, refdy=-6, valdy=6)

# ---------------- section headers ----------------
text('Power: J5 USB_VBUS -|D4|- VIN5V -> TP4057 charger (~370mA, R10=2k7) -> BAT J6; D1/Q1 load-share -> VSYS -> U6 3V3 / U7 VDD_ADC', 34, 18)
text('Analog front end: load cell J2 -> RFI filter -> NAU7802 U1 (VDD_ADC rail)', 44, 80)
text('Sensors: LIS2DW12 U2 (level/vibration, 3V3) + TMP102 U3 (near load cell, VDD_ADC)', 118, 42)
text('HMI: LCD J7 / buttons J8 / buzzer BZ1+Q2', 286, 44)
text('USB: J5 -> U8 ESD -> R19/R20 22R -> U4 IO20(D+)/IO19(D-); C23/C24 DNP pads', 34, 20)

# ---------------- text notes ----------------
text('Load cell: E+=VDD_ADC(3V3 rail), E-=GND, S+/S- via 1k RFI series R. LC1330 Rin=410+/-10R: 2x bridge ~16.5mA worst', 48, 130)
text('Ratiometric: REFP=VDD_ADC=AVDD pin (external AVDD supply). REFN=AVSS. XIN/XOUT open: internal RC osc.', 48, 132)
text('FW must set NAU7802 AVDDS=external (internal LDO off). Vendor recommends 5-12V excitation - 3.3V works but verify accuracy', 48, 134)
text('Rev C: TP4057(~370mA)+D1/Q1 load-share+XC6220(1A)/HT7533 LDOs. ESP32-S3-WROOM-1-N16R8: 16MB flash + 8MB octal PSRAM (IO35-37 in-use). JLCPCB basic parts where possible', 48, 136)
text('Machine build: EXP_5V (J10.11) -|D3 DNP|- VIN5V charges BAT + powers system. D4 blocks EXP_5V from USB receptacle VBUS', 48, 138)

# =================================================================
# POWER CHAIN (top-left)
# =================================================================
# ---------------- J5: USB-C receptacle ----------------
# USB_C_16P pins: left x-15.24 -> grid -12, right +15.24 -> +12
sym('USB_C_16P', 'J5', 'TYPE-C-31-M-12', 's3-1:USB_C_Receptacle_HRO_TYPE-C-31-M-12',
    'USB-C 16-pin receptacle, USB 2.0', 50, 44, npins=17,
    pins=['A1','A4','A5','A6','A7','A8','A9','A12','B1','B4','B5','B6','B7','B8','B9','B12','SH'],
    refdy=-14, valdy=15)
# VBUS pads A4/B4/A9/B9 at y36/38/40/42 -> vertical rail x34, vertex at each stub
for yy in (36, 38, 40, 42):
    wire(38, yy, 34, yy)
wire(34, 36, 34, 38); wire(34, 38, 34, 40); wire(34, 40, 34, 42)
wire(34, 36, 34, 34); wire(34, 34, 34, 32)
wire(34, 34, 30, 34); label('USB_VBUS', 30, 34)
flag(34, 32)
# GND pads A1/B1/A12/B12/SH at y46..54 -> rail x34, vertex at each stub
for yy in (46, 48, 50, 52, 54):
    wire(38, yy, 34, yy)
wire(34, 46, 34, 48); wire(34, 48, 34, 50); wire(34, 50, 34, 52); wire(34, 52, 34, 54)
wire(34, 50, 30, 50); gnd(30, 50)
# CC1 -> R8 5k1 -> GND ; CC2 -> R9 5k1 -> GND
wire(62, 37, 66, 37)
sym('R', 'R8', '5k1', 's3-1:SMD_0805_VR', 'CC1 pulldown 5.1k', 69, 37, rot=90,
    npins=2, refdy=-4, valdy=4)
wire(72, 37, 76, 37); gnd(76, 37)
wire(62, 39, 66, 39)
sym('R', 'R9', '5k1', 's3-1:SMD_0805_VR', 'CC2 pulldown 5.1k', 69, 39, rot=90,
    npins=2, refdy=-4, valdy=4)
wire(72, 39, 76, 39); gnd(76, 39)
# D+ pair -> USB_DP ; D- pair -> USB_DM ; SBU nc
wire(62, 41, 66, 41); wire(66, 41, 66, 42)
wire(62, 43, 66, 43); wire(66, 43, 66, 42)
wire(66, 42, 70, 42); label('USB_DP', 70, 42)
wire(62, 45, 66, 45); wire(66, 45, 66, 46)
wire(62, 47, 66, 47); wire(66, 47, 66, 46)
wire(66, 46, 70, 46); label('USB_DM', 70, 46)
noconn(62, 49)
noconn(62, 51)
# U8 USBLC6-2SC6 ESD on connector side: IO1=D-, IO2=D+, GND, VBUS; IO3/IO4 nc
sym('USBLC6-2SC6', 'U8', 'USBLC6-2SC6', 's3-1:SOT-23-6',
    'USB 2.0 low-cap ESD array', 75, 55, npins=6, refdy=-9, valdy=10,
    datasheet='https://www.st.com/resource/en/datasheet/usblc6-2sc6.pdf')
wire(68, 53, 64, 53); label('USB_DM', 64, 53, rot=180)   # IO1
wire(68, 55, 64, 55); label('USB_DP', 64, 55, rot=180)   # IO2
wire(68, 57, 64, 57); gnd(64, 57)                        # GND
wire(82, 53, 86, 53); label('USB_VBUS', 86, 53)          # VBUS pin
noconn(82, 55); noconn(82, 57)                           # IO3/IO4
# series 22R on D+/D- to module; DNP shunt cap pads on module side
wire(70, 42, 75, 42)
sym('R', 'R19', '22R', 's3-1:SMD_0805_F', 'USB D+ series R 22R', 78, 42, rot=90,
    npins=2, refdy=-4, valdy=4)
wire(81, 42, 90, 42); wire(90, 42, 95, 42); label('USB_DP_M', 95, 42)
sym('C', 'C23', 'DNP', 's3-1:SMD_0805_V', 'USB DP shunt cap pad (DNP)', 90, 45,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0, dnp=True, bom=False)
wire(90, 43, 90, 42)
wire(90, 47, 90, 50); gnd(90, 50)
wire(70, 46, 75, 46)
sym('R', 'R20', '22R', 's3-1:SMD_0805_F', 'USB D- series R 22R', 78, 46, rot=90,
    npins=2, refdy=-4, valdy=4)
wire(81, 46, 92, 46); wire(92, 46, 96, 46); label('USB_DM_M', 96, 46)
sym('C', 'C24', 'DNP', 's3-1:SMD_0805_V', 'USB DM shunt cap pad (DNP)', 92, 49,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0, dnp=True, bom=False)
wire(92, 47, 92, 46)
wire(92, 51, 92, 54); gnd(92, 54)
# C15 10u VBUS-GND
sym('C', 'C15', '10u', 's3-1:SMD_0805', 'USB_VBUS input cap (receptacle side)', 72, 28,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(72, 26, 72, 23); label('USB_VBUS', 72, 23)
wire(72, 30, 72, 33); gnd(72, 33)
# D4 B5819W: USB_VBUS -> VIN5V source isolation (diode-OR with D3 on EXP_5V).
# Blocks EXP_5V back-feed into the USB receptacle pins. Machine build: fit
# diode; scale-only build may fit 0R to keep full 5V on the charger input.
sym('D_Schottky', 'D4', 'B5819WS', 's3-1:D_SOD-323',
    'USB_VBUS->VIN5V source isolation', 84, 30, npins=2, refdy=-5, valdy=5)
wire(80, 30, 76, 30); label('USB_VBUS', 76, 30)
wire(88, 30, 92, 30); label('VIN5V', 92, 30)
wire(92, 30, 100, 30); flag(100, 30)

# ---------------- U5: TP4057 charger ----------------
# left pins x-8.89 -> grid -7 (x88): 4 VCC y38, 6 PROG y40, 2 GND y42
# right x+7 (x102): 1 CHRG y38, 5 STDBY y40, 3 BAT y42
sym('TP4057', 'U5', 'TP4057-42', 's3-1:SOT-23-6',
    'Li-ion charger, ~370mA via R10=2k7', 95, 40, npins=6, refdy=-8, valdy=9,
    datasheet='https://www.lcsc.com/datasheet/C12044.pdf')
wire(88, 38, 82, 38); label('VIN5V', 82, 38)            # VCC
wire(88, 40, 80, 40); wire(80, 40, 80, 42)              # PROG -> R10
sym('R', 'R10', '2k7', 's3-1:SMD_0805', 'PROG resistor, ~370mA charge', 80, 45,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(80, 48, 80, 51); gnd(80, 51)
wire(88, 42, 84, 42); gnd(84, 42)                       # GND
wire(102, 38, 106, 38); wire(106, 38, 110, 38); label('CHRG_STAT', 110, 38, rot=180)
sym('R', 'R11', '10k', 's3-1:SMD_0805', 'CHRG open-drain pull-up', 106, 33,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(106, 30, 106, 27); pwr3v3(106, 27)
wire(106, 36, 106, 38)
noconn(102, 40)                                         # STDBY
wire(102, 42, 108, 42); label('BAT', 108, 42, rot=180)  # BAT out

# ---------------- J6: battery ----------------
# CONN_1x02_BAT pins x-7.62 -> grid -6 (x134): 1 BAT y31, 2 GND y33
sym('CONN_1x02_BAT', 'J6', 'BAT', 's3-1:JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical',
    'Li-ion battery 1S, JST PH', 140, 32, npins=2, refdy=-6, valdy=6)
wire(134, 31, 130, 31); label('BAT', 130, 31)
wire(134, 31, 134, 29); flag(134, 29)
wire(134, 33, 130, 33); gnd(130, 33)
# C16 10u BAT-GND
sym('C', 'C16', '10u', 's3-1:SMD_0805', 'Battery rail cap', 150, 36,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(150, 34, 150, 31); label('BAT', 150, 31)
wire(150, 38, 150, 41); gnd(150, 41)

# ---------------- load sharing: D1 + Q1 + R12 ----------------
# D_Schottky: pin2 A at x-5.08 -> grid -4, pin1 K at +4
sym('D_Schottky', 'D1', 'B5819W', 's3-1:D_SOD-123',
    'VIN5V->VSYS Schottky, load sharing', 110, 60, npins=2, refdy=-5, valdy=5)
wire(106, 60, 102, 60); label('VIN5V', 102, 60)
wire(114, 60, 118, 60); label('VSYS', 118, 60, rot=180)
wire(118, 60, 118, 56); flag(118, 56)
# Q1 AO3401A: 1 G left (x-6), 2 S right-top, 3 D right-bottom
sym('AO3401A', 'Q1', 'AO3401A', 's3-1:SOT-23',
    'P-FET load switch: D=BAT S=VSYS G=VIN5V', 110, 70, npins=3, refdy=-7, valdy=7,
    datasheet='https://www.aosmd.com/res/datasheets/AO3401A.pdf')
wire(104, 70, 100, 70); label('VIN5V', 100, 70)
wire(116, 68, 120, 68); label('VSYS', 120, 68, rot=180)
wire(116, 72, 120, 72); label('BAT', 120, 72, rot=180)
# R12 100k VIN5V-GND (gate pulldown)
sym('R', 'R12', '100k', 's3-1:SMD_0805', 'Q1 gate pulldown', 96, 64,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(96, 61, 96, 59); label('VIN5V', 96, 59)
wire(96, 67, 96, 69); gnd(96, 69)

# ---------------- U6: AP2112K-3.3 main LDO ----------------
# left pins x-8.89 -> grid -7 (x153): 1 VIN y-2, 2 GND y0, 3 EN y+2 ; right x167: 5 VOUT y-2, 4 NC y+2
sym('ME6211', 'U6', 'XC6220B331MR-G', 's3-1:SOT-23-5',
    'Main 3.3V LDO, 1A (S3 Wi-Fi peaks + LCD backlight)', 160, 80, npins=5, refdy=-9, valdy=10,
    datasheet='https://www.lcsc.com/datasheet/C86539.pdf')
wire(153, 78, 149, 78); label('VSYS', 149, 78)
wire(153, 80, 149, 80); gnd(149, 80)
wire(153, 82, 149, 82); label('VSYS', 149, 82)
wire(167, 78, 172, 78); pwr3v3(172, 78)
# C21 10u VSYS input cap
sym('C', 'C21', '10u', 's3-1:SMD_0805', 'VSYS input cap U6', 178, 78,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(178, 76, 178, 73); label('VSYS', 178, 73)
wire(178, 80, 178, 83); gnd(178, 83)
# C9 10u 3V3 output cap
sym('C', 'C9', '10u', 's3-1:SMD_0805', '3V3 LDO output cap', 186, 78,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(186, 76, 186, 73); pwr3v3(186, 73)
wire(186, 80, 186, 83); gnd(186, 83)

# ---------------- U7: AP2112K-3.3 ADC rail ----------------
sym('HT7533', 'U7', 'HT7533-1', 's3-1:SOT-89-3',
    'ADC/analog rail LDO VDD_ADC 3.3V 100mA', 160, 94, npins=3, refdy=-7, valdy=8,
    datasheet='https://www.holtek.com/documents/10179/116711/HT75xx-1v250.pdf')
wire(154, 93, 150, 93); label('VSYS', 150, 93)
wire(154, 95, 150, 95); gnd(150, 95)
wire(166, 94, 170, 94); label('VDD_ADC', 170, 94, rot=180)
# C22 10u VSYS input cap
sym('C', 'C22', '10u', 's3-1:SMD_0805', 'VSYS input cap U7', 178, 94,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(178, 92, 178, 89); label('VSYS', 178, 89)
wire(178, 96, 178, 99); gnd(178, 99)
# C20 10u VDD_ADC output cap
sym('C', 'C20', '10u', 's3-1:SMD_0805', 'VDD_ADC LDO output cap', 186, 94,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(186, 92, 186, 89); label('VDD_ADC', 186, 89)
wire(186, 96, 186, 99); gnd(186, 99)

# ---------------- battery voltage sense ----------------
sym('R', 'R13', '100k', 's3-1:SMD_0805', 'VBAT divider top', 147, 112, rot=90,
    npins=2, refdy=-4, valdy=4)
wire(144, 112, 140, 112); label('BAT', 140, 112)
wire(150, 112, 154, 112)
sym('R', 'R14', '100k', 's3-1:SMD_0805', 'VBAT divider bottom', 154, 116,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(154, 113, 154, 112)
wire(154, 112, 160, 112)
sym('C', 'C17', '100n', 's3-1:SMD_0805', 'VBAT sense filter', 160, 116,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(160, 114, 160, 112)
wire(160, 112, 164, 112); label('VBAT_SENSE', 164, 112, rot=180)
wire(154, 119, 154, 122); gnd(154, 122)
wire(160, 118, 160, 122); gnd(160, 122)

# =================================================================
# ANALOG FRONT END
# =================================================================
# ---------------- J2: load cell ----------------
# CONN_1x05 pins x-7.62, y=-5.08..+5.08 step 2.54 (-4,-2,0,2,4)
sym('CONN_1x05_LC', 'J2', 'LOADCELL', 's3-1:PinHeader_1x05_P2.54mm_H',
    'L6D load cell E+/E-/S-/S+/SHLD', 44, 103, npins=5, refdy=-9, valdy=10)
wire(38, 99, 33, 99); label('VDD_ADC', 33, 99)
wire(38, 101, 33, 101); gnd(33, 101)
wire(38, 103, 33, 103); label('LC_SIGN', 33, 103)
wire(38, 105, 33, 105); label('LC_SIGP', 33, 105)
wire(38, 107, 33, 107); gnd(33, 107)

# ---------------- RFI series resistors ----------------
sym('R', 'R1', '1k', 's3-1:SMD_0805_VR', 'RFI series R, S+', 59, 82, rot=90, npins=2, refdy=-4, valdy=4)
wire(56, 82, 51, 82); label('LC_SIGP', 51, 82)
wire(62, 82, 66, 82); label('SIG_P', 66, 82)
sym('R', 'R2', '1k', 's3-1:SMD_0805_VR', 'RFI series R, S-', 59, 86, rot=90, npins=2, refdy=-4, valdy=4)
wire(56, 86, 51, 86); label('LC_SIGN', 51, 86)
wire(62, 86, 66, 86); label('SIG_N', 66, 86)

# ---------------- filter caps around ADC inputs ----------------
sym('C', 'C5', '100n', 's3-1:SMD_0805_VR', 'Differential cap across VIN1', 75, 84, npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(75, 82, 75, 81); label('SIG_P', 75, 81)
wire(75, 86, 75, 87); label('SIG_N', 75, 87)
sym('C', 'C10', '1n', 's3-1:SMD_0805_F', 'CM filter, SIG_P to GND', 79, 90, npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(79, 88, 79, 87); label('SIG_P', 79, 87)
wire(79, 92, 79, 93); gnd(79, 93)
sym('C', 'C11', '1n', 's3-1:SMD_0805_F', 'CM filter, SIG_N to GND', 83, 90, npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(83, 88, 83, 87); label('SIG_N', 83, 87)
wire(83, 92, 83, 93); gnd(83, 93)

# ---------------- J11: ADC channel 2 (VIN2) ----------------
# CONN_1x04_VIN2 pins x-7.62 -> grid -6 (x38): 1 E+ y+3, 2 E- y+1, 3 S2- y-1, 4 S2+ y-3
# Use cases: PT1000 water temp (remote Rref divider, ratiometric vs VDD_ADC) or
# a second bridge. E+ on VDD_ADC rail (HT7533 100mA): two LC1330 (410R) draw
# ~16.5mA worst case - internal-LDO 10mA limit no longer applies.
sym('CONN_1x04_VIN2', 'J11', 'CH2', 's3-1:PinHeader_1x04_P2.54mm',
    'ADC ch2: 1=E+(VDD_ADC) 2=E-(GND) 3=S2- 4=S2+', 44, 122, npins=4, refdy=-7, valdy=8)
# NOTE: netlister maps pin1 to the TOP pin (symbol pin y is mirrored at
# instantiation); pin order in the netlist: 1=E+ at y119 .. 4=S2+ at y125.
wire(38, 119, 33, 119); label('VDD_ADC', 33, 119)      # pin1 E+
wire(38, 121, 33, 121); gnd(33, 121)                   # pin2 E-
wire(38, 123, 33, 123); label('LC_SIG2N', 33, 123)    # pin3 S2-
wire(38, 125, 33, 125); label('LC_SIG2P', 33, 125)    # pin4 S2+

# ch2 RFI filter mirrors ch1: R22/R23 1k series + C27 diff + C28/C29 CM
sym('R', 'R23', '1k', 's3-1:SMD_0805', 'RFI series R, S2+', 59, 116, rot=90, npins=2, refdy=-4, valdy=4)
wire(56, 116, 51, 116); label('LC_SIG2P', 51, 116)
wire(62, 116, 66, 116); label('SIG2_P', 66, 116)
sym('R', 'R22', '1k', 's3-1:SMD_0805', 'RFI series R, S2-', 59, 120, rot=90, npins=2, refdy=-4, valdy=4)
wire(56, 120, 51, 120); label('LC_SIG2N', 51, 120)
wire(62, 120, 66, 120); label('SIG2_N', 66, 120)
sym('C', 'C27', '100n', 's3-1:SMD_0805_VR', 'Differential cap across VIN2', 75, 118, npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(75, 116, 75, 115); label('SIG2_P', 75, 115)
wire(75, 120, 75, 121); label('SIG2_N', 75, 121)
sym('C', 'C28', '1n', 's3-1:SMD_0805', 'CM filter, SIG2_P to GND', 79, 124, npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(79, 122, 79, 121); label('SIG2_P', 79, 121)
wire(79, 126, 79, 127); gnd(79, 127)
sym('C', 'C29', '1n', 's3-1:SMD_0805', 'CM filter, SIG2_N to GND', 83, 124, npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(83, 122, 83, 121); label('SIG2_N', 83, 121)
wire(83, 126, 83, 127); gnd(83, 127)

# ---------------- U1 NAU7802 ----------------
sym('NAU7802SGI', 'U1', 'NAU7802SGI', 's3-1:SOIC-16_3.9x9.9mm_P1.27mm',
    '24-bit bridge ADC, I2C 0x2A', 86, 95, npins=16, refdy=-17, valdy=19,
    datasheet='https://www.nuvoton.com/export/resource-files/en-us--DS_NAU7802_DataSheet_EN_Rev2.6.pdf')
u1l = 74
wire(u1l, 82, 70, 82); label('VDD_ADC', 70, 82)         # REFP
wire(u1l, 84, 70, 84); label('SIG_N', 70, 84)          # VIN1N
wire(u1l, 86, 70, 86); label('SIG_P', 70, 86)          # VIN1P
wire(u1l, 88, 70, 88); label('SIG2_N', 70, 88)         # VIN2N -> ch2 filter -> J11
wire(u1l, 90, 70, 90); label('SIG2_P', 70, 90)         # VIN2P
wire(u1l, 92, 70, 92); label('VBG', 70, 92)            # VBG
wire(u1l, 94, 70, 94); gnd(70, 94)                     # REFN
wire(u1l, 96, 70, 96); gnd(70, 96)                     # AVSS
u1r = 98
wire(u1r, 94, 103, 94); label('VDD_ADC', 103, 94, rot=180)
wire(u1r, 96, 103, 96); label('VDD_ADC', 103, 96, rot=180)   # DVDD on ADC rail
wire(u1r, 98, 103, 98); label('SDA', 103, 98, rot=180)
wire(u1r, 100, 103, 100); label('SCL', 103, 100, rot=180)
wire(u1r, 102, 103, 102); label('DRDY', 103, 102, rot=180)
noconn(u1r, 104)   # XOUT
noconn(u1r, 106)   # XIN
wire(u1r, 108, 103, 108); gnd(103, 108)

# TP7 on VBG, TP8 on AVDD (bring-up test pads, replace J3)
sym('TP', 'TP7', 'TP_VBG', 's3-1:TestPoint_1.2mm', 'VBG test pad',
    64, 92, npins=1, bom=False, refdy=-6, valdy=6)
wire(70, 92, 62, 92)
sym('TP', 'TP8', 'TP_AVDD', 's3-1:TestPoint_1.2mm', 'AVDD pin test pad (on VDD_ADC rail)',
    105, 94, npins=1, bom=False, refdy=-6, valdy=6)

# VBG bypass
sym('C', 'C4', '100n', 's3-1:SMD_0805_VR', 'VBG bypass', 75, 97, npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(75, 95, 75, 94); label('VBG', 75, 94)
wire(75, 99, 75, 100); gnd(75, 100)

# AVDD caps C2 100n, C3 1u
sym('C', 'C2', '100n', 's3-1:SMD_0805', 'AVDD pin decoupling (VDD_ADC)', 111, 99, npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(111, 97, 111, 96); label('VDD_ADC', 111, 96)
wire(111, 101, 111, 102); gnd(111, 102)
sym('C', 'C3', '1u', 's3-1:SMD_0805', 'AVDD pin bulk (VDD_ADC)', 116, 99, npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(116, 97, 116, 96); label('VDD_ADC', 116, 96)
wire(116, 101, 116, 102); gnd(116, 102)

# DVDD decoupling C1 (on VDD_ADC), VDD_ADC bulk is C20 near U7
sym('C', 'C1', '100n', 's3-1:SMD_0805', 'DVDD decoupling (VDD_ADC)', 111, 107, npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(111, 105, 111, 104); label('VDD_ADC', 111, 104)
wire(111, 109, 111, 110); gnd(111, 110)

# ---------------- I2C pull-ups (populated, to VCC_3V3) ----------------
sym('R', 'R3', '4k7', 's3-1:SMD_0805', 'SDA pull-up to VCC_3V3',
    59, 58, rot=90, npins=2, refdy=-4, valdy=4)
wire(56, 58, 52, 58); pwr3v3(52, 58)
wire(62, 58, 66, 58); label('SDA', 66, 58)
sym('R', 'R4', '4k7', 's3-1:SMD_0805', 'SCL pull-up to VCC_3V3',
    59, 62, rot=90, npins=2, refdy=-4, valdy=4)
wire(56, 62, 52, 62); pwr3v3(52, 62)
wire(62, 62, 66, 62); label('SCL', 66, 62)

# =================================================================
# SENSORS
# =================================================================
# ---------------- U2 LIS2DW12 ----------------
sym('LIS2DW12', 'U2', 'LIS2DW12', 's3-1:LGA-12_2x2mm_P0.5mm_LIS2DW12',
    '3-axis accel, I2C 0x18 (SA0=GND), CS high', 134, 59, npins=12, refdy=-12, valdy=14,
    datasheet='https://www.st.com/resource/en/datasheet/lis2dw12.pdf')
u2l = 124
wire(u2l, 53, 120, 53); label('SCL', 120, 53)
wire(u2l, 55, 120, 55); pwr3v3(120, 55)
wire(u2l, 57, 120, 57); gnd(120, 57)
wire(u2l, 59, 120, 59); label('SDA', 120, 59)
noconn(u2l, 61)
wire(u2l, 63, 120, 63); gnd(120, 63)
wire(u2l, 65, 120, 65); gnd(120, 65)
wire(u2l, 67, 120, 67); gnd(120, 67)
u2r = 144
wire(u2r, 61, 148, 61); label('INT1', 148, 61, rot=180)
wire(u2r, 63, 148, 63); noconn(148, 63)  # INT2 left NC: U2 pad11 pocket unroutable
wire(u2r, 65, 148, 65); pwr3v3(148, 65)
wire(u2r, 67, 148, 67); pwr3v3(148, 67)

# LIS2DW12 decoupling C7 100n, C8 4u7
sym('C', 'C7', '100n', 's3-1:SMD_0805', 'LIS2DW12 VDD decoupling', 153, 61, npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(153, 59, 153, 58); pwr3v3(153, 58)
wire(153, 63, 153, 64); gnd(153, 64)
sym('C', 'C8', '4u7', 's3-1:SMD_0805', 'LIS2DW12 bulk', 157, 61, npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(157, 59, 157, 58); pwr3v3(157, 58)
wire(157, 63, 157, 64); gnd(157, 64)

# ---------------- U3 TMP102 (on VDD_ADC rail, next to load cell) ----------------
sym('TMP102', 'U3', 'TMP102', 's3-1:SOT-563_1.6x1.6mm_P0.5mm',
    'Temp sensor, I2C 0x48 (ADD0=GND), near load cell', 134, 95, npins=6, refdy=-9, valdy=10,
    datasheet='https://www.ti.com/lit/ds/symlink/tmp102.pdf')
u3l = 126
wire(u3l, 92, 122, 92); label('SCL', 122, 92)
wire(u3l, 94, 122, 94); gnd(122, 94)
noconn(u3l, 96)   # ALERT
u3r = 142
wire(u3r, 94, 146, 94); label('SDA', 146, 94, rot=180)
wire(u3r, 96, 146, 96); label('VDD_ADC', 146, 96, rot=180)   # V+ on quiet rail
wire(u3r, 98, 146, 98); gnd(146, 98)

# TMP102 bypass C6 (VDD_ADC)
sym('C', 'C6', '100n', 's3-1:SMD_0805_V', 'TMP102 V+ bypass (VDD_ADC)', 152, 95, npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(152, 93, 152, 92); label('VDD_ADC', 152, 92)
wire(152, 97, 152, 98); gnd(152, 98)

# =================================================================
# U4: ESP32-S3-WROOM-1-N16R8 module
# =================================================================
# Official KiCad symbol: body +-12.7mm, pin tips at +-15.24mm (+-12g). Centre (224,55)g:
#   left x=212: EN p3@37, IO0 p27@41, IO1 p39@43, IO2 p38@45, IO3 p15@47,
#               IO4-IO7 p4-7 @49..55 step2, IO8 p12@57, IO9 p17@59, IO10 p18@61,
#               IO11 p19@63, IO12 p20@65, IO13 p21@67, IO14 p22@69,
#               IO15 p8@71, IO16 p9@73
#   right x=236: TXD0 p37@37, RXD0 p36@39, IO17 p10@41, IO18 p11@43,
#                USB_D- p13@45, USB_D+ p14@47, IO21 p23@49,
#                IO35-37 p28-30@51..55, IO38-42 p31-35@57..65,
#                IO45 p26@67, IO46 p16@69, IO47 p24@71, IO48 p25@73
#   top: 3V3 p2@(224,33); bottom: GND p1/40/41 stacked @(224,77)
text('U4 ESP32-S3-WROOM-1-N16R8: EN = R5 10k + C14 1u RC; BOOT = IO0 low at reset (TP4 to GND)', 176, 14)
text('USB = IO19/IO20 native USB-Serial/JTAG via R19/R20 22R. UART0 on TP5/TP6 + J10. INT1 on IO13 p21. INT2 left NC. IO45/46 strapping + IO35-37 octal PSRAM left NC. IO1/14/15/17/38-42/47 -> J10 expansion. BTN_TARE on IO48 (non-strapping). LCD_RST = local RC (R21/C26).', 176, 16)
sym('ESP32-S3-WROOM-1', 'U4', 'ESP32-S3-WROOM-1-N16R8', 's3-1:ESP32-S3-WROOM-1',
    'Wi-Fi4/BLE5 module, ESP32-S3 Xtensa dual-core, 16MB flash + 8MB octal PSRAM', 224, 55,
    npins=41, refdy=-30, valdy=34,
    datasheet='https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.html')

# --- 3V3 (pin 2, top): decoupling C13 10u + C12 100n + C25 1u near module ---
wire(224, 33, 224, 30); pwr3v3(224, 30)
sym('C', 'C13', '10u', 's3-1:SMD_0805', 'WROOM-1 3V3 bulk cap', 216, 84,
    npins=2, refdx=0, refdy=-4, valdx=0, valdy=6)
wire(216, 82, 216, 80); pwr3v3(216, 80)
wire(216, 86, 216, 88); gnd(216, 88)
sym('C', 'C12', '100n', 's3-1:SMD_0805_F', 'WROOM-1 3V3 decoupling', 220, 84,
    npins=2, refdx=0, refdy=-4, valdx=0, valdy=6)
wire(220, 82, 220, 80); pwr3v3(220, 80)
wire(220, 86, 220, 88); gnd(220, 88)
sym('C', 'C25', '1u', 's3-1:SMD_0805', 'WROOM-1 3V3 mid cap', 228, 84,
    npins=2, refdx=0, refdy=-4, valdx=0, valdy=6)
wire(228, 82, 228, 80); pwr3v3(228, 80)
wire(228, 86, 228, 88); gnd(228, 88)

# --- EN (pin 3 @ y37): R5 10k pull-up + C14 1u delay + TP3 (reset: TP3 to GND) ---
wire(212, 37, 208, 37); wire(208, 37, 202, 37); wire(202, 37, 198, 37)
wire(198, 37, 196, 37); label('EN', 196, 37, rot=180)
sym('R', 'R5', '10k', 's3-1:SMD_0805', 'EN pull-up (RC with C14)', 208, 34,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(208, 31, 208, 28); pwr3v3(208, 28)
sym('C', 'C14', '1u', 's3-1:SMD_0805_V', 'EN delay cap', 202, 33,
    npins=2, refdx=-4, refdy=0, valdx=0, valdy=6)
wire(202, 35, 202, 37)
wire(202, 31, 202, 29); gnd(202, 29)
sym('TP', 'TP3', 'TP_EN', 's3-1:TestPoint_1.2mm', 'EN test pad (reset)',
    198, 37, npins=1, bom=False, refdy=-6, valdy=6)

# --- IO0 BOOT (pin 27 @ y41): R6 10k pull-up + TP4 (short to GND = download) ---
wire(212, 41, 210, 41); wire(210, 41, 210, 43); wire(210, 43, 206, 43)
wire(206, 43, 204, 43); wire(204, 43, 196, 43); label('BOOT', 196, 43, rot=180)
sym('R', 'R6', '10k', 's3-1:SMD_0805', 'IO0 BOOT pull-up', 210, 46,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(210, 49, 210, 52); pwr3v3(210, 52)
sym('TP', 'TP4', 'TP_BOOT', 's3-1:TestPoint_1.2mm', 'BOOT pad: short to GND = download boot',
    206, 43, npins=1, bom=False, refdy=-6, valdy=6)

# --- left-edge GPIO map (rev C) ---
# IO4-IO7 p4-7 = LCD SCK/MOSI/CS/DC; IO8 p12 = CHRG_STAT; IO9 p17 = VBAT_SENSE;
# IO10 p18 = SDA; IO11 p19 = SCL; IO12 p20 = DRDY; IO13 p21 = INT1; IO14 p22 = NC (INT2 unroutable);
# IO15 p8 = NC (LCD_RST moved to local RC POR at J7; pad pocket unroutable);
# IO16 p9 = LCD_BL. IO1/IO2/IO3 = NC (IO3 strapping).
for _y, _net in [(49, 'LCD_SCK'), (51, 'LCD_MOSI'), (53, 'LCD_CS'), (55, 'LCD_DC'),
                 (57, 'CHRG_STAT'), (59, 'VBAT_SENSE'), (61, 'SDA'), (63, 'SCL'),
                 (65, 'DRDY'), (67, 'INT1'),
                 (73, 'LCD_BL')]:
    wire(212, _y, 208, _y); label(_net, 208, _y, rot=180)
wire(212, 69, 208, 69); label('EXP_IO14', 208, 69, rot=180)   # IO14 p22 -> J10
wire(212, 71, 208, 71); label('EXP_IO15', 208, 71, rot=180)   # IO15 p8  -> J10
for _y in (45, 47):                                   # IO2 p38, IO3 p15 (strapping)
    noconn(212, _y)
label('EXP_IO1', 212, 43, rot=180)                    # IO1 p39 -> J10 (label on pin
                                                      #  tip; BOOT jogs through y43)

# --- right edge ---
# UART0 -> TP5/TP6
wire(236, 37, 240, 37); wire(240, 37, 246, 37); label('TXD', 246, 37)
sym('TP', 'TP5', 'TP_TXD', 's3-1:TestPoint_1.2mm', 'UART0 TXD test pad',
    242, 37, npins=1, bom=False, refdy=-6, valdy=6)
wire(236, 39, 240, 39); wire(240, 39, 246, 39); label('RXD', 246, 39)
sym('TP', 'TP6', 'TP_RXD', 's3-1:TestPoint_1.2mm', 'UART0 RXD test pad',
    242, 39, npins=1, bom=False, refdy=-6, valdy=6)
# IO17 p10 @41 -> J10; IO18 p11 @43 = BTN_MODE
wire(236, 41, 242, 41); label('EXP_IO17', 242, 41)
wire(236, 43, 242, 43); label('BTN_MODE', 242, 43)
# native USB: D- p13 @45, D+ p14 @47 (through R19/R20 on the J5 side)
wire(236, 45, 242, 45); label('USB_DM_M', 242, 45)
wire(236, 47, 242, 47); label('USB_DP_M', 242, 47)
# IO21 p23 @49 = BUZZ
wire(236, 49, 242, 49); label('BUZZ', 242, 49)
# IO35-37 (octal PSRAM) + IO45/46 (strapping) -> NC;
# IO38-42 p31-35 @57..65, IO47 p24 @71 -> J10 expansion header;
# IO48 p25 @73 = BTN_TARE (rev C.1: moved off strapping IO46 — TARE pull-up
# held GPIO46 high and broke the GPIO0+GPIO46 download-mode condition)
for _y in range(51, 56, 2):      # IO35 p28, IO36 p29, IO37 p30
    noconn(236, _y)
for _y, _n in [(57, 'EXP_IO38'), (59, 'EXP_IO39'), (61, 'EXP_IO40'),
               (63, 'EXP_IO41'), (65, 'EXP_IO42'), (71, 'EXP_IO47')]:
    wire(236, _y, 242, _y); label(_n, 242, _y)   # IO38-42 p31-35, IO47 p24 -> J10
noconn(236, 67)                  # IO45 p26 — strapping (VDD_SPI), left NC
noconn(236, 69)                  # IO46 p16 — strapping (boot mode), left NC
wire(236, 73, 242, 73); label('BTN_TARE', 242, 73)

# --- module GND (pins 1/40/EP41 stacked) ---
wire(224, 77, 224, 80); gnd(224, 80)

# --- spare rail/bus test pads (bring-up); TP pin tip is at x-2g ---
sym('TP', 'TP9', 'TP_VBUS', 's3-1:TestPoint_1.2mm', 'VIN5V test pad',
    264, 95, npins=1, bom=False, refdy=-6, valdy=6)
wire(262, 95, 262, 93); label('VIN5V', 262, 93)
sym('TP', 'TP10', 'TP_3V3', 's3-1:TestPoint_1.2mm', '3V3 test pad',
    268, 95, npins=1, bom=False, refdy=-6, valdy=6)
wire(266, 95, 266, 93); pwr3v3(266, 93)
sym('TP', 'TP11', 'TP_GND', 's3-1:TestPoint_1.2mm', 'GND test pad',
    272, 95, npins=1, bom=False, refdy=-6, valdy=6)
wire(270, 95, 270, 93); gnd(270, 93)
sym('TP', 'TP12', 'TP_SDA', 's3-1:TestPoint_1.2mm', 'SDA test pad',
    276, 95, npins=1, bom=False, refdy=-6, valdy=6)
wire(274, 95, 274, 93); label('SDA', 274, 93)
sym('TP', 'TP13', 'TP_SCL', 's3-1:TestPoint_1.2mm', 'SCL test pad',
    280, 95, npins=1, bom=False, refdy=-6, valdy=6)
wire(278, 95, 278, 93); label('SCL', 278, 93)

# =================================================================
# EXPANSION (machine power/driver board)
# =================================================================
text('Expansion J10 -> machine power/driver board. A row = GPIO via 100R series, B row = power + bus', 210, 100)
# CONN_2x10_EXP at (250,115): pins 1-10 left x244 y106..124 step2 (row A = GPIO),
# pins 11-20 right x256 y106..124 step2 (row B = power/bus)
sym('CONN_2x10_EXP', 'J10', 'EXP', 's3-1:PinHeader_2x10_P2.54mm_Vertical',
    'Expansion to machine power/driver board', 250, 115, npins=20,
    refdy=-15, valdy=16)
# --- row A: GPIO through 100R series R24-R31 (pins 1-8), IO1/IO47 direct ---
# NOTE: the netlister maps pin1 to the BOTTOM pin of each column
# (pins run 1-10 bottom->top left, 11-20 bottom->top right), so wire reversed.
for _i, (_r, _net, _hnet) in enumerate([
        ('R24', 'EXP_IO38', 'XIO38'), ('R25', 'EXP_IO39', 'XIO39'),
        ('R26', 'EXP_IO40', 'XIO40'), ('R27', 'EXP_IO41', 'XIO41'),
        ('R28', 'EXP_IO42', 'XIO42'), ('R29', 'EXP_IO14', 'XIO14'),
        ('R30', 'EXP_IO15', 'XIO15'), ('R31', 'EXP_IO17', 'XIO17')]):
    _y = 124 - _i * 2
    wire(244, _y, 232, _y); label(_hnet, 238, _y)
    sym('R', _r, '100R', 's3-1:SMD_0805_V',
        f'J10 series R, {_net}', 229, _y, rot=90, npins=2, refdy=-4, valdy=4)
    wire(226, _y, 218, _y); label(_net, 218, _y, rot=180)
wire(244, 108, 218, 108); label('EXP_IO1', 218, 108, rot=180)   # pin9
wire(244, 106, 218, 106); label('EXP_IO47', 218, 106, rot=180)  # pin10
# R32: HEATER_EN-side default-safe pulldown on XIO15 (pin7 at y112); keep
# fitted so a floating/expansionless board can never assert the heater line
sym('R', 'R32', '10k', 's3-1:SMD_0805', 'XIO15 (HEATER_EN) pulldown', 222, 115,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(232, 112, 222, 112)
wire(222, 118, 222, 124); gnd(222, 124)
# --- row B: power + bus (pin11 bottom y124 ... pin20 top y106) ---
wire(256, 124, 264, 124); label('EXP_5V', 264, 124)     # pin11
wire(256, 122, 264, 122); pwr3v3(264, 122)             # pin12
for _y in (116, 118, 120):                              # pins 13-15 GND
    wire(256, _y, 262, _y); gnd(262, _y)
wire(256, 114, 264, 114); label('SDA', 264, 114)        # pin16
wire(256, 112, 264, 112); label('SCL', 264, 112)        # pin17
wire(256, 110, 264, 110); label('TXD', 264, 110)        # pin18
wire(256, 108, 264, 108); label('RXD', 264, 108)        # pin19
wire(256, 106, 264, 106); label('EN', 264, 106)         # pin20
# D3: EXP_5V -> VIN5V injection option (DNP). Fit B5819W = diode-OR (no USB
# backfeed, ~0.3V drop still charges via TP4057) or a wire/0R jumper for
# full 5V. Never fitted on the pure-scale BOM.
sym('D_Schottky', 'D3', 'B5819W', 's3-1:D_SOD-123',
    'EXP_5V -> VIN5V injection (DNP: Schottky or wire jumper)', 272, 124,
    npins=2, refdy=-5, valdy=5, dnp=True, bom=False)
wire(268, 124, 264, 124)
wire(276, 124, 280, 124); label('VIN5V', 280, 124)

# =================================================================
# HMI (right side)
# =================================================================
# ---------------- J7: LCD connector ----------------
# CONN_1x08_LCD pins x-7.62 -> grid -6 (x292): 1 GND y53 .. 8 LCD_BL y67
sym('CONN_1x08_LCD', 'J7', 'LCD', 's3-1:JST_PH_B8B-PH-K_1x08_P2.00mm_Vertical',
    'LCD SPI connector: GND/3V3/SCK/MOSI/CS/DC/RST/BL', 298, 60, npins=8,
    refdy=-12, valdy=13)
wire(292, 53, 288, 53); gnd(288, 53)
wire(292, 55, 288, 55); pwr3v3(288, 55)
wire(292, 57, 288, 57); label('LCD_SCK', 288, 57)
wire(292, 59, 288, 59); label('LCD_MOSI', 288, 59)
wire(292, 61, 288, 61); label('LCD_CS', 288, 61)
wire(292, 63, 288, 63); label('LCD_DC', 288, 63)
wire(292, 65, 288, 65); label('LCD_RST', 288, 65)
wire(292, 67, 288, 67); label('LCD_BL', 288, 67)

# ---------------- J8: button connector ----------------
# CONN_1x04_BTN pins x292: 1 GND y85, 2 BTN_TARE y83, 3 BTN_MODE y81, 4 VCC_3V3 y79
sym('CONN_1x04_BTN', 'J8', 'BTN', 's3-1:JST_PH_B4B-PH-K_1x04_P2.00mm_Vertical',
    'Buttons: 1=GND 2=TARE 3=MODE 4=3V3 (TTP223 touch supply)', 298, 82, npins=4, refdy=-8, valdy=8)
wire(292, 85, 288, 85); gnd(288, 85)
wire(292, 83, 288, 83); label('BTN_TARE', 288, 83)
wire(292, 81, 288, 81); label('BTN_MODE', 288, 81)
wire(292, 79, 288, 79); pwr3v3(288, 79)

# button pull-ups + debounce caps
sym('R', 'R15', '10k', 's3-1:SMD_0805', 'BTN_TARE pull-up', 306, 79,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(306, 76, 306, 73); pwr3v3(306, 73)
wire(306, 82, 306, 84); label('BTN_TARE', 306, 84)
sym('C', 'C18', '100n', 's3-1:SMD_0805', 'BTN_TARE debounce', 312, 79,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(312, 77, 312, 73); label('BTN_TARE', 312, 73)
wire(312, 81, 312, 85); gnd(312, 85)
sym('R', 'R16', '10k', 's3-1:SMD_0805', 'BTN_MODE pull-up', 318, 79,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(318, 76, 318, 73); pwr3v3(318, 73)
wire(318, 82, 318, 84); label('BTN_MODE', 318, 84)
sym('C', 'C19', '100n', 's3-1:SMD_0805', 'BTN_MODE debounce', 324, 79,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(324, 77, 324, 73); label('BTN_MODE', 324, 73)
wire(324, 81, 324, 85); gnd(324, 85)

# ---------------- LCD_RST RC power-on reset ----------------
# Direct U4 IO15 -> J7.7 route was forced around the board perimeter
# (analog B.Cu keepout + connector walls). LCD_RST is now a local RC POR:
# R21 10k pull-up + C26 100n delay at J7. IO15 left NC.
# Board uses silk-trimmed variants: R21=SMD_0805_V_OE (open east toward C26),
# C26=SMD_0805_V_OW (open west toward R21) — facing silk removed.
sym('R', 'R21', '10k', 's3-1:SMD_0805', 'LCD_RST pull-up (RC POR with C26)', 306, 61,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(306, 58, 306, 55); label('LCD_RST', 306, 55)      # pin1 -> LCD_RST node
wire(306, 64, 306, 68); pwr3v3(306, 68)               # pin2 -> 3V3
sym('C', 'C26', '100n', 's3-1:SMD_0805', 'LCD_RST delay cap (RC POR with R21)', 312, 65,
    npins=2, refdx=0, refdy=-4, valdx=0, valdy=6)
wire(312, 63, 312, 61); label('LCD_RST', 312, 61)
wire(312, 67, 312, 70); gnd(312, 70)

# ---------------- buzzer BZ1 + Q2 + D2 + R17/R18 ----------------
sym('BUZZER', 'BZ1', 'MLT-8530', 's3-1:MagneticBuzzer_CUI_CMT-8540S-SMT',
    'Passive magnetic buzzer 8.5mm SMD, driven via Q2', 298, 100, npins=2, refdy=-7, valdy=7,
    datasheet='https://www.cuidevices.com/product/resource/pdf/cmt-8540s-smt.pdf')
wire(292, 99, 288, 99); pwr3v3(288, 99)                # pin1 + -> 3V3
wire(292, 101, 288, 101); label('BUZZ_SW', 288, 101)   # pin2 - -> switched node
# D2 flyback diode across buzzer: anode=BUZZ_SW, cathode=3V3
sym('D', 'D2', '1N4148W', 's3-1:D_SOD-123', 'Buzzer flyback diode', 304, 106,
    npins=2, refdy=-5, valdy=5)
wire(300, 106, 296, 106); label('BUZZ_SW', 296, 106)
wire(308, 106, 312, 106); pwr3v3(312, 106)
# Q2 2N7002 (SOT-23 1=G 2=S 3=D): G left, S right-top, D right-bottom
sym('2N7002', 'Q2', '2N7002', 's3-1:SOT-23', 'Buzzer low-side switch', 304, 112,
    npins=3, refdy=-7, valdy=7)
wire(298, 112, 292, 112)                              # gate <- R17
wire(310, 110, 314, 110); gnd(314, 110)               # source (pin 2)
wire(310, 114, 314, 114); label('BUZZ_SW', 314, 114, rot=180)  # drain (pin 3)
sym('R', 'R17', '1k', 's3-1:SMD_0805', 'Q2 gate series R', 286, 112, rot=90,
    npins=2, refdy=-4, valdy=4)
wire(283, 112, 279, 112); label('BUZZ', 279, 112)
wire(289, 112, 292, 112)
sym('R', 'R18', '100k', 's3-1:SMD_0805', 'Q2 gate pulldown', 292, 117,
    npins=2, refdx=-4, refdy=0, valdx=4, valdy=0)
wire(292, 114, 292, 112)
wire(292, 120, 292, 123); gnd(292, 123)

# ---------------- mounting holes (NPTH, mechanical) ----------------
for _i, (_r, _x) in enumerate((('MH1', 210), ('MH2', 216), ('MH3', 222), ('MH4', 228))):
    sym('MOUNT_HOLE', _r, 'M2', 's3-1:MountingHole_M2_NPTH',
        'M2 mounting hole NPTH', _x, 140, npins=0, bom=False)

w('\t(sheet_instances')
w('\t\t(path "/b2a10000-0000-4000-8000-000000000001"')
w('\t\t\t(page "1")')
w('\t\t)')
w('\t)')
w('\t(symbol_instances')
for _su, _ref, _val, _fp in SYMS:
    w(f'\t\t(path "/b2a10000-0000-4000-8000-000000000001/{_su}"')
    w(f'\t\t\t(reference "{_ref}")')
    w('\t\t\t(unit 1)')
    w(f'\t\t\t(value "{_val}")')
    w(f'\t\t\t(footprint "{_fp}")')
    w('\t\t)')
w('\t)')
w(')')

open('/home/puji/coffee-scale/pcb/s3.1/s3.1.kicad_sch', 'w').write('\n'.join(out) + '\n')
print(f'wrote {len(out)} lines, {uid_n} uuids')
