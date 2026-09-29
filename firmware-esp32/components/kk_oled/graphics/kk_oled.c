#include "kk_oled_internal.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#if defined(ESP_PLATFORM)
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#define OLED_FRAMEBUFFER_ATTR EXT_RAM_BSS_ATTR
#define OLED_FRAMEBUFFER_ALIGN __attribute__((aligned(64)))
#define OLED_HOT_ATTR IRAM_ATTR
#else
#include <time.h>
#define OLED_FRAMEBUFFER_ATTR
#define OLED_FRAMEBUFFER_ALIGN
#define OLED_HOT_ATTR
#endif

/* 核心层负责双缓冲、差分提交、画布状态和逻辑像素到物理显存的映射。 */

/** 刷新请求使用的底层传输方式。 */
typedef enum {
    OLED_UPDATE_BLOCKING = 0, /**< 阻塞发送命令和数据。 */
    OLED_UPDATE_IT,           /**< 命令、数据均使用中断发送。 */
    OLED_UPDATE_DMA           /**< 命令用中断，数据用 DMA。 */
} OLED_UpdateMode;

/* Three physical frames decouple transfer, drawing and background retirement.
 * Each frame starts on its own cache line. Padding is storage-only;
 * comparisons and panel transfers remain exactly 434312 bytes. */
#define OLED_FRAME_COUNT 3U
#define OLED_STORAGE_PIXELS (((OLED_BUFFER_SIZE + 31U) / 32U) * 32U)
static uint16_t oled_buffers[OLED_FRAME_COUNT][OLED_STORAGE_PIXELS] OLED_FRAMEBUFFER_ATTR OLED_FRAMEBUFFER_ALIGN;
_Static_assert(sizeof(oled_buffers[0]) % 64U == 0U, "frame cache isolation");
static bool oled_frame_changed[OLED_FRAME_COUNT];

/* The SH8601 QSPI panel consumes RGB565 most-significant byte first. Keep all public
 * colors and drawing math in ordinary host-endian RGB565, but store each
 * framebuffer word byte-swapped so its in-memory byte sequence is already
 * panel-native. This removes the full-frame byte shuffle from the hot send
 * path. */
static inline uint16_t oled_wire_encode(uint16_t color)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return color;
#else
    return (uint16_t)((color << 8U) | (color >> 8U));
#endif
}

static inline uint16_t oled_wire_decode(uint16_t wire_color)
{
    return oled_wire_encode(wire_color);
}

/* A freshly swapped draw buffer is already cleared. Retain the public Clear
 * semantics without writing the same full PSRAM frame twice. Every write
 * below invalidates this fact. */
static bool oled_uniform[OLED_FRAME_COUNT];
static uint16_t oled_uniform_color[OLED_FRAME_COUNT];

/** Track touched 16-pixel tiles per physical row. A single [lo,hi] span made
 * sparse menu objects clear the untouched gap between them, inflating PSRAM
 * retirement traffic. 16 pixels is the measured best sparse fallback on the
 * ESP32-S3; menu partial bands use a faster contiguous clear below. */
#define OLED_COVERAGE_TILE_SHIFT 4U
#define OLED_COVERAGE_TILE_WIDTH (1U << OLED_COVERAGE_TILE_SHIFT)
#define OLED_COVERAGE_TILE_COUNT \
    ((OLED_PHYSICAL_WIDTH + OLED_COVERAGE_TILE_WIDTH - 1U) / OLED_COVERAGE_TILE_WIDTH)
_Static_assert(OLED_COVERAGE_TILE_COUNT <= 32U, "coverage tile mask must fit uint32_t");
typedef struct {
    uint32_t tiles[OLED_PHYSICAL_HEIGHT];
    uint16_t base_color;
    bool valid;
} OLED_Coverage;

static OLED_Coverage oled_coverage[OLED_FRAME_COUNT];
static OLED_Metrics oled_metrics;
/** Three-buffer ownership: stable recovery source, UI draw target, frozen transfer. */
static volatile uint8_t oled_stable_index;       /**< 最近稳定可完整重发帧的缓冲区索引。 */
static uint8_t oled_draw_index = 1U;             /**< 用户当前正在绘制的缓冲区索引。 */
static volatile uint8_t oled_transfer_index = 1U; /**< 本次传输期间被冻结的缓冲区索引。 */
static volatile uint8_t oled_spare_index = 2U;   /**< 后台清理完成后供下一次绘制使用。 */
static volatile bool oled_spare_ready = true;    /**< spare 是否已经按下一帧策略清理。 */
static volatile bool oled_async_pending;         /**< 核心是否等待异步传输完成。 */
static volatile bool oled_force_full;            /**< 下次刷新是否必须发送全屏。 */
static bool oled_initialized;                    /**< 屏幕和核心状态是否初始化成功。 */
static bool oled_power_save;                     /**< 当前是否处于显示关闭状态。 */
static volatile OLED_Status oled_last_status = OLED_OK; /**< 最近操作的可查询状态。 */
static uint16_t oled_foreground = 0xFFFFU;       /**< 当前 RGB565 前景色。 */
static uint16_t oled_background = 0x0000U;       /**< 当前 RGB565 背景色。 */
static uint16_t oled_clear_y0;
static uint16_t oled_clear_y1 = OLED_PHYSICAL_HEIGHT;
static uint16_t oled_retire_background[OLED_FRAME_COUNT];
static uint16_t oled_retire_y0[OLED_FRAME_COUNT];
static uint16_t oled_retire_y1[OLED_FRAME_COUNT];

#if defined(ESP_PLATFORM)
typedef struct {
    uint8_t index;
    uint16_t background;
    uint16_t y0;
    uint16_t y1;
} OLED_RetireClearJob;

static QueueHandle_t oled_retire_queue;
static TaskHandle_t oled_retire_task;
static TaskHandle_t oled_ui_owner;
static volatile bool oled_retire_pending;
static portMUX_TYPE oled_retire_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE oled_metrics_lock = portMUX_INITIALIZER_UNLOCKED;
#endif

