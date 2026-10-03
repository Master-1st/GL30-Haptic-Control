
/* Fault injection against production BSP code, without touching hardware. */
#include "fake_idf.h"
#include <stdio.h>
static int tests,copy_calls,color_calls,fail_copy_start,fail_copy_wait,fail_color_start,fail_color_wait;
static int heap_calls,spi_fail;
int fake_spi_ready,fake_async_install_after_spi;
static bool copy_active,color_active;
static void *copy_dst,*copy_src;
static const uint8_t *color_src;
static size_t copy_len,color_len,color_offset;
static async_memcpy_isr_cb_t copy_cb;
static void *copy_context;
static uint8_t panel[466*466*2],source[466*466*2],scratch[466*466*4+64];
static int64_t clock_us;
#define CHECK(x) do {++tests;if(!(x)){fprintf(stderr,"BSP FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
#include "../../../components/gl30_board/gl30_board.c"
int64_t esp_timer_get_time(void){return ++clock_us;}
void *heap_caps_aligned_alloc(size_t a,size_t n,unsigned cap){(void)cap;++heap_calls;CHECK(n+64<=sizeof(scratch));return (void *)(((uintptr_t)scratch+a-1)&~(uintptr_t)(a-1));}
int spi_bus_initialize(int host,const spi_bus_config_t *c,int d){(void)host;(void)c;(void)d;fake_spi_ready=spi_fail?0:1;return spi_fail?ESP_FAIL:ESP_OK;}
int esp_async_memcpy(async_memcpy_handle_t h,void *dst,void *src,size_t n,async_memcpy_isr_cb_t cb,void *ctx){
    (void)h;++copy_calls;CHECK(!copy_active);CHECK(!color_active || dst!=(const void *)color_src);
    if(copy_calls==fail_copy_start)return ESP_FAIL;
    copy_dst=dst;copy_src=src;copy_len=n;copy_cb=cb;copy_context=ctx;copy_active=true;return ESP_OK;
}
int esp_lcd_panel_draw_bitmap(void *p,int x0,int y0,int x1,int y1,const void *bytes){
    (void)p;++color_calls;CHECK(!color_active);CHECK(x0==0&&x1==466&&y1<=466);
    CHECK(!copy_active || bytes!=copy_dst);
    if(color_calls==fail_color_start)return ESP_FAIL;
    color_src=bytes;color_len=(size_t)(y1-y0)*466*2;color_offset=(size_t)y0*466*2;color_active=true;return ESP_OK;
}
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem,unsigned ticks){
    if(ticks && sem==s_copy_done && copy_active){
        if(copy_calls==fail_copy_wait)return pdFALSE; /* unknown ownership */
        memcpy(copy_dst,copy_src,copy_len);copy_active=false;
        (void)copy_cb(s_async_memcpy,NULL,copy_context);
    }
    if(ticks && sem==s_color_done && color_active){
        if(color_calls==fail_color_wait)return pdFALSE;
        memcpy(panel+color_offset,color_src,color_len);color_active=false;
        (void)gl30_color_done_cb(s_lcd_io,NULL,sem);
    }
    if(sem->count){sem->count=0;return pdTRUE;}return pdFALSE;
}
static void reset_fixture(void){
    /* A fixture reset models a chip reset, the only legal exit from quarantine. */
    s_initialized=s_display_failed=s_frame_busy=s_control_busy=s_worker_started=false;
    s_maintenance=false;
    s_work_queue_storage.ready=s_result_queue_storage.ready=false;
    s_control_done_storage.count=0;s_control_mutex_storage.count=1;
    copy_active=color_active=false;copy_calls=color_calls=0;
    fail_copy_start=fail_copy_wait=fail_color_start=fail_color_wait=spi_fail=0;
    fake_spi_ready=fake_async_install_after_spi=0;
    memset(panel,0,sizeof(panel));
    CHECK(gl30_board_display_init());
    CHECK(fake_async_install_after_spi==1); /* Regression: LCD/SPI GDMA must be claimed first. */
}
static void test_display_maintenance_requires_drain_and_blocks_writers(void){
    gl30_display_result result={.frame_id=1,.success=true};
    reset_fixture();

    /* A real accepted async frame prevents the claim until its result is drained. */
    CHECK(gl30_board_display_submit((const uint16_t *)(const void *)source,1u)==GL30_DISPLAY_OK);
    CHECK(gl30_board_display_busy());
    CHECK(!gl30_board_display_maintenance_begin());
    CHECK(xQueueSend(s_result_queue,&result,0)==pdTRUE);
    CHECK(gl30_board_display_take_result(&result));
    CHECK(!gl30_board_display_busy());
    s_work_queue_storage.ready=false; /* The fake worker consumed the submitted work. */

    /* A control transaction in progress also prevents maintenance ownership. */
    s_control_busy=true;
    CHECK(!gl30_board_display_maintenance_begin());
    s_control_busy=false;
    CHECK(gl30_board_display_maintenance_begin());
    CHECK(gl30_board_display_maintenance_active());
    CHECK(!gl30_board_display_maintenance_begin());

    const uint32_t accepted_before=gl30_board_display_accepted_frames();
    CHECK(gl30_board_display_submit((const uint16_t *)(const void *)source,2u)==GL30_DISPLAY_BUSY);
    CHECK(!gl30_board_display_frame((const uint16_t *)(const void *)source));
    CHECK(!gl30_board_display_power(false));
    CHECK(!gl30_board_display_contrast(0x40u));
    CHECK(!s_frame_busy&&!s_control_busy&&
          gl30_board_display_accepted_frames()==accepted_before);
    CHECK(!s_work_queue_storage.ready);

    gl30_board_display_maintenance_end();
    CHECK(!gl30_board_display_maintenance_active());
    CHECK(gl30_board_display_submit((const uint16_t *)(const void *)source,3u)==GL30_DISPLAY_OK);
    CHECK(gl30_board_display_busy());
    CHECK(!gl30_board_display_maintenance_begin());
    CHECK(xQueueSend(s_result_queue,&result,0)==pdTRUE);
    CHECK(gl30_board_display_take_result(&result));
    s_work_queue_storage.ready=false;
}
int main(void){
    CHECK(!gl30_board_display_maintenance_begin());
    test_display_maintenance_requires_drain_and_blocks_writers();
    const int expected_strips=(466+GL30_TRANSFER_ROWS-1)/GL30_TRANSFER_ROWS;
    for(unsigned i=0;i<sizeof(source);i++)source[i]=(uint8_t)(i*17+i/932);
    reset_fixture();gl30_transfer_result result;
    CHECK(gl30_transfer_frame((const uint16_t *)(const void *)source,&result));
    CHECK(result.success&&result.bytes_sent==sizeof(source)&&result.strip_count==(uint32_t)expected_strips);
    CHECK(!copy_active&&!color_active&&memcmp(source,panel,sizeof(panel))==0);
    CHECK(result.copy_submit_us>0&&result.copy_span_us>0);
    /* Inject all four failure kinds at every configured strip boundary. */
    for(int kind=0;kind<4;kind++)for(int boundary=1;boundary<=expected_strips;boundary++){
        reset_fixture();
        if(kind==0)fail_copy_start=boundary;
        if(kind==1)fail_copy_wait=boundary;
        if(kind==2)fail_color_start=boundary;
        if(kind==3)fail_color_wait=boundary;
        CHECK(!gl30_transfer_frame((const uint16_t *)(const void *)source,&result));
        CHECK(!result.success&&s_display_failed&&gl30_board_display_busy());
        int alloc_before=heap_calls;CHECK(!gl30_board_display_init()&&heap_calls==alloc_before);
        CHECK(gl30_board_display_submit((const uint16_t *)(const void *)source,100)==GL30_DISPLAY_ERROR);
        /* Consuming an error result must not lift quarantine. */
        gl30_display_result error={.success=false};s_frame_busy=true;
        CHECK(xQueueSend(s_result_queue,&error,0)==pdTRUE);
        CHECK(gl30_board_display_take_result(&error)&&gl30_board_display_busy());
        if(color_active){fail_color_wait=0;(void)xSemaphoreTake(s_color_done,1);}
        if(copy_active){fail_copy_wait=0;(void)xSemaphoreTake(s_copy_done,1);}
        CHECK(gl30_board_display_busy()); /* late success cannot resurrect it */
    }
    CHECK(s_display_failed&&gl30_board_display_busy());
    CHECK(!gl30_board_display_maintenance_begin());
    gl30_board_display_maintenance_end();
    CHECK(s_display_failed&&gl30_board_display_busy());
    /* Partial initialization is terminal, not another allocation on each retry. */
    s_initialized=s_display_failed=false;spi_fail=1;
    CHECK(!gl30_board_display_init());int previous=heap_calls;
    CHECK(!gl30_board_display_init()&&previous==heap_calls);
    printf("BSP production-code fault injection: %d checks; %d failure scenarios passed\n",tests,expected_strips*4);
    return 0;
}
