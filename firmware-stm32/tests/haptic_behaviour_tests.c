#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>

#include "config/board_config.h"
#include "bench/NUCLEO_G474RE_FOC/Core/Inc/bench_haptics.h"
#include "control/foc.h"
#include "haptics/haptics.h"
#include "protocol/v6_protocol.h"

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define CHECK(cond, msg)                                                           \
  do {                                                                            \
    g_tests_run++;                                                                \
    if (!(cond)) {                                                                \
      g_tests_failed++;                                                           \
      fprintf(stderr, "[FAIL] %s\n", (msg));                                     \
    }                                                                             \
  } while (0)

static bool approx(float lhs, float rhs, float eps) {
  return fabsf(lhs - rhs) <= eps;
}

static gl30_haptic_command_t make_valid_command(uint32_t nonce, float user_torque_limit) {
  return (gl30_haptic_command_t){
      .profileId = 11u,
      .commandNonce = nonce,
      .targetPositionRad = 0.0f,
      .targetVelocityRadS = 0.0f,
      .detentWidthRad = 0.40f,
      .detentStrengthNm = 0.01f,
      .endstopMinRad = -0.50f,
      .endstopMaxRad = 0.50f,
      .endstopStrengthNm = 0.02f,
      .dampingNmPerRadS = 0.02f,
      .inertiaKgM2 = 0.01f,
      .frictionNm = 0.01f,
      .userTorqueLimitNm = user_torque_limit,
      .activeSpeedLimitRadS = 0.0f,
      .modeFlags = GL30_HAPTIC_POSITION | GL30_HAPTIC_VELOCITY | GL30_HAPTIC_FRICTION |
                   GL30_HAPTIC_INERTIA | GL30_HAPTIC_ENDSTOP | GL30_HAPTIC_DETENT,
  };
}

static void test_haptic_detent_primitive(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t command = make_valid_command(1u, 0.06f);
  command.modeFlags = GL30_HAPTIC_DETENT;
  command.detentWidthRad = 0.40f;
  command.detentStrengthNm = 0.020f;
  command.dampingNmPerRadS = 0.0f;
  command.frictionNm = 0.0f;
  command.inertiaKgM2 = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &command), "detent: valid command accepted");

  state.observer_initialized = true;
  state.theta_unwrapped_rad = 0.0f;
  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.0f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(fabsf(state.haptic_torque_nm) < 1e-5f, "detent center torque is near zero");

  state.theta_unwrapped_rad = 0.10f; /* +quarter period offset */
  gl30_haptic_tick_2k(&state, true);
  const float detent_plus = state.haptic_torque_nm;
  CHECK(detent_plus < -1e-4f, "detent +quarter produces restoring negative torque");

  state.theta_unwrapped_rad = -0.10f; /* -quarter period offset */
  gl30_haptic_tick_2k(&state, true);
  const float detent_minus = state.haptic_torque_nm;
  CHECK(detent_minus > 1e-4f, "detent -quarter produces restoring positive torque");

  state.theta_unwrapped_rad = 0.50f; /* +one extra period +quarter offset */
  gl30_haptic_tick_2k(&state, true);
  const float detent_period_shift = state.haptic_torque_nm;
  CHECK(approx(detent_period_shift, detent_plus, 1e-4f),
        "detent torque periodic after one width");
}

static void test_haptic_spring_primitive(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t command = make_valid_command(2u, 0.06f);
  command.modeFlags = GL30_HAPTIC_POSITION;
  command.targetPositionRad = 0.25f;
  command.endstopStrengthNm = 0.020f;
  command.dampingNmPerRadS = 0.0f;
  command.frictionNm = 0.0f;
  command.inertiaKgM2 = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &command), "spring: valid command accepted");

  state.observer_initialized = true;
  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.0f;

  state.theta_unwrapped_rad = 0.35f; /* above target => negative spring */
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm < 0.0f, "spring above target pulls back (negative torque)");

  state.theta_unwrapped_rad = 0.15f; /* below target => positive spring */
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm > 0.0f, "spring below target pushes forward (positive torque)");
}

static void test_haptic_endstop_primitive(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t command = make_valid_command(3u, 0.06f);
  command.modeFlags = GL30_HAPTIC_ENDSTOP;
  command.endstopMinRad = -0.30f;
  command.endstopMaxRad = 0.30f;
  command.endstopStrengthNm = 0.050f;
  command.dampingNmPerRadS = 0.0f;
  command.frictionNm = 0.0f;
  command.inertiaKgM2 = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &command), "endstop: valid command accepted");

  state.observer_initialized = true;
  state.theta_unwrapped_rad = 0.0f;
  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.0f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(fabsf(state.haptic_torque_nm) < 1e-6f, "endstop inside interval has no restoring torque");

  state.theta_unwrapped_rad = -0.40f; /* left outside => inward positive */
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm > 0.0f, "endstop outside left pushes inward");

  state.theta_unwrapped_rad = 0.40f; /* right outside => inward negative */
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm < 0.0f, "endstop outside right pushes inward");
}

