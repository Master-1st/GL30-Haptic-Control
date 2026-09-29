# GL30 ESP32-S3 display firmware

**Development version:** `esp32-ui-20260929`

**Target:** Waveshare ESP32-S3-Touch-AMOLED-1.32, 466×466

**Toolchain:** ESP-IDF **5.5.1**, target `esp32s3`

This application recovers the uncommitted September 17 ESP32 workspace and fixes
remaining UI and scheduling issues. The September 8 main baseline contained only
eight ESP32 README files. The [recovery audit](../docs/esp32-ui-recovery-20260929.md)
records the gap, provenance, tests and separately labelled historical data.

Runtime: `main/app_main.c` → `components/gl30_ui` → `KK_UI` → RGB565 `KK_OLED`
→ `gl30_board` → CO5300/CST820. The [host preview](ui/kk-preview/README_CN.md)
compiles the same C renderer.

## UI

- Eight-item perspective carousel, with one icon geometry across sizes and
  selected/adjacent/rear design targets of 14 / 6 / 3 mm.
- Mountain wallpaper retained; visible `SUMMIT / Golden Summit / 日照金山`
  labels removed; weekday and short date grouped below the clock.
- No large desktop weather icon; a lightweight weather text row remains.
- Month calendar with seven weekday columns, six date rows, Sunday/Monday-first
  support, a header separator and current-day highlighting.
- English by default; Settings → Language switches between English and Chinese.
- Runtime settings, timer, stopwatch and demonstration pages. Settings reset on
  reboot; weather is sample data and external services remain open.

## Display scheduling and evidence

The application targets a **16,667 us frame period**. Three PSRAM buffers
separate drawing, immutable transfer/READY storage and background retirement.
DMA and spare-ready wakes service prepared frames immediately. A raster blocked
on storage keeps its due slot; sub-period lateness does not skip a whole frame.

Scheduling reads a lightweight raster counter. Full statistics and percentile
sorting are reserved for diagnostics. Short waits round up to one FreeRTOS tick
at a 100 Hz tick rate; completion notifications can end the wait early.

Archived September 17 runs report **59.9796 FPS over 60.003718 s** and
**59.7975 FPS over 10.033865 s**. They are historical completed-frame metrics,
with `pixel_uniqueness_verified=false` and `motor_tx=0`, and are **not device
measurements of this revision**. Current validation uses host tests and firmware
builds; see the [evidence audit](../docs/esp32-ui-recovery-20260929.md).

## Build and test

From an ESP-IDF **5.5.1** shell:

```sh
cd firmware-esp32
idf.py set-target esp32s3
idf.py build
```

On Windows, [bsp/build-firmware.ps1](bsp/build-firmware.ps1) accepts `-SdkPath`,
`-ToolsPath` and `-BuildDirectory`; it builds without flashing. Managed components
use `dependencies.lock`. Generated SDK configuration, downloaded components and
build products are not committed.

Host tests from the repository root with CMake and a C11 compiler:

```sh
cmake -S firmware-esp32/ui/kk-preview -B build/esp32-ui -DCMAKE_BUILD_TYPE=Debug
cmake --build build/esp32-ui --config Debug --parallel 2
ctest --test-dir build/esp32-ui -C Debug --output-on-failure

cmake -S firmware-esp32/transport/tests -B build/esp32-link -DCMAKE_BUILD_TYPE=Debug
cmake --build build/esp32-link --config Debug --parallel 2
ctest --test-dir build/esp32-link -C Debug --output-on-failure

python firmware-esp32/tools/verify_kk_assets.py
python -m unittest discover -s firmware-esp32/tools/tests
```

Normal builds use checked-in font and wallpaper C assets. Font regeneration is
an explicit external-service operation, not a build or CI dependency. The source
wallpaper is retained under `ui/preview/assets/`.

## STM32 link boundary

UART: 5 Mbaud, 8N1; J1-11 is ESP TX → STM PB11 RX and J1-12 is ESP RX ← STM PB10 TX.
USB Serial/JTAG owns console logging. Expansion signals must not back-power
either 3.3 V rail.

The default firmware receives telemetry but **does not transmit motor-control
commands**. `MOTOR ARM` is unavailable by default. The recovered menu control
expects `0x06 HAPTIC_STATE` acknowledgements, which the current published STM32
application does not emit. `CONFIG_GL30_EXPERIMENTAL_MOTOR_CONTROL=y` is only for
matching development firmware; synthetic host packets do not prove a physical
ESP32↔STM32 control loop.

The additive shared HAPTIC_STATE codec supports the decoder and host tests; it
does not add a producer to the STM32 application. H25 source identity, archived
bench evidence and [hardware-gates.md](../docs/hardware-gates.md) are preserved.
