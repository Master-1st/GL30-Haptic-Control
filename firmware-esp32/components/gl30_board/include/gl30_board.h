#ifndef GL30_BOARD_H
#define GL30_BOARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GL30_BOARD_LCD_WIDTH 466U
#define GL30_BOARD_LCD_HEIGHT 466U
#define GL30_BOARD_LCD_FRAME_BYTES \
    (GL30_BOARD_LCD_WIDTH * GL30_BOARD_LCD_HEIGHT * sizeof(uint16_t))

/*
 * Waveshare ESP32-S3-Touch-AMOLED-1.32 board mapping. The mapping is taken
 * from the official schematic and BSP source; no application GPIO is guessed.
 *
 * https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.32/Resources-And-Documents
 * https://github.com/waveshareteam/Waveshare-ESP32-components/tree/master/bsp/esp32_s3_touch_amoled_1_32
 */

/** Initialize the SH8601 QSPI panel and CST820 I2C touch controller. */
bool gl30_board_display_init(void);

typedef enum {
    GL30_DISPLAY_OK = 0,
    GL30_DISPLAY_BUSY,
    GL30_DISPLAY_ERROR,
} gl30_display_status;

typedef struct {
    uint32_t frame_id;
    bool success;
    uint64_t submit_us;
    uint64_t start_us;
    uint64_t final_dma_done_us;
    uint64_t done_us;
    uint32_t copy_us; /**< Legacy alias of copy_wait_us: residual semaphore wait. */
    uint32_t copy_submit_us; /**< CPU-side API call wall time, includes cache/descriptor work. */
    uint32_t copy_span_us; /**< Sum of API-entry to DMA callback spans; overlaps SPI. */
    uint32_t io_submit_us;
    uint32_t wait_us;
    uint32_t bytes_sent;
    uint32_t strip_count;
} gl30_display_result;

/** Submit one full frame whose BYTES are panel-wire-order MSB-first RGB565.
 * The pointer remains immutable until successful completion. Any fatal error
 * quarantines all DMA-visible memory until reset; busy() then remains true.
 * Public OLED drawing colors remain native RGB565; this BSP boundary does not.
 */
gl30_display_status gl30_board_display_submit(const uint16_t *rgb565,
                                               uint32_t frame_id);

/** Consume one completed frame result; returns false when none is pending. */
bool gl30_board_display_take_result(gl30_display_result *result);

/** True while pending OR fatally quarantined; a late callback cannot clear a fault. */
bool gl30_board_display_busy(void);
/* UI owner only. Claim only after successful DMA/control drain. All producers
 * are blocked under the same state lock until end; quarantine cannot be cleared. */
bool gl30_board_display_maintenance_begin(void);
bool gl30_board_display_maintenance_active(void);
void gl30_board_display_maintenance_end(void);

/** Monotonic count incremented when a frame is accepted for display. */
uint32_t gl30_board_display_accepted_frames(void);

/** Submit one complete frame through the worker and wait for its result. */
bool gl30_board_display_frame(const uint16_t *rgb565);

/** Turn the panel display output on or off. */
bool gl30_board_display_power(bool on);

/** Set panel brightness command 0x51 value (0..255). */
bool gl30_board_display_contrast(uint8_t value);

/** Poll CST820; return true when the controller transaction completed. */
bool gl30_board_touch_read(int16_t *x, int16_t *y, bool *pressed);

/** Observations from the actual panel/touch transactions; read on the UI task. */
typedef struct {
    uint32_t frames_sent;
    uint32_t frame_errors;
    uint32_t last_frame_us;
    uint32_t max_frame_us;
    uint32_t last_copy_us;
    uint32_t last_copy_submit_us;
    uint32_t last_copy_span_us;
    uint32_t last_submit_us;
    uint32_t last_wait_us;
    uint32_t last_frame_id;
    uint32_t accepted_frames;
    uint64_t last_submit_us64;
    uint64_t last_dma_done_us64;
    uint32_t display_stack_free;
    uint32_t internal_dma_largest;
    uint32_t touch_reads;
    uint32_t touch_errors;
    uint32_t touch_presses;
    int16_t last_touch_x;
    int16_t last_touch_y;
    bool touch_pressed;
} gl30_board_stats;
void gl30_board_get_stats(gl30_board_stats *snapshot);

#ifdef __cplusplus
}
#endif

#endif
