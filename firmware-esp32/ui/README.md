# ESP32 UI

The active 466×466 product UI is the C renderer under `../components/gl30_ui`, using `KK_UI` + RGB565 `KK_OLED`.

- [`kk-preview/`](kk-preview/README_CN.md) compiles the **same C renderer** on Linux and Windows for deterministic frame inspection and tests.
- `preview/assets/` contains the source wallpaper used to generate the firmware RGB565 asset. It contains no separate UI implementation.
- The checked-in mountain wallpaper is retained as an asset, while the product screen no longer renders the old theme-name labels over it.

Development version: `esp32-ui-20260929`. See [`../../docs/esp32-ui-recovery-20260929.md`](../../docs/esp32-ui-recovery-20260929.md) for the recovery audit, current verification, and separately labelled historical display measurements.
