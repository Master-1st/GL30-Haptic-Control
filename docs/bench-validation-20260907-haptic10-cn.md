# GL30 HAPTIC10 续测验收：母线窗口调整与 ALIGN 位移故障

> 公开归档说明：正文记录当时状态；本地 `output/`、`tmp/` 位置以代码标记，不伪装成 GitHub 可点击文件。当前结论与本次公开的原始证据包见 [2026-09-08 扩展验收](bench-validation-20260908-haptic25-extended-cn.md)。

> **历史报告：本页是 HAPTIC10 当时的失败与停机证据，正文不改写为成功。** 当前 HAPTIC25 已通过受限 ALIGN、短时双向 IQ 和八模式硬件 smoke，见 [HAPTIC25 续测验收](bench-validation-20260907-haptic25-cn.md)。下文“当前”只指 HAPTIC10 测试时刻。

日期：**2026-09-07 UTC**。当前状态：**`VM_WINDOW_UPDATED / ALIGN_TRAVEL_HOLD`**。本页接续 [HAPTIC8 历史报告](bench-validation-20260907-cn.md)，不是整机放行报告。

## 1. 当前结论

- 当前烧录的是 **`FW=20260907_HAPTIC10`**，NUCLEO-G474RE + TI DRV8316REVM + AS5048A 台架；不是产品 CET6 镜像。
- 已按用户授权把台架母线健康窗口 **9.0–13.2 V → 9.0–15.0 V（含端点）**。没有关闭电压有限值检查，也没有增加相电流、ALIGN 电流、输出矢量电压、速度、失联或 ISR 限额。
- 软件回归、无出力命令、UART 耐久/全双工拥塞/异常字节恢复和 60 秒无 PWM 准备态已通过。
- **HAPTIC10 只发过一次 ALIGN**。约 4.5 秒后因机械最终净位移不足报 `fault=8` 并关断；不再是旧的 13.2 V 健康门提前关断。**真实 IQ 与 8 类 HAPTIC 出力仍未通过，不伪装成全功能通过。**
- 所有工作由主线程完成，未调用其他模型/子代理；不删除文件、不提交 Git、不清理其他未提交改动；没有要求人工守台，没有操作外部电源开关或调大限流。
- 电源 **305 mA** 只是用户报告，未以仪表或电源接口核实；它不是相电流限额。

## 2. 变更、构建与镜像身份

生产修改限于台架 `bench_app.c`：新增 `VM_MIN_V=9.0f` / `VM_MAX_V=15.0f`，在 `health`、`PREPARE`、`DRV_LIVE`、`DRV_TRACE`、`Bench_DriverTraceSafe` 五个检查处统一使用，更新三条拒绝文本和 HAPTIC10 BOOT 标记。相对 HAPTIC9 的生产最小 diff（`../output/bench/haptic10-20260907/production-voltage.diff`）已保存。

| 保留项 | 当前值 |
| --- | --- |
| 相电流采样关断阈值 | 0.35 A |
| ALIGN 静态 d 轴目标 | 0.20 A |
| 电压矢量上限 | 0.30 V；不是直流母线电压 |
| IQ / HAPTIC 限幅 | ±100 mA |
| 速度 / 主机续租 | 8 rad/s / 100 ms |
| ISR 时间预算 | 3600 cycles |
| PWM / 编码器 | 20 kHz / 4 kHz |
| 校准成功条件 | 单向电角一圈后，机械净位移绝对值严格处于 2π/7 的 0.8–1.2 倍范围内 |

TI 官方 DRV8316 Rev.B 手册第 6 页列出 VM 推荐工作范围 4.5–35 V、绝对最大 40 V；本次没有按芯片极限放开窗口，也没有把芯片额定值当作整板、接线或回灌验收。官方原文已归档在本地 PDF（`../output/bench/haptic8-other-20260907/ratings/drv8316.pdf`），本轮也重新读取了 TI 官方 PDF。**9–15 V 是软件接受边界，实物未做全区间扫压；旧 13.451 V 采样的物理成因仍未由仪表闭合。**

| 镜像项 | 实际结果 |
| --- | --- |
| Keil | 0 error / 0 warning；Code 41876、RO 5312、RW 28、ZI 28060 |
| 文件 | `NUCLEO_G474RE_FOC_HAPTIC10.bin`，47224 字节 |
| SHA256 | `87ED9CB2FB54B44BF100FE6082F593EE6B5EDCEC655A42C092BBD0B5D25586D7` |
| 烧录 | 实际 BOOT 收到 HAPTIC10；编程校验及完整可执行镜像区读回一致 |
| 最终复核 | 再次读回相同 47224 字节并核对相同 SHA256；不是声称读取整颗 512 KB Flash |

