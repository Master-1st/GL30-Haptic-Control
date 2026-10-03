#include <stdint.h>
#include <stdio.h>

#include "board_config.h"
#include "current_zero.h"

static unsigned checks;
static unsigned failed;

#define CHECK(condition, message)                                             \
  do {                                                                        \
    checks++;                                                                 \
    if (!(condition)) {                                                       \
      failed++;                                                               \
      fprintf(stderr, "[FAIL] %s\n", (message));                             \
    }                                                                         \
  } while (0)

static void start_calibration(gl30_current_zero_t *ctx, uint64_t start_us) {
  gl30_current_zero_update(ctx, start_us, true, true, NULL);
}

static void test_waiting_and_reset_gates(void) {
  gl30_current_zero_t ctx;
  const uint16_t raw[3] = {2048u, 2048u, 2048u};

  gl30_current_zero_reset(&ctx);
  gl30_current_zero_update(&ctx, 100u, false, true, raw);
  CHECK(ctx.state == GL30_CURRENT_ZERO_WAITING && ctx.count == 0u,
        "analog not ready keeps qualification waiting");
  gl30_current_zero_update(&ctx, 200u, true, false, raw);
  CHECK(ctx.state == GL30_CURRENT_ZERO_WAITING && ctx.count == 0u,
        "bridge output enabled cannot start calibration");
  gl30_current_zero_update(&ctx, 300u, true, true, raw);
  CHECK(ctx.state == GL30_CURRENT_ZERO_SETTLING && ctx.started_us == 300u,
        "qualification starts its settle interval only with analog ready and bridge off");

  gl30_current_zero_update(&ctx, 400u, false, true, raw);
  CHECK(ctx.state == GL30_CURRENT_ZERO_WAITING && ctx.count == 0u &&
            ctx.started_us == 0u,
        "loss of analog readiness resets an in-flight qualification");

  gl30_current_zero_update(&ctx, 500u, true, true, raw);
  CHECK(ctx.state == GL30_CURRENT_ZERO_SETTLING && ctx.started_us == 500u,
        "qualification can start again after a reset");
}

static uint16_t boundary_sample(uint16_t center, unsigned sample_index) {
  return (uint16_t)(center + ((sample_index & 1u) == 0u ? 30 : -30));
}

static void test_511th_and_512th_sample_boundary(void) {
  gl30_current_zero_t ctx;
  const uint64_t start_us = 1000u;

  gl30_current_zero_reset(&ctx);
  start_calibration(&ctx, start_us);
  for (unsigned i = 0u; i < GL30_ADC_ZERO_CAL_SAMPLES - 1u; ++i) {
    const uint16_t raw[3] = {
        boundary_sample(1698u, i),
        boundary_sample(2048u, i),
        boundary_sample(2398u, i),
    };
    gl30_current_zero_update(
        &ctx, start_us + GL30_ADC_ZERO_SETTLE_US + i * 25u, true, true, raw);
  }
  CHECK(ctx.state == GL30_CURRENT_ZERO_COLLECTING &&
            ctx.count == GL30_ADC_ZERO_CAL_SAMPLES - 1u,
        "511 synchronized samples do not publish offsets");

  {
    const unsigned i = GL30_ADC_ZERO_CAL_SAMPLES - 1u;
    const uint16_t raw[3] = {
        boundary_sample(1698u, i),
        boundary_sample(2048u, i),
        boundary_sample(2398u, i),
    };
    gl30_current_zero_update(
        &ctx, start_us + GL30_ADC_ZERO_SETTLE_US + i * 25u, true, true, raw);
  }
  CHECK(ctx.state == GL30_CURRENT_ZERO_READY &&
            ctx.count == GL30_ADC_ZERO_CAL_SAMPLES,
        "the 512th synchronized sample publishes ready");
  CHECK(ctx.min[0] == 1668u && ctx.max[0] == 1728u &&
            ctx.min[2] == 2368u && ctx.max[2] == 2428u,
        "a 60-count span is accepted at each channel");
  CHECK(ctx.offset[0] == 1698.0f && ctx.offset[1] == 2048.0f &&
            ctx.offset[2] == 2398.0f,
        "per-channel offsets include both inclusive mean error boundaries");

  gl30_current_zero_update(&ctx, start_us + GL30_ADC_ZERO_TIMEOUT_US, true,
                           false, NULL);
  CHECK(ctx.state == GL30_CURRENT_ZERO_READY,
        "ready qualification persists while the bridge is later enabled");
  gl30_current_zero_update(&ctx, start_us + GL30_ADC_ZERO_TIMEOUT_US, false,
                           false, NULL);
  CHECK(ctx.state == GL30_CURRENT_ZERO_WAITING && ctx.count == 0u,
        "loss of analog readiness resets even a completed qualification");
}

