# GL30 续测验收报告：2026-09-07

> 公开归档说明：正文记录当时状态；本地 `output/`、`tmp/` 位置以代码标记，不伪装成 GitHub 可点击文件。当前结论与本次公开的原始证据包见 [2026-09-08 扩展验收](bench-validation-20260908-haptic25-extended-cn.md)。

> **历史报告：本页记录 HAPTIC8，保留原始结果，不代表当前烧录镜像。** HAPTIC9 UART 修复及 HAPTIC10 的 9–15 V 窗口调整/ALIGN 位移故障见 [HAPTIC10 历史报告](bench-validation-20260907-haptic10-cn.md)；当前 HAPTIC25 的时序修复、ALIGN / IQ / 八模式 smoke 结果见 [HAPTIC25 续测验收](bench-validation-20260907-haptic25-cn.md)。下文旧 13.2 V 门、HAPTIC8 哈希和停止时间仅适用于当时测试。

## 1. 当前结论与范围

**阶段：`ADC_CLOCK_FIXED / POWERED_ALIGN_HOLD`。不能标记“所有主要功能通过”。**

- 真实台架为 NUCLEO-G474RE + TI DRV8316REVM + GL30 KV290（7 极对）+ AS5048A SPI；当前已刷写并完整回读确认的固件为 `20260907_HAPTIC8`。
- 全部续测、代码修改和最终复核由主线程执行；本轮没有调用其他模型或 Worker，也未要求人工在旁守台。
- 不删除文件，不清理既有未提交改动，不提交 Git；原始失败、旧固件、源文件快照与新结果分目录保留。
- 电源 305 mA 限流为用户报告，未用仪表核实，不等于相电流限流。未操作电源旋钮、开关或接线。
- 当前固件的校准仍失败。无 PWM 稳定性、协议拒绝和故障自动关断通过，不能替代带开关噪声的采样、电流环及真实触觉验收。

## 2. 主要功能验收矩阵

| 功能 | 已验证 | 本轮结论 / 边界 |
| --- | --- | --- |
| C 协议、FOC 数值/安全、状态门、DRV/编码器诊断、触觉原语 | 6/6 测试程序；ASan+UBSan 同样 6/6 | PASS（主机软件；不是 6 项硬件认证） |
| TypeScript 协议/Core/Profile/模拟器 | 4 个 workspace 类型检查；21/21 测试 | PASS（软件） |
| 无硬件端到端 | Profile 2/2；5 个有效解析帧/1 个预期坏帧；3 条应用/2 条丢弃；COMM_LOST 禁止力矩；4096×6 trace→205 bins | PASS（SIM_ONLY） |
| NUCLEO 固件与烧录 | Keil 0 error/0 warning；47288 字节镜像；完整 Flash 读回 SHA256 一致 | PASS（当前 HAPTIC8） |
| ADC 时钟配置 | 同源 PLLP 40 MHz + 异步 ÷1；实机 RCC/ADC common 寄存器精确核对 | PASS（配置修正，不等于已定位模拟异常全部根因） |
| AS5048A 无输出采样 | 3 轮自检，6144/6144 有效点；各轮相邻间隔全部 250 μs | PASS（该无输出采样窗口） |
| PREPARE / 校零 / 无 PWM 稳定性 | health=63、zero=1；60.477 s 准备态；无新增故障 | PASS（只唤醒电子电路，MOE=0） |
| 命令拒绝/解析恢复 | 56 个实机用例：范围、畸形/二进制/过长输入、拆包、8 类未校准触觉拒绝、重复 STOP 等 | PASS（未允许任何出力路径） |
| 软件故障锁存与清错 | FAULTTEST→1024；STOP 保留故障；CLEAR 保留首故障快照但不恢复校准；随后 IQ/HAPTIC 仍拒绝 | PASS（软件注入，不是外部 BKIN/过压电路认证） |
| 现有控制台诊断 | 新接入 ENC_FIELD / CURRENT_DIAG；自动提供只读输入并读取真实 COM4；退出后停止状态通过 | PASS（测试工具首轮作用域错误已修正并保留失败记录） |
| 机械/电角校准 ALIGN | 本版本仅 1 次；约 0.484 s 触发 HEALTH，未完成机械位移验证 | **FAIL / HOLD** |
| 正反向 IQ 电流环 | 当前版本未通过 ALIGN；本轮只测未校准拒绝路径 | **BLOCKED，不能报通过** |
| 8 类 HAPTIC 实物效果 | 软件预设与限幅测试通过；当前版本没有获准实际出力 | **BLOCKED，不能报手感/速度环通过** |
| 产品 GL30_AMOLED_V7 | 同步修正 ADC 时钟；独立 Keil 构建 0 error/0 warning | BUILD_ONLY，未烧到 NUCLEO；factory_encoder_pending 未放行 |
| ESP32、AMOLED/LVGL、无线、HID、OTA、完整 PC 采集/应用适配 | 仓库当前仍是框架或计划，ESP32 无可烧录 ESP-IDF 应用 | **未实现/未验收，不伪造测试成功** |
| 整机机械、热、回灌、寿命及外部保护链 | 本次未取得新的整机验证证据 | HOLD；台架局部通过不放行产品 G4/G5/G7 |

