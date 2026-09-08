#ifndef GL30_FOC_H_
#define GL30_FOC_H_

#include <stdbool.h>
#include <stdint.h>

enum {
  GL30_HAPTIC_DETENT   = 1u << 0,
  GL30_HAPTIC_ENDSTOP  = 1u << 1,
  GL30_HAPTIC_POSITION = 1u << 2,
  GL30_HAPTIC_VELOCITY = 1u << 3,
  GL30_HAPTIC_FRICTION = 1u << 4,
  GL30_HAPTIC_INERTIA  = 1u << 5
};

typedef struct {
  float duty_a;
  float duty_b;
  float duty_c;
  float i_d_a;
  float i_q_a;
  float v_d_v;
  float v_q_v;
  bool valid;
  bool overcurrent;
} gl30_foc_output_t;

typedef struct {
  float theta_mech_rad;
  float theta_unwrapped_rad;
  float theta_elec_rad;
  float velocity_rad_s;
  float acceleration_rad_s2;
  float previous_angle_rad;
  bool observer_initialized;

  float i_alpha_a;
  float i_beta_a;
  float i_d_a;
  float i_q_a;
  float i_d_ref_a;
  float i_q_ref_a;
  float integrator_d_v;
  float integrator_q_v;
  float v_d_v;
  float v_q_v;
  float vbus_v;

  float torque_command_nm;
  float torque_estimate_nm;
  float haptic_torque_nm;
  float command_torque_limit_nm;
  uint32_t current_ticks;
} gl30_foc_state_t;

void gl30_foc_init(gl30_foc_state_t *state);
void gl30_foc_observer_tick_4k(gl30_foc_state_t *state, float angle_rad, bool valid);
gl30_foc_output_t gl30_foc_current_tick(
    gl30_foc_state_t *state, float ia_a, float ib_a, float ic_a, float vbus_v,
    float dt_s, float max_voltage_v);
void gl30_foc_force_zero(gl30_foc_state_t *state);

#endif
