# GL30 HAPTIC25 续测验收：时序修复、双向电流阶梯与八模式 smoke

> 公开归档说明：正文记录当时状态；本地 `output/`、`tmp/` 位置以代码标记，不伪装成 GitHub 可点击文件。当前结论与本次公开的原始证据包见 [2026-09-08 扩展验收](bench-validation-20260908-haptic25-extended-cn.md)。

> **阶段：`BOUNDED_BENCH_SMOKE_PASS`，不是整机或全功能放行。**
> H25 实机测试主要发生于 **2026-09-08 03:08–03:32 UTC（纽约 2026-09-07 晚）**；版本标记/目录沿用 `20260907`。本页接续 [HAPTIC10 历史报告](bench-validation-20260907-haptic10-cn.md)，历史失败没有改写成通过。

机器可读最终验收：final-acceptance.json（`../output/bench/haptic25-20260907/final-acceptance.json`）。它逐项断言源码/快照/镜像一致、回归结果、原始采样、旧失败保留和最终关断，而不是仅汇总 PASS 字符串。

## 1. 本次结论

- 板上为 **`FW=20260907_HAPTIC25`**，硬件是 NUCLEO-G474RE + TI DRV8316REVM + GL30 / AS5048A；不是产品 CET6 镜像。
- **三次真实 ALIGN 成功**；双向 `IQ ±10/±25/±50/±100 mA` 各 20 ms、八种 `HAPTIC 0..7` 各 250 ms 的受限 smoke 通过。旧 H19 失败的 `IQ -100 20` 已在 H25 通过。
- **58 项关断命令回归、60.264 s 无 PWM 准备态、21 组冻结采样独立审计通过。** 本轮 H25 实机输出测试没有新增 `deadline/adc_bad/enc_err/uart_err`；最后均为 0。
- H25 相对 H24 的解决办法是**把机械 observer 计算移出 ADC 实时路径**，不是提高电流/电压或放宽 ISR / PWM 窗口门。本轮没有改变外部电源限流、开关或接线，没有调用其他模型/Worker，没有删除文件或提交 Git。
- 用户报告的电源限流约 **300–305 mA** 未经仪表核实。H25 在外部设定未改的条件下通过；现有故障记录不支持“先加大电源电流才能解决此次失败”。没有实测输入电流/功率，不能把口述“约 3.6 W”当作供电裕量或相电流验收。

**尚未通过：** 完整手感/力矩计量、最大指令时长、长期温升、回灌、外部 nFAULT 整链、产品 CET6 编码器端口、ESP32/AMOLED/无线整机。不得将本页 smoke 泛化成这些项目完成。

## 2. 镜像身份与最终输出状态

| 项目 | 已核对值 |
| --- | --- |
| 固件 | `20260907_HAPTIC25` |
| BIN 长度 | 60136 bytes |
| BIN SHA-256 | `B98161DB5EDAE02EBCE29F0F645405C2DA73CC1B99387A67D55181E98964DFB7` |
| MAP SHA-256 | `88EBE85A86C84718A124E51E811C845BEC27F2B4AE431EBD306BD6083206D8E8` |
| AXF SHA-256 | `D9249145CE67C9D26D178752CA534B398451FABCCCF913BC5B113AF4374C3F7A` |
| 源码与快照 | 38 个文件哈希一致；AXF 再导 BIN 一致 |
| Keil | 0 Error / 0 Warning |
| 最终读回 | HOTPLUG 整个 60136-byte 镜像区 SHA-256 与 BIN 一致 |
| 最终状态 | `mode=0 fault=0 MOE=0 DRVOFF=1 zero=0 calibrated=0` |
| SWD 引脚/寄存器 | 通道使能=0、六路 PWM 输入/输出锁存均低、nSLEEP=1 |

证据：artifact-validation（`../output/bench/haptic25-20260907/artifact-validation.json`）、build-validation（`../output/bench/haptic25-20260907/build-validation.json`）、final-off-2（`../output/bench/haptic25-20260907/hardware/final-off-2/flash-validation.json`）。最后关断验证记录始于 **2026-09-08 03:31:56.504 UTC**，脚本退出码 0 并关闭 COM4；没有物理切断外部电源。

