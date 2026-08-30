# GL30 AMOLED V7 当前架构

> 当前等级：`WIRELESS_POWER_DEFAULT + CAD_CHECKED + CUBEMX_GENERATED + KEIL_AC6_BUILD_ONLY + SIM_ONLY`。
> 当前阻塞：CubeMars 工厂编码器接口、安装面、出线、通孔和载荷数据未公开。

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

`SYS` 是首板唯一默认电机输入路径，不同时保留两套生产实现；它仍是 `HARDWARE_REQUIRED`，必须与独立电池母线做一次脉冲负载、充电终止和回灌 A/B。制动耗能位于隔离后的 `MOTOR_BUS`，在主 MCU 关机或电机高侧开关断开后仍要能处理反拖。Waveshare 模块的 1S 电池口不接产品电池。

当前机械参考为窄型 3S 800 mAh 包，不承诺续航。初始充电目标为 12.45 V/0.4 A，最低运行目标为 9.0 V；BMS、电芯、充电阈值、回灌阈值和正式容量均是 `PCB_HOLD + HARDWARE_REQUIRED`。

## 4. STM32 当前安全链

厂家回复前：

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

厂家确认后只实现一种真实接口，不同时维护 SPI/ABI/PWM 多后端，不保留 MT6835 路径。

其余可复用控制链：

```text
三相电流 ADC 同步采样
  -> 40 kHz FOC 纯函数/调度
  -> TIM1 互补 6-PWM
  -> DRV8316

HARD_FAULT_N -> TIM1 BKIN -> 异步关断
ESP32 <-> USART3 DMA <-> 当前二进制协议
```

唯一 STM32 工程由 STM32CubeMX 6.18.1 生成 MDK-ARM，外设和中断使用 LL，Keil ARMCLANG 6.21 clean rebuild 已达到 `0 Error(s), 0 Warning(s)`。这只证明工程可编译链接，不证明 MCU 已启动或实时截止期已满足。

## 5. 当前冻结与 HOLD

| 项目 | 当前结论 |
| --- | --- |
| 产品电机方向 | GL30 KV290 工厂编码器版，已冻结 |
| 自制 MT6835 磁环 | 已从活动产品树删除 |
| 编码器固件接口 | `PENDING_VENDOR`，失效安全，可构建 |
| 编码器电气/连接器/供电 | `PCB_HOLD` |
| 外壳/旋环默认几何 | `CONCEPT_FIT_DEFAULTS` |
| 无线电源拓扑 | `3S + BQ25798 SYS + 外部隔离 + 独立 MOTOR_BUS 制动`，首板默认 |
| 电池容量、BMS、电芯与充电参数 | `PCB_HOLD + HARDWARE_REQUIRED` |
| SYS 电机路径的脉冲/回灌能力 | `HARDWARE_REQUIRED`；与独立 BAT 轨 A/B 后只保留通过者 |
| 四个普通键/灯条/电源键/Type-C | 左右侧各两键、正面底部嵌入式灯条、后置 PWR/QON、单后置 Type-C，概念几何已固定 |
| 电机安装面、出线、中心孔、轴承 | `MECHANICAL_HOLD` |
| 实物 FOC/热/手感 | `HARDWARE_REQUIRED` |

## 6. 事实源优先级

1. CubeMars/Waveshare/ST/TI 的正式资料与厂家书面回复。
2. `firmware-stm32/config/board_config.h` 和当前生产代码。
3. `hardware/cad/v7_params.py` 与几何报告。
4. `protocol/schema/protocol-v1.md` 与金样。
5. 当前 V7 手册；若与官方资料冲突，以核对后的官方事实和当前工程为准。
