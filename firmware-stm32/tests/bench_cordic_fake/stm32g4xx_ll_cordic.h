#ifndef BENCH_CORDIC_FAKE_LL_H
#define BENCH_CORDIC_FAKE_LL_H
#include "main.h"
#include <math.h>
/* STM32G474 CMSIS bit positions; only LL transport is simulated. */
#define LL_CORDIC_FUNCTION_COSINE 0u
#define LL_CORDIC_PRECISION_6CYCLES 0x60u
#define LL_CORDIC_SCALE_0 0u
#define LL_CORDIC_NBWRITE_2 (1u << 20u)
#define LL_CORDIC_NBREAD_2 (1u << 19u)
#define LL_CORDIC_INSIZE_32BITS 0u
#define LL_CORDIC_OUTSIZE_32BITS 0u
static void LL_CORDIC_Config(CORDIC_TypeDef *c, uint32_t f, uint32_t p,
  uint32_t s, uint32_t w, uint32_t r, uint32_t i, uint32_t o) { c->CSR = f|p|s|w|r|i|o; }
static void LL_CORDIC_WriteData(CORDIC_TypeDef *c, uint32_t word) {
  (void)c; ++writes;
  if (write_index++ == 0u) { last_phase = (int32_t)word; }
  else { modulus = (int32_t)word; polls = 0u; read_index = 0u; }
}
static uint32_t LL_CORDIC_IsActiveFlag_RRDY(CORDIC_TypeDef *c) {
  ++polls; DWT->CYCCNT += 4u;
  if (!never_ready && clock_on && write_index == 2u && polls > ready_after) { c->CSR |= 0x80000000u; }
  return (c->CSR >> 31u);
}
static uint32_t LL_CORDIC_ReadData(CORDIC_TypeDef *c) {
  ++reads;
  const double angle = (double)last_phase * (3.14159265358979323846 / 2147483648.0);
  double value = (read_index == 0u ? cos(angle) : sin(angle)) * (double)modulus;
  value += (double)residual_lsb;
  if (corrupt_result) { value = 0.0; }
  if (++read_index == 2u) { c->CSR &= 0x7fffffffu; write_index = 0u; }
  if (value >= 2147483647.0) { return (uint32_t)INT32_MAX; }
  if (value <= -2147483648.0) { return (uint32_t)INT32_MIN; }
  return (uint32_t)(int32_t)value;
}
#endif
