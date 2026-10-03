/* Exercise the production console sampler and motor API against a tiny host SDK. */
#include "fake_idf.h"
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "gl30_board.h"
#include "gl30_settings_store.h"
#include "gl30_preferences.h"
#include "gl30_motor.h"
#include "esp_err.h"

static uint64_t fake_maintenance_token, fake_begin_token;
static bool fake_motor_claimed, fake_motor_claim_result;
static bool fake_display_claimed, fake_display_begin_result;
static bool fake_store_available;
static gl30_settings_load_result fake_store_load_result;
static esp_err_t fake_store_write_result;
static unsigned fake_begin_calls, fake_motor_claim_calls, fake_motor_end_calls;
static unsigned fake_display_begin_calls, fake_display_end_calls, fake_store_write_calls;
static bool fake_write_saw_motor_and_display_claims;
static uint64_t fake_store_write_token;

static bool input_stop_test_motor_arm(uint64_t captured_stop_generation);
static uint64_t input_stop_test_motor_maintenance_begin(void);
static bool input_stop_test_motor_maintenance_claim(uint64_t token);
static bool input_stop_test_motor_maintenance_held(uint64_t token);
static void input_stop_test_motor_maintenance_end(uint64_t token);
#define gl30_demo_cancel_input input_stop_test_demo_cancel_input
#define gl30_motor_arm input_stop_test_motor_arm
#define gl30_motor_maintenance_begin input_stop_test_motor_maintenance_begin
#define gl30_motor_maintenance_claim input_stop_test_motor_maintenance_claim
#define gl30_motor_maintenance_held input_stop_test_motor_maintenance_held
#define gl30_motor_maintenance_end input_stop_test_motor_maintenance_end
#include "../../../main/app_main.c"
#undef gl30_motor_maintenance_end
#undef gl30_motor_maintenance_held
#undef gl30_motor_maintenance_claim
#undef gl30_motor_maintenance_begin
#undef gl30_motor_arm
#undef gl30_demo_cancel_input
void gl30_demo_cancel_input(void);
bool gl30_motor_arm(uint64_t captured_stop_generation);
#include "../../../components/gl30_motor/src/gl30_motor.c"

static unsigned checks,ui_notifies;
static unsigned demo_cancel_calls;
static unsigned app_arm_attempts;
static uint64_t app_arm_tokens[4];
static int64_t fake_now_us=1000000;
static uint8_t usb_input[96];
static size_t usb_input_length;
static bool force_overflow_on_queue_create;
static bool run_input_task_once;
static bool inside_input_task;
static jmp_buf input_task_return;
static bool run_app_main_until_second_wait;
static unsigned app_main_wait_count;
static jmp_buf app_main_return;
static uint64_t queued_arm_generations[4];
static unsigned queued_arm_count;
typedef struct {
    UBaseType_t capacity,count;
    size_t item_size;
    bool forced_full;
    bool release_forced_full_once;
    unsigned sends;
    unsigned reset_calls;
    uint8_t items[GL30_INPUT_QUEUE_SIZE][128];
} input_queue;
static input_queue queue;

void input_stop_test_demo_cancel_input(void)
{
    ++demo_cancel_calls;
    gl30_demo_cancel_input();
}

bool input_stop_test_motor_arm(uint64_t captured_stop_generation)
{
    if (app_arm_attempts<sizeof(app_arm_tokens)/sizeof(app_arm_tokens[0]))
        app_arm_tokens[app_arm_attempts]=captured_stop_generation;
    ++app_arm_attempts;
    return gl30_motor_arm(captured_stop_generation);
}

#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr,"input-stop FAIL line %d: %s\n",__LINE__,#condition); \
        return false; \
    } \
} while (0)

static uint64_t input_stop_test_motor_maintenance_begin(void)
{
    ++fake_begin_calls;
    return fake_begin_token;
}

static bool input_stop_test_motor_maintenance_claim(uint64_t token)
{
    ++fake_motor_claim_calls;
    if (fake_motor_claim_result && token != 0u && token == fake_maintenance_token)
        fake_motor_claimed = true;
    return fake_motor_claim_result && token != 0u && token == fake_maintenance_token;
}