static void test_haptic_damping_friction_inertia(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);

  gl30_haptic_command_t damping = make_valid_command(4u, 0.06f);
  damping.modeFlags = 0u;
  damping.dampingNmPerRadS = 0.020f;
  damping.frictionNm = 0.0f;
  damping.inertiaKgM2 = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &damping), "damping: valid command accepted");
  state.observer_initialized = true;
  state.theta_unwrapped_rad = 0.0f;

  state.velocity_rad_s = 0.50f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm * state.velocity_rad_s <= 1e-6f, "damping torque opposes positive velocity");

  state.velocity_rad_s = -0.50f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm * state.velocity_rad_s <= 1e-6f, "damping torque opposes negative velocity");

  gl30_haptic_command_t friction = make_valid_command(5u, 0.06f);
  friction.modeFlags = GL30_HAPTIC_FRICTION;
  friction.frictionNm = 0.020f;
  friction.dampingNmPerRadS = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &friction), "friction: valid command accepted");

  state.velocity_rad_s = 0.30f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm * state.velocity_rad_s <= 1e-6f, "friction torque opposes positive velocity");

  state.velocity_rad_s = -0.30f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm * state.velocity_rad_s <= 1e-6f, "friction torque opposes negative velocity");

  gl30_haptic_command_t inertia = make_valid_command(6u, 0.06f);
  inertia.modeFlags = GL30_HAPTIC_INERTIA;
  inertia.inertiaKgM2 = 0.010f;
  inertia.dampingNmPerRadS = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &inertia), "inertia: valid command accepted");

  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.5f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm < 0.0f, "inertia accelerates positive -> torque negative");

  state.acceleration_rad_s2 = -0.5f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm > 0.0f, "inertia accelerates negative -> torque positive");
}

static void test_haptic_velocity_tracking(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);

  gl30_haptic_command_t command = make_valid_command(7u, 0.06f);
  command.modeFlags = GL30_HAPTIC_VELOCITY;
  command.targetVelocityRadS = 1.0f;
  command.dampingNmPerRadS = 0.020f;
  CHECK(gl30_foc_apply_command(&state, &command), "velocity: valid command accepted");
  state.observer_initialized = true;

  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.0f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm * (command.targetVelocityRadS - state.velocity_rad_s) >= -1e-6f,
        "velocity tracking torque aligns with positive error");

  state.velocity_rad_s = 2.0f;
  state.acceleration_rad_s2 = 0.0f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm * (command.targetVelocityRadS - state.velocity_rad_s) >= -1e-6f,
        "velocity tracking torque aligns with negative error");
}

static void test_haptic_user_torque_and_current_limit(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);

  gl30_haptic_command_t command = make_valid_command(8u, 0.005f);
  command.modeFlags = GL30_HAPTIC_POSITION;
  command.targetPositionRad = 0.0f;
  command.endstopStrengthNm = 0.5f;
  command.endstopMinRad = -1.0f;
  command.endstopMaxRad = 1.0f;
  CHECK(gl30_foc_apply_command(&state, &command), "user limit: valid command accepted");

  state.observer_initialized = true;
  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.0f;
  state.theta_unwrapped_rad = 1.0f; /* force large position error */

  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm <= 0.005f + 1e-6f && state.haptic_torque_nm >= -0.005f - 1e-6f,
        "user torque limit clamps haptic torque");

  CHECK(fabsf(state.i_q_ref_a) <= GL30_CURRENT_SOFT_LIMIT_A + 1e-6f,
        "current soft limit caps requested Iq");
}

static void test_haptic_tick_disallow_when_inactive(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t command = make_valid_command(9u, 0.06f);
  command.dampingNmPerRadS = 0.02f;
  CHECK(gl30_foc_apply_command(&state, &command), "inactive: valid command accepted");

  state.observer_initialized = true;
  state.torque_command_nm = 1.0f;
  state.haptic_torque_nm = 1.0f;
  state.i_q_ref_a = 1.0f;

  gl30_haptic_tick_2k(&state, false);
  CHECK(state.torque_command_nm == 0.0f, "tick(allowed=false) disables torque command");
  CHECK(state.haptic_torque_nm == 0.0f, "tick(allowed=false) clears haptic torque");
  CHECK(state.i_q_ref_a == 0.0f, "tick(allowed=false) clears Iq reference");

  state.observer_initialized = false;
  state.torque_command_nm = 1.0f;
  state.haptic_torque_nm = 1.0f;
  state.i_q_ref_a = 1.0f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.torque_command_nm == 0.0f, "tick(observer=false) disables torque command");
  CHECK(state.haptic_torque_nm == 0.0f, "tick(observer=false) clears haptic torque");
  CHECK(state.i_q_ref_a == 0.0f, "tick(observer=false) clears Iq reference");
}

