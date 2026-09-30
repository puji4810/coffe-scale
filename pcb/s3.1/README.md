# s3.1 — 双用途控制板 (rev s3.1)

一块 ESP32-S3 + NAU7802 主控板，同时服务两个产品形态：

1. **独立咖啡秤**——沿用 scale-adc-s3 的全部功能：称重前端、SPI LCD、
   双按键、蜂鸣器、USB-C 供电 / 单节锂电充放电、温度与倾斜传感。
2. **自动手冲机控制器**——同一块板通过 J10 扩展口驱动独立的
   电源/电机板（水泵、加热、流量/温度传感、喷嘴运动）。高压与功率
   驱动电路留在功率板上，本板只做控制。

> 本目录是 `pcb/scale-adc-s3/` 的改版。与 rev C 的差异：
> 90×55 规矩板框、MCU 居中、四角 M2 安装孔；新增 J10 2×10 南缘扩展口
> 与 J11 1×4 称重通道 2（VIN2）接口；NAU7802 VIN2 不再接地；
> 原 J9 扩展尝试在本版落地为 J10；测试点列移至板南侧。

## 目标

- 130×30×22 mm 单点称重传感器（ZEMIC L6D-C3-3kg 或兼容），量程 2–3 kg
- 稳定显示 0.1 g；探索 0.01 g 显示/低量程模式
- TMP102 测传感器附近温度做漂移分析；LIS2DW12 做水平/振动/敲击检测
- **第二称重通道**：NAU7802 ch2 (VIN2) 经 J11 外接第二传感器/PT1000，
  咖啡秤形态可不贴，机器形态接流量或第二称重
- **J10 扩展口**：8× GPIO（100R 串阻）+ 2× 直连 GPIO + 电源/总线排，
  对接机器电源/驱动板；R32 10k 下拉保证 XIO15（HEATER_EN 语义）
  默认安全态——裸板/未插扩展时加热线永不会被置位

## 系统结构

```
USB-C J5 ──► U8 ESD ──► R19/R20 22R ──► U4 IO19/IO20 (native USB-Serial/JTAG)
   │VBUS
   ├─► U5 TP4057 充电 ──► J6 1S Li-ion
   └─► D1/Q1 负载切换 ──► VSYS ──► U6 XC6220 3V3 1A ──► VCC_3V3 ──► U4/外设
                                  └──► U7 HT7533 ──► VDD_ADC ──► U1/U3 (安静电源轨)

L6D 3kg ──J2──► R1/R2 1k + C5/C10/C11 EMI 滤波 ──► U1 NAU7802 ch1 (I2C 0x2A)
J11 ch2 ──► C27 100n 差模电容 ──► U1 VIN2 (不再接地)
U2 LIS2DW12 (0x18)   U3 TMP102 (0x48)   共享 I2C: SDA=IO10, SCL=IO11
J7 SPI LCD          J8 按键 (TARE/MODE)          BZ1 蜂鸣器 (Q2 2N7002 驱动)
J10 2×10 ──► 机器电源/驱动板（泵/加热/流量/喷嘴）
```

## 接口

- **J2 → 称重传感器 ch1**：1=E+ (AVDD 激励), 2=E- (GND), 3=S-, 4=S+, 5=SHLD(GND)
- **J5 → USB-C**：USB 2.0，VBUS 充电/供电
- **J6 → 电池**：1=BAT+, 2=GND（JST PH 2.0）
- **J7 → LCD**：1=GND, 2=3V3, 3=SCK, 4=MOSI, 5=CS, 6=DC, 7=RST(局部 RC 上电复位), 8=BL
- **J8 → 按键/触摸**：1=GND, 2=TARE, 3=MODE, 4=3V3（4P JST PH；供 TTP223 触摸模块，老 3P 按键线仍兼容）
- **J11 → 称重通道 2 / VIN2**：1=E+, 2=E-, 3=S2-, 4=S2+；
  rev C 把 VIN2 接地作废，本版经 C27 100n 差模电容后直连 NAU7802 ch2，
  可接第二称重传感器或 PT1000（比率测量）