static bool input_stop_test_motor_maintenance_held(uint64_t token)
{
    return fake_motor_claimed && token != 0u && token == fake_maintenance_token;
}

static void input_stop_test_motor_maintenance_end(uint64_t token)
{
    if (token != 0u && token == fake_maintenance_token) {
        ++fake_motor_end_calls;
        fake_motor_claimed = false;
    }
}

gl30_settings_load_result gl30_settings_store_boot_load(gl30_preferences *out)
{
    if (out != NULL && fake_store_load_result == GL30_SETTINGS_LOADED)
        (void)gl30_preferences_capture(gl30_demo_state(), out);
    return fake_store_load_result;
}

bool gl30_settings_store_available(void) { return fake_store_available; }

esp_err_t gl30_settings_store_write(const gl30_preferences *preferences, uint64_t token)
{
    (void)preferences;
    ++fake_store_write_calls;
    fake_store_write_token = token;
    fake_write_saw_motor_and_display_claims =
        input_stop_test_motor_maintenance_held(token) && fake_display_claimed;
    return fake_store_write_result;
}

bool gl30_board_display_maintenance_begin(void)
{
    ++fake_display_begin_calls;
    if (fake_display_begin_result) fake_display_claimed = true;
    return fake_display_begin_result;
}

bool gl30_board_display_maintenance_active(void) { return fake_display_claimed; }

void gl30_board_display_maintenance_end(void)
{
    ++fake_display_end_calls;
    fake_display_claimed = false;
}

int64_t esp_timer_get_time(void) { return fake_now_us; }
esp_err_t esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out)
{ (void)args; *out=(void *)1; return ESP_OK; }
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer_handle,uint64_t period)
{ (void)timer_handle; (void)period; return ESP_OK; }
esp_err_t esp_timer_start_once(esp_timer_handle_t timer_handle,uint64_t timeout)
{ (void)timer_handle; (void)timeout; return ESP_OK; }
esp_err_t esp_timer_stop(esp_timer_handle_t timer_handle) { (void)timer_handle; return ESP_OK; }
esp_err_t esp_timer_delete(esp_timer_handle_t timer_handle) { (void)timer_handle; return ESP_OK; }
uint32_t esp_random(void) { return 1u; }

QueueHandle_t xQueueCreate(UBaseType_t length,size_t item_size)
{
    memset(&queue,0,sizeof(queue)); queue.capacity=length; queue.item_size=item_size;
    if (force_overflow_on_queue_create) {
        queue.forced_full=true;
        queue.release_forced_full_once=true;
        force_overflow_on_queue_create=false;
    }
    return &queue;
}
BaseType_t xQueueSend(QueueHandle_t handle,const void *item,TickType_t wait_ticks)
{
    input_queue *target=(input_queue *)handle; (void)wait_ticks;
    if (!target) return pdFALSE;
    ++target->sends;
    if (target->forced_full || target->count>=target->capacity || target->item_size>sizeof(target->items[0])) {
        if (target->forced_full && target->release_forced_full_once) {
            target->forced_full=false;
            target->release_forced_full_once=false;
        }
        return pdFALSE;
    }
    memcpy(target->items[target->count++],item,target->item_size);
    if (target->item_size==sizeof(input_event)) {
        const input_event *event=(const input_event *)item;
        if (event->kind==INPUT_CONSOLE && strcmp(event->value.line,"MOTOR ARM")==0 &&
            queued_arm_count<sizeof(queued_arm_generations)/sizeof(queued_arm_generations[0]))
            queued_arm_generations[queued_arm_count++]=event->arm_generation;
    }
    return pdTRUE;
}
BaseType_t xQueueReceive(QueueHandle_t handle,void *item,TickType_t wait_ticks)
{
    input_queue *target=(input_queue *)handle; (void)wait_ticks;
    if (!target || target->count==0u) return pdFALSE;
    memcpy(item,target->items[0],target->item_size);
    --target->count;
    memmove(target->items[0],target->items[1],target->count*sizeof(target->items[0]));
    return pdTRUE;
}
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t handle)
{ input_queue *target=(input_queue *)handle; return target?target->count:0u; }
BaseType_t xQueueReset(QueueHandle_t handle)
{ input_queue *target=(input_queue *)handle; if(!target)return pdFALSE; target->count=0; ++target->reset_calls; return pdTRUE; }
void vQueueDelete(QueueHandle_t handle) { (void)handle; }

TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)2; }
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry,const char *name,uint32_t stack,
    void *argument,UBaseType_t priority,TaskHandle_t *out,BaseType_t core)
{
    (void)name;(void)stack;(void)priority;(void)core;
    *out=(void *)3;
    if (run_input_task_once && entry==input_task) {
        run_input_task_once=false;
        inside_input_task=true;
        if (setjmp(input_task_return)==0) entry(argument);
        inside_input_task=false;
    }
    return pdPASS;
}
void vTaskDelete(TaskHandle_t handle) { (void)handle; }
void xTaskNotifyGive(TaskHandle_t handle) { if(handle==s_ui_task) ++ui_notifies; }
uint32_t ulTaskNotifyTake(BaseType_t clear,TickType_t ticks)
{
    (void)clear;(void)ticks;
    if (run_app_main_until_second_wait) {
        ++app_main_wait_count;
        if (app_main_wait_count==1u) {
            static const char fresh_arm[]="MOTOR ARM\n";
            memcpy(usb_input,fresh_arm,sizeof(fresh_arm)-1u);
            usb_input_length=sizeof(fresh_arm)-1u;
            console_poll();
            return 1u;
        }
        longjmp(app_main_return,1);
    }
    return 0;
}
TickType_t xTaskGetTickCount(void) { return 0; }
void vTaskDelay(TickType_t ticks) { (void)ticks; }
void xTaskDelayUntil(TickType_t *wake,TickType_t increment)
{
    *wake+=increment;
    if (inside_input_task) longjmp(input_task_return,1);
}
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t handle) { (void)handle;return 0; }

esp_err_t uart_driver_install(uart_port_t uart,int rx,int tx,int count,QueueHandle_t *event_queue_out,int flags)
{ (void)uart;(void)rx;(void)tx;(void)count;(void)event_queue_out;(void)flags;return ESP_OK; }
esp_err_t uart_param_config(uart_port_t uart,const uart_config_t *config)
{ (void)uart;(void)config;return ESP_OK; }
esp_err_t uart_set_pin(uart_port_t uart,int tx,int rx,int rts,int cts)
{ (void)uart;(void)tx;(void)rx;(void)rts;(void)cts;return ESP_OK; }
esp_err_t uart_driver_delete(uart_port_t uart) { (void)uart;return ESP_OK; }
esp_err_t uart_get_buffered_data_len(uart_port_t uart,size_t *length)
{ (void)uart;*length=0;return ESP_OK; }
void uart_flush_input(uart_port_t uart) { (void)uart; }
int uart_read_bytes(uart_port_t uart,void *buffer,uint32_t length,TickType_t wait)
{ (void)uart;(void)buffer;(void)length;(void)wait;return 0; }
esp_err_t uart_wait_tx_done(uart_port_t uart,TickType_t wait)
{ (void)uart;(void)wait;return ESP_OK; }
int uart_tx_chars(uart_port_t uart,const char *buffer,uint32_t length)
{ (void)uart;(void)buffer;return (int)length; }

int usb_serial_jtag_read_bytes(uint8_t *buffer,uint32_t length,TickType_t wait)
{
    size_t take=usb_input_length<length?usb_input_length:length;
    (void)wait; memcpy(buffer,usb_input,take); usb_input_length=0; return (int)take;
}
esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t *config)
{ (void)config;return ESP_OK; }
bool usb_serial_jtag_is_driver_installed(void) { return true; }
void usb_serial_jtag_vfs_use_driver(void) { }
const char *esp_err_to_name(esp_err_t error) { (void)error;return "fake"; }
unsigned heap_caps_get_free_size(unsigned caps) { (void)caps;return 0; }
unsigned heap_caps_get_largest_free_block(unsigned caps) { (void)caps;return 0; }

