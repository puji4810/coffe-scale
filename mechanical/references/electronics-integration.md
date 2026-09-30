# 电子件集成依据（沿用 PCB 只读快照）

## 屏幕选择与接入

推荐先采用 **Waveshare 1.47 inch LCD Module（非触摸版），ST7789V3**：172 × 320，横屏后 320 × 172；模块 38.5 × 22 mm，有效显示约 32.35 × 17.39 mm，适合重量和时间。模型的 5 mm 模块厚度是占位，插头和弯线空间还需实物确认。[厂家产品页](https://www.waveshare.com/1.47inch-LCD-Module.htm)

如果更重视大字，可改 **1.9 inch ST7789V2**：横放模块 51.2 × 27.3 mm，有效显示约 42.72 × 22.70 mm。建议将前显示区扩到约 33–35 mm，或改成斜面；当前前部可见区深约 27 mm，不直接兼容其安装余量。[厂家产品页](https://www.waveshare.com/product/displays/lcd-oled/1.9inch-lcd-module.htm)

当前 PCB J7 是 JST PH 2.0 的 8 针 SPI 接口，**按信号逐根连接，不假定同为 8 针就可直插**：

| J7 针号 | PCB 网络 | ESP32 GPIO | 屏幕信号 |
|---|---|---|---|
| 1 | GND | — | GND |
| 2 | VCC_3V3 | — | VCC（选择支持 3.3 V 供电的模块） |
| 3 | LCD_SCK | 4 | CLK / SCK |
| 4 | LCD_MOSI | 5 | DIN / MOSI |
| 5 | LCD_CS | 6 | CS |
| 6 | LCD_DC | 7 | DC |
| 7 | LCD_RST | 15 | RST |
| 8 | LCD_BL | 16 | BL 逻辑控制输入 |

J7.8 直接连接 GPIO16，主板没有为这个引脚提供功率背光驱动。选带背光驱动且 BL 为逻辑输入的成品模块；如果买到的是裸 LED 背光端，需要外加限流/驱动，不能从 GPIO 直接供电。模块实际电流、3V3 供电余量与固件初始化还需联调。J7 没有引出触摸所需的 I²C 或中断线；当前预留覆膜/膜片按键组件接 J8，机构尚未选定。

## PCB 与电池的装配边界

只读快照见 [references/pcb-snapshot.json](pcb-snapshot.json)，包含原文件 SHA-256、捕获时间、Edge.Cuts、关键器件二维位置及接口网络。默认构建使用快照，因此另一个 Agent 修改 PCB 时，本模型不会悄悄随之漂移。

坐标约定：X 向右，Y 向后，Z 向上，底架下表面 Z=0。PCB 原生 KiCad 坐标转换为 `X=65-x, Y=-45+y`，板底 Z=11；安装后的板边为 X=-10..65、Y=-45..-5。

- **没有在 PCB 占位件上虚构四个孔。** 当前四个 M3 立柱固定的是可拆卸打印托盘，中心为 `(-28,-47)、(-28,-6)、(70,-47)、(70,-6)`，均在主板外侧。
- 托盘有暂定 1 mm 板边支承，必须检查 PCB 背面焊盘、THT 焊脚和板边器件后调整；压片/卡扣尚未定稿。以后 PCB 留好禁布区和安装孔后，可将托盘换成孔位匹配的立柱板。
- 电子器件为从封装 F.Fab 边界提取的矩形包络；高度是假定值，其中 JST 接头按 12 mm 已插接高度预留。**没有导入精确电子件 STEP**，不代表插头、导线和全部小器件均通过检查。
- 天线朝前，模型为天线周围约 15 mm 留出铝件空间，并检查该体积不与上下骨架和铝秤盘相交。它不是射频仿真；还要验证电池、线束、外壳和整机 Wi-Fi。[Espressif 模组布局指南](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/pcb-layout-design.html#general-principles-of-pcb-layout-for-modules-positioning-a-module-on-a-base-board)
- 主板 USB-C 在目前旋转后的布局中朝内部，外壳右前方留了 13 × 7 mm 的延长接口开口。应选支持 USB 数据和供电的短延长件，核对线头、弯曲半径、固定方式后再改孔；当前没有模拟整根线缆和面板插座。
- 电池只放在固定托盘中，另需可拆绑带、绝缘、防挤压余量；当前电池包络不对应已选 SKU。
- 现有 BOM 与 PCB 的 J2 信号正负描述有差异：快照中 J2.3=`LC_SIGN`、J2.4=`LC_SIGP`，按实际 PCB 网络及传感器实物接线核对。

