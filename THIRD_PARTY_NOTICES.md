# Third-party notices

This file records material that is not originally authored for GL30 Haptic Control. The repository license does not replace third-party terms.

## STM32CubeG4, CMSIS, and STM32 LL drivers

The generated STM32 project contains the minimal CMSIS and STM32G4 LL dependency files used by the active Keil target. Their upstream license files remain beside the code:

- `firmware-stm32/cubemx/GL30_AMOLED_V7/Drivers/CMSIS/LICENSE.txt`
- `firmware-stm32/cubemx/GL30_AMOLED_V7/Drivers/CMSIS/Device/ST/STM32G4xx/LICENSE.md`
- `firmware-stm32/cubemx/GL30_AMOLED_V7/Drivers/STM32G4xx_HAL_Driver/LICENSE.md`

The NUCLEO commissioning target also retains the same upstream license files under `firmware-stm32/bench/NUCLEO_G474RE_FOC/Drivers/` (CMSIS, STM32G4 device headers, and LL drivers). The project license does not relicense those files or any firmware release containing them.

STM32CubeMX, STM32CubeG4, STM32, and STMicroelectronics names are owned by STMicroelectronics. Keil, Arm Compiler, and CMSIS names are owned by Arm or their respective rights holders.

## JavaScript dependencies

The TypeScript workspace uses packages declared in `package.json` and workspace package manifests. Exact resolved versions are recorded in `pnpm-lock.yaml`; each package remains under its own license.

## CadQuery and OCP

The parameterized concept model uses CadQuery/OCP as declared in `hardware/cad/requirements.txt`. Those tools remain under their upstream licenses.

## Vendor documentation and geometry

CubeMars and Waveshare STEP/PDF source files are deliberately not redistributed. `hardware/cad/vendor/SOURCE_MANIFEST.md` lists original download locations and hashes for local reproduction. Texas Instruments data sheets and evaluation-module guides are also not copied into the public repository; obtain them from TI.

Product names, logos, data sheets, and reference geometry remain the property of their respective owners. Inclusion of a source link does not imply endorsement.

## Related open-source projects

The compatibility audit currently targets these external force-feedback configuration structures:

- [SmartKnob](https://github.com/scottbez1/smartknob/tree/4eb988399c3fda6ffd3006772856093dfe9adb86), Apache License 2.0;
- [X-Knob](https://github.com/SmallPond/X-Knob/tree/05be44fc62b27c4fa941aabd2a7e9b2553f91fb9), MIT License.

The product-scope research also references [SuperDial](https://github.com/CharlieYu4994/superdial/tree/1973d9436a7220f16eec6f76aac6d7029588c03f), MIT License, as a force-feedback PC-peripheral benchmark. It is not a current configuration-adapter target because the audited public implementation does not expose a portable structured haptic configuration.

No upstream source tree, firmware binary, artwork, CAD, or configuration fixture is vendored in this repository. The current documentation describes configuration-mapping semantics only. Any future adapter that includes upstream code or fixtures must preserve attribution, notices, license compatibility, and a pinned source revision before merge; see `docs/profile-compatibility.md`.
