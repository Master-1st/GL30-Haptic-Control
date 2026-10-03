#include <errno.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gl30_board.h"
#include "gl30_demo.h"
#include "gl30_motor.h"
#include "kk_oled.h"
#include "kk_ui_config.h"
#include "gl30_display_trace.h"
#include "gl30_settings_store.h"

#define GL30_CONSOLE_LINE_SIZE 64U
#define GL30_CONSOLE_READ_SIZE 32U
#define GL30_INPUT_QUEUE_SIZE 64U
#define GL30_TIME_MIN_UNIX 946684800ULL
#define GL30_TIME_MAX_UNIX 4102444800ULL
#define GL30_TARGET_FPS 60U
#define GL30_PRESENT_PERIOD_US 16667ULL

static const char *TAG = "gl30_app";
typedef enum { INPUT_TOUCH, INPUT_CONSOLE, INPUT_LINE_OVERFLOW } input_kind;
typedef struct {
    input_kind kind;
    uint32_t now_ms;
    uint64_t arm_generation;
    gl30_menu_sample motor;
    union {
        struct { int16_t x, y; bool pressed, valid; } touch;
        char line[GL30_CONSOLE_LINE_SIZE];
    } value;
} input_event;
typedef struct {
    uint32_t samples, drops, max_poll_us;
} input_stats;

static QueueHandle_t s_inputs;
static TaskHandle_t s_input_task;
static TaskHandle_t s_ui_task;
static esp_timer_handle_t s_frame_timer;
static portMUX_TYPE s_input_stats_lock = portMUX_INITIALIZER_UNLOCKED;
static input_stats s_input_stats;
static uint32_t s_last_loop_us, s_last_tick_us;
static uint32_t s_frame_deadline_drops;
static uint64_t s_last_idle_yield_us;
/* Only the input task owns the parser and its partial line. */
static char s_console_line[GL30_CONSOLE_LINE_SIZE];
static uint8_t s_console_read[GL30_CONSOLE_READ_SIZE];
static size_t s_console_line_length;
static bool s_console_line_overflow;
static uint64_t s_console_arm_generation;
static gl30_preferences s_observed_preferences,s_saved_preferences;
static gl30_settings_load_result s_preferences_load;
static uint64_t s_preferences_changed_us,s_preferences_attempt_us,s_preferences_token;
static esp_err_t s_preferences_error;
static bool s_preferences_pending;
typedef enum { SETTINGS_IDLE,SETTINGS_RELEASE,SETTINGS_RESUME } settings_stage;
static settings_stage s_preferences_stage;

/* UI owner only. A single immutable blob is written while both reservations
 * are held. Input may queue during Flash; generation fences survive that gap. */
