# GL30 Haptic Control

[English](README.md) · [路线图](ROADMAP.md) · [贡献指南](CONTRIBUTING.md) · [证据边界](docs/evidence-boundary.md)

[![CI](https://github.com/Master-1st/GL30-Haptic-Control/actions/workflows/ci.yml/badge.svg)](https://github.com/Master-1st/GL30-Haptic-Control/actions/workflows/ci.yml)
[![状态：pre-alpha](https://img.shields.io/badge/status-pre--alpha-orange)](ROADMAP.md)
[![许可证：Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue)](LICENSE)

**面向软件定义物理交互的开源实时触觉控制平台。**

它要把“旋钮拧起来是什么感觉”从固定机械结构或写死的电机代码，变成应用可以配置的 **Haptic Profile**：同一套硬件可以表现为有刻度的旋钮、带回弹的控制器、有软限位的调节器、带阻尼的飞轮，或随软件状态变化的触觉界面。

CubeMars GL30 力反馈旋钮是首个参考设备，但平台面向的不只是旋钮，而是 CNC、音视频、CAD、机器人、仿真设备和自定义控制台等软件定义物理接口。

![GL30 Haptic Control 概念渲染](hardware/cad/out/CONCEPT_FIT_DEFAULTS/V7_CONCEPT_FIT_DEFAULTS_isometric.png)

> [!IMPORTANT]
> **当前真实状态：** 无实物部分已经可以构建、测试和仿真；STM32 中的 FOC、触觉原语、安全链、遥测和故障追踪已经实现到代码层。由于 GL30 工厂编码器接口仍待厂家书面确认，真实电机目前被安全门主动禁止上力，因此尚不能宣称已经实现实物闭环或真实触感。

## 它最终能做什么

应用选择一个 Profile，设备就同时改变触感、输入行为和显示内容，而不用为每个应用重写电机控制算法。

| 场景 | 目标交互 |
| --- | --- |
| CNC / 机床手轮 | 粗细进给档位、速度相关阻尼、轴端软限位和危险区触觉提示 |
| 视频剪辑 / DAW | 逐帧或逐拍档位、时间轴惯性、标记点吸附和快速拖动 |
| CAD / 三维软件 | 参数步进、缩放阻尼、模式切换和可感知的数值边界 |
| 机器人 / 遥操作 | 关节限位、回中、阻力变化、状态告警和受限主动引导 |
| 游戏 / 驾驶仿真 | 编码器、配平、阻尼、弹簧和随工况变化的控制手感 |
| 仪器 / 智能设备 | 音量、菜单、精密设定、带屏状态显示和自定义快捷操作 |

这些是平台目标，不是当前成品功能。下方状态表明确区分哪些已经可用、哪些只完成了代码、哪些尚未实现。

## 为什么这个方案有价值

| 优点 | 带来的实际价值 | 当前依据 |
| --- | --- | --- |
| **独立实时电机核** | AMOLED 刷新、BLE/Wi-Fi 流量或上层软件卡顿不进入电机控制关键路径 | STM32G474 独立运行 40 kHz FOC 和 2 kHz 触觉任务；ESP32-S3 只负责 UI、连接和配置 |
| **触感由 Profile 定义** | 应用通过参数组合档位、弹簧、限位、阻尼、摩擦和惯量，不需要修改 FOC | JSON Schema、示例 Profile、二进制命令和六类基础触觉原语已经存在；端到端映射仍待完成 |
| **默认断能的安全设计** | 通信中断、编码器无效、过流或控制超时不会继续输出旧力矩 | DRVOFF、TIM1 BKIN/MOE、启动门、命令超时、看门狗、力矩/电流/转速限制和锁存故障已进入代码与主机测试 |
| **可测量、可回放、可定位** | 不只凭“手感不错”调参，而是能观察电流、力矩、时序、丢包、温度和故障前数据 | 2 kHz 快速遥测、慢速功率/温度遥测、4096 × 6 故障冻结 trace 和确定性协议向量已实现 |
| **从一开始就为复现准备** | 别人能够检查设计、重复测试并指出问题，而不是只能看到成品演示 | CubeMX/Keil 工程、协议、模拟器、测试、BOM、画板指南、参数化 CAD 和证据边界已公开 |

这里的优势是**设计与工程能力**，不是已经完成的性能宣传。力矩质量、噪声、温升、寿命和实时带宽必须等实物测量后再给结论。

## 能否复用前人的配置和生态

**可以复用，但必须区分“原生兼容”“转换兼容”和“只参考交互思想”。** 项目不会要求社区从零重写所有配置，也不会把前人硬件专属的校准值、PID 和电流参数直接带进 GL30。

| 来源 | 当前兼容结论 | 对用户的意义 |
| --- | --- | --- |
| GL30 AMOLED V6 `profileVersion: 1` | ✅ **格式原生兼容** | V6 Schema 与当前 Schema 只有标识信息不同；两个 V6 示例无需改字段即可通过当前校验器，当前实测为 2/2 通过；不一致或越界配置仍会被更严格的语义/安全规则拒绝 |
| V6 实时帧 / `HAPTIC_COMMAND` | 🧩 **已实现子集规格兼容** | 帧头、CRC32C、类型号、`0x01` 快速状态和 64 B `0x10` 命令布局被保留；`0x02` 只有类型/用途来自 V6，V6 未冻结其 payload；尚无两台实物互通证据 |
| SmartKnob `SmartKnobConfig` | 🛠 **计划提供转换器** | 档位宽度、端点、snap point、magnetic positions 等语义可转换；Protobuf、强度单位和运行时状态不能直接复制 |
| X-Knob `XKnobConfig` | 🛠 **计划共用 SmartKnob 映射** | 主要触觉字段相近，但现有模式是 C++ 常量，不是可直接拖入的 Profile 文件 |
| SuperDial | 📖 **交互参考，不宣称文件兼容** | 可借鉴 BLE Dial/HID、同心结构和回中行为；上游没有稳定的外部触觉配置格式 |
| Surface Dial、Home Assistant、MQTT | 🛠 **计划作为上层适配目标** | 复用成熟生态，但它们属于输入输出/数据源适配，不是 STM32 电机协议 |

兼容层将位于 PC Companion：`旧格式 → Source Adapter → Canonical Profile → 校验/限幅/转换报告 → 设备命令`。STM32 实时核仍只保留一种确定性二进制接口，不解析 JSON/Protobuf，也不堆叠多套旧协议。

详细字段映射、安全拒绝项、固定上游 commit 和故意留空的未来适配槽位见 [Profile 兼容与迁移计划](docs/profile-compatibility-cn.md)。**当前只有 V6 Profile 对象达到原生格式兼容；SmartKnob/X-Knob 导入器尚未完成。**

## 系统如何工作

```mermaid
flowchart LR
    Host[PC / 应用] -->|Profile、数据和操作| App[ESP32-S3<br/>AMOLED / HID / 连接]
    App -->|1 kHz 命令| Core[STM32G474<br/>实时触觉与 FOC]
    Core --> Driver[DRV8316R]
    Driver --> Motor[CubeMars GL30]
    Encoder[GL30 工厂编码器<br/>PENDING_VENDOR] --> Core
    Core -->|遥测、trace、故障| App
    App -->|状态与输入事件| Host
    Core -->|独立故障线| App
```

| 实时任务 | 当前固件调度值 | 目的 |
| --- | ---: | --- |
| 电流环 / FOC | 40 kHz | 电流采样、坐标变换、PI 和 SVPWM |
| 编码器 / 观测器 | 4 kHz | 角度、速度和加速度估计 |
| 触觉运行时 | 2 kHz | 根据 Profile/命令计算目标力矩 |
| 快速遥测 | 2 kHz | 角度、电流、力矩、状态和控制时序 |
| 上层命令 | 1 kHz | 接收触觉参数和控制请求 |
| 安全监控 | 200 Hz | 电源、温度、通信和锁存故障管理 |

这些是代码中的调度配置，不代表已经测得同等的机械闭环带宽。

## 功能状态总览

状态口径：

- ✅ **现在可用：** 没有实物也能直接运行或检查；
- 🧩 **代码已实现：** 已进入 STM32/PC 代码和自动测试，但必须上实物验证与调参；
- 🛠 **预计实现：** 已明确进入路线图，当前还不能使用；
- ⏳ **待定：** 依赖厂家资料、样机测量或设计评审，暂时不能冻结。

### ✅ 现在可用：无实物即可完成

| 功能 | 现在能做什么 | 证据边界 |
| --- | --- | --- |
| STM32 工程 | 用 STM32CubeMX 重新生成 MDK-ARM 工程，并使用 LL 活动路径构建 | `BUILD_ONLY`，不等于电机已转动 |
| 协议与 PC 核心库 | 编解码 V1 二进制帧、CRC32C、流式重同步、命令限幅、时钟同步和 trace 降采样 | TypeScript 测试和确定性 golden vectors |
| Haptic Profile | 校验 JSON Profile，并查看游戏性能面板和视频时间轴两个示例 | Schema 可用；尚未贯通到真实设备 |
| 设备模拟器 | 模拟命令、遥测、通信超时、安全状态和无实物端到端数据流 | `SIM_ONLY`，不模拟真实电机与机械触感 |
| 固件算法回归 | 在主机端测试协议、驱动逻辑、FOC 数学、触觉、安全监督和故障 trace | 证明软件回归，不证明模拟外设与功率级 |
| 数字样机资料 | 查看参数化概念 CAD、验证子板 BOM、PartsBridge/AD 导入资料、画板与上电测试指南 | `CAD_CHECKED` / 设计资料，尚无制造和装配证据 |

### 🧩 代码已实现：有实物后验证和调参

| 功能 | 已完成到什么程度 | 实物阶段必须验证 |
| --- | --- | --- |
| 三相 FOC | Clarke/Park、d/q 电流 PI、抗积分饱和、SVPWM、采样窗口和力矩估算 | 相序、电流极性/比例、PWM 死区、母线纹波、电角零位和稳定性 |
| 基础触觉原语 | 档位、位置/弹簧、速度/阻尼、软限位、摩擦和惯量，可在低层组合 | 真实力矩、噪声、振动、手感、参数范围和失稳边界 |
| 电机安全链 | 硬件 BKIN、DRVOFF、MOE 门控、启动自检、通信超时、IWDG、过流/过温/母线故障和锁存故障 | 故障注入、关断延迟、误触发、恢复条件和全工作区安全性 |
| 驱动与采样 | DRV8316R SPI/CSA 框架、三相同步 ADC、电流零点校准、VBUS 与电机温度输入 | SPI 波形、CSA 增益/矩阵、ADC 噪声、比较器阈值、相节点过冲和温升 |
| 遥测与故障追踪 | 快慢遥测、错误/丢包/截止期计数、40 kHz 六通道 RAM trace，故障时冻结 | 长时间吞吐、时间戳一致性、故障前数据有效性和 PC 端显示 |
| 辅助传感器 | INA228 功率/能量与 VEML7700 环境光驱动、重试和遥测接口 | 器件地址、校准、噪声、布局影响和实际用途 |

### 🛠 预计实现：最终用户会得到的功能

1. **打通真实电机：** 写入厂家确认的编码器后端，完成相序、电流、零位和低力矩闭环标定。
2. **打通 Profile 全链路：** `JSON Profile → PC/ESP32 → 二进制命令 → STM32 触觉运行时`，切换应用即可切换物理手感。
3. **补全触觉效果：** 纹理、非对称档位、标记点吸附、组合效果、稳定的主动回中与安全主动位置模式。
4. **完成 ESP32-S3 应用：** 5 Mbaud 电机核通信、日志记录/回放、AMOLED UI、Profile 选择与工程状态页。
5. **提供上层接口：** USB/BLE HID，并按实际需求逐步接入 MIDI、WebSocket、MQTT 或本地应用插件。
6. **完成桌面工具：** 设备连接、实时曲线、故障 trace、参数调节、Profile 编辑/下发和校准向导。
7. **形成可复现整机：** 正式 PCB、结构件、线束、装配、校准、热设计、再生处理、寿命与外部复现记录。

### ⏳ 目前必须待定

| 待定项 | 为什么现在不能拍板 | 闭合后影响 |
| --- | --- | --- |
| GL30 工厂编码器型号、供电、电平、协议、线序、连接器、更新率和延迟 | 公开资料不足，必须以厂家书面答复和实物波形为准 | 编码器驱动、MCU 引脚、连接器和首板原理图 |
| 编码器是否确为 AS5048A 及其具体输出方式 | 型号口头信息不能代替工厂版 SKU、datasheet 与线束定义 | SPI/PWM/ABI 方案只能冻结一种，不能靠猜测并行保留后端 |
| GL30 相参数、力矩常数、允许电流、热阻和再生能量 | 当前数值只是首轮设计输入，批次与工况会影响结果 | 电流环增益、力矩上限、散热、母线保护和电源选型 |
| 最终 PCB 尺寸、铜厚、散热和成本 | 必须先完成验证子板及功率/温升/EMI 测量 | 正式板布局、层数、BOM 与外壳体积 |
| 结构公差、轴承/载荷路径、出线和固定方式 | 需要厂家机械公差、实物装配和载荷测试 | 最终 CAD、旋转间隙、寿命与量产装配 |
| 可用触觉参数和性能指标 | “舒服”“清晰”“安静”必须用样机和测量定义 | 默认 Profile、调参范围、性能页和发布声明 |
| EMC、可靠性与安全等级 | 只能在接近正式硬件后测试 | 是否能从开发平台走向具体产品或认证 |

厂家接口若与当前假设一致，首板阶段主要工作会是参数标定和验证；若编码器电气或协议不同，则必须先修改对应驱动和原理图，不能把它伪装成“只需调参”。

## Haptic Profile 是什么

Profile 描述一个应用希望设备呈现的物理行为。例如下面的片段表达“每 15° 一个档位，并叠加少量阻尼和摩擦”：

```json
{
  "haptic": {
    "mode": "detent",
    "detent": {
      "widthDeg": 15,
      "strengthmNm": 12,
      "waveform": "sine"
    },
    "damping": 0.00035,
    "friction": 0.001,
    "inertia": 0.00003
  }
}
```

当前 Schema 和示例可校验，STM32 也有对应的基础原语；自动识别应用、完整 Profile 翻译、ESP32 下发和实物效果仍属于待实现链路。

## 仓库结构

```text
firmware-stm32/   STM32G474 电机核、CubeMX/Keil 工程和主机测试
firmware-esp32/   ESP32-S3 应用层模块边界（应用尚未完成）
protocol/         帧格式、载荷规范和 TypeScript 参考编解码
pc-companion/     Profile API、设备模拟器和桌面侧核心包
hardware/         验证子板 BOM/指南和参数化概念 CAD
docs/             架构、引脚、上电、测试门和证据边界
scripts/          确定性向量生成和无实物验证
tests/            TypeScript 协议、Profile 和系统测试
```

公开仓库只维护最新快照，不存放历史设计目录、构建目录、本机日志和供应商原始附件副本。

## 立即运行无实物验证

需要 Node.js 24+、pnpm 11.19+、CMake 3.22+ 和 C11 编译器。

```bash
pnpm install --frozen-lockfile
pnpm verify

cmake -S firmware-stm32/tests -B build/stm32-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/stm32-host --config Debug
ctest --test-dir build/stm32-host -C Debug --output-on-failure
```

唯一活动 STM32 工程是：

```text
firmware-stm32/cubemx/GL30_AMOLED_V7/MDK-ARM/GL30_AMOLED_V7.uvprojx
```

工程由 STM32CubeMX 生成，活动外设路径使用 LL；重新生成请运行 `firmware-stm32/cubemx/generate.ps1`，细节见 [电机核说明](firmware-stm32/README.md)。

> [!CAUTION]
> 工厂编码器接口闭合前，`PENDING_VENDOR` 后端会主动拒绝电机 arm，并保持 DRVOFF/TIM1 MOE 关闭。不要绕过 `control_ready()` 或任何硬件安全门。

## 路线与参与方式

项目计划在约 12 个月内向可复现 v1.0 推进，目标时间为 2027 年 8 月。时间是方向，不是承诺；只有厂家接口、电气验证、电机实测、温升/寿命和外部复现逐项闭合，才会把它称为完成。完整证据门见 [ROADMAP.md](ROADMAP.md)。

这是一个工程项目，也是公开的学习过程。欢迎参与安全/原理图审查、触觉算法与 Profile、STM32/ESP32 代码、测试与工具、机械公差、文档和复现。请先阅读 [CONTRIBUTING.md](CONTRIBUTING.md)，设计讨论可进入 [Discussions](https://github.com/Master-1st/GL30-Haptic-Control/discussions)。

## 许可证与第三方材料

未单独说明的原创内容采用 [Apache License 2.0](LICENSE)。STM32Cube 生成依赖及其他第三方组件保留原许可证，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。供应商 STEP/PDF 不在仓库内重复分发，请按 `hardware/cad/vendor/SOURCE_MANIFEST.md` 从原始来源下载。

本项目为独立社区项目，与 CubeMars、STMicroelectronics、Texas Instruments、Espressif、Waveshare、Arm 或 Keil 不存在隶属或官方背书关系。产品名和商标归各自权利人所有。