STOP 会让零点失效，随后 STATUS 中约 2.9 A 的 `ia_ma/ib_ma/ic_ma` 是**未经有效零偏扣除的读数，不能解释为真实相电流**。`iq_ma/id_ma` 也可能保留末次计算值；最终关断以状态门、MOE、通道和引脚证据判断，不拿这些旧值继续调参。

## 3. 软件与无出力验证

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| host C 回归 | 7/7 | host-ctest（`../output/bench/haptic25-20260907/delivery-regression-2/host-ctest.log`） |
| sanitizer 回归 | 7/7 | sanitizer-ctest（`../output/bench/haptic25-20260907/delivery-regression-2/sanitizer-ctest.log`） |
| Release + LTO host | 7/7 | LTO 复跑（`../output/bench/haptic25-20260907/host-lto-test-2.log`） |
| `pnpm verify` | 4 个 workspace 类型检查、21/21 TS、SIM_ONLY E2E 通过 | pnpm 日志（`../output/bench/haptic25-20260907/delivery-regression-2/pnpm-verify.log`） |
| CORDIC 板上启动自检 | 65537 点、0 失败；max=225 cycles；最大误差=1907 ppb、norm=3993 ppb | soak 串口（`../output/bench/haptic25-20260907/hardware/prepared-soak/serial.log`） |
| PREPARED 无 PWM soak | 60264 ms；采样总计 1207832、冻结末 512 点；未触发 idle outlier | soak（`../output/bench/haptic25-20260907/hardware/prepared-soak/validation.json`）、原始读取（`../output/bench/haptic25-20260907/hardware/prepared-soak/current-history/validation.json`） |
| 关断命令回归 | 58/58；STOP 幂等、非法行/范围、未校准拒绝、故障锁存、CLEAR、无出力 SELFTEST | off-command-regression-3（`../output/bench/haptic25-20260907/hardware/off-command-regression-3/validation.json`） |

软件回归最新一次在 2026-09-08 03:46 UTC 重新执行；没有通过模拟器结果宣称屏幕、无线或产品硬件通过。sanitizer / host 的外设 fixture 仍是模拟外设，不能替代板上时序证据。

关断回归在 `MOE=0 / zero=0 / calibrated=0` 下执行 `FAULTTEST`，故意注入 **1024**；验证命令拒绝和 STOP 保留锁存，随后 CLEAR 只清该软件注入，**不恢复校准且保留首故障快照**。最终活动 fault=0；残留 snapshot 的 fault=1024 不是新的电机故障，也不是外部 nFAULT / BKIN 整链认证。没有执行 UART BREAK、需拆 EVM 的 BREAKTEST 或 USB-only 看门狗实验。

## 4. ALIGN、IQ 和触觉实机结果

### 4.1 三次 ALIGN

| 测试会话 | 事件 | ALIGN 结束 STATUS 净位移 | 后续 |
| --- | --- | --- | --- |
| `align-capture` | `OK ALIGNED_RAM_ONLY` | -901 mrad | ±10 mA 与八模式通过 |
| `current-ladder` | 同上 | -857 mrad | +25 mA 通过；下一个指令发送前资格门暂停 |
| `current-ladder-2` | 同上 | -923 mrad | 六项 ±25/50/100 mA 全通过 |

三次均保留真实电流资格、机械净位移门与 RAM-only 方向/零位，没有伪造校准。ALIGN 约 4.5 s、ADC 总计 90001 点；原始 ADC ring 仅保存末 512 点，**不是全段模拟波形**。主 smoke 会话静态最后 100 ms 重算平均 Id 约 0.331778 A，Vd 约 0.380004 V。

### 4.2 六项电流阶梯

每条指令 20 ms，ADC 保留全部 401 个点（首末时间跨度 20000 μs，包含终止边界样本）。下面“独立近似 Iq”由原始 ADC、现有 CSA 矩阵和最近的前向编码器采样角度重建，取可重建样本后半段均值。

