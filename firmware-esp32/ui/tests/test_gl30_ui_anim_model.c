#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>

#include "gl30_ui_anim_model.h"

static int g_passed;
static int g_failed;

static void test_fail(const char* name, const char* detail) {
  ++g_failed;
  printf("FAIL %-55s %s\n", name, detail);
  return;
}

#define EXPECT_TRUE(expr, name) \
  do { \
    if (!(expr)) { \
      test_fail((name), "condition failed: " #expr); \
      return; \
    } \
  } while (0)

#define EXPECT_INT_EQ(actual, expected, name) \
  do { \
    if ((actual) != (expected)) { \
      char __buf[200]; \
      snprintf(__buf, sizeof(__buf), "expected %d got %d", (int)(expected), (int)(actual)); \
      test_fail((name), __buf); \
      return; \
    } \
  } while (0)

#define EXPECT_FLOAT_EQ(actual, expected, eps, name) \
  do { \
    if (!isfinite((double)(actual)) || !isfinite((double)(expected)) || \
        fabsf((actual) - (expected)) > (eps)) { \
      char __buf[200]; \
      snprintf(__buf, sizeof(__buf), "expected %.8f got %.8f", (float)(expected), (float)(actual)); \
      test_fail((name), __buf); \
      return; \
    } \
  } while (0)

#define EXPECT_STR_EQ(actual, expected, name) \
  do { \
    if (strcmp((actual), (expected)) != 0) { \
      test_fail((name), "string mismatch"); \
      return; \
    } \
  } while (0)

#define EXPECT_MEMEQ(actual, expected, n, name) \
  do { \
    if (memcmp((actual), (expected), (n)) != 0) { \
      test_fail((name), "binary state changed"); \
      return; \
    } \
  } while (0)

static float smooth_step(float current, float target, uint32_t elapsed_ms, uint32_t tau_ms) {
  if (tau_ms <= 0u || elapsed_ms == 0u) {
    return current;
  }
  return current + (target - current) * (1.0f - expf(-(float)elapsed_ms / (float)tau_ms));
}

static void test_defaults(void) {
  const char* name = "init defaults are deterministic";
  gl30_ui_anim_model_t model;
  memset(&model, 0xA5, sizeof(model));

  gl30_ui_anim_model_init(&model);

  EXPECT_FLOAT_EQ(model.smoothed_ratio, GL30_UI_MODE_VOLUME_DEFAULT / GL30_UI_MODE_VOLUME_MAX, 1e-6f, name);
  EXPECT_FLOAT_EQ(model.wake_level, 0.0f, 0.0f, name);
  EXPECT_INT_EQ(model.wake_hold_ms, 0u, name);
  EXPECT_INT_EQ(model.last_mode, GL30_UI_MODE_VOLUME, name);
  EXPECT_TRUE(model.initialized, name);
}

static void test_dt_zero_should_not_advance(void) {
  const char* name = "tick dt=0 should not change value interpolation";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;
  gl30_ui_anim_input_t in = {
    .volume = 100.0f,
    .timer_minutes = 120.0f,
    .mode = GL30_UI_MODE_VOLUME,
    .connected = true,
  };

  gl30_ui_anim_model_init(&model);
  gl30_ui_anim_model_tick(&model, &in, 0u, &out);

  EXPECT_FLOAT_EQ(model.smoothed_ratio, GL30_UI_MODE_VOLUME_DEFAULT / GL30_UI_MODE_VOLUME_MAX, 1e-6f, name);
}

static void test_elapsed_is_clamped_to_200ms(void) {
  const char* name = "elapsed_ms should clamp at GL30_UI_MAX_TICK_MS";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;
  gl30_ui_anim_input_t in = {
    .volume = 100.0f,
    .mode = GL30_UI_MODE_VOLUME,
    .connected = true,
  };

  gl30_ui_anim_model_init(&model);
  gl30_ui_anim_model_tick(&model, &in, 1000u, &out);
  const float expected = smooth_step(GL30_UI_MODE_VOLUME_DEFAULT / GL30_UI_MODE_VOLUME_MAX,
                                    1.0f,
                                    GL30_UI_MAX_TICK_MS,
                                    GL30_UI_VALUE_SMOOTH_MS);
  EXPECT_FLOAT_EQ(out.value_ratio, expected, 1e-5f, name);
}

static void test_smooth_formulas_and_angle(void) {
  const char* name = "smooth wake/ratio and angle formula";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;
  gl30_ui_anim_input_t in = {
    .volume = 100.0f,
    .mode = GL30_UI_MODE_VOLUME,
    .connected = true,
  };

  gl30_ui_anim_model_init(&model);
  gl30_ui_anim_model_tick(&model, &in, 120u, &out);

  const float expected_ratio = smooth_step(GL30_UI_MODE_VOLUME_DEFAULT / GL30_UI_MODE_VOLUME_MAX,
                                          1.0f,
                                          120u,
                                          GL30_UI_VALUE_SMOOTH_MS);
  EXPECT_FLOAT_EQ(out.value_ratio, expected_ratio, 1e-5f, name);

  const float expected_angle = GL30_UI_DIAL_MIN_ANGLE_DEG +
                              GL30_UI_DIAL_SPAN_ANGLE_DEG * expected_ratio;
  EXPECT_FLOAT_EQ(out.dial_angle_deg, expected_angle, 1e-5f, name);

  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
    .wake_signal = true,
    .connected = true,
    .volume = 100.0f,
    .mode = GL30_UI_MODE_VOLUME,
  }, 120u, &out);
  const float wake_expected = smooth_step(0.0f, 1.0f, 120u, GL30_UI_WAKE_SMOOTH_MS);
  EXPECT_FLOAT_EQ(out.wake_level, wake_expected, 1e-5f, name);
}

