# Engineering documentation

The detailed engineering notes are currently Chinese-first while the public repository overview and roadmap are bilingual. Contributions that add accurate English counterparts are welcome.

## Read in this order

1. [`feature-research-cn.md`](feature-research-cn.md) / [`feature-research.md`](feature-research.md) — haptic-first product scope, public demand, integrations, and explicit non-capabilities.
2. [`current-architecture-cn.md`](current-architecture-cn.md) — active architecture and source-of-truth order.
3. [`evidence-boundary.md`](evidence-boundary.md) — what each evidence level does and does not prove.
4. [`hardware-gates.md`](hardware-gates.md) — staged release gates before energized testing.
5. [`cubemars-vendor-confirmation-cn.md`](cubemars-vendor-confirmation-cn.md) — unresolved motor/encoder questions.
6. [`stm32-pinout-and-schematic-cn.md`](stm32-pinout-and-schematic-cn.md) — current pin and schematic input table.
7. [`stm32-bringup-and-tuning-cn.md`](stm32-bringup-and-tuning-cn.md) — post-vendor bring-up and tuning order.
8. [`waveshare-baseline.md`](waveshare-baseline.md) — application-module baseline.

Validation-board drawing and test instructions live under [`hardware/drv8316r_bench`](../hardware/drv8316r_bench/README_CN.md).

## Documentation rule

Plans, calculations, simulations, builds, CAD checks, bench measurements, and external reproductions must remain distinguishable. When a result changes, update the narrowest source document and link the raw evidence rather than copying the claim into several files.
