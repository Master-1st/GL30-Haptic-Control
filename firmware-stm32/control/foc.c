#include "foc.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "board_config.h"

#define GL30_PI_F      3.14159265358979323846f
#define GL30_TWO_PI_F  6.28318530717958647692f
#define GL30_SQRT3_BY_2 0.86602540378443864676f

static float clampf(float value, float low, float high) {
  if (value < low) {
    return low;
  }
  if (value > high) {
    return high;
  }
  return value;
}

static float wrap_pi(float value) {
  /* Encoder angles and deltas already in range need no library reduction. */
  if (value >= -GL30_PI_F && value <= GL30_PI_F) {
    return value;
  }
  /* Normal encoder angles, deltas and the 7-pole-pair electrical angle fit
   * within four turns. At most four subtractions avoid the general library
   * reducer in the real-time path; the finite range check bounds both loops. */
  if (value >= -4.0f * GL30_TWO_PI_F && value <= 4.0f * GL30_TWO_PI_F) {
    while (value > GL30_PI_F) { value -= GL30_TWO_PI_F; }
    while (value < -GL30_PI_F) { value += GL30_TWO_PI_F; }
    return value;
  }
  /* Range reduction must terminate even when a corrupt finite sample is huge. */
  value = fmodf(value, GL30_TWO_PI_F);
  if (value > GL30_PI_F) {
    value -= GL30_TWO_PI_F;
  }
  if (value < -GL30_PI_F) {
    value += GL30_TWO_PI_F;
  }
  return value;
}

static bool finite_command(const gl30_haptic_command_t *command) {
  return command != NULL &&
         isfinite(command->targetPositionRad) &&
         isfinite(command->targetVelocityRadS) &&
         isfinite(command->detentWidthRad) &&
         isfinite(command->detentStrengthNm) &&
         isfinite(command->endstopMinRad) &&
         isfinite(command->endstopMaxRad) &&
         isfinite(command->endstopStrengthNm) &&
         isfinite(command->dampingNmPerRadS) &&
         isfinite(command->inertiaKgM2) &&
         isfinite(command->frictionNm) &&
         isfinite(command->userTorqueLimitNm) &&
         isfinite(command->activeSpeedLimitRadS);
}

void gl30_foc_init(gl30_foc_state_t *state) {
  if (state == NULL) {
    return;
  }
  memset(state, 0, sizeof(*state));
  state->command_torque_limit_nm = GL30_USER_TORQUE_LIMIT_NM;
  state->active_command.endstopMinRad = -GL30_PI_F;
  state->active_command.endstopMaxRad = GL30_PI_F;
  state->active_command.userTorqueLimitNm = GL30_USER_TORQUE_LIMIT_NM;
}

bool gl30_foc_apply_command(gl30_foc_state_t *state, const gl30_haptic_command_t *command) {
  if (state == NULL || !finite_command(command)) {
    if (state != NULL) {
      state->rejected_commands++;
    }
    return false;
  }
  if (command->endstopMinRad > command->endstopMaxRad ||
      command->detentWidthRad < 0.0f ||
      command->detentStrengthNm < 0.0f ||
      command->endstopStrengthNm < 0.0f ||
      command->dampingNmPerRadS < 0.0f ||
      command->inertiaKgM2 < 0.0f ||
      command->frictionNm < 0.0f ||
      command->userTorqueLimitNm < 0.0f ||
      command->activeSpeedLimitRadS < 0.0f) {
    state->rejected_commands++;
    return false;
  }

  if ((command->modeFlags & GL30_HAPTIC_DETENT) != 0u) {
    if (command->detentWidthRad <= 1.0e-5f) {
      state->rejected_commands++;
      return false;
    }
    if ((command->modeFlags & GL30_HAPTIC_ENDSTOP) != 0u) {
      const double lo = ceil(((double)command->endstopMinRad -
                              (double)command->targetPositionRad) / (double)command->detentWidthRad);
      const double hi = floor(((double)command->endstopMaxRad -
                               (double)command->targetPositionRad) / (double)command->detentWidthRad);
      if (lo > hi || lo < INT32_MIN || hi > INT32_MAX) {
        state->rejected_commands++;
        return false;
      }
    }
  }

  gl30_haptic_command_t previous = state->active_command;
  state->active_command = *command;
  state->active_command.modeFlags &=
      GL30_HAPTIC_DETENT | GL30_HAPTIC_ENDSTOP | GL30_HAPTIC_POSITION |
      GL30_HAPTIC_VELOCITY | GL30_HAPTIC_FRICTION | GL30_HAPTIC_INERTIA;
  state->active_command.detentStrengthNm =
      clampf(command->detentStrengthNm, 0.0f, GL30_USER_TORQUE_LIMIT_NM);
  state->active_command.endstopStrengthNm =
      clampf(command->endstopStrengthNm, 0.0f, 2.0f);
  state->active_command.dampingNmPerRadS =
      clampf(command->dampingNmPerRadS, 0.0f, 0.20f);
  state->active_command.frictionNm =
      clampf(command->frictionNm, 0.0f, GL30_SELF_DRIVE_LIMIT_NM);
  state->active_command.userTorqueLimitNm =
      clampf(command->userTorqueLimitNm, 0.0f, GL30_USER_TORQUE_LIMIT_NM);
  state->command_torque_limit_nm = state->active_command.userTorqueLimitNm;
  state->last_command_nonce = command->commandNonce;
  /* A transport nonce is not a new effect. Compare normalized commands so
   * repeated clamped parameters also preserve the logical detent and ramp. */
  previous.commandNonce = state->active_command.commandNonce;
  if (memcmp(&previous, &state->active_command, sizeof(previous)) != 0) {
    state->detent_initialized = false;
    state->haptic_transition_from_nm = state->haptic_torque_nm;
    state->haptic_transition_pending = true;
    state->haptic_transition_ticks = 0u;
  }
  return true;
}

