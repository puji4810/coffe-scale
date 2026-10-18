# s3.1 待机与功耗验证

2026-10-05：此次只修改固件。现有 s3.1 的模拟轨、电桥和 LCD 电源仍然常供电；下一版 PCB 要求见 `../pcb/s3.1/LOW_POWER_NEXT_REV.md`。

## 固件行为

- 电池供电、连续 5 分钟无活动后自动待机。正在运行的冲煮计时阻止自动待机；已暂停的计时允许待机。充电或连接 USB 主机时保持运行，网页的手动休眠命令仍可强制待机。
- LCD 先关背光、Display Off，再发 SLPIN。唤醒重新初始化控制器并恢复显示。
- LIS2DW12 运行时是 100 Hz 高性能模式；待机双击检测是 400 Hz、关闭低噪声选项。INT1 只路由双击，不路由单击或运动。检测阈值约 0.625 g（2 g 量程），双击 latency 窗口为 400 ms；实际手感需在装好外壳后验证。
- MODE（IO18，低电平）或双击 INT1（IO13，高电平）唤醒。TARE（IO48）不是 RTC 引脚，无法唤醒。
- 默认 `CONFIG_SCALE_DEEP_SLEEP=y`。深睡使用 MODE→EXT0、INT1→EXT1，以兼容两路相反的有效电平。
- RTC 中保留校准、去皮偏移、物理零点修正、单位、模式、暂停计时。深睡唤醒会重启；滤波、流量和稳定性证据从新样本重新建立。不自动去皮，不自动恢复运行中的计时。完全断电只保留 NVS 中的校准。
- 待机准备和恢复期间串行化传感器访问，避免加速度计轮询清掉尚未处理的唤醒锁存。内部芯片温度传感器仅在原始采集需要 `C` 行时短暂启用。

## 烧录与诊断

```bash
cd /home/puji/coffee-scale/firmware
source /opt/esp-idf/export.sh
idf.py -B build-esp32s3 build flash monitor
```

USB 控制台输入 `p` 显示电池端电压估计、百分比估计、上次待机时长及唤醒来源。待机前日志包含本次运行时长、VBAT 和 deep/light 模式。深睡开机日志显示 `MODE` / `double-tap` / 两者同时；睡眠失败会恢复外设并记录错误，而不是把失败当作成功待机。

电量百分比仍是电压曲线估算（3.55 V 约为 10%），没有库仑计。`p` 不是电流测量工具。

若需对比 light sleep，在 `idf.py -B build-esp32s3 menuconfig` 的 **Coffee scale power** 中关闭深睡，然后重新编译烧录。其余 LCD 与双击修复仍然生效；两种配置不能共用一个烧录镜像。

## 上板验收（尚未实测）

1. 拔掉 USB，用电池供电，不运行计时，静置至少 5 分钟；确认自动黑屏。
2. 在不同方向轻碰、移动秤和单敲，确认不会亮屏；有意双击以及 MODE 短按应正常唤醒。双击识别取决于冲击幅度和间隔，测试的是单击/运动不被路由为唤醒源，并不保证任何振动都不会形成真实的双击事件。
3. 去皮一个杯子并切换单位/模式，休眠后拿走杯子，再唤醒。确认负重量、单位和模式正确。恢复冲煮计时应从暂停值继续，不包含睡眠时间。
4. 正在运行的计时不应自动休眠；暂停计时后静置应自动休眠。手动休眠运行中的计时应先暂停，唤醒后仍保持暂停。
5. 重复待机/双击/MODE 20 次，检查背光、称重、按键、BLE 重连，无卡死或看门狗复位。
6. 在电池线串联电流表，分别记录运行、稳定待机的电流以及单击/轻碰时的波形。使用纯电池供电；USB 会改变供电路径，并可能阻止自动待机。

| 状态 | 电池端电压 | 电流 | 备注 |
| --- | --- | --- | --- |
| 运行、BLE 未连接 | 待测 | 待测 | 背光开 |
| 运行、BLE 已连接 | 待测 | 待测 | 固定连接端/亮度 |
| deep sleep | 待测 | 待测 | 屏幕黑、静置 |
| light sleep 对比 | 待测 | 待测 | 需要另编译镜像 |
| 待机时轻碰/单击 | 待测 | 待测 | 不应完整唤醒 |

按整板实测值计算续航，不能把 ESP32-S3 芯片典型深睡电流当成整板电流。s3.1 电桥始终有激励电压，固件无法消除这一部分。

## 依据与验证边界

- [ST AN5038 §5.6](https://www.st.com/resource/en/application_note/dm00401877-lis2dw12-alwayson-3d-accelerometer-stmicroelectronics.pdf)：INT1_SINGLE_TAP 与 INT1_TAP 分别输出单击和双击，敲击检测需要 400 Hz 或更高。
- [ST DS11811](https://www.st.com/resource/en/datasheet/lis2dw12.pdf)：ODR code 5 = 100 Hz，code 7 = 400 Hz；修正旧驱动遗漏 1.6 Hz 档导致的编码偏移。
- [ESP-IDF sleep modes](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/sleep_modes.html)：RTC 引脚、EXT0/EXT1、电平限制与睡眠后的 GPIO 恢复。
- [ST7789V](https://support.newhavendisplay.com/hc/en-us/article_attachments/4414880310423)：SLPIN 停止内部 DC/DC、振荡器和扫描。

主机回归验证双击路由及待机状态恢复；ESP-IDF 编译验证芯片 API、RTC 段和镜像大小。上述电流、物理唤醒手感与硬件上的循环恢复仍需实物验证。