void gl30_board_get_stats(gl30_board_stats *out) { memset(out,0,sizeof(*out)); }
bool gl30_board_touch_read(int16_t *x,int16_t *y,bool *pressed)
{ *x=0;*y=0;*pressed=false;return false; }
void gl30_display_trace_start(void) { }
void gl30_display_trace_stop(void) { }
void gl30_display_trace_dump(void) { }
bool gl30_display_trace_service(void) { return false; }

static void reset_fixture(void)
{
    memset(&queue,0,sizeof(queue)); queue.capacity=GL30_INPUT_QUEUE_SIZE;
    queue.item_size=sizeof(input_event);
    force_overflow_on_queue_create=false; run_input_task_once=false; inside_input_task=false;
    run_app_main_until_second_wait=false; app_main_wait_count=0u;
    demo_cancel_calls=0u; app_arm_attempts=0u; queued_arm_count=0u;
    s_inputs=(QueueHandle_t)&queue; s_ui_task=(TaskHandle_t)0x22;
    s_input_stats=(input_stats){0};
    s_console_line_length=0; s_console_line_overflow=false; s_console_line[0]='\0';
    s_console_arm_generation=0u; s_input_task=NULL; s_frame_timer=NULL;
    s_frame_deadline_drops=0u; s_last_idle_yield_us=0u;
    usb_input_length=0; ui_notifies=0; fake_now_us=1234000;
    fake_begin_calls=fake_motor_claim_calls=fake_motor_end_calls=0u;
    fake_display_begin_calls=fake_display_end_calls=fake_store_write_calls=0u;
    fake_maintenance_token=UINT64_C(0xAABBCCDD11223344);
    fake_begin_token=0u; fake_motor_claimed=false; fake_motor_claim_result=false;
    fake_display_claimed=false; fake_display_begin_result=false;
    fake_store_available=false; fake_store_load_result=GL30_SETTINGS_MISSING;
    fake_store_write_result=ESP_OK; fake_write_saw_motor_and_display_claims=false;
    fake_store_write_token=0u;
    s_observed_preferences=(gl30_preferences){0}; s_saved_preferences=(gl30_preferences){0};
    s_preferences_load=GL30_SETTINGS_MISSING;
    s_preferences_changed_us=s_preferences_attempt_us=s_preferences_token=0u;
    s_preferences_error=ESP_OK; s_preferences_pending=false; s_preferences_stage=SETTINGS_IDLE;
    arm_gate=(gl30_motor_arm_gate_t){0}; status=(gl30_motor_status){0};
    status.control_ready=true; /* Existing console tests start in a healthy leased session. */
    gl30_esp_link_init(&link); gl30_menu_session_init(&session,17u);
    desired_menu=false; desired_epoch=0; desired_published_us=0; tx_sequence=0;
}

static bool set_console_bytes(const char *text)
{
    size_t length=strlen(text);
    CHECK(length<=sizeof(usb_input));
    memcpy(usb_input,text,length); usb_input_length=length;
    return true;
}

static bool queued_event(unsigned index,input_event *out)
{
    CHECK(index<queue.count);
    memcpy(out,queue.items[index],sizeof(*out));
    return true;
}

static bool preferences_service_fixture(uint64_t changed_at)
{
    reset_fixture();
    CHECK(gl30_demo_init((uint32_t)(changed_at/1000u),0u));
    CHECK(gl30_preferences_capture(gl30_demo_state(),&s_saved_preferences));
    s_observed_preferences=s_saved_preferences;
    gl30_preferences changed=s_saved_preferences;
    changed.volume=(uint8_t)(changed.volume==100u?99u:changed.volume+1u);
    CHECK(gl30_demo_apply_preferences(&changed));
    s_preferences_changed_us=0u;
    s_preferences_attempt_us=0u;
    s_preferences_token=0u;
    s_preferences_error=ESP_OK;
    s_preferences_pending=false;
    s_preferences_stage=SETTINGS_IDLE;
    status.control_ready=true;
    fake_store_available=true;
    fake_begin_token=fake_maintenance_token;
    return true;
}