static void preferences_service(uint64_t now)
{
    gl30_preferences current;
    if(!gl30_preferences_capture(gl30_demo_state(),&current)) {
        s_preferences_error=ESP_ERR_INVALID_ARG;
        return;
    }
    if(!gl30_preferences_equal(&current,&s_observed_preferences)) {
        s_observed_preferences=current; s_preferences_changed_us=now;
    }
    s_preferences_pending=!gl30_preferences_equal(&current,&s_saved_preferences);
    gl30_motor_status motor;
    gl30_motor_snapshot(&motor);
    if(s_preferences_stage==SETTINGS_RESUME) {
        if(motor.control_ready && motor.maintenance_token==0) {
            s_preferences_token=0; s_preferences_stage=SETTINGS_IDLE;
        }
        return;
    }
    if(s_preferences_stage==SETTINGS_IDLE) {
        if(!s_preferences_pending || !gl30_settings_store_available() ||
           now<s_preferences_changed_us || now-s_preferences_changed_us<1500000u ||
           (s_preferences_attempt_us!=0 && now>=s_preferences_attempt_us &&
            now-s_preferences_attempt_us<5000000u)) return;
        s_preferences_attempt_us=now;
        s_preferences_token=gl30_motor_maintenance_begin();
        if(s_preferences_token==0) {
            s_preferences_error=ESP_ERR_INVALID_STATE;
            return;
        }
        s_preferences_stage=SETTINGS_RELEASE;
        return;
    }
    if(now<s_preferences_attempt_us || now-s_preferences_attempt_us>=2000000u) {
        /* No partner/DMA completion: no Flash, retain pending preferences. */
        gl30_demo_maintenance(false);
        gl30_motor_maintenance_end(s_preferences_token);
        s_preferences_error=ESP_ERR_TIMEOUT;
        s_preferences_stage=SETTINGS_RESUME;
        return;
    }
    if(!motor.maintenance_ready || motor.maintenance_token!=s_preferences_token ||
       now<s_preferences_changed_us || now-s_preferences_changed_us<1500000u) return;
    gl30_demo_maintenance(true);
    OLED_Poll();
    if(!gl30_demo_display_ok()) {
        gl30_demo_maintenance(false);
        gl30_motor_maintenance_end(s_preferences_token);
        s_preferences_error=ESP_FAIL;
        s_preferences_stage=SETTINGS_RESUME;
        return;
    }
    if(!gl30_board_display_maintenance_begin()) return;
    if(!gl30_motor_maintenance_claim(s_preferences_token)) {
        gl30_board_display_maintenance_end();
        gl30_demo_maintenance(false);
        gl30_motor_maintenance_end(s_preferences_token);
        s_preferences_error=ESP_ERR_INVALID_STATE;
    } else {
        const gl30_preferences snapshot=current;
        s_preferences_error=gl30_settings_store_write(&snapshot,s_preferences_token);
        if(s_preferences_error==ESP_OK) {
            s_saved_preferences=snapshot;
            s_preferences_pending=false;
            ESP_LOGI(TAG,"preferences saved; motor remains unarmed");
        } else {
            ESP_LOGE(TAG,"preferences write failed (%s); durable outcome uncertain",
                esp_err_to_name(s_preferences_error));
        }
        gl30_board_display_maintenance_end();
        gl30_demo_maintenance(false);
        gl30_motor_maintenance_end(s_preferences_token);
    }
    s_preferences_stage=SETTINGS_RESUME;
}

static input_stats input_snapshot(void)
{
    input_stats result;
    portENTER_CRITICAL(&s_input_stats_lock);
    result = s_input_stats;
    portEXIT_CRITICAL(&s_input_stats_lock);
    return result;
}

static void enqueue_input(const input_event *event)
{
    if (xQueueSend(s_inputs, event, 0) != pdTRUE) {
        /* Lost input can include a stop or a press/release. Do not wait for
         * the UI consumer to notice the overflow before disarming. */
        gl30_motor_stop();
        portENTER_CRITICAL(&s_input_stats_lock);
        ++s_input_stats.drops;
        portEXIT_CRITICAL(&s_input_stats_lock);
    }
    xTaskNotifyGive(s_ui_task);
}
static void frame_deadline(void *argument)
{
    (void)argument;
    xTaskNotifyGive(s_ui_task);
}

static bool parse_time(const char *line, uint64_t *unix_seconds)
{
    const char *text;
    char *end;
    unsigned long long value;
    if (strncmp(line, "TIME", 4) != 0) return false;
    text = line + 4;
    if (*text != ' ' && *text != '\t') return false;
    while (*text == ' ' || *text == '\t') ++text;
    if (*text < '0' || *text > '9') return false;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno == ERANGE || end == text) return false;
    while (*end == ' ' || *end == '\t') ++end;
    if (*end != '\0' || value < GL30_TIME_MIN_UNIX || value > GL30_TIME_MAX_UNIX)
        return false;
    *unix_seconds = (uint64_t)value;
    return true;
}

