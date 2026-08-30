#include "haptics.h"

#include <math.h>
#include <stddef.h>

#include "board_config.h"

#define GL30_TWO_PI_F 6.28318530717958647692f

static float clampf(float value, float low, float high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

void gl30_haptic_tick_2k(gl30_foc_state_t *state, bool torque_allowed) {
  if (state == NULL || !torque_allowed || !state->observer_initialized) {
    gl30_foc_force_zero(state);
    return;
  }

  const gl30_haptic_command_t *command = &state->active_command;
  const uint32_t flags = command->modeFlags;
  float torque = 0.0f;

  if ((flags & GL30_HAPTIC_DETENT) != 0u && command->detentWidthRad > 1.0e-5f) {
    const float error = remainderf(
        state->theta_unwrapped_rad - command->targetPositionRad,
        command->detentWidthRad);
    torque -= command->detentStrengthNm *
              sinf(GL30_TWO_PI_F * error / command->detentWidthRad);
  }

  if ((flags & GL30_HAPTIC_ENDSTOP) != 0u) {
    if (state->theta_unwrapped_rad < command->endstopMinRad) {
      torque += command->endstopStrengthNm *
                (command->endstopMinRad - state->theta_unwrapped_rad);
    } else if (state->theta_unwrapped_rad > command->endstopMaxRad) {
      torque -= command->endstopStrengthNm *
                (state->theta_unwrapped_rad - command->endstopMaxRad);
    }
  }

  if ((flags & GL30_HAPTIC_POSITION) != 0u) {
    torque += command->endstopStrengthNm *
              (command->targetPositionRad - state->theta_unwrapped_rad);
  }

  if ((flags & GL30_HAPTIC_VELOCITY) != 0u) {
    torque += command->dampingNmPerRadS *
              (command->targetVelocityRadS - state->velocity_rad_s);
  } else {
    torque -= command->dampingNmPerRadS * state->velocity_rad_s;
  }

  if ((flags & GL30_HAPTIC_FRICTION) != 0u) {
    torque -= command->frictionNm * tanhf(state->velocity_rad_s / 0.20f);
  }
  if ((flags & GL30_HAPTIC_INERTIA) != 0u) {
    torque -= command->inertiaKgM2 * state->acceleration_rad_s2;
  }

  if (command->activeSpeedLimitRadS > 0.0f &&
      fabsf(state->velocity_rad_s) > command->activeSpeedLimitRadS) {
    torque = clampf(torque, -GL30_SELF_DRIVE_LIMIT_NM, GL30_SELF_DRIVE_LIMIT_NM);
  }

  state->haptic_torque_nm = clampf(
      torque, -state->command_torque_limit_nm, state->command_torque_limit_nm);
  state->torque_command_nm = state->haptic_torque_nm;
  state->i_d_ref_a = 0.0f;
  state->i_q_ref_a = clampf(
      state->torque_command_nm / GL30_MOTOR_KT_NM_PER_A,
      -GL30_CURRENT_SOFT_LIMIT_A,
      GL30_CURRENT_SOFT_LIMIT_A);
}