static OLED_Rotation oled_rotation = OLED_ROTATION_0; /**< 当前画布旋转方向。 */
static OLED_DrawMode oled_draw_mode = OLED_DRAW_SET;  /**< 当前前景像素写入方式。 */
static OLED_BackgroundMode oled_background_mode = OLED_BG_TRANSPARENT; /**< 背景写入方式。 */
static int16_t oled_clip_x0; /**< 裁剪窗口左边界，包含该列。 */
static int16_t oled_clip_y0; /**< 裁剪窗口上边界，包含该行。 */
static int16_t oled_clip_x1 = (int16_t)OLED_PHYSICAL_WIDTH;  /**< 裁剪窗口右边界，不包含。 */
static int16_t oled_clip_y1 = (int16_t)OLED_PHYSICAL_HEIGHT; /**< 裁剪窗口下边界，不包含。 */

/** 获取当前平台的微秒计时；主机只用于验证，性能指标以 ESP 为准。 */
static uint64_t oled_time_us(void)
{
#if defined(ESP_PLATFORM)
    return (uint64_t)esp_timer_get_time();
#else
    struct timespec timestamp;

    if (timespec_get(&timestamp, TIME_UTC) != TIME_UTC) {
        return 0U;
    }
    return (uint64_t)timestamp.tv_sec * 1000000U +
           (uint64_t)timestamp.tv_nsec / 1000U;
#endif
}

/** 计时源不可用或时钟回退时不把时间累计为巨大无符号值。 */
static uint64_t oled_elapsed_us(uint64_t started)
{
    uint64_t finished = oled_time_us();

    return (finished >= started) ? finished - started : 0U;
}

/** 清空一个缓冲区的行覆盖记录，并建立新的基准色。 */
static void oled_reset_coverage(uint8_t index, uint16_t base_color)
{
    memset(oled_coverage[index].tiles, 0, sizeof(oled_coverage[index].tiles));
    oled_coverage[index].base_color = base_color;
    oled_coverage[index].valid = true;
}

/** Mark all 16-pixel tiles touched by one half-open physical row span. */
static void oled_mark_row(uint8_t index, uint32_t y, int32_t x0,
                          int32_t x1)
{
    uint32_t first_tile;
    uint32_t last_tile;
    uint32_t tile_count;
    uint32_t mask;

    if (y >= OLED_PHYSICAL_HEIGHT) return;
    if (x0 < 0) x0 = 0;
    if (x1 > (int32_t)OLED_PHYSICAL_WIDTH) x1 = (int32_t)OLED_PHYSICAL_WIDTH;
    if (x0 >= x1) return;
    first_tile = (uint32_t)x0 >> OLED_COVERAGE_TILE_SHIFT;
    last_tile = ((uint32_t)x1 - 1U) >> OLED_COVERAGE_TILE_SHIFT;
    tile_count = last_tile - first_tile + 1U;
    mask = ((1UL << tile_count) - 1UL) << first_tile;
    oled_coverage[index].tiles[y] |= mask;
}

/** 记录一个物理像素的写入。 */
static void oled_mark_pixel(uint8_t index, uint32_t pixel)
{
    uint32_t y;
    uint32_t x;

    if (pixel >= OLED_BUFFER_SIZE) {
        return;
    }
    y = pixel / OLED_PHYSICAL_WIDTH;
    x = pixel % OLED_PHYSICAL_WIDTH;
    oled_mark_row(index, y, (int32_t)x, (int32_t)x + 1);
}

/** 记录核心跨度写入；当前跨度只使用连续行或逐行单像素两种步长。 */
static void oled_mark_span(uint8_t index, int32_t base, int32_t stride,
                           uint16_t length)
{
    uint16_t offset;
    int32_t pixel = base;

    if (length == 0U) {
        return;
    }
    if (stride == 1) {
        oled_mark_row(index, (uint32_t)(base / (int32_t)OLED_PHYSICAL_WIDTH),
                      base % (int32_t)OLED_PHYSICAL_WIDTH,
                      base % (int32_t)OLED_PHYSICAL_WIDTH + length);
        return;
    }
    if (stride == (int32_t)OLED_PHYSICAL_WIDTH) {
        for (offset = 0U; offset < length; ++offset) {
            oled_mark_pixel(index, (uint32_t)pixel);
            pixel += stride;
        }
        return;
    }
    for (offset = 0U; offset < length; ++offset) {
        oled_mark_pixel(index, (uint32_t)pixel);
        pixel += stride;
    }
}

typedef struct {
    uint64_t us;
    uint64_t pixels;
    bool skipped;
    bool full;
} OLED_ClearResult;

/** Clear only the horizontal 16-pixel tiles actually touched on one row. */
static uint32_t oled_clear_coverage_row(uint8_t index, uint16_t row,
                                        uint16_t background)
{
    uint32_t mask = oled_coverage[index].tiles[row];
    uint32_t tile = 0U;
    uint32_t cleared = 0U;
    const uint16_t wire_background = oled_wire_encode(background);

    while (tile < OLED_COVERAGE_TILE_COUNT) {
        uint32_t first;
        uint32_t lo;
        uint32_t hi;
        uint32_t pixel;

        while (tile < OLED_COVERAGE_TILE_COUNT &&
               (mask & (1UL << tile)) == 0U) {
            ++tile;
        }
        if (tile >= OLED_COVERAGE_TILE_COUNT) break;
        first = tile;
        do {
            ++tile;
        } while (tile < OLED_COVERAGE_TILE_COUNT &&
                 (mask & (1UL << tile)) != 0U);

        lo = first * OLED_COVERAGE_TILE_WIDTH;
        hi = tile * OLED_COVERAGE_TILE_WIDTH;
        if (hi > OLED_PHYSICAL_WIDTH) hi = OLED_PHYSICAL_WIDTH;
        cleared += hi - lo;
        if ((uint8_t)background == (uint8_t)(background >> 8U)) {
            memset(&oled_buffers[index][row * OLED_PHYSICAL_WIDTH + lo],
                   (uint8_t)background,
                   (size_t)(hi - lo) * sizeof(uint16_t));
        } else {
            for (pixel = lo; pixel < hi; ++pixel) {
                oled_buffers[index][row * OLED_PHYSICAL_WIDTH + pixel] =
                    wire_background;
            }
        }
    }
    oled_coverage[index].tiles[row] = 0U;
    return cleared;
}

