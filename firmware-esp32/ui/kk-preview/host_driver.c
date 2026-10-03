#include "kk_oled_driver.h"
#include "kk_oled_internal.h"
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
uint64_t gl30_platform_time_us(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency,counter; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&counter);
    return (uint64_t)(counter.QuadPart/(frequency.QuadPart/1000000.0));
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (uint64_t)ts.tv_sec*1000000+(uint64_t)ts.tv_nsec/1000;
#endif
}
void gl30_platform_display_poll(void) { }
static uint16_t presented[466*466];
static uint32_t frames;
static bool power=true,busy;
static int fail_next,async_mode,poll_completion;
void gl30_host_complete_on_poll(bool fail) { poll_completion=fail?2:1; }
const uint16_t *gl30_host_frame(void) { return presented; }
uint32_t gl30_host_frame_id(void) { return frames; }
bool gl30_host_power(void) { return power; }
void gl30_host_fail_next(int fail) { fail_next=fail; }
void gl30_host_async_mode(int mode) { async_mode=mode; }
static OLED_Status write(void) {
    const uint8_t *wire;
    size_t i;
    if(fail_next) { fail_next=0; return OLED_ERROR; }
    wire=(const uint8_t *)OLED_InternalGetTransferBuffer();
    for(i=0;i<466U*466U;i++) presented[i]=(uint16_t)(((uint16_t)wire[2*i]<<8U)|wire[2*i+1]);
    ++frames; return OLED_OK;
}
OLED_Status OLED_DriverInit(void) {
    memset(presented,0,sizeof(presented)); frames=0; power=true; busy=false;
    fail_next=0; async_mode=2; poll_completion=0; return OLED_OK;
}
OLED_Status OLED_DriverWriteBlocking(void) { return write(); }
static OLED_Status async_write(void) {
    if(!async_mode) return OLED_UNSUPPORTED;
    if(async_mode==2) { OLED_Status result=write(); OLED_InternalTransferFinished(result); return OLED_OK; }
    busy=true; return OLED_OK;
}
OLED_Status OLED_DriverWriteIT(void) { return async_write(); }
OLED_Status OLED_DriverWriteDMA(void) { return async_write(); }
bool OLED_DriverIsBusy(void) { return busy; }
void OLED_DriverPoll(void) {
    if(!busy || !poll_completion) return;
    int result=poll_completion; poll_completion=0;
    if(result==2) OLED_DriverHandleError();
    else OLED_DriverHandleMemTxComplete();
}
OLED_Status OLED_DriverSetContrast(uint8_t value) { (void)value; return OLED_OK; }
OLED_Status OLED_DriverSetPowerSave(bool enable) { power=!enable; return OLED_OK; }
void OLED_DriverHandleMemTxComplete(void) {
    if(!busy) return;
    OLED_Status result=write(); busy=false; OLED_InternalTransferFinished(result);
}
void OLED_DriverHandleError(void) {
    if(!busy) return;
    busy=false; OLED_InternalTransferFinished(OLED_ERROR);
}