证据：构建及 19 项源码哈希（`../output/bench/haptic10-20260907/build-validation.json`）、烧录记录（`../output/bench/haptic10-20260907/hardware/flash-validation.json`）、最终读回/停机（`../output/bench/haptic10-20260907/hardware/final-stop/validation.json`）。最终交付审计另列在审计 JSON（`../output/bench/haptic10-20260907/delivery-audit-final/validation.json`）。

## 3. 软件：先红后绿，最后独立复跑

母线新增测试覆盖 `8.999 / 9 / 12 / 13.450639 / 14.999 / 15 / 15.001 / NaN / +Inf / -Inf`；检查五个入口行为一致、接受电压不会自动开 PWM、越界不会配置/唤醒驱动。

1. 对旧 HAPTIC9 运行新期望：build=0，测试退出码 8，**15/1587 断言失败**，证明旧实现不满足新窗口。红测结果（`../output/bench/haptic10-20260907/voltage-red-result.json`）、红测日志（`../output/bench/haptic10-20260907/voltage-red-test.log`）。
2. 改生产实现后第一次回归仍失败：仅 BOOT 测试还期待 HAPTIC9，旧失败原样保留。更新为真实 HAPTIC10 标记后全部通过；没有弱化电压、校准或出力断言。首次失败（`../output/bench/haptic10-20260907/final-regression/validation.json`）、修正后通过（`../output/bench/haptic10-20260907/final-regression-2/validation.json`）。
3. 硬件测试结束后，于 **21:13 UTC** 再独立执行 build、CTest、Sanitizer、类型检查及 SIM_ONLY E2E，全部通过。最终复跑结果（`../output/bench/haptic10-20260907/delivery-regression/validation.json`）。

| 项目 | 已验证结果 | 证据边界 |
| --- | --- | --- |
| C 回归 | 6/6 测试程序 | 协议/FOC/状态门/驱动及应用诊断/触觉行为；不是 6 项实物功能验收 |
| ASan + UBSan | 同为 6/6 | 软件内存与未定义行为检查 |
| 应用 / 硬件诊断 | 1587 / 4937 次断言 | 包含正常、故障、时间回绕和拒绝路径；断言数不是独立硬件试验数 |
| TypeScript | 21/21；4 个 workspace 类型检查 | 协议、core、profile、模拟器 |
| SIM_ONLY E2E | Profile 2/2；5 parsed + 1 预期坏帧；3 applied / 2 dropped；COMM_LOST 禁止扭矩；4096×6 → 205 bins | 没有真实 ESP32、屏幕、无线或 PC 外设链路 |

本轮没有修改 fake `main.h`；新增行为测试与实际固件对应。测试专用脚本/历史日志保留，不用替换测试结果掩盖失败。

## 4. 唯一一次带 PWM 的 ALIGN

完整采集目录（`../output/bench/haptic10-20260907/hardware/align-capture/`）包括 validation、串口记录、24 条运行态 STATUS、2048 点冻结编码器缓冲及重算 analysis。

- PREPARE 和 12 次校零漂移检查通过。约 **20:45:53.7–20:45:58.2 UTC** 执行一次 ALIGN，没有自动重试。
- 1 秒静态段（前 0.4 秒渐升）后进入 3 秒平滑旋转场，再保持 0.5 秒。旋转段采用约 **0.266 V** 的已测固定 Vd。
- 24 条运行态 STATUS 的 `health=63`，未新增 HEALTH/CURRENT/DRV/ADC/ENC/deadline/UART 故障；`self_max=2534`、`isr_max=3407`，均低于 3600。
- 旋转起点约 **2.140 rad**，中途最低约 **1.678 rad**，随后回到约 **2.141 rad**。电场走完一电周期，转子的最终净位移在首故障记录中截断为 **0 mrad**。
- 预期机械净位移绝对值 **2π/7 ≈ 0.897598 rad**，接受开区间约 **(0.718078, 1.077117) rad**。实际不满足，固件正确锁存 **ALIGN fault=8** 并关断；不把拒绝成功当作校准成功。

冻结首故障：

```text
CURRENT_TRIP sample=642185 us=32109319
raw=2019,1786,1802,1119 zero_mc=1870734,1872447,1872591
sync=1 bad=0 outputs=3 fault=8 align_stage=1 align_move_mrad=0
```

按固件比例重算：`Ia=+0.199136 A`、`Ib=-0.116107 A`、`Ic=-0.094811 A`，相和 `-0.011782 A`，VM `12.099087 V`。这些是 ADC 推算，不是独立电流探头/电压表读数。`CURRENT_TRIP.outputs` 仅 bit0=事前 MOE、bit1=事前 nFAULT，**不含 DRVOFF**。

