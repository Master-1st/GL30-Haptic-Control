# 用户补库核对与接入 R6

2026-09-27；版本 `A-SCH-AD-R6-USER-LIB-NO-TP`。已关联 **142/146 个位号**，较 R4 增加 10 个；按用户决定将 J4 改为单排 5P、2.54 mm。其余 145 个位号的采购料号、数值、封装规格与装配状态保持不变。

## 本次新增

| 位号 | 已购料号 | 核对结果 |
| --- | --- | --- |
| U1 | STM32G474CET6 | 精确料号对应 LQFP48；48 焊盘、0.5 mm 脚距及编号核对通过 |
| C28、C29、C53、C54 | 35SVPF39M | 精确符号关联共用封装 `16SVF270M`；E7 焊盘尺寸与极性一致 |
| SW1 | SKRPASE010 | 焊盘尺寸一致；1/2 常连、3/4 常连，与 NRST/GND 分组一致 |
| J5、J6 | FH12-10S-0.5SH(55) | 10 个信号焊盘和 2 个固定焊盘核对通过；11/12 仅为机械固定焊盘 |
| J7 | B2B-PH-K-S(LF)(SN) | 原库孔径 0.70 mm；项目派生封装改为 0.85 mm 孔、1.40 mm 焊盘 |
| J4 | 通用 1×5P 2.54 mm 直插排针 | 用户库 HDR2.54-LI-5P 派生；孔改 1.02 mm，保留 1.70 mm 焊盘 |

