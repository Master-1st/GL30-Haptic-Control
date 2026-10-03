# NUCLEO-G474RE + TI DRV8316REVM：受限 FOC / 触觉台架联调

> 2026-09-08 新开发源码已进入 `HAPTIC26_OFFLINE_UNQUALIFIED`，仅离线构建/测试，未烧录。下文 H25 是冻结实机基线，不是本轮新代码验收；见 [H26 开发记录](../../../docs/haptic26-ui-offline-20260908-cn.md)。

这是 NUCLEO-G474RE + TI DRV8316REVM 台架工程，不是产品 CET6 固件；继续使用 CubeMX 生成的 LL/Keil ARMCLANG 6.21 工程。**当前烧录版本为 `FW=20260907_HAPTIC25`，阶段为 `BOUNDED_BENCH_SMOKE_PASS`，不是全功能或整机通过。** H25 实测在 2026-09-08 UTC（纽约 9 月 7 日晚），版本目录沿用 `20260907`。

H25 的 60136 字节镜像整区读回、host / sanitizer / Release-LTO 各 7/7、TypeScript 21/21 与 SIM_ONLY E2E、60.264 秒无 PWM 准备态、三次 ALIGN、±10/25/50/100 mA 各 20 ms、八类 HAPTIC 各 250 ms，以及 58 项关断命令回归通过。21 组冻结 RAM / CSV 独立审计一致；时序、ADC、编码器与 UART 未新增错误。这里的“触觉通过”仅是命令/控制链短时 smoke，不是手感、跟踪精度或力矩验收。

新增 37 组实机采样、定时/STOP/失联关断和无出力压力测试见 [2026-09-08 扩展验收](../../../docs/bench-validation-20260908-haptic25-extended-cn.md)。可复用脚本位于 `tools/`，不会闪写固件、改变电源设定或放宽保护。

完整功能矩阵、H19/H24 的真实时序失败和 H25 修复见 [HAPTIC25 续测验收报告](../../../docs/bench-validation-20260907-haptic25-cn.md)。最终 `final-off-2` 记录（2026-09-08 03:31:56 UTC 起）确认 `mode=0 fault=0 MOE=0 DRVOFF=1 zero=0 calibrated=0`；SWD 确认通道关闭、六路 PWM 输入低，nSLEEP=1，串口脚本已释放 COM4。没有操作外部电源开关，软件停止不等于物理断电。旧 HAPTIC10 位移故障和早期 STATIC_BUCK_OCP_HOLD 记录保留，但不代表当前 H25 结果。

## 1. 打开哪个文件

- Keil：[MDK-ARM/NUCLEO_G474RE_FOC.uvprojx](MDK-ARM/NUCLEO_G474RE_FOC.uvprojx)。本机完整目录：`G:\Agent\GL30-Haptic-Control\firmware-stm32\bench\NUCLEO_G474RE_FOC`。
- CubeMX：[NUCLEO_G474RE_FOC.ioc](NUCLEO_G474RE_FOC.ioc)，固件包 STM32CubeG4 v1.6.3。
- 联调控制台：[evm_console.ps1](evm_console.ps1)。只查看状态不会启用输出；交互模式每个动作必须手动输入。
- 编码器接入、动力关闭的回归：[test_encoder_off.ps1](test_encoder_off.ps1)，只发送STATUS / SELFTEST / 缓存DRV_DIAG及空行同步，不执行驱动唤醒、校准或出力。
- 已完成的验证和边界：[TEST_RESULT_CN.md](TEST_RESULT_CN.md)。本机编译产物在 `MDK-ARM/NUCLEO_G474RE_FOC/`，不把编译历史提交到仓库。

配套 `.uvoptx` 保留 CubeMX 的 ST-LINK 调试器选择，NUCLEO 使用板载 ST-LINK 的 USB，无需另买下载器。本次实际烧录和回读由 CubeProgrammer 完成。不要在带动力输出时单步/断点调试；固件刻意不冻结看门狗。

CubeMX 配置和生成的初始化保留在 `Core`；应用分别为 `bench_app.c`（采样/命令/校准）、`bench_hw.c`（LL 端口）、`bench_safety.c`（状态门）、`bench_haptics.c`（受限预设）和 `bench_cordic.c`（台架 CORDIC 端口/启动数学自检）。共享 FOC、AS5048A 编解码和 DRV8316 SPI 帧代码仍在上级 `control`、`drivers`，没有复制另一套 FOC。台架目标启用 LTO / `GL30_FOC_USE_CORDIC`，portable host FOC 保留 libm 路径。