软件记录：最终回归清单（`../output/bench/haptic8-20260907/final-regression/validation.json`）、C 详细日志（`../output/bench/haptic8-20260907/final-regression/host-ctest.log`）、Sanitizer 日志（`../output/bench/haptic8-20260907/final-regression/sanitizer-ctest.log`）、TS/E2E 日志（`../output/bench/haptic8-20260907/final-regression/pnpm-verify.log`）。C 日志中的大数量为断言/枚举检查计数，不包装成同等数量的独立功能用例。

## 3. 本次修正及证据链

### 3.1 首故障快照（HAPTIC7，HAPTIC8 保留）

原先部分 ALIGN 终端失败/健康门失效会使 CURRENT_TRIP 留空或丢失首故障。增加独立首故障标记，所有相关关断先冻结当前 ADC 时间、同步状态、原始值/零点以及 fault、align_stage、align_move；CLEAR 保留证据而允许下一故障事件替换，新 ALIGN/初始化清空。平均电流非有限值不能通过 ALIGN。

回归先出现 13 个失败断言，修复后诊断测试共 420 断言通过；本次普通与 Sanitizer 构建均重新运行。见 HAPTIC7 证据目录（`../output/bench/haptic7-20260907/`）。这是诊断完整性修正，不是绕过故障。

### 3.2 ADC 并发时钟勘误（HAPTIC8 及产品工程）

依据本地归档的 ST ES0430 Rev 9，June 2024（`../output/bench/haptic8-20260907/ES0430.pdf`） 第 1、3、17–18 页，适用条款是 **§2.7.11**，选用同一时钟源、ADC 预分频 ÷1 的 workaround。实机 REV_ID=0x2003 是 **Rev X**。不能把 §2.7.9 的 >1 ms 首转换条件套到持续 50 μs 周期；§2.7.10 不影响 Rev X，不据此添加 dummy rank。

| 实机寄存器 | HAPTIC7 | HAPTIC8 |
| --- | --- | --- |
| DBGMCU_IDCODE | 0x20036469 | 0x20036469 |
| RCC_PLLCFGR | 0x01005032 | 0x41015032 |
| RCC_CCIPR | 0xA0000000 | 0x50000000 |
| ADC12_CCR | 0x00030000 | 0x00000000 |
| ADC345_CCR | 0x00030000 | 0x00000000 |

修正前只读证据（`../output/bench/haptic8-20260907/clock-before/validation.json`）、修正后只读证据（`../output/bench/haptic8-20260907/clock-after/validation.json`）、适用性判断（`../output/bench/haptic8-20260907/errata-assessment.json`）。读取前已 STOP，SWD 使用 HOTPLUG 只读，不停核；没有在出力时调试停核。

台架 main.c 与 IOC 同步改为 HSI/M4/N80/PLLP8；产品 main.c 与 IOC 为 HSE/M6/N80/PLLP8。两者 ADC 实际都维持 40 MHz，采样长度与 rank 不变。**台架仍 20 kHz，产品仍原有 40 kHz PWM；全部保护阈值不变。** 产品编译通过不等于该板硬件通过，也没有用产品固件替换 NUCLEO。

配置满足勘误的规避条件，但不能由此证明它就是之前大幅三相负偏移的唯一原因；新版本 ALIGN 仍失败，根因尚未闭合。

### 3.3 控制台与分析工具

- [evm_console.ps1](../firmware-stm32/bench/NUCLEO_G474RE_FOC/evm_console.ps1) 增加 ENC_FIELD/CURRENT_DIAG 两个诊断入口，不改变出力门。
- 首故障/编码器离线分析器（`../tmp/bench/analyze_haptic_capture.py`） 与准备态分析器（`../tmp/bench/analyze_prepared_soak.py`） 从保存的 CSV/JSON 重算，不访问硬件，不覆盖既有分析。
- 修正准备态脚本对 ordered dictionary 的控制台投影问题；原 validation.json 一直完整有效，旧测试未因打印 null 重跑。
- 控制台测试首轮 Read-Host mock 使用跨脚本 `$script:` 队列导致工具错误；改为本次独立进程的明确共享队列后通过。没有为使测试通过修改生产行为或断言。首轮错误记录（`../output/bench/haptic8-20260907/hardware/console-diagnostics/failure.json`）、修正后实机证据（`../output/bench/haptic8-20260907/hardware/console-diagnostics/retry-1/validation.json`）。

