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

static void settle_haptic_ticks(gl30_foc_state_t *state, uint32_t ticks) {
  for (uint32_t i = 0u; i < ticks; i++) {
    gl30_haptic_tick_2k(state, true);
  }
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
  settle_haptic_ticks(&state, 41u);
  CHECK(state.detent_initialized, "detent: settles and initializes");

  state.theta_unwrapped_rad = 0.0f;
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

static void test_haptic_detent_stateful_griding_and_hysteresis(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t command = make_valid_command(2u, 0.06f);
  command.modeFlags = GL30_HAPTIC_DETENT;
  command.detentWidthRad = 1.0f;
  command.detentStrengthNm = 0.02f;
  command.dampingNmPerRadS = 0.0f;
  command.frictionNm = 0.0f;
  command.inertiaKgM2 = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &command), "detent griding: valid command accepted");

  state.observer_initialized = true;
  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.0f;
  state.theta_unwrapped_rad = 0.0f;
  settle_haptic_ticks(&state, 41u);
  CHECK(state.detent_initialized, "detent griding initializes state");
  CHECK(state.detent_position == 0, "detent griding starts at nearest origin index");
  CHECK(approx(state.detent_center_rad, 0.0f, 1e-5f), "detent griding starts at nearest origin center");

  state.theta_unwrapped_rad = 0.54f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.detent_position == 0, "detent griding retains index below +0.55 threshold");

  state.theta_unwrapped_rad = 0.55f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.detent_position == 0, "detent griding does not switch at exact +0.55 boundary");

  state.theta_unwrapped_rad = 1.49f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.detent_position == 1, "detent griding retains forward index above reverse threshold");

  state.theta_unwrapped_rad = 0.44f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.detent_position == 0, "detent griding reverses only below reverse threshold");

  state.theta_unwrapped_rad = 2.9f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.detent_position == 3, "detent griding jumps multiple grids in O(1)");

  state.theta_unwrapped_rad = -2.3f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.detent_position == -2, "detent griding handles negative unwrapped angles");

  state.theta_unwrapped_rad = -2.9f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.detent_position == -3, "detent griding jumps multiple grids in O(1) negative direction");

  const float center = state.detent_center_rad;
  state.theta_unwrapped_rad = center + 0.3f;
  gl30_haptic_tick_2k(&state, true);
  const float detent_plus = state.haptic_torque_nm;
  CHECK(detent_plus < 0.0f, "detent griding right of center restores toward center");

  state.theta_unwrapped_rad = center - 0.3f;
  gl30_haptic_tick_2k(&state, true);
  const float detent_minus = state.haptic_torque_nm;
  CHECK(detent_minus > 0.0f, "detent griding left of center restores toward center");
  CHECK(approx(-detent_plus, detent_minus, 1e-4f), "detent griding torque is center-symmetric");
}

static void test_haptic_detent_origin_width_relocate(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t first = make_valid_command(11u, 0.06f);
  first.modeFlags = GL30_HAPTIC_DETENT;
  first.targetPositionRad = 0.0f;
  first.detentWidthRad = 1.0f;
  first.detentStrengthNm = 0.02f;
  first.dampingNmPerRadS = 0.0f;
  first.frictionNm = 0.0f;
  first.inertiaKgM2 = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &first), "origin/width relocate: first detent command accepted");

  state.observer_initialized = true;
  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.0f;
  state.theta_unwrapped_rad = 2.3f;
  settle_haptic_ticks(&state, 41u);
  CHECK(state.detent_position == 2, "origin/width relocate: first command settles to index 2");
  CHECK(approx(state.detent_center_rad, 2.0f, 1e-5f), "origin/width relocate: first center is 2.0 rad");

  gl30_haptic_command_t second = first;
  second.commandNonce = 12u;
  second.targetPositionRad = 1.0f;
  second.detentWidthRad = 0.5f;
  CHECK(gl30_foc_apply_command(&state, &second), "origin/width relocate: new command accepted");
  CHECK(state.haptic_transition_pending, "origin/width relocate: command change starts transition");

  settle_haptic_ticks(&state, 41u);
  CHECK(state.detent_position == 3, "origin/width relocate: locator re-targets to new index");
  CHECK(approx(state.detent_center_rad, 2.5f, 1e-5f), "origin/width relocate: new center is 2.5 rad");
}