static bool dirty_preferences_wait_until_stable(uint64_t changed_at)
{
    preferences_service(changed_at);
    CHECK(s_preferences_changed_us==changed_at && s_preferences_pending);
    preferences_service(changed_at+1499999u);
    CHECK(fake_begin_calls==0u && fake_store_write_calls==0u);
    preferences_service(changed_at+1500000u);
    CHECK(fake_begin_calls==1u && s_preferences_stage==SETTINGS_RELEASE);
    CHECK(s_preferences_token==fake_maintenance_token && fake_store_write_calls==0u);
    return true;
}

static bool preferences_noop_debounce_and_absent_partners_do_not_write(void)
{
    reset_fixture();
    CHECK(gl30_demo_init(100u,0u));
    CHECK(gl30_preferences_capture(gl30_demo_state(),&s_saved_preferences));
    s_observed_preferences=s_saved_preferences;
    fake_store_available=true;
    status.control_ready=true;
    preferences_service(100000u);
    preferences_service(2000000u);
    CHECK(fake_begin_calls==0u && fake_store_write_calls==0u && !s_preferences_pending);

    CHECK(preferences_service_fixture(3000000u));
    fake_store_available=false;
    preferences_service(3000000u);
    preferences_service(4500000u);
    CHECK(fake_begin_calls==0u && fake_store_write_calls==0u);

    fake_store_available=true;
    fake_begin_token=0u; /* The motor partner declined the maintenance request. */
    preferences_service(6000000u);
    preferences_service(7500000u);
    CHECK(fake_begin_calls==1u && s_preferences_stage==SETTINGS_IDLE);
    CHECK(fake_store_write_calls==0u && s_preferences_pending);
    CHECK(s_preferences_error==ESP_ERR_INVALID_STATE);
    return true;
}

static bool maintenance_ack_timeout_keeps_preferences_pending(void)
{
    const uint64_t changed_at=10000000u;
    CHECK(preferences_service_fixture(changed_at));
    CHECK(dirty_preferences_wait_until_stable(changed_at));
    const uint64_t attempt=s_preferences_attempt_us;

    preferences_service(attempt+1999999u);
    CHECK(fake_store_write_calls==0u && fake_motor_end_calls==0u);
    preferences_service(attempt+2000000u);
    CHECK(fake_store_write_calls==0u && fake_motor_end_calls==1u);
    CHECK(s_preferences_stage==SETTINGS_RESUME && s_preferences_error==ESP_ERR_TIMEOUT);
    CHECK(s_preferences_pending && !fake_display_claimed && !fake_motor_claimed);
    return true;
}

static bool reach_preferences_release(uint64_t changed_at)
{
    CHECK(preferences_service_fixture(changed_at));
    CHECK(dirty_preferences_wait_until_stable(changed_at));
    status.control_ready=false;
    status.maintenance_ready=true;
    status.maintenance_token=fake_maintenance_token;
    return true;
}

static bool display_drain_and_motor_claim_gate_nvs_write(void)
{
    const uint64_t changed_at=20000000u;
    CHECK(reach_preferences_release(changed_at));
    fake_display_begin_result=false;
    preferences_service(s_preferences_attempt_us+1u);
    CHECK(fake_display_begin_calls==1u && fake_motor_claim_calls==0u);
    CHECK(fake_store_write_calls==0u && s_preferences_stage==SETTINGS_RELEASE);

    fake_display_begin_result=true;
    fake_motor_claim_result=false;
    preferences_service(s_preferences_attempt_us+2u);
    CHECK(fake_display_begin_calls==2u && fake_display_end_calls==1u);
    CHECK(fake_motor_claim_calls==1u && fake_motor_end_calls==1u);
    CHECK(fake_store_write_calls==0u && s_preferences_stage==SETTINGS_RESUME);
    CHECK(s_preferences_pending);
    return true;
}