static void test_display_numbers_round_from_smoothed_ratio(void) {
  const char* name = "value text should follow smoothed ratio rounding";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;
  gl30_ui_anim_input_t in = {
    .volume = 100.0f,
    .mode = GL30_UI_MODE_VOLUME,
    .connected = true,
  };

  gl30_ui_anim_model_init(&model);
  in.volume = 100.0f;
  gl30_ui_anim_model_tick(&model, &in, 100u, &out);

  const float expected_ratio = smooth_step(GL30_UI_MODE_VOLUME_DEFAULT / GL30_UI_MODE_VOLUME_MAX,
                                          1.0f,
                                          100u,
                                          GL30_UI_VALUE_SMOOTH_MS);
  const int expected_text = (int)(expected_ratio * 100.0f + 0.5f);
  char expected_value[20];
  snprintf(expected_value, sizeof(expected_value), "%d", expected_text);

  EXPECT_STR_EQ(out.value_text, expected_value, name);
}

static void test_off_outputs_zero_value_and_wake(void) {
  const char* name = "off mode forces value_ratio and wake_level to 0";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;
  gl30_ui_anim_input_t in = {
    .volume = 100.0f,
    .mode = GL30_UI_MODE_VOLUME,
    .connected = true,
    .wake_signal = true,
  };

  gl30_ui_anim_model_init(&model);
  gl30_ui_anim_model_tick(&model, &in, 100u, &out);
  EXPECT_TRUE(out.wake_level > 0.0f, name);

  in.off = true;
  gl30_ui_anim_model_tick(&model, &in, 100u, &out);
  EXPECT_FLOAT_EQ(out.value_ratio, 0.0f, 0.0f, name);
  EXPECT_FLOAT_EQ(out.wake_level, 0.0f, 0.0f, name);
}

static void test_nan_and_inf_fallback(void) {
  const char* name = "NaN and INF inputs should fallback to valid ranges";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;
  gl30_ui_anim_input_t in = {
    .volume = NAN,
    .timer_minutes = INFINITY,
    .mode = GL30_UI_MODE_VOLUME,
    .connected = true,
  };

  gl30_ui_anim_model_init(&model);
  gl30_ui_anim_model_tick(&model, &in, 100u, &out);
  EXPECT_FLOAT_EQ(out.value_ratio, smooth_step(
                                      GL30_UI_MODE_VOLUME_DEFAULT / GL30_UI_MODE_VOLUME_MAX,
                                      GL30_UI_MODE_VOLUME_DEFAULT / GL30_UI_MODE_VOLUME_MAX,
                                      100u,
                                      GL30_UI_VALUE_SMOOTH_MS), 1e-6f, name);
}

