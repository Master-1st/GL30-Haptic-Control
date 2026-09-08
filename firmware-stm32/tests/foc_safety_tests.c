#include <math.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#include "board_config.h"
#include "control/foc.h"
#include "protocol/v6_protocol.h"

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define CHECK(cond, msg) \
  do { \
    g_tests_run++; \
    if (!(cond)) { \
      g_tests_failed++; \
      fprintf(stderr, "[FAIL] %s\n", (msg)); \
    } \
  } while (0)

static float wrap_pi_for_test(float value) {
  const float pi = acosf(-1.0f);
  const float two_pi = 2.0f * pi;
  while (value > pi) {
    value -= two_pi;
  }
  while (value < -pi) {
    value += two_pi;
  }
  return value;
}

static bool approx_eq(float left, float right) {
  const float delta = fabsf(left - right);
  return delta <= 1e-5f;
}

static void dq_to_phase_currents(float id_a, float iq_a, float theta_elec_rad,
                                float *ia_a, float *ib_a, float *ic_a) {
  const float sin_theta = sinf(theta_elec_rad);
  const float cos_theta = cosf(theta_elec_rad);
  const float i_alpha = cos_theta * id_a - sin_theta * iq_a;
  const float i_beta = sin_theta * id_a + cos_theta * iq_a;

  *ia_a = i_alpha;
  *ib_a = -0.5f * i_alpha + 0.86602540378f * i_beta;
  *ic_a = -0.5f * i_alpha - 0.86602540378f * i_beta;
}

static void exact_first_order_rl_step(float *id_a, float *iq_a, float v_d_cmd_v,
                                     float v_q_cmd_v, float dt_s, float r_phase_ohm,
                                     float l_phase_h) {
  const float exp_d = expf(-(r_phase_ohm / l_phase_h) * dt_s);
  const float steady_id = v_d_cmd_v / r_phase_ohm;
  const float steady_iq = v_q_cmd_v / r_phase_ohm;

  *id_a = steady_id + (*id_a - steady_id) * exp_d;
  *iq_a = steady_iq + (*iq_a - steady_iq) * exp_d;
}

static gl30_foc_state_t base_foc_state(float theta_elec_rad, float i_d_ref_a, float i_q_ref_a) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  state.observer_initialized = true;
  state.theta_mech_rad = 0.0f;
  state.theta_unwrapped_rad = 0.0f;
  state.theta_elec_rad = theta_elec_rad;
  state.i_d_ref_a = i_d_ref_a;
  state.i_q_ref_a = i_q_ref_a;
  return state;
}

static void run_fixed_rotor_step_window(gl30_foc_state_t *state, float theta_elec_rad, float dt_s,
                                       float r_phase_ohm, float l_phase_h, float q_ref_a,
                                       float vbus_v, size_t settle_steps,
                                       float *id_a, float *iq_a,
                                       float *applied_v_d, float *applied_v_q, float max_voltage_v,
                                       bool enforce_terminal_check, float *max_phase_i) {
  state->i_d_ref_a = 0.0f;
  state->i_q_ref_a = q_ref_a;

  const float expected_ref = q_ref_a;
  float ia_a = 0.0f;
  float ib_a = 0.0f;
  float ic_a = 0.0f;
  float local_max_phase_i = *max_phase_i;

  for (size_t step = 0u; step < settle_steps; step++) {
    dq_to_phase_currents(*id_a, *iq_a, theta_elec_rad, &ia_a, &ib_a, &ic_a);
    const gl30_foc_output_t out = gl30_foc_current_tick(
        state, ia_a, ib_a, ic_a, vbus_v, dt_s, max_voltage_v);

    CHECK(out.valid, "foc closed-loop fixed rotor step keeps output valid");
    CHECK(isfinite(out.v_d_v) && isfinite(out.v_q_v),
          "foc fixed-rotor step voltage command remains finite");
    CHECK(hypotf(out.v_d_v, out.v_q_v) <= 0.30001f,
          "foc fixed-rotor step respects 0.30V limit");
    CHECK(!out.overcurrent, "foc fixed-rotor step stays below overcurrent path");

    const float phase_abs = fmaxf(fabsf(ia_a), fmaxf(fabsf(ib_a), fabsf(ic_a)));
    if (phase_abs > local_max_phase_i) {
      local_max_phase_i = phase_abs;
    }

    exact_first_order_rl_step(id_a, iq_a, *applied_v_d, *applied_v_q, dt_s,
                             r_phase_ohm, l_phase_h);
    *applied_v_d = out.v_d_v;
    *applied_v_q = out.v_q_v;

    if (enforce_terminal_check && step == settle_steps - 1u) {
      CHECK(fabsf(state->i_q_a - expected_ref) <= 0.005f,
            "foc closed-loop final dq feedback iq tracks reference");
      CHECK(fabsf(*iq_a - expected_ref) <= 0.015f,
            "foc fixed-rotor plant dq current tracks reference");
    }
  }

  *max_phase_i = local_max_phase_i;
}