| 指令 | ADC 点数 | 串口末帧 Iq / mA | 独立近似 Iq / mA | 原始三相绝对峰值 / A | 结果 |
| --- | ---: | ---: | ---: | ---: | --- |
| `IQ 25 20` | 401 | 23 | 23.09 | 0.037250 | 通过 |
| `IQ -25 20` | 401 | -29 | -24.25 | 0.036620 | 通过 |
| `IQ 50 20` | 401 | 45 | 48.45 | 0.060883 | 通过 |
| `IQ -50 20` | 401 | -49 | -49.49 | 0.058110 | 通过 |
| `IQ 100 20` | 401 | 93 | 95.27 | 0.113264 | 通过 |
| `IQ -100 20` | 401 | -90 | -93.07 | 0.111034 | 通过 |

证据：current-ladder-2（`../output/bench/haptic25-20260907/hardware/current-ladder-2/validation.json`）、独立原始审计（`../output/bench/haptic25-20260907/independent-audit.json`）。末帧不是均值；近似重建不是 bit-exact ISR observer 回放，也不是电流跟踪精度或力矩认证，不按本表额外扩大电流上限。

首次阶梯的失败记录原样保留（`../output/bench/haptic25-20260907/hardware/current-ladder/validation.json`）：+25 mA 已完成，下一个 -25 mA **未发送**，因为一个零输出 STATUS 的速度为 1507 mrad/s，超过原 1500 mrad/s 启动门。重试仅改测试调度：零输出等待最多 3 s，100 ms 采样，要求 5 次连续健康且角度跨度≤10 mrad。没有放宽速度门，没有在等待期间给电或续租；调度离线验证（`../output/bench/haptic25-20260907/settling-script-test.log`）确认超门/故障拒绝。

### 4.3 八种触觉

`HAPTIC 0..7 250` 分别覆盖自由、阻尼、档位、弹簧、限位、摩擦、惯性、速度，全部达到正常定时终止；每项 ADC 总计 5001 点，保留末 512 点。另有 `IQ ±10 20` 各 401 点通过。

证据：align-capture（`../output/bench/haptic25-20260907/hardware/align-capture/validation.json`）。**这是 `TIMED_HARDWARE_SMOKE_PASS_NOT_TACTILE_METROLOGY`**：证明八个入口在实际受限控制链上可运行并退出。自由/阻尼/弹簧等在无人工扰动、接近中心时可能只有很小电流，因此不能证明全角度效果、清晰度、力矩线性或主观手感。

## 5. 为何修时序，而不是再加电流

| 历史版本 | 保留的事实 | 对本轮的含义 |
| --- | --- | --- |
| H10 | 9–15 V 窗口调整后 ALIGN 最终净位移不足，fault=8 | 历史结论，不是 H25 当前状态 |
| H19 | ALIGN 与八模式通过；-100 mA 时只有 2 个 ADC 点即 fault=32，PWM 窗口 reason=2、CNT=267 | 原始相电流峰值仅 0.005407 A，采样 VM≈12.1099 V；本次可见故障是窗口，不是相电流不够 |
| H20 / H21 | H20 再次 ALIGN 故障；H21 仅完成无出力验证 | 不把“改过优化”当出力通过 |
| H22 | ALIGN 通过但 +10 mA 失败；首发总预算 reason=3 | 仅开启 LTO 仍不足以完成当前出力路径 |
| H23 | CORDIC 数学自检门过严，启动即拒绝且无输出 | 失败保留，不解释成电机出力测试 |
| H24 | 数学自检和无出力通过；ALIGN fault=32，reason=2、CNT=260 | CORDIC 加速后仍有 observer 路径挤占 ADC 写入窗口 |
| H25 | 移动 observer 计算；原限额内三次 ALIGN 和所有本页短动作通过 | 改变的是执行位置/并发发布，不是把保护门调到失败值之外 |

