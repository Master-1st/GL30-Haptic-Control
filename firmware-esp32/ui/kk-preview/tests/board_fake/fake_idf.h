
#ifndef GL30_TEST_FAKE_IDF_H
#define GL30_TEST_FAKE_IDF_H
/* Minimal deterministic HAL/RTOS fake. Tests compile the actual gl30_board.c. */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define IRAM_ATTR
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define portYIELD_FROM_ISR() ((void)0)
#define MALLOC_CAP_DMA 1
#define SPI2_HOST 2
#define SPI_DMA_CH_AUTO 0
#define I2C_NUM_1 1
#define I2C_CLK_SRC_DEFAULT 0
#define LCD_RGB_ELEMENT_ORDER_RGB 0
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
typedef int BaseType_t;
typedef unsigned TickType_t;
typedef int portMUX_TYPE;
typedef void *TaskHandle_t;
typedef struct { int count; } StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
typedef struct { size_t item; bool ready; unsigned char data[256]; } StaticQueue_t;
typedef StaticQueue_t *QueueHandle_t;
typedef void *esp_lcd_panel_io_handle_t;
typedef void *esp_lcd_panel_handle_t;
typedef void *esp_lcd_touch_handle_t;
typedef void *i2c_master_bus_handle_t;
typedef intptr_t esp_lcd_spi_bus_handle_t;
typedef struct { int unused; } esp_lcd_panel_io_event_data_t;
typedef struct { int unused; } spi_bus_config_t;
typedef struct { unsigned int pclk_hz; bool (*on_color_trans_done)(esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t *,void *); void *user_ctx; } esp_lcd_panel_io_spi_config_t;
typedef struct { int scl_speed_hz; } esp_lcd_panel_io_i2c_config_t;
typedef struct { int cmd; const void *data; size_t data_bytes; unsigned delay_ms; } co5300_lcd_init_cmd_t;
typedef struct { const co5300_lcd_init_cmd_t *init_cmds; size_t init_cmds_size; struct { int use_qspi_interface; } flags; } co5300_vendor_config_t;
typedef struct { int cmd; const void *data; size_t data_bytes; unsigned delay_ms; } sh8601_lcd_init_cmd_t;
typedef struct { const sh8601_lcd_init_cmd_t *init_cmds; size_t init_cmds_size; struct { int use_qspi_interface; } flags; } sh8601_vendor_config_t;
typedef struct { int reset_gpio_num,rgb_ele_order,bits_per_pixel; void *vendor_config; } esp_lcd_panel_dev_config_t;
typedef struct { int i2c_port,sda_io_num,scl_io_num,clk_source,glitch_ignore_cnt; struct { int enable_internal_pullup; } flags; } i2c_master_bus_config_t;
typedef struct { uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type; } gpio_config_t;
typedef struct { int x_max,y_max,rst_gpio_num,int_gpio_num; struct { int reset,interrupt; } levels; struct { int swap_xy,mirror_x,mirror_y; } flags; } esp_lcd_touch_config_t;
typedef struct { uint16_t x,y; } esp_lcd_touch_point_data_t;
typedef void *async_memcpy_handle_t;
typedef struct { void *data; } async_memcpy_event_t;
typedef bool (*async_memcpy_isr_cb_t)(async_memcpy_handle_t,async_memcpy_event_t *,void *);
typedef struct { unsigned backlog,dma_burst_size,flags; } async_memcpy_config_t;
extern int fake_spi_ready;
extern int fake_async_install_after_spi;
#define ASYNC_MEMCPY_DEFAULT_CONFIG() {8,16,0}
#define CO5300_PANEL_BUS_QSPI_CONFIG(...) {0}
#define CO5300_PANEL_IO_QSPI_CONFIG(...) {0}
#define SH8601_PANEL_BUS_QSPI_CONFIG(...) {0}
#define SH8601_PANEL_IO_QSPI_CONFIG(...) {0}
#define ESP_LCD_TOUCH_IO_I2C_CST820_CONFIG() {0}
static inline void fake_log(const char *tag,...) {(void)tag;}
#define ESP_LOGE(...) fake_log(__VA_ARGS__)
#define ESP_LOGI(...) fake_log(__VA_ARGS__)
static inline const char *esp_err_to_name(int e) {(void)e;return "fake";}
int64_t esp_timer_get_time(void);
BaseType_t xSemaphoreTake(SemaphoreHandle_t,unsigned);
static inline SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *s){s->count=0;return s;}
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *s){s->count=1;return s;}
static inline int xSemaphoreGive(SemaphoreHandle_t s){s->count=1;return pdTRUE;}
static inline int xSemaphoreGiveFromISR(SemaphoreHandle_t s,BaseType_t *w){*w=pdTRUE;return xSemaphoreGive(s);}
static inline QueueHandle_t xQueueCreateStatic(unsigned n,size_t size,uint8_t *buf,StaticQueue_t *q){(void)n;(void)buf;q->item=size;q->ready=false;return q;}
static inline int xQueueSend(QueueHandle_t q,const void *v,unsigned t){(void)t;if(q->ready)return pdFALSE;memcpy(q->data,v,q->item);q->ready=true;return pdTRUE;}
static inline int xQueueReceive(QueueHandle_t q,void *v,unsigned t){(void)t;if(!q->ready)return pdFALSE;memcpy(v,q->data,q->item);q->ready=false;return pdTRUE;}
static inline unsigned uxTaskGetStackHighWaterMark(TaskHandle_t t){(void)t;return 2048;}
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void){return (void *)1;}
static inline void xTaskNotifyGive(TaskHandle_t t){(void)t;}
static inline int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned pri,TaskHandle_t *h,int core){(void)fn;(void)name;(void)stack;(void)arg;(void)pri;(void)core;*h=(void *)1;return pdPASS;}
static inline void vTaskDelay(TickType_t t){(void)t;}
void *heap_caps_aligned_alloc(size_t,size_t,unsigned);
static inline size_t heap_caps_get_largest_free_block(unsigned c){(void)c;return 65536;}
int spi_bus_initialize(int,const spi_bus_config_t *,int);
static inline int esp_lcd_new_panel_io_spi(esp_lcd_spi_bus_handle_t b,const esp_lcd_panel_io_spi_config_t *c,void **o){(void)b;(void)c;*o=(void *)1;return 0;}
static inline int esp_lcd_new_panel_co5300(void *io,const esp_lcd_panel_dev_config_t *c,void **o){(void)io;(void)c;*o=(void *)1;return 0;}
static inline int esp_lcd_new_panel_sh8601(void *io,const esp_lcd_panel_dev_config_t *c,void **o){(void)io;(void)c;*o=(void *)1;return 0;}
static inline int esp_lcd_panel_set_gap(void *p,int x,int y){(void)p;(void)x;(void)y;return 0;}
static inline int esp_lcd_panel_reset(void *p){(void)p;return 0;}
static inline int esp_lcd_panel_init(void *p){(void)p;return 0;}
static inline int esp_lcd_panel_disp_on_off(void *p,bool on){(void)p;(void)on;return 0;}
static inline int esp_lcd_panel_io_tx_param(void *p,int cmd,const void *data,size_t n){(void)p;(void)cmd;(void)data;(void)n;return 0;}
int esp_lcd_panel_draw_bitmap(void *,int,int,int,int,const void *);
static inline int i2c_new_master_bus(const i2c_master_bus_config_t *c,void **o){(void)c;*o=(void *)1;return 0;}
static inline int esp_lcd_new_panel_io_i2c(void *b,const esp_lcd_panel_io_i2c_config_t *c,void **o){(void)b;(void)c;*o=(void *)1;return 0;}
static inline int esp_lcd_touch_new_i2c_cst820(void *io,const esp_lcd_touch_config_t *c,void **o){(void)io;(void)c;*o=(void *)1;return 0;}
static inline int esp_lcd_touch_read_data(void *t){(void)t;return 0;}
static inline int esp_lcd_touch_get_data(void *t,esp_lcd_touch_point_data_t *p,uint8_t *n,int cap){(void)t;(void)p;(void)cap;*n=0;return 0;}
static inline int gpio_config(const gpio_config_t *c){(void)c;return ESP_OK;}
static inline int gpio_set_level(int pin,int level){(void)pin;(void)level;return ESP_OK;}
static inline int esp_async_memcpy_install_gdma_ahb(const async_memcpy_config_t *c,void **h){(void)c;fake_async_install_after_spi=fake_spi_ready;*h=(void *)1;return 0;}
int esp_async_memcpy(async_memcpy_handle_t,void *,void *,size_t,async_memcpy_isr_cb_t,void *);
#define GPIO_NUM_6 6
#define GPIO_NUM_7 7
#define GPIO_NUM_8 8
#define GPIO_NUM_10 10
#define GPIO_NUM_11 11
#define GPIO_NUM_12 12
#define GPIO_NUM_13 13
#define GPIO_NUM_14 14
#define GPIO_NUM_15 15
#define GPIO_NUM_18 18
#define GPIO_NUM_47 47
#define GPIO_NUM_48 48
#endif