static void test_foc_fixed_rotor_average_rl_closed_loop(void) {
  const float r_nom = GL30_MOTOR_PHASE_R_OHM;
  const float l_nom = GL30_MOTOR_PHASE_L_H;
  const float theta_set[] = {0.0f, 0.6f, 2.0f};
  const float dt_set[] = {1.0f / 20000.0f, 1.0f / 40000.0f};
  const float r_scale[] = {0.8f, 1.0f, 1.2f};
  const float l_scale[] = {0.8f, 1.0f, 1.2f};

  for (size_t t = 0u; t < 3u; t++) {
    for (size_t d = 0u; d < 2u; d++) {
      for (size_t s = 0u; s < 3u; s++) {
        for (size_t l = 0u; l < 3u; l++) {
          gl30_foc_state_t state = base_foc_state(theta_set[t], 0.0f, 0.0f);
          float id_a = 0.0f;
          float iq_a = 0.0f;
          float max_phase_i = 0.0f;
          float delayed_v_d = 0.0f;
          float delayed_v_q = 0.0f;
          const float r_ohm = r_nom * r_scale[s];
          const float l_h = l_nom * l_scale[l];
          const float dt = dt_set[d];
          const size_t settle_steps = (size_t)ceilf(0.05f / dt);
          CHECK((float)settle_steps * dt >= 0.05f - 1.0e-6f,
                "polarity dwell duration at least 50ms for fixed-rotor RL regression");
          CHECK(isfinite(id_a) && isfinite(iq_a), "fixed-rotor plant state is initialized");
          run_fixed_rotor_step_window(&state, theta_set[t], dt, r_ohm, l_h, 0.1f, 12.0f,
                                     settle_steps, &id_a, &iq_a,
                                     &delayed_v_d, &delayed_v_q, 0.30f, true, &max_phase_i);
          CHECK(fabsf(state.i_q_a - 0.1f) <= 0.005f,
                "positive-step fixed-rotor final dq feedback tracks +0.1 A ref");
          CHECK(fabsf(iq_a - 0.1f) <= 0.015f,
                "positive-step fixed-rotor plant dq current tracks +0.1 A ref");

          run_fixed_rotor_step_window(&state, theta_set[t], dt, r_ohm, l_h, -0.1f, 12.0f,
                                     settle_steps, &id_a, &iq_a,
                                     &delayed_v_d, &delayed_v_q, 0.30f, true, &max_phase_i);
          CHECK(fabsf(state.i_q_a + 0.1f) <= 0.005f,
                "negative-step fixed-rotor final dq feedback tracks -0.1 A ref");
          CHECK(fabsf(iq_a + 0.1f) <= 0.015f,
                "negative-step fixed-rotor plant dq current tracks -0.1 A ref");
          CHECK(max_phase_i < 0.25f,
                "fixed-rotor plant phase currents peak below 0.25 A");
        }
      }
    }
  }
}

static void assert_nonfinite_current_case(const char *name, float ia_a, float ib_a, float ic_a, float vbus_v,
                                        float theta_elec_rad, float i_d_ref_a, float i_q_ref_a,
                                        float integrator_d_v, float integrator_q_v) {
  gl30_foc_state_t state = base_foc_state(theta_elec_rad, i_d_ref_a, i_q_ref_a);
  state.integrator_d_v = integrator_d_v;
  state.integrator_q_v = integrator_q_v;
  const gl30_foc_output_t out = gl30_foc_current_tick(
      &state, ia_a, ib_a, ic_a, vbus_v, 1.0f / (float)GL30_PWM_HZ, vbus_v);
  CHECK(!out.valid, name);
  CHECK(out.duty_a == 0.5f && out.duty_b == 0.5f && out.duty_c == 0.5f,
        name);
}

typedef enum {
  FIELD_IA = 0,
  FIELD_IB,
  FIELD_IC,
  FIELD_VBUS,
  FIELD_THETA_ELEC,
  FIELD_I_D_REF,
  FIELD_I_Q_REF,
  FIELD_I_D_INTEGRATOR,
  FIELD_I_Q_INTEGRATOR,
  FIELD_V_D_CMD,
  FIELD_V_Q_CMD,
  FIELD_DT,
  FIELD_MAX_VOLTAGE
} nonfinite_field_t;

