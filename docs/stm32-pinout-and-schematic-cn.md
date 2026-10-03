# STM32G474CET6 当前引脚与原理图输入

更新：2026-10-02。硬件依据为 **A-SCH-INPUT-20260914 已购器件版**；本文件覆盖早期“编码器未确认”表及 9/18 指南中冲突的 PB9 建议。目标始终是 STM32G474CET6 / LQFP48，不套用 NUCLEO 引脚。

当前使用 Altium Designer 的 [R6 无测试点原理图](../hardware/pcb/stm32-foc-a-altium-r6/README_CN.md)，继续引用本机用户库。旧 KiCad 源稿仅为历史输入与审查来源。审核后只布局、不布线；操作见[最新画板指南](pcb-drawing-guide-20261003-cn.md)，没有 PCB DRC 或实板通过的结论。

## 1. 现在按什么画

| 项目 | 统一后的用途 | 本轮代码状态 |
| --- | --- | --- |
| PB9 / 46 脚 | MOTOR_PWR_EN，请求上游电机母线 | 已取消 ADC 中断调试脉冲；推挽低电平启动，故障撤销请求 |
| PA12 / 34 脚 | DRV nSLEEP | 已设置推挽低电平启动；不是 USB 预留 |
| PC13 / 2 脚 | 向 U7 输入就绪/故障状态 | 慢速推挽，低表示未就绪；开漏在 U7 输出端，不在 MCU 输入驱动端 |
| PB1 / 17 脚 | 母线 ADC | 软件倍率 **11**；硬件比较器的独立分压仍为 6 |
| PB2 / 18 脚 | 10k/B3950 NTC | 已换成 NTC 方程；开短路及超合理范围锁温度故障，不使用 TMP36 公式 |
| PA4/5/6/7 | 已确认 AS5048A 的 CS/SCK/MISO/MOSI | 产品只读 SPI1 已实现；CS 默认高，SPI Mode 1、16 位、2.5 MHz；待实板波形/时序验收，仍拒绝出力 |
| PC14/PC15/PA11 | R6 明确 NC，无测试点 | ADC 触发使用内部 TIM1 TRGO2/OC4；不按旧预留说明恢复外部焊盘 |
| PB8 | BOOT0 | 本轮仍保留救援用途，未改作 LED 数据 |

**当前固件上电先保持关断，逻辑、温度、编码器和新鲜 ADC 合格后，会自动准备母线和唤醒驱动；准备不使能 PWM。** 母线稳定等待、配置读回、采零及驱动状态确认全部合格后，还需要有效电角零位及新显式命令才能出力。故障先关 PWM/DRVOFF，再撤销 nSLEEP 和母线请求，准备失败不自动重试；正常零扭矩不通过反复切电实现。当前电角零位仍无效，且未烧录这次修订，详情见[启动流程验证记录](../outputs/power-startup-review-20261002/README_CN.md)。

MOTOR_BUS 上的独立再生制动必须位于上游隔离开关的下游，并在逻辑掉电后仍能工作。这是电路要求，尚无本轮硬件验证；仅关母线不能证明反拖安全。

## 2. 完整 48 脚硬件表

下表来自已购器件版结构化输入。它描述接线用途；“编码器用途确定”和“产品固件端口已实现”是两件事。

