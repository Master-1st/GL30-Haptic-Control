#include "bench_haptics.h"
#include "haptics.h"
#include "board_config.h"
#include <math.h>
#include <stddef.h>

#define BENCH_HAPTIC_MAX_IQ_A 0.10f
#define BENCH_PI_F 3.14159265358979323846f

bool bench_haptic_configure(gl30_foc_state_t *state, bench_haptic_kind_t kind,
                            float origin_rad)
{
  if (state == NULL || !isfinite(origin_rad) ||
      kind < BENCH_HAPTIC_FREE || kind > BENCH_HAPTIC_VELOCITY) {
    return false;
  }
  gl30_haptic_command_t command = {0};
  command.targetPositionRad = origin_rad;
  command.endstopMinRad = origin_rad - BENCH_PI_F / 4.0f;
  command.endstopMaxRad = origin_rad + BENCH_PI_F / 4.0f;
  command.userTorqueLimitNm = BENCH_HAPTIC_MAX_IQ_A * GL30_MOTOR_KT_NM_PER_A;
  switch (kind) {
    case BENCH_HAPTIC_FREE:
      break;
    case BENCH_HAPTIC_DAMPING:
      command.dampingNmPerRadS = 0.0008f;
      break;
    case BENCH_HAPTIC_DETENT:
      command.modeFlags = GL30_HAPTIC_DETENT;
      command.detentWidthRad = BENCH_PI_F / 12.0f;
      command.detentStrengthNm = 0.0025f;
      command.dampingNmPerRadS = 0.0002f;
      break;
    case BENCH_HAPTIC_SPRING:
      command.modeFlags = GL30_HAPTIC_POSITION;
      command.endstopStrengthNm = 0.01f;
      command.dampingNmPerRadS = 0.0002f;
      break;
    case BENCH_HAPTIC_ENDSTOP:
      command.modeFlags = GL30_HAPTIC_ENDSTOP;
      command.endstopStrengthNm = 0.012f;
      command.dampingNmPerRadS = 0.0002f;
      break;
    case BENCH_HAPTIC_FRICTION:
      command.modeFlags = GL30_HAPTIC_FRICTION;
      command.frictionNm = 0.0025f;
      break;
    case BENCH_HAPTIC_INERTIA:
      command.modeFlags = GL30_HAPTIC_INERTIA;
      command.inertiaKgM2 = 0.000005f;
      command.dampingNmPerRadS = 0.0001f;
      break;
    case BENCH_HAPTIC_VELOCITY:
      command.modeFlags = GL30_HAPTIC_VELOCITY;
      command.targetVelocityRadS = 1.0f;
      command.dampingNmPerRadS = 0.001f;
      break;
  }
  gl30_foc_init(state);
  return gl30_foc_apply_command(state, &command);
}

float bench_haptic_sample(gl30_foc_state_t *state, float position_rad,
                          float velocity_rad_s, float acceleration_rad_s2,
                          float phase_direction)
{
  if (state == NULL || !isfinite(position_rad) || !isfinite(velocity_rad_s) ||
      !isfinite(acceleration_rad_s2) ||
      !isfinite(phase_direction) || fabsf(phase_direction) < 1.0f ||
      fabsf(phase_direction) > 1.0f) {
    gl30_foc_force_zero(state);
    return NAN;
  }
  state->theta_unwrapped_rad = position_rad;
  state->velocity_rad_s = velocity_rad_s;
  state->acceleration_rad_s2 = acceleration_rad_s2;
  state->observer_initialized = true;
  gl30_haptic_tick_2k(state, true);
  if (!isfinite(state->i_q_ref_a)) { return NAN; }
  /* A negative calibrated encoder direction also reverses electrical torque.
   * Omitting this factor turns mechanical damping into positive feedback. */
  const float iq = fminf(BENCH_HAPTIC_MAX_IQ_A,
                        fmaxf(-BENCH_HAPTIC_MAX_IQ_A, state->i_q_ref_a));
  return phase_direction * iq;
}