/* Runs on the UI task. The sampler never calls UI code or writes console logs. */
static void console_status(void)
{
    gl30_board_stats board;
    gl30_board_get_stats(&board);
    input_stats input = input_snapshot();
    gl30_demo_metrics ui_metrics;
    gl30_demo_get_metrics(&ui_metrics);
    gl30_render_profile render_profile;
    gl30_render_get_profile(&render_profile);
    OLED_Metrics oled;
    OLED_GetMetrics(&oled);
    gl30_motor_status motor;
    gl30_motor_snapshot(&motor);
    printf("GL30_STATUS {\"metric_schema\":2,\"frame_interval_ms\":%u,\"target_fps\":%u,\"frame_period_us\":%u,\"frame_deadline_drops\":%" PRIu32 ",\"uptime_ms\":%" PRId64
           ",\"internal_free\":%u,\"psram_free\":%u,\"stack_free\":%u,"
           "\"frames_sent\":%" PRIu32 ",\"frame_errors\":%" PRIu32
           ",\"last_frame_us\":%" PRIu32 ",\"max_frame_us\":%" PRIu32
           ",\"copy_us\":%" PRIu32 ",\"copy_wait_us\":%" PRIu32
           ",\"copy_submit_us\":%" PRIu32 ",\"copy_span_us\":%" PRIu32
           ",\"submit_us\":%" PRIu32 ",\"wait_us\":%" PRIu32
           ",\"loop_us\":%" PRIu32 ",\"tick_us\":%" PRIu32
           ",\"draw_us\":%" PRIu32 ",\"ui_update_us\":%" PRIu32
           ",\"ui_p95_us\":%" PRIu32 ",\"ui_frames\":%" PRIu32
           ",\"menu_profile_us\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 ",%" PRIu32 "]"
           ",\"motor_connected\":%s,\"motor_armed\":%s,\"motor_menu_valid\":%s"
           ",\"motor_state\":%u,\"motor_fault_bits\":%" PRIu32 ",\"motor_feedback_rx_us\":%" PRIu64
           ",\"motor_rx_fast\":%" PRIu32 ",\"motor_rx_haptic\":%" PRIu32
           ",\"motor_tx\":%" PRIu32 ",\"motor_rx_errors\":%" PRIu32
           ",\"motor_tx_errors\":%" PRIu32 ",\"motor_poll_max_us\":%" PRIu32
           ",\"motor_stop_generation\":%" PRIu64
           ",\"motor_zero_confirmed\":%s,\"motor_zero_command_nonce\":%" PRIu32
           ",\"motor_zero_sent_us\":%" PRIu64 ",\"motor_zero_feedback_us\":%" PRIu64
           ",\"motor_control_ready\":%s,\"motor_lease_generation\":%" PRIu64
           ",\"motor_maintenance_token\":%" PRIu64 ",\"motor_maintenance_ready\":%s"
           ",\"preferences_load\":%d,\"preferences_pending\":%s"
           ",\"preferences_stage\":%d,\"preferences_error\":%d"
           ",\"input_samples\":%" PRIu32 ",\"input_queue_drops\":%" PRIu32
           ",\"input_poll_max_us\":%" PRIu32 ",\"input_stack_free\":%u"
           ",\"touch_reads\":%" PRIu32 ",\"touch_errors\":%" PRIu32
           ",\"touch_presses\":%" PRIu32 ",\"touch_pressed\":%s,"
           "\"last_touch\":[%d,%d],\"clear_us_total\":%" PRIu64
           ",\"compare_us_total\":%" PRIu64 ",\"clear_pixels_total\":%" PRIu64
           ",\"retire_clear_us_total\":%" PRIu64 ",\"retire_clear_pixels_total\":%" PRIu64
           ",\"retire_clear_calls\":%" PRIu32
           ",\"clear_calls\":%" PRIu32 ",\"full_clears\":%" PRIu32
           ",\"identical_frames\":%" PRIu32 ",\"compare_calls\":%" PRIu32
           ",\"skipped_compares\":%" PRIu32 ",\"display_stack_free\":%" PRIu32
           ",\"dma_largest_free\":%u,\"accepted_frames\":%" PRIu32
           ",\"last_frame_id\":%" PRIu32 ",\"ui\":%s}\n",
           (unsigned)KK_UI_FRAME_INTERVAL_MS,(unsigned)GL30_TARGET_FPS,(unsigned)GL30_PRESENT_PERIOD_US,
           s_frame_deadline_drops,esp_timer_get_time() / 1000,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
           (unsigned)uxTaskGetStackHighWaterMark(NULL),
           board.frames_sent, board.frame_errors, board.last_frame_us, board.max_frame_us,
           board.last_copy_us, board.last_copy_us, board.last_copy_submit_us, board.last_copy_span_us, board.last_submit_us, board.last_wait_us,
           s_last_loop_us, s_last_tick_us, ui_metrics.last_draw_us, ui_metrics.last_update_us,
           ui_metrics.p95_update_us, ui_metrics.frames,
           render_profile.menu_layout_us,render_profile.menu_disc_us,render_profile.menu_ring_us,
           render_profile.menu_icon_us,render_profile.menu_label_us,
           motor.connected?"true":"false",motor.armed?"true":"false",motor.sample.valid?"true":"false",
           (unsigned)motor.feedback.state,motor.feedback.fault_bits,motor.feedback.received_us,
           motor.fast_frames,motor.haptic_frames,motor.tx_frames,motor.rx_errors,motor.tx_errors,motor.max_poll_us,
           motor.stop_generation,
           motor.zero_confirmed?"true":"false",motor.zero_command_nonce,
           motor.zero_sent_us,motor.zero_feedback_us,
           motor.control_ready?"true":"false",motor.lease_generation,
           motor.maintenance_token,motor.maintenance_ready?"true":"false",
           (int)s_preferences_load,s_preferences_pending?"true":"false",
           (int)s_preferences_stage,(int)s_preferences_error,
           input.samples, input.drops, input.max_poll_us,
           (unsigned)uxTaskGetStackHighWaterMark(s_input_task),
           board.touch_reads, board.touch_errors, board.touch_presses,
           board.touch_pressed ? "true" : "false", board.last_touch_x, board.last_touch_y,
           oled.clear_us, oled.compare_us, oled.clear_pixels,
           oled.retire_clear_us, oled.retire_clear_pixels, oled.retire_clear_calls,
           oled.clear_calls,
           oled.full_clears, oled.identical_frames,oled.compares,oled.skipped_compares,board.display_stack_free,
           (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
           board.accepted_frames,board.last_frame_id,
           gl30_demo_json());
    fflush(stdout);
}

