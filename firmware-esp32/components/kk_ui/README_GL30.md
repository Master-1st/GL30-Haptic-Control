# KK_UI in GL30

Upstream: https://gitee.com/keysking/kk_ui
Commit: 582c3442ecbc539c1c82a342676b5b2eda69eee0 (MIT, license included).

The two private screen constants are changed from 128x64 to 466x466.
The project config selects asynchronous DMA refresh at 28 ms and disables the unused
128x64 templates and overlays. All GL30 pages are implemented in the upstream
custom-page callbacks. Input dispatch, frame scheduling, display ownership,
error propagation and refresh state remain KK_UI's responsibility.

Do not compile the runtime inside `.agents/skills`; those are upstream skill
assets. Project runtime sources live here.

Known model changes/animations explicitly mark the rendered draw buffer to skip
its optional full-frame comparison. Generic invalidation/default OLED updates
still deduplicate. Busy and error recovery contracts remain in KK_OLED; fatal
BSP DMA faults quarantine memory until reset. See the current display audit in
`docs/esp32-dirty-audit-20260915-cn.md` at the repository root.
