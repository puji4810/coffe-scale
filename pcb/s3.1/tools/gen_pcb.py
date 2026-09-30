#!/usr/bin/env python3
"""Generate s3.1.kicad_pcb — Rev s3.1 dual-use control board. 90x55, 2-layer.
Board: analog block x<27 (quiet, B.Cu solid GND, all analog routing on F.Cu),
ESP32-S3-WROOM-1-N16R8 module top-centre with antenna overhanging the north
edge, power/charge east (x>58), J10 2x10 expansion + J2 load cell + J6 battery
on the south edge, J11 VIN2 on the west edge, TP strip NW, 4x M2 corners."""
import re, os

DIR = "/home/puji/coffee-scale/pcb/s3.1"
PRETTY = os.path.join(DIR, "s3.1.pretty")
SHEET = "b2a10000-0000-4000-8000-000000000001"
uuid_n = [0]
def uid():
    uuid_n[0] += 1
    return f"b2a20000-0000-4000-8000-{uuid_n[0]:012x}"

# symbol ref -> schematic uuid, loaded from the generated .kicad_sch
SYM = {}
_sch = open(os.path.join(DIR, "s3.1.kicad_sch")).read()
for _m in re.finditer(r'\(symbol\s+\(lib_id "s3-1:[^"]+"\)(.*?)\(reference "([^"]+)"',
                      _sch, re.S):
    _u = re.search(r'\(uuid "([^"]+)"\)', _m.group(1))
    if _u:
        SYM[_m.group(2)] = _u.group(1)
if False:
    SYM = {
 "BZ1":"b2a10000-0000-4000-8000-000000000334",
 "C1":"b2a10000-0000-4000-8000-000000000183",
 "C2":"b2a10000-0000-4000-8000-000000000173",
 "C3":"b2a10000-0000-4000-8000-00000000017b",
 "C4":"b2a10000-0000-4000-8000-00000000016b",
 "C5":"b2a10000-0000-4000-8000-00000000011b",
 "C6":"b2a10000-0000-4000-8000-0000000001ec",
 "C7":"b2a10000-0000-4000-8000-0000000001c6",
 "C8":"b2a10000-0000-4000-8000-0000000001cf",
 "C9":"b2a10000-0000-4000-8000-0000000000c0",
 "C10":"b2a10000-0000-4000-8000-000000000122",
 "C11":"b2a10000-0000-4000-8000-00000000012a",
 "C12":"b2a10000-0000-4000-8000-000000000269",
 "C13":"b2a10000-0000-4000-8000-000000000260",
 "C14":"b2a10000-0000-4000-8000-00000000027b",
 "C15":"b2a10000-0000-4000-8000-000000000050",
 "C16":"b2a10000-0000-4000-8000-000000000084",
 "C17":"b2a10000-0000-4000-8000-0000000000ef",
 "C18":"b2a10000-0000-4000-8000-00000000031c",
 "C19":"b2a10000-0000-4000-8000-00000000032c",
 "C20":"b2a10000-0000-4000-8000-0000000000dc",
 "C21":"b2a10000-0000-4000-8000-0000000000b8",
 "C22":"b2a10000-0000-4000-8000-0000000000d4",
 "D1":"b2a10000-0000-4000-8000-00000000008c",
 "D2":"b2a10000-0000-4000-8000-00000000033c",
 "J2":"b2a10000-0000-4000-8000-0000000000fb",
 "J5":"b2a10000-0000-4000-8000-000000000009",
 "J6":"b2a10000-0000-4000-8000-000000000079",
 "J7":"b2a10000-0000-4000-8000-0000000002ee",
 "J8":"b2a10000-0000-4000-8000-000000000309",
 "Q1":"b2a10000-0000-4000-8000-000000000096",
 "Q2":"b2a10000-0000-4000-8000-000000000344",
 "R1":"b2a10000-0000-4000-8000-00000000010d",
 "R2":"b2a10000-0000-4000-8000-000000000114",
 "R3":"b2a10000-0000-4000-8000-00000000018b",
 "R4":"b2a10000-0000-4000-8000-000000000193",
 "R5":"b2a10000-0000-4000-8000-000000000277",
 "R6":"b2a10000-0000-4000-8000-0000000002ab",
 "R7":"b2a10000-0000-4000-8000-0000000002b6",
 "R8":"b2a10000-0000-4000-8000-000000000035",
 "R9":"b2a10000-0000-4000-8000-00000000003c",
 "R10":"b2a10000-0000-4000-8000-000000000063",
 "R11":"b2a10000-0000-4000-8000-00000000006f",
 "R12":"b2a10000-0000-4000-8000-0000000000a0",
 "R13":"b2a10000-0000-4000-8000-0000000000e4",
 "R14":"b2a10000-0000-4000-8000-0000000000ea",
 "R15":"b2a10000-0000-4000-8000-000000000314",
 "R16":"b2a10000-0000-4000-8000-000000000324",
 "R17":"b2a10000-0000-4000-8000-00000000034e",
 "R18":"b2a10000-0000-4000-8000-000000000354",
 "TP3":"b2a10000-0000-4000-8000-000000000282",
 "TP4":"b2a10000-0000-4000-8000-0000000002b1",
 "TP5":"b2a10000-0000-4000-8000-0000000002c4",
 "TP6":"b2a10000-0000-4000-8000-0000000002bf",
 "TP7":"b2a10000-0000-4000-8000-000000000166",
 "TP8":"b2a10000-0000-4000-8000-000000000169",
 "U1":"b2a10000-0000-4000-8000-000000000132",
 "U2":"b2a10000-0000-4000-8000-00000000019b",
 "U3":"b2a10000-0000-4000-8000-0000000001d8",
 "U4":"b2a10000-0000-4000-8000-0000000001f6",
 "U5":"b2a10000-0000-4000-8000-000000000058",
 "U6":"b2a10000-0000-4000-8000-0000000000a8",
 "U7":"b2a10000-0000-4000-8000-0000000000c9",}
VAL = {"J2":"LOADCELL","J5":"TYPE-C-31-M-12","J6":"BAT","J7":"LCD","J8":"BTN",
 "U1":"NAU7802SGI","U2":"LIS2DW12","U3":"TMP102","U4":"ESP32-S3-WROOM-1-N16R8",
 "U5":"TP4057-42","U6":"XC6220B331MR-G","U7":"HT7533-1","U8":"USBLC6-2SC6",
 "Q1":"AO3401A","Q2":"2N7002",
 "D1":"B5819W","D2":"1N4148W","BZ1":"MLT-8530",
 "R1":"1k","R2":"1k","R3":"4k7","R4":"4k7","R5":"10k","R6":"10k",
 "R8":"5k1","R9":"5k1","R10":"2k7","R11":"10k","R12":"100k","R13":"100k",
 "R14":"100k","R15":"10k","R16":"10k","R17":"1k","R18":"100k",
 "R19":"22R","R20":"22R","R21":"10k",
 "R22":"1k","R23":"1k","R24":"100R","R25":"100R","R26":"100R","R27":"100R",
 "R28":"100R","R29":"100R","R30":"100R","R31":"100R","R32":"10k",
 "MH1":"M2","MH2":"M2","MH3":"M2","MH4":"M2",
 "C1":"100n","C2":"100n","C3":"1u","C4":"100n","C5":"100n","C6":"100n",
 "C7":"100n","C8":"4u7","C9":"10u","C10":"1n","C11":"1n","C12":"100n",
 "C13":"10u","C14":"1u","C15":"10u","C16":"10u","C17":"100n","C18":"100n",
 "C19":"100n","C20":"10u","C21":"10u","C22":"10u","C23":"DNP","C24":"DNP",
 "C25":"1u","C26":"100n","C27":"100n","C28":"1n","C29":"1n",
 "J10":"EXP","J11":"CH2","D3":"B5819W",
 "TP3":"TP_EN","TP4":"TP_BOOT","TP5":"TP_TXD","TP6":"TP_RXD",
 "TP7":"TP_VBG","TP8":"TP_AVDD","TP9":"TP_VBUS","TP10":"TP_3V3",
 "TP11":"TP_GND","TP12":"TP_SDA","TP13":"TP_SCL"}
