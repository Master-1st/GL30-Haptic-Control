/* Deterministic host checks of production UI/buffer ownership.
 * These checks model completion events; they do not measure device FPS. */
#include "gl30_demo.h"
#include "kk_oled.h"
#include "kk_oled_driver.h"
#include "kk_oled_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned gl30_host_frame_id(void);
extern const uint16_t *gl30_host_frame(void);
extern void gl30_host_async_mode(int mode);

static unsigned checks;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "PIPELINE FAIL %d: %s\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static void render_at(uint32_t now)
{
    gl30_demo_sample(now);
    gl30_demo_render(now);
}

static void completion_wake_only_submits_prepared_pixels(void)
{
    static uint16_t frozen_copy[466U * 466U];
    static uint16_t ready_copy[466U * 466U];
    CHECK(gl30_demo_init(0, 0));
    CHECK(gl30_demo_raster_count() == 0);
    gl30_host_async_mode(1);
    gl30_demo_shortcut(1);
    render_at(40);
    CHECK(gl30_demo_raster_count() == 1 && OLED_IsBusy());
    const uint16_t *frozen = OLED_InternalGetTransferBuffer();
    memcpy(frozen_copy, frozen, sizeof(frozen_copy));

    /* Raster B overlaps transfer A. A later input must not overwrite B. */
    gl30_demo_rotate(2);
    render_at(80);
    CHECK(gl30_demo_raster_count() == 2 && OLED_IsBusy());
    const uint16_t *ready = OLED_InternalTestGetDrawBuffer();
    CHECK(ready != frozen);
    memcpy(ready_copy, ready, sizeof(ready_copy));
    gl30_demo_rotate(3);
    gl30_demo_sample(90);
    for (uint32_t now = 90; now < 96; ++now) {
        gl30_demo_service_display(now);
        CHECK(gl30_demo_raster_count() == 2 && gl30_host_frame_id() == 0);
        CHECK(memcmp(frozen_copy, frozen, sizeof(frozen_copy)) == 0);
        CHECK(memcmp(ready_copy, ready, sizeof(ready_copy)) == 0);
    }

    /* This wake is deliberately independent of a raster deadline. */
    OLED_DriverHandleMemTxComplete();
    CHECK(gl30_host_frame_id() == 1);
    gl30_demo_service_display(96);
    CHECK(gl30_demo_raster_count() == 2 && OLED_IsBusy());
    CHECK(OLED_InternalGetTransferBuffer() == ready);
    CHECK(memcmp(ready_copy, ready, sizeof(ready_copy)) == 0);
    CHECK(gl30_demo_state()->now_ms == 90); /* No state/animation tick. */
    const uint16_t *next_draw = OLED_InternalTestGetDrawBuffer();
    CHECK(next_draw != ready && next_draw != frozen); /* Third physical frame. */

    OLED_DriverHandleMemTxComplete();
    CHECK(gl30_host_frame_id() == 2);
    gl30_demo_service_display(97);
    gl30_demo_service_display(98);
    CHECK(gl30_demo_raster_count() == 2 && !OLED_IsBusy());
    CHECK(gl30_host_frame_id() == 2); /* No duplicate submit on repeated wakes. */

    render_at(112);
    CHECK(gl30_demo_raster_count() == 3 && OLED_IsBusy());
    CHECK(memcmp(ready_copy, OLED_InternalGetTransferBuffer(), sizeof(ready_copy)) != 0);
    OLED_DriverHandleMemTxComplete();
    gl30_demo_metrics metrics;
    gl30_demo_get_metrics(&metrics);
    CHECK(metrics.frames == gl30_demo_raster_count());
    CHECK(gl30_demo_display_ok());
}

static void failed_completion_never_submits_the_ready_frame(void)
{
    CHECK(gl30_demo_init(0, 0));
    gl30_host_async_mode(1);
    gl30_demo_shortcut(1);
    render_at(40);
    gl30_demo_rotate(1);
    render_at(60);
    CHECK(gl30_demo_raster_count() == 2 && OLED_IsBusy());
    OLED_DriverHandleError();
    gl30_demo_service_display(61);
    CHECK(!gl30_demo_display_ok());
    CHECK(gl30_host_frame_id() == 0 && !OLED_IsBusy());
    OLED_DriverHandleMemTxComplete(); /* A late callback cannot clear the fault. */
    gl30_demo_service_display(62);
    gl30_demo_render(80);
    CHECK(!gl30_demo_display_ok() && gl30_demo_raster_count() == 2);
    CHECK(gl30_host_frame_id() == 0);
}

static void per_buffer_retirement_preserves_only_the_static_rows(void)
{
    const uint16_t *physical[3] = {0};
    CHECK(OLED_Init() == OLED_OK);
    CHECK(OLED_GetFrameBufferCount() == 3);
    gl30_host_async_mode(1);
    for (unsigned frame = 0; frame < 12; ++frame) {
        const uint16_t *draw = OLED_InternalTestGetDrawBuffer();
        if (frame < 3) {
            physical[frame] = draw;
            for (unsigned old = 0; old < frame; ++old) CHECK(physical[old] != draw);
            OLED_SetClearRows(0, 466);
        } else {
            OLED_SetClearRows(56, 366);
        }
        OLED_SetColor(0xffff, 0);
        OLED_Clear();
        if (frame < 3) {
            OLED_DrawPixel(20, 10);  /* Persistent top chrome. */
            OLED_DrawPixel(465, 465); /* Last row and final partial coverage tile. */
        }
        const unsigned x = 14 + frame; /* Cross a 16-pixel coverage boundary. */
        OLED_SetColor(0xf800, 0);
        OLED_DrawPixel((int16_t)x, 56);
        OLED_DrawPixel((int16_t)x, 365); /* Last dynamic row is included. */
        OLED_SetRetireClearRows(56, 366);
        OLED_MarkFrameChanged();
        CHECK(OLED_UpdateDMA() == OLED_OK && OLED_IsBusy());
        CHECK(OLED_InternalGetTransferBuffer() == draw);
        CHECK(OLED_InternalTestGetDrawBuffer() != draw);
        OLED_DriverHandleMemTxComplete();
        const uint16_t *panel = gl30_host_frame();
        CHECK(panel[10 * 466 + 20] == 0xffff);
        CHECK(panel[466 * 466 - 1] == 0xffff);
        for (unsigned column = 0; column < 466; ++column) {
            CHECK(panel[56 * 466 + column] == (column == x ? 0xf800 : 0));
            CHECK(panel[365 * 466 + column] == (column == x ? 0xf800 : 0));
        }
    }
}

int main(void)
{
    completion_wake_only_submits_prepared_pixels();
    failed_completion_never_submits_the_ready_frame();
    per_buffer_retirement_preserves_only_the_static_rows();
    printf("Host display pipeline: %u checks passed (no hardware FPS measurement)\n", checks);
    return 0;
}