/** Clear one owned framebuffer using an immutable policy snapshot. */
static OLED_ClearResult oled_clear_buffer_policy(uint8_t index,
                                                 uint16_t background,
                                                 uint16_t clear_y0,
                                                 uint16_t clear_y1)
{
    uint64_t started = oled_time_us();
    uint32_t pixel;
    uint16_t row;
    OLED_ClearResult result = {0};

    /* Clearing retires every change hint carried by this physical frame. */
    oled_frame_changed[index] = false;

    if (oled_uniform[index] && oled_uniform_color[index] == background) {
        result.skipped = true;
        result.us = oled_elapsed_us(started);
        return result;
    }

    if ((clear_y0 != 0U || clear_y1 != OLED_PHYSICAL_HEIGHT) &&
        oled_coverage[index].valid &&
        oled_coverage[index].base_color == background) {
        for (row = clear_y0; row < clear_y1; ++row) {
            result.pixels += oled_clear_coverage_row(index, row, background);
        }
        /* Rows outside this band intentionally retain persistent chrome. */
        oled_uniform[index] = false;
        result.us = oled_elapsed_us(started);
        return result;
    }

    if (oled_coverage[index].valid &&
        oled_coverage[index].base_color == background) {
        for (row = 0U; row < OLED_PHYSICAL_HEIGHT; ++row) {
            result.pixels += oled_clear_coverage_row(index, row, background);
        }
    } else {
        result.full = true;
        result.pixels += OLED_BUFFER_SIZE;
        if ((uint8_t)background == (uint8_t)(background >> 8U)) {
            memset(oled_buffers[index], (uint8_t)background,
                   OLED_FRAMEBUFFER_BYTES);
        } else {
            for (pixel = 0U; pixel < OLED_BUFFER_SIZE; ++pixel) {
                oled_buffers[index][pixel] = oled_wire_encode(background);
            }
        }
    }
    oled_reset_coverage(index, background);
    oled_uniform[index] = true;
    oled_uniform_color[index] = background;
    result.us = oled_elapsed_us(started);
    return result;
}

/** UI-owner synchronous clear accounting. */
static void oled_clear_buffer(uint8_t index)
{
    OLED_ClearResult result;

    ++oled_metrics.clear_calls;
    result = oled_clear_buffer_policy(index, oled_background,
                                      oled_clear_y0, oled_clear_y1);
    oled_metrics.clear_us += result.us;
    oled_metrics.clear_pixels += result.pixels;
    if (result.skipped) ++oled_metrics.skipped_clears;
    if (result.full) ++oled_metrics.full_clears;
}

static void oled_clear_retired_sync(uint8_t index)
{
    OLED_ClearResult result;

    ++oled_metrics.clear_calls;
    result = oled_clear_buffer_policy(index, oled_retire_background[index],
                                      oled_retire_y0[index], oled_retire_y1[index]);
    oled_metrics.clear_us += result.us;
    oled_metrics.clear_pixels += result.pixels;
    if (result.skipped) ++oled_metrics.skipped_clears;
    if (result.full) ++oled_metrics.full_clears;
}

static void oled_account_retire_clear(OLED_ClearResult result)
{
#if defined(ESP_PLATFORM)
    portENTER_CRITICAL(&oled_metrics_lock);
#endif
    oled_metrics.retire_clear_us += result.us;
    oled_metrics.retire_clear_pixels += result.pixels;
    ++oled_metrics.retire_clear_calls;
#if defined(ESP_PLATFORM)
    portEXIT_CRITICAL(&oled_metrics_lock);
#endif
}

static bool oled_is_spare_ready(void)
{
#if defined(ESP_PLATFORM)
    bool ready;
    portENTER_CRITICAL(&oled_retire_lock);
    ready = oled_spare_ready;
    portEXIT_CRITICAL(&oled_retire_lock);
    return ready;
#else
    return oled_spare_ready;
#endif
}