重新生成：打开本 IOC，选择 MDK-ARM、LL 和固件包，Generate Code 后在本 Keil 工程 Rebuild。不要打开多个 CubeMX 实例。`generate_cube.script` 是本机已运行的 CubeMX 批处理输入，迁移机器需修改其工程与固件包路径；它不是自制工程生成器。保留整个当前工程目录及上级共享源码，勿仅拷贝 `.uvprojx`。新建空目录生成不会自动包含附加应用源文件组。

## 2. 当前受限联调参数

用户已退回此前电机，确认重购的是同款 GL30 及同款 AS5048A SPI。继续使用厂家 R/L/极对数初值和本表受限参数，不沿用旧电机测量或电角零位。新电机从左到右标为 U/V/W，对应 EVM OUTA/B/C。H25 的 PREPARE / ALIGN / 短脉冲已有实测证据，但每次 STOP 或复位后仍须重新校零、ALIGN；不能复用 RAM 校准或由历史通过推定当前状态。H25 相对 H24 没有修改电机初值或保护阈值。

| 项目 | 当前值与原因 |
| --- | --- |
| 控制器 | STM32G474RET6 / NUCLEO-G474RE，160 MHz |
| 电机 | 已到货 GL30，7 极对，AS5048A SPI；本阶段不使用额定/峰值扭矩 |
| 电源 | 12 V 台架；用户报告电源限流 300–305 mA，未仪表核实，不等于相电流限额；本轮未改变 |
| MCU/编码器供电 | NUCLEO 由 USB 供电；原厂编码器板按 5 V 模式供电 |
| PWM / 采样 | 20 kHz 中心对齐 6-PWM，500 ns 死区；ADC1/2/3 同源 PLLP 40 MHz、异步预分频 ÷1，TIM1 同步注入触发 |
| ADC 窗口 | 30–70% 占空比边界；接近 PWM 峰值触发，写入必须赶在下一个底部更新之前 |
| 编码器 | SPI1 Mode 1，16 bit，2.5 MHz，4 kHz 读取；奇偶校验、EF、磁场诊断和 750 μs 新鲜度门 |
| 驱动器 | SPI3 Mode 1，2.5 MHz（APB1 80 MHz ÷32）；底部窗口预约、150 μs 超时关断；配置后逐项读回并周期检查状态/CSA/Buck |
| EVM 模拟量 | 原板 3.0 V VREF，零电流约 1.5 V；CSA 0.6 V/A；VM 分压 75 kΩ / 6.04 kΩ |
| 校准 | 固定 Vd 渐升至 0.38 V（前 800 ms），1 s 静态→3 s 单向平滑电角一圈→0.5 s 保持；最后 100 ms 平均 Id 须在 0.28–0.52 A，机械净位移严格为 2π/7 rad 的 0.8–1.2 倍；方向/零位只存 RAM |
| 测试出力 | IQ：±100 mA、1–2000 ms；HAPTIC：0–7 类、1–10000 ms、±100 mA；均须本次 ALIGN 成功。追加实测 IQ 2 s（仅 1 mA）、自由模式 10 s、八模式各 1 s；不等于满幅/满载最大时长验收 |
| 母线健康窗口 | 有限值 9.0–15.0 V（含端点）；HAPTIC10 统一用于 health、PREPARE、DRV_LIVE、DRV_TRACE 与连续 trace guard；未物理扫压验收全部窗口 |
| 电压矢量上限 | 正常 IQ/HAPTIC 0.30 V；ALIGN 专用上限 0.60 V、实际渐升/扫场 0.38 V；均非直流电源设定值 |
| 附加停止门 | 正常相电流绝对值 >0.35 A、ALIGN >0.65 A；速度 >8 rad/s；通信/编码器/ADC/驱动故障；ISR 3600 cycles 与 PWM 写入窗口 DOWN、CNT≥320 均保留 |
| 主机许可 / B1 | KEEPALIVE 间隔须 <100 ms；已移除 B1 持续按住门，B1 不是急停；PREPARE/CLEAR 仍要求按钮松开 |
| 复位后 | 输出关断、校准无效，必须重新 PREPARE 和 ALIGN；不写 AS5048A OTP |

20 kHz 是独立 EVM 台架基线，**没有把产品板 40 kHz 设置改掉**。`ALIGN_CURRENT_A=0.40` 是静态资格名义电流，不是当前 ALIGN 的闭环电流指令；ALIGN 实际施加渐升 Vd。校准仍要求真实电流和机械净位移同时过门。H25 通过优化执行时序恢复受限输出，没有为通过测试提高电源限流、改大电压/电流/速度门或关闭故障保护。

