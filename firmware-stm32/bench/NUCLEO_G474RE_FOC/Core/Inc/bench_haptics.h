#ifndef BENCH_HAPTICS_H
#define BENCH_HAPTICS_H

#include "foc.h"

typedef enum {
  BENCH_HAPTIC_FREE,
  BENCH_HAPTIC_DAMPING,
  BENCH_HAPTIC_DETENT,
  BENCH_HAPTIC_SPRING,
  BENCH_HAPTIC_ENDSTOP,
  BENCH_HAPTIC_FRICTION,
  BENCH_HAPTIC_INERTIA,
  BENCH_HAPTIC_VELOCITY
} bench_haptic_kind_t;

/* Low-current commissioning presets, independent of the gate and UART. */
bool bench_haptic_configure(gl30_foc_state_t *state, bench_haptic_kind_t kind,
                            float origin_rad);
/* Position/velocity are in encoder coordinates. Return electrical Iq in amps.
 * Invalid data returns NaN; the caller must disable output, never apply it. */
float bench_haptic_sample(gl30_foc_state_t *state, float position_rad,
                          float velocity_rad_s, float acceleration_rad_s2,
                          float phase_direction);

#endif