static void test_nonfinite_field_variation(nonfinite_field_t field, float val, const char *label) {
  float ia_a = 0.05f;
  float ib_a = 0.05f;
  float ic_a = 0.05f;
  float vbus_v = 12.0f;
  float theta_elec_rad = 0.0f;
  float i_d_ref_a = 0.0f;
  float i_q_ref_a = 0.1f;
  float integrator_d_v = 0.0f;
  float integrator_q_v = 0.0f;

  switch (field) {
    case FIELD_IA:
      ia_a = val;
      break;
    case FIELD_IB:
      ib_a = val;
      break;
    case FIELD_IC:
      ic_a = val;
      break;
    case FIELD_VBUS:
      vbus_v = val;
      break;
    case FIELD_THETA_ELEC:
      theta_elec_rad = val;
      break;
    case FIELD_I_D_REF:
      i_d_ref_a = val;
      break;
    case FIELD_I_Q_REF:
      i_q_ref_a = val;
      break;
    case FIELD_I_D_INTEGRATOR:
      integrator_d_v = val;
      break;
    case FIELD_I_Q_INTEGRATOR:
      integrator_q_v = val;
      break;
    default:
      break;
  }

  assert_nonfinite_current_case(label, ia_a, ib_a, ic_a, vbus_v, theta_elec_rad,
                               i_d_ref_a, i_q_ref_a, integrator_d_v, integrator_q_v);
}

static void test_current_tick_rejects_nonfinite_measurements_and_states() {
  const float specials[] = {NAN, INFINITY, -INFINITY};
  const char *labels[] = {"NaN", "+INF", "-INF"};

  for (size_t i = 0u; i < 3u; i++) {
    char name[128];
    snprintf(name, sizeof(name), "current_tick rejects ia=%s", labels[i]);
    test_nonfinite_field_variation(FIELD_IA, specials[i], name);

    snprintf(name, sizeof(name), "current_tick rejects ib=%s", labels[i]);
    test_nonfinite_field_variation(FIELD_IB, specials[i], name);

    snprintf(name, sizeof(name), "current_tick rejects ic=%s", labels[i]);
    test_nonfinite_field_variation(FIELD_IC, specials[i], name);

    snprintf(name, sizeof(name), "current_tick rejects vbus=%s", labels[i]);
    test_nonfinite_field_variation(FIELD_VBUS, specials[i], name);

    snprintf(name, sizeof(name), "current_tick rejects theta_elec=%s", labels[i]);
    test_nonfinite_field_variation(FIELD_THETA_ELEC, specials[i], name);

    snprintf(name, sizeof(name), "current_tick rejects i_d_ref=%s", labels[i]);
    test_nonfinite_field_variation(FIELD_I_D_REF, specials[i], name);

    snprintf(name, sizeof(name), "current_tick rejects i_q_ref=%s", labels[i]);
    test_nonfinite_field_variation(FIELD_I_Q_REF, specials[i], name);

    snprintf(name, sizeof(name), "current_tick rejects integrator_d_v=%s", labels[i]);
    test_nonfinite_field_variation(FIELD_I_D_INTEGRATOR, specials[i], name);

    snprintf(name, sizeof(name), "current_tick rejects integrator_q_v=%s", labels[i]);
    test_nonfinite_field_variation(FIELD_I_Q_INTEGRATOR, specials[i], name);
  }
}

