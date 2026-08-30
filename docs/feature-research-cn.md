# GL30 Haptic Control 功能与社区需求调研

调研日期：2026-08-30

这份文档回答三个问题：用户真正想从力反馈旋钮得到什么、GL30 平台能够怎样实现、哪些能力现在做不到或不应承诺。它是当前产品取舍的依据，不是完成清单，也不把其他项目的演示当作 GL30 的实物证据。

## 结论先行

1. **首要卖点必须是手感，而不是屏幕。** 可调档位、顺滑自由旋转、软限位、回中、阻尼、惯性和标记点吸附决定产品是否值得使用。
2. **最有吸引力的首批应用是音量、定时器、智能家居和天气。** 它们能在不解释控制理论的情况下展示“软件状态改变物理手感”。
3. **创作者场景最能体现力反馈的独特价值。** 视频时间轴、播放速度、MIDI/DAW、调色和 CAD 参数既需要精调，也有边界、中心点和关键位置。
4. **配置可保存、分享和迁移是平台价值。** 社区多次要求把硬编码手感移到文件并提高档位配置灵活性；这正是 Haptic Profile 的核心意义。
5. **连接功能必须有边界。** Home Assistant/MQTT、标准 HID 和网络天气都有明确路径；“直连所有智能家居”“HID 控制所有软件”“天气完全离线”都不真实。
6. **GL30 的差异化方向是独立实时电机核、安全链、物理单位、遥测和可复现调参。** 它们有利于把好手感从一次演示变成可以测量和复用的工程结果，但目前尚无真实手感证据。

## 调研范围与证据口径

本次只使用公开项目源代码、README、release、issue 和协议/平台官方文档。issue 代表公开需求样本，不等于统计学意义上的用户调查；开源项目实现也不等于 GL30 已完成同一功能。