DESC = {"J2":"L6D load cell E+/E-/S-/S+/SHLD","J5":"USB-C 16-pin receptacle, USB 2.0",
 "J6":"Li-ion battery 1S, JST PH","J7":"LCD SPI connector: GND/3V3/SCK/MOSI/CS/DC/RST/BL",
 "J8":"Buttons: 1=GND 2=TARE 3=MODE 4=3V3 (TTP223 touch supply)",
 "U1":"24-bit bridge ADC, I2C 0x2A","U2":"3-axis accel, I2C 0x18 (SA0=GND), CS high",
 "U3":"Temp sensor, I2C 0x48 (ADD0=GND), near load cell",
 "U4":"Wi-Fi4/BLE5 module, ESP32-S3 Xtensa dual-core, 16MB flash + 8MB octal PSRAM",
 "U5":"Li-ion charger, ~370mA via R10=2k7","U6":"Main 3.3V LDO, 1A (S3 Wi-Fi peaks + LCD backlight)",
 "U7":"ADC/analog rail LDO VDD_ADC 3.3V 100mA","U8":"USB 2.0 low-cap ESD array",
 "Q1":"P-FET load switch: D=BAT S=VSYS G=VBUS",
 "Q2":"Buzzer low-side switch","D1":"VBUS->VSYS Schottky, load sharing",
 "D2":"Buzzer flyback diode","BZ1":"Passive magnetic buzzer 8.5mm SMD, driven via Q2",
 "R1":"RFI series R, S+","R2":"RFI series R, S-",
 "R3":"SDA pull-up to VCC_3V3","R4":"SCL pull-up to VCC_3V3","R5":"EN pull-up (RC with C14)",
 "R6":"IO0 BOOT pull-up",
 "R8":"CC1 pulldown 5.1k","R9":"CC2 pulldown 5.1k","R10":"PROG resistor, ~370mA charge",
 "R11":"CHRG open-drain pull-up","R12":"Q1 gate pulldown","R13":"VBAT divider top",
 "R14":"VBAT divider bottom","R15":"BTN_TARE pull-up","R16":"BTN_MODE pull-up",
 "R17":"Q2 gate series R","R18":"Q2 gate pulldown",
 "R19":"USB D+ series R 22R","R20":"USB D- series R 22R",
 "R21":"LCD_RST pull-up (RC POR with C26)",
 "MH1":"M2 mounting hole NPTH","MH2":"M2 mounting hole NPTH",
 "MH3":"M2 mounting hole NPTH","MH4":"M2 mounting hole NPTH",
 "C1":"DVDD decoupling (VDD_ADC)","C2":"AVDD LDO out cap","C3":"AVDD bulk cap",
 "C4":"VBG bypass","C5":"Differential cap across VIN1","C6":"TMP102 V+ bypass (VDD_ADC)",
 "C7":"LIS2DW12 VDD decoupling","C8":"LIS2DW12 bulk","C9":"3V3 LDO output cap",
 "C10":"CM filter, SIG_P to GND","C11":"CM filter, SIG_N to GND",
 "C12":"WROOM-1 3V3 decoupling","C13":"WROOM-1 3V3 bulk cap","C14":"EN delay cap",
 "C15":"VBUS input cap","C16":"Battery rail cap","C17":"VBAT sense filter",
 "C18":"BTN_TARE debounce","C19":"BTN_MODE debounce","C20":"VDD_ADC LDO output cap",
 "C21":"VSYS input cap U6","C22":"VSYS input cap U7","C23":"USB DP shunt cap pad (DNP)",
 "C24":"USB DM shunt cap pad (DNP)","C25":"WROOM-1 3V3 mid cap",
 "C26":"LCD_RST delay cap (RC POR with R21)",
 "TP3":"EN test pad (reset)","TP4":"BOOT pad: short to GND = download boot",
 "TP5":"UART0 TXD test pad","TP6":"UART0 RXD test pad","TP7":"VBG test pad",
 "TP8":"AVDD test pad","TP9":"VBUS test pad","TP10":"3V3 test pad",
 "TP11":"GND test pad","TP12":"SDA test pad","TP13":"SCL test pad",
 "J10":"Expansion to machine power/driver board",
 "J11":"ADC ch2: 1=E+(AVDD) 2=E-(GND) 3=S2- 4=S2+",
 "R22":"RFI series R, S2-","R23":"RFI series R, S2+",
 "R24":"J10 series R, EXP_IO38","R25":"J10 series R, EXP_IO39",
 "R26":"J10 series R, EXP_IO40","R27":"J10 series R, EXP_IO41",
 "R28":"J10 series R, EXP_IO42","R29":"J10 series R, EXP_IO14",
 "R30":"J10 series R, EXP_IO15","R31":"J10 series R, EXP_IO17",
 "R32":"XIO15 (HEATER_EN) pulldown",
 "C27":"Differential cap across VIN2","C28":"CM filter, SIG2_P to GND",
 "C29":"CM filter, SIG2_N to GND",
 "D3":"EXP_5V -> VBUS injection (DNP: Schottky or wire jumper)"}
DNP = {"C23", "C24", "D3"}
DS = {"U1":"https://www.nuvoton.com/export/resource-files/en-us--DS_NAU7802_DataSheet_EN_Rev2.6.pdf",
 "U2":"https://www.st.com/resource/en/datasheet/lis2dw12.pdf",
 "U3":"https://www.ti.com/lit/ds/symlink/tmp102.pdf",
 "U4":"https://documentation.espressif.com/esp32-s3-wroom-1_wroom-1u_datasheet_en.html",
 "U5":"https://www.lcsc.com/datasheet/C12044.pdf",
 "U6":"https://www.lcsc.com/datasheet/C86539.pdf",
 "U7":"https://www.holtek.com/documents/10179/116711/HT75xx-1v250.pdf",
 "U8":"https://www.st.com/resource/en/datasheet/usblc6-2sc6.pdf",
 "Q1":"https://www.aosmd.com/res/datasheets/AO3401A.pdf",
 "BZ1":"https://www.cuidevices.com/product/resource/pdf/cmt-8540s-smt.pdf"}

