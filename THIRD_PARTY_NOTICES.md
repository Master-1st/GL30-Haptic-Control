# Third-party notices

This file records material that is not originally authored for GL30 Haptic Control. The repository license does not replace third-party terms.

## STM32CubeG4, CMSIS, and STM32 LL drivers

The generated STM32 project contains the minimal CMSIS and STM32G4 LL dependency files used by the active Keil target. Their upstream license files remain beside the code:

- `firmware-stm32/cubemx/GL30_AMOLED_V7/Drivers/CMSIS/LICENSE.txt`
- `firmware-stm32/cubemx/GL30_AMOLED_V7/Drivers/CMSIS/Device/ST/STM32G4xx/LICENSE.md`
- `firmware-stm32/cubemx/GL30_AMOLED_V7/Drivers/STM32G4xx_HAL_Driver/LICENSE.md`

STM32CubeMX, STM32CubeG4, STM32, and STMicroelectronics names are owned by STMicroelectronics. Keil, Arm Compiler, and CMSIS names are owned by Arm or their respective rights holders.

## JavaScript dependencies

The TypeScript workspace uses packages declared in `package.json` and workspace package manifests. Exact resolved versions are recorded in `pnpm-lock.yaml`; each package remains under its own license.

## CadQuery and OCP

The parameterized concept model uses CadQuery/OCP as declared in `hardware/cad/requirements.txt`. Those tools remain under their upstream licenses.

## Vendor documentation and geometry

CubeMars and Waveshare STEP/PDF source files are deliberately not redistributed. `hardware/cad/vendor/SOURCE_MANIFEST.md` lists original download locations and hashes for local reproduction. Texas Instruments data sheets and evaluation-module guides are also not copied into the public repository; obtain them from TI.

Product names, logos, data sheets, and reference geometry remain the property of their respective owners. Inclusion of a source link does not imply endorsement.

## Related open-source projects

The wider haptic-knob ecosystem informs the problem domain. No third-party haptic-knob source tree is vendored in this repository. Any future code reuse must preserve attribution and license compatibility here before merge.
