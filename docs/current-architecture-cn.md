# GL30 AMOLED V7 当前架构

2026-10-02 当前代码与交付见[离线入口](../outputs/project-goal-closeout-20261002/README_CN.md)：产品AS5048A端口、驱动/供电/采零保护链已实现离线候选；ESP32为KK_OLED/KK_UI同源界面，新增NVS设置保存及STM32/显示独占维护。三配置ESP32各8/8、STM32各15/15，固件构建通过；未烧录，CET6实体资格和整机手感仍未验收。下方按日期保留设计与台架历史，不能覆盖当前工程的实板边界。

> **2026-09-09 产品设计增量：**新增旋环按压、底部至少 24 颗 RGB、灯光跟随实测角度并替代实体标记。按 3 块自制 PCB 加显示/原厂编码器/电池保护组件规划，详见[板件与装配方案](knob-press-rgb-architecture-cn.md)。这次只更新方案与文档；下述既有 CAD、固件和台架验收不自动覆盖新增功能。

> **当前状态（2026-09-08 UTC）：`BOUNDED_BENCH_ACCEPTANCE_PASS`，产品仍为 pre-alpha。**
> NUCLEO-G474RE + TI DRV8316REVM + GL30/AS5048A 的 H25 已通过受限 ALIGN、双向 IQ、八模式 smoke 和新增状态机/通信压力回归，见 [扩展验收](bench-validation-20260908-haptic25-extended-cn.md)。

9 月 6 日静态 Buck 状态和 H8/H10 中间失败属于历史，不再作为“当前禁止所有台架输出”的状态；历史证据没有改写。台架通过不放行 CET6 端口、无线功率路径、AMOLED 整机、手感计量、热或回灌，也不替代外部 nFAULT→BKIN 整链实测。供电设定和 H25 保护阈值未更改；最终硬件关断证据见报告。

## 1. 活动目录

```text
GL30-Haptic-Control/
├─ firmware-stm32/          # STM32G474 电机、安全、保护、厂家编码器待确认边界
├─ firmware-esp32/          # Waveshare 显示/UI/音频/通信框架
├─ pc-companion/            # PC 协议、Profile、设备模拟、Trace/更新框架
├─ protocol/                # 当前线协议、跨语言金样和参考实现
├─ hardware/
│  ├─ cad/                  # 官方部件、参数化模型和 CONCEPT_FIT_DEFAULTS 输出
│  ├─ motor-control/
│  ├─ power-interface/
│  └─ mechanical/
├─ design/reference/current # 当前 V7 手册；不保存 V6 文件树
├─ docs/                    # 当前执行手册、闸门、厂家清单和时间表
└─ artifacts/               # 仅放当前验证产物
```

## 2. 产品机械链

以下为**既有 CAD**的机械链。新增按压方案改用独立承载的轴向浮动组件，灯环固定于底座；按压力不得默认经过 GL30 轴承、编码器或屏幕玻璃。原实体标记/前灯条由新方案取代，尚未改模；载荷、行程、遮光和装配空间须按专项方案重新核对。

```text
固定链：机壳/后支架 → GL30 定子（具体安装面待厂家确认）
        机壳/中心支撑 → 固定轴承内侧/固定边框 → Waveshare 屏幕

旋转链：手指 → 金属旋环 → 独立支撑包络 → 机壳
        GL30 转子 → 柔顺扭矩耦合 → 旋环
```

当前无线模型外廓为 `96 × 96 mm`、前缘 `20 mm`、后缘 `52 mm`，相对旧 `128 × 100 mm` 占地减少 `28%`。四个普通键移到左右侧壁（每侧两个），正面底部保留 `64 × 2.4 mm` 嵌入式状态/氛围灯条；后侧保留独立凹入式 `PWR/QON` 键，底部只留 BOOT/RESET 服务针孔，后侧只有一个 PD 充电兼 USB2 数据 Type-C。

