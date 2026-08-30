# GL30 AMOLED V7 当前架构

> 当前等级：`CAD_CHECKED + CUBEMX_GENERATED + KEIL_AC6_BUILD_ONLY + SIM_ONLY`。
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

当前模型预留 6808 尺寸包络，使用户载荷不默认经过 GL30/编码器。实际轴承、配合、预紧和寿命没有冻结；如果厂家证明 GL30 轴承可带规定用户载荷，可删除独立支撑。

## 3. STM32 当前安全链

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

## 4. 当前冻结与 HOLD

| 项目 | 当前结论 |
| --- | --- |
| 产品电机方向 | GL30 KV290 工厂编码器版，已冻结 |
| 自制 MT6835 磁环 | 已从活动产品树删除 |
| 编码器固件接口 | `PENDING_VENDOR`，失效安全，可构建 |
| 编码器电气/连接器/供电 | `PCB_HOLD` |
| 外壳/旋环默认几何 | `CONCEPT_FIT_DEFAULTS` |
| 电机安装面、出线、中心孔、轴承 | `MECHANICAL_HOLD` |
| 实物 FOC/热/手感 | `HARDWARE_REQUIRED` |

## 5. 事实源优先级

1. CubeMars/Waveshare/ST/TI 的正式资料与厂家书面回复。
2. `firmware-stm32/config/board_config.h` 和当前生产代码。
3. `hardware/cad/v7_params.py` 与几何报告。
4. `protocol/schema/protocol-v1.md` 与金样。
5. 当前 V7 手册；若与官方资料冲突，以核对后的官方事实和当前工程为准。