# pin -> net, matching the schematic netlist exactly (parity)
NETS = {
 "BZ1":{"1":"VCC_3V3","2":"BUZZ_SW"},
 "C1":{"1":"VDD_ADC","2":"GND"},
 "C2":{"1":"AVDD","2":"GND"},
 "C3":{"1":"AVDD","2":"GND"},
 "C4":{"1":"VBG","2":"GND"},
 "C5":{"1":"SIG_P","2":"SIG_N"},
 "C6":{"1":"VDD_ADC","2":"GND"},
 "C7":{"1":"VCC_3V3","2":"GND"},
 "C8":{"1":"VCC_3V3","2":"GND"},
 "C9":{"1":"VCC_3V3","2":"GND"},
 "C10":{"1":"SIG_P","2":"GND"},
 "C11":{"1":"SIG_N","2":"GND"},
 "C12":{"1":"VCC_3V3","2":"GND"},
 "C13":{"1":"VCC_3V3","2":"GND"},
 "C14":{"1":"GND","2":"EN"},
 "C23":{"1":"USB_DP_M","2":"GND"},
 "C24":{"1":"USB_DM_M","2":"GND"},
 "C25":{"1":"VCC_3V3","2":"GND"},
 "C15":{"1":"VBUS","2":"GND"},
 "C16":{"1":"BAT","2":"GND"},
 "C17":{"1":"VBAT_SENSE","2":"GND"},
 "C18":{"1":"BTN_TARE","2":"GND"},
 "C19":{"1":"BTN_MODE","2":"GND"},
 "C20":{"1":"VDD_ADC","2":"GND"},
 "C21":{"1":"VSYS","2":"GND"},
 "C22":{"1":"VSYS","2":"GND"},
 "D1":{"1":"VSYS","2":"VBUS"},
 "D2":{"1":"VCC_3V3","2":"BUZZ_SW"},
 "J2":{"1":"AVDD","2":"GND","3":"LC_SIGN","4":"LC_SIGP","5":"GND"},
 "J5":{"A1":"GND","A4":"VBUS","A5":"Net-(J5-CC1)","A6":"USB_DP","A7":"USB_DM","A8":"unconnected-(J5-SBU1-PadA8)","A9":"VBUS","B1":"GND","B4":"VBUS","B5":"Net-(J5-CC2)","B6":"USB_DP","B7":"USB_DM","B8":"unconnected-(J5-SBU2-PadB8)","B9":"VBUS","SH":"GND","A12":"GND","B12":"GND"},
 "J6":{"1":"BAT","2":"GND"},
 "J7":{"1":"GND","2":"VCC_3V3","3":"LCD_SCK","4":"LCD_MOSI","5":"LCD_CS","6":"LCD_DC","7":"LCD_RST","8":"LCD_BL"},
 "J8":{"1":"GND","2":"BTN_TARE","3":"BTN_MODE","4":"VCC_3V3"},
 "Q1":{"1":"VBUS","2":"VSYS","3":"BAT"},
 "Q2":{"1":"Net-(Q2-G)","2":"GND","3":"BUZZ_SW"},
 "R1":{"1":"LC_SIGP","2":"SIG_P"},
 "R2":{"1":"LC_SIGN","2":"SIG_N"},
 "R3":{"1":"VCC_3V3","2":"SDA"},
 "R4":{"1":"VCC_3V3","2":"SCL"},
 "R5":{"1":"VCC_3V3","2":"EN"},
 "R6":{"1":"BOOT","2":"VCC_3V3"},
 "R8":{"1":"Net-(J5-CC1)","2":"GND"},
 "R9":{"1":"Net-(J5-CC2)","2":"GND"},
 "R10":{"1":"Net-(U5-PROG)","2":"GND"},
 "R11":{"1":"VCC_3V3","2":"CHRG_STAT"},
 "R12":{"1":"VBUS","2":"GND"},
 "R13":{"1":"BAT","2":"VBAT_SENSE"},
 "R14":{"1":"VBAT_SENSE","2":"GND"},
 "R15":{"1":"VCC_3V3","2":"BTN_TARE"},
 "R16":{"1":"VCC_3V3","2":"BTN_MODE"},
 "R17":{"1":"BUZZ","2":"Net-(Q2-G)"},
 "R18":{"1":"Net-(Q2-G)","2":"GND"},
 "R19":{"1":"USB_DP","2":"USB_DP_M"},
 "R20":{"1":"USB_DM","2":"USB_DM_M"},
 "R21":{"1":"LCD_RST","2":"VCC_3V3"},
 "C26":{"1":"LCD_RST","2":"GND"},
 "MH1":{},"MH2":{},"MH3":{},"MH4":{},
 "J10":{"1":"XIO38","2":"XIO39","3":"XIO40","4":"XIO41","5":"XIO42","6":"XIO14",
        "7":"XIO15","8":"XIO17","9":"EXP_IO1","10":"EXP_IO47","11":"EXP_5V",
        "12":"VCC_3V3","13":"GND","14":"GND","15":"GND","16":"SDA","17":"SCL",
        "18":"TXD","19":"RXD","20":"EN"},
 "J11":{"1":"AVDD","2":"GND","3":"LC_SIG2N","4":"LC_SIG2P"},
 "R22":{"1":"LC_SIG2N","2":"SIG2_N"},
 "R23":{"1":"LC_SIG2P","2":"SIG2_P"},
 "R24":{"1":"EXP_IO38","2":"XIO38"},"R25":{"1":"EXP_IO39","2":"XIO39"},
 "R26":{"1":"EXP_IO40","2":"XIO40"},"R27":{"1":"EXP_IO41","2":"XIO41"},
 "R28":{"1":"EXP_IO42","2":"XIO42"},"R29":{"1":"EXP_IO14","2":"XIO14"},
 "R30":{"1":"EXP_IO15","2":"XIO15"},"R31":{"1":"EXP_IO17","2":"XIO17"},
 "R32":{"1":"XIO15","2":"GND"},
 "C27":{"1":"SIG2_P","2":"SIG2_N"},
 "C28":{"1":"SIG2_P","2":"GND"},
 "C29":{"1":"SIG2_N","2":"GND"},
 "D3":{"1":"VBUS","2":"EXP_5V"},
 "TP3":{"1":"EN"},
 "TP4":{"1":"BOOT"},
 "TP5":{"1":"TXD"},
 "TP6":{"1":"RXD"},
 "TP7":{"1":"VBG"},
 "TP8":{"1":"AVDD"},
 "TP9":{"1":"VBUS"},
 "TP10":{"1":"VCC_3V3"},
 "TP11":{"1":"GND"},
 "TP12":{"1":"SDA"},
 "TP13":{"1":"SCL"},
 "U1":{"1":"AVDD","2":"SIG_N","3":"SIG_P","4":"SIG2_N","5":"SIG2_P","6":"VBG","7":"GND","8":"GND","9":"GND","10":"unconnected-(U1-XIN-Pad10)","11":"unconnected-(U1-XOUT-Pad11)","12":"DRDY","13":"SCL","14":"SDA","15":"VDD_ADC","16":"AVDD"},
 "U2":{"1":"SCL","2":"VCC_3V3","3":"GND","4":"SDA","5":"unconnected-(U2-NC-Pad5)","6":"GND","7":"GND","8":"GND","9":"VCC_3V3","10":"VCC_3V3","11":"unconnected-(U2-INT2-Pad11)","12":"INT1"},
 "U3":{"1":"SCL","2":"GND","3":"unconnected-(U3-ALERT-Pad3)","4":"GND","5":"VDD_ADC","6":"SDA"},
 # ESP32-S3-WROOM-1-N16R8: pads 1-14 left col top->bottom, 15-26 bottom row,
 # 27-40 right col bottom->top, 41 = EPAD. IO35-37 = octal PSRAM (NC),
 # IO3/45/46 strapping (NC), USB_D-/D+ = IO19/IO20, TXD0/RXD0 = IO43/IO44.
 "U4":{"1":"GND","2":"VCC_3V3","3":"EN","4":"LCD_SCK","5":"LCD_MOSI","6":"LCD_CS",
  "7":"LCD_DC","8":"EXP_IO15","9":"LCD_BL",
  "10":"EXP_IO17","11":"BTN_MODE",
  "12":"CHRG_STAT","13":"USB_DM_M","14":"USB_DP_M",
  "15":"unconnected-(U4-IO3-Pad15)","16":"unconnected-(U4-IO46-Pad16)",
  "17":"VBAT_SENSE","18":"SDA","19":"SCL","20":"DRDY","21":"INT1",
  "22":"EXP_IO14",
  "23":"BUZZ","24":"EXP_IO47","25":"BTN_TARE",
  "26":"unconnected-(U4-IO45-Pad26)","27":"BOOT",
  "28":"unconnected-(U4-IO35-Pad28)","29":"unconnected-(U4-IO36-Pad29)",
  "30":"unconnected-(U4-IO37-Pad30)","31":"EXP_IO38",
  "32":"EXP_IO39","33":"EXP_IO40",
  "34":"EXP_IO41","35":"EXP_IO42",
  "36":"RXD","37":"TXD","38":"unconnected-(U4-IO2-Pad38)",
  "39":"EXP_IO1","40":"GND","41":"GND"},
 "U8":{"1":"USB_DM","2":"GND","3":"USB_DP","4":"unconnected-(U8-IO3-Pad4)",
  "5":"VBUS","6":"unconnected-(U8-IO4-Pad6)"},
 "U5":{"1":"CHRG_STAT","2":"GND","3":"BAT","4":"VBUS","5":"unconnected-(U5-STDBY-Pad5)","6":"Net-(U5-PROG)"},
 "U6":{"1":"VSYS","2":"GND","3":"VSYS","4":"unconnected-(U6-NC-Pad4)","5":"VCC_3V3"},
 "U7":{"1":"GND","2":"VSYS","3":"VDD_ADC"},}

G3 = "VCC_3V3"