static void test_haptic_detent_huge_angle_index_overflow_to_zero(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t command = make_valid_command(13u, 0.06f);
  command.modeFlags = GL30_HAPTIC_DETENT;
  command.detentWidthRad = 1.0f;
  command.detentStrengthNm = 0.02f;
  command.dampingNmPerRadS = 0.0f;
  command.frictionNm = 0.0f;
  command.inertiaKgM2 = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &command), "overflow: detent command accepted");

  state.observer_initialized = true;
  state.theta_unwrapped_rad = 1.0e12f;
  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.0f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm == 0.0f, "overflow: immediate zero torque on out-of-range index");
  CHECK(!state.detent_initialized, "overflow: overflow clears detent initialization");
}

static void test_haptic_detent_endstop_grid_boundaries(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t command = make_valid_command(14u, 0.06f);
  command.modeFlags = GL30_HAPTIC_DETENT | GL30_HAPTIC_ENDSTOP;
  command.detentWidthRad = 0.4f;
  command.detentStrengthNm = 0.02f;
  command.targetPositionRad = 0.0f;
  command.endstopMinRad = -0.40f; /* exactly -1 * width */
  command.endstopMaxRad = 0.40f;  /* exactly +1 * width */
  CHECK(gl30_foc_apply_command(&state, &command),
        "boundary: detent+endstop command accepts exact grid boundaries");

  state.observer_initialized = true;
  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.0f;
  state.theta_unwrapped_rad = 1.0f;
  settle_haptic_ticks(&state, 41u);
  CHECK(state.detent_position == 1, "boundary: upper endstop boundary clamps to hi");

  state.theta_unwrapped_rad = -1.0f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.detent_position == -1, "boundary: lower endstop boundary clamps to lo");
}

static void test_haptic_detent_snap_boundary_exact_and_nextafter(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t command = make_valid_command(17u, 0.06f);
  command.modeFlags = GL30_HAPTIC_DETENT;
  command.detentWidthRad = 1.0f;
  command.detentStrengthNm = 0.02f;
  command.dampingNmPerRadS = 0.0f;
  command.frictionNm = 0.0f;
  command.inertiaKgM2 = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &command), "snap boundary: valid detent command accepted");

  state.observer_initialized = true;
  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.0f;
  settle_haptic_ticks(&state, 41u);
  CHECK(state.detent_position == 0, "snap boundary: starts at position zero");

  state.theta_unwrapped_rad = 0.55f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.detent_position == 0, "snap boundary: exact +0.55 does not switch");

  state.theta_unwrapped_rad = nextafterf(0.55f, INFINITY);
  CHECK(state.theta_unwrapped_rad > 0.55f,
        "snap boundary: nextafterf above +0.55 exists and is greater");
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.detent_position == 1, "snap boundary: nextafter(+0.55, +inf) crosses");
}

static void test_haptic_detent_nonzero_deadzone_bounds(void) {
  {
    gl30_foc_state_t state = {0};
    gl30_foc_init(&state);
    gl30_haptic_command_t command = make_valid_command(18u, 0.06f);
    command.modeFlags = GL30_HAPTIC_DETENT;
    command.detentWidthRad = 1.0f;
    command.detentStrengthNm = 0.02f;
    command.dampingNmPerRadS = 0.0f;
    command.frictionNm = 0.0f;
    command.inertiaKgM2 = 0.0f;
    CHECK(gl30_foc_apply_command(&state, &command), "deadzone width1: valid detent command accepted");

    state.observer_initialized = true;
    state.velocity_rad_s = 0.0f;
    state.acceleration_rad_s2 = 0.0f;
    settle_haptic_ticks(&state, 41u);

    state.theta_unwrapped_rad = 0.01f;
    gl30_haptic_tick_2k(&state, true);
    CHECK(fabsf(state.haptic_torque_nm) < 1e-6f,
          "deadzone width1: ±0.01rad is inside deadzone and zero");

    state.theta_unwrapped_rad = -0.01f;
    gl30_haptic_tick_2k(&state, true);
    CHECK(fabsf(state.haptic_torque_nm) < 1e-6f,
          "deadzone width1: negative ±0.01rad is inside deadzone and zero");

    state.theta_unwrapped_rad = 0.03f;
    gl30_haptic_tick_2k(&state, true);
    CHECK(state.haptic_torque_nm < 0.0f && fabsf(state.haptic_torque_nm) > 1e-6f,
          "deadzone width1: +0.03rad restores with negative torque");
  }
  {
    gl30_foc_state_t state = {0};
    gl30_foc_init(&state);
    gl30_haptic_command_t command = make_valid_command(19u, 0.06f);
    command.modeFlags = GL30_HAPTIC_DETENT;
    command.detentWidthRad = 0.1f;
    command.detentStrengthNm = 0.02f;
    command.dampingNmPerRadS = 0.0f;
    command.frictionNm = 0.0f;
    command.inertiaKgM2 = 0.0f;
    CHECK(gl30_foc_apply_command(&state, &command), "deadzone width0.1: valid detent command accepted");

    state.observer_initialized = true;
    state.velocity_rad_s = 0.0f;
    state.acceleration_rad_s2 = 0.0f;
    settle_haptic_ticks(&state, 41u);

    state.theta_unwrapped_rad = 0.005f;
    gl30_haptic_tick_2k(&state, true);
    CHECK(fabsf(state.haptic_torque_nm) < 1e-6f,
          "deadzone width0.1: ±0.005rad is inside deadzone and zero");

    state.theta_unwrapped_rad = -0.005f;
    gl30_haptic_tick_2k(&state, true);
    CHECK(fabsf(state.haptic_torque_nm) < 1e-6f,
          "deadzone width0.1: negative ±0.005rad is inside deadzone and zero");
  }
}