当前模型预留 6808 尺寸包络，使用户载荷不默认经过 GL30/编码器；后仓同时预留 `84 × 24 × 20 mm` 的 3S 电池安全包络和 `84 × 24 × 8 mm` 电子板包络。电池包络与官方 GL30、中心支撑的数字实体交集均为 `0 mm³`。实际电池、轴承、配合、预紧、热路径和寿命没有冻结；如果厂家证明 GL30 轴承可带规定用户载荷，可删除独立支撑。

## 3. 无线电源链

```text
单个 USB-C：HUSB238 请求 15 V，同时保留 USB2 数据
  -> BQ25798 3S buck-boost charger / power path
     -> BAT：3S 电芯组 + BQ77915 保护/均衡 + NTC
     -> SYS：逻辑电源 + 外部双向高侧隔离
             -> INA228 -> MOTOR_BUS -> DRV8316 -> GL30

MOTOR_BUS -> 独立比较器 -> 制动 MOSFET/电阻
          -> 过压关断 -> TIM1 BKIN
```

`SYS` 是首板默认电机输入路径，不同时保留两套生产实现；它仍是 `HARDWARE_REQUIRED`，必须与独立电池母线做一次脉冲负载、充电终止和回灌 A/B。制动耗能位于隔离后的 `MOTOR_BUS`，在主 MCU 关机或电机高侧开关断开后仍要能处理反拖。Waveshare 模块的 1S 电池口不接产品电池。

当前机械参考为窄型 3S 800 mAh 包，不承诺续航。初始充电目标为 12.45 V/0.4 A，最低运行目标为 9.0 V；BMS、电芯、充电阈值、回灌阈值和正式容量均是 `PCB_HOLD + HARDWARE_REQUIRED`。

## 4. STM32 当前安全链

以下门禁属于尚未完成实板端口的 **产品 CET6 工程**；不是说 AS5048A 型号未知，也不表示 NUCLEO 联调驱动尚未实现：

```text
factory_encoder_pending
  -> status = PENDING_VENDOR
  -> valid = false
  -> encoder gate = false
  -> persistent warning + arm denied
  -> TIM1 MOE off + DRV8316 safe off

ACTIVE 后编码器 invalid/stale
  -> 4 kHz 监控锁存 GL30_FAULT_ENCODER
  -> TIM1 MOE off + DRV8316 safe off
```

产品硬件端口完成后仅接入已确定的 AS5048A SPI 驱动，不同时维护 ABI/PWM 多后端，不恢复 MT6835 路径。台架按 NUCLEO 工程既有联调流程推进，不等待所有未来产品载荷资料齐备。

其余可复用控制链：

```text
三相电流 ADC 同步采样
  -> 20 kHz FOC 首测基线（NUCLEO+TI EVM）
  -> TIM1 互补 6-PWM
  -> DRV8316

HARD_FAULT_N -> TIM1 BKIN -> 异步关断
ESP32 <-> USART3 DMA <-> 当前二进制协议
```

产品 CET6 工程与 NUCLEO RET6 联调工程使用同一控制内核、不同硬件端口，均由 STM32CubeMX 6.18.1 生成 MDK-ARM，外设和中断使用 LL。Keil ARMCLANG 6.21 已有两者的 0 错误/0 警告构建记录。NUCLEO H25 已完成真实电机受限 ALIGN、双向小电流和八模式定时 smoke；不再停留在 TI 静态配置阶段。产品 CET6 仍未完成实板端口验证。最新实测边界见 [H25 扩展验收](bench-validation-20260908-haptic25-extended-cn.md)；旧故障按历史记录保留。产品 40 kHz、EVM 20 kHz，不能互相代替参数或验收。

## 5. 当前冻结与 HOLD

