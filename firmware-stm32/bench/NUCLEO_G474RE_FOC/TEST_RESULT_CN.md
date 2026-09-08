# 当前 FOC 联调固件验收记录

> 当前状态（2026-09-07）：`VM_WINDOW_UPDATED / ALIGN_TRAVEL_HOLD`，固件 `20260907_HAPTIC10`。母线窗口已改为 9–15 V，其他出力限额未改；软件回归、PREPARE/校零、60.154 秒无 PWM、56 个拒绝用例、UART 耐久及异常字节恢复通过。单次 ALIGN 约 4.5 秒因机械净位移不足报 fault=8，真实 IQ/8 类触觉仍未通过。完整证据见[HAPTIC10 续测验收报告](../../../docs/bench-validation-20260907-haptic10-cn.md)；本文后续章节保留历史。最后实测 21:08:48 UTC 为 mode=0/fault=0/MOE=0/DRVOFF=1、通道关闭、六路 PWM 输入低、nSLEEP=1，未校准，COM4 已释放；未操作外部电源。

## 历史页首记录（2026-09-06；以下保留原文，不是当前固件/接线状态）

> 当前状态（2026-09-06 09:05 UTC）：`STATIC_BUCK_OCP_HOLD`。用户上电回复约5 mA/0.063 W后，新诊断镜像只执行1次PREPARE并STOP，六项配置精确读回通过，STAT0/1/2=`41/00/20`，失败阶段`STATUS_PRE/STATUS_FLAGS`；失败后重锁和STOP后缓存保留均验证。按TI手册是Buck过流标志，不能判为电机过流、SPI错误、持续短路或板坏。MOE=0/DRVOFF=1，三相断开、串口已释放；已请关闭12 V但尚未收到本次关电确认。下一步只核查断电后的R13及3.3VBK外部供电/负载，不再自动PREPARE、不出力。详见 [本轮静态诊断记录](G:/Agent/GL30-Haptic-Control/output/bench/drv-static/diagnostic-static-result-20260906.md)。

日期：**2026-09-06 UTC**。最新阶段：**STATIC_BUCK_OCP_HOLD**。当前29688字节诊断镜像已烧录、完整回读一致并通过120.012秒无动力验收；随后09:05单次PREPARE/STOP读到STAT=41/00/20，六项配置通过、STATUS_PRE/STATUS_FLAGS失败，失败后重锁及缓存保留通过，尚未ADC校零或FOC。第14节05:47结果属于旧镜像。

当前镜像离线验收：Keil0错误/0警告；主机套件4/4、诊断套件195次断言通过。无动力实物验收：编程校验及29688字节独立回读一致，3次自检、120.012秒编码器新增480223组/错误0，154条STATUS、6591次脚本检查通过，VM采样0～21 mV。另一次带电静态试验实际覆盖非空诊断、STATUS_PRE/STATUS_FLAGS失败、尽力重锁及STOP后缓存保留；不代表其他全部失败分支或驱动就绪通过。原始证据分开记录，见页首链接。

实物为NUCLEO-G474RE、原厂AS5048A与TI DRV8316REVM，三相保持断开。下载在用户近零电压确认后执行，后续用户再上电才运行1次PREPARE/STOP，前后MOE=0/DRVOFF=1、button=0、calibrated=0；未发送电机出力命令。最新已请关12 V但尚待回复，不能将软件STOP当作断电。IOC/引脚/保护参数未变。下文各节为早期镜像对应证据，当前停点以页首为准。

## 1. 此前无动力阶段的实际结果（旧镜像）

| 验证项 | 实测结果 | 边界 |
| --- | --- | --- |
| Keil ARMCLANG 6.21 | **0 错误、0 警告**；Code 23036、RO 1852、RW 28、ZI 9716 字节 | 使用原 CubeMX 生成的联调工程；不是产品 CET6 镜像 |
| 下载及完整镜像读回 | CubeProgrammer 校验成功；独立读回 **24920 字节**，SHA-256 一致 | 校验范围是完整可执行镜像，不是整个 512 KB Flash |
| 现有主机回归 | **3/3 通过**：gl30-host-tests、foc-safety-tests、bench-safety-tests | 不冒充物理电流环实验；本次没有新增单元测试文件 |
| 重复自检 | 连续 **3 次** SELFTEST，每次 20,000 次合成算法迭代，均完成且无失败 | 合成算法使用假输入；不向功率输出写入测试力矩 |
| 连续编码器检查 | **120.021 秒，480,483 组成功 / 0 组失败**，与调度计数增量一致 | 无动力条件下的通信，不是带功率噪声的耐久验证 |
| 采样 / 编码器调度 | ADC 增量 2,402,415；编码器 IRQ 增量 480,483；主机计时估计 **20016.667 / 4003.333 Hz** | 含主机与串口取样边界误差，不是频率计校准结果；固件 rate 门为 1 |
| 错误与健康位 | 最终 enc_err=0、uart_err=0、adc_bad=0、deadline=0；自检完成后 health=34 | health=34 是编码器与时序通过，不是整体出力许可 63 |
| IRQ 执行时间 | 最大 **3239 cycles = 20.244 μs**，原预算 **3600 cycles = 22.5 μs** 不变 | 约 2.256 μs 实测余量不代表所有未来工作模式的最坏执行时间保证 |
| 合成算法时间 | 末次 self_max=2461 cycles；启动记录为 2465 cycles | 每次 SELFTEST 会重新统计 self_max；IRQ 超限累计未被脚本清零 |
| 状态报告 | 全程 **154 条 STATUS**；最大报告年龄 **216 μs**；诊断字均为 0x01FE | 154 个状态快照，不是每次读取的模拟波形记录 |
| 输出状态 | 全部快照 mode=0、fault=0、moe=0、off=1、button=0、calibrated=0 | 未使用示波器测量带电功率级关断延迟 |
| 检查脚本 | PASS，6590 次断言检查；3 条自检启动与 3 条完成回执 | 断言累计数不等于 6590 个独立物理场景 |

本轮没有发送 PREPARE、ALIGN、IQ、KEEPALIVE、BREAKTEST 或 FAULTTEST，没有进行看门狗故障注入、驱动唤醒或电机运动命令。电机角度读数在记录中出现变化，但没有组织受控整圈手转，**不能据此宣称方向、零点或全圈线性已验收**。

## 2. 此前通过编码器验收的镜像身份（现已替换）

- 目标：`NUCLEO_G474RE_FOC`，STM32G474RET6，COM4。
- 工程：`firmware-stm32/bench/NUCLEO_G474RE_FOC/MDK-ARM/NUCLEO_G474RE_FOC.uvprojx`。
- 编程/读回确认时间：`2026-09-05T10:25:30.4509204Z`。
- 完整镜像与读回 SHA-256 相同：

```text
98474E3C8F749C815DC464363D2348BB779521B3720CF9735B02EBC71EF9239E
```

该镜像曾取代此前仅 USB 镜像；本轮又已替换为第 8 节诊断镜像。旧镜像通过的结果不会自动成为新镜像的验收证据。源码或构建选项改变后需重新核对。

## 3. 先复现，再修复

### 3.1 接入编码器后，自检与真实观测叠加超时

旧固件在 OFF 状态再次执行 SELFTEST，期限计数由 **3999 增至 7999**，即增加 4000；`isr_max=4091`，超过原预算 3600。只在未接编码器时通过的裸板测试没有覆盖这个组合。

修复仅在 `Bench_AdcIRQ`：有新真实角度时优先更新真实观测器，其余 ADC 时隙运行合成自检，不让两份观测器和合成电流环挤在同一次 IRQ。真实观测仍为 4 kHz，合成自检仍完成 20,000 次，接入编码器时约需 1.25 秒。全部安全检查和原 DWT 时序门保留。