| 封装脚 | MCU 脚 | 原理图模式 | 网络 | 连接说明 |
| ---: | --- | --- | --- | --- |
| 1 | VBAT | 电源 | VBAT_3V3 | R1接3V3；C1去耦 |
| 2 | PC13 | GPIO 慢速推挽 | SYS_FAULT_N_MCU | 低=故障；R9默认下拉；只驱动U7逻辑输入，不带LED |
| 3 | PC14 | 不连接 | NC | R6 无测试点，不用 LSE |
| 4 | PC15 | 不连接 | NC | R6 无测试点，不用 LSE |
| 5 | PF0 | HSE | HSE_IN | 24 MHz晶体X1.1 |
| 6 | PF1 | HSE | HSE_OUT_MCU | 经R6到X1.3 |
| 7 | NRST | 复位 | NRST | R4上拉、C10、SW1、J4.5；保持复位功能 |
| 8 | PA0 | ADC1_IN1 | SOA_ADC | U9隔离后1k/33p |
| 9 | PA1 | ADC2_IN2 | SOB_ADC | U9隔离后1k/33p |
| 10 | PA2 | GPIO 推挽 | BRAKE_FORCE_MCU | 经R44到B板；只强制开启制动 |
| 11 | PA3 | GPIO 开漏 | DRV_DRVOFF | R10上拉；释放=关桥，拉低=允许 |
| 12 | PA4 | GPIO 推挽 | ENC_CS_MCU | 经R31；R30上拉；片选由GPIO控制 |
| 13 | PA5 | AF5 SPI1_SCK | ENC_SCK_MCU | 经R32到蓝线 |
| 14 | PA6 | AF5 SPI1_MISO | ENC_MISO_MCU | 绿线经R34 |
| 15 | PA7 | AF5 SPI1_MOSI | ENC_MOSI_MCU | 经R33到黄线 |
| 16 | PB0 | ADC3_IN12 | SOC_ADC | U9隔离后1k/33p |
| 17 | PB1 | ADC1_IN12 | VBUS_ADC | 独立200k/20k分压，换算倍率11 |
| 18 | PB2 | ADC2_IN12 | MOTOR_TEMP_ADC | 10k NTC探头；不是TMP36换算 |
| 19 | VSSA | 电源 | GND | 统一GND，模拟回流就近 |
| 20 | VREF+ | 电源 | 3V3A | C7/C8；关闭VREFBUF输出 |
| 21 | VDDA | 电源 | 3V3A | C5/C6 |
| 22 | PB10 | AF7 USART3_TX | STM_UART_TX | U8 A1输入，向ESP发送 |
| 23 | VSS | 电源 | GND | 地 |
| 24 | VDD | 电源 | 3V3 | C2；MCU共用C9 |
| 25 | PB11 | AF7 USART3_RX | STM_UART_RX | U8 A2Y输出 |
| 26 | PB12 | AF6 TIM1_BKIN | HARD_FAULT_N | 低有效；滤波0；AOE=0 |
| 27 | PB13 | AF6 TIM1_CH1N | PWM_LA_MCU | R21=47Ω，驱动端R27=100k下拉 |
| 28 | PB14 | AF6 TIM1_CH2N | PWM_LB_MCU | R23=47Ω，驱动端R29=100k下拉 |
| 29 | PB15 | AF4 TIM1_CH3N | PWM_LC_MCU | R25=47Ω，驱动端R43=100k下拉 |
| 30 | PA8 | AF6 TIM1_CH1 | PWM_HA_MCU | R20=47Ω，驱动端R26=100k下拉 |
| 31 | PA9 | AF6 TIM1_CH2 | PWM_HB_MCU | R22=47Ω，驱动端R28=100k下拉；禁用UCPD下拉 |
| 32 | PA10 | AF6 TIM1_CH3 | PWM_HC_MCU | R24=47Ω，驱动端R42=100k下拉；禁用UCPD下拉 |
| 33 | PA11 | 外部不连接 | NC | R6 无测试点；TIM1 TRGO2/OC4 在内部触发 ADC |
| 34 | PA12 | GPIO 推挽 | DRV_NSLEEP_MCU | 本版正式使用；R11+R12/C20；默认睡眠 |
| 35 | VSS | 电源 | GND | 地 |
| 36 | VDD | 电源 | 3V3 | C3 |
| 37 | PA13 | SWD | SWDIO | J4.2 |
| 38 | PA14 | SWD | SWCLK | J4.4 |
| 39 | PA15 | AF4 I2C1_SCL | I2C_SCL | 2.2k上拉3V3；与B/C低速器件相连 |
| 40 | PB3 | AF6 SPI3_SCK | DRV_SCK_MCU | R14=22Ω；不再用SWO/JTAG |
| 41 | PB4 | AF6 SPI3_MISO | DRV_SDO | R17=1k上拉；禁用UCPD死电池下拉 |
| 42 | PB5 | AF6 SPI3_MOSI | DRV_SDI_MCU | R15=22Ω |
| 43 | PB6 | GPIO 推挽 | DRV_CS_MCU | R16=22Ω；R18=10k上拉；禁用UCPD下拉 |
| 44 | PB7 | AF4 I2C1_SDA | I2C_SDA | 2.2k上拉3V3 |
| 45 | PB8 | BOOT0/GPIO输入 | BOOT0 | 100k下拉；JP1焊桥临时接3V3 |
| 46 | PB9 | GPIO 推挽 | MOTOR_PWR_EN_MCU | 本版控制B板电机母线使能；R46/R47默认关 |
| 47 | VSS | 电源 | GND | 地 |
| 48 | VDD | 电源 | 3V3 | C4 |

## 3. 必须保留的电气细节