2048/2048 冻结编码器样本有效，覆盖故障前约 **0.512 秒**，不是完整 4.5 秒波形；末窗口码值 5580–5588，软件重放未超过 8 rad/s。主机完整轨迹依据稀疏 STATUS，不能把它当作独立轴角或高带宽三相波形。

**根因仍未证明。** 当前证据可确认“在现有限额下没有完成预期机械净位移”，但不能单凭它区分机械跟随/齿槽或装配阻力、磁场/位置测量等原因。正反方向校准主机测试通过，未找到可用本次实物证据证明的算法缺陷；不改通过阈值、不写入假零位、不增加电流/电压、不盲目重复 ALIGN。

## 5. HAPTIC10 其他实机测试

| 项目 | 结果与关键数据 | 证据 |
| --- | --- | --- |
| 编码器磁场只读 | 64 次：mask=15、valid=7，诊断高位 0x100；angle 5764–5769，AGC=255，magnitude 4510–4522；CLEAR 前后首故障保留 | field validation（`../output/bench/haptic10-20260907/hardware/encoder-field-after-align-3/validation.json`） |
| 无出力命令/故障恢复 | 56 cases；IQ 和 8 类 HAPTIC 未校准拒绝、格式/范围、分片/二进制/超长行、STOP 幂等、FAULTTEST=1024、CLEAR 不恢复校准、SELFTEST | command validation（`../output/bench/haptic10-20260907/hardware/off-command-regression-2/validation.json`） |
| UART 耐久 | 4096 有序回显；95 字节合法边界；流量下 SELFTEST；200 STATUS 拥塞及恢复；120 秒/727 轮 soak；742 STATUS、728 CURRENT_LIVE；3 次重连不重新上电 | endurance validation（`../output/bench/haptic10-20260907/hardware/io-endurance/validation.json`） |
| 全双工拥塞 | 400 请求 → 204 个完整回复 + 196 个由计数核对的整帧丢弃；无半帧/混帧，幸存回显唯一有序，随后恢复 | duplex validation（`../output/bench/haptic10-20260907/hardware/uart-duplex-malformed/validation.json`） |
| 非法输入 | 159 种非法二进制字节不拼成 SELFTEST；96/255/256/257/4096 字节超长行拒绝并恢复；同轮共 258 条检查记录（含重复状态检查） | 同上；**不含 UART BREAK** |
| PREPARE 无 PWM soak | **60.154 秒**，125 条准备态 STATUS、124 次 CURRENT_LIVE、16 次 ENC_FIELD；全部 MOE=0，准备态 fault=0/health=63/zero=1 | soak validation（`../output/bench/haptic10-20260907/hardware/prepared-soak/validation.json`）、离线分析（`../output/bench/haptic10-20260907/hardware/prepared-soak/analysis.json`） |

无 PWM soak 中，ADC 推算 VM **12.0234–12.1531 V**；三相电流 RMS 约 **3.392/3.333/2.775 mA**。准备态期间 ADC 增加 1194141、编码器有效回复增加 238829，ENC/ADC/deadline/UART/self_fail/IWDG 计数增量为零。宿主稀疏快照不是完整采样波形，也不证明带 PWM 模拟完整性或磁场余量。

AS5048A 四帧 SPI pipeline 只有三份有效数据，所以 `mask=15, valid=7` 是本协议的正确结果。AGC=255 是读数，不独立等同“磁铁坏”或“磁场完全通过”。

UART endurance 的 batch16 延迟中位数 **81.8864 ms** 是 16 条命令组成的批次耗时，不是单条 RTT。没有遇到真实时间戳回绕，**实物时间戳回绕未验收**。最终 `uart_err=390` 来自两个已记录的故意拥塞阶段（194+196），不能写成总计数为零；恢复及后续 soak 无新增。

## 6. HAPTIC9 UART 修复与仍开放的 BREAK 问题

HAPTIC10 保留 HAPTIC9 的两项修复：RX 损坏时清队列并丢弃至下一换行，避免残片拼成合法命令；PING 回复按完整帧原子入队，避免拥塞时半条回复。有效红测为 uart-red-2-test.log（`../output/bench/haptic8-other-20260907/uart-red-2-test.log`） 的 **14/459 断言失败**；HAPTIC9 最终 1501 次应用断言通过。更早含 fixture 问题的 18/459 不是可靠红测。