static void test_current_tick_rejects_invalid_dt_and_voltage_limits(void) {
  gl30_foc_state_t state = base_foc_state(0.0f, 0.0f, 0.1f);

  const gl30_foc_output_t out_dt_nan =
      gl30_foc_current_tick(&state, 0.01f, 0.01f, 0.01f, 12.0f, NAN, 12.0f);
  CHECK(!out_dt_nan.valid, "current_tick rejects NaN dt");
  CHECK(out_dt_nan.duty_a == 0.5f && out_dt_nan.duty_b == 0.5f && out_dt_nan.duty_c == 0.5f,
        "current_tick NaN dt keeps duty 0.5");

  const gl30_foc_output_t out_dt_ninf =
      gl30_foc_current_tick(&state, 0.01f, 0.01f, 0.01f, 12.0f, -INFINITY, 12.0f);
  CHECK(!out_dt_ninf.valid, "current_tick rejects -INF dt");
  CHECK(out_dt_ninf.duty_a == 0.5f && out_dt_ninf.duty_b == 0.5f && out_dt_ninf.duty_c == 0.5f,
        "current_tick -INF dt keeps duty 0.5");

  const gl30_foc_output_t out_dt_inf =
      gl30_foc_current_tick(&state, 0.01f, 0.01f, 0.01f, 12.0f, INFINITY, 12.0f);
  CHECK(!out_dt_inf.valid, "current_tick rejects +INF dt");
  CHECK(out_dt_inf.duty_a == 0.5f && out_dt_inf.duty_b == 0.5f && out_dt_inf.duty_c == 0.5f,
        "current_tick +INF dt keeps duty 0.5");

  const gl30_foc_output_t out_dt_neg =
      gl30_foc_current_tick(&state, 0.01f, 0.01f, 0.01f, 12.0f, -0.0005f, 12.0f);
  CHECK(!out_dt_neg.valid, "current_tick rejects negative dt");
  CHECK(out_dt_neg.duty_a == 0.5f && out_dt_neg.duty_b == 0.5f && out_dt_neg.duty_c == 0.5f,
        "current_tick negative dt keeps duty 0.5");

  const gl30_foc_output_t out_dt_zero =
      gl30_foc_current_tick(&state, 0.01f, 0.01f, 0.01f, 12.0f, 0.0f, 12.0f);
  CHECK(!out_dt_zero.valid, "current_tick rejects zero dt");
  CHECK(out_dt_zero.duty_a == 0.5f && out_dt_zero.duty_b == 0.5f && out_dt_zero.duty_c == 0.5f,
        "current_tick zero dt keeps duty 0.5");

  const gl30_foc_output_t out_dt_large =
      gl30_foc_current_tick(&state, 0.01f, 0.01f, 0.01f, 12.0f, 0.01f, 12.0f);
  CHECK(!out_dt_large.valid, "current_tick rejects dt > 1/1000");
  CHECK(out_dt_large.duty_a == 0.5f && out_dt_large.duty_b == 0.5f && out_dt_large.duty_c == 0.5f,
        "current_tick dt >1/1000 keeps duty 0.5");

  const gl30_foc_output_t out_voltage_nan =
      gl30_foc_current_tick(&state, 0.01f, 0.01f, 0.01f, 12.0f, 1.0f / (float)GL30_PWM_HZ, NAN);
  CHECK(!out_voltage_nan.valid, "current_tick rejects NaN max_voltage");
  CHECK(out_voltage_nan.duty_a == 0.5f && out_voltage_nan.duty_b == 0.5f && out_voltage_nan.duty_c == 0.5f,
        "current_tick NaN max_voltage keeps duty 0.5");

  const gl30_foc_output_t out_voltage_inf =
      gl30_foc_current_tick(&state, 0.01f, 0.01f, 0.01f, 12.0f, 1.0f / (float)GL30_PWM_HZ, INFINITY);
  CHECK(!out_voltage_inf.valid, "current_tick rejects +INF max_voltage");
  CHECK(out_voltage_inf.duty_a == 0.5f && out_voltage_inf.duty_b == 0.5f && out_voltage_inf.duty_c == 0.5f,
        "current_tick +INF max_voltage keeps duty 0.5");

  const gl30_foc_output_t out_voltage_nonpos =
      gl30_foc_current_tick(&state, 0.01f, 0.01f, 0.01f, 12.0f, 1.0f / (float)GL30_PWM_HZ, -0.1f);
  CHECK(!out_voltage_nonpos.valid, "current_tick rejects max_voltage <=0");
  CHECK(out_voltage_nonpos.duty_a == 0.5f && out_voltage_nonpos.duty_b == 0.5f && out_voltage_nonpos.duty_c == 0.5f,
        "current_tick max_voltage <=0 keeps duty 0.5");

  const gl30_foc_output_t out_voltage_zero =
      gl30_foc_current_tick(&state, 0.01f, 0.01f, 0.01f, 12.0f, 1.0f / (float)GL30_PWM_HZ, 0.0f);
  CHECK(!out_voltage_zero.valid, "current_tick rejects max_voltage ==0");
  CHECK(out_voltage_zero.duty_a == 0.5f && out_voltage_zero.duty_b == 0.5f && out_voltage_zero.duty_c == 0.5f,
        "current_tick zero max_voltage keeps duty 0.5");

  const gl30_foc_output_t out_voltage_ninf =
      gl30_foc_current_tick(&state, 0.01f, 0.01f, 0.01f, 12.0f, 1.0f / (float)GL30_PWM_HZ, -INFINITY);
  CHECK(!out_voltage_ninf.valid, "current_tick rejects -INF max_voltage");
  CHECK(out_voltage_ninf.duty_a == 0.5f && out_voltage_ninf.duty_b == 0.5f && out_voltage_ninf.duty_c == 0.5f,
        "current_tick -INF max_voltage keeps duty 0.5");
}