static void console_handle_line(const char *line, uint32_t received_ms,
                                uint64_t arm_generation)
{
    uint64_t unix_seconds;
    if (strcmp(line, "STATUS") == 0) {
        console_status();
    } else if (strcmp(line,"TRACE START")==0) {
        gl30_display_trace_start();
    } else if (strcmp(line,"TRACE STOP")==0) {
        gl30_display_trace_stop();
    } else if (strcmp(line,"TRACE DUMP")==0) {
        gl30_display_trace_dump();
    } else if (strcmp(line,"MOTOR ARM")==0) {
        if (!gl30_motor_arm(arm_generation))
            ESP_LOGW(TAG, "MOTOR ARM discarded: a newer stop invalidated the command");
    } else if (strcmp(line,"MOTOR OFF")==0) {
        /* The sampler already stopped the motor before enqueueing this line.
         * Repeating it here would invalidate a genuinely newer ARM command. */
    } else if (strcmp(line, "BUTTON 0") == 0 || strcmp(line, "BUTTON 1") == 0) {
        gl30_demo_button(line[7] == '1', received_ms);
    } else if (strncmp(line, "ROTATE ", 7) == 0) {
        char *end;
        errno = 0;
        long steps = strtol(line + 7, &end, 10);
        if (errno == 0 && end != line + 7 && *end == '\0' && steps >= -360 && steps <= 360)
            gl30_demo_rotate((int16_t)steps);
        else
            ESP_LOGW(TAG, "ROTATE requires an integer from -360 to 360");
    } else if (parse_time(line, &unix_seconds)) {
        gl30_demo_set_clock(unix_seconds);
        ESP_LOGI(TAG, "clock set to %" PRIu64, unix_seconds);
    } else {
        ESP_LOGW(TAG, "use STATUS, TIME <unix_seconds>, BUTTON 0/1, ROTATE <steps>, MOTOR ARM/OFF");
    }
}