H19 与 H24 的 原始独立审计（`../output/bench/haptic19-20260907/independent-audit.json`）、H24 审计（`../output/bench/haptic24-20260907/independent-audit.json`）分别核对 19 / 2 组 capture，仍明确 `FAULT_RECORDED`。**证据读取一致性 PASS 不等于功能 PASS。** H24 的 total=4067 包含 fault 处理开销；必须看首发 reason 和写入窗口，不能把所有超总数都误判成首发 reason=3。

H23→H24 的数值残差验收依据是本地 STM32CubeG4 v1.6.3 官方 CORDIC_CosSin 例程，readme（`../output/bench/haptic24-20260907/st-reference/CORDIC_CosSin-readme.txt`） / 源文件（`../output/bench/haptic24-20260907/st-reference/CORDIC_CosSin-main.c`）。这是 CORDIC Q1.31 数学残差验收，不是电机保护放宽；本页不引入未核实的在线表号或芯片耐压推断。

### H25 最小改动及验证

- [bench_app.c](../firmware-stm32/bench/NUCLEO_G474RE_FOC/Core/Src/bench_app.c)：4 kHz observer 在较低优先级 encoder IRQ 计算，使用两个 164-byte 状态缓冲；ADC priority=1、encoder priority=2，ADC 可抢占 encoder。
- 先记录真实采样时间，在未关中断区复制/计算 inactive buffer，再在短临界区发布 index / angle / timestamp / seq。无效 SPI 不发布有效帧。
- ADC 仅在新序号出现时复制 7 个 observer 字段，保留校准方向/零位覆盖，**不复制 PI、当前命令或触觉状态**。CORDIC 运行时仍只由 ADC 使用，不引入跨 IRQ 竞争。
- [专项测试](../firmware-stm32/tests/bench_app_diagnostic_tests.c)覆盖首帧/连续帧、±π 与计数回绕、坏 SPI、三个抢占插入点、800 μs 延迟/时钟回绕的 freshness 拒绝、校准与 PI/命令状态所有权。
- 反汇编（`../output/bench/haptic25-20260907/disassembly.txt`）核对 inactive copy 和 observer 调用均发生在发布 mask 之前；需按分支控制流看，不能只按地址排序。
- 全局最大实测 `isr_max=3351` cycles，原预算仍 3600；160 MHz 下约 20.94 μs vs 22.50 μs。这个数据只覆盖本轮已执行路径，**不是所有干扰组合的最坏执行时间证明**。

早期 concurrency-review.json（`../output/bench/haptic25-20260907/concurrency-review.json`）生成于刷写前，所以 `TargetTimingProof=PENDING` 保留为历史；本轮实机证据与最终验收中的 `OBSERVED_EXERCISED_PATHS_PASS` 补齐了已执行路径验证。`LOOP_TIMING` 的终止 stage=3/4 可能保留前一帧 foc/trajectory 字段，不把旧值当本帧执行成本。

## 6. 当前限额与不可混淆的量

| 量 | H25 当前值 |
| --- | --- |
| 母线健康窗口 | 9–15 V，有限值检查保留；沿用 H10 后窗口，未做整个窗口的物理扫压 |
| 普通相电流停止门 | 0.35 A |
| ALIGN 相电流停止门 / 电压上限 | 0.65 A / 0.60 V |
| ALIGN 实际控制 | 800 ms 渐升固定 Vd 至 0.38 V；1 s 静态、3 s 扫场、0.5 s 保持 |
| `ALIGN_CURRENT_A` | 0.40 A 是资格名义值，静态平均 Id 须 0.28–0.52 A；不是闭环目标指令 |
| 正常电压矢量上限 | 0.30 V，不是直流电源电压 |
| IQ / HAPTIC 命令幅值 | ±100 mA；不是外部电源输入限流 |
| IQ / HAPTIC 最大命令时长 | 2000 / 10000 ms；本轮只分别测试 20 / 250 ms |
| 速度 / 主机 lease | 8 rad/s / 100 ms |
| ADC / PWM | 20 kHz；ISR 3600 cycles；PWM 写入必须 DOWN 且 CNT≥320 |
| encoder / DRV SPI | encoder 4 kHz；SPI3 2.5 MHz、底部预约、150 μs 超时关断 |