static void test_haptic_mode_switch_caps_transition_first_tick_under_speed_limit(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t first = make_valid_command(15u, 0.06f);
  first.modeFlags = GL30_HAPTIC_DETENT;
  first.detentWidthRad = 1.0f;
  first.detentStrengthNm = 0.02f;
  first.dampingNmPerRadS = 0.0f;
  first.frictionNm = 0.0f;
  first.inertiaKgM2 = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &first), "speed-limit: initial detent command accepted");

  state.observer_initialized = true;
  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.0f;
  state.theta_unwrapped_rad = 0.45f;
  settle_haptic_ticks(&state, 41u);
  CHECK(fabsf(state.haptic_torque_nm) > GL30_SELF_DRIVE_LIMIT_NM,
        "speed-limit: settled torque starts above self-drive clamp");

  gl30_haptic_command_t second = first;
  second.commandNonce = 16u;
  second.modeFlags = GL30_HAPTIC_POSITION;
  second.targetPositionRad = 0.0f;
  second.activeSpeedLimitRadS = 1.0f;
  CHECK(gl30_foc_apply_command(&state, &second), "speed-limit: mode switch command accepted");
  state.velocity_rad_s = 2.0f; /* above activeSpeedLimit */
  gl30_haptic_tick_2k(&state, true);
  CHECK(fabsf(state.haptic_torque_nm) <= GL30_SELF_DRIVE_LIMIT_NM + 1e-6f,
        "speed-limit: transition first tick clamps to self-drive limit");
}

static void test_haptic_detent_mode_transition_blend(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t command_a = make_valid_command(3u, 0.06f);
  gl30_haptic_command_t command_b = make_valid_command(4u, 0.06f);

  command_a.modeFlags = GL30_HAPTIC_DETENT;
  command_a.detentWidthRad = 1.0f;
  command_a.detentStrengthNm = 0.01f;
  command_a.dampingNmPerRadS = 0.0f;
  command_a.frictionNm = 0.0f;
  command_a.inertiaKgM2 = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &command_a), "detent blend: first command accepted");

  state.observer_initialized = true;
  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.0f;
  state.theta_unwrapped_rad = 0.60f;
  settle_haptic_ticks(&state, 41u);
  const float old_torque = state.haptic_torque_nm;

  command_b.modeFlags = GL30_HAPTIC_DETENT;
  command_b.detentWidthRad = 1.0f;
  command_b.detentStrengthNm = 0.06f;
  command_b.userTorqueLimitNm = 0.005f;
  command_b.dampingNmPerRadS = 0.0f;
  command_b.frictionNm = 0.0f;
  command_b.inertiaKgM2 = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &command_b), "detent blend: second command accepted");
  CHECK(state.haptic_transition_pending, "detent blend starts transition");

  const float expected_first_tick =
      fabsf(old_torque) > 0.005f ? (old_torque < 0.0f ? -0.005f : 0.005f) : old_torque;
  gl30_haptic_tick_2k(&state, true);
  CHECK(approx(state.haptic_torque_nm, expected_first_tick, 1e-5f),
        "detent blend first tick uses previous torque unless lower capped");

  for (uint32_t i = 0u; i < 39u; ++i) {
    gl30_haptic_tick_2k(&state, true);
  }
  CHECK(fabsf(state.haptic_torque_nm - old_torque) > 1e-3f,
        "detent blend transitions over multiple ticks");

  gl30_foc_state_t expected = {0};
  gl30_foc_init(&expected);
  CHECK(gl30_foc_apply_command(&expected, &command_b), "detent blend expected-state setup");
  expected.observer_initialized = true;
  expected.theta_unwrapped_rad = 0.60f;
  settle_haptic_ticks(&expected, 41u);

  gl30_haptic_tick_2k(&state, true);
  CHECK(approx(state.haptic_torque_nm, expected.haptic_torque_nm, 1e-5f),
        "detent blend reaches new target by 41st tick");
  CHECK(!state.haptic_transition_pending, "detent blend ends transition");
}

