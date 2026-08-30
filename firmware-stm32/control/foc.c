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
  while (value > GL30_PI_F) {
    value -= GL30_TWO_PI_F;
  }
  while (value < -GL30_PI_F) {
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
  return true;
}

void gl30_foc_observer_tick_4k(gl30_foc_state_t *state, float angle_rad, bool valid) {
  const float dt = 1.0f / (float)GL30_OBSERVER_HZ;
  if (state == NULL || !valid || !isfinite(angle_rad)) {
    return;
  }
  angle_rad = wrap_pi(angle_rad);

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
  state->theta_elec_rad = wrap_pi(
      GL30_PHASE_DIRECTION *
      ((float)GL30_MOTOR_POLE_PAIRS *
       (state->theta_mech_rad - GL30_ELECTRICAL_ZERO_RAD)));
}

gl30_foc_output_t gl30_foc_current_tick_40k(
    gl30_foc_state_t *state, float ia_a, float ib_a, float ic_a, float vbus_v) {
  gl30_foc_output_t output = {
      .duty_a = 0.5f, .duty_b = 0.5f, .duty_c = 0.5f, .valid = false};
  if (state == NULL || !isfinite(ia_a) || !isfinite(ib_a) || !isfinite(ic_a) ||
      !isfinite(vbus_v) || vbus_v < 1.0f || !state->observer_initialized) {
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

  const float i_alpha = (2.0f * ia - ib - ic) / 3.0f;
  const float i_beta = (ib - ic) / 1.73205080756887729353f;
  const float sin_theta = sinf(state->theta_elec_rad);
  const float cos_theta = cosf(state->theta_elec_rad);
  const float i_d = cos_theta * i_alpha + sin_theta * i_beta;
  const float i_q = -sin_theta * i_alpha + cos_theta * i_beta;

  const float error_d = state->i_d_ref_a - i_d;
  const float error_q = state->i_q_ref_a - i_q;
  const float dt = 1.0f / (float)GL30_PWM_HZ;
  state->integrator_d_v += GL30_CURRENT_KI_V_PER_AS * dt * error_d;
  state->integrator_q_v += GL30_CURRENT_KI_V_PER_AS * dt * error_q;

  const float v_d_unsat = GL30_CURRENT_KP_V_PER_A * error_d + state->integrator_d_v;
  const float v_q_unsat = GL30_CURRENT_KP_V_PER_A * error_q + state->integrator_q_v;
  /* Preserve the all-low-side current-sampling window around the timer peak.
   * For centered SVPWM, vector magnitude <= duty_span / sqrt(3) * VBUS. */
  const float duty_span = GL30_TIM1_MAX_DUTY - GL30_TIM1_MIN_DUTY;
  const float voltage_limit =
      0.57735026918962576451f * duty_span * vbus_v * 0.95f;
  const float magnitude = sqrtf(v_d_unsat * v_d_unsat + v_q_unsat * v_q_unsat);
  const float scale = (magnitude > voltage_limit && magnitude > 0.0f)
                        ? (voltage_limit / magnitude)
                        : 1.0f;
  const float v_d = v_d_unsat * scale;
  const float v_q = v_q_unsat * scale;
  state->integrator_d_v += GL30_CURRENT_KAW * (v_d - v_d_unsat);
  state->integrator_q_v += GL30_CURRENT_KAW * (v_q - v_q_unsat);
  state->integrator_d_v = clampf(state->integrator_d_v, -voltage_limit, voltage_limit);
  state->integrator_q_v = clampf(state->integrator_q_v, -voltage_limit, voltage_limit);

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

  output.duty_a = clampf(0.5f + va / vbus_v,
                         GL30_TIM1_MIN_DUTY, GL30_TIM1_MAX_DUTY);
  output.duty_b = clampf(0.5f + vb / vbus_v,
                         GL30_TIM1_MIN_DUTY, GL30_TIM1_MAX_DUTY);
  output.duty_c = clampf(0.5f + vc / vbus_v,
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

void gl30_foc_force_zero(gl30_foc_state_t *state) {
  if (state == NULL) {
    return;
  }
  state->torque_command_nm = 0.0f;
  state->haptic_torque_nm = 0.0f;
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
  out->logicalPosition = (int32_t)lrintf(state->theta_unwrapped_rad / GL30_TWO_PI_F);
  out->subPosition = wrap_pi(state->theta_unwrapped_rad);
  out->motorState = motor_state;
  out->faultBits = fault_bits;
  out->warningBits = warning_bits;
  out->isrCycles = isr_cycles;
  out->encoderStatus = encoder_status;
  out->droppedCmds = dropped_commands;
}
