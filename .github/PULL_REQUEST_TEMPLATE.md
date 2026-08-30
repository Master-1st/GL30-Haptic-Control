## What changed

Describe the smallest observable change and link the issue it closes.

## Why

Explain the user, safety, reproducibility, or maintenance reason.

## Verification

List exact commands and results. For hardware, include revision, setup, limits, instruments, raw evidence, and stop conditions.

- [ ] `pnpm verify` (when applicable)
- [ ] STM32 host C tests (when applicable)
- [ ] CubeMX/Keil build with tool versions and error/warning count (when applicable)
- [ ] Documentation and relative links checked

Evidence level: `DESIGN_ONLY / SIM_ONLY / BUILD_ONLY / CAD_CHECKED / BENCH_MEASURED / HARDWARE_REPRODUCED`

## Safety and scope

- [ ] I did not bypass `control_ready()`, DRVOFF, TIM1 MOE, BKIN, or another protection gate.
- [ ] I did not guess an undocumented vendor interface.
- [ ] I did not present simulation, CAD, a dummy load, or a development board as product-hardware evidence.
- [ ] I did not add credentials, private paths, generated output, or third-party files without redistribution permission.
- [ ] The diff contains no unrelated refactor or compatibility layer.
