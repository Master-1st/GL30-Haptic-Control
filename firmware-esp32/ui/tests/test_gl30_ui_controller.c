// Unit tests for UI controller composition of link+app.
// Verifies: fast frame ingestion drives volume UI, disconnect/reconnect behavior,
// fault blocking, and timestamped actions.

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "gl30_ui_controller.h"

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

#define EXPECT_FLOAT_EQ(actual, expected, eps, name) \
  do { \
    if (!isfinite((actual)) || !isfinite((expected)) || fabsf((actual) - (expected)) > (eps)) { \
      char __buf[200]; \
      snprintf(__buf, sizeof(__buf), "expected %.8f got %.8f", (float)(expected), (float)(actual)); \
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

#define EXPECT_INT_EQ(actual, expected, name) \
  do { \
    if ((actual) != (expected)) { \
      char __buf[200]; \
      snprintf(__buf, sizeof(__buf), "expected %d got %d", (int)(expected), (int)(actual)); \
      test_fail((name), __buf); \
      return; \
    } \
  } while (0)

static size_t build_fast_frame(
    uint8_t *out, size_t out_cap, uint32_t seq, float angle, uint32_t fault_bits, size_t *out_len) {
  gl30_motor_state_fast_t state = {0};
  state.angleRad = angle;
  state.faultBits = fault_bits;
  state.velocityRadPerSec = 1.0f;
  state.accelerationRadPerSec2 = 2.0f;
  state.iqRefA = 3.0f;
  state.iqMeasA = 4.0f;
  state.idMeasA = 5.0f;
  state.torqueCmdNm = 6.0f;
  state.torqueEstNm = 7.0f;
  state.busVoltageV = 8.0f;
  state.busCurrentA = 9.0f;
  state.logicalPosition = 42;
  state.subPosition = 0.5f;
  state.motorState = 0u;
  state.warningBits = 0u;
  state.isrCycles = 1u;
  state.encoderStatus = 2u;
  state.droppedCmds = 3u;
  state.reserved = 4u;

  uint8_t payload[GL30_MOTOR_STATE_FAST_LEN];
  if (gl30_encode_motor_state_fast(&state, payload, sizeof(payload)) != 0) {
    return 0u;
  }
  size_t n = 0u;
  if (gl30_frame_encode(
          GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST, 0u, seq, 0u, payload, sizeof(payload), out, out_cap, &n) !=
      0) {
    return 0u;
  }
  if (out_len != NULL) {
    *out_len = n;
  }
  return n;
}

static void test_controller_receive_crc_fast_updates_volume_on_valid_frame(void) {
  const char* name = "valid CRC fast frame drives observed volume";
  gl30_ui_controller_t c;
  gl30_ui_anim_output_t out;
  gl30_ui_controller_init(&c);

  c.app.input.mode = GL30_UI_MODE_VOLUME;
  c.app.input.volume = 42.0f;
  const float step = 0.104719755f;
  uint8_t frame[256];
  size_t n = 0u;

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 1u, 0.0f, 0u, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_ui_controller_receive(&c, frame, n, 1000u), 0, name);
  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 2u, step, 0u, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_ui_controller_receive(&c, frame, n, 1200u), 0, name);

  gl30_ui_controller_tick(&c, 1250u, &out);
  EXPECT_FLOAT_EQ(c.app.input.volume, 43.0f, 1e-6f, name);
  EXPECT_INT_EQ(out.mode, GL30_UI_MODE_VOLUME, name);
}

