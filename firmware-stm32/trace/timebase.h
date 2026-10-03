#ifndef GL30_TIMEBASE_H_
#define GL30_TIMEBASE_H_

#include <stdint.h>

void gl30_timebase_init(void);
uint64_t gl30_timebase_now_us(void);
void gl30_timebase_delay_us(uint32_t delay_us);
void gl30_timebase_advance_us(uint32_t delta_us);
#if !GL30_BUILD_ONLY
/* Owns the UIF check/clear and high-word update as one atomic operation. */
void gl30_timebase_on_tim2_overflow(void);
#endif

#endif
