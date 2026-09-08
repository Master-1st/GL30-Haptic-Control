# HAPTIC25 扩展实机验收（2026-09-08）

> **结论：有限台架覆盖通过，不是整机、满载或完整手感验收。**
> 固件仍为 `20260907_HAPTIC25`；本轮没有修改板上固件，没有改变供电限流/接线，没有关闭关键保护，也没有调用其他模型。

## 1. 本轮新增实测

运行测试时间：2026-09-08 04:27:46–04:29:25 UTC（纽约 00:27:46–00:29:25）。

| 项目 | 实际覆盖 | 结果 |
| --- | --- | --- |
| 重新准备和校准 | 2 次 PREPARE/零偏资格/ALIGN | 两次成功；不是复用 STOP 前的校准 |
| 指令最短边界 | IQ +1/-1 mA 各 1 ms；FREE 1 ms | 每项 21 个 ADC 总采样 |
| IQ 持续时间上限 | **仅 +1 mA**，2000 ms | 正常定时关断；不代表 ±100 mA 连续 2 s |
| HAPTIC 持续时间上限 | **FREE 零触觉指令**，10000 ms | 正常定时关断；不是全部模式满幅 10 s |
| 八类模式 | kind 0–7 各 1000 ms | 有限、无外部人工施力轨迹 smoke |
| 重复运行 | FREE 50 ms ×20 | 每次重新验证 READY/零输出稳定，没有重复 ALIGN |
| ACTIVE 命令拒绝 | PREPARE、ALIGN、IQ、HAPTIC、CLEAR、SELFTEST、FAULTTEST、CURRENT_DIAG、ENC_DIAG | 9 项拒绝；每项后验证仍为原 FREE ACTIVE |
| 运行中 STOP | FREE ACTIVE → STOP | mode=0、MOE=0、DRVOFF=1；zero/calibrated 均失效 |
| STOP 后误重启 | 未 PREPARE/ALIGN 即发 IQ/HAPTIC | 两项拒绝，桥不重新使能 |
| 主机失联/租约 | FREE 1000 ms 启动后**不发送 KEEPALIVE** | 2001 ADC 总采样后锁存 **fault=4**，约 100 ms 关断 |

共 **37 个实机案例、37 组冻结 ADC/RAM 采样**（包含 2 次 ALIGN、33 次定时动作、1 次 ACTIVE/STOP、1 次租约故障）。37 组均通过独立 RAM/MAP/CSV 一致性审计。租约故障在审计中仍保留 `FAULT_RECORDED`，由明确测试意图判为预期结果，未将其抹成“无故障”。只有在故障码、时间、冻结快照、硬关断都验证后，执行了一次 CLEAR；没有对意外故障循环清错。

### 无出力压力测试

最终完整运行发生于 2026-09-08 04:34:11–04:34:27 UTC：

- 2000 个带序号 PING/PONG，8 命令一组，验证每个响应顺序和值。
- 11 个分片位置的串口命令重组。
- 3 个独立坏帧：超长、NUL、DEL；逐个拒绝、换行后重新同步，不拼接执行 PREPARE。
- 50 次 PREPARE→校零就绪→STOP；每次确认无 PWM、校准无效。
- 20 次 PREPARE 紧接 STOP；再等 100 ms 确认不会延迟重新准备/使能。
- 编码器字段只读诊断有效；没有 UART BREAK、故意 RX 溢出或拆线操作。

**测试脚本自身也发现并修正一个问题：** 首次 off-transport-1 的 PowerShell 数组优先级把 3 个坏帧合成了 1 个，原脚本误报整体 PASS。独立计数发现覆盖不足；原结果、原脚本和 [覆盖审查](evidence/haptic25-20260908/coverage-correction.json)均保留。修正每个数组元素括号，并增加精确测试计数断言后，off-transport-2 的全部计划项通过。它是测试覆盖缺陷，不是板上故障；不使用首次运行证明 3 类坏帧通过。

## 2. 原始证据边界