void gl30_foc_observer_tick_4k(gl30_foc_state_t *state, float angle_rad, bool valid) {
  const float dt = 1.0f / (float)GL30_OBSERVER_HZ;
  if (state == NULL || !valid || !isfinite(angle_rad)) {
    return;
  }
  angle_rad = wrap_pi(angle_rad);
  state->theta_elec_rad = wrap_pi(
      GL30_PHASE_DIRECTION *
      ((float)GL30_MOTOR_POLE_PAIRS *
       (angle_rad - GL30_ELECTRICAL_ZERO_RAD)));

  if (!state->observer_initialized) {
    state->theta_mech_rad = angle_rad;
    state->theta_unwrapped_rad = angle_rad;
    state->previous_angle_rad = angle_rad;
    state->observer_initialized = true;
    return;
  }

  const float delta = wrap_pi(angle_rad - state->previous_angle_rad);
  const float raw_velocity = delta / dt;
  const float previous_velocity = state->velocity_rad_s;
  state->theta_unwrapped_rad += delta;
  state->theta_mech_rad = angle_rad;
  state->previous_angle_rad = angle_rad;
  state->velocity_rad_s += 0.28f * (raw_velocity - state->velocity_rad_s);
  const float raw_acceleration = (state->velocity_rad_s - previous_velocity) / dt;
  state->acceleration_rad_s2 +=
      0.12f * (raw_acceleration - state->acceleration_rad_s2);
}