自检完成回执现在还要求 `deadline=0`，避免单看算法内核耗时就报告整体自检通过。

### 3.2 STATUS 时间戳竞争

第一版时序修复已消除超限，但上板回归捕获：

```text
health=0 age_us=1 enc_ok=2142125 enc_err=0
self_left=19940 self_fail=0 isr_max=3230 deadline=0
```

检查脚本正确地判为 FAIL，未被改成忽略编码器健康位。源码存在如下竞争：主循环先读 `now`，编码器中断随后更新 `g_enc_us`，继续计算无符号 `now - g_enc_us` 时发生下溢；报文中另一次年龄读取却正常。

最终修复在 STATUS 内建立相关健康位、编码器和 self_left 的短临界区快照，解锁后才格式化和发送。没有修改实际健康阈值。最终 154 条报告中，自检运行时健康位为 2，完成时为 34，未再出现上述矛盾。模拟量等其他报文字段不承诺整行严格同一 ADC 时刻。

### 3.3 检查脚本本身的验收

首次执行硬件前，主线程发现并纠正了 PowerShell 断言的命令参数求值问题，以及“自检尚在运行时就要求时序就绪”的错误条件。最终脚本：

- 运行中仍要求编码器健康、年龄有效、错误不增长、输出关闭；
- 只有 self_left=0 才允许通过自检完成条件，并要求 health 包含 34；
- 正常采样阶段不接受自检未完成；
- 原预算 3600 cycles、750 μs 年龄门均未放宽。

主线程独立提取脚本函数作内存正反例检查，验证忙/完成状态，以及异常 health、deadline、self_fail、isr_max、MOE、UART 的拒绝行为；随后实际运行硬件脚本，不以静态语法检查代替上板结果。

自检调度取舍参考了 DeepSeek 的独立分析；Spark 负责检查脚本与竞争点的局部核查。生产代码、硬件操作及最终复核由主线程完成。本轮没有调用千问，不声称三模型实物验收。

## 4. 复现与证据

保持 TI 动力关闭、不按 B1，不同时打开第二个串口工具：

```powershell
Set-Location 'G:/Agent/GL30-Haptic-Control/firmware-stm32/bench/NUCLEO_G474RE_FOC'
.\test_encoder_off.ps1 -Port COM4 -ConfirmDriverPowerOff -DurationSeconds 120 -SelfTestRuns 3
```

确认参数代表操作者已核实电源关闭；软件读到 VM 不足启动阈值不能替代物理断电确认。

当前证据目录：`G:/Agent/GL30-Haptic-Control/output/bench/encoder-off`：

- `encoder-off-result.json`：PASS、计数差值、频率估计、起止与启动状态。
- `encoder-off-transcript.log`：带 UTC 时间戳的逐条 TX/RX。

构建、失败复现与镜像证据：`G:/Agent/.cache/gl30-timing-20260905`：

- `keil-build.log`、`program.log`、`flash-readback.log`、`flash-hash-result.json`。
- `firmware.bin`、`flash-readback.bin`：当前相同镜像。
- `status-race-before.log`、`status-race-before.json`：真实失败记录，未用通过结果覆盖。
- `bench_app.before.c`、`firmware.before.hex`：本次修改前备份，留在缓存，不另造历史工程。

本机已有 Linux 主机测试构建目录，本轮通过 WSL Ubuntu 运行：

```text
cmake --build build/foc-preflight-host
ctest --test-dir build/foc-preflight-host --output-on-failure
```

不要混用 Windows CMake 改写这个已有 Linux 构建缓存。此前仅 USB 的原始记录仍在 `artifacts/bench/nucleo-g474re-foc`；刹车/看门狗故障注入仅在当时全拆线裸板状态做过，**本轮未复跑，当前接线状态不得运行这些裸板脚本**。

## 5. 尚未完成及下一道门

当前只通过无动力编码器通信和合成 FOC 自检，没有真实电流环、速度环或力反馈手感结论。

1. 用户在场、先启动采集，再缓慢手转转子整圈：检查角度回绕、变化和诊断状态。这不是角度精度/线性标定。
2. 之后由用户配合复核 TI 接线、电源、固定与无输出条件，再进行上电静态 PREPARE、驱动 SPI 读回和 ADC 校零。
3. 最后才安排电角零位/方向校准，以及受 B1、主机许可、电压/电流/时间限制的小力矩脉冲。

带功率噪声的编码器可靠性、ADC 极性与增益、实际采样窗口、外部 nFAULT 和真实停机链路、电流环稳定性、回灌与温升均待实测。不得把本记录当成“只需调参即可保证全部可用”。操作和针脚表见 [README_CN.md](README_CN.md)。

## 6. 最新：12 V 上电后，仅 STATUS 读取检查

2026-09-05，用户告知开启台式电源 **12 V / 300 mA 限流**，人不在现场。该设定属于用户报告，主机没有读取电源实际输入电流或 CV/CC 状态。本轮未发送 PREPARE、ALIGN、IQ、KEEPALIVE、SELFTEST 或复位命令，未烧录固件，未修改保护门限。

验收区间：`2026-09-05T11:11:58.4867612Z` 至 `2026-09-05T11:14:12.0290902Z`，**133.542 秒**。只读窗口重新打开并保留，所有发送记录均为 STATUS；没有启用电机输出的主机命令。窗口标题改为“不启用电机”，不再把外部已上电误写为动力保持关闭。

| 项目 | 当前实测 | 限制 |
| --- | --- | --- |
| 完整 STATUS / CSV | 1085 / 1085，无无效编码器快照 | 窗口之后仍继续采集；本表只覆盖上述固定区间 |
| 编码器读取计数增量 | 成功 534367，错误 0 → 0 | 启动后的调度计数增量，不是 534367 条主机曲线点 |
| 母线 ADC 推算值 | 11.958–12.153 V | 分压和 ADC 初值推算，未经万用表交叉校准；不能证明实际 CV/CC 模式 |
| 编码器新鲜度 | 最大 215 μs，diag=510（0x01FE） | 无电机 PWM 开关噪声验证 |
| 输出状态快照 | mode=0、fault=0、moe=0、off=1、button=0、calibrated=0，全部符合 | MCU 状态不能替代对实物 DRVOFF、nSLEEP 和相输出的测量 |
| 健康位 | health=42，即编码器 2 + 母线窗口 8 + 时序 32 | 不是完整 health=63；驱动 SPI、ADC 校零和 nFAULT 高均未通过 |
| ADC / 期限异常 | adc_bad=0、deadline=0；IRQ 最大 3230 cycles | 未校零时 ia/ib/ic 不解释为真实相电流 |
| UART 错误 | 进入本轮前已有 1，整个区间 1 → 1 | 原因尚未查明；没有清零或把它写成“UART 从未报错” |
| 驱动状态 / 电流零点 | drv=4294967295、zero=0 | 未配置/未取得有效驱动读回，不是可解码的芯片真实故障寄存器 |
| 软件输出策略 | 1086 次 TX，全为 STATUS | 最后一次请求可能仍在接收，故 TX 可比完整快照多 1 |

结论：**在已采集区间内，通过带外部电源、保持输出关断的 STATUS 观察检查；未完成 DRV8316 唤醒、零电流校准或真实 FOC。** 没有强行把 nFAULT 低判断成芯片损坏或已验证正常。300 mA 是台式电源输入限流设定，不作为相电流或转矩的保护保证。