## 4. 真实硬件结果，不将失败解释为成功

### 4.1 HAPTIC4–8 阶段记录

| 版本 | 真实结果 | 证据 |
| --- | --- | --- |
| HAPTIC4 | HOTPLUG 下载首试失败，后来 under-reset 下载/完整回读通过；3 自检、256 次磁场读取通过，不代表校准通过 | 硬件目录（`../output/bench/haptic4-20260907/hardware/`） |
| HAPTIC5 | 单次 ALIGN 触发 CURRENT=16；捕获 470 个编码器点；自动停止 | 记录（`../output/bench/haptic5-20260907/hardware/align-capture/validation.json`） |
| HAPTIC6 | 200 ms 初始保持单独通过但未校准；完整 ALIGN 触发 ALIGN=8、位移不足，旧首故障快照留空暴露诊断缺陷 | 短保持（`../output/bench/haptic6-20260907/hardware/current-trip-probe/validation.json`）、完整尝试（`../output/bench/haptic6-20260907/hardware/align-capture/validation.json`） |
| HAPTIC7 | 首故障诊断修复后单次 ALIGN 触发 CURRENT=16；三相重算总和约 -0.953 A，提示测量链异常，不能靠滤波忽略 | 原始结果（`../output/bench/haptic7-20260907/hardware/align-capture/validation.json`）、重算（`../output/bench/haptic7-20260907/hardware/align-capture/analysis.json`） |
| HAPTIC8 | 时钟修正、无输出采样通过；单次 ALIGN 触发 HEALTH=1；随后 60 s 无 PWM 准备态和命令回归通过 | ALIGN（`../output/bench/haptic8-20260907/hardware/align-capture/validation.json`）、准备态（`../output/bench/haptic8-20260907/hardware/prepared-soak/validation.json`）、56 用例（`../output/bench/haptic8-20260907/hardware/off-command-regression/validation.json`） |

### 4.2 HAPTIC8 的单次 ALIGN 首故障

2026-09-07 16:19:38 UTC 开始该次会话；只发一次 ALIGN，没有失败后反复带电尝试。

```text
CURRENT_TRIP sample=3759799 us=187990019
raw=2018,1790,1799,1244
zero_mc=1871500,1873269,1873347
sync=1 bad=0 outputs=3 fault=1 align_stage=0 align_move_mrad=-71
```

按固件既有 3.3 V/4095、CSA 0.6 V/A、VM 分压 75 kΩ/6.04 kΩ换算：Ia=+0.19676 A、Ib=-0.11184 A、Ic=-0.09986 A，总和=-0.01493 A；VM=**13.45064 V**。该 VM 超过既有 13.2 V 健康门；故障时仍在初始静态场阶段，速度观测约 -0.282 rad/s。`outputs=3` 只表示故障关断前 MOE=1、nFAULT=1；该字段不含 DRVOFF。

故障证据说明软件门正确关断，**不证明电源真实过压，也不证明 ADC/模拟链已可靠**。尚不能区分真实母线瞬态、模拟地/VREF 干扰或采样异常。1935 个编码器点均有效、相邻间隔 250 μs，不是独立的轴运动测量。原始串口（`../output/bench/haptic8-20260907/hardware/align-capture/serial.log`）、CSV（`../output/bench/haptic8-20260907/hardware/align-capture/samples.csv`）、可复现重算（`../output/bench/haptic8-20260907/hardware/align-capture/analysis.json`）。

### 4.3 无输出采样与 60 秒准备态

- 3 轮无输出采样合计 6144/6144 点，各轮全部相邻 dt=250 μs。`self_max=2501`、历史 `isr_max=3381`，低于未改变的 3600 cycles 预算；`window_min=974`；deadline/adc_bad/enc_err/uart_err/self_fail/iwdg_reset 为 0。无输出数据（`../output/bench/haptic8-20260907/hardware/off-captures/analysis.json`）。
- 60.477 s 准备态中，主机读取 125 个 CURRENT_LIVE、记录 126 个 PREPARED STATUS；所有 STATUS 的 MOE=0，准备态 health=63/zero=1/fault=0。两端计数差 ADC=1,200,775、有效编码器=240,155；错误计数无新增。
- 125 个稀疏主机快照的 VM 推算范围 **12.01259–12.15315 V**；三相零电流 RMS 约 **3.16/3.09/2.62 mA**，最大绝对偏差约 11.38 mA。这不是连续 20 kHz 原始波形，也不是仪表实测精度。
- 16 次 ENC_FIELD 协议和诊断均有效；AGC 全为 255，magnitude 4505–4515。仅记录观测值，不据此宣称磁铁损坏、磁场裕量合格或编码器是全部根因。
- 准备态最终 STOP 并释放 COM4。离线分析（`../output/bench/haptic8-20260907/hardware/prepared-soak/analysis.json`）。