static gl30_foc_output_t foc_tick(
    gl30_foc_state_t *state, float ia_a, float ib_a, float ic_a, float vbus_v,
    float dt_s, float max_voltage_v, bool regulate_current,
    float requested_d_v, float requested_q_v) {
  gl30_foc_output_t output = {
      .duty_a = 0.5f, .duty_b = 0.5f, .duty_c = 0.5f, .valid = false};
  if (state == NULL || !isfinite(ia_a) || !isfinite(ib_a) || !isfinite(ic_a) ||
      !isfinite(vbus_v) || vbus_v < 1.0f || !isfinite(dt_s) ||
      dt_s <= 0.0f || dt_s > 0.001f || !isfinite(max_voltage_v) ||
      max_voltage_v <= 0.0f || !state->observer_initialized ||
      !isfinite(state->theta_elec_rad) || !isfinite(state->i_d_ref_a) ||
      !isfinite(state->i_q_ref_a) || !isfinite(state->integrator_d_v) ||
      !isfinite(state->integrator_q_v) || !isfinite(requested_d_v) ||
      !isfinite(requested_q_v)) {
    gl30_foc_force_zero(state);
    return output;
  }

  /* TI DRV8316 three-CSA offset-correction matrix, datasheet equations 5-7. */
  const float ia = 0.995832f * ia_a - 0.028199f * ib_a - 0.014988f * ic_a;
  const float ib = 0.037737f * ia_a + 1.007723f * ib_a - 0.033757f * ic_a;
  const float ic = 0.009226f * ia_a + 0.029805f * ib_a + 1.003268f * ic_a;
  const float peak_current = fmaxf(fabsf(ia), fmaxf(fabsf(ib), fabsf(ic)));
  output.overcurrent = peak_current > GL30_CURRENT_HARD_LIMIT_A;
  if (output.overcurrent) {
    return output;
  }

  const float i_alpha = (2.0f * ia - ib - ic) * (1.0f / 3.0f);
  const float i_beta = (ib - ic) * (1.0f / 1.73205080756887729353f);
#if defined(GL30_FOC_USE_CORDIC)
  float sin_theta, cos_theta;
  if (!gl30_foc_sincos(wrap_pi(state->theta_elec_rad), &sin_theta, &cos_theta)) {
    gl30_foc_force_zero(state);
    return output;
  }
#else
  /* Share canonical-angle range reduction locally: an out-of-line helper
   * added register saves and worsened the small-angle PWM deadline in H20.
   * Both small-angle functions still use libm, without approximate kernels. */
  float angle = state->theta_elec_rad;
  int quadrant = 0;
  if (fabsf(angle) > 0.25f * GL30_PI_F && fabsf(angle) <= GL30_PI_F) {
    if (angle >= 0.75f * GL30_PI_F) {
      angle -= GL30_PI_F; quadrant = 2;
    } else if (angle > 0.0f) {
      angle -= 0.5f * GL30_PI_F; quadrant = 1;
    } else if (angle <= -0.75f * GL30_PI_F) {
      angle += GL30_PI_F; quadrant = 2;
    } else {
      angle += 0.5f * GL30_PI_F; quadrant = -1;
    }
  }
  const float s = sinf(angle), c = cosf(angle);
  float sin_theta = s, cos_theta = c;
  if (quadrant == 1) { sin_theta = c; cos_theta = -s; }
  else if (quadrant == -1) { sin_theta = -c; cos_theta = s; }
  else if (quadrant == 2) { sin_theta = -s; cos_theta = -c; }
#endif
  const float i_d = cos_theta * i_alpha + sin_theta * i_beta;
  const float i_q = -sin_theta * i_alpha + cos_theta * i_beta;

  const float error_d = state->i_d_ref_a - i_d;
  const float error_q = state->i_q_ref_a - i_q;
  if (regulate_current) {
    state->integrator_d_v += GL30_CURRENT_KI_V_PER_AS * dt_s * error_d;
    state->integrator_q_v += GL30_CURRENT_KI_V_PER_AS * dt_s * error_q;
  } else {
    state->integrator_d_v = 0.0f;
    state->integrator_q_v = 0.0f;
  }
  const float v_d_unsat = regulate_current ?
      GL30_CURRENT_KP_V_PER_A * error_d + state->integrator_d_v : requested_d_v;
  const float v_q_unsat = regulate_current ?
      GL30_CURRENT_KP_V_PER_A * error_q + state->integrator_q_v : requested_q_v;
  /* Preserve the all-low-side current-sampling window around the timer peak.
   * For centered SVPWM, vector magnitude <= duty_span / sqrt(3) * VBUS. */
  const float duty_span = GL30_TIM1_MAX_DUTY - GL30_TIM1_MIN_DUTY;
  const float voltage_limit = fminf(max_voltage_v,
      0.57735026918962576451f * duty_span * vbus_v * 0.95f);
  const float magnitude_squared = v_d_unsat * v_d_unsat + v_q_unsat * v_q_unsat;
  if (!isfinite(i_d) || !isfinite(i_q) || !isfinite(voltage_limit) ||
      !isfinite(v_d_unsat) || !isfinite(v_q_unsat) ||
      !isfinite(magnitude_squared)) {
    gl30_foc_force_zero(state);
    return output;
  }
  const float magnitude = sqrtf(magnitude_squared);
  const float scale = (magnitude > voltage_limit && magnitude > 0.0f)
                        ? (voltage_limit / magnitude)
                        : 1.0f;
  const float v_d = v_d_unsat * scale;
  const float v_q = v_q_unsat * scale;
  if (regulate_current) {
    state->integrator_d_v += GL30_CURRENT_KAW * (v_d - v_d_unsat);
    state->integrator_q_v += GL30_CURRENT_KAW * (v_q - v_q_unsat);
    state->integrator_d_v = clampf(state->integrator_d_v, -voltage_limit, voltage_limit);
    state->integrator_q_v = clampf(state->integrator_q_v, -voltage_limit, voltage_limit);
  }

  const float v_alpha = cos_theta * v_d - sin_theta * v_q;
  const float v_beta = sin_theta * v_d + cos_theta * v_q;
  float va = v_alpha;
  float vb = -0.5f * v_alpha + GL30_SQRT3_BY_2 * v_beta;
  float vc = -0.5f * v_alpha - GL30_SQRT3_BY_2 * v_beta;
  const float common_mode =
      -0.5f * (fmaxf(va, fmaxf(vb, vc)) + fminf(va, fminf(vb, vc)));
  va += common_mode;
  vb += common_mode;
  vc += common_mode;

  /* One measured-bus reciprocal replaces three hardware divisions. */
  const float inv_vbus = 1.0f / vbus_v;
  output.duty_a = clampf(0.5f + va * inv_vbus,
                         GL30_TIM1_MIN_DUTY, GL30_TIM1_MAX_DUTY);
  output.duty_b = clampf(0.5f + vb * inv_vbus,
                         GL30_TIM1_MIN_DUTY, GL30_TIM1_MAX_DUTY);
  output.duty_c = clampf(0.5f + vc * inv_vbus,
                         GL30_TIM1_MIN_DUTY, GL30_TIM1_MAX_DUTY);
  output.i_d_a = i_d;
  output.i_q_a = i_q;
  output.v_d_v = v_d;
  output.v_q_v = v_q;
  output.valid = true;

  state->i_alpha_a = i_alpha;
  state->i_beta_a = i_beta;
  state->i_d_a = i_d;
  state->i_q_a = i_q;
  state->v_d_v = v_d;
  state->v_q_v = v_q;
  state->vbus_v = vbus_v;
  state->torque_estimate_nm = i_q * GL30_MOTOR_KT_NM_PER_A;
  state->current_ticks++;
  return output;
}