- **J10 → 机器电源/驱动板**（2×10 PTH，南缘居中）：

  | pin | 信号 | pin | 信号 |
  |---|---|---|---|
  | 1 | XIO38 (经 R24 100R) | 11 | EXP_5V |
  | 2 | XIO39 (R25) | 12 | 3V3 |
  | 3 | XIO40 (R26) | 13–15 | GND |
  | 4 | XIO41 (R27) | 16 | SDA |
  | 5 | XIO42 (R28) | 17 | SCL |
  | 6 | XIO14 (R29) | 18 | TXD (UART0) |
  | 7 | XIO15 (R30, R32 10k 下拉) | 19 | RXD (UART0) |
  | 8 | XIO17 (R31) | 20 | EN |
  | 9 | XIO1 (直连) | | |
  | 10 | XIO47 (直连) | | |

  A 排（1–10）= GPIO，其中 pin1–8 各经 100R 串阻限流/保护；
  B 排（11–20）= 电源 + 总线。pin20 EN 可让功率板复位主控；
  pin18/19 是 UART0，兼作烧录/调试兜底。

- **D3 (DNP) → EXP_5V→VBUS 注入**：功率板 5V 反灌选项。贴 B5819W
  = 二极管 OR（防 USB 反灌，~0.3 V 压降仍可经 TP4057 充电）；
  或 0R/飞线直通。纯秤 BOM 永不贴。

## U4 (ESP32-S3-WROOM-1-N16R8) 引脚映射

| 信号 | GPIO | 备注 |
|---|---|---|
| LCD_SCK/MOSI/CS/DC | IO4/IO5/IO6/IO7 | SPI |
| CHRG_STAT | IO8 | TP4057 开漏充电指示 |
| VBAT_SENSE | IO9 | R13/R14 100k 分压 |
| SDA / SCL | IO10 / IO11 | I2C 总线，并上 J10.16/17 |
| DRDY | IO12 | NAU7802 数据就绪 |
| INT1 | IO13 | LIS2DW12 中断（唤醒/敲击/就绪） |
| EXP_IO14 / EXP_IO15 | IO14 / IO15 | J10 pin6/pin7（经 R29/R30）；IO15 原 LCD_RST 改局部 RC POR 后释放 |
| LCD_BL | IO16 | |
| EXP_IO17 | IO17 | J10 pin8（经 R31） |
| BTN_MODE | IO18 | |
| USB D-/D+ | IO19 / IO20 | 原生 USB，经 R19/R20 22R + U8 ESD |
| BUZZ | IO21 | Q2 低侧开关 + D2 续流 |
| EXP_IO38–42 | IO38–IO42 | J10 pin1–5（经 R24–R28） |
| EXP_IO47 | IO47 | J10 pin10 直连 |
| EXP_IO1 | IO1 | J10 pin9 直连（原 UART1 候选脚，strapping 无关） |
| BTN_TARE | IO48 | 非 strapping 脚（rev C.1 从 IO46 改来） |
| UART0 TXD/RXD | TXD0 / RXD0 | TP5/TP6 + J10.18/19 |
| BOOT | IO0 | R6 10k 上拉；TP4 短接 GND = 下载模式 |
| EN | EN | R5 10k + C14 1u RC；TP3 复位 + J10.20 |
| NC | IO2/IO3, IO45/46 | IO35–37 被八线 PSRAM 占用；IO45/46 strapping 脚留空 |

## 关键设计决策

- **NAU7802 外部 AVDD = VDD_ADC 3.3 V**：AVDD_LDO/REFP/DVDD 与 ch1/ch2
  传感器激励 E+ 全部挂 U7 HT7533 安静轨，比率测量中激励波动抵消。
  固件必须保持 **AVDDS=0**（AVDD 引脚作电源输入，内部 LDO 关闭、
  VLDO 无效）。AVDD 上电即有 ~3.3 V，空板可直接量到（TP8）。
- **VIN2 解地 + J11**：rev C 把 NAU7802 ch2 输入接地作废；本版经
  C27 差模电容引出到 J11 1×4（E+/E-/S2-/S2+）。机器形态接第二称重
  或 PT1000；纯秤 BOM 可不贴 J11/C27，固件忽略 ch2。
- **J10 扩展口 = 唯一新增对外接口**：机器形态下功率板只做
  电源分配与功率驱动（泵/加热/电机驱动），所有控制、传感 I2C、
  UART、GPIO 都经 J10 进出。100R 串阻在排针侧限流保护 IO；
  R32 下拉确保 HEATER_EN 语义的 XIO15 默认低。
- **EXP_5V 供电选项（DNP）**：机器形态下功率板可经 D3 向 VBUS 侧
  反灌 5V，省掉 USB 供电线；纯秤不贴。