### 4.4 无出力拒绝路径与最终状态

2026-09-07 16:42:55 UTC 开始的 56 用例中，所有 IQ/HAPTIC 请求只在已断言 OFF/FAULT、未校准状态发送并验证拒绝；没有发送 ALIGN、BREAKTEST 或 DRV_TRACE。范围测试包含正负边界、超长整数、错误格式、二进制字节、过长行与后续恢复；软件注入验证 CLEAR 不恢复出力资格。完整串口日志（`../output/bench/haptic8-20260907/hardware/off-command-regression/serial.log`）。

控制台集成于 2026-09-07 16:52:50 UTC 完成；随后于 **2026-09-07 17:18:04 UTC** 独立执行 STOP / STATUS / CURRENT_DIAG / STOP / STATUS 并再次关闭串口，最终停止证据（`../output/bench/haptic8-20260907/hardware/final-stop/validation.json`）通过。最终真实 STATUS：`mode=0 fault=0 MOE=0 DRVOFF=1 zero=0 calibrated=0`，错误计数无新增；串口关闭，无后台运动任务。CURRENT_TRIP 此时保留的是上述测试故意注入的 **1024**，不是新电流/母线故障；HAPTIC8 真实 HEALTH 首故障已单独归档，不能混淆。软件 STOP 不代表外部电源已断开。

## 5. 构建身份与复核

| 镜像 | 字节数 | SHA256 | 使用边界 |
| --- | --- | --- | --- |
| NUCLEO HAPTIC8 | 47288 | `A6C5D89C8D6FA9FCC0F7CD5F8DE5057BC7A1D400B89BE0D2D4BF9766A2375665` | 已烧录，完整读回一致 |
| 产品 ADC_PLLP40 | 31320 | `B58B68085349C31FF57B13B902E2460A88FB8B5E20EA936C150C3E922644CEEF` | 只构建，未刷硬件 |

台架源码清单（`../output/bench/haptic8-20260907/build-validation.json`）、烧录/完整回读（`../output/bench/haptic8-20260907/hardware/flash-validation.json`）、产品源码与构建清单（`../output/bench/haptic8-20260907/product-clock/build-validation.json`）。已核对的安全参数维持：相电流 0.35 A、ALIGN 0.20 A、矢量 0.30 V、母线 9.0–13.2 V、速度 8 rad/s、IQ/HAPTIC ±100 mA、失联 100 ms、ALIGN 超时 6 s、ISR 3600 cycles。B1 持续按住门早已移除，本轮没有恢复；也未继续放宽上述保护。

最终独立审计（`../output/bench/haptic8-20260907/final-audit.json`）逐项重算台架 19 项和产品 317 项构建清单记录、两套镜像及 Flash 回读的 SHA256，核对控制台源码身份、main/IOC 变更边界、原始故障换算、测试日志与本文链接。清单项有跨工程重复，不表示 336 个独立模块。该审计不访问硬件，也不把 ALIGN 的 HOLD 改成通过。

最终停止检查距控制台集成的两端计数差为 ADC +30,267,206、有效编码器 +6,053,442；错误计数无新增。这里只是两端累计计数核对，不是这段时间的连续原始波形。停止后的 zero=0、CURRENT_LIVE valid=0；未校零的电流字段不作真实相电流测量。

## 6. 继续推进所需的真实条件

1. 先获取可区分真实 VM 瞬态与 ADC/模拟链异常的证据，例如可信 VM、VSENVM、CSA/VREF 波形或更密集的故障前原始采样；不能凭单帧推断直接改滤波、提高电压/电流门。
2. 出力采样问题闭合后，才做有边界且记录完整的 ALIGN；成功后按顺序验证短正反 IQ，再验证 8 类触觉。短时启动/完成、软件自检和拒绝路径都不能代替这一步。
3. 产品仍保留 factory_encoder_pending；ESP32 传输/界面和整机功能需要真实实现与各自构建、E2E 及硬件证据。不能靠台架通过跳过 [硬件闸门](hardware-gates.md)。

以上是实际完成与真实阻塞，不把未实现、未测或已失败的项目计为通过。