static void console_poll(void)
{
    int count = usb_serial_jtag_read_bytes(s_console_read, sizeof(s_console_read), 0);
    for (int index = 0; index < count; ++index) {
        uint8_t value = s_console_read[index];
        if (value == '\r' || value == '\n') {
            if (s_console_line_overflow || s_console_line_length != 0U) {
                input_event event = {0};
                event.kind = s_console_line_overflow ? INPUT_LINE_OVERFLOW : INPUT_CONSOLE;
                event.now_ms = (uint32_t)(esp_timer_get_time() / 1000);
                if (event.kind == INPUT_CONSOLE && strcmp(s_console_line,"MOTOR OFF")==0)
                    gl30_motor_stop();
                gl30_motor_status motor;
                gl30_motor_snapshot(&motor); event.motor=motor.sample;
                event.arm_generation=strcmp(s_console_line,"MOTOR ARM")==0 ?
                    s_console_arm_generation : motor.stop_generation;
                memcpy(event.value.line, s_console_line, s_console_line_length);
                enqueue_input(&event);
            }
            s_console_line_length = 0;
            s_console_line_overflow = false;
            s_console_line[0] = '\0';
        } else if (!s_console_line_overflow) {
            if (s_console_line_length + 1U >= sizeof(s_console_line)) {
                s_console_line_overflow = true;
            } else {
                /* Fence a partial ARM line too. A stop before its completion
                 * must not turn old buffered input into a new authorization. */
                if (s_console_line_length==0U) {
                    gl30_motor_status motor;
                    gl30_motor_snapshot(&motor);
                    s_console_arm_generation=motor.stop_generation;
                }
                s_console_line[s_console_line_length++] = (char)value;
                s_console_line[s_console_line_length] = '\0';
            }
        }
    }
}

static void input_task(void *argument)
{
    (void)argument;
    TickType_t wake = xTaskGetTickCount();
    int64_t previous_us = 0;
    for (;;) {
        int64_t started_us = esp_timer_get_time();
        input_event event = {.kind = INPUT_TOUCH, .now_ms = (uint32_t)(started_us / 1000)};
        event.value.touch.valid = gl30_board_touch_read(&event.value.touch.x,
                                                       &event.value.touch.y,
                                                       &event.value.touch.pressed);
        gl30_motor_status motor;
        gl30_motor_snapshot(&motor); event.motor=motor.sample;
        enqueue_input(&event);
        console_poll();
        portENTER_CRITICAL(&s_input_stats_lock);
        ++s_input_stats.samples;
        if (previous_us && started_us - previous_us > s_input_stats.max_poll_us)
            s_input_stats.max_poll_us = (uint32_t)(started_us - previous_us);
        portEXIT_CRITICAL(&s_input_stats_lock);
        previous_us = started_us;
        xTaskDelayUntil(&wake, pdMS_TO_TICKS(10));
    }
}

static bool console_init(void)
{
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t error = usb_serial_jtag_driver_install(&config);
    if (error != ESP_OK && !(error == ESP_ERR_INVALID_STATE && usb_serial_jtag_is_driver_installed())) {
        ESP_LOGE(TAG, "USB driver install failed: %s", esp_err_to_name(error));
        return false;
    }
    usb_serial_jtag_vfs_use_driver();
    return true;
}

