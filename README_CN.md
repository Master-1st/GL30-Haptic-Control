# GL30 Haptic Control

**2026-10-03 更新：**[当前代码与离线交付](docs/offline-delivery-20261003-cn.md)、[A 板 AD 画板指南](docs/pcb-drawing-guide-20261003-cn.md)及[无测试点原理图](hardware/pcb/stm32-foc-a-altium-r6/README_CN.md)已同步。ESP32 当前源 8/8、STM32 15/15 回归及固件构建已留存验证；本次候选未烧录，自制板尚未布局布线。

[English](README.md) · 简体中文 · [文档](docs/README.md) · [路线图](ROADMAP.md) · [参与开发](CONTRIBUTING.md)

[![CI](https://github.com/Master-1st/GL30-Haptic-Control/actions/workflows/ci.yml/badge.svg)](https://github.com/Master-1st/GL30-Haptic-Control/actions/workflows/ci.yml)
[![pre-alpha](https://img.shields.io/badge/status-pre--alpha-orange)](ROADMAP.md)
[![Apache-2.0](https://img.shields.io/badge/license-Apache--2.0-blue)](LICENSE)

**让旋转操作拥有可以编程的手感。**

2026-09-11：[R4 完整内部布置与模型](docs/full-assembly-r4-20260911-cn.md) 已补入电路板、电池和全部按钮；[单套采购表](docs/one-set-cost-down-20260911-cn.md) 同步更新。固定屏柱藏于机内。当前为整机空间布置，未完成 PCB 布线及实物验收。

GL30 Haptic Control 是一个正在开发的开源力反馈旋钮项目，面向桌面控制、创作工具和日常交互。它以 CubeMars GL30 无刷电机为基础，通过编码器和闭环控制产生不同的旋转反馈：清晰的档位、连续的阻尼、松手回弹，以及转到边界时的阻力。

项目希望让同一个旋钮随着应用改变手感：调音量时一格一格地转，浏览时间轴时平滑移动，调整有范围的参数时能直接摸到边界。圆形屏幕提供信息，触觉反馈帮助你感知正在做的操作。

![GL30 Haptic Control 概念外观](hardware/cad/out/CONCEPT_FIT_DEFAULTS/V7_CONCEPT_FIT_DEFAULTS_isometric.png)

*现有概念渲染，非成品实拍；图片尚未包含新增的按压与底部灯环。*

## 不同的操作，不同的触感

- **档位：** 用软件设置每一格的位置与强度，让菜单选择、步进调节有明确反馈。
- **阻尼：** 为连续调节提供阻力，让精细操作和快速旋转具有不同感受。
- **回弹：** 形成带中心位置的控制方式，适合探索播放速度、方向等输入。
- **软限位：** 在参数边界增加阻力，让范围不仅能看见，也能感觉到。

这些行为围绕 **Haptic Profile（触感配置）**组织。目标是把“应用需要怎样的手感”与底层电机控制分开，使开发者可以修改配置、组合效果，并为自己的应用设计交互。

## 想用它做什么？

以下是项目优先发展的应用方向，相关整机与软件集成仍在开发。

| 场景 | 交互体验 |
| --- | --- |
| **音量与媒体** | 用档位调音量，在旋钮上查看状态，并结合按压控制播放 |
| **专注定时器** | 转一圈设定一小时；多圈橙色进度线表达更长时间，按压开始或暂停 |
| **视频与创作工具** | 浏览时间轴、逐帧移动、调整参数，让不同操作使用不同的阻尼与档位 |
| **智能家居** | 配合 Home Assistant 等平台调节灯光、温度或场景，屏幕显示当前对象 |

GL30 是首个参考设备。项目更长远的方向，是让这套触感配置与应用接口可以服务于更多物理控制界面。更多场景及取舍见[功能研究](docs/feature-research-cn.md)。

## 屏幕、灯光与旋转一起响应

外观与交互方向是黑色旋环、黑色屏幕过渡面，以及藏在旋钮与底座缝隙中的 RGB 灯光。底部灯环至少 24 颗 LED，以亮点位置表达实际旋钮角度，取代外壳上的固定标记；旋环按压承担确认、开始和暂停等操作。

屏幕动效强调有节奏的预备、夸张的弹性变化和回弹，让数值变化与手上的动作相呼应。关机时则尽量让屏幕融入黑色表面。

这是整机交互目标；新增按压和灯环尚未落实到模型与固件。板件和装配细节单独放在[设计文档](docs/knob-press-rgb-architecture-cn.md)。

## 一套可以继续开发的交互平台

- **实时控制与界面分工：** STM32G474 负责电机和触觉控制，ESP32-S3 承担屏幕、应用与连接；界面与联网任务不直接承担电流环计算。
- **可查看、可修改的触感配置：** 仓库提供 Profile 格式、示例和校验工具，便于研究应用与手感之间的映射。
- **不接硬件也能开始：** 协议、设备模拟器和主机端测试可在电脑上运行，UI 模型也有独立构建入口。
- **从软件到结构的开放资料：** 电机固件、应用层源码、协议、原型 CAD 和工程记录分别组织，方便从自己熟悉的部分参与。

## 当前进展

项目处于 **pre-alpha 工程原型阶段**。

- **已有台架验证：** GL30 与原厂 AS5048A 编码器已完成受限电机闭环、基础触觉模式及停止/失联关断测试。
- **已有离线软件：** 当前协议、Profile 校验、设备模拟器、同源 KK C 界面、NVS 设置保存与控制权交接，以及产品端口保护链回归。
- **已有工程资料：** A 板七页无测试点 AD 原理图、BOM 与布局指南；R21 机械名义模型及装配手册。
- **下一步：** 补 A 板四个焊盘封装并完成布局，完成 B/C 电路，核对实屏和机构，进行自制板受控上电及整机手感验收。

目前还不是可直接复刻的成熟套件。台架通过不代表整机手感、温升、续航或寿命已经验证；具体记录见[台架报告](docs/bench-validation-20260908-haptic25-extended-cn.md)，后续目标见[路线图](ROADMAP.md)。

## 从这里开始

如果想先了解效果与实现：

- [UI 模型与主机预览](firmware-esp32/ui/README.md)：在电脑上构建现有 UI 模型，了解显示层。
- [触感配置与兼容方向](docs/profile-compatibility-cn.md)：查看 Profile 与其他开源旋钮配置的关系。
- [原型结构与打印资料](hardware/cad/README_CN.md)：查看概念模型与结构验证资料。

如果想从软件入手，在仓库根目录执行：

```bash
pnpm install --frozen-lockfile
pnpm verify
```

需要 Node.js 24+ 和 pnpm 11.19+。这些命令运行协议、Profile 和模拟器等无硬件检查，不启动图形界面或驱动电机。

继续深入：[STM32 电机内核](firmware-stm32/README.md) · [PC Companion](pc-companion/README.md) · [通信协议](protocol/schema/protocol-v1.md) · [完整文档](docs/README.md)。

## 一起做出更好的手感

欢迎贡献触觉效果、应用适配、屏幕动效、结构改进、测试和文档。使用场景、体验建议和复现记录同样有价值。

可以先在 [Discussions](https://github.com/Master-1st/GL30-Haptic-Control/discussions) 交流想法，或阅读[贡献指南](CONTRIBUTING.md)开始参与。

[SmartKnob](https://github.com/scottbez1/smartknob) 和 [X-Knob](https://github.com/SmallPond/X-Knob) 等开源项目提供了重要的交互参考；具体来源见[功能研究](docs/feature-research-cn.md)。本项目的硬件参数和验证结果独立记录，不直接沿用其他旋钮的校准配置。

## 许可证

原创内容采用 [Apache-2.0](LICENSE)，第三方组件保留各自许可，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。供应商资料从[原始来源](hardware/cad/vendor/SOURCE_MANIFEST.md)获取。

本项目独立于相关器件厂商和参考项目，不代表其官方产品或背书。