static void test_foreground_timeout_and_timestamp_rules(void) {
  gl30_current_zero_t ctx;
  const uint64_t start_us = 4000u;
  const uint16_t raw[3] = {2048u, 2048u, 2048u};

  gl30_current_zero_reset(&ctx);
  start_calibration(&ctx, start_us);
  gl30_current_zero_update(&ctx, start_us + GL30_ADC_ZERO_SETTLE_US, true,
                           true, NULL);
  CHECK(ctx.state == GL30_CURRENT_ZERO_COLLECTING && ctx.count == 0u,
        "foreground timeout check can enter collection without fabricating a sample");
  gl30_current_zero_update(&ctx, start_us + GL30_ADC_ZERO_TIMEOUT_US - 1u,
                           true, true, NULL);
  CHECK(ctx.state == GL30_CURRENT_ZERO_COLLECTING,
        "a no-sample foreground check remains live immediately before timeout");
  gl30_current_zero_update(&ctx, start_us + GL30_ADC_ZERO_TIMEOUT_US, true,
                           true, NULL);
  CHECK(ctx.state == GL30_CURRENT_ZERO_FAILED,
        "foreground no-sample check fails at the overall 50 ms deadline");

  gl30_current_zero_reset(&ctx);
  start_calibration(&ctx, start_us);
  gl30_current_zero_update(&ctx, start_us + GL30_ADC_ZERO_SETTLE_US, true,
                           true, raw);
  CHECK(ctx.count == 1u, "first post-settle sample is counted");
  gl30_current_zero_update(&ctx, start_us + GL30_ADC_ZERO_SETTLE_US, true,
                           true, raw);
  CHECK(ctx.count == 1u,
        "a repeated sample timestamp is ignored instead of double-counted");
  gl30_current_zero_update(&ctx, start_us + GL30_ADC_ZERO_SETTLE_US - 1u,
                           true, true, raw);
  CHECK(ctx.state == GL30_CURRENT_ZERO_FAILED,
        "a backwards sample timestamp fails qualification");
}

static void test_settle_and_overall_deadline_edges(void) {
  gl30_current_zero_t ctx;
  const uint16_t raw[3] = {2048u, 2048u, 2048u};

  gl30_current_zero_reset(&ctx);
  start_calibration(&ctx, 0u);
  gl30_current_zero_update(&ctx, GL30_ADC_ZERO_SETTLE_US - 1u, true, true,
                           raw);
  CHECK(ctx.state == GL30_CURRENT_ZERO_SETTLING && ctx.count == 0u,
        "a sample at 9999 us is not collected");
  gl30_current_zero_update(&ctx, GL30_ADC_ZERO_SETTLE_US, true, true, raw);
  CHECK(ctx.state == GL30_CURRENT_ZERO_COLLECTING && ctx.count == 1u,
        "a sample at exactly 10000 us begins collection");

  for (unsigned i = 1u; i < GL30_ADC_ZERO_CAL_SAMPLES - 1u; ++i) {
    gl30_current_zero_update(&ctx,
                             GL30_ADC_ZERO_SETTLE_US + i * 25u, true, true,
                             raw);
  }
  CHECK(ctx.state == GL30_CURRENT_ZERO_COLLECTING &&
            ctx.count == GL30_ADC_ZERO_CAL_SAMPLES - 1u,
        "the final sample remains pending just before the overall timeout");
  gl30_current_zero_update(&ctx, GL30_ADC_ZERO_TIMEOUT_US, true, true, raw);
  CHECK(ctx.state == GL30_CURRENT_ZERO_FAILED &&
            ctx.count == GL30_ADC_ZERO_CAL_SAMPLES - 1u,
        "a 512th sample exactly at 50000 us fails because timeout is checked first");
}

