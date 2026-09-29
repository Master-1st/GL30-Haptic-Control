#include "gl30_board.h"

#include <stddef.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_async_memcpy.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_lcd_sh8601.h"
#include "esp_lcd_touch_cst820.h"

#define GL30_LCD_SPI_HOST SPI2_HOST
#define GL30_LCD_CS_GPIO GPIO_NUM_10
#define GL30_LCD_PCLK_GPIO GPIO_NUM_11
#define GL30_LCD_DATA0_GPIO GPIO_NUM_12
#define GL30_LCD_DATA1_GPIO GPIO_NUM_13
#define GL30_LCD_DATA2_GPIO GPIO_NUM_14
#define GL30_LCD_DATA3_GPIO GPIO_NUM_15
#define GL30_LCD_RESET_GPIO GPIO_NUM_8
#define GL30_BOARD_POWER_ENABLE_GPIO GPIO_NUM_18
#define GL30_LCD_PCLK_HZ (80U * 1000U * 1000U)

#define GL30_TOUCH_I2C_PORT I2C_NUM_1
#define GL30_TOUCH_SDA_GPIO GPIO_NUM_47
#define GL30_TOUCH_SCL_GPIO GPIO_NUM_48
#define GL30_TOUCH_RESET_GPIO GPIO_NUM_7
#define GL30_TOUCH_INT_GPIO GPIO_NUM_6
#define GL30_TOUCH_SCL_HZ 400000U

/* Two 64-row internal-RAM strips pipeline AHB-GDMA PSRAM reads with SPI DMA.
 * KK_OLED already stores RGB565 in panel MSB-first wire byte order, so staging never
 * performs per-pixel byte conversion. A strip is reused only after both its
 * SPI transfer and the next asynchronous copy reach a known completion. */
#define GL30_TRANSFER_ROWS 64U
#define GL30_TRANSFER_TIMEOUT_MS 500U
#define GL30_DISPLAY_TASK_STACK 4096U
#define GL30_DISPLAY_TASK_PRIORITY 2U
#define GL30_DISPLAY_TASK_CORE 1

static const char *TAG = "gl30_board";

typedef enum {
    GL30_DISPLAY_WORK_FRAME = 0,
    GL30_DISPLAY_WORK_POWER,
    GL30_DISPLAY_WORK_CONTRAST,
} gl30_display_work_kind;

typedef struct {
    gl30_display_work_kind kind;
    const uint16_t *pixels;
    uint32_t frame_id;
    uint64_t submit_us;
    bool asynchronous;
    bool power_on;
    uint8_t contrast;
} gl30_display_work;

typedef struct {
    bool success;
} gl30_display_control_reply;

typedef struct {
    bool success;
    uint64_t start_us;
    uint64_t final_dma_done_us;
    uint64_t done_us;
    uint32_t copy_us; /* Compatibility alias: copy_wait_us, not CPU memcpy time. */
    uint32_t copy_submit_us;
    uint32_t copy_span_us;
    uint32_t io_submit_us;
    uint32_t wait_us;
    uint32_t bytes_sent;
    uint32_t strip_count;
} gl30_transfer_result;

static esp_lcd_panel_io_handle_t s_lcd_io;
static esp_lcd_panel_handle_t s_lcd_panel;
static esp_lcd_panel_io_handle_t s_touch_io;
static esp_lcd_touch_handle_t s_touch;
static i2c_master_bus_handle_t s_i2c_bus;
static uint8_t *s_transfer_bytes;
static async_memcpy_handle_t s_async_memcpy;
static SemaphoreHandle_t s_color_done;
static StaticSemaphore_t s_color_done_storage;
static SemaphoreHandle_t s_copy_done;
static StaticSemaphore_t s_copy_done_storage;
static QueueHandle_t s_work_queue;
static StaticQueue_t s_work_queue_storage;
static uint8_t s_work_queue_buffer[sizeof(gl30_display_work)];
static QueueHandle_t s_result_queue;
static StaticQueue_t s_result_queue_storage;
static uint8_t s_result_queue_buffer[sizeof(gl30_display_result)];
static SemaphoreHandle_t s_control_mutex;
static StaticSemaphore_t s_control_mutex_storage;
static SemaphoreHandle_t s_control_done;
static StaticSemaphore_t s_control_done_storage;
static TaskHandle_t s_display_worker;
static TaskHandle_t s_ui_owner;
static gl30_display_control_reply s_control_reply;
static bool s_initialized;
static bool s_display_failed;
static bool s_worker_started;
static bool s_frame_busy;
static bool s_control_busy;
static gl30_board_stats s_stats;
static portMUX_TYPE s_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_state_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile int64_t s_color_done_us;
static volatile int64_t s_copy_started_us;
static volatile uint32_t s_copy_span_us;

