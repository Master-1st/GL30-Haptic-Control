# README 参考原文与本项目改写对照

查阅日期：2026-09-09。这里只比较项目首页的写法，不把别人的功能、完成度或硬件参数移植为 GL30 的事实。

## 先看原文

| 项目 | 本次实际查阅的 README | 最值得看的部分 |
| --- | --- | --- |
| SmartKnob | [原文与图片](https://github.com/scottbez1/smartknob/blob/4eb988399c3fda6ffd3006772856093dfe9adb86/README.md) · [原始 Markdown](https://raw.githubusercontent.com/scottbez1/smartknob/4eb988399c3fda6ffd3006772856093dfe9adb86/README.md) | 开头定位、演示、视频时间轴场景、制作入口 |
| X-Knob | [中文原文与图片](https://github.com/SmallPond/X-Knob/blob/05be44fc62b27c4fa941aabd2a7e9b2553f91fb9/README.md) · [原始 Markdown](https://raw.githubusercontent.com/SmallPond/X-Knob/05be44fc62b27c4fa941aabd2a7e9b2553f91fb9/README.md) | 项目起源、演示视频、已支持/待支持功能、入门 |
| SuperDial | [本次 GitHub 版本](https://github.com/CharlieYu4994/superdial/blob/1973d9436a7220f16eec6f76aac6d7029588c03f/readme.md) · [原始 Markdown](https://raw.githubusercontent.com/CharlieYu4994/superdial/1973d9436a7220f16eec6f76aac6d7029588c03f/readme.md) | 桌面外设定位、项目介绍、制作/源码入口 |

链接固定到本次核对的提交，方便以后仍能对照同一版本。SuperDial 一项指实际读取的公开 GitHub 副本，不认定它是原作者最新主仓；本次不依据它的旧更新日志判断现售硬件状态。原文保留在作者/托管仓库，不把完整 README 或演示图片复制进 GL30。

## 别人是怎么介绍项目的

以下顺序为阅读结构的概括，不是原文全文或逐字目录。

### SmartKnob：先说明“这是什么”，再让人看到效果

开头用一句话定义可配置触觉输入，紧接着解释电机与编码器如何产生可调手感。后面展示设计、演示和时间轴应用，再进入制作、常见问题与贡献。它也说明项目尚不适合普通即插即用用户。[来源](https://github.com/scottbez1/smartknob/blob/4eb988399c3fda6ffd3006772856093dfe9adb86/README.md)

**本项目借鉴：** 首段直接说“开源可编程力反馈旋钮”，用档位、阻尼和回弹说明用途；先放自己的概念图，再引导读者看工程资料。不借用对方视频充当 GL30 演示。

### X-Knob：把交互体验和具体应用放在读者看得到的位置

其介绍从项目起源切入，随后给出普通演示和智能家居演示，再列硬件、支持功能、待支持功能与入门方式。读者能较快找到设备适合做什么，以及下一步从哪里开始。[来源](https://github.com/SmallPond/X-Knob/blob/05be44fc62b27c4fa941aabd2a7e9b2553f91fb9/README.md)

**本项目借鉴：** 以音量、定时器、时间轴等场景介绍目标体验，同时把“已有台架/离线软件”与“整机应用开发中”分清。不能因为 X-Knob 列了 MQTT 或 Surface Dial，就说 GL30 已经支持。

### SuperDial：定位具体，制作资料容易找到

本次版本在更新日志之后介绍桌面力反馈外设的用途，并提供演示、制作、结构、烧录、源码与未来计划等入口。[来源](https://github.com/CharlieYu4994/superdial/blob/1973d9436a7220f16eec6f76aac6d7029588c03f/readme.md)

**本项目借鉴：** 让读者知道这是桌面交互项目，也能找到源码和结构资料。**不照搬：** 长更新日志占据首屏、具体焊接和供电参数混入项目介绍。这些内容继续留在专项文档。

## 本次改写怎么对应

| 首页需要回答 | 新版处理 | 详细内容去向 |
| --- | --- | --- |
| 这是什么项目？ | 首段给出设备定位和软件定义手感 | 不用内部测试代号开场 |
| 它有什么吸引力？ | 档位、阻尼、回弹，加屏幕与灯光的交互方向 | 不堆叠电路参数 |
| 我可以用来做什么？ | 集中介绍少量应用方向，并明确集成仍在开发 | [功能研究](feature-research-cn.md) |
| 现在能看到什么？ | 自有概念图、简短状态说明、离线源码入口 | [台架记录](bench-validation-20260908-haptic25-extended-cn.md) |
| 我怎么开始？ | 无硬件检查、UI 模型、固件/CAD 文档导航 | 各模块 README |
| 板数和装配呢？ | 只保留导航，不占首页正文 | [按压、RGB 与装配方案](knob-press-rgb-architecture-cn.md) |

原有方案没有删除，也没有因首页精简而变成“已实现”。首页负责介绍项目，架构/装配文档负责解释实现，台架记录负责提供证据。

## 看本项目的新稿

- [中文项目首页](../README_CN.md)
- [英文项目首页](../README.md)

本轮按用户要求将上述参考结构和本地核实的项目事实交给 DeepSeek V4 Pro 重写，再由主线程校正、编辑并检查链接。协作过程不放在项目首页；不包含新硬件测试、固件修改或 GitHub 发布。

DeepSeek 任务 `20260909090704-6dbc0c111551`（Pro/high）已自然完成并返回中英文初稿。主线程采用其“定位—效果—探索入口”的组织建议，进一步减少重复状态说明、改写生硬术语，并删除了未经实现的“计时结束自动回零”表述。最终中英文稿以本地项目证据为准。