static void test_current_tick_rejects_finite_huge_integrators_to_prevent_overflow(void) {
  gl30_foc_state_t state = base_foc_state(0.0f, 0.0f, 0.1f);
  state.integrator_d_v = 1.0e30f;
  state.integrator_q_v = 1.0e30f;
  const gl30_foc_output_t out = gl30_foc_current_tick(
      &state, 0.01f, 0.01f, 0.01f, 12.0f, 1.0f / (float)GL30_PWM_HZ, 12.0f);
  CHECK(!out.valid, "huge finite integrators are rejected to avoid overflow");
  CHECK(out.duty_a == 0.5f && out.duty_b == 0.5f && out.duty_c == 0.5f, "huge finite integrators force center duty");
  CHECK(state.integrator_d_v == 0.0f && state.integrator_q_v == 0.0f,
        "huge finite integrators are cleared by force_zero");
}

static void test_current_tick_20k_and_40k_dt_integrator_difference(void) {
  gl30_foc_state_t state_40k = base_foc_state(0.0f, 0.1f, 0.0f);
  gl30_foc_state_t state_20k = base_foc_state(0.0f, 0.1f, 0.0f);

  const float dt_40k = 1.0f / (float)GL30_PWM_HZ;
  const float dt_20k = 1.0f / 20000.0f;

  (void)gl30_foc_current_tick(&state_40k, 0.01f, -0.01f, 0.01f, 12.0f, dt_40k, 12.0f);
  (void)gl30_foc_current_tick(&state_20k, 0.01f, -0.01f, 0.01f, 12.0f, dt_20k, 12.0f);

  CHECK(fabsf(state_20k.integrator_d_v) > fabsf(state_40k.integrator_d_v),
        "TI-EVM dt updates integrator more than 40k dt");
  CHECK(fabsf(state_20k.integrator_d_v - 2.0f * state_40k.integrator_d_v) <= 1.0e-4f,
        "20k integrator gain is ~2x vs 40k for same error");
}

static void test_current_tick_uses_max_voltage_cap_0p30(void) {
  gl30_foc_state_t state_ti_no_cap = base_foc_state(0.0f, 25.0f, -25.0f);
  gl30_foc_state_t state_ti_cap = base_foc_state(0.0f, 25.0f, -25.0f);

  const float dt_20k = 1.0f / 20000.0f;

  const gl30_foc_output_t out_ti_no_cap =
      gl30_foc_current_tick(&state_ti_no_cap, 0.01f, -0.02f, 0.01f, 12.0f, dt_20k, 12.0f);
  const gl30_foc_output_t out_ti_cap =
      gl30_foc_current_tick(&state_ti_cap, 0.01f, -0.02f, 0.01f, 12.0f, dt_20k, 0.30f);

  CHECK(out_ti_no_cap.valid, "TI-EVM max_voltage=12.0 path stays valid");
  CHECK(out_ti_cap.valid, "TI-EVM max_voltage=0.30 path stays valid");

  const float mag_no_cap = hypotf(out_ti_no_cap.v_d_v, out_ti_no_cap.v_q_v);
  const float mag_cap = hypotf(out_ti_cap.v_d_v, out_ti_cap.v_q_v);
  CHECK(isfinite(mag_no_cap) && isfinite(mag_cap), "max-voltage regression keeps finite voltage magnitudes");
  CHECK(mag_no_cap > 0.30001f, "non-TI EVM voltage magnitude exceeds TI 0.30V cap when no explicit cap is set");
  CHECK(mag_cap <= 0.30001f, "TI-EVM max_voltage cap clamps voltage magnitude to 0.30V limit");
  CHECK(mag_cap <= mag_no_cap, "0.30V cap reduces vector magnitude compared with no explicit cap");
}

static void assert_nonfinite_voltage_case(const char *name, float ia_a, float ib_a, float ic_a,
                                        float vbus_v, float theta_elec_rad, float i_d_ref_a,
                                        float i_q_ref_a, float integrator_d_v,
                                        float integrator_q_v, float requested_d_v,
                                        float requested_q_v, float dt, float max_voltage_v,
                                        bool expected_valid) {
  gl30_foc_state_t state = base_foc_state(theta_elec_rad, i_d_ref_a, i_q_ref_a);
  state.integrator_d_v = integrator_d_v;
  state.integrator_q_v = integrator_q_v;
  const gl30_foc_output_t out = gl30_foc_voltage_tick(
      &state, ia_a, ib_a, ic_a, vbus_v, dt, max_voltage_v, requested_d_v, requested_q_v);
  CHECK(out.valid == expected_valid, name);
  if (!expected_valid) {
    CHECK(out.duty_a == 0.5f && out.duty_b == 0.5f && out.duty_c == 0.5f,
          name);
  }
}