# ---- placement: ref, footprint, x, y, rot ----
# NOTE: this KiCad build does not rotate pad shapes, so all footprints stay at
# rot 0 (orientation comes from the SMD_0805/_F/_V/_VR variants). J5 at rot 0
# already has pads on the interior (north) side, mouth facing the bottom edge.
FP = [
 # ===== corner M2 NPTH mounting holes =====
 ("MH1","MountingHole_M2_NPTH", 4, 4, 0),
 ("MH2","MountingHole_M2_NPTH", 86, 4, 0),
 ("MH3","MountingHole_M2_NPTH", 4, 51, 0),
 ("MH4","MountingHole_M2_NPTH", 86, 51, 0),
 # ===== zone A: analog front end, west column (x<27, quiet) =====
 # ch1: J2 south edge -> R1/R2 -> SIG pair north -> C5 diff -> U1.2/3
 ("J2","PinHeader_1x05_P2.54mm_H", 8, 51, 0),       # pads x8..18.16 @y51
 ("R2","SMD_0805_VR", 13.08, 48, 0),               # p1 S=LC_SIGN, p2 N=SIG_N
 ("R1","SMD_0805_VR", 15.62, 48, 0),               # p1 S=LC_SIGP, p2 N=SIG_P
 ("C11","SMD_0805_F", 11.0, 18.05, 0),              # p1 E=SIG_N (stub off C5.2), p2 W=GND
 ("C10","SMD_0805_F", 11.0, 19.95, 0),              # p1 E=SIG_P (stub off C5.1), p2 W=GND
 ("C5","SMD_0805_VR", 13.45, 19.0, 0),              # p1 S=SIG_P (19.95), p2 N=SIG_N (18.05)
 ("U1","SOIC-16_3.9x9.9mm_P1.27mm", 18, 22, 0),
 ("C4","SMD_0805_VR", 11.0, 23.3, 0),              # p1 S=VBG (24.25), p2 N=GND
 ("TP7","TestPoint_1.2mm", 11.0, 26.4, 0),
 ("C1","SMD_0805", 23.3, 18.825, 0),               # p1 W=VDD_ADC (22.35), p2 E=GND
 ("C2","SMD_0805", 23.3, 16.9, 0),                 # p1 W=AVDD (22.35), p2 E=GND
 ("C3","SMD_0805", 23.3, 15.3, 0),                 # p1 W=AVDD (22.35), p2 E=GND
 ("TP8","TestPoint_1.2mm", 20.6, 15.3, 0),
 ("U7","SOT-89-3", 22.5, 11.7, 0),                 # pads x20.55: p1 GND 10.2, p2 tab VSYS 11.7, p3 VDD_ADC 12.9
 ("C20","SMD_0805", 17.5, 12.8, 0),                # p1 W=VDD_ADC (16.55), p2 E=GND
 ("C22","SMD_0805", 26.9, 11.7, 0),                # p1 W=VSYS (25.95), p2 E=GND
 # ch2: J11 west edge -> R22/R23 -> SIG2 pair -> C27 diff -> U1.4/5
 ("J11","PinHeader_1x04_P2.54mm", 4, 30, 0),        # pads y30..37.62 @x4
 ("R22","SMD_0805", 7, 35.08, 0),                  # p1 W=LC_SIG2N, p2 E=SIG2_N
 ("R23","SMD_0805", 7, 37.62, 0),                  # p1 W=LC_SIG2P, p2 E=SIG2_P
 ("C27","SMD_0805_VR", 13.0, 22.7, 0),            # p1 S=SIG2_P (23.45), p2 N=SIG2_N (21.55)
 ("C28","SMD_0805", 9.4, 41.4, 0),                 # p1 W=SIG2_P (leg x8.8 pierces pad), p2 E=GND
 ("C29","SMD_0805", 10.7, 39.5, 0),                # p1 W=SIG2_N (leg x9.3 pierces pad), p2 E=GND
 ("U3","SOT-563_1.6x1.6mm_P0.5mm", 22, 31.5, 0),   # L: 1 SCL,2 GND,3 ALERT  R: 6 SDA,5 VDD_ADC,4 GND
 ("C6","SMD_0805_V", 20.2, 36.5, 0),               # p1 N=VDD_ADC (35.55), p2 S=GND
 ("C17","SMD_0805", 23.1, 35.6, 0),                # p1 W=VBAT_SENSE, p2 E=GND
 ("R13","SMD_0805", 23.1, 37.6, 0),                # p1 W=BAT, p2 E=VBAT_SENSE
 ("R14","SMD_0805", 23.1, 39.7, 0),                # p1 W=VBAT_SENSE, p2 E=GND
 # ===== NW test-point strip (moved out of the old SE routing wall) =====
 ("TP3","TestPoint_1.2mm", 6, 6.5, 0),             # EN
 ("TP4","TestPoint_1.2mm", 9, 6.5, 0),             # BOOT
 ("TP5","TestPoint_1.2mm", 12, 6.5, 0),            # TXD
 ("TP6","TestPoint_1.2mm", 15, 6.5, 0),            # RXD
 ("TP9","TestPoint_1.2mm", 6, 10, 0),              # VBUS
 ("TP10","TestPoint_1.2mm", 9, 10, 0),             # 3V3
 ("TP11","TestPoint_1.2mm", 12, 10, 0),            # GND
 ("TP12","TestPoint_1.2mm", 15, 10, 0),            # SDA
 ("TP13","TestPoint_1.2mm", 18, 10, 0),            # SCL
 # ===== zone C: ESP32-S3-WROOM-1-N16R8 module, top centre =====
 # pads: west col x36.25 y5.74..22.25, south row y23.5 x38.01..51.97,
 # east col x53.75 y22.25..5.74; antenna strip overhangs the north edge
 ("U4","ESP32-S3-WROOM-1", 45, 11, 0),
 ("C12","SMD_0805_F", 33.5, 7, 0),                  # p1 E=3V3 near U4.2, p2 W=GND
 ("C13","SMD_0805", 34, 26.5, 0),                   # p1 W=3V3, p2 E=GND (south of module)
 ("C25","SMD_0805", 59.5, 31.8, 0),                   # p1 W=3V3, p2 E=GND
 ("R5","SMD_0805", 33.5, 10.8, 0),                  # p1 W=3V3, p2 E=EN
 ("C14","SMD_0805_V", 33.5, 13.8, 0),               # p1 N=GND, p2 S=EN
 ("R6","SMD_0805", 58.5, 16, 0),                  # p1 W=BOOT (U4.27 IO0), p2 E=3V3
 # ===== NE: HMI connectors + button pullups + LCD_RST RC =====
 ("J7","JST_PH_B8B-PH-K_1x08_P2.00mm_Vertical", 64, 3.5, 0),   # pads x64..78
 ("J8","JST_PH_B4B-PH-K_1x04_P2.00mm_Vertical", 60, 10.2, 0),   # pads 60,62,64,66
 ("R21","SMD_0805", 77.5, 8.4, 0),                  # p1 W=LCD_RST, p2 E=3V3
 ("C26","SMD_0805", 73, 8.4, 0),                    # p1 W=LCD_RST, p2 E=GND
 ("R15","SMD_0805", 69.5, 11.8, 0),                   # p1 W=3V3, p2 E=BTN_TARE
 ("C18","SMD_0805", 66.2, 14.7, 0),                     # p1 W=BTN_TARE, p2 E=GND
 ("R16","SMD_0805", 73, 11.8, 0),                 # p1 W=3V3, p2 E=BTN_MODE
 ("C19","SMD_0805", 69.5, 14, 0),                   # p1 W=BTN_MODE, p2 E=GND
 # ===== mid: accel + I2C pullups + buzzer =====
 ("U2","LGA-12_2x2mm_P0.5mm_LIS2DW12", 32, 33, 0),
 ("C7","SMD_0805", 29, 29.5, 0),                    # p1 W=3V3, p2 E=GND
 ("C8","SMD_0805", 29, 36.5, 0),                    # p1 W=3V3, p2 E=GND
 ("R3","SMD_0805", 50, 28, 0),                      # p1 W=3V3, p2 E=SDA
 ("R4","SMD_0805", 50, 30.5, 0),                    # p1 W=3V3, p2 E=SCL
 ("BZ1","MagneticBuzzer_CUI_CMT-8540S-SMT", 62, 24, 0),
 ("D2","D_SOD-123", 69.5, 22, 0),                 # p1 K W=3V3, p2 A E=BUZZ_SW
 ("Q2","SOT-23", 55.5, 28, 0),                        # p1 G=Q2-G, p2 S=GND, p3 D=BUZZ_SW
 ("R17","SMD_0805", 52.0, 26.0, 0),                   # p1 W=BUZZ, p2 E=Q2 gate
 ("R18","SMD_0805", 54, 30.5, 0),                   # p1 W=Q2 gate, p2 E=GND
 # ===== zone B: power + charge, east (x58..86) =====
 ("U5","SOT-23-6", 64, 32, 0),                      # TP4057
 ("R10","SMD_0805", 66.5, 35, 0),                   # p1 W=PROG, p2 E=GND
 ("R11","SMD_0805", 60, 29.6, 0),                   # p1 W=3V3, p2 E=CHRG_STAT
 ("R12","SMD_0805", 64, 39, 0),                     # p1 W=VBUS, p2 E=GND
 ("C16","SMD_0805", 60, 36, 0),                     # p1 W=BAT, p2 E=GND
 ("C15","SMD_0805", 70.5, 43, 0),                   # p1 W=VBUS, p2 E=GND
 ("J6","JST_PH_B2B-PH-K_1x02_P2.00mm_Vertical", 62, 51, 0),
 ("D1","D_SOD-123", 66, 42, 0),                     # p1 K W=VSYS, p2 A E=VBUS
 ("Q1","SOT-23", 68, 38, 0),                        # p1 G=VBUS, p2 S=VSYS, p3 D=BAT
 ("U6","SOT-23-5", 74, 34, 0),                      # p1/p3 VSYS, p2 GND, p5 3V3
 ("C21","SMD_0805", 72, 37, 0),                     # p1 W=VSYS, p2 E=GND
 ("C9","SMD_0805", 76.5, 37, 0),                    # p1 W=3V3, p2 E=GND
 ("J5","USB_C_Receptacle_HRO_TYPE-C-31-M-12", 74, 50, 0),
 ("R8","SMD_0805_VR", 73.0, 42.3, 0),                # p1 S=CC1 (43.95), p2 N=GND
 ("R9","SMD_0805_VR", 75.75, 42.3, 0),                # p1 S=CC2 (43.95), p2 N=GND
 ("U8","SOT-23-6", 79, 40.5, 0),                    # USBLC6: 1 DM,2 GND,3 DP | 4 nc,5 VBUS,6 nc
 ("R20","SMD_0805_F", 60, 18.5, 0),                # p1 E=USB_DM (J5), p2 W=USB_DM_M (U4.13)
 ("R19","SMD_0805_F", 64.5, 18.5, 0),               # p1 E=USB_DP (J5), p2 W=USB_DP_M (U4.14)
 ("C24","SMD_0805_V", 56.5, 19, 0),                 # p1 N=USB_DM_M tap, p2 S=GND (DNP)
 ("C23","SMD_0805_V", 70.5, 26.8, 0),                 # p1 N=USB_DP_M tap, p2 S=GND (DNP)
 # ===== zone D: J10 expansion corridor, south centre =====
 ("J10","PinHeader_2x10_P2.54mm_Vertical", 34, 50, 0),   # row A y50, row B y52.54
 # 100R series R on the fast GPIOs; p1 N = MCU side (EXP_IOx), p2 S = header
 ("R24","SMD_0805_V", 34, 46.5, 0),                 # IO38
 ("R25","SMD_0805_V", 36.54, 46.5, 0),              # IO39
 ("R26","SMD_0805_V", 39.08, 46.5, 0),              # IO40
 ("R27","SMD_0805_V", 41.62, 46.5, 0),              # IO41
 ("R28","SMD_0805_V", 44.16, 46.5, 0),              # IO42
 ("R29","SMD_0805_V", 46.7, 46.5, 0),               # IO14
 ("R30","SMD_0805_V", 49.24, 46.5, 0),              # IO15
 ("R31","SMD_0805_V", 51.78, 46.5, 0),              # IO17
 ("R32","SMD_0805", 55.5, 46.8, 0),                 # p1 W=XIO15 tap, p2 E=GND
 ("D3","D_SOD-123", 30, 49.5, 0),                   # p1 K=VBUS, p2 A=EXP_5V (DNP)
]