## 3. 接线核对表（仅此 NUCLEO + TI REVM 组合）

依据 TI SLVUBZ9B 的 J3/J4 定义及 ST UM2505 Rev 7 的 Morpho 表。`J3.12` 表示 J3 的 12 号引脚；必须看板上 1 脚标记辨认双排奇偶编号，不能按照片左右猜。若实物型号/版本或丝印不符，先停下核对。**不要把 EVM 直接叠插到 NUCLEO，也不要同时接 C2000 LaunchPad/另一块控制器。**

| 功能 | TI DRV8316REVM | NUCLEO 引脚 | Morpho 位置 |
| --- | --- | --- | --- |
| A 高侧 PWM | J4.1 INHA | PA8 | CN10.23 |
| A 低侧 PWM | J4.3 INLA | PB13 | CN10.30 |
| B 高侧 PWM | J4.5 INHB | PA9 | CN10.21 |
| B 低侧 PWM | J4.7 INLB | PB14 | CN10.28 |
| C 高侧 PWM | J4.9 INHC | PA10 | CN10.33 |
| C 低侧 PWM | J4.11 INLC | PB15 | CN10.26 |
| 驱动 SPI 时钟 | J3.13 SCLK | PC10 | CN7.1 |
| 驱动 SPI MOSI | J4.12 SDI | PC12 | CN7.3 |
| 驱动 SPI MISO | J4.14 SDO | PC11 | CN7.2 |
| 驱动片选 | J4.4 nSCS | PC9 | CN10.1 |
| 驱动唤醒 | J3.17 nSLEEP | PC7 | CN10.19 |
| 驱动强制关断 | J4.16 DRVOFF | PC8 | CN10.2 |
| 故障/硬件刹车 | J3.19 nFAULT | PB12 / TIM1_BKIN | CN10.16 |
| A 电流 | J3.14 ISENA | PA0 / ADC1 | CN7.28 |
| B 电流 | J3.16 ISENB | PA1 / ADC2 | CN7.30 |
| C 电流 | J3.18 ISENC | PB0 / ADC3 | CN7.34 |
| 母线采样 | J3.12 VSENVM | PB1 / ADC1 | CN10.24 |
| 信号共地 | J3.4 AGND、J4.2 AGND | GND | CN7.20、CN10.20 |

**容易接错的地方：J3.12 是 VM 分压采样，不是 3.3 V；J3.1 是 DNP 的 3.3VBK 位置。J3.9 / J3.15 是 DNP 备选位，不能拿来替代 J3.17 / J3.19。**

原厂 AS5048A 六线独立接入 NUCLEO，不接 EVM 的 Hall 接口：

| 厂家颜色/功能 | NUCLEO 引脚 | Morpho 位置 |
| --- | --- | --- |
| 黑 GND | GND | CN7.19 |
| 红 +5 V | +5 V | CN7.18 |
| 绿 MISO | PB4 | CN10.27 |
| 黄 MOSI | PB5 | CN10.29 |
| 蓝 CLK | PB3 | CN10.31 |
| 白 CSn | PB6 | CN10.17 |

注意 **PB6 是 CN10.17，不是 CN10.3**。原厂板保持芯片和两颗电容原状；AS5048A 5 V 供电模式的 SPI 电平基于内部 VDDCORE，按手册适配 3.3 V MCU，不能因此泛化成“所有 5 V 模块都可直接接”。

电源与机械注意：

- 动力电源仅送 EVM 标识的 VM / PGND 电源端子；电机三相接 OUTA/B/C 动力端子。三根电机相线先标 A/B/C，软件校准识别方向，不要求猜出厂家 UVW 顺序；换线必须断电且重新校准。
- NUCLEO 的 5 V/3.3 V 不给 EVM 动力供电，也不与 EVM Buck 输出硬并联，核实 R13 为原厂 DNP 状态。EVM 的 3.0 V 基准依赖其 Buck。**Buck 修正版与 TI 配置读回已通过，HAPTIC10 历史 60.154 秒与 H25 本次 60.264 秒无 PWM 准备态均未复现旧静态故障：** 用户照片确认 L1=47 μH，与 SLVUBZ9B 图 7-6 一致。按 DRV8316 表 8-5/8-23，CTRL6（地址 8）为 `0x10`：3.3 V、Buck 开启、BUCK_CL=0、BUCK_PS_DIS=1。代码写入/读回预期均已修正，单次 PREPARE 已通过该值的精确读回；旧 `0x08` 不再适用于本台架。内部 Buck 的 600 mA 档不是台式电源或相电流限流。配置通过不证明模拟启动、纹波或整个驱动健康，不能以此放行出力。
- SPI/ADC 使用短信号线，远离电机相线；逻辑地回路不能承载电机动力回流。不要用细杜邦线承载电机相电流。
- 固定电机**定子/底座**，转子可自由小角度转动；首次不装重旋钮，不用手抓转子。不在带电或转动时插拔线。
- B1 不再承担持续许可或急停功能；不能以松开 B1 作为关断操作。软件使用 STOP/失联保护，物理停止使用动力电源断开。示波器测相电压须用合适差分方式，普通接地夹不能夹到相输出。
- 若评估板的必要排针/端子未焊接，需合适转接件或焊接；不能仅凭型号保证完全免焊。