static void test_haptic_detent_reapply_same_nonce_no_restart(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t command = make_valid_command(5u, 0.06f);

  command.modeFlags = GL30_HAPTIC_DETENT;
  command.detentWidthRad = 0.8f;
  command.detentStrengthNm = 0.02f;
  command.dampingNmPerRadS = 0.0f;
  command.frictionNm = 0.0f;
  command.inertiaKgM2 = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &command), "detent stable: valid command accepted");

  state.observer_initialized = true;
  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.0f;
  state.theta_unwrapped_rad = 0.2f;
  settle_haptic_ticks(&state, 41u);

  const float stable_torque = state.haptic_torque_nm;
  const int32_t stable_position = state.detent_position;
  command.commandNonce = 6u;
  CHECK(gl30_foc_apply_command(&state, &command), "detent stable: re-apply same effective command accepted");
  CHECK(!state.haptic_transition_pending, "detent same nonce command does not restart transition");
  CHECK(state.detent_position == stable_position, "detent same command preserves position");
  CHECK(approx(state.haptic_torque_nm, stable_torque, 1e-5f), "detent same command keeps settled torque");
}

static void test_haptic_detent_forcezero_and_clear_paths(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t command = make_valid_command(7u, 0.06f);
  command.modeFlags = GL30_HAPTIC_DETENT;
  command.detentWidthRad = 1.0f;
  command.detentStrengthNm = 0.02f;
  command.dampingNmPerRadS = 0.0f;
  command.frictionNm = 0.0f;
  command.inertiaKgM2 = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &command), "detent clear: valid command accepted");

  state.observer_initialized = true;
  state.theta_unwrapped_rad = 0.8f;
  state.velocity_rad_s = 0.0f;
  state.acceleration_rad_s2 = 0.0f;
  settle_haptic_ticks(&state, 41u);

  command.detentStrengthNm = 0.04f;
  CHECK(gl30_foc_apply_command(&state, &command), "detent clear: change command starts transition");
  CHECK(state.haptic_transition_pending, "detent clear: transition started");
  CHECK(!state.detent_initialized, "detent clear: transition clears locator state");

  gl30_foc_force_zero(&state);
  CHECK(state.haptic_transition_pending, "detent clear: force_zero keeps pending reset state");
  CHECK(!state.detent_initialized, "detent clear: force_zero clears state");
  CHECK(state.haptic_torque_nm == 0.0f, "detent clear: force_zero clears haptic torque");

  gl30_haptic_tick_2k(&state, false);
  CHECK(!state.detent_initialized, "detent clear: disallowed tick keeps cleared state");
  CHECK(state.haptic_transition_pending, "detent clear: disallowed tick keeps reset transition state");

  state.observer_initialized = false;
  state.torque_command_nm = 1.0f;
  state.haptic_torque_nm = 1.0f;
  state.i_q_ref_a = 1.0f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(!state.detent_initialized, "detent clear: observer-invalid keeps cleared state");
  CHECK(state.haptic_transition_pending, "detent clear: observer-invalid keeps reset transition state");

  state.observer_initialized = true;
  state.theta_unwrapped_rad = 0.0f;
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.detent_initialized, "detent clear: re-enable enters initialized state from zero");
}