电容封装名称 `16SVF270M` 是库里的共用名称，采购料号仍为 **35SVPF39M、39 µF/35 V**。其两焊盘内间距 2.8 mm、外侧总跨度 11.1 mm、焊盘宽 1.9 mm，与松下 E7 推荐值一致，1 为正极、2 为负极。库中 3D 投影包含底座和端子，不能将该平面包络与圆柱直径、器件高度混作同一尺寸。[松下推荐焊盘](https://industrial.panasonic.com/cdbs/www-data/pdf/AAB8000/AAB8000COL10.pdf)

J7 原库的 0.70 mm 孔偏紧。JST 对两针 PH 插座在玻纤基镀通孔板上的建议成品孔径为 0.80–0.85 mm，因此本工程采用 0.85 mm；焊盘扩大到 1.40 mm，最小环宽为 0.275 mm，保持 2 mm 脚距。修正只存在于 `GL30_Adjusted_User.PcbLib` 中，原库未改。原引脚编号、位置、外形和嵌入的 3D 模型数据保持一致。[JST 官方孔径说明](https://www.jst.com/resources/faq/)

STM32 的用户库焊盘长为 1.475 mm，ST 示例为 1.20 mm；脚距、宽度和编号一致，本轮保留用户库的较长焊盘。FFC 的全部焊盘还与工程已有的 KiCad FH12 封装逐一比对，坐标原点换算后位置和尺寸一致。PCB 阶段保留 11/12 固定焊盘且不自动连接信号网；实际排线仍须确认触点朝向并逐根测通。[ST 图纸第 206–208 页](https://www.st.com/resource/en/datasheet/stm32g474ce.pdf)，[Hirose FH12](https://www.hirose.com/en/product/p/CL0586-0522-3-55)，[ALPS SKRP 图纸第 2 页](https://tech.alpsalpine.com/cms.media/product_catalog_ta_02_skrp_en_2cd80f610b.pdf)

## J4 用户确认后的更改

采用普通单排 5P、2.54 mm 直插公排针。引脚：**1=3V3/VTREF，2=SWDIO，3=GND，4=SWCLK，5=NRST**。VTREF 只接调试器目标电压检测端，不能接 5 V 或调试器电源输出端。1 脚是方形焊盘，按本版编号接线。

原用户封装孔径 0.90 mm 对常见 0.64 mm 方针余量较小，因此在 `GL30_J4_Header.PcbLib` 中将孔径改为 1.02 mm。原 2.54 mm 脚距、1.70 mm 焊盘、编号、外形及嵌入 3D 数据保留；没有修改用户源库。[Samtec 标准方针孔径参考](https://suddendocs.samtec.com/catalog_english/tsw_th.pdf)，不要求采购这个品牌。

修改只涉及 J4 的符号、连接、规格和封装；其余原生对象及采购器件保留。AD 实际读出的 J4 已无 6–10 脚，五个信号与上表逐一一致。详见 `verification/j4_change_r6.json`。

## 全部已关联封装

| 工程库 | 封装名称 | 位号数 |
| --- | --- | --- |
| `GL30_Adjusted_User.PcbLib` | `GL30_B2B_PH_K_S_FR4_D085` | 1 |
| `GL30_J4_Header.PcbLib` | `GL30_HDR_1X05_P254_D102` | 1 |
| `User_LCSC.PcbLib` | `C151256_DO-214AA` | 1 |
| `User_LCSC.PcbLib` | `C181596_TSSOP-14` | 2 |
| `User_LCSC.PcbLib` | `C5218861_VQFN-40_5x7` | 1 |
| `User_STM32F407_Ctrl.PcbLib` | `C0603_M` | 43 |
| `User_STM32F407_Ctrl.PcbLib` | `C0805_M` | 6 |
| `User_STM32F407_Ctrl.PcbLib` | `R0603_M` | 70 |
| `User_STM32F407_Ctrl.PcbLib` | `R0805_M` | 1 |
| `User_STM32F407_Ctrl.PcbLib` | `XTAL-3225-4P` | 1 |
| `User_SamacSys.PcbLib` | `16SVF270M` | 4 |
| `User_SamacSys.PcbLib` | `CAPC3225X270N` | 1 |
| `User_SamacSys.PcbLib` | `FH1210S05SH55` | 2 |
| `User_SamacSys.PcbLib` | `QFP50P900X900X160-48N` | 1 |
| `User_SamacSys.PcbLib` | `RESC3116X65N` | 1 |
| `User_SamacSys.PcbLib` | `SKRPASE010` | 1 |
| `User_SamacSys.PcbLib` | `SOP50P310X90-8N` | 1 |
| `User_SamacSys.PcbLib` | `SOP65P640X120-14N` | 1 |
| `User_SamacSys.PcbLib` | `SOT95P280X145-5N` | 3 |

逐位号对应关系与来源见 `BOM_no_testpoints.csv`、`user_footprint_mapping.json`。三份 `User_*.PcbLib` 与当前用户原库逐字节相同；`GL30_Adjusted_User.PcbLib` 与 `GL30_J4_Header.PcbLib` 是明确标注的项目派生库。

## 仍需确定

| 位号 | 当前定义 | 需要完成的内容 |
| --- | --- | --- |
| J1 | 母线直焊接口，2 焊盘 | 按导线与板形确定孔径、间距及应力释放 |
| J2 | 电机三相直焊接口，3 焊盘 | 按电机原线和板形确定焊接结构 |
| J3 | 编码器直焊接口，6 焊盘 | 按实际线束及空间确定排布并标清编号 |
| JP1 | 默认断开的两焊盘焊桥 | 随 PCB 设计补入，不需要采购器件 |

J1/J2/J3 的不装配标记表示直接焊线，PCB 焊盘必须保留。C53/C54 仍为可选不装配电容。

## 实际验证

- 本机 AD 26.10.1.5 实际识别 142 个位号的封装，包含 J4/J7 修正版。
- 7 页、146 个位号、447 个连接引脚、9 个明确未连接引脚、98 个网络与设计输入及用户确认的 J4 更改一致；测试点为 0。
- AD 报告 **0 错误、34 条警告**：4 个上述板上焊盘封装缺项、30 个无驱动源提示。没有关闭检查规则。
- `DM_Compile` 返回 0；本轮连接提取与逐引脚比对通过，不代表所有编译检查通过或投板放行。
- J7 派生封装经保存回读验证：镀通孔、孔径、焊盘尺寸正确；其他图元几何和嵌入 3D 数据一致。
- 封装关联前后，7 页图面完全一致。没有 PCB 布局、布线或硬件上电验证。

最终证据见 `verification/ad_acceptance.json`、`verification/connectivity.json`、`verification/added_library_review_r5.json` 、`verification/j7_derivative_check.json` 和 `verification/j4_change_r6.json`。
