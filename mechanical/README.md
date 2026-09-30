> **最新平顶外壳版本：** [v0.4.4](v044/README.md)，取消长条显示台座，显示玻璃与外露塑料顶边共面，仅秤盘凸起；保留 LC7012 和可调整装配结构。

> **阶梯显示过渡版：** [v0.4.3](v043/README.md)，前区显示玻璃降低 4 mm，仍有独立台座；由 v0.4.4 平顶外壳取代。

> **可调整装配基线：** [v0.4.2](v042/README.md)，基于 LC7012 薄型版，增加整面垫片、重复定位、四点限位锁紧与检修通路；默认高度 43.8 mm。

> **薄型双触摸键基线：** [v0.4.1](v041/README.md)，名义高度 43.8 mm，前部固定黑色玻璃背面安装两个 TTP223。

> **新增 LC7012 版本：** [v0.4 居中四叉臂模型](v04/README.md)使用新屏幕、103450 电池和 s3.1 四角 M2 主板；以下保留 LC1330 历史版本说明。

# LC1330 咖啡秤机械概念 v0.3

**保留 LC1330，采用一体带筋铝承重盘；顶部连续无孔，活动缝藏在下翻边内侧。** 默认使用矩形减重铝底架，外壳和电子件托盘打印。PCB 使用只读快照，没有修改 PCB 文件。

![当前装配](exports/overview.png)

## 当前尺寸与结构

| 项目 | 当前模型 |
|---|---|
| 整机 | 160 × 140 × 48 mm，含 4 mm 脚垫 |
| 一体承重盘 | 160 × 113 × 11 mm；连续顶层 3 mm；底筋与下翻边同体 |
| 承重盘安装 | 28 × 56 × 4 mm 转接板；M6 接传感器，下方 M4 接盘底盲孔 |
| LC1330 | 130 × 30 × 22 mm，沿左右方向横放 |
| 遮缝结构 | 固定内圈挡水立边与可动下翻边无接触搭接 |
| 名义空载间隙 | 侧向 2 mm，下沿开口 3 mm，挡水立边顶到盘底 2.5 mm |
| 固定显示面板 | 独立连续覆膜盖住屏幕、按键开口；覆膜只粘固定侧 |
| 向下限位 | 四点可调，模型初值 0.8 mm，须按实物位移重新设定 |
| PCB / 电池 | 75 × 40 × 1.6 mm PCB 快照，无安装孔；32 × 38 × 8 mm 电池占位 |
| 铝件估算质量 | 承重盘 243.82 g + 转接板 15.50 g；不含螺钉 |

结构、剖面、紧固件和装配顺序见 **[一体承重盘设计](pan-design.md)**。屏幕接线、PCB 托盘和天线避让依据见 [电子件集成说明](references/electronics-integration.md)。

固定屏幕区仍与可动秤面分离，只把分界缝遮到下沿。当前是防泼溅几何概念，未验证防水等级；USB 开口、底盖、膜片按键和线束仍需完善。不得用胶或密封圈跨接活动缝。

## 受力与验证边界

受力路径：杯子 → 一体承重盘 → 小转接板 → LC1330 加载端 → 固定端垫块 → 铝底架 → 脚垫。显示屏、主板和电池都在固定侧。

盘中心 `(0,13.5)`，加载孔组中心 `(53,15)`，左右偏置仍为 53 mm。一体盘减少零件连接，但不会消除偏心力矩。3 kg 集中载荷位于盘中心时，相对加载孔组约产生 1.56 N·m 力矩，未计盘自重；不能据此换算称重误差。

本版已检查实体有效性、零件连通性、45 个实体两两体积干涉、天线铝件避让、可动组件下移 0.4 / 0.8 mm、完整顶层和挡水边、下方工具通路，以及 STEP 导出回读。检查使用刚性几何和电子件包络，未做有限元、倾斜变形、公差叠加、液体仿真或样机称重测试。

样机需在中心、四角及四边中点进行 500 / 1000 / 2000 g 九点测试，各点间不重新标定，各点前后回中心、每档重复至少三轮，并记录回零和漂移。载荷加可动结构自重不得超过传感器额定载荷。`max|W_i−W_O|≤0.2 g` 是待验证偏载目标，不能代替绝对示值、温漂、蠕变和重复性测试。

## 运行

Python 3.12；`ocp-gordon` 单独锁定，以匹配 build123d 0.10 的 OCP 版本。

```bash
cd /home/puji/coffee-scale
uv venv --python 3.12 mechanical/.venv
uv pip install --python mechanical/.venv/bin/python -r mechanical/requirements.txt

mechanical/.venv/bin/python mechanical/assembly.py
mechanical/.venv/bin/python mechanical/assembly.py --frame butterfly
mechanical/.venv/bin/python mechanical/pan_review.py
mechanical/.venv/bin/python mechanical/render.py
mechanical/.venv/bin/python mechanical/render_pan.py
```

默认沿用 [PCB 快照](references/pcb-snapshot.json)。明确要同步 PCB 时才运行 `assembly.py --refresh-pcb`；遇到板形改变会要求重新布置托盘，不会自动解决布局。

| 文件 | 用途 |
|---|---|
| [dimensions.py](dimensions.py) | 尺寸入口；`pan_*` 为当前盘参数，`upper_* / plate_*` 保留历史对照 |
| [integrated_pan.py](integrated_pan.py) | 一体盘、转接板、固定挡水边 |
| [assembly.py](assembly.py) | 完整装配、几何验证、STEP/STL 导出 |
| [pan_review.py](pan_review.py) | 顶部无孔、挡水边连续性、工具通路和质量检查 |
| [exports/v03/rectangle/assembly.step](exports/v03/rectangle/assembly.step) | 当前默认装配 |
| [exports/v03/integrated_pan.step](exports/v03/integrated_pan.step) | 一体承重盘单件 |
| [exports/v03/load_adapter.step](exports/v03/load_adapter.step) | 小转接板单件 |
| [exports/v03/water-path.png](exports/v03/water-path.png) | 实际 CAD 的前缘和侧边剖面 |

`exports/v03/{rectangle,butterfly}/` 包含单件模型、`validation.json`、`step-roundtrip.json` 和导出参数；`exports/v03/pan-review.json` 包含无孔检查及质量对照。STEP/STL 与虚拟环境被本目录 `.gitignore` 排除，本地仍可打开，可由源码重新生成。VTK 离屏渲染在部分 Linux 环境可能需要 Xvfb。

历史 v0.2 的独立秤盘和上托架保留在 [support-design.md](support-design.md)、`upper_support.py` 与 `exports/upper-support/`；`exports/rectangle/` 和 `exports/butterfly/` 是旧装配。**当前设计以 `exports/v03/` 为准。**