gl30_foc_output_t gl30_foc_current_tick(
    gl30_foc_state_t *state, float ia_a, float ib_a, float ic_a, float vbus_v,
    float dt_s, float max_voltage_v) {
  return foc_tick(state, ia_a, ib_a, ic_a, vbus_v, dt_s, max_voltage_v,
                  true, 0.0f, 0.0f);
}

gl30_foc_output_t gl30_foc_voltage_tick(
    gl30_foc_state_t *state, float ia_a, float ib_a, float ic_a, float vbus_v,
    float dt_s, float max_voltage_v, float v_d_v, float v_q_v) {
  return foc_tick(state, ia_a, ib_a, ic_a, vbus_v, dt_s, max_voltage_v,
                  false, v_d_v, v_q_v);
}

void gl30_foc_force_zero(gl30_foc_state_t *state) {
  if (state == NULL) {
    return;
  }
  state->torque_command_nm = 0.0f;
  state->haptic_torque_nm = 0.0f;
  state->detent_initialized = false;
  state->detent_position = 0;
  state->detent_center_rad = 0.0f;
  state->detent_fraction = 0.0f;
  state->haptic_transition_from_nm = 0.0f;
  state->haptic_transition_ticks = 0u;
  state->haptic_transition_pending = true;
  state->i_d_ref_a = 0.0f;
  state->i_q_ref_a = 0.0f;
  state->integrator_d_v = 0.0f;
  state->integrator_q_v = 0.0f;
  state->v_d_v = 0.0f;
  state->v_q_v = 0.0f;
}

void gl30_foc_make_telemetry(
    const gl30_foc_state_t *state,
    uint32_t motor_state,
    uint32_t fault_bits,
    uint32_t warning_bits,
    uint16_t isr_cycles,
    uint16_t encoder_status,
    uint16_t dropped_commands,
    gl30_motor_state_fast_t *out) {
  if (state == NULL || out == NULL) {
    return;
  }
  memset(out, 0, sizeof(*out));
  out->angleRad = state->theta_unwrapped_rad;
  out->velocityRadPerSec = state->velocity_rad_s;
  out->accelerationRadPerSec2 = state->acceleration_rad_s2;
  out->iqRefA = state->i_q_ref_a;
  out->iqMeasA = state->i_q_a;
  out->idMeasA = state->i_d_a;
  out->torqueCmdNm = state->torque_command_nm;
  out->torqueEstNm = state->torque_estimate_nm;
  out->busVoltageV = state->vbus_v;
  out->busCurrentA = fabsf(state->i_q_a);
  if ((state->active_command.modeFlags & GL30_HAPTIC_DETENT) != 0u) {
    out->logicalPosition = state->detent_initialized ? state->detent_position : 0;
    out->subPosition = state->detent_initialized ?
        (state->theta_unwrapped_rad - state->detent_center_rad) /
          state->active_command.detentWidthRad : 0.0f;
  } else {
    out->logicalPosition = (int32_t)lrintf(state->theta_unwrapped_rad / GL30_TWO_PI_F);
    out->subPosition = wrap_pi(state->theta_unwrapped_rad);
  }
  out->motorState = motor_state;
  out->faultBits = fault_bits;
  out->warningBits = warning_bits;
  out->isrCycles = isr_cycles;
  out->encoderStatus = encoder_status;
  out->droppedCmds = dropped_commands;
}
