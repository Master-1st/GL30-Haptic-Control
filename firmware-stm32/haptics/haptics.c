#include "haptics.h"

#include <math.h>
#include <stddef.h>

#include "board_config.h"

#define GL30_DETENT_SNAP_RATIO ((double)0.55f)
#define GL30_DETENT_DEADZONE_RAD 0.017453292519943295
#define GL30_HAPTIC_TRANSITION_TICKS 40u /* 20 ms at the existing 2 kHz rate. */

static float clampf(float value, float low, float high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

/* Behaviour inspired by SmartKnob's logical detents; physical Nm parameters
 * and bounded multi-detent arithmetic are local, not copied voltage/PID gains.
 * No per-crossed-detent loop, heap allocation, or peripheral access. */
static bool detent_torque(gl30_foc_state_t *state, float *torque) {
  const gl30_haptic_command_t *c = &state->active_command;
  const double width = (double)c->detentWidthRad;
  if (!isfinite(width) || width <= 1.0e-5) { return false; }
  const double relative = ((double)state->theta_unwrapped_rad -
                           (double)c->targetPositionRad) / width;
  if (!isfinite(relative)) { return false; }
  double position = state->detent_initialized ? state->detent_position :
                    floor(relative + 0.5);
  if (state->detent_initialized) {
    const double error = relative - position;
    if (error > GL30_DETENT_SNAP_RATIO) {
      position += ceil(error - GL30_DETENT_SNAP_RATIO);
    } else if (error < -GL30_DETENT_SNAP_RATIO) {
      position += floor(error + GL30_DETENT_SNAP_RATIO);
    }
  }
  if ((c->modeFlags & GL30_HAPTIC_ENDSTOP) != 0u) {
    const double lo = ceil(((double)c->endstopMinRad - (double)c->targetPositionRad) / width);
    const double hi = floor(((double)c->endstopMaxRad - (double)c->targetPositionRad) / width);
    if (!isfinite(lo) || !isfinite(hi) || lo > hi || lo < INT32_MIN || hi > INT32_MAX) {
      return false;
    }
    position = fmin(hi, fmax(lo, position));
  }
  if (position < INT32_MIN || position > INT32_MAX) { return false; }
  const double center = (double)c->targetPositionRad + position * width;
  const double error = (double)state->theta_unwrapped_rad - center;
  const double deadzone = fmin(0.1 * width, GL30_DETENT_DEADZONE_RAD);
  const double active_error = error - fmin(deadzone, fmax(-deadzone, error));
  const double fraction = fmin(1.0, fmax(-1.0, active_error / (0.5 * width - deadzone)));
  *torque = (float)(-(double)c->detentStrengthNm * fraction);
  if (!isfinite(center) || !isfinite(*torque) || !isfinite((float)center)) { return false; }
  state->detent_position = (int32_t)position;
  state->detent_center_rad = (float)center;
  state->detent_fraction = (float)(relative - position);
  state->detent_initialized = true;
  return true;
}

void gl30_haptic_tick_2k(gl30_foc_state_t *state, bool torque_allowed) {
  if (state == NULL || !torque_allowed || !state->observer_initialized ||
      !isfinite(state->theta_unwrapped_rad) || !isfinite(state->velocity_rad_s) ||
      !isfinite(state->acceleration_rad_s2)) {
    gl30_foc_force_zero(state);
    return;
  }

  const gl30_haptic_command_t *command = &state->active_command;
  const uint32_t flags = command->modeFlags;
  float torque = 0.0f;

  if ((flags & GL30_HAPTIC_DETENT) != 0u && !detent_torque(state, &torque)) {
    gl30_foc_force_zero(state);
    return;
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

  float effective_limit = state->command_torque_limit_nm;
  if (command->activeSpeedLimitRadS > 0.0f &&
      fabsf(state->velocity_rad_s) > command->activeSpeedLimitRadS) {
    effective_limit = fminf(effective_limit, GL30_SELF_DRIVE_LIMIT_NM);
  }

  if (!isfinite(torque) || !isfinite(state->command_torque_limit_nm) ||
      state->command_torque_limit_nm < 0.0f || !isfinite(state->haptic_transition_from_nm)) {
    gl30_foc_force_zero(state);
    return;
  }
  torque = clampf(torque, -effective_limit, effective_limit);
  if (state->haptic_transition_pending) {
    state->haptic_transition_ticks = GL30_HAPTIC_TRANSITION_TICKS;
    state->haptic_transition_pending = false;
  }
  if (state->haptic_transition_ticks > 0u) {
    const float old_weight = (float)state->haptic_transition_ticks /
                             GL30_HAPTIC_TRANSITION_TICKS;
    torque = state->haptic_transition_from_nm * old_weight + torque * (1.0f - old_weight);
    state->haptic_transition_ticks--;
  }
  /* A reduced user limit or safety stop always takes priority over continuity. */
  state->haptic_torque_nm = clampf(torque, -effective_limit, effective_limit);
  state->torque_command_nm = state->haptic_torque_nm;
  state->i_d_ref_a = 0.0f;
  state->i_q_ref_a = clampf(
      state->torque_command_nm / GL30_MOTOR_KT_NM_PER_A,
      -GL30_CURRENT_SOFT_LIMIT_A,
      GL30_CURRENT_SOFT_LIMIT_A);
}