static void test_haptic_telemetry_outputs_detent_position(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  gl30_haptic_command_t command = make_valid_command(8u, 0.06f);
  command.modeFlags = GL30_HAPTIC_DETENT;
  command.detentWidthRad = 0.5f;
  command.detentStrengthNm = 0.02f;
  command.dampingNmPerRadS = 0.0f;
  command.frictionNm = 0.0f;
  command.inertiaKgM2 = 0.0f;
  CHECK(gl30_foc_apply_command(&state, &command), "detent telemetry: valid command accepted");

  state.observer_initialized = true;
  state.theta_unwrapped_rad = 1.15f;
  settle_haptic_ticks(&state, 41u);
  const float expected_sub = (state.theta_unwrapped_rad - state.detent_center_rad) /
                            state.active_command.detentWidthRad;

  gl30_motor_state_fast_t telemetry = {0};
  gl30_foc_make_telemetry(&state, 0u, 0u, 0u, 0u, 0u, 0u, &telemetry);
  CHECK(telemetry.logicalPosition == state.detent_position, "detent telemetry emits logicalPosition");
  CHECK(approx(telemetry.subPosition, expected_sub, 1e-4f),
        "detent telemetry emits normalized subPosition");
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
  settle_haptic_ticks(&state, 41u);

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
  settle_haptic_ticks(&state, 41u);
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
  settle_haptic_ticks(&state, 41u);

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
  settle_haptic_ticks(&state, 41u);

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
  settle_haptic_ticks(&state, 41u);

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
  settle_haptic_ticks(&state, 41u);

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

  state.theta_unwrapped_rad = INFINITY;
  state.torque_command_nm = 1.0f;
  state.haptic_torque_nm = 1.0f;
  state.i_q_ref_a = 1.0f;
  CHECK(gl30_foc_apply_command(&state, &command), "inactive: re-apply valid command accepted");
  gl30_haptic_tick_2k(&state, true);
  CHECK(state.torque_command_nm == 0.0f, "tick(INF observer angle) disables torque command");
  CHECK(state.haptic_torque_nm == 0.0f, "tick(INF observer angle) clears haptic torque");
  CHECK(state.i_q_ref_a == 0.0f, "tick(INF observer angle) clears Iq reference");
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

  command = make_valid_command(13u, 0.06f);
  command.modeFlags = GL30_HAPTIC_DETENT;
  command.detentWidthRad = 1.0e-6f;
  CHECK(!gl30_foc_apply_command(&state, &command),
        "apply rejects detent width at/below minimum");

  command = make_valid_command(14u, 0.06f);
  command.modeFlags = GL30_HAPTIC_DETENT | GL30_HAPTIC_ENDSTOP;
  command.detentWidthRad = 0.4f;
  command.endstopMinRad = 0.05f;
  command.endstopMaxRad = 0.15f;
  CHECK(!gl30_foc_apply_command(&state, &command),
        "apply rejects detent command with no legal endstop grid");
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
    state.observer_initialized = true;
    settle_haptic_ticks(&state, 41u);
    const float iq = bench_haptic_sample(&state, 0.0f, 0.2f, 0.0f, 1.0f);
    CHECK(isfinite(iq), "bench preset sample returns finite Iq for valid inputs");
    CHECK(fabsf(iq) <= 0.100f + 1e-6f, "bench sample clamps Iq within ±0.10A");
  }

  CHECK(bench_haptic_configure(&state, BENCH_HAPTIC_SPRING, 0.0f),
        "bench spring preset re-configurable");
  state.observer_initialized = true;
  settle_haptic_ticks(&state, 41u);
  float spring_iq_plus = bench_haptic_sample(&state, 0.08f, 0.0f, 0.0f, 1.0f);
  float spring_torque_plus = state.haptic_torque_nm;
  float spring_iq_minus = bench_haptic_sample(&state, 0.08f, 0.0f, 0.0f, -1.0f);
  CHECK(isfinite(spring_iq_plus) && isfinite(spring_iq_minus), "spring phase directions return finite Iq");
  CHECK(approx(-spring_iq_plus, spring_iq_minus, 1e-5f),
        "bench spring phase -1 flips electrical Iq");
  CHECK(spring_torque_plus < 0.0f && state.haptic_torque_nm < 0.0f,
        "bench spring keeps restoring direction in encoder coordinates");

  CHECK(bench_haptic_configure(&state, BENCH_HAPTIC_DAMPING, 0.0f), "bench damping preset re-configurable");
  state.observer_initialized = true;
  settle_haptic_ticks(&state, 41u);
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
  test_haptic_detent_stateful_griding_and_hysteresis();
  test_haptic_detent_origin_width_relocate();
  test_haptic_detent_huge_angle_index_overflow_to_zero();
  test_haptic_detent_endstop_grid_boundaries();
  test_haptic_detent_snap_boundary_exact_and_nextafter();
  test_haptic_detent_nonzero_deadzone_bounds();
  test_haptic_mode_switch_caps_transition_first_tick_under_speed_limit();
  test_haptic_detent_mode_transition_blend();
  test_haptic_detent_reapply_same_nonce_no_restart();
  test_haptic_detent_forcezero_and_clear_paths();
  test_haptic_telemetry_outputs_detent_position();
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
