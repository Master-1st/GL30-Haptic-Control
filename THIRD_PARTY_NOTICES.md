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

## LVGL host UI rendering

The optional ESP UI host renderer links [LVGL 9.2.0](https://github.com/lvgl/lvgl/tree/aa7446344c6ec7631112ef031983ef24077e24d5), pinned to commit `aa7446344c6ec7631112ef031983ef24077e24d5`, under the MIT license (Copyright (c) 2021 LVGL Kft). The source is fetched into the ignored build directory, not vendored. Its `LICENCE.txt` must accompany redistribution of the linked renderer or substantial portions of LVGL. Host builds use the upstream Montserrat font data; its `src/font/lv_font_montserrat_*.c` files retain their upstream notices.

## CadQuery and OCP

The parameterized concept model uses CadQuery/OCP as declared in `hardware/cad/requirements.txt`. Those tools remain under their upstream licenses.

## KK_OLED and KK_UI

The project-local Skills under `.agents/skills/kk-*` were copied without modification from [KK_OLED](https://gitee.com/keysking/kk_oled), commit `f01831d63b1d426b629921edaba644732aa29223`, and [KK_UI](https://gitee.com/keysking/kk_ui), commit `582c3442ecbc539c1c82a342676b5b2eda69eee0`. The 49-file integrity record is `.agents/skills/KK_UPSTREAM_MANIFEST.json`.

The embedded runtime copies in `firmware-esp32/components/kk_oled` and `kk_ui` retain their upstream MIT licenses. KK_OLED is Copyright (c) 2026 Qingdao BaudDance Technology Co., Ltd.; KK_UI is Copyright (c) 2026 keysking. The GL30 version changes the original monochrome framebuffer to RGB565, expands the canvas to 466×466, and uses custom application pages. These changes are project adaptations, not claims about upstream color support. Component `README_GL30.md` files describe the changes.

## ESP32 display and touch dependencies

The ESP32 build uses ESP-IDF v5.5.1 and the components pinned by `firmware-esp32/dependencies.lock`: `espressif/esp_lcd_co5300` 2.2.0, `espressif/esp_lcd_touch` 1.2.1, `kodediy/esp_lcd_touch_cst820` 1.0.1, and `espressif/cmake_utilities` 0.5.3. Their fetched license files remain in `firmware-esp32/managed_components/`; the firmware delivery includes copies of the component licenses. ESP-IDF and its third-party components retain their own license terms.

Board wiring and panel initialization follow the [Waveshare ESP32-S3-Touch-AMOLED-1.32 BSP](https://github.com/waveshareteam/Waveshare-ESP32-components/tree/d081959d3841e0b370c2957c122bf8604ab42bc8/bsp/esp32_s3_touch_amoled_1_32), Apache-2.0. The local board layer replaces the upstream LVGL and audio integration with a blocking RGB565 display and touch interface. The upstream license is retained as `firmware-esp32/components/gl30_board/LICENSE_WAVESHARE`.

## Display fonts and background

The GL30 font arrays are glyph subsets of WenQuanYi Micro Hei Mono (`WenQuanDengKuanWeiMiHei`), produced using the [LEDFont API](https://ledfont.botelvdong.com/agent-api.md). The source font permits Apache-2.0 or GPLv3 with a font exception; this distribution uses Apache-2.0. The font copyright notices, release notes, and Apache license are retained in `firmware-esp32/components/gl30_ui/assets/WQY_README.txt` and `LICENSE_WQY_Apache2.txt`. The generation manifest records the selected face, sizes, and glyphs.

The mountain background reuses the project's existing `firmware-esp32/ui/preview/assets/summit-dawn.png`. This integration only resizes and converts that existing artwork to RGB565; it does not introduce a new stock-photo source or grant new rights to the original image.

The current compact glyph coverage arrays in `gl30_type_data.c` are generated from Noto Sans CJK SC Regular under SIL Open Font License 1.1. Copyright and license text are retained in `firmware-esp32/components/gl30_ui/assets/LICENSE_Noto_OFL.txt`. The large source OTF is a regeneration input and is not shipped in Git; `firmware-esp32/tools/font-source/README.md` explains the local input and upstream source. This does not relicense the glyph material as Apache-2.0.

## Vendor documentation and geometry

CubeMars and Waveshare STEP/PDF source files are deliberately not redistributed. `hardware/cad/vendor/SOURCE_MANIFEST.md` lists original download locations and hashes for local reproduction. Texas Instruments data sheets and evaluation-module guides are also not copied into the public repository; obtain them from TI.

Product names, logos, data sheets, and reference geometry remain the property of their respective owners. Inclusion of a source link does not imply endorsement.

## Related open-source projects

The compatibility audit currently targets these external force-feedback configuration structures:

- [SmartKnob](https://github.com/scottbez1/smartknob/tree/4eb988399c3fda6ffd3006772856093dfe9adb86), Apache License 2.0;
- [X-Knob](https://github.com/SmallPond/X-Knob/tree/05be44fc62b27c4fa941aabd2a7e9b2553f91fb9), MIT License.

The product-scope research also references [SuperDial](https://github.com/CharlieYu4994/superdial/tree/1973d9436a7220f16eec6f76aac6d7029588c03f), MIT License, as a force-feedback PC-peripheral benchmark. It is not a current configuration-adapter target because the audited public implementation does not expose a portable structured haptic configuration.

No upstream source tree, firmware binary, artwork, CAD, or configuration fixture is vendored in this repository. In addition to configuration-mapping research, the H26 offline haptics implementation borrows SmartKnob's behavioural ideas of logical detents, snap hysteresis, and a centre deadzone. The local arithmetic, physical Nm parameters, bounded multi-detent updates, and transition rules are independently implemented; upstream voltage/PID gains are not copied. Any future adapter that includes upstream code or fixtures must preserve attribution, notices, license compatibility, and a pinned source revision before merge; see `docs/profile-compatibility.md`.