#if defined(ESP_PLATFORM)
static void oled_retire_clear_worker(void *argument)
{
    OLED_RetireClearJob job;
    (void)argument;

    for (;;) {
        if (xQueueReceive(oled_retire_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        OLED_ClearResult result = oled_clear_buffer_policy(
            job.index, job.background, job.y0, job.y1);
        oled_account_retire_clear(result);
        portENTER_CRITICAL(&oled_retire_lock);
        if (oled_spare_index == job.index) {
            oled_spare_ready = true;
        }
        oled_retire_pending = false;
        portEXIT_CRITICAL(&oled_retire_lock);
        /* A READY frame may be blocked only by this spare retirement. Wake
         * the UI owner immediately instead of waiting for the next frame timer
         * or 100 Hz input sample. */
        if (oled_ui_owner != NULL) {
            xTaskNotifyGive(oled_ui_owner);
        }
    }
}

static bool oled_retire_worker_init(void)
{
    if (oled_retire_queue != NULL && oled_retire_task != NULL) {
        return true;
    }
    oled_retire_queue = xQueueCreate(1U, sizeof(OLED_RetireClearJob));
    if (oled_retire_queue == NULL) {
        return false;
    }
    if (xTaskCreatePinnedToCore(oled_retire_clear_worker, "oled_clear", 3072,
                                NULL, tskIDLE_PRIORITY, &oled_retire_task, 1) != pdPASS) {
        vQueueDelete(oled_retire_queue);
        oled_retire_queue = NULL;
        return false;
    }
    return true;
}
#endif

/** Retire old stable storage without blocking the UI owner on ESP. */
static void oled_retire_buffer(uint8_t index)
{
    uint16_t background = oled_retire_background[index];
    uint16_t y0 = oled_retire_y0[index];
    uint16_t y1 = oled_retire_y1[index];

    oled_spare_index = index;
#if defined(ESP_PLATFORM)
    OLED_RetireClearJob job = {
        .index = index,
        .background = background,
        .y0 = y0,
        .y1 = y1,
    };
    portENTER_CRITICAL(&oled_retire_lock);
    oled_spare_ready = false;
    oled_retire_pending = true;
    portEXIT_CRITICAL(&oled_retire_lock);
    if (oled_retire_queue != NULL && xQueueSend(oled_retire_queue, &job, 0) == pdTRUE) {
        return;
    }
    /* Resource exhaustion is safe but slower: clear synchronously. */
    OLED_ClearResult result = oled_clear_buffer_policy(index, background, y0, y1);
    oled_account_retire_clear(result);
    portENTER_CRITICAL(&oled_retire_lock);
    oled_spare_ready = true;
    oled_retire_pending = false;
    portEXIT_CRITICAL(&oled_retire_lock);
#else
    oled_spare_ready = false;
    OLED_ClearResult result = oled_clear_buffer_policy(index, background, y0, y1);
    oled_account_retire_clear(result);
    oled_spare_ready = true;
#endif
}

/** 比较新旧帧；彩色 AMOLED 按完整 RGB565 帧提交。 */
static bool oled_prepare_dirty(uint8_t new_index, uint8_t old_index, bool full)
{
    bool identical;
    uint64_t started;

    if (full) {
        return true;
    }
    if (oled_frame_changed[new_index]) {
        ++oled_metrics.skipped_compares;
        return true;
    }
    started = oled_time_us();
    ++oled_metrics.compares;
    identical = memcmp(oled_buffers[new_index], oled_buffers[old_index],
                       OLED_FRAMEBUFFER_BYTES) == 0;
    oled_metrics.compare_us += oled_elapsed_us(started);
    if (identical) {
        ++oled_metrics.identical_frames;
    }
    return !identical;
}

/** Complete a blocking/no-transfer commit using the already-clean spare. */
static void oled_commit_blocking(void)
{
    uint8_t old_stable = oled_stable_index;
    uint8_t old_draw = oled_draw_index;
    uint8_t old_spare = oled_spare_index;

    oled_stable_index = old_draw;
    oled_draw_index = old_spare;
    oled_spare_index = old_stable;
    oled_spare_ready = false;
    /* Blocking paths are not throughput-critical and return fully settled. */
    oled_clear_retired_sync(oled_spare_index);
    oled_spare_ready = true;
    oled_force_full = false;
    oled_last_status = OLED_OK;
}

/** 统一处理三种刷新入口的差分准备、驱动启动和缓冲区交换。 */
static OLED_Status oled_begin_update(OLED_UpdateMode mode)
{
    OLED_Status status;
    uint8_t old_stable;
    uint8_t old_draw;
    uint8_t old_spare;

    if (!oled_initialized) {
        oled_last_status = OLED_ERROR;
        return OLED_ERROR;
    }
    if (OLED_IsBusy() || !oled_is_spare_ready()) {
        return OLED_BUSY;
    }

    oled_transfer_index = oled_draw_index;
    if (!oled_prepare_dirty(oled_transfer_index, oled_stable_index, oled_force_full)) {
        oled_commit_blocking();
        return OLED_OK;
    }

    if (mode == OLED_UPDATE_BLOCKING) {
        status = OLED_DriverWriteBlocking();
        if (status == OLED_OK) {
            oled_commit_blocking();
        } else {
            oled_force_full = true;
            oled_last_status = status;
        }
        return status;
    }

    old_stable = oled_stable_index;
    old_draw = oled_draw_index;
    old_spare = oled_spare_index;
    oled_transfer_index = old_draw;
    oled_async_pending = true;
    oled_last_status = OLED_BUSY;
    oled_draw_index = old_spare;
    oled_spare_index = old_stable;
    oled_spare_ready = false;

    status = (mode == OLED_UPDATE_IT) ? OLED_DriverWriteIT() : OLED_DriverWriteDMA();
    if (status != OLED_OK && oled_async_pending) {
        /* Driver never accepted ownership: restore every pre-submit role. */
        oled_async_pending = false;
        oled_stable_index = old_stable;
        oled_draw_index = old_draw;
        oled_spare_index = old_spare;
        oled_spare_ready = true;
        oled_force_full = true;
        oled_last_status = status;
        return status;
    }

    /* Accepted transfer buffer is now the latest complete recovery source,
     * even if the physical transaction later reports an error. Old stable can
     * retire in the low-priority clear worker without blocking the UI owner. */
    oled_stable_index = oled_transfer_index;
    oled_retire_buffer(old_stable);
    return status;
}

OLED_Status OLED_Init(void)
{
    OLED_Status status; /* 屏幕驱动初始化结果。 */
    uint8_t index;

    if (OLED_IsBusy()
#if defined(ESP_PLATFORM)
        || oled_retire_pending
#endif
    ) {
        return OLED_BUSY;
    }
#if defined(ESP_PLATFORM)
    if (!oled_retire_worker_init()) {
        oled_last_status = OLED_ERROR;
        return OLED_ERROR;
    }
    xQueueReset(oled_retire_queue);
    oled_ui_owner = xTaskGetCurrentTaskHandle();
#endif

    /* 恢复所有核心状态，保证重复初始化也从确定的空白帧开始。 */
    oled_foreground = 0xFFFFU;
    oled_background = 0x0000U;
    oled_clear_y0 = 0U;
    oled_clear_y1 = OLED_PHYSICAL_HEIGHT;
    memset(oled_buffers, 0, sizeof(oled_buffers));
    memset(&oled_metrics, 0, sizeof(oled_metrics));
    for (index = 0U; index < OLED_FRAME_COUNT; ++index) {
        oled_reset_coverage(index, 0U);
        oled_uniform[index] = true;
        oled_uniform_color[index] = 0U;
        oled_frame_changed[index] = false;
        oled_retire_background[index] = 0U;
        oled_retire_y0[index] = 0U;
        oled_retire_y1[index] = OLED_PHYSICAL_HEIGHT;
    }
    oled_stable_index = 0U;
    oled_draw_index = 1U;
    oled_transfer_index = 1U;
    oled_spare_index = 2U;
    oled_spare_ready = true;
#if defined(ESP_PLATFORM)
    oled_retire_pending = false;
#endif
    oled_async_pending = false;
    /* Initial panel GRAM is unknown: even an all-black first frame must send. */
    oled_force_full = true;
    oled_power_save = false;
    oled_rotation = OLED_ROTATION_0;
    oled_draw_mode = OLED_DRAW_SET;
    oled_background_mode = OLED_BG_TRANSPARENT;
    OLED_ResetClipWindow();

    status = OLED_DriverInit();
    oled_initialized = (status == OLED_OK);
    oled_last_status = status;
    return status;
}

void OLED_Poll(void)
{
    if (oled_initialized) OLED_DriverPoll();
}

void OLED_MarkFrameChanged(void)
{
    if (oled_initialized) {
        oled_frame_changed[oled_draw_index] = true;
    }
}

OLED_Status OLED_Update(void)
{
    return oled_begin_update(OLED_UPDATE_BLOCKING);
}

OLED_Status OLED_UpdateIT(void)
{
    return oled_begin_update(OLED_UPDATE_IT);
}

OLED_Status OLED_UpdateDMA(void)
{
    return oled_begin_update(OLED_UPDATE_DMA);
}

bool OLED_IsBusy(void)
{
    return oled_async_pending || OLED_DriverIsBusy();
}

OLED_Status OLED_GetLastStatus(void)
{
    return oled_last_status;
}

void OLED_GetMetrics(OLED_Metrics *out)
{
    if (out != NULL) {
#if defined(ESP_PLATFORM)
        portENTER_CRITICAL(&oled_metrics_lock);
#endif
        *out = oled_metrics;
#if defined(ESP_PLATFORM)
        portEXIT_CRITICAL(&oled_metrics_lock);
#endif
    }
}

OLED_Status OLED_SetContrast(uint8_t value)
{
    OLED_Status status;

    if (OLED_IsBusy()) {
        return OLED_BUSY;
    }
    status = OLED_DriverSetContrast(value);
    oled_last_status = status;
    return status;
}

OLED_Status OLED_SetPowerSave(bool enable)
{
    OLED_Status status; /* 关屏命令、恢复帧或开屏命令的结果。 */

    if (OLED_IsBusy()) {
        return OLED_BUSY;
    }
    if (!oled_initialized) {
        oled_last_status = OLED_ERROR;
        return OLED_ERROR;
    }
    if (enable == oled_power_save) {
        oled_last_status = OLED_OK;
        return OLED_OK;
    }

    if (enable) {
        status = OLED_DriverSetPowerSave(true);
        if (status == OLED_OK) {
            oled_power_save = true;
        }
        oled_last_status = status;
        return status;
    }

    /* 显示保持关闭，先完整恢复最近稳定帧，再发送 AF 点亮。 */
    oled_transfer_index = oled_stable_index;
    (void)oled_prepare_dirty(oled_transfer_index, oled_transfer_index, true);
    status = OLED_DriverWriteBlocking();
    if (status == OLED_OK) {
        status = OLED_DriverSetPowerSave(false);
    }
    if (status == OLED_OK) {
        oled_power_save = false;
        /* 唤醒重发不消费异步错误约定的“下一次正常刷新全刷”标志。 */
    } else {
        oled_force_full = true;
    }
    oled_last_status = status;
    return status;
}

uint16_t OLED_GetWidth(void)
{
    return OLED_InternalGetLogicalWidth();
}

uint16_t OLED_GetHeight(void)
{
    return OLED_InternalGetLogicalHeight();
}

uint8_t OLED_GetFrameBufferCount(void)
{
    return OLED_FRAME_COUNT;
}

void OLED_SetColor(uint16_t foreground, uint16_t background)
{
    oled_foreground = foreground;
    oled_background = background;
}

void OLED_Clear(void)
{
    oled_clear_buffer(oled_draw_index);
}

void OLED_SetClearRows(uint16_t y0, uint16_t y1)
{
    if (y0 >= y1 || y1 > OLED_PHYSICAL_HEIGHT) {
        oled_clear_y0 = 0U;
        oled_clear_y1 = OLED_PHYSICAL_HEIGHT;
        return;
    }
    oled_clear_y0 = y0;
    oled_clear_y1 = y1;
}

void OLED_SetRetireClearRows(uint16_t y0, uint16_t y1)
{
    uint8_t index = oled_draw_index;

    if (y0 >= y1 || y1 > OLED_PHYSICAL_HEIGHT) {
        y0 = 0U;
        y1 = OLED_PHYSICAL_HEIGHT;
    }
    oled_retire_background[index] = oled_background;
    oled_retire_y0[index] = y0;
    oled_retire_y1[index] = y1;
}

void OLED_Fill(void)
{
    uint32_t pixel;
    oled_frame_changed[oled_draw_index] = false;

    for (pixel = 0U; pixel < OLED_BUFFER_SIZE; ++pixel) {
        oled_buffers[oled_draw_index][pixel] = oled_wire_encode(oled_foreground);
    }
    oled_reset_coverage(oled_draw_index, oled_foreground);
    oled_uniform[oled_draw_index] = true;
    oled_uniform_color[oled_draw_index] = oled_foreground;
}

/** RGB565 位图尺寸沿用 XBM 的单边上限，避免源索引和接口范围溢出。 */
static bool oled_rgb565_bitmap_size_valid(uint16_t width, uint16_t height)
{
    uint16_t logical_width = OLED_InternalGetLogicalWidth();
    uint16_t logical_height = OLED_InternalGetLogicalHeight();
    uint16_t extent = (logical_width > logical_height) ? logical_width : logical_height;

    return width > 0U && height > 0U && width <= extent && height <= extent;
}

void OLED_BlitRGB565(int16_t x, int16_t y, uint16_t width, uint16_t height,
                     const uint16_t *pixels)
{
    int32_t visible_x0 = x;
    int32_t visible_y0 = y;
    int32_t visible_x1 = (int32_t)x + width;
    int32_t visible_y1 = (int32_t)y + height;
    int32_t destination_y;

    if (pixels == NULL || !oled_rgb565_bitmap_size_valid(width, height) ||
        !OLED_InternalIntersectClip(&visible_x0, &visible_y0,
                                    &visible_x1, &visible_y1)) {
        return;
    }
    oled_uniform[oled_draw_index] = false;
    for (destination_y = visible_y0; destination_y < visible_y1; ++destination_y) {
        int32_t destination_x;
        size_t source_row = (size_t)(destination_y - y) * (size_t)width;

        /* The normal display orientation is a contiguous clipped scanline.
         * Avoid repeating the coordinate mapping for every photo pixel. */
        if (oled_rotation == OLED_ROTATION_0) {
            oled_mark_row(oled_draw_index, (uint32_t)destination_y,
                          visible_x0, visible_x1);
            {
                size_t destination_base = (size_t)destination_y * OLED_PHYSICAL_WIDTH +
                                          (size_t)visible_x0;
                size_t source_base = source_row + (size_t)(visible_x0 - x);
                size_t count = (size_t)(visible_x1 - visible_x0);
                size_t column;

                for (column = 0U; column < count; ++column) {
                    oled_buffers[oled_draw_index][destination_base + column] =
                        oled_wire_encode(pixels[source_base + column]);
                }
            }
            continue;
        }
        for (destination_x = visible_x0; destination_x < visible_x1; ++destination_x) {
            size_t source_column = (size_t)(destination_x - x);
            OLED_InternalPlotColor((int16_t)destination_x, (int16_t)destination_y,
                                   pixels[source_row + source_column]);
        }
    }
}

void OLED_BlendPixelRGB565(int16_t x, int16_t y, uint16_t color,
                           uint8_t alpha)
{
    OLED_InternalBlendColor(x, y, color, alpha);
}

void OLED_SetRotation(OLED_Rotation rotation)
{
    if (rotation > OLED_ROTATION_270) {
        rotation = OLED_ROTATION_0;
    }
    oled_rotation = rotation;
    OLED_ResetClipWindow();
}

void OLED_SetDrawMode(OLED_DrawMode mode)
{
    if (mode <= OLED_DRAW_XOR) {
        oled_draw_mode = mode;
    }
}

void OLED_SetBackgroundMode(OLED_BackgroundMode mode)
{
    if (mode <= OLED_BG_SOLID) {
        oled_background_mode = mode;
    }
}

void OLED_SetClipWindow(int16_t x, int16_t y, uint16_t width, uint16_t height)
{
    int32_t x1 = (int32_t)x + width; /* 请求窗口的半开右边界。 */
    int32_t y1 = (int32_t)y + height; /* 请求窗口的半开下边界。 */
    int32_t logical_width = OLED_InternalGetLogicalWidth();   /* 当前逻辑宽度。 */
    int32_t logical_height = OLED_InternalGetLogicalHeight(); /* 当前逻辑高度。 */

    if (width > (uint16_t)INT16_MAX || height > (uint16_t)INT16_MAX) {
        return;
    }

    /* 先裁剪左上角，再裁剪右下角，并保持窗口不会出现反向区间。 */
    if (x < 0) {
        oled_clip_x0 = 0;
    } else if (x > logical_width) {
        oled_clip_x0 = (int16_t)logical_width;
    } else {
        oled_clip_x0 = x;
    }
    if (y < 0) {
        oled_clip_y0 = 0;
    } else if (y > logical_height) {
        oled_clip_y0 = (int16_t)logical_height;
    } else {
        oled_clip_y0 = y;
    }

    if (x1 < oled_clip_x0) {
        x1 = oled_clip_x0;
    }
    if (y1 < oled_clip_y0) {
        y1 = oled_clip_y0;
    }
    if (x1 > logical_width) {
        x1 = logical_width;
    }
    if (y1 > logical_height) {
        y1 = logical_height;
    }
    oled_clip_x1 = (int16_t)x1;
    oled_clip_y1 = (int16_t)y1;
}

void OLED_ResetClipWindow(void)
{
    oled_clip_x0 = 0;
    oled_clip_y0 = 0;
    oled_clip_x1 = (int16_t)OLED_InternalGetLogicalWidth();
    oled_clip_y1 = (int16_t)OLED_InternalGetLogicalHeight();
}

/** 将逻辑坐标按当前裁剪和旋转映射到线性 RGB565 帧下标。 */
static bool oled_logical_to_index(int16_t x, int16_t y, uint32_t *index)
{
    int32_t physical_x; /* 旋转映射后的物理列。 */
    int32_t physical_y; /* 旋转映射后的物理行。 */

    if (x < oled_clip_x0 || x >= oled_clip_x1 ||
        y < oled_clip_y0 || y >= oled_clip_y1) {
        return false;
    }
    switch (oled_rotation) {
    case OLED_ROTATION_90:
        physical_x = (int32_t)OLED_PHYSICAL_WIDTH - 1 - y;
        physical_y = x;
        break;
    case OLED_ROTATION_180:
        physical_x = (int32_t)OLED_PHYSICAL_WIDTH - 1 - x;
        physical_y = (int32_t)OLED_PHYSICAL_HEIGHT - 1 - y;
        break;
    case OLED_ROTATION_270:
        physical_x = y;
        physical_y = (int32_t)OLED_PHYSICAL_HEIGHT - 1 - x;
        break;
    case OLED_ROTATION_0:
    default:
        physical_x = x;
        physical_y = y;
        break;
    }
    if (physical_x < 0 || physical_x >= (int32_t)OLED_PHYSICAL_WIDTH ||
        physical_y < 0 || physical_y >= (int32_t)OLED_PHYSICAL_HEIGHT) {
        return false;
    }
    *index = (uint32_t)physical_y * (uint32_t)OLED_PHYSICAL_WIDTH +
             (uint32_t)physical_x;
    return true;
}

/** 按 RGB565 的 5/6/5 位通道进行 alpha 混合。 */
static inline uint32_t oled_div255(uint32_t value)
{
    /* Exact for this renderer's range: max is 63*255+127 = 16192. */
    return (value + 1U + (value >> 8U)) >> 8U;
}

static uint16_t oled_blend_rgb565(uint16_t destination, uint16_t source,
                                  uint8_t alpha)
{
    uint32_t inverse = 255U - alpha;
    uint32_t red = oled_div255(((uint32_t)(source >> 11U) * alpha) +
                               ((uint32_t)(destination >> 11U) * inverse) + 127U) & 0x1FU;
    uint32_t green = oled_div255(((uint32_t)((source >> 5U) & 0x3FU) * alpha) +
                                 ((uint32_t)((destination >> 5U) & 0x3FU) * inverse) + 127U) & 0x3FU;
    uint32_t blue = oled_div255(((uint32_t)(source & 0x1FU) * alpha) +
                                ((uint32_t)(destination & 0x1FU) * inverse) + 127U) & 0x1FU;

    return (uint16_t)((red << 11U) | (green << 5U) | blue);
}

/** 将物理跨度按一次确定的绘图状态写入当前绘制缓冲区。 */
static void OLED_HOT_ATTR oled_write_physical_span(int32_t base, int32_t stride,
                                                    uint16_t length, uint16_t color,
                                                    bool xor_pixels)
{
    uint16_t offset;
    int32_t index = base;

    oled_uniform[oled_draw_index] = false;
    oled_mark_span(oled_draw_index, base, stride, length);
    for (offset = 0U; offset < length; ++offset) {
        if (xor_pixels) {
            oled_buffers[oled_draw_index][index] =
                (uint16_t)~oled_buffers[oled_draw_index][index];
        } else {
            oled_buffers[oled_draw_index][index] = oled_wire_encode(color);
        }
        index += stride;
    }
}

void OLED_HOT_ATTR OLED_InternalPlotSourceSpan(int32_t x, int32_t y, uint16_t length,
                                               bool horizontal, bool source_pixel)
{
    int32_t x0 = x;
    int32_t y0 = y;
    int32_t x1;
    int32_t y1;
    int32_t physical_x;
    int32_t physical_y;
    int32_t base;
    int32_t stride;
    uint16_t color = 0U;
    bool xor_pixels = false;

    if (length == 0U) {
        return;
    }
    x1 = horizontal ? x + (int32_t)length : x + 1;
    y1 = horizontal ? y + 1 : y + (int32_t)length;

    if (!OLED_InternalIntersectClip(&x0, &y0, &x1, &y1)) {
        return;
    }
    if (!source_pixel) {
        if (oled_background_mode == OLED_BG_TRANSPARENT ||
            oled_draw_mode == OLED_DRAW_XOR) {
            return;
        }
        /* OLED_BG_SOLID 延续原语义：SET 的反操作写背景，
         * CLEAR 的反操作写前景。 */
        color = (oled_draw_mode == OLED_DRAW_SET) ?
                    oled_background : oled_foreground;
    } else if (oled_draw_mode == OLED_DRAW_SET) {
        color = oled_foreground;
    } else if (oled_draw_mode == OLED_DRAW_CLEAR) {
        color = oled_background;
    } else {
        xor_pixels = true;
    }

    /* The mapped endpoints are reduced to one contiguous or strided physical
     * span.  Reversed logical directions use the lower physical endpoint;
     * writing order is immaterial for SET, CLEAR, and one-pass XOR. */
    switch (oled_rotation) {
    case OLED_ROTATION_90:
        if (horizontal) {
            physical_x = (int32_t)OLED_PHYSICAL_WIDTH - 1 - y0;
            physical_y = x0;
            base = physical_y * (int32_t)OLED_PHYSICAL_WIDTH + physical_x;
            stride = (int32_t)OLED_PHYSICAL_WIDTH;
        } else {
            physical_x = (int32_t)OLED_PHYSICAL_WIDTH - y1;
            physical_y = x0;
            base = physical_y * (int32_t)OLED_PHYSICAL_WIDTH + physical_x;
            stride = 1;
        }
        break;
    case OLED_ROTATION_180:
        if (horizontal) {
            physical_x = (int32_t)OLED_PHYSICAL_WIDTH - x1;
            physical_y = (int32_t)OLED_PHYSICAL_HEIGHT - 1 - y0;
            base = physical_y * (int32_t)OLED_PHYSICAL_WIDTH + physical_x;
            stride = 1;
        } else {
            physical_x = (int32_t)OLED_PHYSICAL_WIDTH - 1 - x0;
            physical_y = (int32_t)OLED_PHYSICAL_HEIGHT - y1;
            base = physical_y * (int32_t)OLED_PHYSICAL_WIDTH + physical_x;
            stride = (int32_t)OLED_PHYSICAL_WIDTH;
        }
        break;
    case OLED_ROTATION_270:
        if (horizontal) {
            physical_x = y0;
            physical_y = (int32_t)OLED_PHYSICAL_HEIGHT - x1;
            base = physical_y * (int32_t)OLED_PHYSICAL_WIDTH + physical_x;
            stride = (int32_t)OLED_PHYSICAL_WIDTH;
        } else {
            physical_x = y0;
            physical_y = (int32_t)OLED_PHYSICAL_HEIGHT - 1 - x0;
            base = physical_y * (int32_t)OLED_PHYSICAL_WIDTH + physical_x;
            stride = 1;
        }
        break;
    case OLED_ROTATION_0:
    default:
        if (horizontal) {
            physical_x = x0;
            physical_y = y0;
            base = physical_y * (int32_t)OLED_PHYSICAL_WIDTH + physical_x;
            stride = 1;
        } else {
            physical_x = x0;
            physical_y = y0;
            base = physical_y * (int32_t)OLED_PHYSICAL_WIDTH + physical_x;
            stride = (int32_t)OLED_PHYSICAL_WIDTH;
        }
        break;
    }

    oled_write_physical_span(base, stride,
                             (uint16_t)(horizontal ? x1 - x0 : y1 - y0),
                             color, xor_pixels);
}

void OLED_InternalPlotSpan(int32_t x, int32_t y, uint16_t length,
                           bool horizontal)
{
    OLED_InternalPlotSourceSpan(x, y, length, horizontal, true);
}

void OLED_InternalPlotSource(int16_t x, int16_t y, bool source_pixel)
{
    OLED_InternalPlotSourceSpan(x, y, 1U, true, source_pixel);
}

void OLED_InternalPlotColor(int16_t x, int16_t y, uint16_t color)
{
    uint32_t index;

    if (oled_logical_to_index(x, y, &index)) {
        oled_uniform[oled_draw_index] = false;
        oled_mark_pixel(oled_draw_index, index);
        oled_buffers[oled_draw_index][index] = oled_wire_encode(color);
    }
}

void OLED_HOT_ATTR OLED_InternalBlendColor(int16_t x, int16_t y, uint16_t color,
                                           uint8_t alpha)
{
    uint32_t index;

    if (alpha == 0U) {
        return;
    }
    /* GL30 renders in the panel's native orientation. Avoid the generic
     * rotation mapping and mark_pixel division/modulo on every AA pixel;
     * keep the exact generic path for callers that do rotate the canvas. */
    if (oled_rotation == OLED_ROTATION_0) {
        if (x < oled_clip_x0 || x >= oled_clip_x1 ||
            y < oled_clip_y0 || y >= oled_clip_y1) {
            return;
        }
        index = (uint32_t)y * OLED_PHYSICAL_WIDTH + (uint32_t)x;
        oled_uniform[oled_draw_index] = false;
        oled_mark_row(oled_draw_index, (uint32_t)y, x, (int32_t)x + 1);
        if (alpha == UINT8_MAX) {
            oled_buffers[oled_draw_index][index] = oled_wire_encode(color);
        } else {
            oled_buffers[oled_draw_index][index] = oled_wire_encode(
                oled_blend_rgb565(oled_wire_decode(oled_buffers[oled_draw_index][index]),
                                  color, alpha));
        }
        return;
    }
    if (!oled_logical_to_index(x, y, &index)) return;
    oled_uniform[oled_draw_index] = false;
    oled_mark_pixel(oled_draw_index, index);
    if (alpha == UINT8_MAX) {
        oled_buffers[oled_draw_index][index] = oled_wire_encode(color);
    } else {
        oled_buffers[oled_draw_index][index] = oled_wire_encode(
            oled_blend_rgb565(oled_wire_decode(oled_buffers[oled_draw_index][index]),
                              color, alpha));
    }
}

void OLED_InternalPlot(int16_t x, int16_t y)
{
    OLED_InternalPlotSource(x, y, true);
}

uint16_t OLED_InternalGetLogicalWidth(void)
{
    return (oled_rotation == OLED_ROTATION_90 || oled_rotation == OLED_ROTATION_270)
               ? OLED_PHYSICAL_HEIGHT
               : OLED_PHYSICAL_WIDTH;
}

uint16_t OLED_InternalGetLogicalHeight(void)
{
    return (oled_rotation == OLED_ROTATION_90 || oled_rotation == OLED_ROTATION_270)
               ? OLED_PHYSICAL_WIDTH
               : OLED_PHYSICAL_HEIGHT;
}

void OLED_InternalGetClip(int16_t *x0, int16_t *y0, int16_t *x1, int16_t *y1)
{
    *x0 = oled_clip_x0;
    *y0 = oled_clip_y0;
    *x1 = oled_clip_x1;
    *y1 = oled_clip_y1;
}

bool OLED_InternalIntersectClip(int32_t *x0, int32_t *y0,
                                int32_t *x1, int32_t *y1)
{
    if (*x0 < oled_clip_x0) {
        *x0 = oled_clip_x0;
    }
    if (*y0 < oled_clip_y0) {
        *y0 = oled_clip_y0;
    }
    if (*x1 > oled_clip_x1) {
        *x1 = oled_clip_x1;
    }
    if (*y1 > oled_clip_y1) {
        *y1 = oled_clip_y1;
    }
    return *x0 < *x1 && *y0 < *y1;
}

const uint16_t *OLED_InternalGetTransferBuffer(void)
{
    return oled_buffers[oled_transfer_index];
}

void OLED_InternalTransferFinished(OLED_Status status)
{
    if (!oled_async_pending) {
        return;
    }

    /* 无论成功与否，冻结帧都是下一次休眠恢复时可完整重发的稳定图像。 */
    oled_stable_index = oled_transfer_index;
    oled_async_pending = false;
    oled_last_status = status;
    if (status == OLED_OK) {
        oled_force_full = false;
    } else {
        /* 异步失败后屏幕内容不可信，下一帧强制全屏恢复。 */
        oled_force_full = true;
    }
}

#if defined(KK_OLED_TEST)
const uint16_t *OLED_InternalTestGetDrawBuffer(void)
{
    return oled_buffers[oled_draw_index];
}

const uint16_t *OLED_InternalTestGetStableBuffer(void)
{
    return oled_buffers[oled_stable_index];
}

bool OLED_InternalTestIsForceFull(void)
{
    return oled_force_full;
}
#endif