static bool successful_write_requires_both_claims_and_resume_never_arms(void)
{
    const uint64_t changed_at=30000000u;
    CHECK(reach_preferences_release(changed_at));
    fake_display_begin_result=true;
    fake_motor_claim_result=true;
    preferences_service(s_preferences_attempt_us+1u);

    CHECK(fake_store_write_calls==1u && fake_write_saw_motor_and_display_claims);
    CHECK(fake_store_write_token==fake_maintenance_token);
    CHECK(fake_display_end_calls==1u && fake_motor_end_calls==1u);
    CHECK(s_preferences_stage==SETTINGS_RESUME && !s_preferences_pending);
    CHECK(!arm_gate.arm_request && !session.armed);

    preferences_service(s_preferences_attempt_us+2u);
    CHECK(s_preferences_stage==SETTINGS_RESUME && s_preferences_token==fake_maintenance_token);
    status.maintenance_token=0u;
    status.maintenance_ready=false;
    preferences_service(s_preferences_attempt_us+3u);
    CHECK(s_preferences_stage==SETTINGS_RESUME && s_preferences_token==fake_maintenance_token);
    status.control_ready=true;
    preferences_service(s_preferences_attempt_us+4u);
    CHECK(s_preferences_stage==SETTINGS_IDLE && s_preferences_token==0u);
    CHECK(fake_store_write_calls==1u && !arm_gate.arm_request && !session.armed);
    return true;
}

static bool failed_store_write_retains_dirty_preferences(void)
{
    const uint64_t changed_at=40000000u;
    CHECK(reach_preferences_release(changed_at));
    const gl30_preferences saved_before=s_saved_preferences;
    fake_display_begin_result=true;
    fake_motor_claim_result=true;
    fake_store_write_result=ESP_FAIL;
    preferences_service(s_preferences_attempt_us+1u);

    CHECK(fake_store_write_calls==1u && fake_write_saw_motor_and_display_claims);
    CHECK(s_preferences_error==ESP_FAIL && s_preferences_stage==SETTINGS_RESUME);
    CHECK(s_preferences_pending);
    CHECK(gl30_preferences_equal(&saved_before,&s_saved_preferences));
    CHECK(fake_display_end_calls==1u && fake_motor_end_calls==1u);
    return true;
}

static bool off_fences_queued_arm_and_allows_new_token(void)
{
    reset_fixture();
    gl30_motor_status initial;
    gl30_motor_snapshot(&initial);
    CHECK(initial.stop_generation==0u);
    CHECK(set_console_bytes("MOTOR ARM\nMOTOR OFF\n"));
    console_poll();
    CHECK(queue.count==2u && ui_notifies==2u);

    input_event arm,off;
    CHECK(queued_event(0,&arm) && queued_event(1,&off));
    CHECK(strcmp(arm.value.line,"MOTOR ARM")==0 && arm.arm_generation==0u);
    CHECK(strcmp(off.value.line,"MOTOR OFF")==0 && off.arm_generation==1u);
    CHECK(arm_gate.stop_generation==1u && !arm_gate.arm_request);

    console_handle_line(arm.value.line,arm.now_ms,arm.arm_generation);
    CHECK(!arm_gate.arm_request && arm_gate.stop_generation==1u);
    console_handle_line(off.value.line,off.now_ms,off.arm_generation);
    CHECK(arm_gate.stop_generation==1u && !arm_gate.arm_request);

    console_handle_line("MOTOR ARM",off.now_ms,off.arm_generation);
    CHECK(arm_gate.arm_request && arm_gate.arm_changed && arm_gate.stop_generation==1u);
    return true;
}

static bool split_arm_line_keeps_the_first_byte_generation(void)
{
    reset_fixture();
    CHECK(set_console_bytes("MOTOR A"));
    console_poll();
    CHECK(queue.count == 0u && s_console_line_length == 7u);

    gl30_motor_stop();
    CHECK(arm_gate.stop_generation == 1u);
    CHECK(set_console_bytes("RM\n"));
    console_poll();
    CHECK(queue.count == 1u && ui_notifies == 1u);

    input_event stale_arm;
    CHECK(queued_event(0u, &stale_arm));
    CHECK(strcmp(stale_arm.value.line, "MOTOR ARM") == 0);
    CHECK(stale_arm.arm_generation == 0u);
    console_handle_line(stale_arm.value.line, stale_arm.now_ms, stale_arm.arm_generation);
    CHECK(!arm_gate.arm_request && arm_gate.stop_generation == 1u);

    CHECK(set_console_bytes("MOTOR ARM\n"));
    console_poll();
    input_event current_arm;
    CHECK(queued_event(1u, &current_arm));
    CHECK(current_arm.arm_generation == 1u);
    console_handle_line(current_arm.value.line, current_arm.now_ms, current_arm.arm_generation);
    CHECK(arm_gate.arm_request && arm_gate.stop_generation == 1u);
    return true;
}