早期同步写大 burst 的宿主工具有接收缓冲丢帧混杂，不能据此独立归因固件。改用全双工后，HAPTIC9 拥塞子项通过，但随后 UART BREAK 出现 BOOT，**整轮仍为 FAIL**，旧完整失败记录（`../output/bench/haptic9-20260907/hardware/uart-fault-recovery-duplex/validation.json`）没有改成 PASS。

烧 HAPTIC10 前，以 SWD HOTPLUG 只读 RAM 中缓存的 RCC_CSR：**2026-09-07 20:38:21 UTC，0x04000003，仅 PINRSTF=1**，BOR/SFT/IWDG/WWDG/LPWR/OBL 标志均为零；没有 halt、复位或写寄存器。它只证明缓存的引脚复位标志，**不能证明谁拉了复位脚，更不能直接归因某个 ST-LINK 功能**。只读取证（`../output/bench/haptic9-20260907/hardware/reset-cause-readonly/validation.json`）。

HAPTIC10 没有再次发送 UART BREAK；新异常字节测试使用单独 runner/结果名，明确排除此项。旧 USB-only watchdog / BREAKTEST 需要 EVM 断开，本次没有运行，不能计为通过。

## 7. 工具失败与证据完整性

以下失败均保留原目录，成功重试另建目录，没有降低生产通过条件：

- HAPTIC10 第一轮软件绿测：唯一失败为仍期待旧 BOOT 标记；修正测试身份后复跑通过。
- `encoder-field-after-align`：在 FAULT 状态请求只允许 OFF 的 ENC_DIAG，被固件正确拒绝。第二次误把 valid mask 期望成 15；修正为真实四帧 pipeline 的 7，第三次通过。失败/不完整记录以及两份 runner 快照保留。
- `off-command-regression`：先前诊断退出后仍处于 ALIGN fault=8，入口 OFF 断言拒绝；读取/保留真实故障并正确 CLEAR 后，新目录 `off-command-regression-2` 的 56 项通过。
- UART 新 runner 曾遇 Python 字符串编译的 BOM 工具问题；UTF-8 修正及静态检查后才运行实机。旧 BREAK 失败脚本与结果没有被替换。
- 后续 OFF 命令回归有意注入 fault=1024，因此当前 RAM 的 CURRENT_TRIP 可能对应该注入；**它不是又一次真实 ALIGN/过流故障**。真实 fault=8 已在唯一 ALIGN 目录冻结保存。

旧文档快照、HAPTIC9 生产源码快照、红测/失败日志和当前镜像均保留。当前工作区原有未提交内容未清理，也未删除文件。

## 8. 最终停止状态与未通过项

**最后实机验证：2026-09-07 21:08:48.739 UTC（美东 17:08:48.739）。**

- 串口：`mode=0 fault=0 calibrated=0 zero=0 moe=0 off=1`；最后 STOP 已应答，COM4 关闭。
- SWD 只读：`MOE=0`、`CCER=0`、`ChannelEnables=0`、`DRVOFF=1`、六路 PWM 输入及输出锁存均低；**nSLEEP=1**，电子电路仍醒着，不能写成物理断电或全板休眠。
- 全部 47224 字节可执行镜像区再次读回 SHA 一致；读取期间 ADC 前进，`adc_bad=enc_err=deadline=self_fail=iwdg_reset=0`，`rate=1`。
- STOP 使校零无效，`zero=0` 后 STATUS 中安培级相电流字段不能当作真实相电流；应使用准备态有效校零或冻结故障数据。

| 尚未通过 | 真实限制 |
| --- | --- |
| ALIGN 机械/电角校准 | 这一次确实失败，原因未闭合 |
| 真实 IQ 闭环、8 类 HAPTIC | 校准未通过，仅拒绝路径与软件行为通过；没有运行成功校准后的实物出力测试 |
| UART BREAK 恢复 | HAPTIC9 曾复位；HAPTIC10 未重试，不计为通过 |
| 实物时间戳回绕、USB-only watchdog / BREAKTEST | 本轮未覆盖；主机回绕测试不替代实物证据 |
| CET6 产品端口、ESP32/AMOLED、无线/HID/OTA、真实 PC/设备整链 | 不是本次台架验收；原 HAPTIC8 功能矩阵与实现边界仍保留，不虚构整机通过 |
| 热、回灌、动态电气波形、全母线窗口、机械手感 | 没有本轮独立仪表或完整实物验收证据 |

后续应先获得能区分机械跟随与位置测量问题的证据，再决定针对性修改；不能只通过放宽校准成功条件来继续出力。**本轮结论是“电压窗口已调整并实际续测，多项回归通过；真实校准仍失败”，不是“所有主要功能已通过”。**
