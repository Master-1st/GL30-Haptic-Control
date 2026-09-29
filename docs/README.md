# Engineering documentation

The detailed engineering notes are currently Chinese-first while the public repository overview and roadmap are bilingual. Contributions that add accurate English counterparts are welcome.

## Read in this order

ESP32 界面与显示调度先看 [2026-09-29 恢复审计](esp32-ui-recovery-20260929.md)：原 main 缺失核对、本机源码恢复、UI/流水线修复、主机测试与历史 FPS 证据边界。

当前实测先看 [2026-09-08 H25 扩展验收](bench-validation-20260908-haptic25-extended-cn.md)：37 组新增冻结采样、定时边界、ACTIVE STOP/失联关断、20 次重复运行和无出力通信/准备状态压力测试通过。此前 H25 电流阶梯见 [基线报告](bench-validation-20260907-haptic25-cn.md)，H8/H10 及中间失败保留为历史。

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