static void test_null_input_lost_connection_flag(void) {
  const char* name = "NULL input should report lost_connection";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;

  gl30_ui_anim_model_init(&model);
  gl30_ui_anim_model_tick(&model, NULL, 100u, &out);
  EXPECT_TRUE(out.lost_connection == true, name);
  EXPECT_STR_EQ(out.status_text, "DISCONNECTED", name);
}

static void test_input_clamping_limits(void) {
  const char* name = "volume and timer inputs are clamped to configured ranges";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;
  gl30_ui_anim_input_t in = {
    .volume = -5.0f,
    .mode = GL30_UI_MODE_VOLUME,
    .connected = true,
  };

  gl30_ui_anim_model_init(&model);
  gl30_ui_anim_model_tick(&model, &in, GL30_UI_MAX_TICK_MS, &out);
  const float expected_low = smooth_step(GL30_UI_MODE_VOLUME_DEFAULT / GL30_UI_MODE_VOLUME_MAX,
                                        GL30_UI_MODE_VOLUME_MIN / GL30_UI_MODE_VOLUME_MAX,
                                        GL30_UI_MAX_TICK_MS,
                                        GL30_UI_VALUE_SMOOTH_MS);
  EXPECT_FLOAT_EQ(out.value_ratio, expected_low, 1e-5f, name);
  EXPECT_FLOAT_EQ(out.dial_angle_deg,
                  GL30_UI_DIAL_MIN_ANGLE_DEG +
                    GL30_UI_DIAL_SPAN_ANGLE_DEG * expected_low,
                  1e-5f,
                  name);

  in.mode = GL30_UI_MODE_TIMER;
  in.timer_minutes = 200.0f;
  gl30_ui_anim_model_init(&model);
  gl30_ui_anim_model_tick(&model, &in, GL30_UI_MAX_TICK_MS, &out);
  EXPECT_FLOAT_EQ(out.value_ratio, 1.0f, 1e-5f, name);
}

static void test_fault_has_priority_over_lost(void) {
  const char* name = "fault should be prioritized over lost connection";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;
  gl30_ui_anim_input_t in = {
    .fault = true,
    .connected = false,
    .mode = GL30_UI_MODE_VOLUME,
    .volume = 20.0f,
  };

  gl30_ui_anim_model_init(&model);
  gl30_ui_anim_model_tick(&model, &in, 100u, &out);
  EXPECT_STR_EQ(out.status_text, "FAULT", name);
}

static void test_mode_fallback_and_switch(void) {
  const char* name = "invalid mode falls back to last legal mode and switch works";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;
  gl30_ui_anim_input_t in = {
    .timer_minutes = 10.0f,
    .mode = GL30_UI_MODE_TIMER,
    .connected = true,
  };

  gl30_ui_anim_model_init(&model);
  gl30_ui_anim_model_tick(&model, &in, 100u, &out);
  EXPECT_INT_EQ(out.mode, GL30_UI_MODE_TIMER, name);

  in.mode = (gl30_ui_mode_t)99;
  gl30_ui_anim_model_tick(&model, &in, 100u, &out);
  EXPECT_INT_EQ(out.mode, GL30_UI_MODE_TIMER, name);

  in.mode = GL30_UI_MODE_VOLUME;
  in.volume = 10.0f;
  gl30_ui_anim_model_tick(&model, &in, 100u, &out);
  EXPECT_INT_EQ(out.mode, GL30_UI_MODE_VOLUME, name);
}

static void test_mode_switch_ratio_jumps_without_interpolation(void) {
  const char* name = "mode switch jumps ratio to timer semantics target";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;
  gl30_ui_anim_input_t in = {
    .volume = 100.0f,
    .mode = GL30_UI_MODE_VOLUME,
    .connected = true,
  };

  gl30_ui_anim_model_init(&model);
  gl30_ui_anim_model_tick(&model, &in, 200u, &out);

  in.mode = GL30_UI_MODE_TIMER;
  in.timer_minutes = 30.0f;
  gl30_ui_anim_model_tick(&model, &in, 100u, &out);
  EXPECT_FLOAT_EQ(out.value_ratio, 0.25f, 1e-6f, name);
}