最终验收脚本（`../tmp/bench/accept_haptic25_2.py`）逐个比较 H24 快照与 H25 当前关键 define，确认上述本轮没有放宽。台式电源输入电流、ADC 三相电流、dq 电流和电压矢量不是同一个量；本页仅报告实际采集到的对应量，不以电源功率估算取消本地相电流或电压门。

## 7. 独立原始数据审计及工具自身检查

审计器（`../tmp/bench/audit_frozen_captures.py`）不访问硬件：核对 BIN/MAP 哈希、按 MAP 独立解 RAM、比较两次冻结读取、重算 ring 顺序/CSV 全字段、零偏 sum/count、ADC flags / VM / 电流 / 间隔、编码器尾段、ALIGN 静态量及首故障。

H25 的 **21 组**组成：soak 1、主 ALIGN + smoke 11、首次阶梯 2、第二次阶梯 7。全部保留的 ADC 间隔为 50 μs、同步异常 0、保留段 VM 在 9–15 V 内、相电流未超过各自门，编码器保留段无 invalid；`Captured=0` 时首故障输出为 null，不把空结构的 sample=0 当故障。

审计对抗检查（`../output/bench/haptic25-20260907/audit-negative-fixtures/validation.json`）共 **9 项**：7 种破坏（CSV 原始值、第二次 RAM、ring count、零偏累加值、reader 汇总、伪造首故障、MAP 哈希）均被拒绝；原样 H25 对照通过且不凭空造故障，真实 H19 fault32 对照仍标故障。原证据哈希未变，所有破坏 fixture 另存且不删除。

工具/调度失败同样保留：

- `current-ladder` 的未稳定资格失败没有改 PASS；`current-ladder-2` 是新证据目录。
- 首次 `off-command-regression-2` 缺少 final-off 前置文件，在串口打开前退出；待 final-off 成功后使用 `off-command-regression-3`，不是覆盖重跑。
- 首次最终汇总日志（`../output/bench/haptic25-20260907/acceptance-console.log`）因 Windows PowerShell UTF-16 日志被按 UTF-8 读取而失败。修正为明确 BOM 解码后，第二次汇总（`../output/bench/haptic25-20260907/acceptance-2-console.log`）通过，断言未弱化。
- 新 observer 坏 SPI 用例曾暴露测试 fixture 未 reset `g_encoder_errors`；原失败（`../output/bench/haptic25-20260907/observer-test-fixture-failure.log`）保留，修正测试初始状态后完整重跑，没有改生产门。

## 8. 验收边界与下一步

**已验证**的是当前 NUCLEO/TI 台架的受限基本控制链、短时双向电流、八个模式入口和关断命令。**未验证**的真实缺口包括：

1. 电流计量精度/全段波形、手动扰动下的力矩和八模式手感/噪声、最大时长和反复加载。
2. 电源实际限流/输入功率、温升、回灌、外部 nFAULT→BKIN 的完整电气链路与示波器证据。
3. 产品 CET6 的 `factory_encoder_pending` 端口、ESP32/AMOLED/无线、屏幕/音量/定时器整机交互。

继续工作的优先级是补测量条件和整机端口，不是无限加大电流或延长无人值守运动。已有台架通过项不需要为了“全功能”而拆掉；未接入/未计量部分不能用 smoke 或代码存在替代验收。

离线可复核入口为 `audit_frozen_captures.py`（`../tmp/bench/audit_frozen_captures.py`）、`verify_frozen_audit_negatives.py`（`../tmp/bench/verify_frozen_audit_negatives.py`）、`accept_haptic25_2.py`（`../tmp/bench/accept_haptic25_2.py`）。它们拒绝覆盖已有报告/fixture；复跑应使用新输出，不删除原证据。台架脚本也是一次性有限动作，不能直接改成循环升流工具。