static void finish_with_pattern(gl30_current_zero_t *ctx, uint64_t start_us,
                                uint16_t center_a, uint16_t center_b,
                                uint16_t center_c, bool span_over_limit) {
  gl30_current_zero_reset(ctx);
  start_calibration(ctx, start_us);
  for (unsigned i = 0u; i < GL30_ADC_ZERO_CAL_SAMPLES; ++i) {
    const int deviation = span_over_limit
                              ? ((i & 1u) == 0u ? 0 : 61)
                              : 0;
    const uint16_t raw[3] = {
        (uint16_t)(center_a + deviation), center_b, center_c};
    gl30_current_zero_update(
        ctx, start_us + GL30_ADC_ZERO_SETTLE_US + i * 25u, true, true, raw);
  }
}

static void test_quality_rejection_and_failed_latch(void) {
  gl30_current_zero_t ctx;
  const uint64_t start_us = 8000u;
  const uint16_t rail[3] = {2048u, 4096u, 2048u};

  gl30_current_zero_reset(&ctx);
  start_calibration(&ctx, start_us);
  gl30_current_zero_update(&ctx, start_us + GL30_ADC_ZERO_SETTLE_US, true,
                           true, rail);
  CHECK(ctx.state == GL30_CURRENT_ZERO_FAILED && ctx.count == 0u,
        "raw samples above the 12-bit ADC range fail immediately");

  gl30_current_zero_update(&ctx, start_us + GL30_ADC_ZERO_SETTLE_US + 1u,
                           true, true, rail);
  CHECK(ctx.state == GL30_CURRENT_ZERO_FAILED && ctx.count == 0u,
        "failed qualification stays latched while analog power remains ready");

  finish_with_pattern(&ctx, start_us, 1697u, 2048u, 2048u, false);
  CHECK(ctx.state == GL30_CURRENT_ZERO_FAILED &&
            ctx.count == GL30_ADC_ZERO_CAL_SAMPLES,
        "a mean outside the inclusive 2048 plus-or-minus 350 window is rejected");

  finish_with_pattern(&ctx, start_us, 2048u, 2048u, 2048u, true);
  CHECK(ctx.state == GL30_CURRENT_ZERO_FAILED,
        "a 61-count range is rejected after the full sample window");

  gl30_current_zero_reset(&ctx);
  CHECK(ctx.state == GL30_CURRENT_ZERO_WAITING && ctx.count == 0u &&
            ctx.offset[0] == GL30_ADC_ZERO_DEFAULT_COUNTS,
        "explicit reset clears failure and restores default offsets");
  gl30_current_zero_update(&ctx, start_us + 2u, true, true, NULL);
  CHECK(ctx.state == GL30_CURRENT_ZERO_SETTLING,
        "a reset after failure allows a new calibration attempt");
}

int main(void) {
  test_waiting_and_reset_gates();
  test_511th_and_512th_sample_boundary();
  test_foreground_timeout_and_timestamp_rules();
  test_settle_and_overall_deadline_edges();
  test_quality_rejection_and_failed_latch();
  printf("current zero: %u checks, %u failed\n", checks, failed);
  return failed == 0u ? 0 : 1;
}