- PA3 为开漏 DRVOFF，外部 10k 上拉；PB12 为低有效 BKIN，保持无滤波、AOE 禁止自动恢复。PA11 的 TIM1_CH4 继续用于 ADC 触发观察。
- nSLEEP 使用原输入的 2.2k 串阻、100k 下拉、100nF；母线使能沿用原输入 R46/R47 默认关断。复位时 GPIO 默认值不能替代板上的外部上下拉。
- PC13 只慢速驱动 U7 的高阻逻辑输入。U7 输入有 100k 下拉，输出开漏上拉到 **3V3_ESP**；STM 自身 3V3 不与屏幕 3V3 并联。
- UART 保留已经选定的 **TXU0202**；不用因 9/18 的概念建议再换购两个单门缓冲。PB10→ESP GPIO44，PB11←ESP GPIO43，5 Mbaud。
- 原厂 AS5048A：黑 GND、红 5V、绿 MISO、黄 MOSI、蓝 CLK、白 CSn。板上 J3 自定义焊盘号不代表厂家插头几何顺序；后续实现使用已存在的 AS5048A 编解码，不另猜型号。
- SO 三相的 ADC 支路经过 TMUX1511；硬件比较器支路不经过该开关。DRV 物理 nFAULT 与汇总 HARD_FAULT_N 经 U6 分开，不直接互相反灌。
- 母线 ADC 使用 100k＋100k / 20k，比较器使用 100k / 20k。不得混用分压节点或倍率。ADC 采样窗口、源阻抗及两颗 1nF 的实际建立时间仍需在实板验证。
- NTC 是 10k 上拉到参考电源域、10k/B3950 到地；公式在现有 **200 Hz** 监测任务运行，ADC 中断只保存原始读数。软件 -40～125 ℃ 为信号合理性窗口，70/85 ℃ 为现有告警/故障阈值；不代表热敏电阻精度、安装温差或故障响应时间已测定。

所有逐器件脚号、66 个测试点、两条 10P FFC 的针序仍见 [已购器件版完整连接说明](../outputs/01a08963-e2d7-71e2-a353-8710a44dc948/stm32-foc-schematic-20260914/GL30_A板_STM32_FOC_原理图连接说明.md)。

## 4. 灯环与普通按键：尚未冻结的部分

PB9 已确定归母线使能，撤销 9/18 的 PB9/TIM17 灯带建议。也不能把 Waveshare 的 J1 GP0/GP1/GP2 当作未占用脚直接接灯带：GP0 涉及启动，GP1 已用于故障，GP2 需保留模组原有电源边界。

PB8 的 TIM16_CH1 在 ST 的该封装数据库中存在，是后续灯带候选；它同时是 BOOT0，必须先审查启动选项字节、焊桥限流、缓冲输入在掉电时的负载和复位波形，再决定是否采用。本轮没有把 PB8 改成输出，也没有把这一候选写进采购/投板结论。

普通按键和低速使能可继续评估 9/18 的 IO 扩展器方案，但型号、供电域、地址、连接器和驱动仍待收敛；TCA9535 不是已购/已接入事实。安全关断与电机使能不依赖 I2C 扩展器。

## 5. 验证和下一步

本轮先通过回归检查复现 PA12、PB9、母线倍率三项旧错误，再修改。NTC 增加开路、短路、合理范围、温度参考点及全 ADC 区间单调性检查。日志和构建结果见 [本轮接口核对包](../outputs/product-interface-20260927/README_CN.md)。

重新生成时还修正了 IOC 的 `ADC12CLockSelection/ADC345CLockSelection` 大小写，移除无效派生字段；选源仍是 PLLP 的 40 MHz，未改动已有生成 C 的 ADC 实际时钟设计。BKIN、死区及禁止自动恢复保留。

原生 [KiCad A 板原理图首版](../hardware/pcb/stm32-foc-a/README_CN.md)已完成：9 页、518 连接端点对照一致、ERC 0 违规；并非可投板结论。产品 AS5048A SPI1 只读端口已接入，见[10/02 记录](../outputs/project-advance-20261002/README_CN.md)。母线请求、唤醒、配置读回、采零与驱动确认已接成[无转矩准备状态机](../outputs/power-startup-review-20261002/README_CN.md)，尚未烧录或实板验收。下一步闭环实物封装、排线/编码器小板、灯环/按键接口，再按合并机械包络布局，保持不布线。零位标定、采样比例、保护注入、ADC 时序、回生、按压/皮带手感属于实物验证，不由离线测试替代。

官方核对入口：[ST STM32G474CE](https://www.st.com/resource/en/datasheet/stm32g474ce.pdf)、[TI DRV8316](https://www.ti.com/lit/ds/symlink/drv8316.pdf)、[TI SN74LVC1G07](https://www.ti.com/lit/ds/symlink/sn74lvc1g07.pdf)、[Waveshare 1.32 原理图](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.32/ESP32-S3-Touch-AMOLED-1.32-Schematic.pdf)。

2026-10-03 补充：R6 的 PC14/PC15/PA11 外部连接为 NC。冻结固件 board_config.h 的 PA11 描述条目仍记 AF11，未在本轮修改；不能把描述条目当作新增走线要求。J4 最新为单排 5P/2.54 mm，NRST 在第 5 脚。历史逐引脚接线文档中的测试点及 2×5 J4 已被 R6 取代。