void app_main(void)
{
    uint32_t last_event_ms = (uint32_t)(esp_timer_get_time() / 1000);
    uint64_t next_frame_us=0;
    const uint64_t frame_period_us=GL30_PRESENT_PERIOD_US;
    uint32_t observed_drops = 0;
    s_ui_task=xTaskGetCurrentTaskHandle();
    gl30_preferences restored;
    s_preferences_load=gl30_settings_store_boot_load(&restored);
    if (!console_init() || !gl30_demo_init(last_event_ms, 0U)) {
        ESP_LOGE(TAG, "display/UI/USB initialization failed");
        return;
    }
    if(s_preferences_load==GL30_SETTINGS_LOADED)
        (void)gl30_demo_apply_preferences(&restored);
    if(!gl30_preferences_capture(gl30_demo_state(),&s_observed_preferences) || !gl30_motor_init()) {
        ESP_LOGE(TAG,"preferences/motor initialization failed"); return;
    }
    s_saved_preferences=s_observed_preferences;
    ESP_LOGI(TAG,"preferences boot load=%d, storage available=%d",
        s_preferences_load,gl30_settings_store_available());
    s_inputs = xQueueCreate(GL30_INPUT_QUEUE_SIZE, sizeof(input_event));
    if (s_inputs == NULL) {
        ESP_LOGE(TAG, "input queue allocation failed");
        return;
    }
    const esp_timer_create_args_t timer_args={.callback=frame_deadline,.name="gl30_frame"};
    ESP_ERROR_CHECK(esp_timer_create(&timer_args,&s_frame_timer));
    if (xTaskCreatePinnedToCore(input_task, "gl30_input", 4096, NULL, 3,
                                &s_input_task, 1) != pdPASS) {
        ESP_LOGE(TAG, "input task creation failed");
        vQueueDelete(s_inputs);
        return;
    }
    ESP_LOGI(TAG, "GL30 ready; input 100Hz; USB: STATUS, TIME, BUTTON 0/1, ROTATE -360..360");
    for (;;) {
        int64_t started_us = esp_timer_get_time();
        input_stats input = input_snapshot();
        if (input.drops != observed_drops) {
            /* Overflow loses gesture state: clear pending input conservatively.
             * A new ARM must be captured after this cleanup to be accepted. */
            gl30_motor_stop();
            gl30_demo_cancel_input();
            xQueueReset(s_inputs);
            observed_drops = input.drops;
            ESP_LOGW(TAG, "input queue overflow; motor stop requested and pending gesture cancelled");
        }
        /* Bound each batch so a producer cannot starve painting. */
        UBaseType_t pending = uxQueueMessagesWaiting(s_inputs);
        for (UBaseType_t index = 0; index < pending; ++index) {
            input_event event;
            if (xQueueReceive(s_inputs, &event, 0) != pdTRUE) break;
            /* Angle snapshot belongs to this input sample. A later drawing
             * completion must not change which item a click selects. */
            gl30_demo_motor_menu(event.motor.epoch,event.motor.session,event.motor.position,
                event.motor.fraction,event.motor.valid,event.now_ms);
            if (event.kind == INPUT_TOUCH) {
                if (event.value.touch.valid)
                    gl30_demo_touch(event.value.touch.x, event.value.touch.y,
                                    event.value.touch.pressed, event.now_ms);
                else
                    gl30_demo_cancel_input();
            } else if (event.kind == INPUT_CONSOLE) {
                console_handle_line(event.value.line, event.now_ms, event.arm_generation);
            } else {
                ESP_LOGW(TAG, "console command too long");
            }
            gl30_demo_sample(event.now_ms);
            const gl30_model *model=gl30_demo_state();
            gl30_motor_desire_menu(model->page==GL30_MENU && !model->off && !model->fault &&
                                  gl30_demo_display_ok(),model->menu_epoch);
        }
        /* Never advance past queued input timestamps: a sample captured during
         * painting must still be accepted on the next pass. */
        /* Read-only presentation can use the newest feedback independently of
         * the captured q/f that determines which item an input selects. */
        gl30_motor_status motor;
        gl30_motor_snapshot(&motor);
        gl30_demo_motor_feedback(motor.feedback.state,motor.feedback.fault_bits);
        int64_t tick_started_us = esp_timer_get_time();
        preferences_service((uint64_t)tick_started_us);
        if(next_frame_us==0) next_frame_us=(uint64_t)tick_started_us;
        /* Completion/spare-ready wakes may flush one immutable prepared frame
         * immediately. This never rasterizes, so presentation can overlap DMA
         * without allowing extra animation frames between 60 Hz deadlines. */
        gl30_demo_service_display((uint32_t)(tick_started_us/1000));
        if(!gl30_demo_display_ok()) gl30_motor_stop();
        bool frame_due=(uint64_t)tick_started_us>=next_frame_us;
        bool frame_blocked=false;
        if(frame_due) {
            uint64_t behind=(uint64_t)tick_started_us-next_frame_us;
            if(behind>=frame_period_us) {
                uint64_t skipped=behind/frame_period_us;
                next_frame_us+=skipped*frame_period_us;
                s_frame_deadline_drops+=(uint32_t)skipped;
            }
            gl30_demo_metrics before_raster,after_raster;
            gl30_demo_get_metrics(&before_raster);
            gl30_demo_render((uint32_t)(tick_started_us/1000));
            gl30_demo_get_metrics(&after_raster);
            bool rastered=after_raster.frames!=before_raster.frames;
            if(rastered || !gl30_demo_animation_active()) {
                /* Consume this 60 Hz slot only after it really produced a
                 * raster (or the page became static). If the frame was merely
                 * blocked by DMA/spare ownership, keep the slot due so a
                 * completion wake can retry it without creating an extra slot. */
                next_frame_us+=frame_period_us;
            } else {
                frame_blocked=true;
            }
            if(!gl30_demo_display_ok()) gl30_motor_stop();
        }
        s_last_tick_us = (uint32_t)(esp_timer_get_time() - tick_started_us);
        s_last_loop_us = (uint32_t)(esp_timer_get_time() - started_us);
        /* Rendering can become continuously runnable when the display
         * pipeline is saturated.  Yield one tick periodically (not once per
         * frame) so IDLE0 services the watchdog without imposing a 1 ms tax
         * on every presentation. Trace formatting also gets an explicit
         * yield because it can hold the main task runnable for many lines. */
        bool trace_work = gl30_display_trace_service();
        uint64_t after_work_us = (uint64_t)esp_timer_get_time();
        if (trace_work || after_work_us - s_last_idle_yield_us >= 100000U) {
            vTaskDelay(1);
            s_last_idle_yield_us = (uint64_t)esp_timer_get_time();
        }
        /* Raster cadence stays on the 16.667 ms absolute grid. DMA/spare wakes
         * are allowed to service a prepared frame, but never create a raster.
         * If ordinary work crosses a deadline by less than one full period,
         * run that due slot immediately instead of incorrectly dropping it. */
        uint64_t now_us=(uint64_t)esp_timer_get_time();
        if(frame_blocked) {
            /* Wait for DMA/spare-ready/input to free ownership, but retain a
             * short bounded fallback so a lost notification cannot stall UI. */
            (void)ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(4));
            continue;
        }
        if(next_frame_us<=now_us) continue;
        (void)esp_timer_stop(s_frame_timer);
        ESP_ERROR_CHECK(esp_timer_start_once(s_frame_timer,next_frame_us-now_us));
        (void)ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
    }
}