# pads that need solid zone connect (pour can only reach them via 1 spoke)
SOLID_PADS = {"J5": {"A1", "A12", "B1", "B12", "SH"}, "U3": {"2", "4"}, "C4": {"2"},
              "U1": {"7", "8", "9"}, "U4": {"41"},   # EPAD: solid to GND pour (internal ground vias)
              "U2": {"3", "6", "7", "8"}, "R8": {"2"}, "R9": {"2"}}

TR = []
def tr(net, layer, *pts, w=0.25): TR.append((net, layer, list(pts), w))
VIA = []
def via(net, x, y, size=0.7, drill=0.35): VIA.append((net, x, y, size, drill))

# ==================== hand routing (locked; analog + local rails) ====================
# Topology: 4 ADC signals run south below the part row, east in dedicated
# corridors, then north in the x17.3..18.8 channel east of U1 pads (x<=16.1).
# Corridor order N->S matches channel order W->E; pin targets decrease E->W
# (SIG2_P 22.635, SIG2_N 21.365, SIG_P 20.095, SIG_N 18.825) so no crossings.
# --- LC_ stubs J2 -> R2/R1 ---
tr("LC_SIGN","F.Cu",(13.08,51),(13.08,48.95))
tr("LC_SIGP","F.Cu",(15.62,51),(15.62,48.95))
tr("LC_SIG2N","F.Cu",(4,35.08),(6.05,35.08))
tr("LC_SIG2P","F.Cu",(4,37.62),(6.05,37.62))
# --- ch1: SIG_N (R2 -> corridor y45.15, B.Cu hop under SIG_P leg -> ch x19.0)
tr("SIG_N","F.Cu",(13.08,47.05),(13.08,45.15),(14.8,45.15))
tr("SIG_N","B.Cu",(14.8,45.15),(17.3,45.15))
tr("SIG_N","F.Cu",(17.3,45.15),(19.0,45.15),(19.0,18.825),(15.3,18.825))
via("SIG_N",14.8,45.15)
via("SIG_N",17.3,45.15)
# SIG_P (R1 -> leg x16.5 -> corridor y44.4 -> ch x18.5)
tr("SIG_P","F.Cu",(15.62,47.05),(16.5,47.05),(16.5,44.4),(18.5,44.4),
   (18.5,20.095),(15.3,20.095))
# C5 diff-cap taps off U1.2/3 pad west edges; C10/C11 CM shunts off C5 W edges
tr("SIG_N","F.Cu",(14.5,18.825),(13.95,18.6))
tr("SIG_N","F.Cu",(12.95,18.05),(12.2,18.05))
tr("SIG_P","F.Cu",(14.5,20.095),(13.95,19.95))
tr("SIG_P","F.Cu",(12.95,19.95),(12.2,19.95))
# --- ch2: braid pair; one forced crossing taken as a B.Cu hop on the SIG2_P
# pad tap (between pad5 E edge x16.1 and SIG2_N channel x17.3).
# SIG2_N (R22 -> leg x9.3 pierces C29.1 -> corr y42.6 -> ch x17.3)
tr("SIG2_N","F.Cu",(7.95,35.08),(9.3,35.08),(9.3,42.6),
   (17.3,42.6),(17.3,21.365),(15.3,21.365))
# SIG2_P (R23 -> leg x8.8 pierces C28.1 -> corr y43.4 -> ch x17.9 -> B.Cu tap)
tr("SIG2_P","F.Cu",(7.95,37.62),(8.8,37.62),(8.8,43.4),
   (17.9,43.4),(17.9,22.635))