官方交叉核对：[TI DRV8316 数据手册 Rev B，第 53 页](https://www.ti.com/lit/ds/symlink/drv8316.pdf) 说明 nSLEEP 低时内部 SPI、CSA 等关闭；DRVOFF 高会独立禁止全部 MOSFET。手册对 nFAULT 低的说明包含上/下电过渡和 DRVOFF 引起的故障条件，**不能把本次持续低电平直接概括为“睡眠时必然正常”**；这项未接受模型复核中的宽泛推断，仍保留现场及唤醒后的验证。

窗口 18/18 现有单元测试通过，已检查真实显示截图；曲线来自真实串口数据。界面仍提示已有的 UART 错误，未隐藏该计数。Spark 只读复核了 STATUS / PREPARE / 输出门路径，DeepSeek Pro 独立复核了无人现场检查边界；两者均不构成实物保护认证，最终操作和证据由主线程检查。

证据：

- 汇总：`output/bench/power-on-readonly/latest-result.json`。
- CSV：`output/bench/encoder-viewer/encoder-status-20260905_111158_021-25684.csv`。
- 原始串口：`output/bench/encoder-viewer/encoder-serial-20260905_111158_021-25684.log`。
- 显示检查：`G:/Agent/.cache/gl30-encoder-viewer/power-on-readonly-window.png`。

下一步需要人在现场：先断动力并等待放电，按 README 首次 PREPARE 流程暂时断开三相、复核静态关断和模拟基准，再安排驱动 SPI 读回和零点检查。ALIGN / IQ 继续保留现场 B1 许可、固定检查和故障停止要求；本轮不绕过这些门。结束后若无人照看，不建议为了保持曲线而继续给 TI 动力供电；NUCLEO 与编码器只需原有 USB 供电即可保持角度读取。

## 7. 最新：三相断开后，第一次 PREPARE 失败

用户明确确认现场有人、已断开 OUTA/B/C 三根电机相线、12 V 电源重新开启；沿用用户设定 300 mA，不按 B1。主线程正常关闭只读曲线窗口释放 COM4，通过现有 `evm_console.ps1` 执行一次 PREPARE，未新造控制协议或修改固件。

真实回执为 **`ERR DRV_SPI_READBACK_OR_FAULT`**。控制台立即发送 STOP，随后收到了 `OK STOP`。这不是 `OK PREPARED_NO_OUTPUT`，不能宣称驱动初始化或电流零点校准通过。

- 尝试前及之后：mode=0、fault=0、moe=0、off=1、button=0、health=42、zero=0、calibrated=0。
- 一条失败后的记录：vm_mv=12077、enc 有效、diag=510、age_us=25、deadline=0、adc_bad=0；该控制台最后一次 STATUS 为 vm_mv=12055、diag=510、age_us=167。
- enc_err=2、uart_err=1 在进入本轮前已存在，早期失败后状态未增长。**后来重新打开窗口时编码器已持续失效，详见第 8 节，不能把早期稳定结果延伸到现在。**
- `drv=4294967295` 是 `g_drv_status` 的初始未更新值，本次失败可能发生在更新前。**不能把这个字段当成 SPI 已实测返回全 1。**
- 代码在 `bench_hw_driver_configure()` 失败后直接返回；没有进入 `g_preparing=true` 和 512 次零点采集，故 ia/ib/ic 继续属于未校零显示，不能解释为真实相电流。

为排除 MCU 未设置引脚的情况，使用现有 CubeProgrammer 的 HOTPLUG 连接，只发送 `-r32` 读取命令，没有烧录、复位、停 CPU 或写寄存器命令。随后串口计数持续增长，未见 BOOT：

| MCU 只读结果 | 解读 |
| --- | --- |
| GPIOC ODR=0x0380、IDR=0x0B80 | PC7/nSLEEP、PC8/DRVOFF、PC9/CS 的输出位及输入回读均高 |
| GPIOB IDR=0x0040 | PB12/nFAULT 回读仍低 |
| TIM1 BDTR=0x02001C50 | MOE 位为 0 |
| SPI3 CR1=0x0365、CR2=0x0F00、SR=0x0002 | SPI 已使能、Mode 1、16 位、分频 DIV32，采样时无等待接收字 |

这些结果只能确认 MCU 端，不替代 TI 板端测量。STOP 关输出/通道但不停止 TIM1 计数，也不把 nSLEEP 拉低；测量期间预期 MCU nSLEEP 与 DRVOFF 都为高。

对照 [TI DRV8316 数据手册 Rev B，§8.5](https://www.ti.com/lit/ds/symlink/drv8316.pdf)，现有 Mode 1、16 位、MSB 优先、6 位地址及 **SDI 输入** bit8 偶校验帧没有发现直接矛盾；**SDO 返回的 bit8 是 S0/FAULT，不是校验位**。CTRL2 0x68 保持 SDO 推挽，CTRL1 3/6 分别为解锁/上锁码。聚合报错仍不能区分哪次 SPI 传输、哪个寄存器读回或 nFAULT 检查失败，不据此定性接线错误或芯片损坏。

随后用户测得：J3.17/nSLEEP 对 AGND **2.8 V**，J4.16/DRVOFF **3.3 V**，电源实际显示 **12 V、0.013 A**。TI §7.5 规定 nSLEEP 高电平最低 1.6 V、DRVOFF 最低 1.5 V，故两项 DC 值可识别为高；不证明波形质量，也不解释 nSLEEP 相对 3.3 V 的压降原因。电源读数不呈现限流降压，但 CV/CC 指示未单独报告。保持三相断开、B1 松开；不循环重试、不绕过 nFAULT、不提高电流门限。

证据：`output/bench/drv-static/prepare-20260905-session.log` 与 `output/bench/drv-static/latest-result.json`。未执行 ALIGN、IQ、KEEPALIVE、SELFTEST、看门狗/刹车故障注入或 MCU 复位命令。

## 8. 最新：修正 NPOR、烧录诊断镜像；编码器仍未恢复

### 软件修复与边界

主线程对照 TI Rev B 表 8-10 / 8-13 / 8-19 发现：NPOR 是低有效复位记录，正常值为 1。原 `bench_hw_driver_status()` 将所有位直接 OR、要求结果为 0，因此可能拒绝正常 NPOR=1，也不能正确表达 NPOR=0。该**源码缺陷已复现**，但第一次实物 PREPARE 的实际失败点尚无逐寄存器记录，不能宣称就是它导致。

最小修复：

- 新纯函数只翻转 IC_Status 和每帧 SDO summary 中的 NPOR，其他故障位、保留异常位及 SDO 的 FAULT 位全部保留；STAT1/2 的低字节 bit3 不翻转。
- 睡眠/唤醒后先读全部状态字；仅允许独立 NPOR 记录、且 nFAULT 为高时，写一次 CTRL2 `0x69` 确认启动复位，严格回读自清后的 `0x68`。若还有其他故障、SPI 失败或 nFAULT 低则退出，不自动重试、不屏蔽故障。
- PREPARE 失败增加 `DRV_DIAG`：最后一次 tx/rx、是否传输完成、原始状态字、有效读取掩码、MCU 引脚值。`stat_mask=0` 代表未读到状态寄存器，不把原始值 0 当成通过。
- GPIO 初始化、SPI 时钟/模式、PWM、B1 许可、电流/电压/时间门限、编码器 ISR 均未改变；诊断格式化仅在主循环错误路径。

单元测试代理负责测试文件；主线程审查并纠正了其中 STAT1 左移位置的错误预期，再独立执行回归。最初提取旧实现时 **26/239 检查失败**，修正后最终 **3/3 套件通过**：271 项主机检查、649124 项 FOC 安全检查、98 项台架安全检查。这是软件检查数量，不是同等数量的物理实验。DeepSeek 复核提出过将 SDO bit8 当校验位屏蔽的错误建议，主线程依据官方表格拒绝，未采纳。

### 当前实际板内镜像

用户确认 12 V 已关闭后才下载；NUCLEO USB 保留，三相保持断开。Keil ARMCLANG 6.21 最终 **0 错误、0 警告**；Code=24144、RO=2008、RW=28、ZI=9724 字节。一次初始构建的 bool 格式转换警告已修正，未以带警告构建冒充最终结果。

- 完整执行镜像：**26184 字节**。
- CubeProgrammer 下载校验成功，另用 HOTPLUG 独立读取完整镜像，比对 SHA-256 相同：

```text
7FE3538392CD9D20969C7A4B0636856FC6BA80B11008893B18533CB607FB5D11
```

烧录包含正常 MCU 复位；**只有第 7 节首次读取期间没有复位**，不将该旧边界套用到烧录动作。未恢复 12 V，也未对新镜像运行 PREPARE / ALIGN / IQ / KEEPALIVE。

### 新发现的真实阻塞

烧录前的曲线窗口记录已出现 `health=40, diag=0, enc_ok=20612125` 不增长，`enc_err` 持续增加；这发生在新诊断镜像烧录之前，本轮没有修改编码器代码。关闭 12 V 后错误仍在。

烧录后的首个控制台状态：`mode=0, fault=0, moe=0, off=1, button=0, health=32, enc_ok=0, enc_err=303816, enc_irq=303816, diag=0, zero=0, calibrated=0`；`deadline=0, adc_bad=0, uart_err=0, isr_max=3223, self_max=2467, iwdg_reset=0`。当前固件尚无一次有效编码器数据，不能称为编码器已恢复。

此时母线 ADC 推算约 1.77 V，不能据此断言关闭电源后实际母线为 0 V；可能残余或耦合来源未定位，不在未知供电状态下插拔。`diag=0` 也不能单独等同于 MISO 全 0：底层失败/解码失败会导致调用方保持初值。

随后用户实测编码器红线对黑线为 **0 V**，NUCLEO 丝印 5V 对 GND 为 **5 V**。板上 5 V 来源有输出，但编码器端有效供电尚未建立，红线、黑线回路或测量接触问题还需区分。下一项将万用表黑表笔固定在已确认的 NUCLEO GND，分别测编码器端红线、黑线对该 GND 的电压（正常目标分别约 5 V、0 V），不移动供电跳帽、不带电插拔；保持 USB、TI 12 V 关闭、三相断开、B1 松开。主机最新窗口仍无有效编码器快照。恢复供电和有效编码器记录、核对后，再安排新镜像的一次静态 PREPARE。**真实电流闭环和转动测试仍未进行。**

进一步用户报告：编码器红线、黑线分别对 NUCLEO GND 均为 **0 V**。因此优先检查红线正电源路径/触点，不能把黑线 0 V 单独当成地线导通证明。按 ST UM2505 图 18 核对，CN7.18 是 +5 V、CN7.19 是 GND。下一步先拔掉 NUCLEO USB（TI 12 V 仍关闭），再核对并重新插牢编码器红线至已经实测有 5 V 的针脚；不改 SPI、不动跳帽、不触碰裸露芯片脚。接回 USB 后复测编码器红黑电压并重新读取状态，未经该复测不宣称供电已修复。

重接后用户报告编码器红黑间已恢复 **5 V**。主线程关闭原已掉线的窗口、释放串口后，用既有 `evm_console.ps1 -StatusOnly` 读取；第一次返回 `ERR LINE_TOO_LONG_OR_BINARY`，第二次成功返回 STATUS。没有发送 CLEAR / PREPARE / ALIGN / IQ，也没有再次烧录；协议行错误的原因不作无证据归因。

有效 STATUS：`mode=0, fault=0, health=32, moe=0, off=1, button=0, enc=13948, diag=0, age_us=156780488, enc_ok=2478, enc_err=627122, enc_irq=629600, uart_err=1, deadline=0, adc_bad=0, zero=0, calibrated=0`。说明本次启动曾读到数据，但当前角度已经陈旧，**供电恢复不等于通信恢复**。未从 diag=0 推断 SPI 返回内容，也未认定一定是锁存错误。

本阶段提出的下一步是利用现有 PREPARE 的编码器清错及驱动静态诊断。源码已确认该过程不调用 `bench_hw_arm()`，保持 DRVOFF 高、MOE=0；只有额外的 ALIGN/IQ 流程才允许出力。随后用户确认重新开启 12 V / 300 mA，已执行一次并 STOP，详见第 9 节；本节不是当前电源状态。

证据：`G:/Agent/.cache/gl30-drv-static-20260905` 下构建、下载、读回日志与二进制；`output/bench/drv-static/diagnostic-20260905-session.log`；烧录前错误序列位于 `output/bench/encoder-viewer/encoder-serial-20260905_123646_653-56792.log`。原已通过的 120 秒无动力验收文件未覆盖。

## 9. 驱动配置读回通过，但静态故障门未通过

用户回复“开启了”后，只运行一次 PREPARE。真实返回：

```text
DRV_DIAG tx=8400 rx=0900 transfer_ok=1 stat_raw=000009 stat_mask=7 nsleep=1 off=1 nfault=0
ERR DRV_SPI_READBACK_OR_FAULT
OK STOP
```

该代码路径已经完成六个配置寄存器的写入和精确读回，再读完三组状态字。`STAT0=0x09、STAT1=0x00、STAT2=0x00` 表示 FAULT=1、NPOR=1，其他低字节保护位本次未置位；检查在重锁寄存器、CLR_FLT 和 ADC 校零之前退出。**配置传输通过不等于驱动健康、校零或 FOC 通过。** 不将 `drv=4294967295` 当成这次原始返回值。

失败后 `mode=0、moe=0、off=1、button=0、zero=0、calibrated=0`，未发送 ALIGN/IQ/KEEPALIVE，未释放 DRVOFF。编码器清错前有效计数 2478，之后曾到 4322（新增 1844 次），随后停止增长；没有持续恢复。

电路依据只采用 [TI DRV8316 Rev B §8.4.2](https://www.ti.com/lit/ds/symlink/drv8316.pdf)：DRVOFF 高会关闭全部 MOSFET，且可触发导致 nFAULT 低的故障。**这提示当前“DRVOFF 高且要求 nFAULT 高”的流程需按手册复核，但不能据此认定本板低电平必然正常。** 本轮不屏蔽 FAULT，不自动清驱动故障，不以 AI 建议作为解除关断的依据。

用户随后确认“已经关闭”：TI 12 V 已关闭、USB 保留、三相仍断开。断电后 STATUS 的 `vm_mv=1762` 只是 ADC 推算，不等同于物理母线为 0 V。证据：`output/bench/drv-static/prepare-diagnostic-20260905-session.log`。

## 10. 历史故障定位：编码器原始错误已读出，当时持续通信未通过

### 断动力诊断镜像与软件验收

用户确认 TI 关闭后，保留既有 CubeMX/LL/Keil 工程。没有改 GPIO/SPI 时钟或模式、超时阈值、ADC/PWM 参数及出力保护；仅增加定长原始诊断快照和 OFF 态命令，不在 ISR 格式化或自动清错。

- `ENC_DIAG`：复制首个失败、最新样本和最近一次显式清错记录；本命令不访问 SPI、不清错。
- `ENC_CLEAR`：仅在 OFF、未 PREPARE/SELFTEST、B1 松开、MOE=0、DRVOFF=1 时，单次发送 READ ERROR + NOP，保存流水线原始返回；禁止自动循环。
- `mask` 是完整传输字的有效掩码，缺失的 raw=0/flags=0 不代表成功。样本 `sample=0` 表示没有首错快照。`reason`：0有效、1传输失败、2角度帧无效、3诊断帧无效、4磁场诊断不通过。`flags`：bit0 主机验出的返回字奇偶错误，bit1 传感器 EF。与传感器 ERROR 寄存器 bit2 的定义不可混淆。
- SPI 失败阶段：1等待 TXE/BSY、2等待 RXNE 超时、3OVR、4等待 BSY 清零超时。只记录实际命中分支，不推断是哪根线有问题。
- 单次清错返回字传输不全、奇偶无效或保留载荷位异常时返回 ERR；`OK ENC_CLEAR_ATTEMPT_OUTPUT_OFF` 仅确认可检查的单次读/清错尝试，不保证后续通信恢复。

Keil ARMCLANG 6.21：**0 错误、0 警告**，Code=25396、RO=2320、RW=28、ZI=9820。CubeProgrammer 下载校验成功，另独立 HOTPLUG 全镜像读回：**27752 字节**，两份 SHA-256 一致：

```text
98392FB757FDD3ABF2CC80141B890942855FD6DB640F0962B4EB775E2488572B
```

主线程重跑既有 CTest：**3/3 套件通过**。台架单测 262248 项检查，包含穷举 65536 个 16 位返回字的 parity/EF、NULL、失败不写输出语义；该数量不代表物理实验次数。单元测试代理仅改测试文件，期望奇偶值采用独立逐位计数算法；最初 API 未实现时链接失败，补齐后通过。只读代理发现清错失败回执易误解，主线程加了明确 ERR 路径并重建，未新增电路操作。

### 真实读数

烧录后、显式清错前：

```text
ENC_FIRST sample=1 us=390 raw=C000,7679,41FC mask=7 reason=2 angle_flags=2 diag_flags=2 spi_stage=0
ENC_LATEST sample=187194 us=46798625 raw=C000,767A,41FC mask=7 reason=2 angle_flags=2 diag_flags=2 spi_stage=0
```

本轮仅一次 `ENC_CLEAR`，完整返回：

```text
ENC_ERROR_READ count=1 previous=C000 raw=4004 mask=3 flags=2 spi_stage=0 spi_us=0 spi_sr=0000
OK ENC_CLEAR_ATTEMPT_OUTPUT_OFF
```

按 [AS5048 DS000298 v1-11，第 16 页 Figure 22、19 页 Figure 25/26](https://look.ams-osram.com/m/287d7ad97d1ca22e/original/AS5048-DS000298.pdf)：ERROR 低位 `0x0004` 是传感器记录的 **Parity Error**，返回帧中的 EF 仍可为 1；这是传感器收到命令发生奇偶错误的记录，不是“主机计算这次 MISO 返回字奇偶错误”。本次 `0x4004` 的返回字偶校验通过。

清错后有效读取 **784 次**，随即再次失效；首个新失败：

```text
ENC_FIRST sample=292773 us=73193376 raw=0000,BCF4,767A mask=7 reason=3 angle_flags=0 diag_flags=2 spi_stage=0
ENC_LATEST sample=364132 us=91033125 raw=C000,767A,41FC mask=7 reason=2 angle_flags=2 diag_flags=2 spi_stage=0
```

因此该首错三次传输都完成、诊断返回槽含 EF，**并非本次记录命中了 SPI 超时分支**。不把 EF 载荷当有效角度或磁场数据。新 EF 是否仍是同一种 ERROR 位没有再次清错读取，保留待验证；不夸大为整段没有任何 SPI 瞬态。

最后 STATUS：`mode=0、fault=0、health=32、moe=0、off=1、button=0、enc_ok=784、enc_err=363214、age_us=17806536、zero=0、calibrated=0、isr_max=3245、self_max=2470、deadline=0、adc_bad=0、uart_err=0`。退出控制台时发送 STOP 并释放 COM4，未再次 PREPARE、未对驱动上电、未带转。

### 下一步需要的最少现场证据

先保持 TI 12 V 关闭、三相断开、B1 松开。下一步需要在**编码器端**采集 CSn/CLK/MOSI/MISO，而不是继续调整 FOC 电流参数：核对每个 CS 低窗口 16 个时钟、Mode 1（CPOL=0/CPHA=1）、MSB-first，预期命令为 `FFFF / 7FFD / 0000`。采集准备好后再由主线程协调一次显式清错，保存首个 EF 前后的原始数据，不只截后续恒定 EF。

手册 v1-11 第 12 页 Figure 14/15：CS 下降至首个时钟上升至少 350 ns，CS 帧间高至少 350 ns，末时钟下降至 CS 上升至少 50 ns，时钟高/低各至少 50 ns，MOSI 数据建立至少 20 ns。数字逻辑分析仪可核对位序和帧；若需判断电平、过冲、毛刺或建立时间余量，还需合适采样/带宽的示波器，不能凭万用表 DC 数值判定。

第 7 页 Figure 8/9 使用 **VDDCORE** 定义数字阈值（VIH ≥0.7×VDDCORE、VIL ≤0.3×VDDCORE），不是把外部 5 V 直接乘 0.7；不能据此错误推断 STM32 3.3 V 必然不兼容，也不等同于本机波形已验证。用户原始 PDF 路径现已不可访问，上述两页使用此前保留的 v1-11 页面图和文本逐项核对；ERROR 位定义另与官方索引核对。没有新装 PDF 软件。

证据：`output/bench/drv-static/encoder-diagnostic-20260905-session.log`；`G:/Agent/.cache/gl30-encoder-diag-20260905/` 的 build/program/readback 日志及二进制；结构化结果见 `output/bench/drv-static/latest-result.json`。这是 **错误定位进展**，不是编码器持续通信或实物 FOC 验收通过。

## 11. 编码器无动力连续测试及短窗原始波形通过

用户确认退出原控制台释放 COM4 后，主线程在现有镜像上执行 `test_encoder_off.ps1 -Port COM4 -ConfirmDriverPowerOff -DurationSeconds 180 -SelfTestRuns 3`。边界沿用用户已经确认的 TI 12 V 关闭、三相断开；未修改/烧录固件，未发送 ENC_CLEAR、PREPARE、ALIGN、IQ 或 KEEPALIVE。SELFTEST 是无输出的算法自检，不是电机转动测试。

### 连续测试证据

- 记录 UTC：2026-09-05 15:00:00 至 15:03:04；其中连续测量窗 180.016 秒，前段为 3 次算法自检。
- `enc_ok`：5031547 → 5751944，新增 **720397**；`enc_irq` 同增 720397，有效比例 100%。
- `enc_err`：0 → 0；`uart_err`：1 → 1。没有新增错误，但不把既有 1 次串口错误抹掉。
- 计数估算：编码器 4001.842 Hz、ADC 20009.213 Hz；估算含主机计时/串口延迟，不作为精密频率计结果。
- 自检 3 次通过；214 条 STATUS 均为 `mode=0、fault=0、MOE=0、DRVOFF=1、button=0`。`deadline=0、adc_bad=0、self_fail=0`；结束 `isr_max=3241、self_max=2441`，均小于 3600 cycle 门限。
- 主线程独立复核完整日志：只有同步空行、STATUS、SELFTEST；没有其他命令或输出使能状态。脚本 PASS，共 9170 条检查。

证据：`output/bench/encoder-off/encoder-off-result.json` 和 `encoder-off-transcript.log`。稍后单独读取 STATUS/ENC_DIAG 时 `enc_ok=7995903、enc_err=0`，首错缓存为空，最新有效原始帧 `0000,3690,81FC`；见同目录 `encoder-off-final-snapshot.log`。该快照是后续读数，不把整个上电期间都说成由主机连续记录。

`ENC_CLEAR` 完成两次传输后会清空首错缓存，并在恢复 TIM6 前打印已缓存的 latest；因此紧随清错出现的 `ENC_FIRST sample=0` 和正常 latest，不能独立证明清错后已经持续稳定。正常返回 ERROR `raw=0000` 且 `mask=3/flags=0` 表示本次读回错误位为零；`previous` 才是上一流水线回复。本次稳定性结论来自未清错的连续计数与独立采集，不依赖该误解。

### 已有 ATK 原始数据的离线核对

识别本机窗口为 DL32 Plus；直接只读其配置和恢复缓存，没有操作原生 UI、改写运行中配置或驱动分析仪输出。保留采集标记 `CollectDate=2026-09-05 11:02:28.010`：100 MHz、100000 样本，即 **1 ms**，不是 1 秒。采样门限配置为 1.6 V，D0–D17 启用。

从每通道已存在的首个数据块读取，以官方开源 `Segment::GetSample` 的字节内 LSB-first 样本顺序和起始偏移解释；本脚本只支持这份单块缓存，不充当通用 `.atkdl` 导入器，不解释扩展恢复元数据。原文件副本和 SHA-256 保留在 `output/bench/logic-capture/`，运行 `node output/bench/logic-capture/inspect-encoder-off.cjs` 可复核。

| 数字波形核对项 | 本段结果 |
| --- | --- |
| 编码器命令 | 4 组 `FFFF / 7FFD / 0000`，共 12 个完整帧 |
| 每帧时钟 | 16 个，采样点为下降沿；时钟周期 400 ns，约 2.5 MHz |
| 返回 | 每组 `0000 / 角度 / 81FC`；角度槽 B68F、368D、368D、368E；全部偶校验正确、EF=0 |
| CS 建立/保持/高间隔 | 最小约 550 / 590 / 1620 ns；以 10 ns 采样分辨率计，不是模拟边沿测量 |
| TI 数字状态 | D4 nSCS=1、D8 nSLEEP=0、D9 DRVOFF=1、D10 nFAULT=0，D11–D16 均为 0；TI SPI 没有交易 |
| 采集边界 | 末尾有下一帧 CS 起始，采集结束截断，不计为完整帧或通信故障 |

上述 CS 判据来自 AS5048 DS000298 v1-11 Figure 14/15；时序短窗通过不证明模拟过冲、接地完整性、长期抗干扰或带动力通信均通过。探头究竟位于编码器端还是 MCU 端没有新实物照片确认，不将采集位置写成已目视核实。

### 分析仪显示设置及下一门槛

读取运行配置发现：第一组 SPI 是 CPOL=1/CPHA=0、16 bit；第二组是 CPOL=0/CPHA=0、8 bit。离线检查按正确的 Mode 1 解码，未替用户改动运行中窗口。第一组错误配置同样采下降沿，可能显示可读数值，但仍不符合实际空闲电平；不能把“能解码”视为设置正确。两个 SPI 解码器均应为 **CPOL=0、CPHA=1、16 bit、MSB-first、CS active-low**，第一组 D0–D3、第二组 D4–D7。

TI 电气依据重新核对官方 DRV8316 Rev B 第 53–55 页 §8.4.2/§8.5、第 58 页表 8-13：DRVOFF 高会关闭全部 MOSFET，也可能导致 nFAULT 低；此前 STAT0=09 是 FAULT=1、NPOR=1，不能直接归因于欠压或接线错误。本轮保留 FAULT/BKIN/DRVOFF 保护，没有为了通过 PREPARE 而屏蔽它们。需用户重新确认三相仍断开并开启 12 V/300 mA 后，才进一步取得带电静态证据；若电源进入 CC/降压，先停止检查，不自动加大限流。**尚未校零、对齐、带转或做力反馈。**

## 12. 前次：上电静态故障复现，nFAULT 与 Buck 安装值待核

用户确认 12 V/300 mA 开启，显示 13 mA/0.156 W；三相继续断开、B1 松开。2026-09-05 15:30:54 UTC 仅一次 PREPARE，六个配置寄存器写入及精确读回后得到 `tx=8400 rx=0900 stat_raw=000009 stat_mask=7 nsleep=1 off=1 nfault=0`。返回 `ERR DRV_SPI_READBACK_OR_FAULT` 后立即 STOP 并收到确认。没有驱动 CLR_FLT、ADC 校零、ALIGN、IQ、KEEPALIVE 或 DRVOFF 释放；PREPARE 内部原有一次编码器清错已执行，清错计数变为 2，不能称此命令为纯只读。

STATUS 前后母线 ADC 为 12034/12023 mV，编码器有效计数 12432000 → 12432313，编码器错误维持 0，串口历史错误维持 1，`mode=0、MOE=0、DRVOFF=1、button=0、zero=0、calibrated=0`。未改固件行为、未烧录；只更正了源码中未核实电感值的注释。物理电源仍视为开启，直到用户确认关闭。

官方 PDF 新核对事项：

- SLVUBZ9B 第 27 页图 7-8 的 nFAULT 支路为 3.3VBK → D2 LED → R15 330 Ω → nFAULT；不是经过核验的直接逻辑上拉。第 26 页图 7-7 对应 J3.19。当前 PB12 配置内部下拉。测量 nFAULT/3.3VBK 后再区分原因，不直接换线、短接或改上下拉。
- SLVUBZ9B 第 26 页图 7-6 默认 L1=47 μH，22 μH 的 L2 为 DNP。此前代码/README 断言原板为 22 μH，没有足够实物依据，已标记 HOLD。DRV8316 Rev B 第 25 页表 8-5、第 67 页表 8-23 对 3.3 V 的 47 μH 条件给出 CTRL6=`0x10`，22 μH 条件为 `0x08`；BUCK_PS_DIS 是 bit4。未确认安装值前不更改运行参数，也不再 PREPARE。
- DeepSeek Pro 完成独立控制流审查（任务 `20260905153418-884e3499c53d`）；其条件性 DRVOFF/nFAULT 门冲突分析有价值，但不构成硬件因果证明。主线程按手册纠正“可能没等完唤醒”的泛化：实际代码已等 10 ms，超过 tWAKE 最大 1 ms；也不把 NPOR=1 当 POR 故障。

下一项：在保持三相断开、输出关闭时测 J3.19 对 AGND 与 3.3VBK 测试点对 AGND 的直流电压；测后关闭 TI 12 V，拍电感标记与位号。不能安全探测则先关电，不带电改线。进一步 CLR_FLT、上下拉或 DRVOFF 状态变更尚未授权执行；不使用自动清故障或错误掩码放行。

证据：`output/bench/drv-static/prepare-retest-20260905-session.log` 与 `latest-result.json`。本轮是静态故障复现及参数依据纠正，不是电流环或力反馈通过。

## 13. 最新：47 μH 配置读回通过，静态 FAULT 仍存在

用户补充万用表测量：J3.19 nFAULT 对 AGND 为 **0.13 V**，板上 3.3VBK 测试点对 AGND 为 **3.26 V**；照片可见 **L1 / 47uH / 470**。这确认了 TI 排针处低电平和实际 Buck 安装值，但没有测得上电瞬态，也没有证明 nFAULT 原因。照片源文件为 `G:/Cache/xwechat_files/wxid_nawzs3g0usr511_e25d/temp/RWTemp/2026-09/9e20f478899dc29eb19741386f9343c8/d9853f642e49f2dffc2685059b176406.jpg`。

主线程逐页复核官方 SLVUBZ9B 第 26 页图 7-6、SLVSF16B 第 25 页表 8-5及第 67 页表 8-23：47 μH、3.3 V 对应 `BUCK_PS_DIS=1（bit4）、BUCK_CL=0（bit3）、BUCK_SEL=0、BUCK_DIS=0`，CTRL6=`0x10`。原镜像 `0x08` 的 22 μH 假设已被实物证据否定。内部 Buck 600 mA 档不改变台式电源 300 mA 限流，不代表可用相电流；不必为匹配旧代码改焊电感。

生产 Worker 仅修改 `bench_hw.c`，主线程检查并修正注释适用边界后独立验收：配置表和运行状态读回使用同一个 `DRV8316REVM_CTRL6_BUCK_3V3` 常量。未修改其它寄存器、SPI、nSLEEP 时序、GPIO、故障掩码、nFAULT/BKIN/DRVOFF 门、清错或电机参数。

2026-09-06 03:19:56 UTC 已验证：

- 原 CubeMX 生成的 Keil ARMCLANG 6.21 工程完整 Rebuild，0 错误、0 警告。
- `cmake --build build/stm32-host --config Debug --parallel 2` 成功；`ctest --test-dir build/stm32-host -C Debug --output-on-failure --timeout 10 -V` 三组全部通过，检查数 271 / 649124 / 262248。这些已有主机测试不编译 `bench_hw.c`，不是 Buck 硬件验收；未新建或修改单元测试。
- 独立源码检查确认 CTRL6 字段=0x10、写入/读回共享值、两处 NPOR 的 `0x08` 掩码保留。
- 新镜像 27752 字节，SHA-256 `9D66823473B32EB78A7D522AF7F658C99702896946BFF89B4C9F7167DCFA7C03`。与此前已回读的 `98392FB757FDD3ABF2CC80141B890942855FD6DB640F0962B4EB775E2488572B` 镜像等长，仅偏移 `0x35C8、0x3624、0x380C` 三个字节由 `08` 变为 `10`。这一比较不替代上板寄存器读回。

### 用户关电确认后的实板验证

用户回复“已经关闭”后，主线程于 2026-09-06 03:29:31–03:29:32 UTC 操作唯一匹配的 ST-LINK（`003E002F3235511337333439`），CubeProgrammer v2.21.0 在 SWD Under Reset / 1000 kHz 下烧录上述精确镜像到 `0x08000000`，校验成功后复位 MCU。随后 HOTPLUG 上传完整 27752 字节，SHA-256 与 `9D66823473B32EB78A7D522AF7F658C99702896946BFF89B4C9F7167DCFA7C03` 完全一致；未修改选项字节或保护设置。

首次运行无动力检查时，在空行同步/首个 STATUS 后收到 `ERR LINE_TOO_LONG_OR_BINARY`，脚本立即报 FAIL，尚未执行 SELFTEST。保留 `encoder-off/` 的原始失败记录。随后单独 STATUS 返回正常 OFF/health=34/enc_err=0/uart_err=1，原样保留串口错误计数，未清错或复位来消除它；原因未确定。

确认状态后，以原有检查断言执行 60 秒/3 次 SELFTEST，另存 `encoder-off-confirmed/`，不覆盖失败或此前 180 秒的证据。只给现有脚本增加 `-EvidenceDirectory` 输出路径参数，未改变测试阈值或忽略错误。脚本语法检查及实际输出位置通过验证。

| 新镜像无动力验收项 | 实测结果 |
| --- | --- |
| 连续测量时间 / 自检 | 60.016 秒 / 3 次通过 |
| 编码器有效计数 | 553844 → 794146，新增 240302，有效率 100% |
| 错误计数 | enc_err 0 → 0；uart_err 1 → 1，观察窗内无新增，不等于整次会话无错误 |
| 独立日志复核 | 94 条 STATUS；全部 mode=0、fault=0、MOE=0、DRVOFF=1、B1=0、calibrated=0、zero=0；4010 项检查通过 |
| 时序 / 数据 / 看门狗 | 末态 self_max=2441、isr_max=3240、deadline=0、adc_bad=0、iwdg_reset=0 |
| UART 命令集合 | 空行同步、STATUS、SELFTEST；没有 ENC_CLEAR、PREPARE、ALIGN、IQ、KEEPALIVE |

上述无动力阶段保持 TI 12 V 关闭、USB 保留、三相断开；脚本退出并释放 COM4。STATUS 的 ADC 推算值在未供电/未校零时不作为万用表电压或真实相电流证据。之后用户授权的上电复测如下，不把前一阶段的 OFF 状态当成当前物理供电状态。

构建/测试证据：`G:/Agent/.cache/gl30-buck47-20260905/`（目录沿用台架会话日期；实际复核 UTC 见上），当前结果 `output/bench/drv-static/latest-result.json`。

### 修正镜像的单次带电静态复测

用户回报“13ma，0.156w，我已开启”，沿用 12 V/300 mA、三相断开。2026-09-06 03:42:21 UTC 主线程先确认 STATUS 的安全门、编码器与母线范围，仅执行一次 PREPARE，失败后立即 STOP。原始记录 `output/bench/drv-static/prepare-buck47-20260906-session.log`：

```text
DRV_DIAG tx=8400 rx=0900 transfer_ok=1 stat_raw=000009 stat_mask=7 nsleep=1 off=1 nfault=0
ERR DRV_SPI_READBACK_OR_FAULT
OK STOP
```

| 项目 | 本次证据与边界 |
| --- | --- |
| 配置 | 与已完整回读镜像匹配的代码路径完成六项写入及精确读回，包含 CTRL6=0x10；日志并非逐项寄存器转储 |
| 故障 | STAT0=09、STAT1=00、STAT2=00，三个公开状态字均已读到；FAULT=1、NPOR=1（没有 POR），nFAULT=0，根因未确定 |
| 停止状态 | 前后均 mode=0、MOE=0、DRVOFF=1、B1=0、calibrated=0，STOP 已确认；nSLEEP=1，软件 STOP 不切断 TI 电源 |
| 母线 / 编码器 | ADC 推算 12077 → 12045 mV；enc_ok 3075551 → 3076009，新增 458；enc_err=0、uart_err=1 均未增加，不是持续带动力验证 |
| 编码器内部清错 | PREPARE 既有清错计数 0 → 1，ERROR raw=0000、mask=3、flags=0；没有另发 ENC_CLEAR |
| 未执行 | 驱动 CLR_FLT、配置重锁、ADC 校零、ALIGN、IQ、KEEPALIVE、DRVOFF 释放、MCU 复位、烧录 |

独立日志核对只有 STATUS、ENC_DIAG、PREPARE、STOP、STATUS、ENC_DIAG 六条命令，COM4 已关闭。主机脚本完成不是驱动通过；STATUS 的 `fault=0` 是 MCU 状态，不覆盖 TI FAULT；未校零的相电流读数不能当成真实电流。此前 0.13 V/3.26 V 是修正复测之前的用户万用表数据，不是本次新测量。正确 Buck 参数没有消除现象，也不能据此完全排除启动瞬态影响或判板坏。

### 官方网络核对及最少下一步

- SLVUBZ9B 第 25–27 页图 7-4/7-7/7-8：**R8=5.1 kΩ 是 AVDD 到 SDO/MODE 的上拉，不是 nFAULT 上拉**。nFAULT 经 R50=0 Ω 到 J3.19，并有 3.3VBK → D2 LED → R15=330 Ω 支路；所核对网络未画独立直拉电阻，不等于实物所有元件及外接支路均已验证。
- SLVSF16B 第 19 页表 8-1 推荐裸芯片使用 5.1 kΩ nFAULT 上拉，第 53 页要求外部上拉电源条件下上电 nFAULT >2.2 V；同页 §8.4.2 说明 DRVOFF 高可触发 nFAULT 低。静态低电平不能证明内部测试模式，也不能证明一定由 DRVOFF 导致。不直接加电阻、改 PB12 上下拉或绕过保护。
- DeepSeek Pro 审查 `20260906034907-2569bb7994d2` 已完成；接受先取证、不绕过关断的意见。主线程按第 63 页表 8-19 **纠正其 CLR_FLT 写 0x69 后应读回 0x69 的错误：该位自动清零，正确预期是 0x68**，未执行该清错。MCU nSLEEP=1 不替代实物波形；公开 STAT0/1/2 已全部读出，不声称漏读未知故障寄存器。模型审查不作为电路依据。
- 用户随后明确说“我关电测的”，TI 12 V 已按其说明记录为关闭；三相保持断开、B1 松开。目标采集接点为 CH1=TI J3.19 nFAULT、CH2=TI J3.17 nSLEEP，接地夹只接 TI AGND，不能接相输出或 SW_BUCK。×10 探头与通道倍率一致，DC/高阻输入，初始 1 V/div、5 ms/div、CH2 上升沿约 1.5 V、单次触发且保留预触发。但下述照片尚未确认探头落点，先核对再协调采集，不盲重试。
- 该采集针对 nSLEEP 唤醒，不代替 VM 真正上电时 >2.2 V 条件的模拟验证；如仍需排查该条件，再另安排上电触发。没有新模拟波形前不继续相同 PREPARE、不清驱动故障、不解除 DRVOFF、不烧录。

当前结论：**DRV_BUCK47_READBACK_OK_STATIC_FAULT_HOLD，ADC 校零/闭环/实物 FOC 均未通过。**

### 关电后的示波器照片：待确认探头落点

用户提交 SDS5054X 三张照片：均显示 Stop，C1=100 mV/div、C2=2 V/div，10X/DC1M/20 MHz 限带；C2 频率读数 12.01417 kHz。三种水平刻度分别为 5 μs/div、200 μs/div、5 ms/div，不能算成三次独立实验。C1 最大约 133–137 mV、最小约 -150 至 -200 mV，只是当前接法下的低幅信号，不据此判 nFAULT 损坏、短路或故障解除。

本轮重新检查源码：nSLEEP 在初始化时置低，PREPARE 时低 2 ms 后高并等 10 ms，STOP 不改变它，没有周期性 12 kHz 输出。编码器约 4 kHz 采样且每组 3 次片选交易，约 12 kframe/s；照片特征与编码器 CSn 相近仅是核对方向，不是已经证明接错。USB 如仍接通，TI 关电后编码器通信仍可运行。本轮没有串口命令、复位、烧录或驱动操作。

按 SIGLENT SDS5000X 官方手册 EN01F 第 40–41、43 页核对：C1 的 66.7 mV、C2 的 -4.23 V 是垂直偏移；Stop 显示最后采集，不代表实时刷新。最少下一项是能辨认探头尖端、地夹及脚位的板端近照；确认接点后将两路设为 1 V/div，用 Auto/Run 取得新的静态记录，再准备单次唤醒。TI 保持关闭。原始照片与手册链接已记入主设计 MD；没有新增 FOC 通过结论。

## 14. 接线修正后：单次唤醒已捕获，STAT0变为01但仍拒绝就绪

2026-09-06 05:47:06 UTC；唯一原始日志 `output/bench/drv-static/prepare-wiring-corrected-20260906T054705804Z-session.log`。六条命令依次 STATUS、ENC_DIAG、PREPARE、STOP、STATUS、ENC_DIAG，无重试。PREPARE回报：

```text
DRV_DIAG tx=8400 rx=0100 transfer_ok=1 stat_raw=000001 stat_mask=7 nsleep=1 off=1 nfault=0
ERR DRV_SPI_READBACK_OR_FAULT
OK STOP
```

六项配置精确读回按匹配源文件/镜像控制流通过，驱动CLR_FLT、重锁和ADC校零仍未执行。前后mode0/moe0/off1/button0/zero0/calibrated0；enc_ok新增455，enc_err0与uart_err1均无新增。PREPARE内部编码器ERROR读/清错计数1→2，raw0000/mask3/flags0；未发独立ENC_CLEAR。VM推算12077→12066 mV，未校零约2.9 A不是相电流实测。COM4已关闭，12 V需用户另行关闭并确认。

新图 `codex-clipboard-f5d32497-aad3-43fd-acd3-949b8137798e.png`：按确认的CH1=nFAULT、CH2=nSLEEP测点，两路1 V/div、5 ms/div、10X/DC1M/20 MHz，C2低脉冲目测约2 ms，之后高；C1平均99.31 mV、最大166.7 mV、最小-33.33 mV，未恢复有效高。通道框2.40 V/-1.67 V是偏移。照片不替代真正VM上电、AVDD/VBK、六路IN安全验证。

按SLVSF16B表8-13，新01=FAULT1/NPOR0，不能断言持续欠压。采纳外部审核的分阶段错误报告、完整返回帧、异常退出尽力重锁和配置/运行就绪分离方向，尚未改代码。DRVOFF条件性互锁和LED支路/PB12下拉关系均待实证；未来单变量对照不得用“必须变08”作为唯一标准，因为NPOR可能在不清错时保持低。不得绕过BKIN/故障门、循环清错或自动带转。官方依据及裁定见主设计MD和独立审核说明。

## 15. 2026-09-07：HAPTIC8 时钟修正及自主续测

本阶段全部由主线程执行，不调用其他模型，不删除历史证据，不要求人工在旁守台。完整阶段顺序、源码清单、日志与限制统一维护在[2026-09-07 续测验收报告](../../../docs/bench-validation-20260907-cn.md)，避免旧页首静态故障被误当成当前结论。

| 项目 | 结果及边界 |
| --- | --- |
| 首故障诊断 | HAPTIC7 修复相关失败路径的冻结快照；先 13 个失败断言，修正后 420 个诊断断言通过，HAPTIC8 保留该修正 |
| ADC 时钟 | 按 ST ES0430 §2.7.11 改为共同 PLLP 40 MHz、异步 ÷1；实机 RCC/ADC common 寄存器核对通过。保持 PWM 20 kHz、采样长度、rank 和既有保护 |
| 台架构建/镜像 | Keil 0 error/0 warning，47288 字节，烧录及完整镜像回读一致；SHA256 见完整报告 |
| 软件回归 | C 6/6；ASan+UBSan 6/6；4 个 workspace 类型检查；TS 21/21；无硬件 E2E 通过 |
| 无 PWM 实机 | 3 轮编码器合计 6144/6144 有效点，逐间隔 250 μs；60.477 s 准备态未新增错误；125 个电流快照只是稀疏诊断，不是连续波形 |
| 命令/故障回归 | 56 个无出力用例通过；软件注入 1024、STOP 保留故障、CLEAR 保留首故障但不恢复校准资格；不是外部硬件故障链认证 |
| 控制台 | 新增 ENC_FIELD/CURRENT_DIAG 入口；真实串口集成通过。测试工具首轮队列作用域错误及修正后日志均保留 |
| 产品工程 | 同步修正 ADC 时钟，构建 0 error/0 warning；未刷硬件，PWM 仍 40 kHz，factory gate 不放行 |

本版本仅一次 ALIGN，约 0.484 s 在初始静态场阶段触发 HEALTH=1；首故障 raw=2018,1790,1799,1244，三相换算和约 -14.93 mA，VM 换算 13.45064 V。软件门自动关断通过，校准本身失败。尚无证据区分真实 VM 瞬态与模拟/采样异常，不重试带电校准、不放宽阈值。正反 IQ、8 类真实触觉和整机 G4/G5/G7 仍未通过。

最后实测于 2026-09-07 17:18:04 UTC：mode=0、fault=0、MOE=0、DRVOFF=1、zero=0、calibrated=0，deadline/adc_bad/enc_err/uart_err/self_fail/iwdg_reset 均为 0；COM4 关闭，无后台运动任务。缓存 CURRENT_TRIP=1024 来自本轮 FAULTTEST 注入，不是新的母线/电流故障。真实 ALIGN 首故障已独立归档；软件 STOP 不等于动力电源断开。