void gl30_board_get_stats(gl30_board_stats *snapshot)
{
    uint32_t display_stack_free = 0U;

    if (s_display_worker != NULL) {
        display_stack_free = (uint32_t)uxTaskGetStackHighWaterMark(s_display_worker);
    }
    portENTER_CRITICAL(&s_stats_lock);
    *snapshot = s_stats;
    snapshot->display_stack_free = display_stack_free;
    portEXIT_CRITICAL(&s_stats_lock);
}

static const sh8601_lcd_init_cmd_t s_lcd_init_cmds[] = {
    {0xFE, (uint8_t[]) {0x00}, 1, 0},
    {0xC4, (uint8_t[]) {0x80}, 1, 0},
    {0x3A, (uint8_t[]) {0x55}, 1, 0},
    {0x35, (uint8_t[]) {0x00}, 1, 0},
    {0x53, (uint8_t[]) {0x20}, 1, 0},
    {0x51, (uint8_t[]) {0xFF}, 1, 0},
    {0x63, (uint8_t[]) {0xFF}, 1, 0},
    {0x2A, (uint8_t[]) {0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]) {0x00, 0x00, 0x01, 0xD1}, 4, 0},
    {0x11, (uint8_t[]) {0x00}, 0, 100},
    {0x29, (uint8_t[]) {0x00}, 0, 0},
};

static bool IRAM_ATTR gl30_color_done_cb(esp_lcd_panel_io_handle_t panel_io,
                                          esp_lcd_panel_io_event_data_t *edata,
                                          void *user_ctx)
{
    BaseType_t high_task_woken = pdFALSE;

    (void)panel_io;
    (void)edata;
    s_color_done_us = esp_timer_get_time();
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_ctx, &high_task_woken);
    /* ESP-IDF 5.5.1's SPI LCD post callback ignores our bool return. Yield
     * here so each completed strip does not wait for the next 10 ms tick. */
    if (high_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
    return high_task_woken == pdTRUE;
}

static bool IRAM_ATTR gl30_copy_done_cb(async_memcpy_handle_t mcp_hdl,
                                        async_memcpy_event_t *event,
                                        void *user_ctx)
{
    BaseType_t high_task_woken = pdFALSE;

    (void)mcp_hdl;
    (void)event;
    s_copy_span_us = (uint32_t)(esp_timer_get_time() - s_copy_started_us);
    xSemaphoreGiveFromISR((SemaphoreHandle_t)user_ctx, &high_task_woken);
    return high_task_woken == pdTRUE;
}

static bool gl30_wait_for_color_transfer(uint32_t *wait_us,
                                         uint64_t *done_us)
{
    int64_t started_us = esp_timer_get_time();
    bool completed = xSemaphoreTake(s_color_done,
                                    pdMS_TO_TICKS(GL30_TRANSFER_TIMEOUT_MS)) == pdTRUE;
    if (wait_us != NULL) {
        *wait_us += (uint32_t)(esp_timer_get_time() - started_us);
    }
    if (completed && done_us != NULL) {
        *done_us = (uint64_t)s_color_done_us;
    }
    return completed;
}

static bool gl30_start_copy(void *dst, const void *src, size_t bytes,
                            gl30_transfer_result *result)
{
    while (xSemaphoreTake(s_copy_done, 0) == pdTRUE) {
    }
    s_copy_started_us = esp_timer_get_time();
    s_copy_span_us = 0;
    esp_err_t err = esp_async_memcpy(s_async_memcpy, dst, (void *)src, bytes,
                                    gl30_copy_done_cb, s_copy_done);
    result->copy_submit_us += (uint32_t)(esp_timer_get_time() - s_copy_started_us);
    return err == ESP_OK;
}

static bool gl30_wait_for_copy(gl30_transfer_result *result)
{
    int64_t started_us = esp_timer_get_time();
    bool completed = xSemaphoreTake(s_copy_done,
                                    pdMS_TO_TICKS(GL30_TRANSFER_TIMEOUT_MS)) == pdTRUE;
    result->copy_us += (uint32_t)(esp_timer_get_time() - started_us);
    if (completed) result->copy_span_us += s_copy_span_us;
    return completed;
}