static void test_nonfinite_voltage_variation(nonfinite_field_t field, float val, const char *label) {
  const float ia_a = 0.02f;
  const float ib_a = 0.01f;
  const float ic_a = -0.03f;
  const float vbus_v = 12.0f;
  const float theta_elec_rad = 0.0f;
  const float i_d_ref_a = 0.1f;
  const float i_q_ref_a = 0.0f;
  const float integrator_d_v = 0.0f;
  const float integrator_q_v = 0.0f;
  const float requested_d_v = 0.10f;
  const float requested_q_v = 0.00f;
  const float dt = 1.0f / (float)GL30_PWM_HZ;
  const float max_voltage_v = 0.30f;

  float local_ia = ia_a;
  float local_ib = ib_a;
  float local_ic = ic_a;
  float local_vbus = vbus_v;
  float local_theta = theta_elec_rad;
  float local_i_d = i_d_ref_a;
  float local_i_q = i_q_ref_a;
  float local_id_integrator = integrator_d_v;
  float local_iq_integrator = integrator_q_v;
  float local_v_d = requested_d_v;
  float local_v_q = requested_q_v;
  float local_dt = dt;
  float local_max_voltage = max_voltage_v;

  switch (field) {
    case FIELD_IA: local_ia = val; break;
    case FIELD_IB: local_ib = val; break;
    case FIELD_IC: local_ic = val; break;
    case FIELD_VBUS: local_vbus = val; break;
    case FIELD_THETA_ELEC: local_theta = val; break;
    case FIELD_I_D_REF: local_i_d = val; break;
    case FIELD_I_Q_REF: local_i_q = val; break;
    case FIELD_I_D_INTEGRATOR: local_id_integrator = val; break;
    case FIELD_I_Q_INTEGRATOR: local_iq_integrator = val; break;
    case FIELD_V_D_CMD: local_v_d = val; break;
    case FIELD_V_Q_CMD: local_v_q = val; break;
    case FIELD_DT: local_dt = val; break;
    case FIELD_MAX_VOLTAGE: local_max_voltage = val; break;
    default: break;
  }

  assert_nonfinite_voltage_case(label, local_ia, local_ib, local_ic, local_vbus, local_theta,
                               local_i_d, local_i_q, local_id_integrator,
                               local_iq_integrator, local_v_d, local_v_q,
                               local_dt, local_max_voltage, false);
}

static void test_voltage_tick_rejects_nonfinite_inputs_and_commands(void) {
  const float specials[] = {NAN, INFINITY, -INFINITY};
  const char *labels[] = {"NaN", "+INF", "-INF"};

  for (size_t i = 0u; i < 3u; i++) {
    char name[128];
    snprintf(name, sizeof(name), "voltage_tick rejects ia=%s", labels[i]);
    test_nonfinite_voltage_variation(FIELD_IA, specials[i], name);
  }

  for (size_t i = 0u; i < 3u; i++) {
    char name[128];
    snprintf(name, sizeof(name), "voltage_tick rejects ib=%s", labels[i]);
    test_nonfinite_voltage_variation(FIELD_IB, specials[i], name);
  }

  for (size_t i = 0u; i < 3u; i++) {
    char name[128];
    snprintf(name, sizeof(name), "voltage_tick rejects ic=%s", labels[i]);
    test_nonfinite_voltage_variation(FIELD_IC, specials[i], name);
  }

  for (size_t i = 0u; i < 3u; i++) {
    char name[128];
    snprintf(name, sizeof(name), "voltage_tick rejects vbus=%s", labels[i]);
    test_nonfinite_voltage_variation(FIELD_VBUS, specials[i], name);
  }

  for (size_t i = 0u; i < 3u; i++) {
    char name[128];
    snprintf(name, sizeof(name), "voltage_tick rejects theta_elec=%s", labels[i]);
    test_nonfinite_voltage_variation(FIELD_THETA_ELEC, specials[i], name);
  }

  for (size_t i = 0u; i < 3u; i++) {
    char name[128];
    snprintf(name, sizeof(name), "voltage_tick rejects i_d_ref=%s", labels[i]);
    test_nonfinite_voltage_variation(FIELD_I_D_REF, specials[i], name);
  }

  for (size_t i = 0u; i < 3u; i++) {
    char name[128];
    snprintf(name, sizeof(name), "voltage_tick rejects i_q_ref=%s", labels[i]);
    test_nonfinite_voltage_variation(FIELD_I_Q_REF, specials[i], name);
  }

  for (size_t i = 0u; i < 3u; i++) {
    char name[128];
    snprintf(name, sizeof(name), "voltage_tick rejects integrator_d_v=%s", labels[i]);
    test_nonfinite_voltage_variation(FIELD_I_D_INTEGRATOR, specials[i], name);
  }

  for (size_t i = 0u; i < 3u; i++) {
    char name[128];
    snprintf(name, sizeof(name), "voltage_tick rejects integrator_q_v=%s", labels[i]);
    test_nonfinite_voltage_variation(FIELD_I_Q_INTEGRATOR, specials[i], name);
  }

  for (size_t i = 0u; i < 3u; i++) {
    char name[128];
    snprintf(name, sizeof(name), "voltage_tick rejects requested v_d=%s", labels[i]);
    test_nonfinite_voltage_variation(FIELD_V_D_CMD, specials[i], name);
  }

  for (size_t i = 0u; i < 3u; i++) {
    char name[128];
    snprintf(name, sizeof(name), "voltage_tick rejects requested v_q=%s", labels[i]);
    test_nonfinite_voltage_variation(FIELD_V_Q_CMD, specials[i], name);
  }

  for (size_t i = 0u; i < 3u; i++) {
    char name[128];
    snprintf(name, sizeof(name), "voltage_tick rejects dt=%s", labels[i]);
    test_nonfinite_voltage_variation(FIELD_DT, specials[i], name);
  }

  for (size_t i = 0u; i < 3u; i++) {
    char name[128];
    snprintf(name, sizeof(name), "voltage_tick rejects max_voltage=%s", labels[i]);
    test_nonfinite_voltage_variation(FIELD_MAX_VOLTAGE, specials[i], name);
  }

  assert_nonfinite_voltage_case(
      "voltage_tick accepts finite requested voltages", 0.02f, 0.01f, -0.03f, 12.0f, 0.0f,
      0.1f, 0.0f, 0.0f, 0.0f, 0.10f, 0.00f,
      1.0f / (float)GL30_PWM_HZ, 0.30f, true);
}

