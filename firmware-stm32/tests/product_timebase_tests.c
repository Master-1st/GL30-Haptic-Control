/* Exercise the real MCU branch with only LL registers/IRQ masking replaced. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define GL30_BUILD_ONLY 0
#define __MAIN_H
static uint32_t counter, primask;
static bool update_flag, wrap_after_read;
static unsigned unlocked_clear_count;
#define TIM2 2
#define TIM2_IRQn 28
#define LL_APB1_GRP1_PERIPH_TIM2 1
#define LL_TIM_COUNTERMODE_UP 0
#define LL_APB1_GRP1_EnableClock(x) ((void)(x))
#define LL_TIM_DisableCounter(x) ((void)(x))
#define LL_TIM_EnableCounter(x) ((void)(x))
#define LL_TIM_SetPrescaler(x,y) ((void)(x),(void)(y))
#define LL_TIM_SetAutoReload(x,y) ((void)(x),(void)(y))
#define LL_TIM_SetCounterMode(x,y) ((void)(x),(void)(y))
#define LL_TIM_SetCounter(x,y) ((void)(x),counter=(y))
#define LL_TIM_EnableIT_UPDATE(x) ((void)(x))
#define LL_TIM_GenerateEvent_UPDATE(x) ((void)(x),update_flag=true)
#define NVIC_SetPriority(x,y) ((void)(x),(void)(y))
#define NVIC_EnableIRQ(x) ((void)(x))
#define __DSB() ((void)0)
#define __NOP() ((void)0)
static uint32_t __get_PRIMASK(void) { return primask; }
static void __disable_irq(void) { primask=1u; }
static void __enable_irq(void) { primask=0u; }
static uint32_t LL_TIM_GetCounter(int tim) {
  (void)tim;
  const uint32_t sampled=counter;
  if (wrap_after_read) { counter=0u; update_flag=true; wrap_after_read=false; }
  return sampled;
}
static bool LL_TIM_IsActiveFlag_UPDATE(int tim) { (void)tim; return update_flag; }
static void LL_TIM_ClearFlag_UPDATE(int tim) {
  (void)tim;
  if (primask==0u) { unlocked_clear_count++; }
  update_flag=false;
}
#include "../trace/timebase.c"
static unsigned failed, checks;
#define CHECK(x,msg) do { checks++; if (!(x)) { failed++; fprintf(stderr,"[FAIL] %s\n",msg); } } while(0)
static void reset(uint32_t low, uint32_t high, bool pending) {
  counter=low; g_timebase_hi_us=high; update_flag=pending;
  primask=0u; wrap_after_read=false; unlocked_clear_count=0u;
}
int main(void) {
  reset(123u,4u,false);
  CHECK(gl30_timebase_now_us()==((4ull<<32)+123u),"normal time uses software high word");
  CHECK(primask==0u,"reader restores enabled IRQ state");
  primask=1u;
  CHECK(gl30_timebase_now_us()==((4ull<<32)+123u),"reader works inside masked IRQ scope");
  CHECK(primask==1u,"reader preserves incoming masked state");
  reset(7u,4u,true);
  CHECK(gl30_timebase_now_us()==((5ull<<32)+7u),"pending wrap accounts one high word");
  gl30_timebase_on_tim2_overflow();
  CHECK(!update_flag,"overflow handler owns clearing UIF");
  CHECK(g_timebase_hi_us==5u,"overflow handler accounts wrap once");
  CHECK(gl30_timebase_now_us()==((5ull<<32)+7u),"time unchanged after pending IRQ serviced");
  CHECK(unlocked_clear_count==0u,"clear and high-word update cannot be preempted");
  gl30_timebase_on_tim2_overflow();
  CHECK(g_timebase_hi_us==5u,"spurious overflow entry cannot add a wrap");
  reset(UINT32_MAX,4u,false); wrap_after_read=true;
  CHECK(gl30_timebase_now_us()==(5ull<<32),"wrap between CNT and UIF reads does not jump a full period");
  CHECK(gl30_timebase_now_us()==(5ull<<32),"following time never goes backwards after the race");
  reset(3u,9u,true); primask=1u;
  gl30_timebase_on_tim2_overflow();
  CHECK(primask==1u,"overflow handler preserves incoming masked state");
  printf("product timebase: %u checks, %u failed\n",checks,failed);
  return failed ? 1 : 0;
}
