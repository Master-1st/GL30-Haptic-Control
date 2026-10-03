/* TIM2 free-running timebase:
 * - counter clocked at 1 MHz from APB1 timer clock,
 * - 32-bit auto-reload,
 * - software 64-bit extension via TIM2 update interrupt.
 */

#include "timebase.h"

#include <stdint.h>

#include "../config/board_config.h"

#if GL30_BUILD_ONLY

static uint64_t g_timebase_us = 0u;

void gl30_timebase_init(void) {
  g_timebase_us = 0u;
}

uint64_t gl30_timebase_now_us(void) {
  return g_timebase_us;
}

void gl30_timebase_delay_us(uint32_t delay_us) {
  g_timebase_us += (uint64_t)delay_us;
}

void gl30_timebase_advance_us(uint32_t delta_us) {
  g_timebase_us += (uint64_t)delta_us;
}

#else

#include "../cubemx/GL30_AMOLED_V7/Core/Inc/main.h"

static volatile uint32_t g_timebase_hi_us = 0u;

void gl30_timebase_init(void) {
  const uint32_t prescaler = GL30_APB1_TIMER_HZ / GL30_TIMEBASE_HZ - 1u;

  g_timebase_hi_us = 0u;

  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM2);
  LL_TIM_DisableCounter(TIM2);
  LL_TIM_SetPrescaler(TIM2, (uint16_t)prescaler);
  LL_TIM_SetAutoReload(TIM2, 0xFFFFFFFFu);
  LL_TIM_SetCounter(TIM2, 0u);
  LL_TIM_SetCounterMode(TIM2, LL_TIM_COUNTERMODE_UP);
  LL_TIM_EnableIT_UPDATE(TIM2);
  LL_TIM_GenerateEvent_UPDATE(TIM2);

  __DSB();
  LL_TIM_ClearFlag_UPDATE(TIM2);
  __DSB();

  NVIC_SetPriority(TIM2_IRQn, 2u);
  NVIC_EnableIRQ(TIM2_IRQn);
  LL_TIM_EnableCounter(TIM2);
}

uint64_t gl30_timebase_now_us(void) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();

  uint32_t hi = g_timebase_hi_us;
  uint32_t low = LL_TIM_GetCounter(TIM2);
  if (LL_TIM_IsActiveFlag_UPDATE(TIM2) != 0u) {
    /* CNT can wrap after the first read even with CPU IRQs masked. Once
     * UIF is seen, re-read the low word from the new timer epoch. */
    low = LL_TIM_GetCounter(TIM2);
    ++hi;
  }

  if (primask == 0u) {
    __enable_irq();
  }

  return ((uint64_t)hi << 32u) | low;
}

void gl30_timebase_delay_us(uint32_t delay_us) {
  const uint64_t start_us = gl30_timebase_now_us();
  while ((gl30_timebase_now_us() - start_us) < (uint64_t)delay_us) {
    __NOP();
  }
}

void gl30_timebase_advance_us(uint32_t delta_us) {
  (void)delta_us;
}

void gl30_timebase_on_tim2_overflow(void) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  /* Higher-priority readers must not observe cleared UIF with the old high
   * word. This function owns both operations; callers must not clear UIF. */
  if (LL_TIM_IsActiveFlag_UPDATE(TIM2) != 0u) {
    LL_TIM_ClearFlag_UPDATE(TIM2);
    ++g_timebase_hi_us;
  }
  if (primask == 0u) {
    __enable_irq();
  }
}

#endif