static void test_controller_reconnect_does_not_rebase_jump(void) {
  const char* name = "first frame then 100ms disconnect + jump does not bump volume";
  gl30_ui_controller_t c;
  gl30_ui_anim_output_t out;
  gl30_ui_controller_init(&c);
  c.app.input.mode = GL30_UI_MODE_VOLUME;

  const float step = 0.104719755f;
  const float normal = 42.0f;
  uint8_t frame[256];
  size_t n = 0u;

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 1u, 0.0f, 0u, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_ui_controller_receive(&c, frame, n, 1000u), 0, name);
  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 2u, step, 0u, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_ui_controller_receive(&c, frame, n, 1100u), 0, name);

  gl30_ui_controller_tick(&c, 1200u, &out);
  EXPECT_FLOAT_EQ(c.app.input.volume, normal + 1.0f, 1e-6f, name);

  gl30_ui_controller_tick(&c, 130000u, &out);
  EXPECT_TRUE(out.lost_connection, name);

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 3u, 100.0f * step, 0u, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_ui_controller_receive(&c, frame, n, 130100u), 0, name);
  EXPECT_FLOAT_EQ(c.app.input.volume, normal + 1.0f, 1e-6f, name);

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 4u, 100.0f * step + step, 0u, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_ui_controller_receive(&c, frame, n, 130200u), 0, name);
  EXPECT_FLOAT_EQ(c.app.input.volume, normal + 2.0f, 1e-6f, name);
}

static void test_controller_fault_blocks_volume_adjust(void) {
  const char* name = "fault input stops rotation-volume updates";
  gl30_ui_controller_t c;
  gl30_ui_anim_output_t out;
  gl30_ui_controller_init(&c);
  c.app.input.mode = GL30_UI_MODE_VOLUME;

  const float step = 0.104719755f;
  uint8_t frame[256];
  size_t n = 0u;
  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 1u, 0.0f, 0u, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_ui_controller_receive(&c, frame, n, 100u), 0, name);
  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 2u, step, 0u, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_ui_controller_receive(&c, frame, n, 200u), 0, name);

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 3u, 2.0f * step, 1u, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_ui_controller_receive(&c, frame, n, 300u), 0, name);
  gl30_ui_controller_tick(&c, 400u, &out);
  EXPECT_FLOAT_EQ(c.app.input.volume, 43.0f, 1e-6f, name);
}

static void test_controller_action_takes_tick_delta_before_pause(void) {
  const char* name = "controller action advances timer by now delta between actions";
  gl30_ui_controller_t c;
  gl30_ui_anim_output_t out;
  gl30_ui_controller_init(&c);

  c.app.input.mode = GL30_UI_MODE_TIMER;
  c.app.input.timer_running = false;
  c.app.input.timer_paused = false;
  c.app.input.timer_minutes = 1.0f;
  c.app.input.timer_remaining_ms = 60000u;

  gl30_ui_controller_action(&c, 1000000u, GL30_UI_ACTION_PRIMARY);
  EXPECT_TRUE(c.app.input.timer_running, name);
  gl30_ui_controller_action(&c, 7000000u, GL30_UI_ACTION_PRIMARY);
  EXPECT_TRUE(c.app.input.timer_paused, name);
  EXPECT_TRUE(!c.app.input.timer_running, name);
  EXPECT_UINT_EQ(c.app.input.timer_remaining_ms, 54000u, name);

  gl30_ui_controller_tick(&c, 8000u, &out);
  EXPECT_TRUE(!out.timer_finished, name);
}

typedef void (*test_fn_t)(void);

typedef struct {
  const char* name;
  test_fn_t fn;
} unit_test_t;

int main(void) {
  int local_failures = 0;
  int local_passed = 0;
  unit_test_t tests[] = {
    {"rx_valid_fast_volume", test_controller_receive_crc_fast_updates_volume_on_valid_frame},
    {"reconnect_no_jump", test_controller_reconnect_does_not_rebase_jump},
    {"fault_block", test_controller_fault_blocks_volume_adjust},
    {"action_with_tick_delta", test_controller_action_takes_tick_delta_before_pause},
  };

  for (size_t i = 0u; i < sizeof(tests) / sizeof(tests[0]); ++i) {
    g_failed = 0;
    tests[i].fn();
    if (g_failed == 0) {
      ++local_passed;
      printf("PASS %-55s\n", tests[i].name);
    } else {
      ++local_failures;
    }
    g_failed = 0;
  }
  g_passed = local_passed;

  printf("\n%d passed, %d failed\n", g_passed, local_failures);
  return local_failures == 0 ? 0 : 1;
}
