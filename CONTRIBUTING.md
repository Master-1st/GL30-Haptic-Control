# Contributing

Thank you for helping this project become safer, more reproducible, and easier to learn from. English and Chinese contributions are both welcome.

## Before opening work

- Use a Discussion for architecture questions, broad ideas, and learning conversations.
- Use an Issue for a reproducible bug, bounded feature, documentation gap, measurement proposal, or hardware review.
- For changes that can energize the motor, alter a protection threshold, change a connector, or modify the power stage, open an issue before a pull request.
- Never describe simulation, CAD, a dummy load, or a development board as product-hardware evidence.

## Development workflow

1. Fork the repository and create a focused branch from `main`.
2. Make the smallest change that closes the issue.
3. Add or update tests and documentation that directly support the change.
4. Run the relevant checks locally.
5. Open a pull request using the template and state the evidence level.

The project uses a single `main` branch plus short-lived pull-request branches. Releases are tagged; generated output and personal IDE state are not committed.

## Required checks

For TypeScript/protocol/profile changes:

```bash
pnpm install --frozen-lockfile
pnpm verify
```

For portable STM32 motor-core logic:

```bash
cmake -S firmware-stm32/tests -B build/stm32-host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/stm32-host
ctest --test-dir build/stm32-host --output-on-failure
```

For CubeMX/Keil changes, describe the CubeMX version, STM32CubeG4 version, Keil/ARMCLANG version, exact target, error/warning count, and whether any hardware was connected.

## Evidence labels

Use the narrowest true label in issues and pull requests:

- `DESIGN_ONLY`: design intent without executable or physical evidence;
- `SIM_ONLY`: host test, simulation, or synthetic data only;
- `BUILD_ONLY`: a toolchain build succeeded, without target execution;
- `CAD_CHECKED`: digital geometry assertions only;
- `BENCH_MEASURED`: real bench measurement with setup and raw evidence;
- `HARDWARE_REPRODUCED`: independent build and test evidence.

Hardware evidence should include the board revision, wiring, instruments, firmware commit, supply/current limits, raw captures or logs, expected result, observed result, and stop conditions.

## Code and document expectations

- Keep real-time motor control separate from UI/connectivity code.
- Preserve fail-safe defaults, especially DRVOFF, TIM1 MOE, BKIN, communication timeout, and `control_ready()`.
- Do not guess an undocumented vendor interface or silently add a fallback backend.
- Keep CubeMX user-code boundaries and the existing LL style.
- Keep third-party license headers and update `THIRD_PARTY_NOTICES.md` when dependencies change.
- Do not commit secrets, local absolute paths, vendor files without redistribution permission, or generated build directories.

## Review

Maintainers may ask for a smaller patch, stronger evidence, a safer test sequence, or clearer claim boundaries. Review focuses on correctness and reproducibility, not on how advanced a contribution sounds.