static void test_timer_fill_ready_running_paused_and_finished(void) {
  const char* name = "timer_fill reflects ready/running/paused semantics with clamp and finished state";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;

  gl30_ui_anim_model_init(&model);

  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
      .mode = GL30_UI_MODE_TIMER,
      .timer_minutes = 2.0f,
      .connected = true,
  }, 100u, &out);
  EXPECT_FLOAT_EQ(out.timer_fill, 1.0f, 1e-6f, name);

  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
      .mode = GL30_UI_MODE_TIMER,
      .timer_minutes = 1.0f,
      .timer_running = true,
      .timer_remaining_ms = 30000u,
      .connected = true,
  }, 100u, &out);
  EXPECT_FLOAT_EQ(out.timer_fill, 0.5f, 1e-6f, name);

  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
      .mode = GL30_UI_MODE_TIMER,
      .timer_minutes = 1.0f,
      .timer_paused = true,
      .timer_remaining_ms = 15000u,
      .connected = true,
  }, 100u, &out);
  EXPECT_FLOAT_EQ(out.timer_fill, 0.25f, 1e-6f, name);

  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
      .mode = GL30_UI_MODE_TIMER,
      .timer_minutes = 1.0f,
      .timer_running = true,
      .timer_remaining_ms = 120000u,
      .connected = true,
  }, 100u, &out);
  EXPECT_FLOAT_EQ(out.timer_fill, 1.0f, 1e-6f, name);

  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
      .mode = GL30_UI_MODE_TIMER,
      .timer_minutes = 1.0f,
      .timer_running = true,
      .timer_remaining_ms = 0u,
      .connected = true,
  }, 100u, &out);
  EXPECT_FLOAT_EQ(out.timer_fill, 0.0f, 1e-6f, name);

  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
      .mode = GL30_UI_MODE_TIMER,
      .timer_minutes = 2.0f,
      .timer_finished = true,
      .timer_remaining_ms = 0u,
      .connected = true,
  }, 100u, &out);
  EXPECT_FLOAT_EQ(out.timer_fill, 0.0f, 1e-6f, name);

  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
      .mode = GL30_UI_MODE_TIMER,
      .timer_minutes = 0.0f,
      .connected = true,
  }, 100u, &out);
  EXPECT_FLOAT_EQ(out.timer_fill, 0.0f, 1e-6f, name);
}

static void test_timer_completion_animates_once_until_finished_reset(void) {
  const char* name = "completion pulses once when entering finished and resets after clear";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;

  gl30_ui_anim_model_init(&model);

  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
      .mode = GL30_UI_MODE_TIMER,
      .timer_minutes = 1.0f,
      .timer_running = true,
      .timer_remaining_ms = 1000u,
      .connected = true,
  }, 100u, &out);
  EXPECT_FLOAT_EQ(out.completion, 0.0f, 1e-6f, name);

  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
      .mode = GL30_UI_MODE_TIMER,
      .timer_minutes = 1.0f,
      .timer_finished = true,
      .timer_remaining_ms = 0u,
      .connected = true,
  }, 100u, &out);
  EXPECT_FLOAT_EQ(out.completion, 0.0f, 1e-6f, name);

  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
      .mode = GL30_UI_MODE_TIMER,
      .timer_minutes = 1.0f,
      .timer_finished = true,
      .timer_remaining_ms = 0u,
      .connected = true,
  }, 100u, &out);
  EXPECT_FLOAT_EQ(out.completion, 100.0f / 900.0f, 1e-6f, name);

  for (int i = 0; i < 8; ++i) {
    gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
        .mode = GL30_UI_MODE_TIMER,
        .timer_minutes = 1.0f,
        .timer_finished = true,
        .timer_remaining_ms = 0u,
        .connected = true,
    }, 100u, &out);
  }
  EXPECT_FLOAT_EQ(out.completion, 1.0f, 1e-6f, name);

  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
      .mode = GL30_UI_MODE_TIMER,
      .timer_minutes = 1.0f,
      .timer_running = true,
      .timer_remaining_ms = 1000u,
      .connected = true,
  }, 100u, &out);
  EXPECT_FLOAT_EQ(out.completion, 0.0f, 1e-6f, name);

  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
      .mode = GL30_UI_MODE_TIMER,
      .timer_minutes = 1.0f,
      .timer_finished = true,
      .timer_remaining_ms = 0u,
      .connected = true,
  }, 100u, &out);
  EXPECT_FLOAT_EQ(out.completion, 0.0f, 1e-6f, name);
}