| 来源 | 固定版本或页面 | 本次采用的信息 |
| --- | --- | --- |
| [SmartKnob](https://github.com/scottbez1/smartknob/tree/4eb988399c3fda6ffd3006772856093dfe9adb86) | `4eb9883` | 软件可配置档位/限位、按压触觉、视频时间轴演示、未来 MQTT/Home Assistant 方向、公开 issue |
| [X-Knob](https://github.com/SmallPond/X-Knob/tree/05be44fc62b27c4fa941aabd2a7e9b2553f91fb9) | `05be44f` | 七类力反馈模式、LVGL、Surface Dial、按压振动、Home Assistant/MQTT、Web 配置、OTA 与功耗功能 |
| [SuperDial](https://github.com/CharlieYu4994/superdial/tree/1973d9436a7220f16eec6f76aac6d7029588c03f) | `1973d94` | BLDC 力反馈、BLE Dial、屏幕/按键交互、PC 外设与性能监视器方向；没有可迁移的结构化触觉配置 |
| [Home Assistant MQTT](https://www.home-assistant.io/integrations/mqtt/) | 官方当前文档 | Broker、Discovery、实体与 availability 机制 |
| [Home Assistant Weather](https://www.home-assistant.io/integrations/weather) | 官方当前文档 | 天气 entity、当前状态、小时/日预报及数据源依赖 |
| [Open-Meteo Forecast API](https://open-meteo.com/en/docs) | 官方当前文档 | 以经纬度获取 JSON 预报；适合作为首个直连演示源，许可、限额和商业使用条件必须单独遵守 |
| [ESP32-S3 产品概览](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/product-overview.html) | 乐鑫官方当前文档 | 芯片提供 2.4 GHz Wi-Fi 与 Bluetooth LE，不包含 802.15.4 |
| [ESP-Matter 编程指南](https://docs.espressif.com/projects/esp-matter/en/latest/esp32s3/esp-matter-en-master-esp32s3.pdf) | 乐鑫官方当前文档 | ESP32-S3 可做 Matter over Wi-Fi；Matter over Thread 需要具有 802.15.4 的芯片或边界路由器 |
| [USB HID](https://www.usb.org/hid) | USB-IF 官方当前文档 | 标准 HID Usage 的能力与“Usage 存在不等于所有主机支持”的边界 |
| [Windows Volume Controls](https://learn.microsoft.com/en-us/windows/win32/coreaudio/volume-controls) | Microsoft 官方文档 | 设备音量与分应用音频会话是不同接口，分应用控制需要主机软件 |

## 前人项目已经证明了什么

### SmartKnob

SmartKnob 把 BLDC 与磁编码器闭环结合，用软件动态改变档位和限位。它的公开视频时间轴示例尤其重要：片段边界可以被“摸到”，播放速度可以弹簧回到暂停并吸附在 1×、2×、4×。这说明力反馈旋钮的价值不在于替代普通编码器，而在于把软件里的语义边界变成触觉。

它也明确说明项目尚非成熟即插即用产品，并把 Wi-Fi/MQTT、Home Assistant 和真实应用列为未来工作。这是 GL30 不应把“协议字段存在”写成“应用已完成”的直接提醒。

### X-Knob

X-Knob 是力反馈旋钮，不是普通 UI 旋钮。其 release 公开列出边界、棘轮、回弹等七种组合模式，并实现 LVGL、Surface Dial、按压振动、MQTT 接入 Home Assistant、Web 配置、OTA 和电源管理。它证明“手感 + 屏幕 + 智能家居”的产品组合可行，也暴露了真实工程问题：MQTT 配置、设备发现、UI、内存、功耗和异常输入都需要单独解决。

### SuperDial

SuperDial 使用 BLDC/SimpleFOC 产生回中力，并通过 BLE Dial 与 PC 交互，公开计划包括多级 UI 和 PC 性能监视器。它证明国内社区同样重视 PC 外设场景。但当前公开代码主要是硬编码行为，没有独立、可版本化的触觉配置结构，因此可作为场景参考，不能作为现阶段的配置转换目标。

## 社区反复提出的需求

以下是定性归类，不按点赞数伪造“最受欢迎排行”。

| 需求主题 | 公开依据 | 对 GL30 的产品含义 |
| --- | --- | --- |
| 惯性与动量滚动 | SmartKnob [#38](https://github.com/scottbez1/smartknob/issues/38)、[#43](https://github.com/scottbez1/smartknob/issues/43) | 不能把 `inertia` 只当一个数值字段；需要可停止、可打断、不会失控的完整状态机 |
| 更灵活且可保存的手感 | SmartKnob [#52](https://github.com/scottbez1/smartknob/issues/52)、[#53](https://github.com/scottbez1/smartknob/issues/53) | Haptic Profile、编辑器、预览、导入、限幅报告和分享应是主线能力 |
| 独立的触觉示例 | SmartKnob [#92](https://github.com/scottbez1/smartknob/issues/92) | 每种原语都应有最小示例、物理单位、默认值、危险边界和可复现实验 |
| MIDI、DJ 与创作工具 | SmartKnob [#80](https://github.com/scottbez1/smartknob/issues/80)、[#132](https://github.com/scottbez1/smartknob/issues/132)、[#161](https://github.com/scottbez1/smartknob/issues/161) | MIDI CC/高分辨率参数、0 点吸附、节拍档位和软件映射值得进入 P2 |
| 视频 shuttle/jog | SmartKnob [公开演示](https://github.com/scottbez1/smartknob#demo-video-editor-timeline-control)、[#173](https://github.com/scottbez1/smartknob/issues/173) | 逐帧、片段标记、惯性时间轴、弹簧播放速度是最佳展示场景之一 |
| Home Assistant、MQTT、ESPHome | SmartKnob [#17](https://github.com/scottbez1/smartknob/issues/17)、[#137](https://github.com/scottbez1/smartknob/issues/137)、[#144](https://github.com/scottbez1/smartknob/issues/144)；X-Knob [#3](https://github.com/SmallPond/X-Knob/issues/3)、[#4](https://github.com/SmallPond/X-Knob/issues/4) | 先支持一个成熟网关和实体模型，不为每个灯泡品牌写私有直连 |
| 更多物理输入和反馈 | SmartKnob [#57](https://github.com/scottbez1/smartknob/issues/57)、[#75](https://github.com/scottbez1/smartknob/issues/75)、[#174](https://github.com/scottbez1/smartknob/issues/174) | 触摸、按键、显示和灯效可以补充旋转，但不能喧宾夺主或破坏实时任务 |
| 便携与电池 | SmartKnob [#100](https://github.com/scottbez1/smartknob/issues/100)；X-Knob 电源管理 | 已进入主产品：当前采用 3S 无线力反馈架构和 800 mAh 机械参考；不能复制 X-Knob 的续航结论，容量、安全和续航必须实测 |
| “发脆”“发涩”等手感缺陷 | SmartKnob [#159](https://github.com/scottbez1/smartknob/issues/159) | UI/网络任务不能干扰电机时序；GL30 的 STM32/ESP32 分核架构正面解决这一风险，但仍需实测 |
| SpaceMouse 类控制 | SmartKnob [#133](https://github.com/scottbez1/smartknob/issues/133) | 可以服务 CAD 的缩放和参数输入，但单轴机构不能宣传为六自由度设备 |

## GL30 手感功能矩阵

状态定义：

- 🧩 **低层已实现，待实物：** STM32/协议中有执行代码和主机测试，但没有真实触感证据；
- 🛠 **明确可实现，尚未完成：** 架构和接口路径成立，当前不可使用；
- 🔌 **外部依赖：** 还需要主机、网络、服务、网关或第三方软件；
- ❌ **当前不支持或不承诺：** 现有硬件/证据不满足，或不应进入当前范围。

| 手感能力 | 当前基础 | 缺少的闭环 | 状态 |
| --- | --- | --- | --- |
| 自由旋转 / 低阻尼 | 触觉 flags、阻尼和摩擦可归零，安全链仍保持工作 | 电机齿槽、轴承阻力、编码器纹波、电流纹波、零速噪声及必要补偿 | 🧩 |
| 周期虚拟档位 | 2 kHz haptic tick 已执行正弦档位力矩并受用户力矩限幅 | GL30 相序/编码器/电流标定、齿槽与噪声测量、默认参数盲测 | 🧩 |
| 弹簧回中 / 目标位置 | 位置误差到力矩的低层原语存在 | 触碰/松手策略、回中速度、超调、夹手与失效安全验证 | 🧩 |
| 软限位 | 最小/最大角度外的恢复力已实现 | 软区形状、峰值力矩、越界恢复与机械限位配合 | 🧩 |
| 阻尼 | 速度相关力矩项已实现 | 编码器速度噪声、时延、稳定范围和主观手感 | 🧩 |
| 摩擦 | 低速平滑的摩擦项已实现 | 静摩擦感、零速抖动与齿槽补偿 | 🧩 |
| 惯量 | 加速度相关项已实现 | 状态化动量、用户接管、停止条件、长列表映射 | 🛠 |
| 磁性位置 / 标记点 | Profile 有 `magneticPositions` | Profile→command 映射与 STM32 多标记点运行时 | 🛠 |
| 非对称档位 | Profile 有 `asymmetry` | 波形定义、方向语义、运行时代码与调参 | 🛠 |
| 纹理与短促触觉提示 | Profile/协议有 texture 字段和 ID | 发生器、采样率、频谱/噪声边界与实际效果 | 🛠 |
| 动态切换与组合 | 命令含 mode flags，低层原语可叠加 | 完整 Profile 下发、平滑过渡、防跳变和上层状态同步 | 🛠 |
| 主动指针/自动旋转 | 协议预留目标位置、速度和自驱限幅 | 触碰检测、再生、失控检测、真实安全测试 | ❌ 当前不作为首批用户功能 |

## 应用功能矩阵

| 功能 | 最小可用版本 | 实现路径 | 当前结论 |
| --- | --- | --- | --- |
| 全局音量/媒体 | 转动音量、触摸或按键静音、播放/暂停、切歌 | ESP32 USB/BLE HID + 音量 Profile | 🛠 可实现，尚无 HID 应用 |
| Windows 分应用音量 | 选择当前音频会话，显示应用名并同步音量 | PC Companion + Windows Core Audio + 设备协议 | 🔌 可实现，不能只靠 HID |
| 定时器/番茄钟 | 设置、开始/暂停、剩余时间、到时触觉提示 | ESP32 本地状态机 + UI + 持久化 | 🛠 可实现，可离线；尚无应用 |
| Home Assistant 灯光 | 选择实体，控制开关、亮度、色温并回读状态 | MQTT Discovery/状态/命令 topic + Profile 映射 | 🔌 可实现，需要 HA/Broker |
| Home Assistant 恒温器/窗帘/场景 | 0.5 °C 档位、0–100% 软限位、场景列表 | MQTT climate/cover/scene 等实体映射 | 🔌 可实现，需逐域定义语义 |
| 天气与空气质量 | 当前值、小时预报、阈值标记、离线缓存提示 | P1 先接 Open-Meteo HTTPS JSON；以后可增加 HA weather adapter | 🔌 可实现，需要位置/网络/服务 |
| 视频时间轴 | 逐帧、标记点、惯性浏览、shuttle 速度回中 | PC 插件/Companion + 动态 Profile | 🔌 可实现，需要目标软件适配 |
| MIDI/DAW | CC、精调、中心/0 dB 吸附、节拍档位 | USB MIDI 或主机桥接 | 🛠 可实现，尚无描述符/映射器 |
| CAD/创作软件 | 缩放、参数、笔刷、历史和模式切换 | HID 快捷键用于基础控制；插件用于语义反馈 | 🔌 部分通用、完整体验需插件 |
| PC 性能面板 | FPS、帧时间、CPU/GPU、设备遥测 | PresentMon/LHM/RTSS adapter + UI | 🛠 Schema 有字段，采集器未实现 |
| Profile 导入与分享 | 文件导入、预览、限幅报告、下发和导出 | PC Companion Canonical Profile | 🛠 V6 格式可校验；UI/下发/外部转换器未完成 |

## 明确排除的宣传

| 不应宣传的能力 | 原因 | 若未来要改变结论 |
| --- | --- | --- |
| 已有“高级、顺滑、静音”手感 | 无实物闭环和测量 | 完成齿槽、线性、时序、声学和用户盲测，公开原始结果 |
| 直连所有 Zigbee/Thread 设备 | ESP32-S3 没有 802.15.4 射频 | 增加相应芯片/模块，或明确要求 HA/边界路由器 |
| 天气完全离线、永久免费 | 无本地气象阵列，第三方条款会变化 | 自建数据服务或增加真实传感器，并重新评估许可/成本 |
| 任意软件零配置兼容 | 应用语义和权限不同，HID 只覆盖通用输入 | 逐个提供维护中的插件/适配器和版本测试 |
| 单轴 6-DOF SpaceMouse | 机构与传感自由度不足 | 增加多轴机构与传感器，成为另一个硬件项目 |
| 直接复用其他电机控制参数 | 电机、编码器、驱动、电源和热设计不同 | 只导入触觉意图，在 GL30 上重新标定 |
| 数月电池续航 | 当前 3S 800 mAh 机械参考只服务小体积目标，尚无平均功耗和续航数据 | 实测各模式平均/峰值功率、温升、循环与安全，再决定是否扩大电池和机壳 |
| 安全关键或无人值守主动运动 | 当前没有触碰检测和整机故障验证 | 完成危害分析、双通道限制和完整实物测试；可能仍需认证 |

## 实现优先级

### P0：先把“好手感”做成可测结果

1. 取得并验证 GL30 工厂编码器资料，保持 `PENDING_VENDOR` 安全门直到接口闭合。
2. 低能量打通 FOC，测量电流、编码器误差、齿槽、延迟、噪声、温升和再生。
3. 依次验收自由旋转、档位、阻尼、软限位和回中；每项都发布原始数据和主观评价方法。
4. 打通 `Profile → ESP32/PC → HapticCommand → STM32`，加入平滑切换和限幅报告。
5. 验证 3S 电池、BQ25798 SYS 电源路径、关机反拖和独立再生制动，发布母线电压/双向能量/温升数据。

### P1：用四个应用让普通用户立即理解价值

1. **音量：** 标准 HID 全局音量 + 静音 + 软限位，作为最小端到端应用。
2. **定时器：** 本地离线状态机，验证不同转速下粗/细调和到时提示。
3. **Home Assistant：** 先只做 `light`，再增加 `climate`、`cover`、`scene`；使用 MQTT Discovery 和 availability。
4. **天气：** 首个演示固定使用 Open-Meteo，经纬度由用户设置；必须显示来源、更新时间和离线状态。Home Assistant weather 作为后续独立适配器，不做隐式 fallback。

### P2：开放创作者和配置生态

1. 视频时间轴/shuttle 参考应用；
2. USB MIDI 与可映射 CC；
3. Windows 分应用音量；
4. Haptic Profile 编辑、预览、分享以及 SmartKnob/X-Knob 转换器；
5. CAD/DAW/调色等由社区按稳定插件接口扩展。

### 暂不进入主线

- 语音助手、直接 Zigbee/Thread、六自由度输入；
- 无人值守主动旋转、作为安全告警或生产设备的安全控制器；
- 为每个品牌智能家居做私有协议直连。

这些功能不是永远否定，而是不能在核心触感、实时链路和安全证据尚未闭合时分散主线。