static bool full_queue_off_and_ordinary_overflow_stop_immediately(void)
{
    reset_fixture();
    CHECK(gl30_motor_arm(0u));
    queue.forced_full=true;
    CHECK(set_console_bytes("MOTOR OFF\n"));
    console_poll();
    CHECK(queue.count==0u && queue.sends==1u && s_input_stats.drops==1u);
    CHECK(arm_gate.stop_generation>0u && !arm_gate.arm_request && ui_notifies==1u);

    reset_fixture();
    CHECK(gl30_motor_arm(0u));
    queue.forced_full=true;
    input_event ordinary={.kind=INPUT_TOUCH,.now_ms=77u};
    enqueue_input(&ordinary);
    CHECK(queue.count==0u && queue.sends==1u && s_input_stats.drops==1u);
    CHECK(arm_gate.stop_generation==1u && !arm_gate.arm_request && ui_notifies==1u);
    return true;
}

static bool parser_queue_handler_preserve_wide_arm_generation(void)
{
    reset_fixture();
    const uint64_t wide_generation=(uint64_t)UINT32_MAX+UINT64_C(0x12345);
    arm_gate.stop_generation=wide_generation;
    CHECK(set_console_bytes("MOTOR ARM\n"));
    console_poll();
    CHECK(queue.count==1u && queued_arm_count==1u);

    input_event arm;
    CHECK(queued_event(0u,&arm));
    CHECK(arm.arm_generation==wide_generation);
    CHECK(queued_arm_generations[0]==wide_generation);
    console_handle_line(arm.value.line,arm.now_ms,arm.arm_generation);
    CHECK(arm_gate.stop_generation==wide_generation && arm_gate.arm_request);
    return true;
}

static bool actual_ui_pass_cleans_overflow_once_and_accepts_fresh_arm(void)
{
    reset_fixture();
    CHECK(set_console_bytes("MOTOR ARM\n"));
    force_overflow_on_queue_create=true;
    run_input_task_once=true;
    run_app_main_until_second_wait=true;

    if (setjmp(app_main_return)==0) app_main();
    run_app_main_until_second_wait=false;

    CHECK(app_main_wait_count==2u);
    CHECK(s_input_stats.drops==1u);
    CHECK(queued_arm_count==2u);
    CHECK(queued_arm_generations[0]==1u);
    CHECK(queued_arm_generations[1]==2u);
    CHECK(queue.reset_calls==1u && queue.count==0u);
    CHECK(demo_cancel_calls==1u);
    CHECK(app_arm_attempts==1u && app_arm_tokens[0]==2u);
    CHECK(arm_gate.stop_generation==2u && arm_gate.arm_request);
    return true;
}

int main(void)
{
    if (!preferences_noop_debounce_and_absent_partners_do_not_write()) return 1;
    if (!maintenance_ack_timeout_keeps_preferences_pending()) return 1;
    if (!display_drain_and_motor_claim_gate_nvs_write()) return 1;
    if (!successful_write_requires_both_claims_and_resume_never_arms()) return 1;
    if (!failed_store_write_retains_dirty_preferences()) return 1;
    if (!off_fences_queued_arm_and_allows_new_token()) return 1;
    if (!split_arm_line_keeps_the_first_byte_generation()) return 1;
    if (!full_queue_off_and_ordinary_overflow_stop_immediately()) return 1;
    if (!parser_queue_handler_preserve_wide_arm_generation()) return 1;
    if (!actual_ui_pass_cleans_overflow_once_and_accepts_fresh_arm()) return 1;
    printf("input stop production-path: %u checks passed\n",checks);
    return 0;
}