static bool gl30_init_board_power(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << GL30_BOARD_POWER_ENABLE_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "power enable GPIO config failed: %s", esp_err_to_name(err));
        return false;
    }
    err = gpio_set_level(GL30_BOARD_POWER_ENABLE_GPIO, 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "power enable GPIO high failed: %s", esp_err_to_name(err));
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "board PWR_EN GPIO%d high", (int)GL30_BOARD_POWER_ENABLE_GPIO);
    return true;
}

static bool gl30_init_display(void)
{
    if (!gl30_init_board_power()) {
        return false;
    }
    const spi_bus_config_t bus_config = SH8601_PANEL_BUS_QSPI_CONFIG(
        GL30_LCD_PCLK_GPIO,
        GL30_LCD_DATA0_GPIO,
        GL30_LCD_DATA1_GPIO,
        GL30_LCD_DATA2_GPIO,
        GL30_LCD_DATA3_GPIO,
        GL30_BOARD_LCD_FRAME_BYTES);
    esp_lcd_panel_io_spi_config_t io_config =
        SH8601_PANEL_IO_QSPI_CONFIG(GL30_LCD_CS_GPIO, NULL, NULL);
    io_config.pclk_hz = GL30_LCD_PCLK_HZ;
    sh8601_vendor_config_t vendor_config = {
        .init_cmds = s_lcd_init_cmds,
        .init_cmds_size = sizeof(s_lcd_init_cmds) / sizeof(s_lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GL30_LCD_RESET_GPIO,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor_config,
    };
    esp_err_t err;

    err = spi_bus_initialize(GL30_LCD_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return false;
    }

    io_config.on_color_trans_done = gl30_color_done_cb;
    io_config.user_ctx = s_color_done;
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)GL30_LCD_SPI_HOST,
                                   &io_config, &s_lcd_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_spi failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_lcd_new_panel_sh8601(s_lcd_io, &panel_config, &s_lcd_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_sh8601 failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_lcd_panel_set_gap(s_lcd_panel, 0x06, 0x00);
    if (err == ESP_OK) {
        err = esp_lcd_panel_reset(s_lcd_panel);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_init(s_lcd_panel);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_on_off(s_lcd_panel, true);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SH8601 panel initialization failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static bool gl30_transfer_frame(const uint16_t *rgb565,
                                gl30_transfer_result *result)
{
    const size_t strip_bytes = GL30_BOARD_LCD_WIDTH * GL30_TRANSFER_ROWS * 2U;
    size_t row = 0U;
    unsigned strip = 0U;
    bool completed = true;

    if (result == NULL || rgb565 == NULL || s_transfer_bytes == NULL ||
        s_async_memcpy == NULL || s_lcd_panel == NULL) {
        return false;
    }
    *result = (gl30_transfer_result){
        .start_us = (uint64_t)esp_timer_get_time(),
    };
    while (xSemaphoreTake(s_color_done, 0) == pdTRUE) {
    }
    while (xSemaphoreTake(s_copy_done, 0) == pdTRUE) {
    }

    /* Prime the first strip. Subsequent PSRAM->SRAM copies run on AHB-GDMA
     * while the previous SRAM strip is being sent by SPI DMA. */
    {
        size_t rows = GL30_TRANSFER_ROWS;
        if (rows > GL30_BOARD_LCD_HEIGHT) rows = GL30_BOARD_LCD_HEIGHT;
        if (!gl30_start_copy(s_transfer_bytes,
                             rgb565,
                             rows * GL30_BOARD_LCD_WIDTH * sizeof(uint16_t), result) ||
            !gl30_wait_for_copy(result)) {
            completed = false;
        }
    }

    while (completed && row < GL30_BOARD_LCD_HEIGHT) {
        size_t rows = GL30_TRANSFER_ROWS;
        size_t next_row;
        unsigned next_strip = strip ^ 1U;
        bool copy_pending = false;
        uint8_t *bytes = s_transfer_bytes + strip * strip_bytes;
        esp_err_t err;
        int64_t stage_us;

        if (rows > GL30_BOARD_LCD_HEIGHT - row) {
            rows = GL30_BOARD_LCD_HEIGHT - row;
        }

        stage_us = esp_timer_get_time();
        err = esp_lcd_panel_draw_bitmap(s_lcd_panel, 0, (int)row,
                                        GL30_BOARD_LCD_WIDTH, (int)(row + rows), bytes);
        result->io_submit_us += (uint32_t)(esp_timer_get_time() - stage_us);
        if (err != ESP_OK) {
            completed = false;
            break;
        }
        result->bytes_sent += (uint32_t)(rows * GL30_BOARD_LCD_WIDTH * 2U);
        ++result->strip_count;

        next_row = row + rows;
        if (next_row < GL30_BOARD_LCD_HEIGHT) {
            size_t next_rows = GL30_TRANSFER_ROWS;
            if (next_rows > GL30_BOARD_LCD_HEIGHT - next_row) {
                next_rows = GL30_BOARD_LCD_HEIGHT - next_row;
            }
            copy_pending = gl30_start_copy(
                s_transfer_bytes + next_strip * strip_bytes,
                rgb565 + next_row * GL30_BOARD_LCD_WIDTH,
                next_rows * GL30_BOARD_LCD_WIDTH * sizeof(uint16_t), result);
            if (!copy_pending) {
                completed = false;
            }
        }

        if (!gl30_wait_for_color_transfer(&result->wait_us,
                                          &result->final_dma_done_us)) {
            completed = false;
        }
        /* Reap the copy even if SPI failed. If either completion is unknown,
         * quarantine the frame and staging memory until a hardware reset. */
        if (copy_pending && !gl30_wait_for_copy(result)) {
            completed = false;
        }
        row = next_row;
        strip = next_strip;
    }

    if (!completed) {
        portENTER_CRITICAL(&s_state_lock);
        s_display_failed = true;
        portEXIT_CRITICAL(&s_state_lock);
    }
    result->success = completed;
    result->done_us = completed && result->final_dma_done_us != 0U
                          ? result->final_dma_done_us
                          : (uint64_t)esp_timer_get_time();
    return completed;
}

static bool gl30_init_touch(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = GL30_TOUCH_I2C_PORT,
        .sda_io_num = GL30_TOUCH_SDA_GPIO,
        .scl_io_num = GL30_TOUCH_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = {
            .enable_internal_pullup = true,
        },
    };
    esp_lcd_panel_io_i2c_config_t io_config =
        ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG();
    const esp_lcd_touch_config_t touch_config = {
        .x_max = GL30_BOARD_LCD_WIDTH,
        .y_max = GL30_BOARD_LCD_HEIGHT,
        .rst_gpio_num = GL30_TOUCH_RESET_GPIO,
        .int_gpio_num = GL30_TOUCH_INT_GPIO,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    esp_err_t err;

    err = i2c_new_master_bus(&bus_config, &s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return false;
    }
    io_config.scl_speed_hz = GL30_TOUCH_SCL_HZ;
    err = esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_config, &s_touch_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch panel IO initialization failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_lcd_touch_new_i2c_cst820(s_touch_io, &touch_config, &s_touch);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CST820 initialization failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

static void gl30_record_frame_stats(uint32_t frame_id,
                                    uint64_t submit_us,
                                    const gl30_transfer_result *transfer)
{
    uint64_t frame_us = 0U;
    uint32_t frame_us32;

    if (transfer == NULL) {
        return;
    }
    if (transfer->done_us >= submit_us) {
        frame_us = transfer->done_us - submit_us;
    }
    frame_us32 = frame_us > UINT32_MAX ? UINT32_MAX : (uint32_t)frame_us;

    portENTER_CRITICAL(&s_stats_lock);
    s_stats.last_frame_id = frame_id;
    s_stats.last_submit_us64 = submit_us;
    s_stats.last_dma_done_us64 = transfer->final_dma_done_us;
    s_stats.last_copy_us = transfer->copy_us;
    s_stats.last_copy_submit_us = transfer->copy_submit_us;
    s_stats.last_copy_span_us = transfer->copy_span_us;
    s_stats.last_submit_us = transfer->io_submit_us;
    s_stats.last_wait_us = transfer->wait_us;
    s_stats.last_frame_us = frame_us32;
    if (frame_us32 > s_stats.max_frame_us) {
        s_stats.max_frame_us = frame_us32;
    }
    if (transfer->success) {
        ++s_stats.frames_sent;
    } else {
        ++s_stats.frame_errors;
    }
    portEXIT_CRITICAL(&s_stats_lock);
}

static void gl30_update_worker_stack(void)
{
    const uint32_t free_words = (uint32_t)uxTaskGetStackHighWaterMark(NULL);

    portENTER_CRITICAL(&s_stats_lock);
    s_stats.display_stack_free = free_words;
    portEXIT_CRITICAL(&s_stats_lock);
}

static void gl30_complete_control(bool success)
{
    s_control_reply.success = success;
    (void)xSemaphoreGive(s_control_done);
}

static void gl30_display_worker(void *arg)
{
    gl30_display_work work;

    (void)arg;
    for (;;) {
        if (xQueueReceive(s_work_queue, &work, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (work.kind == GL30_DISPLAY_WORK_FRAME) {
            gl30_transfer_result transfer = {0};
            const bool success = gl30_transfer_frame(work.pixels, &transfer);

            if (!success) {
                portENTER_CRITICAL(&s_state_lock);
                /* A timeout leaves the DMA buffer ownership unknown. */
                s_display_failed = true;
                portEXIT_CRITICAL(&s_state_lock);
            }
            gl30_record_frame_stats(work.frame_id, work.submit_us, &transfer);
            if (work.asynchronous) {
                gl30_display_result result = {
                    .frame_id = work.frame_id,
                    .success = success,
                    .submit_us = work.submit_us,
                    .start_us = transfer.start_us,
                    .final_dma_done_us = transfer.final_dma_done_us,
                    .done_us = transfer.done_us,
                    .copy_us = transfer.copy_us,
                    .copy_submit_us = transfer.copy_submit_us,
                    .copy_span_us = transfer.copy_span_us,
                    .io_submit_us = transfer.io_submit_us,
                    .wait_us = transfer.wait_us,
                    .bytes_sent = transfer.bytes_sent,
                    .strip_count = transfer.strip_count,
                };
                /* The one in-flight frame owns this queue slot until the UI
                 * consumes the result, so this cannot discard completion data. */
                (void)xQueueSend(s_result_queue, &result, portMAX_DELAY);
                if (s_ui_owner != NULL) {
                    xTaskNotifyGive(s_ui_owner);
                }
            } else {
                gl30_complete_control(success);
            }
        } else if (work.kind == GL30_DISPLAY_WORK_POWER) {
            const bool success = !s_display_failed && s_lcd_panel != NULL &&
                                 esp_lcd_panel_disp_on_off(s_lcd_panel,
                                                           work.power_on) == ESP_OK;
            gl30_complete_control(success);
        } else if (work.kind == GL30_DISPLAY_WORK_CONTRAST) {
            uint32_t lcd_cmd = 0x51U;
            bool success = false;

            if (!s_display_failed && s_lcd_io != NULL) {
                /* SH8601 QSPI command packing follows the official Waveshare BSP. */
                lcd_cmd = (lcd_cmd << 8U) | (0x02U << 24U);
                success = esp_lcd_panel_io_tx_param(s_lcd_io, lcd_cmd,
                                                    &work.contrast, 1) == ESP_OK;
            }
            gl30_complete_control(success);
        }
        gl30_update_worker_stack();
    }
}

static bool gl30_submit_control(gl30_display_work *work)
{
    bool accepted = false;

    if (work == NULL || !s_initialized || !s_worker_started ||
        s_display_failed || s_work_queue == NULL || s_control_mutex == NULL ||
        s_control_done == NULL) {
        return false;
    }
    if (xSemaphoreTake(s_control_mutex, 0) != pdTRUE) {
        return false;
    }
    portENTER_CRITICAL(&s_state_lock);
    if (!s_frame_busy && !s_control_busy && !s_display_failed) {
        s_control_busy = true;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_state_lock);
    if (accepted) {
        (void)xSemaphoreTake(s_control_done, 0);
    }
    if (!accepted || xQueueSend(s_work_queue, work, 0) != pdTRUE) {
        if (accepted) {
            portENTER_CRITICAL(&s_state_lock);
            s_control_busy = false;
            portEXIT_CRITICAL(&s_state_lock);
        }
        (void)xSemaphoreGive(s_control_mutex);
        return false;
    }
    (void)xSemaphoreTake(s_control_done, portMAX_DELAY);
    accepted = s_control_reply.success;
    portENTER_CRITICAL(&s_state_lock);
    s_control_busy = false;
    portEXIT_CRITICAL(&s_state_lock);
    (void)xSemaphoreGive(s_control_mutex);
    return accepted;
}

bool gl30_board_display_init(void)
{
    size_t scratch_bytes = (size_t)GL30_BOARD_LCD_WIDTH * GL30_TRANSFER_ROWS *
                           sizeof(uint16_t);
    BaseType_t task_created;

    if (s_display_failed) {
        return false; /* Includes partial initialization: retry only after reset. */
    }
    if (s_initialized) {
        return true;
    }
    /* Fail closed for any allocation/peripheral-init failure below. Do not
     * retry on top of partially owned SPI/GDMA resources. */
    s_display_failed = true;
    s_color_done = xSemaphoreCreateBinaryStatic(&s_color_done_storage);
    s_copy_done = xSemaphoreCreateBinaryStatic(&s_copy_done_storage);
    if (s_color_done == NULL || s_copy_done == NULL) {
        return false;
    }
    s_work_queue = xQueueCreateStatic(1U, sizeof(gl30_display_work),
                                      s_work_queue_buffer,
                                      &s_work_queue_storage);
    s_result_queue = xQueueCreateStatic(1U, sizeof(gl30_display_result),
                                        s_result_queue_buffer,
                                        &s_result_queue_storage);
    s_control_mutex = xSemaphoreCreateMutexStatic(&s_control_mutex_storage);
    s_control_done = xSemaphoreCreateBinaryStatic(&s_control_done_storage);
    if (s_work_queue == NULL || s_result_queue == NULL ||
        s_control_mutex == NULL || s_control_done == NULL) {
        return false;
    }
    s_transfer_bytes = heap_caps_aligned_alloc(64U, scratch_bytes * 2U, MALLOC_CAP_DMA);
    if (s_transfer_bytes == NULL) {
        ESP_LOGE(TAG, "RGB565 DMA scratch allocation failed");
        return false;
    }
    if (!gl30_init_display() || !gl30_init_touch()) {
        return false;
    }
    /* SPI/LCD must acquire its GDMA resources before the AHB async memcpy
     * engine is installed. Installing async memcpy first can leave LCD API
     * transactions reporting success while the physical panel remains black. */
    async_memcpy_config_t memcpy_config = ASYNC_MEMCPY_DEFAULT_CONFIG();
    memcpy_config.backlog = 2U;
    memcpy_config.dma_burst_size = 64U;
    if (esp_async_memcpy_install_gdma_ahb(&memcpy_config, &s_async_memcpy) != ESP_OK) {
        ESP_LOGE(TAG, "async PSRAM memcpy DMA install failed");
        return false;
    }
    s_ui_owner = xTaskGetCurrentTaskHandle();
    task_created = xTaskCreatePinnedToCore(gl30_display_worker,
                                           "gl30_display",
                                           GL30_DISPLAY_TASK_STACK,
                                           NULL,
                                           GL30_DISPLAY_TASK_PRIORITY,
                                           &s_display_worker,
                                           GL30_DISPLAY_TASK_CORE);
    if (task_created != pdPASS) {
        s_display_failed = true;
        return false;
    }
    uint32_t largest = (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    portENTER_CRITICAL(&s_stats_lock);
    s_stats.internal_dma_largest = largest;
    portEXIT_CRITICAL(&s_stats_lock);
    portENTER_CRITICAL(&s_state_lock);
    s_initialized = true;
    s_worker_started = true;
    s_display_failed = false;
    portEXIT_CRITICAL(&s_state_lock);
    return true;
}

gl30_display_status gl30_board_display_submit(const uint16_t *rgb565,
                                               uint32_t frame_id)
{
    gl30_display_work work = {
        .kind = GL30_DISPLAY_WORK_FRAME,
        .pixels = rgb565,
        .frame_id = frame_id,
        .submit_us = (uint64_t)esp_timer_get_time(),
        .asynchronous = true,
    };
    bool accepted = false;

    if (rgb565 == NULL || s_work_queue == NULL) {
        return GL30_DISPLAY_ERROR;
    }
    portENTER_CRITICAL(&s_state_lock);
    if (!s_initialized || !s_worker_started || s_display_failed) {
        accepted = false;
    } else if (s_frame_busy || s_control_busy) {
        portEXIT_CRITICAL(&s_state_lock);
        return GL30_DISPLAY_BUSY;
    } else {
        s_frame_busy = true;
        accepted = true;
    }
    portEXIT_CRITICAL(&s_state_lock);
    if (!accepted || xQueueSend(s_work_queue, &work, 0) != pdTRUE) {
        if (accepted) {
            portENTER_CRITICAL(&s_state_lock);
            s_frame_busy = false;
            portEXIT_CRITICAL(&s_state_lock);
        }
        return GL30_DISPLAY_ERROR;
    }
    portENTER_CRITICAL(&s_stats_lock);
    ++s_stats.accepted_frames;
    s_stats.last_submit_us64 = work.submit_us;
    portEXIT_CRITICAL(&s_stats_lock);
    return GL30_DISPLAY_OK;
}

bool gl30_board_display_take_result(gl30_display_result *result)
{
    gl30_display_result completed;

    if (result == NULL || s_result_queue == NULL ||
        xQueueReceive(s_result_queue, &completed, 0) != pdTRUE) {
        return false;
    }
    *result = completed;
    portENTER_CRITICAL(&s_state_lock);
    s_frame_busy = false;
    portEXIT_CRITICAL(&s_state_lock);
    return true;
}

bool gl30_board_display_busy(void)
{
    bool busy;

    portENTER_CRITICAL(&s_state_lock);
    /* ERROR is reported through the completion result. BUSY also stays true
     * after a fatal fault so OLED_Init cannot memset possibly DMA-owned RAM. */
    busy = s_frame_busy || s_control_busy || s_display_failed;
    portEXIT_CRITICAL(&s_state_lock);
    return busy;
}

uint32_t gl30_board_display_accepted_frames(void)
{
    uint32_t accepted;

    portENTER_CRITICAL(&s_stats_lock);
    accepted = s_stats.accepted_frames;
    portEXIT_CRITICAL(&s_stats_lock);
    return accepted;
}

bool gl30_board_display_frame(const uint16_t *rgb565)
{
    gl30_display_work work = {
        .kind = GL30_DISPLAY_WORK_FRAME,
        .pixels = rgb565,
        .submit_us = (uint64_t)esp_timer_get_time(),
        .asynchronous = false,
    };

    if (rgb565 == NULL) {
        return false;
    }
    return gl30_submit_control(&work);
}

bool gl30_board_display_power(bool on)
{
    gl30_display_work work = {
        .kind = GL30_DISPLAY_WORK_POWER,
        .power_on = on,
        .submit_us = (uint64_t)esp_timer_get_time(),
    };

    return gl30_submit_control(&work);
}

bool gl30_board_display_contrast(uint8_t value)
{
    gl30_display_work work = {
        .kind = GL30_DISPLAY_WORK_CONTRAST,
        .contrast = value,
        .submit_us = (uint64_t)esp_timer_get_time(),
    };

    return gl30_submit_control(&work);
}

static bool gl30_record_touch(bool valid, int16_t x, int16_t y, bool pressed)
{
    portENTER_CRITICAL(&s_stats_lock);
    ++s_stats.touch_reads;
    if (!valid) {
        ++s_stats.touch_errors;
    }
    if (valid && pressed) {
        if (!s_stats.touch_pressed) {
            ++s_stats.touch_presses;
        }
        s_stats.last_touch_x = x;
        s_stats.last_touch_y = y;
    }
    s_stats.touch_pressed = valid && pressed;
    portEXIT_CRITICAL(&s_stats_lock);
    return valid;
}

bool gl30_board_touch_read(int16_t *x, int16_t *y, bool *pressed)
{
    esp_lcd_touch_point_data_t point = {0};
    uint8_t point_count = 0;

    if (x == NULL || y == NULL || pressed == NULL) {
        return false;
    }
    *x = 0;
    *y = 0;
    *pressed = false;
    if (!s_initialized || s_touch == NULL) {
        return false;
    }
    if (esp_lcd_touch_read_data(s_touch) != ESP_OK) {
        return gl30_record_touch(false, 0, 0, false);
    }
    if (esp_lcd_touch_get_data(s_touch, &point, &point_count, 1) != ESP_OK) {
        return gl30_record_touch(false, 0, 0, false);
    }
    if (point_count == 0U) {
        return gl30_record_touch(true, 0, 0, false);
    }
    if (point.x >= GL30_BOARD_LCD_WIDTH || point.y >= GL30_BOARD_LCD_HEIGHT) {
        return gl30_record_touch(true, 0, 0, false);
    }
    *x = (int16_t)point.x;
    *y = (int16_t)point.y;
    *pressed = true;
    return gl30_record_touch(true, *x, *y, true);
}
