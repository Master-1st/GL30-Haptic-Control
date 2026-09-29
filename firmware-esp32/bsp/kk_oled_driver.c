#include "kk_oled_driver.h"
#include "kk_oled_internal.h"
#include "gl30_board.h"
#include "gl30_demo.h"
#include "gl30_display_trace.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define TRACE_CAPACITY 4096U /* 60 s captures up to 60 FPS, with margin. */
typedef struct {
    gl30_frame_metrics draw;
    gl30_display_result send;
    uint64_t consumed_us;
} frame_trace;
static frame_trace trace[TRACE_CAPACITY] EXT_RAM_BSS_ATTR;
static frame_trace inflight;
static bool pending,recording,dumping;
static uint32_t next_id,trace_count,trace_overflow,dump_cursor;
static uint64_t trace_start_us,trace_stop_us;

uint64_t gl30_platform_time_us(void) { return (uint64_t)esp_timer_get_time(); }
void gl30_platform_display_poll(void) {
    gl30_display_result result;
    if(!gl30_board_display_take_result(&result)) return;
    bool valid=pending && result.frame_id==inflight.send.frame_id;
    inflight.send=result; inflight.consumed_us=gl30_platform_time_us();
    if(recording && result.submit_us>=trace_start_us) {
        if(trace_count<TRACE_CAPACITY) trace[trace_count++]=inflight;
        else ++trace_overflow;
    }
    pending=false;
    /* Only this UI owner changes the OLED buffer indices. The worker has
     * finished reading PSRAM; failed/unknown DMA keeps the BSP fault latched. */
    OLED_InternalTransferFinished(valid && result.success?OLED_OK:OLED_ERROR);
}
OLED_Status OLED_DriverInit(void) {
    pending=recording=dumping=false; next_id=trace_count=trace_overflow=dump_cursor=0;
    trace_start_us=trace_stop_us=0;
    return gl30_board_display_init()?OLED_OK:OLED_ERROR;
}
OLED_Status OLED_DriverWriteBlocking(void) {
    if(gl30_board_display_busy()) return OLED_BUSY;
    return gl30_board_display_frame(OLED_InternalGetTransferBuffer())?OLED_OK:OLED_ERROR;
}
OLED_Status OLED_DriverWriteIT(void) { return OLED_UNSUPPORTED; }
OLED_Status OLED_DriverWriteDMA(void) {
    if(pending || gl30_board_display_busy()) return OLED_BUSY;
    uint32_t id=next_id+1;
    gl30_display_status result=gl30_board_display_submit(OLED_InternalGetTransferBuffer(),id);
    if(result==GL30_DISPLAY_BUSY) return OLED_BUSY;
    if(result!=GL30_DISPLAY_OK) return OLED_ERROR;
    memset(&inflight,0,sizeof(inflight));
    inflight.send.frame_id=id; next_id=id; pending=true;
    gl30_demo_get_frame_metrics(&inflight.draw);
    return OLED_OK;
}
bool OLED_DriverIsBusy(void) { return pending || gl30_board_display_busy(); }
void OLED_DriverPoll(void) { gl30_platform_display_poll(); }
OLED_Status OLED_DriverSetContrast(uint8_t value) { return gl30_board_display_contrast(value)?OLED_OK:OLED_ERROR; }
OLED_Status OLED_DriverSetPowerSave(bool enable) { return gl30_board_display_power(!enable)?OLED_OK:OLED_ERROR; }
/* The SPI ISR signals the BSP semaphore; these entrypoints are UI-owner only. */
void OLED_DriverHandleMemTxComplete(void) { gl30_platform_display_poll(); }
void OLED_DriverHandleError(void) { gl30_platform_display_poll(); }

void gl30_display_trace_start(void) {
    if(dumping) { printf("GL30_TRACE_ERROR dump_in_progress\n"); fflush(stdout); return; }
    gl30_platform_display_poll();
    trace_count=trace_overflow=0; trace_stop_us=0;
    trace_start_us=gl30_platform_time_us(); recording=true;
    printf("GL30_TRACE_START %" PRIu64 "\n",trace_start_us); fflush(stdout);
}
void gl30_display_trace_stop(void) {
    gl30_platform_display_poll();
    trace_stop_us=gl30_platform_time_us(); recording=false;
    printf("GL30_TRACE_STOP %" PRIu64 "\n",trace_stop_us); fflush(stdout);
}
void gl30_display_trace_dump(void) {
    if(recording) { printf("GL30_TRACE_ERROR stop_before_dump\n"); fflush(stdout); return; }
    printf("GL30_TRACE_HEADER {\"start_us\":%" PRIu64 ",\"stop_us\":%" PRIu64
        ",\"count\":%" PRIu32 ",\"overflow\":%" PRIu32 "}\n",
        trace_start_us,trace_stop_us,trace_count,trace_overflow);
    dump_cursor=0; dumping=true;
}
bool gl30_display_trace_service(void) {
    if(!dumping) return false;
    /* Bound USB diagnostics so the 100 Hz input queue continues to drain. */
    uint32_t end=dump_cursor+4;
    if(end>trace_count) end=trace_count;
    for(;dump_cursor<end;dump_cursor++) {
        const frame_trace *f=&trace[dump_cursor];
        printf("GL30_FRAME {\"id\":%" PRIu32 ",\"raster_id\":%" PRIu32
            ",\"capture_ms\":%" PRIu32 ",\"render_ms\":%" PRIu32
            ",\"begin_us\":%" PRIu64 ",\"draw_begin_us\":%" PRIu64 ",\"draw_end_us\":%" PRIu64
            ",\"submit_us\":%" PRIu64 ",\"start_us\":%" PRIu64 ",\"dma_done_us\":%" PRIu64
            ",\"consumed_us\":%" PRIu64 ",\"clear_us\":%" PRIu32 ",\"compare_us\":%" PRIu32
            ",\"copy_us\":%" PRIu32 ",\"copy_wait_us\":%" PRIu32
            ",\"copy_submit_us\":%" PRIu32 ",\"copy_span_us\":%" PRIu32
            ",\"io_submit_us\":%" PRIu32 ",\"wait_us\":%" PRIu32
            ",\"bytes\":%" PRIu32 ",\"strips\":%" PRIu32 ",\"success\":%s}\n",
            f->send.frame_id,f->draw.frame_id,f->draw.capture_ms,f->draw.render_ms,
            f->draw.begin_us,f->draw.draw_begin_us,f->draw.draw_end_us,f->send.submit_us,
            f->send.start_us,f->send.final_dma_done_us,f->consumed_us,f->draw.clear_us,f->draw.compare_us,
            f->send.copy_us,f->send.copy_us,f->send.copy_submit_us,f->send.copy_span_us,
            f->send.io_submit_us,f->send.wait_us,
            f->send.bytes_sent,f->send.strip_count,f->send.success?"true":"false");
    }
    if(dump_cursor==trace_count) { dumping=false; printf("GL30_TRACE_END\n"); }
    fflush(stdout);
    return true;
}