static void test_voltage_tick_uses_supplied_voltage_and_clears_integrators(void) {
  gl30_foc_state_t state = base_foc_state(0.3f, 0.2f, -0.15f);
  state.integrator_d_v = 1.5f;
  state.integrator_q_v = -2.5f;

  const gl30_foc_output_t out1 = gl30_foc_voltage_tick(&state, 0.30f, -0.01f, 0.01f, 12.0f,
                                                       1.0f / (float)GL30_PWM_HZ, 12.0f,
                                                       0.20f, -0.10f);
  CHECK(out1.valid, "voltage_tick stays valid for finite bounded command");
  CHECK(approx_eq(state.integrator_d_v, 0.0f), "voltage_tick clears d-axis integrator before output");
  CHECK(approx_eq(state.integrator_q_v, 0.0f), "voltage_tick clears q-axis integrator before output");
  CHECK(approx_eq(out1.v_d_v, 0.20f), "voltage_tick uses requested d-axis voltage");
  CHECK(approx_eq(out1.v_q_v, -0.10f), "voltage_tick uses requested q-axis voltage");

  const gl30_foc_output_t out2 = gl30_foc_voltage_tick(&state, 0.40f, 0.10f, -0.20f, 12.0f,
                                                       1.0f / (float)GL30_PWM_HZ, 12.0f,
                                                       0.20f, -0.10f);
  CHECK(out2.valid, "voltage_tick remains valid with high but safe measured currents");
  CHECK(approx_eq(state.integrator_d_v, 0.0f), "voltage_tick keeps integrators at zero after reuse");
  CHECK(approx_eq(state.integrator_q_v, 0.0f), "voltage_tick keeps integrators at zero on subsequent calls");
}

static void test_voltage_tick_still_respects_voltage_limit_and_overcurrent_path(void) {
  gl30_foc_state_t state_limit = base_foc_state(0.0f, 0.0f, 0.0f);
  const gl30_foc_output_t out_limit = gl30_foc_voltage_tick(
      &state_limit, 0.01f, 0.01f, 0.01f, 12.0f, 1.0f / (float)GL30_PWM_HZ, 0.30f,
      0.40f, 0.00f);
  CHECK(out_limit.valid, "voltage_tick accepts high but finite requested voltages");
  CHECK(hypotf(out_limit.v_d_v, out_limit.v_q_v) <= 0.30001f,
        "voltage_tick respects max voltage cap");
  CHECK(out_limit.duty_a >= GL30_TIM1_MIN_DUTY && out_limit.duty_a <= GL30_TIM1_MAX_DUTY &&
        out_limit.duty_b >= GL30_TIM1_MIN_DUTY && out_limit.duty_b <= GL30_TIM1_MAX_DUTY &&
        out_limit.duty_c >= GL30_TIM1_MIN_DUTY && out_limit.duty_c <= GL30_TIM1_MAX_DUTY,
        "voltage_tick clamps voltages inside PWM duty window");

  gl30_foc_state_t state_oc = base_foc_state(0.0f, 0.0f, 0.0f);
  const gl30_foc_output_t out_oc = gl30_foc_voltage_tick(
      &state_oc, 4.0f, 4.0f, 4.0f, 12.0f, 1.0f / (float)GL30_PWM_HZ, 0.30f,
      0.10f, 0.00f);
  CHECK(out_oc.overcurrent, "voltage_tick still detects overcurrent path");
  CHECK(!out_oc.valid, "voltage_tick overcurrent path remains invalid");
  CHECK(out_oc.duty_a == 0.5f && out_oc.duty_b == 0.5f && out_oc.duty_c == 0.5f,
        "voltage_tick overcurrent path keeps duties centered");
}

