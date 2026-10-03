# Engineering documentation

[当前离线交付](offline-delivery-20261003-cn.md) · [A 板怎么画](pcb-drawing-guide-20261003-cn.md) · [公开项目入口](../PROJECT_INDEX_CN.md)

当前进度先看 [2026-09-27 项目进度整理](project-progress-20260927-cn.md)；按模块查找见 [项目索引版](../PROJECT_INDEX_CN.md)。[项目全量单文件版](../PROJECT_ALL_IN_ONE_CN.md)含历史首批 BOM 和早期方案，不作为最新完成状态或整套采购指令。

原 `G:\Agent\workspace` 的 GL30 文件已归回项目，见 [统一入口与版本关系](../outputs/workspace-consolidated-20260927/README_CN.md)：包含 R12–R20、装配手册、正式板指南和独立发布准备工作区。9/27 已建立 [R21 合并机械候选](../outputs/r21-integrated-mechanical-20260927/README_R21_先看.md)，仍需实物验证。

A 板当前使用 [原生 AD 工程，无测试点](../hardware/pcb/stm32-foc-a-altium-r6/README_CN.md)、[AD ZIP 与验证](../outputs/a-schematic-altium-20260927/README_CN.md)：7 页、146 个位号，AD 实际连接比对通过；已关联用户库封装 142/146 个位号，仍有 4 个封装及 30 个驱动源警告，未完成投板审核。此前 [KiCad/PDF 版](../outputs/a-schematic-kicad-20260927/README_CN.md)是含测试点的历史源稿。PB9/PA12/PC13 与母线/NTC 已统一，固件验证见 [接口核对包](../outputs/product-interface-20260927/README_CN.md)。[OSHWHub 类似项目对照](../hardware/pcb/stm32-foc-a/开源项目对照.md)列出可参考部分及差异。

The detailed engineering notes are currently Chinese-first while the public repository overview and roadmap are bilingual. Contributions that add accurate English counterparts are welcome.

面向首次访问者的介绍见[中文首页](../README_CN.md) / [English overview](../README.md)。本次首页写法的[类似项目原文与改写对照](readme-reference-cn.md)单独保留，不与工程方案混排。

## Read in this order

最新屏幕状态及证据见 [9/27 汇总](project-progress-20260927-cn.md)：9/17 连续菜单记录 **59.980 FPS / 60 秒**，目标 60 Hz，显示错误、输入丢弃和电机发送均为 0；已加入彩色图标、中英切换及设置页。电机未参与此性能验收。[9/16 的 61.587 FPS 显示链审计](esp32-60fps-optimization-20260916-cn.md)和 [9/15 的 35.706 FPS 记录](esp32-dirty-audit-20260915-cn.md)保留为历史阶段证据，不能混合版本和测试条件。

早期产品交互方向见 [旋环按压、24 RGB 与板件装配方案](knob-press-rgb-architecture-cn.md)（2026-09-09）；后续机械实物反馈和正式板路线以 [当前汇总](project-progress-20260927-cn.md)为入口，均不自动继承台架验收。

最新离线实现见 [H26 行为层与 ESP 动画](haptic26-ui-offline-20260908-cn.md)：新源码未烧录，不继承 H25 的硬件验收。

当前实测先看 [2026-09-08 H25 扩展验收](bench-validation-20260908-haptic25-extended-cn.md)：37 组新增冻结采样、定时边界、ACTIVE STOP/失联关断、20 次重复运行和无出力通信/准备状态压力测试通过。此前 H25 电流阶梯见 [基线报告](bench-validation-20260907-haptic25-cn.md)，H8/H10 及中间失败保留为历史。

整体进度以 [9/27 汇总](project-progress-20260927-cn.md)为入口；[9/9 总进度](project-progress-review-20260908-cn.md)、[9/8 原始审查](project-progress-review-20260908-history-cn.md)与 [9/5 重评](project-reassessment-cn.md)保留为历史，不再作为当前源码或首测操作基线。

1. [`feature-research-cn.md`](feature-research-cn.md) / [`feature-research.md`](feature-research.md) — haptic-first product scope, public demand, integrations, and explicit non-capabilities.
2. [`current-architecture-cn.md`](current-architecture-cn.md) — active architecture and source-of-truth order.
3. [`evidence-boundary.md`](evidence-boundary.md) — what each evidence level does and does not prove.
4. [`hardware-gates.md`](hardware-gates.md) — staged release gates before energized testing.
5. [`cubemars-vendor-confirmation-cn.md`](cubemars-vendor-confirmation-cn.md) — unresolved motor/encoder questions.
6. [`stm32-pinout-and-schematic-cn.md`](stm32-pinout-and-schematic-cn.md) — current pin and schematic input table.
7. [`stm32-bringup-and-tuning-cn.md`](stm32-bringup-and-tuning-cn.md) — post-vendor bring-up and tuning order.
8. [`../hardware/motor-control/GL30_KV290_ARRIVAL_BASELINE_CN.md`](../hardware/motor-control/GL30_KV290_ARRIVAL_BASELINE_CN.md) — received motor parameters, consistency checks, safe defaults, and first-test gates.
9. [`waveshare-baseline.md`](waveshare-baseline.md) — application-module baseline.
10. [`NUCLEO + TI EVM FOC bring-up`](../firmware-stm32/bench/NUCLEO_G474RE_FOC/README_CN.md) — current HAPTIC25 NUCLEO-G474RE firmware, exact EVM/encoder pin map, restricted console and evidence boundary; ALIGN, short bidirectional IQ and eight-mode hardware smoke pass, not product/tactile acceptance.

Validation-board drawing and test instructions live under [`hardware/drv8316r_bench`](../hardware/drv8316r_bench/README_CN.md).

## Documentation rule

Plans, calculations, simulations, builds, CAD checks, bench measurements, and external reproductions must remain distinguishable. When a result changes, update the narrowest source document and link the raw evidence rather than copying the claim into several files.