- **规矩板框 + 居中 MCU**：90×55 mm，MH1–MH4 M2 NPTH 四角安装孔
  (4,4)/(86,4)/(4,51)/(86,51)；U4 模组顶部居中（天线伸北缘外），
  模拟簇留西北，电源簇东南，J10 南缘居中——后续加东西走线都规整。
- **模拟岛**：西北角模拟 GND 经缝合过孔单点接主地，两层铺铜
  island_removal 开启；模拟 keepout 只挡 tracks 不挡 vias，
  北界 y24（只护 U1/U3/信号列簇）。
- **LCD_RST 局部 RC 上电复位**：沿用 rev C 方案（R21+C26 在 J7 旁）。
- **主 LDO 热预算**：U6 XC6220 1 A 指标仅指电流能力；Wi-Fi+背光+
  蜂鸣器全开须实测温升。机器形态下若贴 D3 走 EXP_5V，热预算重算。
- **电池**：J6 直挂 VSYS，无独立过放/过流切断——必须使用带保护板
  的 1S 电池包；机器形态可不贴电池，由 EXP_5V/USB 供电。

## BOM 变体

| 位号 | 纯秤 BOM | 机器 BOM |
|---|---|---|
| J10, R24–R32, D3 | J10+R24–R31 可贴可省（调试用）；R32 贴（安全下拉）；D3 不贴 | 全贴，D3 按供电方案选 B5819W 或 0R |
| J11, C27 | 可不贴（ch2 不用） | 贴（第二称重/PT1000） |
| J6, 电池 | 贴 | 可不贴 |
| J7/J8/BZ1/LCD | 贴 | 按需（机器形态可用板载 LCD 做状态屏，也可不贴走 J10 远程显示） |

## 文件

- `s3.1.kicad_sch` / `s3.1.kicad_pcb` — KiCad 10 工程
- `s3.1.pretty/` — 项目内自包含封装库（含 PinHeader_2x10_P2.54mm_Vertical 等新封装）
- `tools/gen_sch.py`, `tools/gen_pcb.py`, `tools/gen_cpl.py` — 生成脚本
- `tools/autoroute.py` — FreeRouting 自动布线（DSN/SES 往返 + 合并 +
  relock + DRC/rip-up 循环）
- `tools/route_check.py` — 候选走线几何校验器（候选路径落地前必过）
- `tools/net_islands.py`, `tools/region_dump.py` — 连通性/区域铜皮审计
- `s3.1-erc.txt` / `s3.1-drc.txt` — 验证报告

## 验证（kicad-cli 10.0）

```
kicad-cli sch erc --format report --output s3.1-erc.txt \
    --severity-all s3.1.kicad_sch
kicad-cli pcb drc --format report --output s3.1-drc.txt \
    --severity-all --refill-zones s3.1.kicad_pcb
```

布线流程：`gen_pcb.py` 只布模拟区与关键电源的手布走线并打 `(locked yes)`；
其余交 `autoroute.py`（FreeRouting）全局布线，手工修补一律先过
`route_check.py` 校验再落地并加锁。

当前状态见 `s3.1-drc.txt`；布线收敛过程见 git 历史与 `tools/fix_routes*.py`。

## Bring-up 顺序

1. 断电先量 3V3/GND、VBUS/GND、BAT/GND 无短路；上电后量 VBUS、VSYS、
   VCC_3V3、VDD_ADC。AVDD 由外部 VDD_ADC 供电（TP8），上电即应量到
   ~3.3 V，不再依赖固件使能 NAU7802 内部 LDO。
2. USB 插电脑：原生 USB-Serial/JTAG 应直接枚举；否则 TP4 短 GND +
   TP3 复位进下载模式，用 UART0（TP5/TP6 或 J10.18/19）兜底烧录。
3. I2C 扫描应见 0x18 / 0x2A / 0x48；J10.16/17 与板内 I2C 同总线，
   机器侧挂传感时注意总线负载与上拉。
4. 挂 ch1 传感器采 10/80 SPS 记录噪声/零点/重复性/温漂；
   贴 J11 时同样方法验证 ch2。
5. LIS2DW12 验证 INT1；TMP102 与传感器温漂对比。
6. 对比 LCD/背光/蜂鸣器/Wi-Fi 与 USB/电池/EXP_5V 供电下的称重读数。
7. 机器形态：先不接功率板验证 XIO* 逻辑电平与 HEATER_EN 默认低
   （R32 下拉），再对接功率板做泵/加热的低压联调，最后上高压。