static void test_foc_observer_first_angle_sets_electrical() {
  const float wrapped_angle = wrap_pi_for_test(1.23456789f);
  const float expected_theta_elec = wrap_pi_for_test(
      GL30_PHASE_DIRECTION * ((float)GL30_MOTOR_POLE_PAIRS * (wrapped_angle - GL30_ELECTRICAL_ZERO_RAD)));

  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_foc_observer_tick_4k(&state, wrapped_angle, true);

  CHECK(state.observer_initialized, "observer first tick marks initialized");
  CHECK(approx_eq(state.theta_elec_rad, expected_theta_elec),
        "first valid observer tick sets theta_elec_rad by phase-direction/pole-pair formula");
}

static void test_observer_large_angle_has_bound_runtime() {
  const float huge_angle = 1.0e20f;
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_foc_observer_tick_4k(&state, huge_angle, true);
  CHECK(state.observer_initialized, "observer tick with huge finite angle completes");
}

static void test_current_tick_normal_small_q_ref() {
  gl30_foc_state_t state = base_foc_state(0.4f, 0.0f, 0.1f);
  const gl30_foc_output_t out =
      gl30_foc_current_tick(&state, 0.010f, -0.008f, 0.004f, 12.0f, 1.0f / (float)GL30_PWM_HZ, 12.0f);
  CHECK(out.valid, "normal finite currents at 12V produce valid output");
  CHECK(isfinite(out.duty_a) && isfinite(out.duty_b) && isfinite(out.duty_c),
        "normal finite output duty values are finite");
  CHECK(out.duty_a >= GL30_TIM1_MIN_DUTY && out.duty_a <= GL30_TIM1_MAX_DUTY,
        "normal output duty_a stays inside clamp window");
  CHECK(out.duty_b >= GL30_TIM1_MIN_DUTY && out.duty_b <= GL30_TIM1_MAX_DUTY,
        "normal output duty_b stays inside clamp window");
  CHECK(out.duty_c >= GL30_TIM1_MIN_DUTY && out.duty_c <= GL30_TIM1_MAX_DUTY,
        "normal output duty_c stays inside clamp window");
}

static void test_current_tick_overcurrent_blocked() {
  gl30_foc_state_t state = base_foc_state(0.4f, 0.0f, 0.1f);
  const gl30_foc_output_t out =
      gl30_foc_current_tick(&state, 3.0f, 3.0f, 3.0f, 12.0f, 1.0f / (float)GL30_PWM_HZ, 12.0f);
  CHECK(out.overcurrent, "overcurrent path is detected");
  CHECK(!out.valid, "overcurrent path is not marked valid");
  CHECK(out.duty_a == 0.5f && out.duty_b == 0.5f && out.duty_c == 0.5f,
        "overcurrent path keeps duty at 0.5 center");
}

int main(void) {
  test_foc_observer_first_angle_sets_electrical();
  test_current_tick_rejects_nonfinite_measurements_and_states();
  test_current_tick_rejects_invalid_dt_and_voltage_limits();
  test_current_tick_rejects_finite_huge_integrators_to_prevent_overflow();
  test_current_tick_20k_and_40k_dt_integrator_difference();
  test_current_tick_uses_max_voltage_cap_0p30();
  test_current_tick_normal_small_q_ref();
  test_current_tick_overcurrent_blocked();
  test_voltage_tick_rejects_nonfinite_inputs_and_commands();
  test_voltage_tick_uses_supplied_voltage_and_clears_integrators();
  test_voltage_tick_still_respects_voltage_limit_and_overcurrent_path();
  test_observer_large_angle_has_bound_runtime();
  test_foc_fixed_rotor_average_rl_closed_loop();

  if (g_tests_failed == 0) {
    printf("PASS: %d tests\n", g_tests_run);
    return 0;
  }
  printf("FAIL: %d / %d tests failed\n", g_tests_failed, g_tests_run);
  return 1;
}