via("SIG2_P",17.9,22.635,0.5,0.3)
tr("SIG2_P","B.Cu",(17.9,22.635),(16.65,22.635))
via("SIG2_P",16.65,22.635,0.6,0.3)
tr("SIG2_P","F.Cu",(16.65,22.635),(15.3,22.635))
# C27 diff-cap taps off U1.4/5 pad west edges
tr("SIG2_N","F.Cu",(14.5,21.4),(13.4,21.8))
tr("SIG2_P","F.Cu",(14.5,22.7),(13.4,23.3))
# VBG: U1.6 W edge -> south duck under C27.1 -> C4.1 E edge ; C4.1 -> TP7
tr("VBG","F.Cu",(14.8,23.9),(14.0,23.9),(14.0,25.4),(11.0,25.4),(11.0,25.9))
tr("VBG","F.Cu",(11.0,24.7),(11.0,25.4))
# U1 GND: pin7 stub south across pin8 pad -> via ; pin9 stub east -> via
tr("GND","F.Cu",(15.3,25.175),(15.3,26.2),(14.0,26.2), w=0.4)
via("GND",14.0,26.2)
via("GND",21.0,26.445)   # U1.9 GND: via-in-pad (proto board, hand assembled)
# AVDD: U1.16 -> C2 -> C3 -> TP8 -> U1.1 ; E+ spine down west edge -> J2.1/J11.1
tr("AVDD","F.Cu",(20.7,17.555),(22.35,17.3),(22.35,15.3),(20.6,15.3),
   (20.0,15.3),(19.3,14.9),(14.5,14.9),(14.5,17.555),(15.3,17.555), w=0.4)
tr("AVDD","F.Cu",(14.5,14.9),(2.4,14.9),(2.4,48.8),(8,48.8),(8,51), w=0.6)
tr("AVDD","F.Cu",(2.4,30),(4,30), w=0.4)   # E+ branch to J11.1
# VDD_ADC: U7.3 -> C20 ; U7.3 -> C1 -> U1.15 ; ch x23.3 south -> U3.5 + C6.1
tr("VDD_ADC","F.Cu",(20.55,13.2),(20.55,14.1),(16.55,14.1),(16.55,12.8), w=0.4)
tr("VDD_ADC","F.Cu",(20.55,13.2),(21.4,14.1),(23.3,14.1),(23.3,18.825),
   (22.35,18.825),(20.7,18.825), w=0.4)
tr("VDD_ADC","F.Cu",(23.3,18.825),(23.3,34.5),(20.2,34.5),
   (20.2,35.55), w=0.4)                                              # -> C6.1
tr("VDD_ADC","F.Cu",(22.575,31.5),(23.3,31.5), w=0.25)               # U3.5 tap
# I2C to U1: SDA U3.6 -> U1.14 ; SCL U3.1 -> x21.9 squeeze -> U1.13
tr("SDA","F.Cu",(22.575,31.0),(22.575,20.095),(21.2,20.095))
tr("SCL","F.Cu",(21.425,31.0),(21.9,31.0),(21.9,21.365),(21.2,21.365))
# U1.12/13/14 east exits are walled by the signal channels: via-in-pad ->
# B.Cu stubs east through the keepout notch -> vias at x27.5 (router picks up)
for _y, _net in ((20.095, "SDA"), (21.365, "SCL"), (22.635, "DRDY")):
    via(_net, 20.7, _y, 0.5, 0.3)
    tr(_net, "B.Cu", (20.7, _y), (26.8, _y))
    via(_net, 26.8, _y)
# VSYS feed to U7 tab from C22
tr("VSYS","F.Cu",(25.95,11.7),(20.64,11.7), w=0.5)
# --- J10 row-A stubs: R24..R31 south pads -> header pins (short, locked) ---
for _i, _net in enumerate(["XIO38","XIO39","XIO40","XIO41","XIO42","XIO14",
                           "XIO15","XIO17"]):
    _x = 34 + _i * 2.54
    tr(_net,"F.Cu",(_x,47.45),(_x,50))
# R32 fail-safe pulldown tap on XIO15: J10.7 south -> between pad rows -> R32.1 W
tr("XIO15","F.Cu",(49.24,50.9),(49.24,51.27),(53.05,51.27),(53.05,46.8),
   (54.05,46.8))
tr("GND","F.Cu",(56.45,46.8),(57.4,46.8)); via("GND",57.4,46.8)   # R32.2
# EXP_5V: J10.11 west around the header to D3 anode (DNP injection path)
tr("EXP_5V","F.Cu",(34,52.54),(32,52.54),(32,49.5),(31.65,49.5), w=0.4)
# --- connector GND escapes (TH pads the pour cannot reach reliably) ---
tr("GND","F.Cu",(18.16,51),(18.16,52.6), w=0.5)     # J2.5 SHLD
via("GND",18.16,52.6)
tr("GND","F.Cu",(4,32.54),(5.5,32.54), w=0.5)      # J11.2 E-
via("GND",5.5,32.54)
tr("GND","F.Cu",(64,51),(64,52.6), w=0.5)          # J6.2
via("GND",64,52.6)
tr("GND","F.Cu",(60,8.5),(60,6.3), w=0.5)          # J8.1
via("GND",60,6.3)
tr("GND","F.Cu",(64,3.5),(64,1.8), w=0.5)          # J7.1
via("GND",60.5,1.5)
# J10 GND pins 13/14/15: chain on F.Cu + one via
tr("GND","F.Cu",(39.08,52.54),(39.08,53.8),(44.16,53.8),(44.16,52.54), w=0.4)
tr("GND","F.Cu",(41.62,52.54),(41.62,53.8), w=0.4)
via("GND",41.62,53.8)
# J10.12 VCC_3V3: stub south past row-B pads -> via (links to 3V3 pour/frags)
tr("VCC_3V3","F.Cu",(36.54,52.54),(36.54,53.9), w=0.4)
via("VCC_3V3",36.54,53.9)
# U3 GND: p2 west around ALERT pad -> via ; p4 south -> via
tr("GND","F.Cu",(21.425,31.5),(20.8,31.5),(20.5,32.1))
via("GND",20.5,32.1)
tr("GND","F.Cu",(22.575,32.0),(22.3,32.0),(22.3,33.6))
via("GND",22.3,33.6)
tr("BUZZ_SW","F.Cu",(71.15,22),(71.15,24.5),(66.6,24.5))  # D2.2 -> BZ1.2
# GND stubs for SMD pads that only see the pour through a via
tr("GND","F.Cu",(11.65,39.5),(12.3,39.5));  via("GND",12.3,39.5)   # C29.2
tr("GND","F.Cu",(10.35,41.4),(11.5,41.4));  via("GND",11.5,41.4)   # C28.2
tr("GND","F.Cu",(10.05,18.05),(9.3,18.05),(9.3,19.95),(10.05,19.95))  # C10/11.2
via("GND",9.3,19.0)
tr("GND","F.Cu",(10.5,22.35),(9.9,22.35));  via("GND",9.9,22.35)   # C4.2
tr("GND","F.Cu",(20.2,37.45),(20.2,39.2));  via("GND",20.2,39.2)   # C6.2
tr("GND","F.Cu",(24.05,35.6),(24.9,35.6));  via("GND",24.9,35.6)   # C17.2
tr("GND","F.Cu",(24.05,39.7),(24.9,39.7));  via("GND",24.9,39.7)   # R14.2
# GND stitching vias in the analog block (F.Cu islands -> solid B.Cu)
for xy in [(16.9,16.5),(18.0,8.0),(4.0,10.0),(6.0,22.0),(5.0,42.0),
           (13.8,29.3),(12.4,29.6),(21.4,33.4),(28.0,20.0),(24.0,41.2),
           (62.5,29.5),(70.0,30.0),(80.0,45.0),(35.0,44.0),
           (44.0,25.0),(47.5,27.8),(49.0,9.5),(49.0,26.0)]:
    via("GND",*xy)