static void test_foc_apply_command_rejects_invalid_values(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t command = make_valid_command(10u, 0.06f);
  const uint32_t before = state.rejected_commands;

  command.targetPositionRad = NAN;
  CHECK(!gl30_foc_apply_command(&state, &command), "apply rejects NaN target position");
  CHECK(state.rejected_commands == before + 1u, "NaN target increments rejected_commands");

  command = make_valid_command(11u, 0.06f);
  command.targetPositionRad = 0.0f;
  command.detentWidthRad = INFINITY;
  CHECK(!gl30_foc_apply_command(&state, &command), "apply rejects infinite detent width");
  CHECK(state.rejected_commands == before + 2u, "infinite detent increments rejected_commands");

  command = make_valid_command(12u, 0.06f);
  command.userTorqueLimitNm = -0.01f;
  CHECK(!gl30_foc_apply_command(&state, &command), "apply rejects negative user torque limit");
  CHECK(state.rejected_commands == before + 3u, "negative user limit increments rejected_commands");
}

static void test_haptic_bench_presets_and_phase_direction(void) {
  const bench_haptic_kind_t kinds[] = {
      BENCH_HAPTIC_FREE,
      BENCH_HAPTIC_DAMPING,
      BENCH_HAPTIC_DETENT,
      BENCH_HAPTIC_SPRING,
      BENCH_HAPTIC_ENDSTOP,
      BENCH_HAPTIC_FRICTION,
      BENCH_HAPTIC_INERTIA,
      BENCH_HAPTIC_VELOCITY,
  };
  gl30_foc_state_t state = {0};

  for (size_t i = 0u; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
    CHECK(bench_haptic_configure(&state, kinds[i], 0.0f), "bench configure accepts all preset kinds");
    const float iq = bench_haptic_sample(&state, 0.0f, 0.2f, 0.0f, 1.0f);
    CHECK(isfinite(iq), "bench preset sample returns finite Iq for valid inputs");
    CHECK(fabsf(iq) <= 0.100f + 1e-6f, "bench sample clamps Iq within ±0.10A");
  }

  CHECK(bench_haptic_configure(&state, BENCH_HAPTIC_SPRING, 0.0f),
        "bench spring preset re-configurable");
  float spring_iq_plus = bench_haptic_sample(&state, 0.08f, 0.0f, 0.0f, 1.0f);
  float spring_torque_plus = state.haptic_torque_nm;
  float spring_iq_minus = bench_haptic_sample(&state, 0.08f, 0.0f, 0.0f, -1.0f);
  CHECK(isfinite(spring_iq_plus) && isfinite(spring_iq_minus), "spring phase directions return finite Iq");
  CHECK(approx(-spring_iq_plus, spring_iq_minus, 1e-5f),
        "bench spring phase -1 flips electrical Iq");
  CHECK(spring_torque_plus < 0.0f && state.haptic_torque_nm < 0.0f,
        "bench spring keeps restoring direction in encoder coordinates");

  CHECK(bench_haptic_configure(&state, BENCH_HAPTIC_DAMPING, 0.0f), "bench damping preset re-configurable");
  float damping_iq_plus = bench_haptic_sample(&state, 0.0f, 1.0f, 0.0f, 1.0f);
  float damping_torque_plus = state.haptic_torque_nm;
  float damping_iq_minus = bench_haptic_sample(&state, 0.0f, 1.0f, 0.0f, -1.0f);
  CHECK(isfinite(damping_iq_plus) && isfinite(damping_iq_minus), "damping phase directions return finite Iq");
  CHECK(fabsf(damping_iq_minus) <= 0.100f + 1e-6f && fabsf(damping_iq_plus) <= 0.100f + 1e-6f,
        "bench damping sample Iq remains bounded");
  CHECK(approx(-damping_iq_plus, damping_iq_minus, 1e-5f),
        "bench damping phase -1 flips electrical Iq");
  CHECK(damping_torque_plus < 0.0f && state.haptic_torque_nm < 0.0f,
        "bench damping keeps encoder-coordinate damping sign");
}

static void test_bench_sample_invalid_inputs(void) {
  gl30_foc_state_t state = {0};
  CHECK(!isfinite(bench_haptic_sample(&state, 0.0f, 0.1f, 0.0f, 2.0f)),
        "bench sample rejects invalid phase direction");
  CHECK(!isfinite(bench_haptic_sample(&state, NAN, 0.1f, 0.0f, 1.0f)),
        "bench sample rejects NaN inputs");
  CHECK(!isfinite(bench_haptic_sample(NULL, 0.0f, 0.1f, 0.0f, 1.0f)),
        "bench sample rejects null state");
}

int main(void) {
  test_haptic_detent_primitive();
  test_haptic_spring_primitive();
  test_haptic_endstop_primitive();
  test_haptic_damping_friction_inertia();
  test_haptic_velocity_tracking();
  test_haptic_user_torque_and_current_limit();
  test_haptic_tick_disallow_when_inactive();
  test_foc_apply_command_rejects_invalid_values();
  test_haptic_bench_presets_and_phase_direction();
  test_bench_sample_invalid_inputs();

  if (g_tests_failed == 0) {
    printf("PASS: %d tests\n", g_tests_run);
    return 0;
  }
  printf("FAIL: %d / %d tests failed\n", g_tests_failed, g_tests_run);
  return 1;
}
