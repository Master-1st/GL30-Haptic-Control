#include "gl30_motor.h"
#include "sdkconfig.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* Waveshare J1.11 U0TXD=GPIO43, J1.12 U0RXD=GPIO44, 3.3 V.
 * USB Serial/JTAG owns logs. Never send text on this binary UART. */
static const uart_port_t port=UART_NUM_0;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t task;
static QueueHandle_t events;
static esp_timer_handle_t timer;
static gl30_esp_link_t link;
static gl30_menu_session session;
static gl30_motor_status status;
static bool desired_menu,arm_request,arm_changed;
static uint32_t desired_epoch;
#ifdef CONFIG_GL30_EXPERIMENTAL_MOTOR_CONTROL
static uint32_t tx_sequence;
#endif
static uint64_t desired_published_us;

void gl30_motor_desire_menu(bool active,uint32_t epoch) {
    uint64_t now=(uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&lock);
    desired_menu=active; desired_epoch=epoch; desired_published_us=now;
    portEXIT_CRITICAL(&lock);
}
void gl30_motor_arm(bool enabled) {
#ifndef CONFIG_GL30_EXPERIMENTAL_MOTOR_CONTROL
    if(enabled) {
        ESP_LOGW("gl30_motor", "MOTOR ARM refused: experimental control is disabled; "
                 "current main STM32 does not emit HAPTIC_STATE (0x06)");
        return;
    }
#endif
    portENTER_CRITICAL(&lock); arm_request=enabled; arm_changed=true; portEXIT_CRITICAL(&lock);
}
void gl30_motor_snapshot(gl30_motor_status *out) {
    portENTER_CRITICAL(&lock); *out=status; portEXIT_CRITICAL(&lock);
}
static void wake(void *arg) { (void)arg; xTaskNotifyGive(task); }
static void run(void *arg) {
    (void)arg;
    uint8_t bytes[256];
#ifdef CONFIG_GL30_EXPERIMENTAL_MOTOR_CONTROL
    uint8_t frame[GL30_FRAME_HEADER_BYTES+GL30_HAPTIC_COMMAND_LEN];
#endif
    uint64_t previous=0;
    gl30_motor_status next={0};
    for(;;) {
        ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
        uint64_t now=(uint64_t)esp_timer_get_time();
        if(previous && now-previous>next.max_poll_us) next.max_poll_us=(uint32_t)(now-previous);
        previous=now;
        bool want_menu,want_arm,change_arm;
        uint32_t epoch;
        uint64_t published_us;
        portENTER_CRITICAL(&lock);
        want_menu=desired_menu; epoch=desired_epoch;
        published_us=desired_published_us;
        want_arm=arm_request; change_arm=arm_changed; arm_changed=false;
        portEXIT_CRITICAL(&lock);
        gl30_menu_session_desire(&session,want_menu,epoch,published_us);
        uart_event_t event;
        while(xQueueReceive(events,&event,0)==pdTRUE) {
            if(event.type==UART_FIFO_OVF || event.type==UART_BUFFER_FULL ||
               event.type==UART_FRAME_ERR || event.type==UART_PARITY_ERR) {
                ++next.rx_errors; uart_flush_input(port);
                gl30_esp_link_init(&link); gl30_menu_session_arm(&session,false);
                change_arm=false;
            }
        }
        /* Bounded drain. An old RX backlog is unsafe as fresh angle evidence. */
        size_t buffered=0;
        uart_get_buffered_data_len(port,&buffered);
        if(buffered>2048) {
            ++next.rx_errors; uart_flush_input(port); gl30_esp_link_init(&link);
            gl30_menu_session_arm(&session,false); change_arm=false;
        } else for(unsigned batch=0;batch<8;batch++) {
            int n=uart_read_bytes(port,bytes,sizeof(bytes),0);
            if(n<=0) break;
            if(gl30_esp_link_feed(&link,bytes,(size_t)n,now)!=0) ++next.rx_errors;
        }
        if(change_arm) gl30_menu_session_arm(&session,want_arm);
        bool send=gl30_menu_session_step(&session,&link,now,&next.sample);
#ifdef CONFIG_GL30_EXPERIMENTAL_MOTOR_CONTROL
        if(send && uart_wait_tx_done(port,0)==ESP_OK) {
            size_t length=0;
            int error=gl30_esp_link_build_haptic_command_frame(&session.command,0,
                tx_sequence++,now,frame,sizeof(frame),&length);
            if(error || uart_tx_chars(port,(const char *)frame,(uint32_t)length)!=(int)length) {
                ++next.tx_errors; gl30_menu_session_arm(&session,false); next.sample.valid=false;
            } else { ++next.tx_frames; gl30_menu_session_sent(&session); }
        }
#else
        /* Default UI builds are receive-only, including the stop-command path.
         * Keeping the TX boundary here prevents future callers from bypassing
         * the explicit opt-in enforced by gl30_motor_arm(). */
        (void)send;
#endif
        next.connected=gl30_esp_link_connected(&link,now);
        next.armed=session.armed;
        next.fast_frames=(uint32_t)link.stats.fast_frames;
        next.haptic_frames=(uint32_t)link.stats.haptic_frames;
        portENTER_CRITICAL(&lock); status=next; portEXIT_CRITICAL(&lock);
    }
}
bool gl30_motor_init(void) {
    uart_config_t config={.baud_rate=5000000,.data_bits=UART_DATA_8_BITS,
        .parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,
        .flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
    if(uart_driver_install(port,8192,0,32,&events,0)!=ESP_OK) return false;
    if(uart_param_config(port,&config)!=ESP_OK ||
       uart_set_pin(port,43,44,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK) {
        uart_driver_delete(port); return false;
    }
    gl30_esp_link_init(&link); gl30_menu_session_init(&session,esp_random());
    if(xTaskCreatePinnedToCore(run,"gl30_motor",4096,NULL,4,&task,1)!=pdPASS) {
        uart_driver_delete(port); return false;
    }
    esp_timer_create_args_t args={.callback=wake,.name="gl30_motor_1k",.skip_unhandled_events=true};
    if(esp_timer_create(&args,&timer)!=ESP_OK) {
        vTaskDelete(task); uart_driver_delete(port); return false;
    }
    if(esp_timer_start_periodic(timer,1000)!=ESP_OK) {
        esp_timer_delete(timer); vTaskDelete(task); uart_driver_delete(port); return false;
    }
    return true;
}