## 4. 当前续测入口与后续放行条件

**H25 已完成受限台架 smoke；当前停在硬关断、校准无效状态。** 后续测试必须在新证据目录执行有限动作，不能循环 CLEAR / 重试或把原始失败改为 PASS。最新结果以 [HAPTIC25 报告](../../../docs/bench-validation-20260907-haptic25-cn.md)为准。

1. `STATUS` 核对停止；缓存诊断不启用输出，`CURRENT_DIAG` 读取实时 ADC 与首故障，`ENC_FIELD` 只读磁场，`TIMING_DIAG` / `CURRENT_DIAG` 只能在输出关闭时使用。
2. PREPARE 唤醒电子电路并校零，不启用 PWM；就绪为 `mode=1 health=63 moe=0 off=0 zero=1 drv=0`，六路 PWM 输入低。校零/健康丢失就中止，不用 CLEAR 掩盖。
3. ALIGN 约 4.5 s、机械净位移约一极距（51.4°），不是无运动测量；成功须收到 `OK ALIGNED_RAM_ONLY`。H25 已有三次成功，但每次仍必须检查本次资格。
4. 同一校准会话内才可尝试受限 IQ 或 HAPTIC。`HAPTIC`：0 自由、1 阻尼、2 档位、3 弹簧、4 限位、5 摩擦、6 惯性、7 速度；本轮各 250 ms 只证明链路 smoke，不证明手动扰动下的完整手感。
5. 阶梯动作之间在零输出状态等待稳定：保留原 1500 mrad/s 启动门，最多 3 s，要求 5 个连续健康点且角度跨度≤10 mrad。超时或错误立即停止，不在等待时发出力/续租命令。
6. 故障先保存诊断再 STOP；CLEAR 只在明确原因后使用，保留首故障但清校准。STOP/复位也使校准失效，B1 不是急停。不要执行要求拆开 EVM 的 BREAKTEST / USB-only 看门狗实验，也不要用 UART BREAK。

本机当前端口 COM4，不要同时开两个串口工具：

```powershell
Set-Location 'G:\Agent\GL30-Haptic-Control\firmware-stm32\bench\NUCLEO_G474RE_FOC'
.\evm_console.ps1 -Port COM4 -StatusOnly
# 已确认接线的既有台架诊断入口；本次只读 enc_field/current_diag，然后 quit：
.\evm_console.ps1 -Port COM4 -ConfirmBenchWiring
# 仅在确实关闭 TI 动力后，才可使用要求此确认的旧无动力回归：
.\test_encoder_off.ps1 -Port COM4 -ConfirmDriverPowerOff -DurationSeconds 120 -SelfTestRuns 3
```

历史单次硬件脚本位于 `tmp/bench`，原始证据保留在 `output/bench`；它们拒绝覆盖已有结果，不是自动循环的运动测试入口。控制台若获准出力，会每约 30 ms 续发 KEEPALIVE、约 250 ms 请求状态；Windows 调度卡顿也会触发 100 ms 失联关断，不扩大超时来掩盖。`BREAKTEST` 仅供断开 EVM 的裸 NUCLEO、VM<1 V 且 nSLEEP=0 使用，本次不执行。串口 UART BREAK 也未重试：HAPTIC9 曾因此出现 BOOT，缓存复位标志仅 PINRSTF=1；原因尚未定位，HAPTIC10 的异常字节测试明确不含 BREAK。

## 5. 状态与故障怎么看

