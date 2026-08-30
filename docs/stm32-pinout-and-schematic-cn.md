# STM32G474CET6 引脚与原理图输入表

> 目标：`STM32G474CET6 / LQFP48`。
> 状态：公共电路可画；工厂编码器电气为 `PCB_HOLD`。
> 规则：厂家回复前，编码器候选脚只作 MCU 内部资源预留，不画编码器连接器、供电、上拉、下拉、电平转换或协议名。

官方依据：[STM32G474CE 数据手册 DS12288](https://www.st.com/resource/en/datasheet/stm32g474ce.pdf)、[RM0440](https://www.st.com/resource/en/reference_manual/rm0440-stm32g4-series-advanced-armbased-32bit-mcus-stmicroelectronics.pdf)、[DRV8316](https://www.ti.com/lit/ds/symlink/drv8316.pdf)、[INA228](https://www.ti.com/lit/ds/symlink/ina228.pdf)、[VEML7700](https://www.vishay.com/docs/84286/veml7700.pdf)、[Waveshare 原理图](https://files.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-1.32/ESP32-S3-Touch-AMOLED-1.32-Schematic.pdf)。

## 1. 当前 48 引脚表

| 脚 | MCU 引脚 | 当前模式 | 当前网络 | 画图规则 |
| ---: | --- | --- | --- | --- |
| 1 | VBAT | 电源 | `VBAT_3V3` | 0 Ω 接 3V3，100 nF 就近 |
| 2 | PC13 | OD 输出 | `SYS_FAULT_N_BUF` | 经带 Ioff 的开漏缓冲到 ESP GP1 |
| 3 | PC14 | 模拟/高阻 | `RSV_ENC_AUX` | 只在 MCU 端标注并悬空；也是 `OSC32_IN`，未冻结 |
| 4 | PC15 | PP 输出 | `EXT_WATCHDOG_WDI_DNP` | 首版外部看门狗 DNP，留普通调试焊盘 |
| 5 | PF0 | HSE | `HSE_IN_24M` | 24 MHz 晶体 |
| 6 | PF1 | HSE | `HSE_OUT_24M` | 负载电容按晶体 CL 复核 |
| 7 | NRST | 复位 | `NRST` | 10 kΩ 上拉、100 nF、SWD |
| 8 | PA0 | ADC1_IN1 | `DRV_SOA` | 1 kΩ/33 pF ADC 支路 |
| 9 | PA1 | ADC2_IN2 | `DRV_SOB` | 1 kΩ/33 pF ADC 支路 |
| 10 | PA2 | PP 输出 | `BRAKE_FORCE_TEST` | 只可强制制动，不可关闭硬件自动制动 |
| 11 | PA3 | OD 输出 | `DRV_DRVOFF` | 外部 10 kΩ 上拉；复位默认关断；不可用 100 kΩ 与 DRV 内部下拉分压 |
| 12 | PA4 | 模拟/高阻 | `RSV_ENC_0` | MCU 端预留并悬空，不写 SPI/ABI/PWM |
| 13 | PA5 | 模拟/高阻 | `RSV_ENC_1` | MCU 端预留并悬空，不加串阻/测试点 |
| 14 | PA6 | 模拟/高阻 | `RSV_ENC_2` | MCU 端预留并悬空，不加上下拉 |
| 15 | PA7 | 模拟/高阻 | `RSV_ENC_3` | MCU 端预留并悬空，不接编码器电源 |
| 16 | PB0 | ADC3_IN12 | `DRV_SOC` | 1 kΩ/33 pF ADC 支路 |
| 17 | PB1 | ADC1_IN12 | `VBUS_SENSE` | 100 kΩ/20.0 kΩ，0.1%，比值 6:1 |
| 18 | PB2 | ADC2_IN12 | `MOTOR_TEMP` | TMP36 等效初值，实物再校准 |
| 19 | VSSA | 电源 | `AGND` | 与整面 GND 低阻连接 |
| 20 | VREF+ | 电源 | `3V3A_REF` | 100 nF + 1 µF，就近 |
| 21 | VDDA | 电源 | `3V3A` | 磁珠由 3V3 供电，100 nF + 1 µF |
| 22 | PB10 | AF7 USART3_TX | `STM_TX_TO_ESP_J1_12_RX` | 5 Mbaud，源端 22–33 Ω |
| 23 | VSS | 电源 | `GND` | 就近回流 |
| 24 | VDD | 电源 | `3V3` | 100 nF 就近，MCU 旁 4.7 µF |
| 25 | PB11 | AF7 USART3_RX | `STM_RX_FROM_ESP_J1_11_TX` | 5 Mbaud |
| 26 | PB12 | AF6 TIM1_BKIN | `HARD_FAULT_N` | 2.2 kΩ 上拉，低有效，无数字滤波 |
| 27 | PB13 | AF6 TIM1_CH1N | `DRV_INLA` | 47 Ω 串联、100 kΩ 下拉 |
| 28 | PB14 | AF6 TIM1_CH2N | `DRV_INLB` | 47 Ω 串联、100 kΩ 下拉 |
| 29 | PB15 | AF4 TIM1_CH3N | `DRV_INLC` | 47 Ω 串联、100 kΩ 下拉 |
| 30 | PA8 | AF6 TIM1_CH1 | `DRV_INHA` | 47 Ω 串联、100 kΩ 下拉 |
| 31 | PA9 | AF6 TIM1_CH2 | `DRV_INHB` | 47 Ω 串联、100 kΩ 下拉 |
| 32 | PA10 | AF6 TIM1_CH3 | `DRV_INHC` | 47 Ω 串联、100 kΩ 下拉 |
| 33 | PA11 | AF11 TIM1_CH4 | `ADC_SAMPLE_TRIG` | OC4REF/ADC 同步标记，必须留测试点 |
| 34 | PA12 | 保留 | `USB_DP_DNP` | 首版不连接 |
| 35 | VSS | 电源 | `GND` | 就近回流 |
| 36 | VDD | 电源 | `3V3` | 100 nF 就近 |
| 37 | PA13 | SWD | `SWDIO` | 10 针 Cortex 调试口 |
| 38 | PA14 | SWD | `SWCLK` | 10 针 Cortex 调试口 |
| 39 | PA15 | AF4 I2C1_SCL | `I2C1_SCL` | INA228/VEML7700，2.2 kΩ 上拉 |
| 40 | PB3 | AF6 SPI3_SCK | `DRV8316_SPI3_SCK` | Mode 1，5 MHz，源端 22 Ω |
| 41 | PB4 | AF6 SPI3_MISO | `DRV8316_SPI3_MISO` | 驱动器到 MCU |
| 42 | PB5 | AF6 SPI3_MOSI | `DRV8316_SPI3_MOSI` | 源端 22 Ω |
| 43 | PB6 | PP 输出 | `DRV8316_CS_N` | 10 kΩ 上拉 |
| 44 | PB7 | AF4 I2C1_SDA | `I2C1_SDA` | INA228/VEML7700，2.2 kΩ 上拉 |
| 45 | PB8 | 输入 | `BOOT0_STRAP` | 100 kΩ 下拉 + 测试点 |
| 46 | PB9 | PP 输出 | `SYNC_SCOPE_TP` | ADC ISR/控制节拍示波标记 |
| 47 | VSS | 电源 | `GND` | 就近回流 |
| 48 | VDD | 电源 | `3V3` | 100 nF 就近 |

## 2. 编码器候选资源：只作核对，不是冻结

ST DS12288 Rev.6 的 LQFP48 引脚与复用表给出：

| 引脚 | 封装脚 | 可用候选功能 | 当前动作 |
| --- | ---: | --- | --- |
| PA4 | 12 | GPIO、SPI1_NSS、TIM3_CH2 等 | `RSV_ENC_0`，高阻 |
| PA5 | 13 | GPIO、SPI1_SCK 等 | `RSV_ENC_1`，高阻 |
| PA6 | 14 | GPIO、SPI1_MISO、TIM3_CH1 等 | `RSV_ENC_2`，高阻 |
| PA7 | 15 | GPIO、SPI1_MOSI、TIM3_CH2 等 | `RSV_ENC_3`，高阻 |
| PC14 | 3 | GPIO、OSC32_IN | `RSV_ENC_AUX`，高阻；LSE 冲突待定 |

这组资源理论上覆盖常见 SPI 或 A/B 候选，但不代表 CubeMars 使用这些接口。厂家回复后根据真实接口只选一套，并重新核对 DMA、定时器、LSE、保护和调试资源。

厂家回复前禁止：

- 给候选脚加协议名、连接器脚号、上拉/下拉、TVS、串阻或电平转换。
- 从 3V3、5V、VBUS 或电机母线给“编码器 VCC”供电。
- 把候选脚写成 PCB/固件最终冻结。

## 3. DRV8316 与约 2 A 硬件保护

DRV8316 采用 6-PWM、SPI Mode 1/5 MHz、40 kHz。驱动器内部短路 OCP 只承担短路层，不能冒充项目 2 A 上限。

三路 SOx 各分为 ADC 支路和外部窗口比较器支路：

```text
DRV SOx ──1 kΩ── ADCx
             └──33 pF── AGND
       └──1 kΩ/100 pF── 外部窗口比较器
```

`VREF_CSA=3.300 V`、名义增益 `0.600 V/A` 时，首板采用 `100 kΩ/21.0 kΩ` 对称分压，比较阈值为 `0.573 V` 与 `2.727 V`，典型约 `±1.80 A`。把 CSA `±10.5%` 增益误差、约 `±50 mA` 等效偏置、AVDD=`3.1–3.465 V` 和比较器全温失调放入保守计算后，粗略动作范围约 `1.47–2.17 A`，因此它不是精密的“保证 2 A”保护；首板必须测每相零点、增益和动作点后再换阻值。六路开集比较器、DRV8316 `nFAULT` 和母线 16 V 比较器线与到 `HARD_FAULT_N`；PB12 低有效、无数字滤波、自动恢复关闭。首板必须逐路注入验证。

PWM 外部下拉、`DRVOFF` 外部上拉和 BKIN 硬件关断必须在 MCU 未运行时仍成立。禁止把 `nFAULT` 组合反馈到 `DRVOFF` 形成不可分析自锁。

## 4. 母线与再生制动初值

```text
MOTOR_BUS（3S，约 9.0–12.6 V）── 10 Ω / 50 W 脉冲电阻 ── 60 V N-MOS ── GND
                                ↑
                 14.4 V 硬件比较器自动开启
                 PA2 只能通过二极管 OR 强制开启
```

- 首板上游固定为 `BQ25798 SYS → 外部双向高侧隔离开关 → INA228 → MOTOR_BUS`；`BAT` 接带 BQ77915 级独立保护/均衡/NTC 的 3S 电芯组。Waveshare 的 1S 电池口不接产品电池。
- 建议 14.4 V 开启、13.6 V 释放；独立 16.0 V 比较器拉低 `HARD_FAULT_N`。
- 电阻、MOS、制动占空比和冷却必须用 `E=1/2 Jω²`、实际惯量和连续拨动工况复核。
- 制动比较器和 MOSFET 驱动必须在 MCU/逻辑电源关闭后仍由 `MOTOR_BUS` 或常开安全域工作，才能覆盖关机反拖。
- TVS 不替代制动电阻；14.4/16.0 V 是实验初值，不得按当前 12.45 V 充电默认直接写成量产阈值，所有阈值在首板用隔离注入实测。

## 5. ESP32-S3、INA228、VEML7700

- PB10/PB11 与 Waveshare 模块按 5 Mbaud 8N1 DMA；PC13 经 Ioff 开漏缓冲给 ESP 故障输入。
- 两块板各自稳压，只共地和信号，不通过 12Pin 反向供电。
- I2C1 使用 PA15/PB7，3V3A 各 2.2 kΩ 上拉；PB6 是 DRV8316 片选。目标 400 kHz，实板按波形调时序。
- INA228 地址 `0x40`，5 mΩ 四端分流器；VEML7700 地址 `0x10`，光窗不得被旋环遮挡。
- 慢速传感器失败不能代替本地硬件过流/过压关断。

## 6. 必画测试点

`VBUS_PD_15V`、`BAT_3S`、`SYS`、`MOTOR_BUS`、`3V3`、`3V3A`、`AGND`、六路 PWM、`ADC_SAMPLE_TRIG`、`DRVOFF`、`nFAULT`、`HARD_FAULT_N`、SOA/B/C、V_LOW、V_HIGH、VBUS_SENSE、BRAKE_GATE、SPI3_SCK/CS、I2C1_SCL/SDA、INA228_IN+/IN−/ALERT、UART_TX/RX、PB9、NRST、SWDIO、SWCLK。

编码器候选脚本轮不列入测试点；厂家确认接口后再按信号完整性和调试需要添加。

## 7. 投板前禁止项

- G0 厂家问题未闭合，不得冻结编码器电路或投最终板。
- 未画并独立审查约 2 A 窗口比较器（首板名义 ±1.8 A）、14.4 V 制动和 16 V 关断，不得投主功率板。
- 未完成 BQ25798 SYS 与独立 BAT 电机路径的脉冲/回灌 A/B、BQ77915 保护注入和关机反拖验证，不得冻结产品电源版。
- 未完成电源域反灌、ERC、封装/连接器方向和保护注入点复核，不得投板。
- 本文所有电压、电流、频率和 RC 是首板输入，不是实测结论。
