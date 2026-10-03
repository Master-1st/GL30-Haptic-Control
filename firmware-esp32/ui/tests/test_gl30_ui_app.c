// Unit tests for ui app orchestration layer.
//
// These tests target production UI behavior only:
// - timer MM:SS rendering
// - long elapsed ticks for countdown
// - pause / resume / reset / off-screen continuity
// - volume mute + rotation interaction
// - 6° per rotation unit
// - reconnect rebase behavior
// - fault blocking operations
#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stddef.h>

#include "gl30_ui_app.h"

static int g_passed;
static int g_failed;

static void test_fail(const char* name, const char* detail) {
  ++g_failed;
  printf("FAIL %-55s %s\n", name, detail);
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

#define EXPECT_UINT_EQ(actual, expected, name) \
  do { \
    if ((actual) != (expected)) { \
      char __buf[200]; \
      snprintf(__buf, sizeof(__buf), "expected %u got %u", (unsigned)(expected), (unsigned)(actual)); \
      test_fail((name), __buf); \
      return; \
    } \
  } while (0)

#define EXPECT_FLOAT_EQ(actual, expected, eps, name) \
  do { \
    if (fabsf((actual) - (expected)) > (eps)) { \
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

static const float k_step_rad = 0.104719755f;  // 6 degrees in radians.

static void test_timer_mmss_and_full_elapsed_is_not_clamped(void) {
  const char* name = "MM:SS timer text and full elapsed tick is honored";
  gl30_ui_app_t app = {0};
  gl30_ui_anim_output_t out;

  gl30_ui_app_init(&app);
  app.input.mode = GL30_UI_MODE_TIMER;
  app.input.timer_minutes = 1.0f;
  app.input.timer_remaining_ms = 60000u;
  app.input.timer_running = true;
  app.input.connected = true;

  gl30_ui_app_tick(&app, 0u, &out);
  EXPECT_STR_EQ(out.value_text, "01:00", name);
  EXPECT_TRUE(!out.timer_finished, name);

  gl30_ui_app_tick(&app, 70000u, &out);
  EXPECT_TRUE(out.timer_finished, name);
  EXPECT_STR_EQ(out.value_text, "00:00", name);
  EXPECT_STR_EQ(out.status_text, "DONE", name);
}

static void test_pause_resume_reset_and_offscreen_continues(void) {
  const char* name = "pause/resume/reset/off-screen all keep timer semantics";
  gl30_ui_app_t app = {0};
  gl30_ui_anim_output_t out;

  gl30_ui_app_init(&app);
  app.input.mode = GL30_UI_MODE_TIMER;
  app.input.timer_minutes = 2.0f;
  app.input.timer_remaining_ms = 120000u;
  app.input.timer_running = true;

  gl30_ui_app_tick(&app, 0u, &out);
  EXPECT_STR_EQ(out.value_text, "02:00", name);

  gl30_ui_app_action(&app, GL30_UI_ACTION_PRIMARY);  // pause
  gl30_ui_app_tick(&app, 1000u, &out);
  EXPECT_TRUE(out.timer_paused && !out.timer_running, name);
  EXPECT_STR_EQ(out.value_text, "02:00", name);

  gl30_ui_app_action(&app, GL30_UI_ACTION_PRIMARY);  // resume
  gl30_ui_app_tick(&app, 2000u, &out);
  EXPECT_TRUE(out.timer_running && !out.timer_paused, name);

  gl30_ui_app_action(&app, GL30_UI_ACTION_POWER);    // off-screen
  EXPECT_TRUE(app.input.off, name);
  const uint32_t remaining_before_off = app.input.timer_remaining_ms;
  gl30_ui_app_tick(&app, 12000u, &out);  // continue counting while off
  EXPECT_TRUE(app.input.timer_running, name);
  EXPECT_TRUE(app.input.timer_remaining_ms < remaining_before_off, name);

  gl30_ui_app_action(&app, GL30_UI_ACTION_POWER);    // wake
  gl30_ui_app_action(&app, GL30_UI_ACTION_RESET);    // reset
  EXPECT_TRUE(!app.input.timer_running && !app.input.timer_paused, name);
  EXPECT_INT_EQ((int)app.input.timer_remaining_ms, 120000, name);
}

static void test_mute_and_rotation_unmutes(void) {
  const char* name = "volume primary mutes then rotation step unmutes";
  gl30_ui_app_t app = {0};
  gl30_ui_anim_output_t out;
  const float initial_volume = 42.0f;

  gl30_ui_app_init(&app);
  app.input.volume = initial_volume;
  gl30_ui_app_action(&app, GL30_UI_ACTION_PRIMARY);
  EXPECT_TRUE(app.input.muted, name);

  gl30_ui_app_observe(&app, 0.0f, true, false);
  gl30_ui_app_observe(&app, k_step_rad, true, false);
  EXPECT_TRUE(!app.input.muted, name);
  EXPECT_FLOAT_EQ(app.input.volume, initial_volume + 1.0f, 1e-6f, name);
  gl30_ui_app_tick(&app, 0u, &out);
  EXPECT_STR_EQ(out.status_text, "READY", name);
}

static void test_rotation_is_6_degrees_per_unit(void) {
  const char* name = "rotation 6deg increments exactly one volume unit";
  gl30_ui_app_t app = {0};
  gl30_ui_anim_output_t out;

  gl30_ui_app_init(&app);
  const float initial_volume = app.input.volume;
  gl30_ui_app_observe(&app, 0.0f, true, false);
  gl30_ui_app_observe(&app, 2.0f * k_step_rad, true, false);
  gl30_ui_app_tick(&app, 0u, &out);
  gl30_ui_app_tick(&app, 100u, &out);
  EXPECT_FLOAT_EQ(app.input.volume, initial_volume + 2.0f, 1e-5f, name);
  EXPECT_TRUE(!out.lost_connection, name);
  EXPECT_TRUE(out.value_ratio > 0.42f, name);
}

static void test_reconnect_rebases_without_angle_jump(void) {
  const char* name = "disconnect/reconnect only rebase angle and no immediate jump";
  gl30_ui_app_t app = {0};
  gl30_ui_anim_output_t out;
  gl30_ui_app_init(&app);
  const float initial_volume = app.input.volume;

  gl30_ui_app_observe(&app, 0.0f, true, false);
  gl30_ui_app_observe(&app, 1.2f, true, false);
  EXPECT_TRUE(app.input.volume > initial_volume, name);
  const float after_rotation = app.input.volume;

  gl30_ui_app_observe(&app, 2.0f, false, false); /* disconnect */
  gl30_ui_app_observe(&app, 9.0f, true, false); /* first angle after reconnect should rebase */
  EXPECT_FLOAT_EQ(app.input.volume, after_rotation, 1e-6f, name);

  gl30_ui_app_observe(&app, 9.0f + (1.0f * k_step_rad), true, false);
  EXPECT_TRUE(app.input.volume > initial_volume, name);
  EXPECT_FLOAT_EQ(app.input.volume, after_rotation + 1.0f, 1e-6f, name);
  gl30_ui_app_observe(&app, 9.0f + (2.0f * k_step_rad), true, false);
  gl30_ui_app_tick(&app, 0u, &out);
  EXPECT_TRUE(!out.lost_connection, name);
}

static void test_fault_blocks_user_actions(void) {
  const char* name = "fault state blocks adjust and primary action";
  gl30_ui_app_t app = {0};
  gl30_ui_anim_output_t out;

  gl30_ui_app_init(&app);
  app.input.fault = true;
  const float before = app.input.volume;
  app.input.mode = GL30_UI_MODE_VOLUME;
  gl30_ui_app_action(&app, GL30_UI_ACTION_PRIMARY);
  EXPECT_FLOAT_EQ(app.input.volume, before, 0.0f, name);
  EXPECT_TRUE(!app.input.muted, name);

  gl30_ui_app_adjust(&app, 5);
  gl30_ui_app_observe(&app, 2.0f * k_step_rad, true, true);
  EXPECT_FLOAT_EQ(app.input.volume, before, 0.0f, name);

  gl30_ui_app_tick(&app, 0u, &out);
}

static void test_clock_rollback_keeps_state_monotonic(void) {
  const char* name = "clock rollback does not create negative elapsed";
  gl30_ui_app_t app = {0};
  gl30_ui_anim_output_t out;

  gl30_ui_app_init(&app);
  app.input.mode = GL30_UI_MODE_TIMER;
  app.input.timer_minutes = 1.0f;
  app.input.timer_remaining_ms = 60000u;
  app.input.timer_running = true;

  gl30_ui_app_tick(&app, 0u, &out);
  gl30_ui_app_tick(&app, 5000u, &out);
  EXPECT_UINT_EQ(app.input.timer_remaining_ms, 55000u, name);

  gl30_ui_app_tick(&app, 1000u, &out); // rollback timestamp, should not go negative
  EXPECT_UINT_EQ(app.input.timer_remaining_ms, 55000u, name);

  gl30_ui_app_tick(&app, 7000u, &out);
  EXPECT_UINT_EQ(app.input.timer_remaining_ms, 53000u, name);
}

typedef void (*test_fn_t)(void);

typedef struct {
  const char* name;
  test_fn_t fn;
} unit_test_t;

int main(void) {
  int local_failures = 0;
  unit_test_t tests[] = {
    {"timer_mmss", test_timer_mmss_and_full_elapsed_is_not_clamped},
    {"pause_resume_off", test_pause_resume_reset_and_offscreen_continues},
    {"mute", test_mute_and_rotation_unmutes},
    {"rotation", test_rotation_is_6_degrees_per_unit},
    {"angle_rebase", test_reconnect_rebases_without_angle_jump},
    {"fault", test_fault_blocks_user_actions},
    {"rollback", test_clock_rollback_keeps_state_monotonic},
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
