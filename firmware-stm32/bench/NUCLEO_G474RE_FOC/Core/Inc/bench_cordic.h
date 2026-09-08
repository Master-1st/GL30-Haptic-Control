#ifndef BENCH_CORDIC_H
#define BENCH_CORDIC_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint32_t samples, failures, max_cycles;
  float max_error, max_norm_error;
} bench_cordic_diag_t;

/* Boot only, after outputs are parked and DWT starts, before ADC/TIM6 start.
 * After this call the ADC IRQ exclusively owns CORDIC; no DMA or other user. */
bool bench_cordic_startup_test(void);
const bench_cordic_diag_t *bench_cordic_diagnostics(void);
#endif