# ---------- footprint embedding ----------
SILK = {   # ref -> (dx, dy, rot) local offset for Reference text, or "hide"
 "J2": (12.9, 0, 0), "J5": (7.6, 1.4, 0), "J6": "hide", "J7": "hide", "J8": "hide",
 "U1": (-0.5, -5.7, 0), "U2": (0, 2.2, 0), "U3": (0, 2.0, 0), "U4": "hide",
 "U5": (-3.4, 0, 0), "U6": (0, 2.5, 0), "U7": (-4.2, -0.3, 0), "U8": (0, -2.7, 0),
 "Q1": (1, -2.7, 0), "Q2": (0, -2.4, 0), "D1": (3, 0, 0), "D2": (-3.6, 0, 0),
 "BZ1": (0, -4.2, 0),
 "R8": (-2.3, 0, 0), "R9": "hide",
 "R1": "hide", "R2": "hide", "R3": "hide", "R4": "hide", "R5": "hide",
 "R6": "hide", "R7": "hide", "R10": "hide", "R11": "hide", "R12": "hide",
 "R13": "hide", "R14": "hide", "R15": "hide", "R16": "hide", "R17": "hide",
 "R18": "hide", "R19": "hide", "R20": "hide", "R21": "hide",
 "R22": "hide", "R23": "hide", "R24": "hide", "R25": "hide", "R26": "hide",
 "R27": "hide", "R28": "hide", "R29": "hide", "R30": "hide", "R31": "hide",
 "R32": "hide",
 "MH1": "hide", "MH2": "hide", "MH3": "hide", "MH4": "hide",
 "C1": "hide", "C2": "hide", "C3": "hide", "C4": "hide", "C5": "hide",
 "C6": "hide", "C7": "hide", "C8": "hide", "C9": "hide", "C10": "hide",
 "C11": "hide", "C12": "hide", "C13": "hide", "C14": "hide", "C15": "hide",
 "C16": "hide", "C17": "hide", "C18": "hide", "C19": "hide", "C20": "hide",
 "C21": "hide", "C22": "hide", "C23": "hide", "C24": "hide", "C25": "hide",
 "C26": "hide", "C27": "hide", "C28": "hide", "C29": "hide",
 "J10": (0, -3.0, 0), "J11": (0, -3.2, 0), "D3": "hide",
 "TP3": "hide", "TP4": "hide", "TP5": "hide", "TP6": "hide", "TP7": "hide",
 "TP8": "hide", "TP9": "hide", "TP10": "hide", "TP11": "hide", "TP12": "hide",
 "TP13": "hide",
}
def embed(ref, fname, x, y, rot, padnets):
    src = open(os.path.join(PRETTY, fname + ".kicad_mod")).read()
    src = re.sub(r'\n\t\((?:version|generator|generator_version)\s[^\n]*', '', src)
    src = src.replace('(at 0 0 0)\n\t\t(descr', '(descr', 1)  # drop lib-level origin
    src = src.replace(f'(footprint "{fname}"', f'(footprint "s3-1:{fname}"', 1)
    if ref in SILK:
        m = re.search(r'(\(property "Reference" "[^"]*"\s*\n\t+\(at )[-\d. ]+?0?\)', src)
        if SILK[ref] == "hide":
            src = src[:m.start()] + m.group(1) + '0 0 0)\n\t\t(hide yes)' + src[m.end():]
        else:
            dx, dy, dr = SILK[ref]
            src = src[:m.start()] + m.group(1) + f'{dx} {dy} {dr})' + src[m.end():]
    src = re.sub(r'(\(property "Reference" )"[^"]*"', rf'\1"{ref}"', src, count=1)
    src = re.sub(r'(\(property "Value" )"[^"]*"', rf'\1"{VAL[ref]}"', src, count=1)
    # Description/Datasheet: replace if present, else inject so board matches schematic
    def set_prop(src, name, val):
        if f'(property "{name}"' in src:
            return re.sub(rf'(\(property "{name}" )"[^"]*"', rf'\1"{val}"', src, count=1)
        return src.replace(f'(property "Value"',
            f'(property "{name}" "{val}"\n\t\t(at 0 0 0)\n\t\t(layer "F.Fab")\n\t\t(hide yes)\n\t)\n\t(property "Value"', 1)
    src = set_prop(src, "Description", DESC[ref])
    if ref in DS:
        src = set_prop(src, "Datasheet", DS[ref])
    if ref in DNP:
        src = re.sub(r'\(attr (smd|through_hole)\)(?!.*dnp)',
                     r'(attr \1 dnp exclude_from_bom)', src, count=1)
    solid = SOLID_PADS.get(ref, set())
    def fix_pad(m):
        blk = m.group(0)
        num = re.search(r'\(pad "([^"]*)"', blk).group(1)
        net = padnets.get(num)
        core = blk.rstrip('\n\t ')
        core = core[:-1].rstrip('\n\t ')
        if net:
            core += f'\n\t\t(net "{net}")'
        if num in solid:
            core += '\n\t\t(zone_connect 2)'
        core += f'\n\t\t(uuid "{uid()}")'
        return core + '\n\t)'
    body = re.sub(r'(?m)^([ \t]+)\(pad [^\n]*\n(?:[^\n]*\n)*?^\1\)[ \t]*$',
                  fix_pad, src)
    def add_uuid(m):
        blk = m.group(0)
        if '(uuid' in blk: return blk
        core = blk.rstrip('\n\t ')
        core = core[:-1].rstrip('\n\t ')
        return core + f'\n\t\t(uuid "{uid()}")\n\t)'
    body = re.sub(r'\((fp_line|fp_arc|fp_circle|fp_rect|fp_poly)\s[^)]*\n(?:[^\n]+\n)*?\t\)', add_uuid, body)
    body = re.sub(r'\(property [^\n]+\n(?:[^\n]+\n)*?\n\t\)', add_uuid, body)
    at_blk = (f'(layer "F.Cu")\n\t\t(uuid "{uid()}")\n\t\t(at {x} {y}'
              + (f' {rot}' if rot else '') + ')')
    if ref in SYM:
        at_blk += f'\n\t\t(path "/{SHEET}/{SYM[ref]}")'
    body = body.replace('(layer "F.Cu")', at_blk, 1)
    return body