static void test_pause_with_fixed_remaining_freezes_phase(void) {
  const char* name = "paused timer keeps fixed remaining so phase is stable";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;

  gl30_ui_anim_model_init(&model);
  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
      .mode = GL30_UI_MODE_TIMER,
      .timer_minutes = 1.0f,
      .timer_paused = true,
      .timer_remaining_ms = 5000u,
      .connected = true,
  }, 100u, &out);
  const float phase_before = out.phase;

  gl30_ui_anim_model_tick(&model, &(gl30_ui_anim_input_t){
      .mode = GL30_UI_MODE_TIMER,
      .timer_minutes = 1.0f,
      .timer_paused = true,
      .timer_remaining_ms = 5000u,
      .connected = true,
  }, 200u, &out);
  EXPECT_FLOAT_EQ(out.phase, phase_before, 1e-6f, name);
}

static void test_wake_hold_then_decay(void) {
  const char* name = "wake holds for 1300ms then decays";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_output_t out;
  gl30_ui_anim_input_t in = {
    .wake_signal = true,
    .connected = true,
    .volume = 42.0f,
    .mode = GL30_UI_MODE_VOLUME,
  };

  gl30_ui_anim_model_init(&model);
  gl30_ui_anim_model_tick(&model, &in, 100u, &out);

  float hold_peak = 0.0f;
  for (int i = 0; i < 6; ++i) {
    in.wake_signal = false;
    gl30_ui_anim_model_tick(&model, &in, 200u, &out);
    hold_peak = out.wake_level > hold_peak ? out.wake_level : hold_peak;
  }
  EXPECT_TRUE(hold_peak > 0.0f, name);

  gl30_ui_anim_model_tick(&model, &in, 200u, &out);
  EXPECT_TRUE(out.wake_level < hold_peak, name);
}

static void test_null_model_or_output_no_crash(void) {
  const char* name = "NULL model/output pointer should not crash";
  gl30_ui_anim_model_t model;
  gl30_ui_anim_model_t model_before;
  gl30_ui_anim_output_t out;
  gl30_ui_anim_input_t in = {
    .volume = 50.0f,
    .connected = true,
    .mode = GL30_UI_MODE_VOLUME,
  };

  gl30_ui_anim_model_init(&model);
  memcpy(&model_before, &model, sizeof(model));
  gl30_ui_anim_model_tick(NULL, &in, 100u, &out);
  gl30_ui_anim_model_tick(&model, &in, 100u, NULL);
  EXPECT_MEMEQ(&model, &model_before, sizeof(model), name);
  gl30_ui_anim_model_tick(NULL, NULL, 100u, NULL);
}

typedef void (*test_fn_t)(void);

typedef struct {
  const char* name;
  test_fn_t fn;
} unit_test_t;

int main(void) {
  int local_failures = 0;
  unit_test_t tests[] = {
    {"defaults", test_defaults},
    {"dt_zero", test_dt_zero_should_not_advance},
    {"elapsed_clamp", test_elapsed_is_clamped_to_200ms},
    {"smooth_angle", test_smooth_formulas_and_angle},
    {"smoothed_numeric_text", test_display_numbers_round_from_smoothed_ratio},
    {"off_zero_outputs", test_off_outputs_zero_value_and_wake},
    {"nan_inf", test_nan_and_inf_fallback},
    {"null_input_lost", test_null_input_lost_connection_flag},
    {"fault_vs_lost", test_fault_has_priority_over_lost},
    {"mode_fallback", test_mode_fallback_and_switch},
    {"mode_jump", test_mode_switch_ratio_jumps_without_interpolation},
    {"timer_fill", test_timer_fill_ready_running_paused_and_finished},
    {"completion", test_timer_completion_animates_once_until_finished_reset},
    {"pause_phase", test_pause_with_fixed_remaining_freezes_phase},
    {"wake_hold", test_wake_hold_then_decay},
    {"null_input", test_null_model_or_output_no_crash},
    {"input_limits", test_input_clamping_limits},
  };

  for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
    g_failed = 0;
    tests[i].fn();
    if (g_failed == 0) {
      ++g_passed;
      printf("PASS %-55s\n", tests[i].name);
    } else {
      ++local_failures;
    }
    g_failed = 0;
  }

  printf("\n%d passed, %d failed\n", g_passed, local_failures);
  return local_failures == 0 ? 0 : 1;
}