- 保留的 ADC 段均为 50 us 间隔，无同步错误。采样换算母线范围约 **11.969–12.207 V**，不是外部仪表母线计量。
- 33 个定时动作保留段的最大原始相电流约 **0.04024 A**；不等于整段最大值或力矩计量。
- 全局 ISR 最大值仍为 **3351 cycles**；deadline/ADC/encoder/UART 错误均无新增且最后为 0。
- 长动作只保留最后 512 ADC / 2048 encoder 样本，**不是全程波形**。持续时间使用固件总采样计数核对，不用电脑等待时间代替 MCU 定时证据。
- 前一轮双向 ±10/25/50/100 mA 各 20 ms、八模式各 250 ms、60.264 s 无 PWM 和 58 项关断回归见 [H25 基线](bench-validation-20260907-haptic25-cn.md)。不重复虚增这些计数。
- 输入电源约 300–305 mA 仅为用户报告，没有外部电流/功率/温度仪表读数。本轮没有尝试提高输入限流，输入电流与相电流不可混同。

## 3. 最终硬件状态

[最终关断记录](evidence/haptic25-20260908/final-off.json)始于 **2026-09-08 04:35:25.944 UTC**：

- mode=0、fault=0、MOE=0、DRVOFF=1、zero=0、calibrated=0。
- SWD HOTPLUG 确认 TIM1 通道使能=0、六路 PWM 输入与输出锁存均低；nSLEEP=1。
- 整个 60136-byte 镜像读回 SHA-256 仍为 `B98161DB5EDAE02EBCE29F0F645405C2DA73CC1B99387A67D55181E98964DFB7`。
- 串口已释放；没有物理断开外部电源。STOP 后约 2.9 A 的 STATUS 相电流是无有效零偏的换算值，不能当成真实出力电流。

## 4. 复现与公开证据

- [源码身份清单](evidence/haptic25-20260908/source-identity.json)：37 个项目验收文件，同时保存原始字节 SHA-256 和 UTF-8/LF 规范化 SHA-256。Git 换行转换不再误报源码改变。原 38 项验收清单中的 1 个 CMake 自动生成编译器探测文件不是项目源码/MCU 镜像输入；公开清单明确记录该排除项，原始验收记录不改写。
- [本轮摘要](evidence/haptic25-20260908/extended-summary.json)、[独立审计](evidence/haptic25-20260908/independent-audit.json)、[无出力压力测试](evidence/haptic25-20260908/off-transport.json)。
- [H25 基线验收](evidence/haptic25-20260908/baseline-acceptance.json)。源码/测试与精简、去本机路径证据随仓库发布；完整保留的冻结 RAM/CSV/串口日志在 [H25 台架预发布](https://github.com/Master-1st/GL30-Haptic-Control/releases/tag/bench-haptic25-20260908)的 evidence ZIP。解压到仓库内的一个新目录后可离线复核，原始 programmer/Keil 私人日志和无关 CAD 不发布。

离线审计（不会接触硬件）：

```powershell
python firmware-stm32/bench/NUCLEO_G474RE_FOC/tools/verify_source_identity.py docs/evidence/haptic25-20260908/source-identity.json
python firmware-stm32/bench/NUCLEO_G474RE_FOC/tools/audit_frozen_captures.py --build output/h25-evidence/build --captures output/h25-evidence/extended/run-1 --out output/h25-evidence/audit-new.json
```

实机测试工具是 `firmware-stm32/bench/NUCLEO_G474RE_FOC/tools/test_extended_h25.ps1` 和 `test_off_transport_h25.ps1`。前者要求显式 `-RunBoundedHardware`、串口、Python、CubeProgrammer、H25 构建证据目录和**全新**输出目录；先校验镜像/源码/零偏/就绪，再允许有限动作，finally 发 STOP。不能在不确定接线或换硬件后盲目运行。工具不会刷写、调电源、发送 BREAK、永久保存校准或移除保护；不能替代物理装配检查。

软件发布验收在隔离工作树执行：Debug、ASan+UBSan、Release+LTO 各 7/7、TypeScript 21/21 与 SIM_ONLY E2E、编码器查看器纯单元测试，以及 Keil 两目标构建。最终结果见同目录 `publication-validation.json`，未执行的项目不能标成通过。GitHub CI 只证明软件，不重复声称它运行了本地硬件。

## 5. 尚未具备条件的验证

完整手感/力矩与噪声计量、全角度/外力条件覆盖、模拟电流增益与极性计量、满电流长时/堵转/过载、热、回灌/电源裕量、外部 nFAULT→BKIN 整链、实际失电/IWDG 整链、CET6 端口、ESP32/AMOLED/无线整机均未由本轮证明。没有可用的外部测量链或未接入的整机，不通过提高阈值、软件注入或仿真把这些项目变成“已通过”。
