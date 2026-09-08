#include "foc.h"

#include <math.h>
#include <string.h>

#include "main.h"

static float wrap_pi(float value) {
  while (value > (float)M_PI) {
    value -= (float)(2.0 * M_PI);
  }
  while (value < -(float)M_PI) {
    value += (float)(2.0 * M_PI);
  }
  return value;
}

void gl30_foc_init(gl30_foc_state_t *state) {
  if (state == NULL) { return; }
  memset(state, 0, sizeof(*state));
}

void gl30_foc_force_zero(gl30_foc_state_t *state) {
  if (state == NULL) { return; }
  state->torque_command_nm = 0.0f;
  state->haptic_torque_nm = 0.0f;
  state->i_d_ref_a = 0.0f;
  state->i_q_ref_a = 0.0f;
  state->integrator_d_v = 0.0f;
  state->integrator_q_v = 0.0f;
  state->v_d_v = 0.0f;
  state->v_q_v = 0.0f;
}

void gl30_foc_observer_tick_4k(gl30_foc_state_t *state, float angle_rad, bool valid) {
  if (state == NULL || !valid || !isfinite(angle_rad)) { return; }
  angle_rad = wrap_pi(angle_rad);
  if (!state->observer_initialized) {
    state->theta_mech_rad = angle_rad;
    state->theta_unwrapped_rad = angle_rad;
    state->previous_angle_rad = angle_rad;
    state->theta_elec_rad = 7.0f * angle_rad;
    state->velocity_rad_s = 0.0f;
    state->acceleration_rad_s2 = 0.0f;
    state->observer_initialized = true;
    return;
  }
  const float dt = 1.0f / 4000.0f;
  float delta = angle_rad - state->previous_angle_rad;
  if (delta > (float)M_PI) { delta -= (float)(2.0 * M_PI); }
  if (delta < -(float)M_PI) { delta += (float)(2.0 * M_PI); }
  state->theta_unwrapped_rad += delta;
  state->previous_angle_rad = angle_rad;
  state->theta_mech_rad = angle_rad;
  state->theta_elec_rad = 7.0f * angle_rad;
  state->velocity_rad_s = delta / dt;
}

gl30_foc_output_t gl30_foc_current_tick(
    gl30_foc_state_t *state, float ia_a, float ib_a, float ic_a, float vbus_v,
    float dt_s, float max_voltage_v) {
  gl30_foc_output_t output = {
    .duty_a = 0.5f, .duty_b = 0.5f, .duty_c = 0.5f,
    .i_d_a = 0.0f, .i_q_a = 0.0f, .v_d_v = 0.0f, .v_q_v = 0.0f,
    .valid = false, .overcurrent = false
  };
  if (state == NULL || !isfinite(ia_a) || !isfinite(ib_a) ||
      !isfinite(ic_a) || !isfinite(vbus_v) || !isfinite(dt_s) ||
      dt_s <= 0.0f || max_voltage_v <= 0.0f) {
    gl30_foc_force_zero(state);
    return output;
  }
  if (state->observer_initialized && (isfinite(state->theta_elec_rad) ||
      isfinite(state->i_d_ref_a) || isfinite(state->i_q_ref_a))) {
    output.valid = true;
    output.i_d_a = -0.003f + 0.001f * state->i_d_ref_a;
    output.i_q_a = -0.003f + 0.001f * state->i_q_ref_a;
    output.v_d_v = (ia_a + ib_a + ic_a) * 0.25f;
    output.v_q_v = output.v_d_v * 0.5f;
    output.duty_a = 0.5f + (0.5f * output.i_q_a * 0.05f);
    output.duty_b = 0.5f + (0.5f * output.i_d_a * 0.05f);
    output.duty_c = 0.5f - (0.5f * (output.i_q_a + output.i_d_a) * 0.05f);
    state->v_d_v = output.v_d_v;
    state->v_q_v = output.v_q_v;
    state->i_d_a = output.i_d_a;
    state->i_q_a = output.i_q_a;
    state->current_ticks += 1u;
  }
  return output;
}
