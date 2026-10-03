#ifndef GL30_STOP_TEST_FAKE_IDF_H
#define GL30_STOP_TEST_FAKE_IDF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *esp_timer_handle_t;
typedef int esp_err_t;
typedef int uart_port_t;
typedef void (*TaskFunction_t)(void *);
typedef struct { int unused; } portMUX_TYPE;
typedef struct {
    void (*callback)(void *);
    void *arg;
    const char *name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;
typedef struct { int unused; } usb_serial_jtag_driver_config_t;
typedef struct {
    int baud_rate;
    int data_bits;
    int parity;
    int stop_bits;
    int flow_ctrl;
    int source_clk;
} uart_config_t;
typedef enum {
    UART_DATA = 0,
    UART_FIFO_OVF,
    UART_BUFFER_FULL,
    UART_FRAME_ERR,
    UART_PARITY_ERR,
    UART_BREAK,
    UART_DATA_BREAK,
} uart_event_type_t;
typedef struct { uart_event_type_t type; } uart_event_t;

#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERROR_CHECK(expression) do { if ((expression) != ESP_OK) abort(); } while (0)

#define UART_NUM_0 0
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE (-1)

#define MALLOC_CAP_INTERNAL 0x01u
#define MALLOC_CAP_8BIT 0x02u
#define MALLOC_CAP_SPIRAM 0x04u
#define MALLOC_CAP_DMA 0x08u

#define USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT() ((usb_serial_jtag_driver_config_t){0})

#define ESP_LOGI(tag, format, ...) ((void)0)
#define ESP_LOGW(tag, format, ...) ((void)0)
#define ESP_LOGE(tag, format, ...) ((void)0)

int64_t esp_timer_get_time(void);
esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *out);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t timeout_us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
esp_err_t esp_timer_delete(esp_timer_handle_t timer);
uint32_t esp_random(void);

BaseType_t xQueueSend(QueueHandle_t queue, const void *item, TickType_t wait_ticks);
BaseType_t xQueueReceive(QueueHandle_t queue, void *item, TickType_t wait_ticks);
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t queue);
BaseType_t xQueueReset(QueueHandle_t queue);
QueueHandle_t xQueueCreate(UBaseType_t length, size_t item_size);
void vQueueDelete(QueueHandle_t queue);

TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry, const char *name,
    uint32_t stack_size, void *argument, UBaseType_t priority,
    TaskHandle_t *out, BaseType_t core_id);
void vTaskDelete(TaskHandle_t task);
void xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear_count, TickType_t ticks);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(TickType_t ticks);
void xTaskDelayUntil(TickType_t *wake_tick, TickType_t increment);
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);

esp_err_t uart_driver_install(uart_port_t port, int rx_buffer_size, int tx_buffer_size,
    int event_queue_size, QueueHandle_t *event_queue, int interrupt_flags);
esp_err_t uart_param_config(uart_port_t port, const uart_config_t *config);
esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts);
esp_err_t uart_driver_delete(uart_port_t port);
esp_err_t uart_get_buffered_data_len(uart_port_t port, size_t *length);
void uart_flush_input(uart_port_t port);
int uart_read_bytes(uart_port_t port, void *buffer, uint32_t length, TickType_t wait_ticks);
esp_err_t uart_wait_tx_done(uart_port_t port, TickType_t wait_ticks);
int uart_tx_chars(uart_port_t port, const char *buffer, uint32_t length);

int usb_serial_jtag_read_bytes(uint8_t *buffer, uint32_t length, TickType_t wait_ticks);
esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t *config);
bool usb_serial_jtag_is_driver_installed(void);
void usb_serial_jtag_vfs_use_driver(void);
const char *esp_err_to_name(esp_err_t error);
unsigned heap_caps_get_free_size(unsigned capabilities);
unsigned heap_caps_get_largest_free_block(unsigned capabilities);

#endif