with open(os.path.join(DIR, "s3.1.kicad_pcb"), "w") as f:
    w = f.write
    w('''(kicad_pcb
\t(version 20260206)
\t(generator "pcbnew")
\t(generator_version "10.0")
\t(general
\t\t(thickness 1.6)
\t\t(legacy_teardrops no)
\t)
\t(paper "A4")
\t(title_block
\t\t(title "coffee control board s3.1 (ESP32-S3 + NAU7802 x2ch + J10 expansion)")
\t\t(date "2026-09-17")
\t\t(rev "s3.1")
\t\t(company "coffee-scale")
\t)
\t(layers
\t\t(0 "F.Cu" signal)
\t\t(2 "B.Cu" signal)
\t\t(9 "F.Adhes" user "F.Adhesive")
\t\t(11 "B.Adhes" user "B.Adhesive")
\t\t(13 "F.Paste" user)
\t\t(15 "B.Paste" user)
\t\t(5 "F.SilkS" user "F.Silkscreen")
\t\t(7 "B.SilkS" user "B.Silkscreen")
\t\t(1 "F.Mask" user)
\t\t(3 "B.Mask" user)
\t\t(17 "Dwgs.User" user "User.Drawings")
\t\t(19 "Cmts.User" user "User.Comments")
\t\t(21 "Eco1.User" user "User.Eco1")
\t\t(23 "Eco2.User" user "User.Eco2")
\t\t(25 "Edge.Cuts" user)
\t\t(27 "Margin" user)
\t\t(31 "F.CrtYd" user "F.Courtyard")
\t\t(29 "B.CrtYd" user "B.Courtyard")
\t\t(35 "F.Fab" user)
\t\t(33 "B.Fab" user)
\t)
\t(setup
\t\t(pad_to_mask_clearance 0)
\t\t(allow_soldermask_bridges_in_footprints yes)
\t\t(tenting
\t\t\t(front yes)
\t\t\t(back yes)
\t\t)
\t\t(covering
\t\t\t(front no)
\t\t\t(back no)
\t\t)
\t\t(plugging
\t\t\t(front no)
\t\t\t(back no)
\t\t)
\t\t(capping no)
\t\t(filling no)
\t\t(pcbplotparams
\t\t\t(layerselection 0x00000000_00000000_55555555_5755f5ff)
\t\t\t(plot_on_all_layers_selection 0x00000000_00000000_00000000_00000000)
\t\t\t(disableapertmacros no)
\t\t\t(usegerberextensions no)
\t\t\t(usegerberattributes yes)
\t\t\t(usegerberadvancedattributes yes)
\t\t\t(creategerberjobfile yes)
\t\t\t(dashed_line_dash_ratio 12)
\t\t\t(dashed_line_gap_ratio 3)
\t\t\t(svgprecision 4)
\t\t\t(plotframeref no)
\t\t\t(mode 1)
\t\t\t(useauxorigin no)
\t\t\t(pdf_front_fp_property_popups yes)
\t\t\t(pdf_back_fp_property_popups yes)
\t\t\t(pdf_metadata yes)
\t\t\t(pdf_single_document no)
\t\t\t(dxfpolygonmode yes)
\t\t\t(dxfimperialunits yes)
\t\t\t(dxfusepcbnewfont yes)
\t\t\t(psnegative no)
\t\t\t(psa4output no)
\t\t\t(plot_black_and_white yes)
\t\t\t(sketchpadsonfab no)
\t\t\t(plotpadnumbers no)
\t\t\t(hidednponfab no)
\t\t\t(sketchdnponfab yes)
\t\t\t(crossoutdnponfab yes)
\t\t\t(subtractmaskfromsilk no)
\t\t\t(outputformat 1)
\t\t\t(mirror no)
\t\t\t(drillshape 1)
\t\t\t(scaleselection 1)
\t\t\t(outputdirectory "")
\t\t)
\t)
\t(net_class "Default" "Default net class"
\t\t(clearance 0.2)
\t\t(trace_width 0.25)
\t\t(via_dia 0.7)
\t\t(via_drill 0.35)
\t\t(uvia_dia 0.3)
\t\t(uvia_drill 0.1)
\t\t(diff_pair_width 0.25)
\t\t(diff_pair_gap 0.25)
\t)
''')
    for ref, fname, x, y, rot in FP:
        w('\t' + embed(ref, fname, x, y, rot, NETS[ref]).replace('\n', '\n\t').rstrip() + '\n')
    w(f'''\t(gr_rect
\t\t(start 0 0)
\t\t(end 90 55)
\t\t(stroke
\t\t\t(width 0.2)
\t\t\t(type solid)
\t\t)
\t\t(fill no)
\t\t(layer "Edge.Cuts")
\t\t(uuid "{uid()}")
\t)
''')
    def gtext(txt, x, y, size=1.0, th=0.15, layer="F.SilkS"):
        w(f'''\t(gr_text "{txt}"
\t\t(at {x} {y})
\t\t(layer "{layer}")
\t\t(uuid "{uid()}")
\t\t(effects
\t\t\t(font
\t\t\t\t(size {size} {size})
\t\t\t\t(thickness {th})
\t\t\t)
\t\t)
\t)
''')
    gtext("COFFEE-CONTROL-S3.1", 30, 2.5, 1.0)
    gtext("J2: 1=E+ 2=E- 3=S- 4=S+ 5=SHLD", 9, 47.5, 0.8, 0.12)
    gtext("J11: 1=E+ 2=E- 3=S2- 4=S2+", 7, 28.5, 0.8, 0.12)
    gtext("J7 1=GND 2=3V3 3=SCK 4=MOSI", 65, 6.5, 0.8, 0.12)
    gtext("J7 5=CS 6=DC 7=RST 8=BL", 65, 7.7, 0.8, 0.12)
    gtext("J8: 1=GND 2=TARE 3=MODE 4=3V3", 61, 11.2, 0.8, 0.12)
    gtext("J6: 1=BAT+ 2=GND", 59, 48.5, 0.8, 0.12)
    gtext("J10 A: GPIO  B: PWR/I2C/UART", 36, 43.5, 0.8, 0.12)
    gtext("EN", 6, 8.2, 0.7, 0.12)
    gtext("BOOT", 9, 8.2, 0.7, 0.12)
    gtext("TXD", 12, 8.2, 0.7, 0.12)
    gtext("RXD", 15, 8.2, 0.7, 0.12)
    gtext("VBUS", 6, 11.7, 0.7, 0.12)
    gtext("3V3", 9, 11.7, 0.7, 0.12)
    gtext("GND", 12, 11.7, 0.7, 0.12)
    gtext("SDA", 15, 11.7, 0.7, 0.12)
    gtext("SCL", 18, 11.7, 0.7, 0.12)
    for net, layer, pts, wid in TR:
        for a, b in zip(pts, pts[1:]):
            w(f'''\t(segment
\t\t(start {a[0]} {a[1]})
\t\t(end {b[0]} {b[1]})
\t\t(width {wid})
\t\t(locked yes)
\t\t(layer "{layer}")
\t\t(net "{net}")
\t\t(uuid "{uid()}")
\t)
''')
    for net, x, y, vsz, vdr in VIA:
        w(f'''\t(via
\t\t(at {x} {y})
\t\t(size {vsz})
\t\t(drill {vdr})
\t\t(locked yes)
\t\t(layers "F.Cu" "B.Cu")
\t\t(net "{net}")
\t\t(uuid "{uid()}")
\t)
''')
    for layer in ("F.Cu", "B.Cu"):
        w(f'''\t(zone
\t\t(net "GND")
\t\t(layer "{layer}")
\t\t(uuid "{uid()}")
\t\t(hatch edge 0.5)
\t\t(connect_pads
\t\t\t(clearance 0.3)
\t\t)
\t\t(min_thickness 0.25)
\t\t(fill yes
\t\t\t(thermal_gap 0.3)
\t\t\t(thermal_bridge_width 0.3)
\t\t\t(island_removal_mode 0)
\t\t)
\t\t(polygon
\t\t\t(pts
\t\t\t\t(xy 0.4 0.4) (xy 89.6 0.4) (xy 89.6 54.6) (xy 0.4 54.6)
\t\t\t)
\t\t)
\t)
''')
    # B.Cu track keepout under the analog front end (solid GND return there)
    w(f'''\t(zone
\t\t(net 0)
\t\t(net_name "")
\t\t(layer "B.Cu")
\t\t(uuid "{uid()}")
\t\t(name "analog B.Cu no-track")
\t\t(hatch edge 0.508)
\t\t(connect_pads
\t\t\t(clearance 0)
\t\t)
\t\t(min_thickness 0.254)
\t\t(keepout
\t\t\t(tracks not_allowed)
\t\t\t(vias allowed)
\t\t\t(pads allowed)
\t\t\t(copperpour allowed)
\t\t\t(footprints allowed)
\t\t)
\t\t(fill
\t\t\t(thermal_gap 0.508)
\t\t\t(thermal_bridge_width 0.508)
\t\t\t(island_removal_mode 0)
\t\t)
\t\t(polygon
\t\t\t(pts
\t\t\t\t(xy 0 24) (xy 18 24) (xy 18 44) (xy 14 44) (xy 14 54) (xy 0 54) (xy 0 40.4) (xy 11.4 40.4) (xy 11.4 37.5) (xy 0 37.5)
\t\t\t)
\t\t)
\t)
''')
    # board-level antenna keepout: S3 module antenna strip overhangs the top
    # edge; no copper/tracks/vias/pads under the on-board part (x34..56, y0..5)
    w(f'''\t(zone
\t\t(net 0)
\t\t(net_name "")
\t\t(layers "F.Cu" "B.Cu")
\t\t(uuid "{uid()}")
\t\t(name "antenna keepout")
\t\t(hatch edge 0.508)
\t\t(connect_pads
\t\t\t(clearance 0)
\t\t)
\t\t(min_thickness 0.254)
\t\t(keepout
\t\t\t(tracks not_allowed)
\t\t\t(vias not_allowed)
\t\t\t(pads not_allowed)
\t\t\t(copperpour not_allowed)
\t\t\t(footprints allowed)
\t\t)
\t\t(fill
\t\t\t(thermal_gap 0.508)
\t\t\t(thermal_bridge_width 0.508)
\t\t\t(island_removal_mode 0)
\t\t)
\t\t(polygon
\t\t\t(pts
\t\t\t\t(xy 34.0 0) (xy 56.0 0) (xy 56.0 5.0) (xy 34.0 5.0)
\t\t\t)
\t\t)
\t)
''')
    w(')\n')
print("written")