| 项目 | 当前结论 |
| --- | --- |
| 产品电机方向 | GL30 工厂编码器版实物已到；7 极对和 `Kt=0.038 N·m/A` 作初值；官方网页与到货空载点对应 `255 rpm/V`，`KV290` 视为型号标签，不强制要求先测 Ke 作为首测前置条件 |
| 自制 MT6835 磁环 | 已从活动产品树删除 |
| 编码器固件接口 | `AS5048A + SPI`；NUCLEO 联调已有真实驱动；产品后端 `PENDING_VENDOR` 保留至产品硬件端口完成验证 |
| 编码器电气/连接器/供电 | NUCLEO H25 台架已有 AS5048A SPI/角度诊断和有限 ALIGN 实测；不等于完整角度精度、产品电气端口或连接器料号已确认 |
| 外壳/旋环默认几何 | `CONCEPT_FIT_DEFAULTS` |
| 无线电源拓扑 | `3S + BQ25798 SYS + 外部隔离 + 独立 MOTOR_BUS 制动`，候选工程基线；台架先用外部电源 |
| 电池容量、BMS、电芯与充电参数 | `PCB_HOLD + HARDWARE_REQUIRED` |
| SYS 电机路径的脉冲/回灌能力 | `HARDWARE_REQUIRED`；与独立 BAT 轨 A/B 后只保留通过者 |
| 四个普通键/灯光/电源键/Type-C | 保留左右各两键、后置 PWR/QON 与单 Type-C；新方向为旋环按压及底部至少 24 RGB，替代原前灯条与实体标记，尚未改 CAD |
| 自制 PCB 数量 | 规划控制板、功率板、RGB/按压板共 3 块；显示、原厂编码器及 BMS 另计，排布/层数/连接器尚未冻结 |
| 电机安装面、出线、中心孔、轴承 | `MECHANICAL_HOLD` |
| 实物 FOC/热/手感 | H25 受限校准、电流与模式 smoke 已通过；完整手感、速度跟踪性能、热、回灌及整机仍为 `HARDWARE_REQUIRED` |

## 6. 事实源优先级

**电路专项约束（用户要求，2026-09-05）：电路结论只能依据对应器件与评估板的官方手册、官方原理图。** 记录型号、文档编号/版本、章节或表格、适用条件；资料不足时标记待厂家确认，不以其他型号、第三方教程、商家介绍或 AI 回答替代。千问、DeepSeek 等仅协助核查，不作为改线、选电平、解释故障或解除保护的依据。手册规定、实测记录、待验证假设必须分开；软件通过不代表实板通过。

该约束已同步写入[主设计 MD](G:/Agent/GL30_AMOLED_V7_Product_Edition/design/reference/current/GL30_AMOLED_V7_Product_Edition_Design_Manual.md)开头，不提升版本。当前台架按 TI DRV8316 Rev B、DRV8316REVM SLVUBZ9B、ams AS5048 DS000298 v1-11、ST UM2505/MB1367 对应资料核对；实际波形和读数另见[联调验收记录](../firmware-stm32/bench/NUCLEO_G474RE_FOC/TEST_RESULT_CN.md)。

1. CubeMars/Waveshare/ST/TI/ams OSRAM 的对应官方手册、官方原理图；厂家书面回复作为待核验补充，不替代器件电气限制。
2. 对应硬件端口的当前代码/配置；EVM 操作参照 `firmware-stm32/bench/NUCLEO_G474RE_FOC/README_CN.md`，产品板参照 `firmware-stm32/config/board_config.h`。
3. `hardware/cad/v7_params.py` 与几何报告。
4. `protocol/schema/protocol-v1.md` 与金样。
5. 当前 V7 手册；若与官方资料冲突，以核对后的官方事实和当前工程为准。

最新进展和开源借鉴决策见 [2026-09-08 项目审查](project-progress-review-20260908-cn.md)。模型用途及早期取舍保留在 [2026-09-05 历史复盘](project-reassessment-cn.md)，其中旧首测条件不作为当前操作指令。
