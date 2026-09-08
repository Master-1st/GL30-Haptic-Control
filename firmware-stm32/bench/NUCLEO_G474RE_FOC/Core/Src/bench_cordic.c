#include "bench_cordic.h"
#include "bench_hw.h"
#include "foc.h"
#include "main.h"
#include "stm32g4xx_ll_cordic.h"
#include <math.h>
#include <stddef.h>

#define CORDIC_PI_F 3.14159265358979323846f
/* ST CubeG4 v1.6.3 Examples_LL/CORDIC/CORDIC_CosSin specifies 2^-19
 * residual error at six cycles (4096 Q1.31 LSB). Include float angle/Q1.31
 * conversion and reference rounding. H23 incorrectly required 1e-6.
 * |s^2+c^2-1| <= 2*sqrt(2)*e + 2*e^2, plus float arithmetic error. */
#define CORDIC_MAX_ERROR 2.5e-6f
#define CORDIC_MAX_NORM_ERROR 1.0e-5f
#define CORDIC_CONFIG (LL_CORDIC_FUNCTION_COSINE | LL_CORDIC_PRECISION_6CYCLES | \
    LL_CORDIC_SCALE_0 | LL_CORDIC_NBWRITE_2 | LL_CORDIC_NBREAD_2 | \
    LL_CORDIC_INSIZE_32BITS | LL_CORDIC_OUTSIZE_32BITS)
static bool g_cordic_ready;
static bench_cordic_diag_t g_cordic_diag;

bool gl30_foc_sincos(float angle, float *sine, float *cosine)
{
  if (!g_cordic_ready || sine == NULL || cosine == NULL ||
      !isfinite(angle) || angle < -CORDIC_PI_F || angle > CORDIC_PI_F) {
    return false;
  }
  /* Unexpected configuration or unread data is a hardware/ownership failure.
   * Never drain, reset or silently switch math backends during live control. */
  if (CORDIC->CSR != CORDIC_CONFIG) { g_cordic_ready = false; return false; }
  const float scaled = angle * (2147483648.0f / CORDIC_PI_F);
  int32_t phase;
  if (scaled >= 2147483648.0f) { phase = INT32_MAX; }
  else if (scaled <= -2147483648.0f) { phase = INT32_MIN; }
  else { phase = (int32_t)scaled; }
  LL_CORDIC_WriteData(CORDIC, (uint32_t)phase);
  /* Explicit modulus avoids reliance on reset-state or a previous argument. */
  LL_CORDIC_WriteData(CORDIC, (uint32_t)INT32_MAX);
  unsigned int remaining = 32u;
  while (!LL_CORDIC_IsActiveFlag_RRDY(CORDIC)) {
    if (--remaining == 0u) { g_cordic_ready = false; return false; }
  }
  /* COSINE mode returns cosine first, then sine. Drain both results. */
  *cosine = (float)(int32_t)LL_CORDIC_ReadData(CORDIC) * (1.0f / 2147483648.0f);
  *sine = (float)(int32_t)LL_CORDIC_ReadData(CORDIC) * (1.0f / 2147483648.0f);
  return true;
}

bool bench_cordic_startup_test(void)
{
  g_cordic_ready = false;
  g_cordic_diag = (bench_cordic_diag_t){0};
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_CORDIC);
  LL_AHB1_GRP1_ForceReset(LL_AHB1_GRP1_PERIPH_CORDIC);
  LL_AHB1_GRP1_ReleaseReset(LL_AHB1_GRP1_PERIPH_CORDIC);
  LL_CORDIC_Config(CORDIC, LL_CORDIC_FUNCTION_COSINE,
      LL_CORDIC_PRECISION_6CYCLES, LL_CORDIC_SCALE_0,
      LL_CORDIC_NBWRITE_2, LL_CORDIC_NBREAD_2,
      LL_CORDIC_INSIZE_32BITS, LL_CORDIC_OUTSIZE_32BITS);
  g_cordic_ready = true;
  /* Finite boot-only audit against target libm, including both endpoints.
   * No control timer or motor output is started until every point passes. */
  for (uint32_t i = 0u; i <= 65536u; ++i) {
    const float angle = -CORDIC_PI_F + (float)i * (2.0f * CORDIC_PI_F / 65536.0f);
    float sine = 0.0f, cosine = 0.0f;
    const uint32_t start = DWT->CYCCNT;
    const bool valid = gl30_foc_sincos(angle, &sine, &cosine);
    const uint32_t cycles = DWT->CYCCNT - start;
    if (cycles > g_cordic_diag.max_cycles) { g_cordic_diag.max_cycles = cycles; }
    const float error = fmaxf(fabsf(sine - sinf(angle)), fabsf(cosine - cosf(angle)));
    const float norm = fabsf(sine * sine + cosine * cosine - 1.0f);
    if (error > g_cordic_diag.max_error) { g_cordic_diag.max_error = error; }
    if (norm > g_cordic_diag.max_norm_error) { g_cordic_diag.max_norm_error = norm; }
    g_cordic_diag.samples++;
    if (!valid || !isfinite(error) || !isfinite(norm) || error > CORDIC_MAX_ERROR || norm > CORDIC_MAX_NORM_ERROR) {
      g_cordic_diag.failures++;
      g_cordic_ready = false;
      return false;
    }
    /* Bounded startup audit, bridge asleep; normal runtime ADC-qualified
     * watchdog feeding is unchanged and begins only after hardware init. */
    if ((i & 255u) == 0u) { bench_hw_watchdog_feed(); }
  }
  return true;
}

const bench_cordic_diag_t *bench_cordic_diagnostics(void) { return &g_cordic_diag; }
