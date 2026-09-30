# 滤杯侧托架称重传感器初选

研究日期：2026-09-18。用途已明确为“立柱侧面伸出托架承托滤杯”；本文件针对新增滤杯托架，不是现有底座秤的结构定稿。

建议优先采用**带偏载补偿的小量程单点式／平行梁称重传感器**，以丽景 **LC7012-2kg** 作为首轮询价和样机候选，HBK **PW4M-2kg** 作为技术对照。2 kg 尚未定案：必须计入托架、滤杯、湿粉及最大存水的全部重量，并核算装卸时的冲击和力矩。电子去皮不会释放机械量程。

## 类型与安装方式

建议载荷路径：立柱 → 刚性安装座 → 传感器固定端 → 传感器承载端 → 刚性托架 → 滤杯。传感器的指定测量方向应与竖直重力方向一致；“安装在立柱侧面”不等于把传感器随意转向。单点式的偏载补偿适合承托小平台，但仍有允许台面与偏载范围，不能据此允许任意长的悬臂。[单点式原理与类别](https://www.flintec.com/weight-sensors/load-cells)、[HBK 载荷引入指南](https://cloud.hbkworld.com/en/knowledge/resource-center/articles/load-application-in-load-cells)

| 类型 | 对本结构的判断 | 原因 |
| --- | --- | --- |
| 小型单点式／平行梁 | 首选 | 固定端与承载端容易分别连接立柱安装座和托架，并有偏载补偿。 |
| S 型拉压 | 非首选 | 更适合沿轴线拉压、悬吊。侧托架产生的弯矩需要额外机构隔离，安装复杂度增加。 |
| 按钮式压向传感器 | 非首选 | 中心压向加载容易实现，但单个按钮本身不能完整约束侧伸托架，需要导向／柔性机构处理倾覆力矩，并避免摩擦或旁路承重。 |

以上适配判断属于结合本项目结构的工程推断；轴向加载与避免侧向力、扭矩的依据见 [HBK 指南](https://cloud.hbkworld.com/en/knowledge/resource-center/articles/load-application-in-load-cells)，S 型常见悬吊用途见 [Flintec S 型说明](https://www.flintec.com/weight-sensors/load-cells/s-type)。因此不应笼统认为“梁式不能做侧托架”，也不需要仅因外形像支架就改用 S 型。

托架与立柱、外罩、防水件之间应留出工作变形间隙；限位件仅在过载时接触，线缆应留松弛段。固定座应足够刚，避免其他接触路径分走载荷。杯中心到传感器加载区域的距离必须在图纸上明确；厂商的 200×200 mm 台面规格不能直接解释为允许向外伸出 200 mm。[HBK 安装原则](https://cloud.hbkworld.com/en/knowledge/resource-center/articles/load-application-in-load-cells)

## 候选参数

| 项目 | 丽景 LC7012 | HBK PW4M（非 OP 版） | 丽景 LC1110 |
| --- | --- | --- | --- |
| 定位 | 首轮样机优先候选 | 低压激励条件明确的对照 | 较长窄体的备选 |
| 尺寸 | 长70×宽12×高22 mm，4×M3 | 2/3 kg：长70×宽22×高15 mm | 110×10×33 mm |
| 量程 | 网页规格表0.3/0.5/1/2/3 kg；PDF另列5 kg | 本次B02224表列0.3/0.5/2/3 kg | 0.2/0.3/0.6/1/1.5/3 kg，无标准2 kg档 |
| 输出 | 2 kg网页写2 mV/V，PDF写1.0±0.2 mV/V，需确认 | 2 kg：2.0±0.2 mV/V | 1.0±0.2 mV/V |
| 输入电阻 | 410±10 Ω | 300–500 Ω | 410±10 Ω |
| 激励 | 推荐5–12 V | 额定工作范围1–8 V，参考5 V | 推荐5–12 V |
| 精度指标 | 综合误差±0.02%额定输出；30 min蠕变≤±0.02% | C3；2 kg最小检定分度值0.2 g；非线性和滞后典型各±0.015% | 综合误差±0.02%；30 min蠕变±0.02% |
| 适用台面 | 200×200 mm | 200×200 mm | 200×200 mm |

参数来源：[LC7012 产品页](https://www.labloadcell.com/lc7012-parallel-beam-aluminum-alloy-weight-sensor-product/)、[LC7012 官方 PDF（已目检尺寸图）](https://www.labloadcell.com/uploads/LC7012.pdf)、[PW4M 官方 B02224 数据表](https://www.hbm.com/fileadmin/mediapool/hbmdoc/technical/B02224.pdf)、[LC1110 产品页](https://www.labloadcell.com/lc1110-aluminum-alloy-single-point-load-cell-for-retail-scale-product/)、[LC1110 厂家尺寸说明](https://www.labloadcell.com/news/introduction-to-the-models-and-features-of-single-point-load-cells/)。丽景出口网站的厂商身份见其[公司说明](https://www.labloadcell.com/news/lascaux-a-load-cell-supplier-in-china-we-value-the-rd-capabilities-of-structural-engineers-and-electronic-engineers/)。

LC7012 的官方网页与 PDF 对灵敏度、可选量程、安装扭矩等存在版本冲突；不把网页或旧 PDF 的扭矩直接作为加工装配指令。下单前应取得与所购量程、版本一致的签认图纸、灵敏度和拧紧扭矩。网页标注 IP65/IP66 也不一致，防护能力同样以所购版本确认单为准。

## 分辨率与准确度

“显示0.1 g”不等于“误差保证±0.1 g”。LC7012 若选2 kg，规格中0.02%额定输出换算约为0.4 g；它不是按当前称量值的百分比计算。蠕变、温度、偏载和结构摩擦还需要单独验证，不能把一次校准替代全部工况测试。[LC7012 规格](https://www.lascaux.com.cn/en/goods.php?id=75)

PW4M-2kg 的最小检定分度值是0.2 g，这也不是整机在任何环境下都能达到±0.2 g的承诺。该表未独立列出30分钟蠕变数值，不以零蠕变理解。[PW4M 数据表](https://www.hbm.com/fileadmin/mediapool/hbmdoc/technical/B02224.pdf)

2.7 V处于PW4M标注激励范围内，却低于丽景这两个型号的推荐5–12 V范围。丽景传感器低压下可否满足目标噪声、漂移与准确度，需要厂商确认与样机验证。

## 与当前 s3.1 PCB 的关系

项目当前约束：首路底座为LC1330（输入410±10 Ω）；两路E+共用NAU7802的2.7 V AVDD，按10 mA外部负载上限设计。芯片供电与参考方式应遵循 [NAU7802 数据手册](https://www.nuvoton.com/export/resource-files/en-us--DS_NAU7802_DataSheet_EN_Rev2.6.pdf)。以下是按输入电阻计算的电桥静态负载，不包含额外余量：

| 同时激励的组合 | 2.7 V下的合计电流 | 现有AVDD是否可直接承载 |
| --- | --- | --- |
| LC1330 + LC7012 / LC1110 | 标称13.17 mA；两者取400 Ω时13.50 mA | 超预算 |
| LC1330 + PW4M | LC1330取410 Ω、PW4M取500 Ω时仍约11.99 mA | 超预算 |

**这些候选在机械形式上合适，并不代表可以直接同时插到现板使用。** ADC切换通道不会切断另一桥的激励。若要同时称底座与滤杯，建议机器版评估第二路独立ADC与激励电源，或重新设计两路共同使用的外部激励／参考。单颗NAU7802两通道共用参考，不能仅给第二桥单独换一个E+电压就忽略比例测量、共模范围及内部LDO连接。激励架构应另作电路设计与审核，本研究不构成改线方案。

首板只用底座LC1330、J11空置，仍可沿用单桥方案进行验证；未来第二桥不应在未改激励架构时直接补装。

## 采购和结构定稿前的未决项

1. 滤杯中心距立柱、传感器固定端和加载端分别多远；允许安装的长、宽、高是多少。
2. 托架和滤杯自重、最大湿粉与存水重量，以及是否存在手压、放杯冲击。由此确认1/2/3 kg量程，而非先锁定2 kg。
3. 最终目标是显示0.1 g、短时重复性0.1 g，还是整个冲煮温度范围内的绝对误差±0.1 g；后者要求不同。
4. LC7012实际供货版本的灵敏度、允许偏载／力矩、安装图及扭矩、2.7 V下性能是否获厂商确认。
5. 热水、蒸汽到传感器的热传导与防水结构；满载蠕变、偏心放杯、温变和振动的样机验收结果。
6. 两路独立采样速度需求和激励架构；当前两候选均不能与LC1330共同直接使用原有10 mA AVDD预算。