| 字段 | 含义 |
| --- | --- |
| mode | 0=OFF，1=PREPARED，2=ALIGNING，3=READY，4=ACTIVE，5=FAULT |
| health | 位和：1=DRV、2=编码器、4=ADC 零点/新鲜度、8=母线、16=nFAULT、32=时序；全好=63 |
| moe / off | STOP/FAULT 应为 `0 / 1`；PREPARED/READY 可为 `0 / 0`（驱动唤醒、六路 PWM 输入低）；定时器运行不等于功率输出打开 |
| zero / calibrated | 电流零点有效 / 已完成本次 RAM 电角校准，不是永久出厂标定 |
| self_* | 合成数据算法自检；不是接上了真实电机 |
| enc_irq | 编码器调度中断计数；计数增加不等于 SPI 读取有效 |
| enc_ok / enc_err | 通过全部检查的编码器读取组数 / 失败组数；持续检查应有效计数增长、错误不增加 |
| age_us / diag | 最后有效编码器数据的年龄 / 诊断字；没接编码器时年龄增长、诊断 0 正常地禁止启动 |
| deadline / adc_bad / uart_err | 时序超限 / ADC 同步异常 / 串口错误；正常联调应为 0 |
| iwdg_reset | 本次启动由独立看门狗复位；验收故障注入后为 1，不是正在反复复位 |

未接模拟线或 `zero=0` 时，`ia_ma` 等可能显示安培级偏置，这不是可信的相电流，不能用于调参。只能在 PREPARE 零点校准通过后解释电流值；母线电压也只是 ADC 换算，未替代仪表测量。

若 TI 动力关闭、编码器接通且自检完成，`health=34` 表示编码器位 2 和时序位 32 通过，不要求达到 63。合成自检进行中 `self_left>0` 会有意暂时撤销时序位；完成后应恢复 34，且 `self_fail=0 deadline=0`。此阶段必须始终 `mode=0 moe=0 off=1 calibrated=0`，不要运行仅供全拆线裸板使用的刹车/看门狗故障注入脚本。

`fault` 为位和：1=健康状态丢失，2=未使用（B1 门已移除），4=主机许可超时，8=校准不通过/超时，16=相电流门，32=执行窗口/时间，64=硬件刹车，128=UART，256=数值/内核保护，512=ADC 不同步，1024=无输出软件测试注入，2048=速度门。时序累计异常不会被 CLEAR 擦掉，需排查后复位重新自检。



`CURRENT_TRIP` 是冻结快照：`raw` 为 A/B/C/VM 原始 ADC，`zero_mc` 为故障时零点×1000；`outputs` 位 0/1 分别为故障关断前 MOE/nFAULT，不包含 DRVOFF。`captured=0` 时不解释空结构中的 sample=0 为故障。H25 关断回归故意注入软件 fault=1024 并验证 CLEAR 保留快照、但不恢复校准；最终活动 fault=0。历史 H19/H24 的真实 fault=32 另存原始报告，不能混淆。`LOOP_TIMING` 的 stage=3/4 终止分支可能保留旧 foc/trajectory 字段，不把它们当本帧成本；全局最大值看 `isr_max`。

## 6. 还不能声称完成的事

已验证 H25 的 PREPARE/电流校零、三次机械 ALIGN、双向短时 IQ、八类触觉 smoke、时序诊断和无出力命令/软件故障回归；这不等于全量出力波形、模拟增益/极性计量、力矩线性、手感/噪声、满幅/满载长时最大指令、nFAULT 外部整链、温升或回灌已验收。ESP32/AMOLED/无线整机与产品 CET6 端口仍未通过，不能用厂家额定/峰值参数代替这些证据。

下一阶段应优先补齐测量条件下的电流/力矩/手感、热与回灌证据及整机端口，而不是为扩大“通过”范围无限提高电流或延长无人值守运动。H25 已有的时序修复和受限通过证据不需要拆掉重做。

## 官方依据

- [TI DRV8316REVM 用户指南 SLVUBZ9B](https://www.ti.com/lit/ug/slvubz9b/slvubz9b.pdf)：J3/J4 表与原板原理图、VREF/Buck、采样分压。
- [TI DRV8316 数据手册](https://www.ti.com/lit/ds/symlink/drv8316.pdf)：SPI、CSA、保护和 Buck 配置。
- [ST NUCLEO-G4 UM2505](https://www.st.com/resource/en/user_manual/um2505-getting-started-with-stm32g4-series-nucleo64-board-stmicroelectronics.pdf)：Morpho 引脚表，当前核对版本 Rev 7。
- [AS5048A/AS5048B 数据手册](https://www.infineon.com/assets/row/public/documents/24/49/infineon-as5048a-as5048b-datasheet-en.pdf)：5 V/3.3 V 模式、SPI 流水线和诊断寄存器。
