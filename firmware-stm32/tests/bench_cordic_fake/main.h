#ifndef BENCH_CORDIC_FAKE_MAIN_H
#define BENCH_CORDIC_FAKE_MAIN_H
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint32_t CYCCNT; } DWT_TypeDef;
typedef struct { uint32_t CSR; } CORDIC_TypeDef;
static DWT_TypeDef fake_dwt;
static CORDIC_TypeDef fake_cordic;
#define DWT (&fake_dwt)
#define CORDIC (&fake_cordic)
#define LL_AHB1_GRP1_PERIPH_CORDIC 8u
static unsigned writes, reads, polls, reloads;
static int32_t last_phase, modulus, residual_lsb;
static unsigned write_index, read_index, ready_after;
static bool clock_on, never_ready, corrupt_result;
static void LL_AHB1_GRP1_EnableClock(uint32_t mask) { clock_on = mask == 8u; }
static void LL_AHB1_GRP1_ForceReset(uint32_t mask) {
  (void)mask; fake_cordic.CSR = 0u; write_index = read_index = 0u;
}
static void LL_AHB1_GRP1_ReleaseReset(uint32_t mask) { (void)mask; }
#endif
