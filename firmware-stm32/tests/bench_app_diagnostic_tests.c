#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

/*
 * Keep ADC diagnostics real by disabling the production stub shipped in
 * tests/bench_hw_fake/main.h for this translation unit.
 */
#define BENCH_HW_FAKE_USE_REAL_ADC_DIAGNOSTICS

#include "bench/NUCLEO_G474RE_FOC/Core/Inc/bench_hw.h"
#include "bench/NUCLEO_G474RE_FOC/Core/Inc/bench_app.h"
#include "bench_hw_fake/main.h"

/* Reuse production helper logic for SPI frame construction and sensor decode. */
#include "drivers/drv8316_spi.c"
#include "drivers/as5048a.c"

/* Real logic under test: safety, hardware abstraction, and app command/IRQs. */
#include "bench/NUCLEO_G474RE_FOC/Core/Src/bench_safety.c"
#include "bench/NUCLEO_G474RE_FOC/Core/Src/bench_hw.c"
#include "control/foc.c"
/* Test-only scheduling seams around the real app. No target hooks or alternate
 * observer math: inject ADC preemption while TIM6 builds/publishes a frame. */
static void app_test_observer(gl30_foc_state_t *, float, bool);
static void app_test_disable_irq(void);
static void app_test_set_primask(uint32_t);
#define gl30_foc_observer_tick_4k app_test_observer
#define __disable_irq app_test_disable_irq
#define __set_PRIMASK app_test_set_primask
#include "bench/NUCLEO_G474RE_FOC/Core/Src/bench_app.c"
#undef gl30_foc_observer_tick_4k
#undef __disable_irq
#undef __set_PRIMASK

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

#define CHECK_EQ_U32(a, b, msg) CHECK(((uint32_t)(a)) == ((uint32_t)(b)), (msg))
#define CHECK_EQ_U16(a, b, msg) CHECK(((uint16_t)(a)) == ((uint16_t)(b)), (msg))

static bool g_test_observer_preempt, g_test_publish_preempt, g_test_published_preempt;
static uint32_t g_test_observer_delay_us, g_test_observer_start_us;
static unsigned int g_test_preemptions;

static void check_observation(const gl30_foc_state_t *a, const gl30_foc_state_t *b) {
  CHECK(a->theta_mech_rad == b->theta_mech_rad, "complete observation: mechanical angle");
  CHECK(a->theta_elec_rad == b->theta_elec_rad, "complete observation: electrical angle");
  CHECK(a->theta_unwrapped_rad == b->theta_unwrapped_rad, "complete observation: unwrapped angle");
  CHECK(a->previous_angle_rad == b->previous_angle_rad, "complete observation: previous angle");
  CHECK(a->velocity_rad_s == b->velocity_rad_s, "complete observation: velocity");
  CHECK(a->acceleration_rad_s2 == b->acceleration_rad_s2, "complete observation: acceleration");
  CHECK(a->observer_initialized == b->observer_initialized, "complete observation: initialized");
}

static void adc_consumes_published_observation(void) {
  const gl30_foc_state_t expected = g_encoder_observer[g_encoder_observer_index];
  CHECK(!__get_PRIMASK(), "ADC scheduling seam is never inside publication lock");
  bench_hw_fake_set_adc_samples(1861u, 1861u, 1861u, 2700u);
  bench_hw_fake_set_adc_jeos(true);
  Bench_AdcIRQ();
  check_observation(&g_foc, &expected);
  CHECK(g_last_observer_seq == g_enc_seq, "ADC consumes exactly the published sequence");
  ++g_test_preemptions;
}

static void app_test_observer(gl30_foc_state_t *state, float angle, bool valid) {
  if (state == &g_encoder_observer[0] || state == &g_encoder_observer[1]) {
    CHECK(!__get_PRIMASK(), "real encoder observer runs with IRQs enabled");
    CHECK(state != &g_encoder_observer[g_encoder_observer_index], "encoder never mutates published buffer");
    g_test_observer_start_us = TIM2->CNT;
    if (g_test_observer_preempt) {
      g_test_observer_preempt = false;
      const gl30_foc_state_t saved = *state;
      state->theta_mech_rad = 999.0f;
      state->velocity_rad_s = -999.0f;
      adc_consumes_published_observation();
      *state = saved;
    }
    gl30_foc_observer_tick_4k(state, angle, valid);
    TIM2->CNT += g_test_observer_delay_us;
  } else {
    gl30_foc_observer_tick_4k(state, angle, valid);
  }
}

static void app_test_disable_irq(void) {
  if (g_test_publish_preempt) {
    g_test_publish_preempt = false;
    adc_consumes_published_observation();
  }
  __disable_irq();
}

static void app_test_set_primask(uint32_t mask) {
  __set_PRIMASK(mask);
  if (!mask && g_test_published_preempt) {
    g_test_published_preempt = false;
    adc_consumes_published_observation();
  }
}

static void bench_app_fake_reset_all(void)
{
  static uint32_t fixture_time;
  bench_hw_fake_reset();
  g_test_observer_preempt = g_test_publish_preempt = g_test_published_preempt = false;
  g_test_observer_delay_us = g_test_observer_start_us = 0u;
  g_test_preemptions = 0u;
  fixture_time += 100000u;
  TIM2->CNT = fixture_time;
  g_tim1_pwm_pins_parked = false;
  g_preparing = false;
  g_tracing = false;
  g_driver_trace = (bench_drv_trace_t){0};
  g_drv_ready = false;
  g_hw_ready = false;
  g_rate_ok = false;
  g_self_left = 0u;
  g_self_fail = 0u;
  g_self_max = 0u;
  g_deadlines = 0u;
  g_adc_bad = 0u;
  g_adc_count = 0u;
  g_enc_irq_count = 0u;
  g_encoder_errors = 0u;
  g_enc_seq = 0u;
  g_enc_us = 0u;
  g_enc_valid = false;
  g_enc_angle = 0u;
  g_enc_diag = 0u;
  g_uart_errors = 0u;
  g_last_observer_seq = 0u;
  g_prepare_us = 0u;
  g_align_voltage_sum = 0.0f;
  g_zero_sum[0] = 0u;
  g_zero_sum[1] = 0u;
  g_zero_sum[2] = 0u;
  g_zero_min[0] = 4095u;
  g_zero_min[1] = 4095u;
  g_zero_min[2] = 4095u;
  g_zero_max[0] = 0u;
  g_zero_max[1] = 0u;
  g_zero_max[2] = 0u;
  g_zero_count = 0u;
  g_zero_valid = false;
  g_zero_adc_bad_start = 0u;
  g_zero_sync_failed = false;
  g_zero_error_pending = false;
  g_zero[0] = 1861.0f;
  g_zero[1] = 1861.0f;
  g_zero[2] = 1861.0f;
  g_vm = 10.3f;
  g_tx_read = 0u;
  g_tx_write = 0u;
  g_rx_read = 0u;
  g_rx_write = 0u;
  g_line_len = 0u;
  g_discard_line = false;
  g_applied_field = 0.0f;
  Bench_Init();
  /* Most fixtures below directly seed enc_seq at angle zero rather than SPI.
   * Supply that complete observation; dedicated IRQ tests start empty. */
  gl30_foc_observer_tick_4k(&g_encoder_observer[0], 0.0f, true);
  g_encoder_observer[1] = g_encoder_observer[0];
  g_rate_ok = true;
  g_self_left = 0u;
  g_self_max = 1u;
  g_gate.faults = 0u;
  g_gate.mode = BENCH_OFF;
  g_gate.calibrated = false;
  bench_hw_fake_set_nfault_input(true);
}

/* Keep only the tiny subset required by bench_app behavior under test. */
enum {
  BENCH_APP_STEP_UNLOCK_WRITE = 0u,
  BENCH_APP_STEP_CFG4_WRITE = 1u,
  BENCH_APP_STEP_CFG4_READ = 2u,
  BENCH_APP_STEP_CFG5_WRITE = 3u,
  BENCH_APP_STEP_CFG5_READ = 4u,
  BENCH_APP_STEP_CFG6_WRITE = 5u,
  BENCH_APP_STEP_CFG6_READ = 6u,
  BENCH_APP_STEP_CFG7_WRITE = 7u,
  BENCH_APP_STEP_CFG7_READ = 8u,
  BENCH_APP_STEP_CFG8_WRITE = 9u,
  BENCH_APP_STEP_CFG8_READ = 10u,
  BENCH_APP_STEP_CFG12_WRITE = 11u,
  BENCH_APP_STEP_CFG12_READ = 12u,
  BENCH_APP_STEP_STATUS_PRE_0 = 13u,
  BENCH_APP_STEP_STATUS_PRE_1 = 14u,
  BENCH_APP_STEP_STATUS_PRE_2 = 15u,
  BENCH_APP_STEP_CLEAR_WRITE = 16u,
  BENCH_APP_STEP_CLEAR_READ = 17u,
  BENCH_APP_STEP_LOCK_WRITE = 18u,
  BENCH_APP_STEP_LOCK_READ = 19u,
  BENCH_APP_STEP_STATUS_FINAL_0 = 20u,
  BENCH_APP_STEP_STATUS_FINAL_1 = 21u,
  BENCH_APP_STEP_STATUS_FINAL_2 = 22u,
  BENCH_APP_STEP_FINAL_GAIN = 23u,
  BENCH_APP_STEP_FINAL_BUCK = 24u,
};

static uint16_t bench_app_step_tx(uint8_t step) {
  switch (step) {
    case BENCH_APP_STEP_UNLOCK_WRITE: return gl30_drv8316_make_frame(false, 3u, 0x03u);
    case BENCH_APP_STEP_CFG4_WRITE:   return gl30_drv8316_make_frame(false, 4u, 0x68u);
    case BENCH_APP_STEP_CFG4_READ:    return gl30_drv8316_make_frame(true,  4u, 0x00u);
    case BENCH_APP_STEP_CFG5_WRITE:   return gl30_drv8316_make_frame(false, 5u, 0x5Fu);
    case BENCH_APP_STEP_CFG5_READ:    return gl30_drv8316_make_frame(true,  5u, 0x00u);
    case BENCH_APP_STEP_CFG6_WRITE:   return gl30_drv8316_make_frame(false, 6u, 0x10u);
    case BENCH_APP_STEP_CFG6_READ:    return gl30_drv8316_make_frame(true,  6u, 0x00u);
    case BENCH_APP_STEP_CFG7_WRITE:   return gl30_drv8316_make_frame(false, 7u, 0x02u);
    case BENCH_APP_STEP_CFG7_READ:    return gl30_drv8316_make_frame(true,  7u, 0x00u);
    case BENCH_APP_STEP_CFG8_WRITE:   return gl30_drv8316_make_frame(false, 8u, 0x10u);
    case BENCH_APP_STEP_CFG8_READ:    return gl30_drv8316_make_frame(true,  8u, 0x00u);
    case BENCH_APP_STEP_CFG12_WRITE:  return gl30_drv8316_make_frame(false, 12u, 0x00u);
    case BENCH_APP_STEP_CFG12_READ:   return gl30_drv8316_make_frame(true, 12u, 0x00u);
    case BENCH_APP_STEP_STATUS_PRE_0:
    case BENCH_APP_STEP_STATUS_PRE_1:
    case BENCH_APP_STEP_STATUS_PRE_2:
      return gl30_drv8316_make_frame(true, (uint8_t)(step - BENCH_APP_STEP_STATUS_PRE_0), 0x00u);
    case BENCH_APP_STEP_STATUS_FINAL_0:
    case BENCH_APP_STEP_STATUS_FINAL_1:
    case BENCH_APP_STEP_STATUS_FINAL_2:
      return gl30_drv8316_make_frame(true, (uint8_t)(step - BENCH_APP_STEP_STATUS_FINAL_0), 0x00u);
    case BENCH_APP_STEP_CLEAR_WRITE:   return gl30_drv8316_make_frame(false, 4u, 0x69u);
    case BENCH_APP_STEP_CLEAR_READ:    return gl30_drv8316_make_frame(true,  4u, 0x00u);
    case BENCH_APP_STEP_LOCK_WRITE:    return gl30_drv8316_make_frame(false, 3u, 0x06u);
    case BENCH_APP_STEP_LOCK_READ:     return gl30_drv8316_make_frame(true,  3u, 0x00u);
    case BENCH_APP_STEP_FINAL_GAIN:    return gl30_drv8316_make_frame(true,  7u, 0x00u);
    case BENCH_APP_STEP_FINAL_BUCK:    return gl30_drv8316_make_frame(true,  8u, 0x00u);
    default: return 0x0000u;
  }
}

static uint16_t bench_app_step_rx(uint8_t step, const uint16_t pre_status[3], const uint16_t final_status[3]) {
  switch (step) {
    case BENCH_APP_STEP_UNLOCK_WRITE:
    case BENCH_APP_STEP_CFG4_WRITE:
    case BENCH_APP_STEP_CFG5_WRITE:
    case BENCH_APP_STEP_CFG6_WRITE:
    case BENCH_APP_STEP_CFG7_WRITE:
    case BENCH_APP_STEP_CFG8_WRITE:
    case BENCH_APP_STEP_CFG12_WRITE:
    case BENCH_APP_STEP_CLEAR_WRITE:
    case BENCH_APP_STEP_LOCK_WRITE:
      return 0x0000u;
    case BENCH_APP_STEP_FINAL_GAIN: return 0x0002u;
    case BENCH_APP_STEP_FINAL_BUCK: return 0x0010u;
    case BENCH_APP_STEP_CFG4_READ: return 0x0068u;
    case BENCH_APP_STEP_CFG5_READ: return 0x005Fu;
    case BENCH_APP_STEP_CFG6_READ: return 0x0010u;
    case BENCH_APP_STEP_CFG7_READ: return 0x0002u;
    case BENCH_APP_STEP_CFG8_READ: return 0x0010u;
    case BENCH_APP_STEP_CFG12_READ:return 0x0000u;
    case BENCH_APP_STEP_CLEAR_READ: return 0x0068u;
    case BENCH_APP_STEP_LOCK_READ: return 0x0006u;
    case BENCH_APP_STEP_STATUS_PRE_0:
    case BENCH_APP_STEP_STATUS_PRE_1:
    case BENCH_APP_STEP_STATUS_PRE_2:
      return pre_status[step - BENCH_APP_STEP_STATUS_PRE_0];
    case BENCH_APP_STEP_STATUS_FINAL_0:
    case BENCH_APP_STEP_STATUS_FINAL_1:
    case BENCH_APP_STEP_STATUS_FINAL_2:
      return final_status[step - BENCH_APP_STEP_STATUS_FINAL_0];
    default:
      return 0x0000u;
  }
}

static bool bench_app_clear_needed(const uint16_t status[3]) {
  return ((gl30_drv8316_status_word_faults(0u, status[0u]) |
           gl30_drv8316_status_word_faults(1u, status[1u]) |
           gl30_drv8316_status_word_faults(2u, status[2u])) & 0x08u) != 0u;
}

static size_t bench_app_queue_driver_configure(const uint16_t pre_status[3], const uint16_t final_status[3]) {
  const bool clear_needed = bench_app_clear_needed(pre_status);
  bench_hw_fake_spi_queue_clear_spi3();
  for (uint8_t step = BENCH_APP_STEP_UNLOCK_WRITE; step <= BENCH_APP_STEP_FINAL_BUCK; ++step) {
    if (!clear_needed && step >= BENCH_APP_STEP_CLEAR_WRITE && step <= BENCH_APP_STEP_CLEAR_READ) {
      continue;
    }
    bench_hw_fake_spi3_push(
      bench_app_step_tx(step),
      bench_app_step_rx(step, pre_status, final_status),
      false);
  }
  return bench_hw_fake_spi3_queue_len();
}

static size_t bench_app_queue_driver_status_only(void) {
  const uint16_t status[3] = {0x0808u, 0x0800u, 0x0800u};
  bench_hw_fake_spi_queue_clear_spi3();
  for (uint8_t step = BENCH_APP_STEP_STATUS_FINAL_0; step <= BENCH_APP_STEP_STATUS_FINAL_2; ++step) {
    bench_hw_fake_spi3_push(bench_app_step_tx(step), bench_app_step_rx(step, status, status), false);
  }
  bench_hw_fake_spi3_push(bench_app_step_tx(BENCH_APP_STEP_FINAL_GAIN), 0x0002u, false);
  bench_hw_fake_spi3_push(bench_app_step_tx(BENCH_APP_STEP_FINAL_BUCK), 0x0010u, false);
  return 5u;
}

static void bench_app_set_adc_sample(uint16_t ch1_rank1, uint16_t ch2_rank1,
                                    uint16_t ch3_rank1, uint16_t ch1_rank2, bool synchronized) {
  bench_hw_fake_set_adc_samples(ch1_rank1, ch2_rank1, ch3_rank1, ch1_rank2);
  bench_hw_fake_set_adc_sync_flags(synchronized, synchronized);
  Bench_AdcIRQ();
}

static void bench_app_start_prepare(void) {
  const uint16_t status[3] = {0x0808u, 0x0800u, 0x0800u};
  const size_t planned = bench_app_queue_driver_configure(status, status);
  command("PREPARE");
  CHECK(g_preparing && g_drv_ready, "fixture actually reached ADC calibration");
  CHECK_EQ_U32(g_spi3_state.transfers, planned, "configure consumed complete scripted exchange");
  CHECK_EQ_U32(g_spi3_state.mismatch_count, 0u, "configure used expected real SPI commands");
}

static uint16_t as5048a_make_response(uint16_t payload) {
  payload &= 0x7FFFu;
  uint16_t word = payload;
  word ^= word >> 8u;
  word ^= word >> 4u;
  word ^= word >> 2u;
  word ^= word >> 1u;
  return (uint16_t)(payload | ((uint16_t)(word & 1u) << 15u));
}

static void bench_app_force_prepare_commit_age(uint32_t older_than_us) {
  g_prepare_us = bench_now_us() - older_than_us;
  bench_hw_fake_set_bench_time_step(1u);
  g_enc_valid = true;
  g_enc_seq = 1u;
  g_enc_us = bench_now_us();
}

static void test_1_unsync_while_zeroing_aborts_and_turns_outputs_off(void) {
  bench_app_fake_reset_all();
  const float baseline_zero0 = g_zero[0];
  const float baseline_zero1 = g_zero[1];
  const float baseline_zero2 = g_zero[2];

  bench_app_start_prepare();
  bench_app_queue_driver_status_only();

  CHECK(g_preparing && g_drv_ready, "prepare enters active zeroing state");

  bench_app_set_adc_sample(1800u, 1810u, 1820u, 1110u, true);
  CHECK_EQ_U32(g_zero_count, 1u, "first synchronized sample is counted");

  bench_app_set_adc_sample(1830u, 1840u, 1850u, 1110u, false);
  CHECK(g_zero_sync_failed, "unsynced sample during zeroing marks sync failure");
  CHECK(g_zero_error_pending, "unsync raises pending zero error once");
  CHECK(!g_preparing, "unsync while preparing aborts immediately");
  CHECK(!g_drv_ready, "aborted prepare drops drv-ready flag");
  CHECK_EQ_U32(g_zero_count, 1u, "aborted unsync sample is not counted");
  CHECK(baseline_zero0 == g_zero[0] && baseline_zero1 == g_zero[1] && baseline_zero2 == g_zero[2],
        "aborted unsync path leaves offsets untouched");
  CHECK(LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin), "DRVOFF asserted on unsync");
  CHECK((g_gate.faults & BENCH_FAULT_ADC) != 0u, "ADC fault bit set on unsync");
}

static void test_2_unsync_after_512samples_still_rejected(void) {
  bench_app_fake_reset_all();
  bench_app_start_prepare();
  bench_app_queue_driver_status_only();

  for (uint32_t i = 0u; i < 512u; ++i) {
    bench_app_set_adc_sample(1860u, 1870u, 1880u, 1110u, true);
  }
  CHECK_EQ_U32(g_zero_count, 512u, "exactly 512 synchronized samples accumulated");

  bench_app_set_adc_sample(1800u, 1810u, 1820u, 1110u, false);
  CHECK(g_zero_sync_failed, "unsync after 512 samples is detected");
  CHECK_EQ_U32(g_zero_count, 512u, "sample count does not grow past 512 on late unsync");
  CHECK(!g_zero_valid, "zero offsets remain invalid after late unsync");
  CHECK(!g_preparing, "preparing drops when unsync appears after sample window");
  bench_app_force_prepare_commit_age(32000u);
  Bench_Loop();
  CHECK(!g_zero_valid && g_gate.mode != BENCH_PREPARED, "late sync failure cannot be committed by main loop");
  CHECK(g_zero[0] == 1861.0f && g_zero[1] == 1861.0f && g_zero[2] == 1861.0f,
        "late sync failure preserves all previous offsets");
}

static void test_3_clean_prepare_with_existing_adc_bad_baseline(void) {
  bench_app_fake_reset_all();
  g_adc_bad = 99u;
  bench_app_start_prepare();
  bench_app_queue_driver_status_only();

  /* Healthy enough for bench_gate_prepare once 30ms window is reached. */
  g_self_max = 4u;
  g_rate_ok = true;
  g_enc_valid = true;
  g_enc_seq = 1u;
  g_enc_us = bench_now_us();
  g_hw_ready = true;
  g_vm = 10.2f;

  for (uint32_t i = 0u; i < 512u; ++i) {
    bench_app_set_adc_sample(1865u, 1866u, 1867u, 1110u, true);
  }
  bench_app_force_prepare_commit_age(32000u);

  Bench_Loop();
  CHECK(!g_preparing, "prepare exits after successful prepare window");
  CHECK(g_zero_valid, "successful sample set becomes valid");
  CHECK(g_gate.mode == BENCH_PREPARED, "gate enters PREPARED after successful zeroing");
  CHECK(g_zero[0] == 1865.0f && g_zero[1] == 1866.0f && g_zero[2] == 1867.0f,
        "all zero offsets exactly match synchronized samples");
  CHECK_EQ_U32(g_zero_adc_bad_start, 99u, "prepare captures prior ADC bad baseline");
}

static void test_4_failed_stats_or_health_does_not_change_offsets(void) {
  /* Isolate each gate: excessive spread, invalid mean, otherwise-good health. */
  for (unsigned int failure = 0u; failure < 3u; ++failure) {
    bench_app_fake_reset_all();
    const float old_zero0 = g_zero[0], old_zero1 = g_zero[1], old_zero2 = g_zero[2];
    if (failure == 2u) { g_deadlines = 1u; }
    bench_app_start_prepare();
    bench_app_queue_driver_status_only();
    for (uint32_t i = 0u; i < 512u; ++i) {
      const uint16_t sample = failure == 0u ? (uint16_t)(1800u + i % 100u) :
          failure == 1u ? 2300u : 1865u;
      bench_app_set_adc_sample(sample, sample, sample, 1110u, true);
    }
    CHECK_EQ_U32(g_zero_count, 512u, "invalid candidate still exercised a full sample window");
    bench_app_force_prepare_commit_age(32000u);
    Bench_Loop();
    CHECK(!g_zero_valid && !g_preparing && !g_drv_ready,
          "each invalid mean/spread/health gate rejects preparation");
    CHECK(g_zero[0] == old_zero0 && g_zero[1] == old_zero1 && g_zero[2] == old_zero2,
          "each invalid candidate leaves all previous offsets unchanged");
    CHECK(LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin) && !TIM1->moe,
          "each invalid candidate leaves outputs hard off");
  }
}

static void test_5_zero_timeout_keeps_outputs_off(void) {
  bench_app_fake_reset_all();
  bench_app_start_prepare();
  bench_app_queue_driver_status_only();

  bench_app_set_adc_sample(1860u, 1860u, 1860u, 1110u, true);
  bench_app_force_prepare_commit_age(210000u);

  Bench_Loop();
  CHECK(!g_preparing, "prepare clears on 200ms timeout");
  CHECK(!g_drv_ready, "prepare timeout drops drv ready");
  CHECK(!g_zero_valid, "timeout keeps zero validity false");
  CHECK(LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin),
        "timeout forces safe outputs off");
}

static void test_6_prepare_resets_stats_and_baseline_before_drv_ready(void) {
  bench_app_fake_reset_all();
  g_adc_bad = 17u;
  bench_app_start_prepare();

  CHECK(g_preparing, "prepare enters first phase");
  CHECK_EQ_U32(g_zero_count, 0u, "zero sample count reset before finalize");
  CHECK(g_zero_sum[0] == 0u && g_zero_sum[1] == 0u && g_zero_sum[2] == 0u,
        "zero sums reset before drv_ready republish");
  CHECK_EQ_U32(g_zero_min[0], 4095u, "zero min baseline reset");
  CHECK_EQ_U32(g_zero_max[0], 0u, "zero max baseline reset");
  CHECK_EQ_U32(g_zero_adc_bad_start, 17u, "baseline bad count captured");
  CHECK(g_drv_ready, "drv_ready republished only after reset");
}

static void trace_app_adc_tick(void) {
  if (g_bench_hw_fake_primask) { return; }
  g_adc_us = TIM2->CNT;
  g_adc_count++;
  g_adc_synchronized = true;
}

static void trace_app_setup(void) {
  bench_app_fake_reset_all();
  g_tx_read = g_tx_write; /* Drain boot output before each command. */
  trace_app_adc_tick();
  g_bench_hw_fake_time_hook = trace_app_adc_tick;
}

static const char *trace_app_output(void) {
  static char text[2048];
  size_t len = 0u;
  while (g_tx_read != g_tx_write && len < sizeof(text) - 1u) {
    text[len++] = (char)g_tx[g_tx_read];
    g_tx_read = (uint16_t)((g_tx_read + 1u) & 2047u);
  }
  text[len] = '\0';
  return text;
}

static void trace_app_push(bool read, uint8_t reg, uint8_t data, uint16_t reply) {
  bench_hw_fake_spi3_push(gl30_drv8316_make_frame(read, reg, data), reply, false);
}

static void trace_app_status(bool reserved, bool cleared) {
  trace_app_push(true, 0u, 0u, cleared ? 0x0808u : 0u);
  trace_app_push(true, 1u, 0u, cleared ? 0x0800u : 0u);
  trace_app_push(true, 2u, 0u, (cleared ? 0x0800u : 0u) | (reserved ? 0x80u : 0u));
}

static void bench_app_seed_haptic_ready(void) {
  const uint32_t now = bench_now_us();
  g_gate.mode = BENCH_READY;
  g_gate.faults = 0u;
  g_gate.calibrated = true;
  g_gate.health = BENCH_HEALTH_ALL;
  g_gate.started_us = now;
  g_gate.lease_us = now;
  g_gate.duration_us = 0u;
  g_gate.iq_ma = 0;
  g_drv_ready = true;
  g_drv_checked_us = now;
  g_hw_ready = true;
  g_rate_ok = true;
  g_self_left = 0u;
  g_self_fail = 0u;
  g_self_max = 1u;
  g_deadlines = 0u;
  g_enc_valid = true;
  g_enc_seq = 1u;
  g_enc_us = now;
  g_zero_valid = true;
  g_adc_us = now;
  g_vm = 10.3f;
  g_haptic_running = false;
  g_haptic_iq = 0.0f;
  g_haptic_us = now;
  g_applied_field = 0.0f;
  g_align_stage = 0u;
  g_align_current_sum = 0.0f;
  g_align_current_count = 0u;
  g_align_voltage_sum = 0.0f;
  g_align_origin = 0.0f;
  g_align_forward = 0.0f;
  g_zero_angle = 0.0f;
  g_direction = 1.0f;
  bench_hw_fake_set_nfault_input(true);
  bench_hw_fake_set_button_input(false);
  bench_hw_idle();
  LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin);
}

static void test_trace_command_never_publishes_ready_and_preserves_cache(void) {
  for (unsigned int reserved = 0u; reserved < 2u; ++reserved) {
    trace_app_setup();
    const bench_drv_diag_t before = g_drv_diag;
    static const uint8_t regs[][2] = {
      {4u, 0x68u}, {5u, 0x5fu}, {6u, 0x10u}, {7u, 2u}, {8u, 0x10u}, {12u, 0u}
    };
    trace_app_status(reserved, false);
    trace_app_push(false, 3u, 3u, 0u);
    trace_app_push(true, 3u, 0u, 3u);
    trace_app_status(reserved, false);
    for (unsigned int i = 0u; i < 6u; ++i) {
      trace_app_push(false, regs[i][0], regs[i][1], 0u);
      trace_app_push(true, regs[i][0], 0u, regs[i][1]);
      trace_app_status(reserved, false);
    }
    const uint8_t ctrl2 = 0x68u;
    trace_app_push(true, 4u, 0u, ctrl2);
    trace_app_push(false, 4u, (uint8_t)(ctrl2 | 1u), ctrl2);
    trace_app_push(true, 4u, 0u, ctrl2);
    trace_app_status(reserved, true);
    trace_app_push(false, 3u, 6u, 3u);
    trace_app_push(true, 3u, 0u, 6u);
    const size_t frames = g_spi3_state.queue_len;
    command("DRV_TRACE MOTOR_DISCONNECTED");
    CHECK(g_spi3_state.transfers == frames && !g_spi3_state.mismatch_count,
          "application consumes exact hardware trace without using PREPARE");
    const char *output = trace_app_output();
    CHECK(strstr(output, "complete=1") && strstr(output, "OK DRV_TRACE_OUTPUT_OFF_NOT_READY"),
          "trace report is complete and explicitly not ready");
    CHECK(!g_uart_errors, "summary fits UART ring without truncation");
    CHECK(g_gate.mode == BENCH_OFF && !g_gate.calibrated && !g_drv_ready && !g_zero_valid &&
          !g_preparing && !g_tracing && !g_zero_count, "diagnostic never grants preparation or alignment");
    CHECK(!TIM1->moe && !TIM1->CCER && LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin),
          "application always returns hard off");
    CHECK(!bench_hw_fake_tim1_enable_all_outputs_count() && !bench_hw_fake_tim1_cc_enable_count(),
          "application never enables PWM even when cleared status is healthy");
    CHECK(g_drv_diag.raw_status == before.raw_status && g_drv_diag.rx == before.rx,
          "existing PREPARE evidence remains unchanged");
    CHECK_EQ_U32(g_driver_trace.count, 9u, "all six configuration checkpoints run with or without bit7");
    CHECK_EQ_U32(g_driver_trace.last.config_read_mask, 0x3Fu, "all configuration readbacks verified");
    CHECK_EQ_U32(g_driver_trace.last.normalized_status, 0u, "reserved bit does not become a software fault");
    CHECK_EQ_U32(g_driver_trace.points[8].rx[2], reserved ? 0x0880u : 0x0800u,
                 "cached trace retains the unmasked STAT2 word");
    for (unsigned int i = 0u; i < g_driver_trace.count; ++i) {
      char line[32];
      (void)snprintf(line, sizeof(line), "DRV_TRACE_GET %u", i);
      command(line);
      output = trace_app_output();
      CHECK(strstr(output, "DRV_TRACE_POINT index=") && strstr(output, "s2=") &&
            strstr(output, "OK DRV_TRACE_GET_CACHED_OUTPUT_OFF"), "every indexed record is complete");
      CHECK_EQ_U32(g_spi3_state.transfers, frames, "cached record retrieval never touches SPI");
      CHECK(!g_uart_errors, "individual trace record fits UART ring");
    }
    command("ALIGN");
    CHECK(!TIM1->moe && g_gate.mode == BENCH_OFF, "trace completion cannot authorize ALIGN");
  }
}

static void test_trace_command_guards_and_bad_indexes(void) {
  for (unsigned int failure = 0u; failure < 16u; ++failure) {
    trace_app_setup();
    if (failure == 0u) { g_vm = 0.0f; }
    if (failure == 1u) { g_vm = 15.1f; }
    if (failure == 2u) { g_vm = NAN; }
    if (failure == 3u) { g_gate.faults = BENCH_FAULT_TEST; }
    if (failure == 4u) { g_gate.mode = BENCH_PREPARED; }
    if (failure == 5u) { g_preparing = true; }
    if (failure == 6u) { g_self_left = 1u; }
    if (failure == 7u) { g_rate_ok = false; }
    if (failure == 8u) { g_hw_ready = false; }
    if (failure == 9u) { bench_hw_fake_set_button_input(true); }
    if (failure == 10u) { TIM1->moe = true; }
    if (failure == 11u) { LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin); }
    if (failure == 12u) { g_self_fail = 1u; }
    if (failure == 13u) { g_deadlines = 1u; }
    if (failure == 14u) { g_bench_hw_fake_time_hook = NULL; g_adc_us = bench_now_us() - 1000u; }
    if (failure == 15u) { g_bench_hw_fake_time_hook = NULL; g_adc_synchronized = false; }
    command("DRV_TRACE MOTOR_DISCONNECTED");
    CHECK_EQ_U32(g_spi3_state.transfers, 0u, "unsafe application entry never communicates with driver");
    CHECK(strstr(trace_app_output(), failure < 13u ? "ERR DRV_TRACE_REQUIRES" : "reason=GUARD"),
          "unsafe application entry has explicit rejection");
  }
  trace_app_setup();
  static const char *const malformed[] = {"DRV_TRACE", "DRV_TRACE WRONG", "DRV_TRACE_GET -1",
      "DRV_TRACE_GET 9", "DRV_TRACE_GET 0junk", "DRV_TRACE_GET 0"};
  for (unsigned int i = 0u; i < sizeof(malformed) / sizeof(malformed[0]); ++i) {
    command((char *)malformed[i]);
    CHECK(strstr(trace_app_output(), "ERR"), "missing isolation token or invalid index rejected");
    CHECK_EQ_U32(g_spi3_state.transfers, 0u, "malformed or cached commands cannot start hardware trace");
  }
}

static void test_alignment_tick_success_forward_and_negative(void) {
  const float target = 0.9f * (2.0f * (float)3.14159265358979323846f / 7.0f);
  for (unsigned int pass = 0u; pass < 2u; ++pass) {
    bench_app_fake_reset_all();
    bench_app_seed_haptic_ready();
    const uint32_t start = bench_now_us();
    g_gate.mode = BENCH_ALIGNING;
    g_gate.faults = 0u;
    g_gate.calibrated = false;
    g_gate.started_us = start;
    g_align_stage = 0u;
    g_align_current_sum = 0.0f;
    g_align_current_count = 0u;
    g_align_voltage_sum = 0.0f;
    g_align_origin = 0.0f;
    g_align_forward = 0.0f;
    g_zero_angle = 0.0f;

    g_foc.i_d_a = 0.05f;
    alignment_tick(start + 100000u);
    CHECK(fabsf(g_align_voltage - 0.0475f) < 1e-5f && g_foc.i_d_ref_a == 0.0f, "alignment first 100ms ramps voltage, not current");

    alignment_tick(start + 200000u);
    alignment_tick(start + 300000u);
    alignment_tick(start + 400000u);
    CHECK(fabsf(g_align_voltage - 0.19f) < 1e-5f && g_foc.i_d_ref_a == 0.0f, "alignment reaches 0.19V at 400ms");
    alignment_tick(start + 800000u);
    CHECK(fabsf(g_align_voltage - 0.38f) < 1e-5f && g_foc.i_d_ref_a == 0.0f, "alignment reaches 0.38V at 800ms, including timer wrap");

    g_foc.i_d_a = 0.40f;
    g_foc.v_d_v = 0.10f;
    for (uint32_t t = 910000u; t < 1000000u; t += 50000u) {
      g_foc.v_d_v = 0.10f;
      alignment_tick(start + t);
    }
    CHECK(g_align_current_count > 0u, "alignment samples are accumulated after 900ms");

    alignment_tick(start + 1050000u);
    CHECK_EQ_U32(g_align_stage, 1u, "alignment enters motion stage after 1s current window");
    CHECK_EQ_U32(g_gate.mode, BENCH_ALIGNING, "alignment holds ALIGNING until 4.5s motion window completes");

    alignment_tick(start + 2500000u);
    CHECK(g_foc.theta_elec_rad > 2.0f && g_foc.theta_elec_rad < 4.0f, "alignment uses one-way smoothstep trajectory");

    alignment_tick(start + 4500000u - 100000u);
    CHECK_EQ_U32(g_align_stage, 1u, "alignment holds motion stage through 500ms settle");
    CHECK(fabsf(g_foc.theta_elec_rad) < 1e-5f, "alignment settles at electrical zero during 500ms dwell");

    g_foc.theta_unwrapped_rad = pass ? -target : target;
    g_foc.theta_mech_rad = 1.234f + (float)pass;
    alignment_tick(start + 4500000u);
    CHECK_EQ_U32(g_align_stage, 2u, "alignment advances to snapshot-locked stage after settle window");
    CHECK(fabsf((g_align_forward > 0.0f ? g_align_forward : -g_align_forward) - target) < 0.02f,
          "alignment travel hits 0.8..1.2 electrical pitch window");
    CHECK(pass ? (g_direction < 0.0f) : (g_direction > 0.0f),
          pass ? "alignment direction is negative when final move is reverse" :
                 "alignment direction is positive when final move is forward");
    CHECK_EQ_U32(g_gate.mode, BENCH_READY, "bench_gate_alignment_done(true) restores READY");
    CHECK(g_gate.calibrated, "alignment success keeps calibration asserted");
    CHECK(fabsf(g_zero_angle - (1.234f + (float)pass)) < 1e-5f,
        "alignment snapshots mechanical zero angle on completion");
  }
}

static void test_alignment_tick_voltage_mode_uses_fixed_voltage_and_trip_faults(void) {
  bench_app_fake_reset_all();
  bench_app_seed_haptic_ready();
  const uint32_t start = bench_now_us();
  g_gate.health = BENCH_HEALTH_ALL;
  g_gate.mode = BENCH_ALIGNING;
  g_gate.faults = 0u;
  g_gate.calibrated = false;
  g_gate.started_us = start - 1500000u;
  g_gate.lease_us = start;
  g_gate.duration_us = 6000000u;
  g_align_stage = 1u;
  g_align_voltage = 0.12f;
  g_align_origin = 0.0f;
  g_align_forward = 0.0f;
  g_align_current_sum = 0.0f;
  g_align_current_count = 0u;
  g_align_voltage_sum = 0.0f;
  g_foc.i_d_ref_a = 0.0f;
  g_foc.i_q_ref_a = 0.0f;
  g_foc.v_d_v = 0.03f;
  g_foc.v_q_v = 0.41f;
  g_foc.i_d_a = 0.0f;
  g_foc.i_q_a = 0.0f;
  g_foc.theta_unwrapped_rad = 0.0f;
  g_foc.theta_elec_rad = 0.0f;
  g_foc.theta_mech_rad = 0.0f;
  g_rate_ok = true;
  g_drv_ready = true;
  g_hw_ready = true;
  g_self_left = 0u;
  g_self_fail = 0u;
  g_self_max = 1u;
  g_deadlines = 0u;
  g_zero_valid = true;
  g_enc_valid = true;
  g_enc_seq = 7u;
  g_last_observer_seq = 0u;
  g_enc_us = start;
  g_adc_us = start;
  g_drv_checked_us = start;
  g_haptic_running = false;
  g_haptic_iq = 0.0f;
  g_haptic_us = start;
  bench_hw_fake_set_bench_time_step(1u);
  bench_hw_fake_set_button_input(false);
  bench_hw_fake_set_nfault_input(true);
  TIM1->moe = true;
  TIM1->BDTR |= LL_TIM_BDTR_MOE;
  TIM1->direction = LL_TIM_COUNTERDIRECTION_DOWN;
  TIM1->CNT = 500u;
  LL_TIM_EnableAllOutputs(TIM1);

  bench_app_set_adc_sample(1861u, 1861u, 1861u, 1110u, true);
  CHECK(g_gate.mode == BENCH_ALIGNING && !g_gate.faults, "alignment stage1 fixture remains in ALIGNING");
  CHECK(LL_TIM_IsEnabledAllOutputs(TIM1) && TIM1->moe && TIM1->direction == LL_TIM_COUNTERDIRECTION_DOWN,
        "stage1 fixture runs with MOE-enabled timer and clockwise-down direction");
  CHECK(fabsf(g_foc.v_d_v - 0.12f) < 1e-5f && fabsf(g_foc.v_q_v) < 1e-6f,
        "voltage stage uses fixed 0.12 Vd and zero Vq despite zero refs");

  const uint32_t disable_count_before = bench_hw_fake_tim1_disable_all_outputs_count();
  bench_app_set_adc_sample(1861u, 1861u, 1861u, 1110u, true);
  CHECK(g_gate.mode == BENCH_ALIGNING && !g_gate.faults, "stage1 valid sample stays in ALIGNING without fault");
  CHECK(LL_TIM_IsEnabledAllOutputs(TIM1), "valid stage1 voltage control keeps timer outputs enabled");
  CHECK_EQ_U32(bench_hw_fake_tim1_disable_all_outputs_count(), disable_count_before,
               "valid stage1 sample does not disable outputs");

  bench_app_set_adc_sample(2400u, 1861u, 1861u, 1110u, true);
  CHECK(g_gate.mode == BENCH_FAULT && g_gate.faults == BENCH_FAULT_CURRENT,
        "overcurrent on stage1 sample latches BENCH_FAULT_CURRENT");
  CHECK(!LL_TIM_IsEnabledAllOutputs(TIM1) && LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin),
        "overcurrent fault transitions to hard-off outputs");
}

static void test_alignment_tick_ramps_voltage_and_completes_with_forward_travel(void) {
  bench_app_fake_reset_all();
  bench_app_seed_haptic_ready();
  const uint32_t start = bench_now_us();
  g_gate.mode = BENCH_ALIGNING;
  g_gate.faults = 0u;
  g_gate.calibrated = false;
  g_gate.started_us = start;
  g_align_stage = 0u;
  g_align_current_sum = 0.0f;
  g_align_current_count = 0u;
  g_align_voltage_sum = 0.0f;
  g_align_origin = 0.0f;
  g_align_forward = 0.0f;
  g_zero_angle = 0.0f;

  g_foc.i_d_a = 0.05f;
  g_foc.v_d_v = 0.08f;
  alignment_tick(start + 100000u);
  CHECK(fabsf(g_align_voltage - 0.0475f) < 1e-5f && g_foc.i_d_ref_a == 0.0f, "alignment soft ramp is 0.0475V at 100ms");

  alignment_tick(start + 200000u);
  alignment_tick(start + 300000u);
  alignment_tick(start + 400000u);
  CHECK(fabsf(g_align_voltage - 0.19f) < 1e-5f && g_foc.i_d_ref_a == 0.0f, "alignment soft ramp is 0.19V at 400ms");
  alignment_tick(start + 800000u);
  CHECK(fabsf(g_align_voltage - 0.38f) < 1e-5f && g_foc.i_d_ref_a == 0.0f, "alignment reaches the 0.38V static hold at 800ms");
  alignment_tick(start + 900000u);
  CHECK(g_align_current_count == 0u, "alignment mean excludes ramp and first 100ms settling");

  g_foc.i_d_a = 0.40f;
  g_foc.v_d_v = 0.12f;
  for (uint32_t t = 910000u; t < 1000000u; t += 50000u) {
    alignment_tick(start + t);
  }
  CHECK(g_align_current_count > 0u, "alignment still accumulates current and voltage in stage0 hold");

  alignment_tick(start + 1050000u);
  CHECK_EQ_U32(g_align_stage, 1u, "alignment moves to voltage hold stage at 1s");
  CHECK(fabsf(g_foc.i_d_ref_a) < 1e-6f && g_foc.i_q_ref_a == 0.0f,
        "alignment starts voltage stage with current refs held at zero");
  CHECK(g_align_origin >= 0.0f, "alignment origin is latched once current window ends");

  g_foc.theta_unwrapped_rad = 0.0f;
  alignment_tick(start + 2500000u);
  CHECK(g_foc.theta_elec_rad > 2.0f && g_foc.theta_elec_rad < 4.0f,
        "alignment voltage stage drives smoothfield trajectory");

  g_foc.theta_unwrapped_rad = 0.90f;
  g_foc.theta_mech_rad = 1.234f;
  alignment_tick(start + 4500000u);
  CHECK_EQ_U32(g_align_stage, 2u, "alignment finalizes at settle completion when travel is valid");
  CHECK(g_gate.mode == BENCH_READY && g_gate.calibrated, "voltage-stage alignment can return READY");
  CHECK(fabsf(g_align_forward - 0.90f) < 0.02f, "alignment forward travel remains near one electrical revolution/7");
  CHECK(g_direction > 0.0f, "alignment keeps forward direction when travel is positive");
}

static void test_alignment_tick_rejects_invalid_average_voltage_sample(void) {
  bench_app_fake_reset_all();
  bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_ALIGNING;
  g_gate.faults = 0u;
  g_gate.calibrated = false;
  g_gate.started_us = bench_now_us();
  g_align_stage = 0u;
  g_align_current_sum = 0.0f;
  g_align_current_count = 0u;
  g_align_voltage_sum = 0.0f;
  g_align_origin = 0.0f;
  g_align_forward = 0.0f;

  g_foc.i_d_a = 0.40f;
  g_foc.v_d_v = NAN;
  for (uint32_t t = 510000u; t < 1000000u; t += 50000u) {
    alignment_tick(g_gate.started_us + t);
  }
  alignment_tick(g_gate.started_us + 1050000u);
  CHECK(g_gate.mode == BENCH_FAULT && g_gate.faults == BENCH_FAULT_ALIGN,
        "alignment fails immediately when average voltage sample is NaN");
}

static void test_alignment_tick_fails_with_bad_current_windows(void) {
  bench_app_fake_reset_all();
  bench_app_seed_haptic_ready();
  const uint32_t start = bench_now_us();
  g_gate.mode = BENCH_ALIGNING;
  g_gate.faults = 0u;
  g_gate.calibrated = false;
  g_gate.started_us = start;
  g_align_stage = 0u;
  g_align_current_sum = 0.0f;
  g_align_current_count = 0u;
  g_align_voltage_sum = 0.0f;

  g_foc.i_d_a = 0.10f;
  g_foc.v_d_v = 0.10f;
  for (uint32_t t = 510000u; t < 1000000u; t += 50000u) {
    g_foc.v_d_v = 0.10f;
    alignment_tick(start + t);
  }
  alignment_tick(start + 1050000u);
  CHECK(g_gate.mode == BENCH_FAULT && g_gate.faults == BENCH_FAULT_ALIGN,
        "alignment fails latch and latches align fault when current is under 0.28A");

  bench_app_fake_reset_all();
  bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_ALIGNING;
  g_gate.faults = 0u;
  g_gate.calibrated = false;
  g_gate.started_us = bench_now_us();
  g_align_stage = 0u;
  g_align_current_sum = 0.0f;
  g_align_current_count = 0u;
  g_align_voltage_sum = 0.0f;
  g_foc.i_d_a = 0.60f;
  g_foc.v_d_v = 0.10f;
  for (uint32_t t = 510000u; t < 1000000u; t += 50000u) {
    g_foc.v_d_v = 0.10f;
    alignment_tick(g_gate.started_us + t);
  }
  alignment_tick(g_gate.started_us + 1050000u);
  CHECK(g_gate.mode == BENCH_FAULT && g_gate.faults == BENCH_FAULT_ALIGN,
        "alignment fails latch and latches align fault when current is above 0.52A");

  bench_app_fake_reset_all();
  bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_ALIGNING;
  g_gate.faults = 0u;
  g_gate.calibrated = false;
  g_gate.started_us = bench_now_us();
  g_align_stage = 0u;
  g_align_current_sum = 0.0f;
  g_align_current_count = 0u;
  g_align_voltage_sum = 0.0f;
  g_foc.i_d_a = 0.0f;
  g_foc.v_d_v = 0.10f;
  for (uint32_t t = 510000u; t < 1000000u; t += 50000u) {
    g_foc.v_d_v = 0.10f;
    alignment_tick(g_gate.started_us + t);
  }
  alignment_tick(g_gate.started_us + 1050000u);
  CHECK(g_gate.mode == BENCH_FAULT && g_gate.faults == BENCH_FAULT_ALIGN,
        "alignment fails latch and latches align fault when current sum is zero");

  bench_app_fake_reset_all();
  bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_ALIGNING;
  g_gate.faults = 0u;
  g_gate.calibrated = false;
  g_gate.started_us = bench_now_us();
  g_align_stage = 0u;
  g_align_current_sum = 0.0f;
  g_align_current_count = 0u;
  g_align_voltage_sum = 0.0f;

  g_foc.i_d_a = 0.40f;
  g_foc.v_d_v = 0.10f;
  for (uint32_t t = 510000u; t < 1000000u; t += 50000u) {
    g_foc.v_d_v = 0.10f;
    alignment_tick(g_gate.started_us + t);
  }
  alignment_tick(g_gate.started_us + 1050000u);
  CHECK_EQ_U32(g_align_stage, 1u, "alignment enters motion stage with valid current");

  g_foc.theta_unwrapped_rad = 0.55f;
  alignment_tick(g_gate.started_us + 4500000u);
  CHECK(g_gate.mode == BENCH_FAULT && g_gate.faults == BENCH_FAULT_ALIGN,
        "alignment fails latch on insufficient travel");
  CHECK(!g_gate.calibrated && g_gate.mode == BENCH_FAULT,
        "insufficient travel does not keep calibration nor READY state");

  bench_app_fake_reset_all();
  bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_ALIGNING;
  g_gate.faults = 0u;
  g_gate.calibrated = false;
  g_gate.started_us = bench_now_us();
  g_align_stage = 0u;
  g_align_current_sum = 0.0f;
  g_align_current_count = 0u;
  g_align_voltage_sum = 0.0f;

  g_foc.i_d_a = 0.40f;
  g_foc.v_d_v = 0.10f;
  for (uint32_t t = 510000u; t < 1000000u; t += 50000u) {
    g_foc.v_d_v = 0.10f;
    alignment_tick(g_gate.started_us + t);
  }
  alignment_tick(g_gate.started_us + 1050000u);
  CHECK_EQ_U32(g_align_stage, 1u, "alignment enters motion stage with valid current");

  g_foc.theta_unwrapped_rad = 2.2f;
  alignment_tick(g_gate.started_us + 4500000u);
  CHECK(g_gate.mode == BENCH_FAULT && g_gate.faults == BENCH_FAULT_ALIGN,
        "alignment fails latch on excessive travel");
  CHECK(!g_gate.calibrated && g_gate.mode == BENCH_FAULT,
        "excessive travel does not keep calibration nor READY state");
}

static void test_latch_preserves_first_motion_snapshot(void) {
  bench_app_fake_reset_all();
  bench_gate_init(&g_gate);
  g_foc.velocity_rad_s = 0.12f;
  g_foc.theta_unwrapped_rad = 1.0f;
  g_foc.theta_elec_rad = 1.0f;
  g_applied_field = -2.0f;
  g_enc_angle = 123u;
  latch(BENCH_FAULT_CURRENT);
  CHECK(fabsf(g_trip_velocity - 0.12f) < 1e-6f, "first latch stores seeded velocity 0.12");
  CHECK(fabsf(g_trip_angle - 1.0f) < 1e-6f, "first latch stores seeded theta_unwrapped 1.0");
  CHECK(fabsf(g_trip_field - (-2.0f)) < 1e-6f, "first latch stores seeded theta_elec -2.0");
  CHECK(g_trip_encoder == 123u, "first latch stores seeded encoder 123");
  const float first_velocity = g_trip_velocity;
  const float first_angle = g_trip_angle;
  const float first_field = g_trip_field;
  const uint16_t first_encoder = g_trip_encoder;
  g_foc.velocity_rad_s = -1.0f;
  g_foc.theta_unwrapped_rad = -3.0f;
  g_foc.theta_elec_rad = 4.0f;
  g_enc_angle = 456u;
  latch(BENCH_FAULT_UART);
  CHECK(first_velocity == g_trip_velocity && first_angle == g_trip_angle &&
        first_field == g_trip_field && first_encoder == g_trip_encoder,
        "first latch preserves one motion snapshot even after later latch");
}

static void test_haptic_command_requires_ready_health_and_range(void) {
  bench_app_fake_reset_all();
  bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_READY;
  g_gate.calibrated = false;
  command("HAPTIC 1 100");
  CHECK(strstr(trace_app_output(), "ERR HAPTIC_NEEDS_ALIGNMENT"),
        "HAPTIC rejects when not calibrated");

  g_gate.calibrated = true;
  g_self_left = 1u;
  command("HAPTIC 1 100");
  CHECK(strstr(trace_app_output(), "ERR HAPTIC_NEEDS_ALIGNMENT"),
        "HAPTIC rejects when timing health is not complete");

  g_self_left = 0u;
  command("HAPTIC 1 0");
  CHECK(strstr(trace_app_output(), "ERR HAPTIC_RANGE_KIND_0_7_MS_10000"),
        "HAPTIC rejects zero duration");
  command("HAPTIC 1 10001");
  CHECK(strstr(trace_app_output(), "ERR HAPTIC_RANGE_KIND_0_7_MS_10000"),
        "HAPTIC rejects duration above 10000ms");
  command("HAPTIC 99 100");
  CHECK(strstr(trace_app_output(), "ERR HAPTIC_RANGE_KIND_0_7_MS_10000"),
        "HAPTIC rejects kind outside 0..7");

  command("HAPTIC 1 100");
  CHECK(strstr(trace_app_output(), "OK HAPTIC_KEEPALIVE"),
        "HAPTIC accepts valid kind and duration under full health");
  CHECK(g_haptic_running && g_gate.mode == BENCH_ACTIVE && g_gate.calibrated,
        "HAPTIC command sets active haptic state when valid");
}

static void test_drv_poll_reports_cached_status_and_respects_guards(void) {
  bench_app_fake_reset_all();
  LL_GPIO_SetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin);

  command("DRV_POLL");
  const char *first = trace_app_output();
  CHECK(strstr(first, "DRV_POLL NONE") && strstr(first, "OK DRV_POLL_OUTPUT_OFF"),
        "DRV_POLL with no prior failure reports empty cache and stays OFF");
  CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), 0u, "empty DRV_POLL path does not use SPI");

  bench_app_fake_reset_all();
  LL_GPIO_SetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin);
  g_gate.mode = BENCH_FAULT;
  g_gate.faults = BENCH_FAULT_TEST;
  g_gate.health = BENCH_HEALTH_ALL;
  const uint16_t status[3] = {0x0808u, 0x0800u, 0x0800u};
  bench_hw_fake_spi3_push(gl30_drv8316_make_frame(true, 0u, 0u), status[0u], false);
  bench_hw_fake_spi3_push(gl30_drv8316_make_frame(true, 1u, 0u), status[1u], false);
  bench_hw_fake_spi3_push(gl30_drv8316_make_frame(true, 2u, 0u), status[2u], false);
  bench_hw_fake_spi3_push(gl30_drv8316_make_frame(true, 7u, 0u), 0x0002u, false);
  bench_hw_fake_spi3_push(gl30_drv8316_make_frame(true, 8u, 0u), 0x0010u, false);
  bench_hw_fake_set_nfault_input(false);
  uint32_t status_value = 0u;
  CHECK(!bench_hw_driver_status(&status_value), "pre-run status failure captures poll cache for DRV_POLL");
  CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), 5u, "status failure consumes exactly five SPI3 reads");
  command("DRV_POLL");
  const char *cached = trace_app_output();
  CHECK(cached && strstr(cached, "DRV_POLL cached=1") &&
        strstr(cached, "POLL_ENTRY") && strstr(cached, "POLL_RESULT") &&
        strstr(cached, "OK DRV_POLL_OUTPUT_OFF"),
        "cached DRV_POLL reports first failed periodic state and output-off status");
  CHECK(g_gate.faults == BENCH_FAULT_TEST, "DRV_POLL does not clear a latched fault");

  command("DRV_POLL");
  const char *still_cached = trace_app_output();
  CHECK(strstr(still_cached, "DRV_POLL cached=1") &&
        strstr(still_cached, "OK DRV_POLL_OUTPUT_OFF"),
        "cached DRV_POLL remains stable without issuing SPI");
  CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), 5u, "DRV_POLL command reads cached poll diagnostics only");

  bench_app_fake_reset_all();
  bench_hw_fake_set_button_input(true);
  command("DRV_POLL");
  CHECK(strstr(trace_app_output(), "ERR DRV_POLL_REQUIRES_OFF_OR_FAULT_RELEASE_BUTTON_MOE0_DRVOFF1"),
        "DRV_POLL guard rejects when deadman button is asserted");
  CHECK_EQ_U32(bench_hw_fake_spi3_queue_pos(), 0u, "guarded DRV_POLL does not queue/consume SPI");
}

static void test_iq_and_haptic_stale_reference_behaviors(void) {
  bench_app_fake_reset_all();
  bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_READY;
  g_gate.calibrated = true;
  command("IQ 10 2001");
  CHECK(strstr(trace_app_output(), "ERR IQ_RANGE_MA_100_MS_2000"), "IQ rejects above 2000ms");
  command("IQ 10 2000");
  CHECK(strstr(trace_app_output(), "OK IQ_KEEPALIVE"), "IQ still accepts up to 2000ms");

  CHECK(g_gate.mode == BENCH_ACTIVE && !g_haptic_running, "IQ command enters ACTIVE and does not set HAPTIC flag");

  command("HAPTIC 1 500");
  CHECK(strstr(trace_app_output(), "ERR HAPTIC_NEEDS_ALIGNMENT"),
        "HAPTIC is rejected while already in IQ ACTIVE mode");

  command("STOP");
  CHECK(strstr(trace_app_output(), "OK STOP"), "STOP confirms and clears haptic command state");
  CHECK(!g_haptic_running && !bench_gate_output_allowed(&g_gate),
        "STOP clears HAPTIC/IQ execution path");

  bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_READY;
  g_gate.calibrated = true;
  command("HAPTIC 1 500");
  CHECK(strstr(trace_app_output(), "OK HAPTIC_KEEPALIVE"), "HAPTIC command accepted under READY+health");
  CHECK(g_haptic_running && g_gate.mode == BENCH_ACTIVE,
        "HAPTIC transitions to ACTIVE for command window");

  const uint32_t now = bench_now_us();
  TIM1->moe = true;
  TIM1->BDTR |= LL_TIM_BDTR_MOE;
  TIM1->direction = LL_TIM_COUNTERDIRECTION_DOWN;
  TIM1->CNT = 1000u;
  g_gate.health = BENCH_HEALTH_ALL;
  g_gate.faults = 0u;
  g_gate.started_us = now;
  g_gate.lease_us = now;
  g_gate.duration_us = 10000000u;
  g_rate_ok = true;
  g_self_left = 0u;
  g_self_fail = 0u;
  g_self_max = 1u;
  g_deadlines = 0u;
  g_zero_valid = true;
  g_drv_ready = true;
  g_hw_ready = true;
  g_enc_valid = true;
  g_enc_angle = 1u;
  gl30_foc_observer_tick_4k(&g_encoder_observer[g_encoder_observer_index],
      TWO_PI / 16384.0f, true);
  g_enc_us = now;
  g_last_observer_seq = 0u;
  g_enc_seq = 7u;
  g_adc_us = now;
  g_drv_checked_us = now;
  g_haptic_us = now - 1000u;
  g_haptic_iq = 0.04f;
  g_foc.theta_unwrapped_rad = 0.0f;
  g_foc.theta_elec_rad = 0.0f;
  g_foc.theta_mech_rad = 0.0f;
  g_foc.i_d_ref_a = 0.0f;
  g_foc.i_q_ref_a = 0.0f;
  CHECK(g_haptic_running, "HAPTIC command leaves haptic-running flag asserted");
  CHECK(g_gate.mode == BENCH_ACTIVE && !g_gate.faults && bench_gate_output_allowed(&g_gate),
        "haptic holds ACTIVE/no-fault and remains output-allowed while evaluating stale reference");
  CHECK(TIM1->moe, "MOE is on during stale-reference evaluation");
  bench_app_set_adc_sample(1860u, 1860u, 1860u, 1110u, true);
  CHECK(fabsf(g_foc.i_q_ref_a - 0.04f) < 1e-5f, "fresh cached haptic reference is applied while ACTIVE");

  g_haptic_us = bench_now_us() - 6000u;
  bench_app_set_adc_sample(1860u, 1860u, 1860u, 1110u, true);
  CHECK(g_gate.mode == BENCH_ACTIVE && !g_gate.faults,
        "stale-reference sample remains ACTIVE and no fault");
  CHECK(fabsf(g_foc.i_q_ref_a) < 1e-6f, "haptic ref older than 5ms becomes zero Iq_ref");
}

static void test_trace_adc_irq_keeps_inputs_low_and_fault_overrides_idle(void) {
  trace_app_setup();
  g_tracing = true;
  LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin);
  bench_app_set_adc_sample(1860u, 1860u, 1860u, 1110u, true);
  CHECK(!LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin), "ADC IRQ preserves trace DRVOFF state");
  CHECK(!TIM1->moe && !TIM1->CCER, "trace ADC IRQ only parks six inputs, never enables outputs");
  CHECK_EQ_U32(g_zero_count, 0u, "trace cannot collect calibration samples");
  g_gate.faults = BENCH_FAULT_ADC;
  bench_app_set_adc_sample(1860u, 1860u, 1860u, 1110u, true);
  CHECK(!g_tracing && LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin),
        "latched fault overrides trace idle and asserts hard stop");
}

static void arm_app_seed_snapshot(void) {
  trace_app_setup();
  g_arm_diag = (bench_arm_diag_t){
    .attempt = 7u, .time_us = 123456u, .stage = BENCH_ARM_STAGE_ENABLE,
    .tim1_sr = 0x80u, .tim1_bdtr = 0x02001c50u, .tim1_ccer = 0x555u,
    .gpiob_idr = 0x40u, .gpioc_idr = USER_BUTTON_Pin, .gpioc_odr = 0u
  };
}

static void test_arm_diag_cached_command_and_fault_retention(void) {
  for (unsigned int faulted = 0u; faulted < 2u; ++faulted) {
    arm_app_seed_snapshot();
    g_gate.mode = faulted ? BENCH_FAULT : BENCH_OFF;
    g_gate.faults = faulted ? BENCH_FAULT_BREAK : 0u;
    bench_hw_fake_set_button_input(true); /* Cached read needs no deadman release. */
    const bench_arm_diag_t saved = g_arm_diag;
    command("ARM_DIAG");
    const char *output = trace_app_output();
    CHECK(strstr(output, "ARM_DIAG cached=1 attempt=7 us=123456 stage=2 guard=0") &&
          strstr(output, "sr=00000080 bdtr=02001C50 ccer=00000555") &&
          strstr(output, "OK ARM_DIAG_CACHED_OUTPUT_OFF"),
          "ARM_SYNC: OFF/FAULT command reports cached pre-cleanup evidence");
    CHECK_EQ_U32(g_gate.faults, faulted ? BENCH_FAULT_BREAK : 0u,
                 "ARM_SYNC: cached read cannot clear a latched fault");
    CHECK(!g_spi3_state.transfers && !g_spi1_state.transfers &&
          !bench_hw_fake_tim1_enable_all_outputs_count() && !bench_hw_fake_tim1_cc_enable_count(),
          "ARM_SYNC: cached command never performs SPI or enables output");
    CHECK(memcmp(&saved, &g_arm_diag, sizeof(saved)) == 0 && !g_bench_hw_fake_primask,
          "ARM_SYNC: cached command preserves snapshot and restores IRQ mask");
    bench_hw_fake_set_button_input(false);
    command("STOP");
    command("CLEAR");
    (void)trace_app_output();
    command("ARM_DIAG");
    CHECK(strstr(trace_app_output(), "attempt=7 us=123456") &&
          memcmp(&saved, &g_arm_diag, sizeof(saved)) == 0,
          "ARM_SYNC: STOP and explicit CLEAR retain the last arm snapshot");
  }
}

static void test_arm_diag_rejects_active_or_unsafe_states(void) {
  for (unsigned int failure = 0u; failure < 8u; ++failure) {
    arm_app_seed_snapshot();
    if (failure < 4u) { g_gate.mode = (bench_mode_t)(BENCH_PREPARED + failure); }
    if (failure == 4u) { g_preparing = true; }
    if (failure == 5u) { g_tracing = true; }
    if (failure == 6u) { TIM1->moe = true; TIM1->BDTR |= LL_TIM_BDTR_MOE; }
    if (failure == 7u) { LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin); }
    const bench_arm_diag_t saved = g_arm_diag;
    const uint32_t off_before = GPIOC->ODR;
    const bool moe_before = TIM1->moe;
    command("ARM_DIAG");
    CHECK(strstr(trace_app_output(), "ERR ARM_DIAG_REQUIRES_OFF_OR_FAULT_MOE0_DRVOFF1"),
          "ARM_SYNC: active/unsafe cached command rejected");
    CHECK(!g_spi3_state.transfers && !g_spi1_state.transfers &&
          !bench_hw_fake_tim1_enable_all_outputs_count() && !bench_hw_fake_tim1_cc_enable_count(),
          "ARM_SYNC: rejected command does not communicate or enable hardware");
    CHECK(memcmp(&saved, &g_arm_diag, sizeof(saved)) == 0 && GPIOC->ODR == off_before && TIM1->moe == moe_before,
          "ARM_SYNC: rejected cached read has no hardware or evidence side effects");
  }
}


static void test_encoder_capture_one_shot_and_readback(void) {
  bench_app_fake_reset_all();
  (void)trace_app_output();
  const bench_gate_t saved = g_gate;
  command("ENC_CAPTURE");
  CHECK(strstr(trace_app_output(), "OK ENC_CAPTURE_OUTPUT_OFF"), "capture starts without output");
  CHECK(g_enc_capture_running && !g_enc_capture_rolling, "off capture is one-shot");
  CHECK(memcmp((const void *)&g_gate, &saved, sizeof(saved)) == 0 && !g_zero_valid && !g_drv_ready,
        "capture does not grant readiness, clear faults, or calibrate");
  CHECK(!TIM1->moe && !TIM1->CCER && !bench_hw_fake_tim1_enable_all_outputs_count(),
        "capture never arms PWM");
  command("ENC_CAPTURE_GET 0");
  CHECK(strstr(trace_app_output(), "ERR ENC_CAPTURE_GET_REQUIRES_FROZEN_OUTPUT_OFF"),
        "cannot dump a changing capture");
  for (uint32_t i = 0u; i < ENC_CAPTURE_POINTS + 3u; ++i) {
    encoder_capture_record(0xfffffff0u + i * 250u, (uint16_t)i, 0x1ffu);
  }
  CHECK(!g_enc_capture_running && g_enc_capture_count == ENC_CAPTURE_POINTS &&
        g_enc_capture_total == ENC_CAPTURE_POINTS, "one-shot freezes exactly at capacity");
  CHECK(g_enc_capture[0].time_us == 0xfffffff0u && g_enc_capture[0].angle == 0u &&
        g_enc_capture[1].time_us == 234u, "raw timestamps preserve uint32 wrap");
  command("ENC_CAPTURE_GET 0");
  const char *text = trace_app_output();
  CHECK(strstr(text, "count=2048 total=2048") && strstr(text, "start=0 points=16"),
        "readback reports frozen count and bounded page");
  CHECK(strstr(text, "index=0 us=4294967280 angle=0 diag=01FF") &&
        strstr(text, "index=15 ") && !strstr(text, "index=16 "),
        "first page contains only the requested sixteen samples");
  CHECK(!g_uart_errors, "capture page fits UART ring");
  TIM1->CCER = LL_TIM_CHANNEL_CH1;
  command("ENC_CAPTURE_GET 0");
  CHECK(strstr(trace_app_output(), "ERR ENC_CAPTURE_GET_REQUIRES_FROZEN_OUTPUT_OFF"),
        "even one enabled PWM channel prevents a diagnostic dump");
  TIM1->CCER = 0u;
  command("ENC_CAPTURE_GET 2047");
  text = trace_app_output();
  CHECK(strstr(text, "start=2047 points=1") && strstr(text, "angle=2047"), "last page is bounded by count");
  const char *bad[] = {"ENC_CAPTURE_GET -1", "ENC_CAPTURE_GET 2048", "ENC_CAPTURE_GET 1x", "ENC_CAPTURE_GET "};
  for (unsigned int i = 0u; i < sizeof(bad) / sizeof(bad[0]); ++i) {
    char line[64]; (void)snprintf(line, sizeof(line), "%s", bad[i]); command(line);
    CHECK(strstr(trace_app_output(), "ERR ENC_CAPTURE_GET_INDEX"), "invalid read index rejected");
  }
}

static void test_encoder_capture_rolling_preserves_fault_tail(void) {
  bench_app_fake_reset_all();
  (void)trace_app_output();
  encoder_capture_start(true);
  for (uint32_t i = 0u; i < ENC_CAPTURE_POINTS + 20u; ++i) {
    encoder_capture_record(1000u + 250u * i, (uint16_t)i, 0x100u);
  }
  CHECK(g_enc_capture_running && g_enc_capture_count == ENC_CAPTURE_POINTS &&
        g_enc_capture_total == ENC_CAPTURE_POINTS + 20u, "rolling capture retains full count and total");
  latch(BENCH_FAULT_SPEED);
  encoder_capture_record(999999u, 999u, 0u);
  CHECK(!g_enc_capture_running && g_enc_capture_total == ENC_CAPTURE_POINTS + 20u,
        "first hardware fault freezes history before later observations");
  command("ENC_CAPTURE_GET 0");
  const char *text = trace_app_output();
  CHECK(strstr(text, "index=0 us=6000 angle=20 diag=0100"), "ring readback starts at oldest retained sample");
  command("ENC_CAPTURE_GET 2047");
  CHECK(strstr(trace_app_output(), "angle=2067"), "ring readback ends at newest retained sample");
  command("CLEAR"); (void)trace_app_output();
  CHECK(g_enc_capture_count == ENC_CAPTURE_POINTS && !g_enc_capture_running,
        "clear does not erase fault capture");
}

static void test_encoder_capture_guards_and_empty_read(void) {
  for (unsigned int bad = 0u; bad < 10u; ++bad) {
    bench_app_fake_reset_all(); (void)trace_app_output();
    if (bad < 4u) { g_gate.mode = (bench_mode_t)(BENCH_PREPARED + bad); }
    if (bad == 4u) { g_preparing = true; }
    if (bad == 5u) { g_tracing = true; }
    if (bad == 6u) { g_self_left = 1u; }
    if (bad == 7u) { TIM1->moe = true; }
    if (bad == 8u) { LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin); }
    if (bad == 9u) { g_hw_ready = false; }
    command("ENC_CAPTURE");
    CHECK(strstr(trace_app_output(), "ERR ENC_CAPTURE_REQUIRES_OUTPUT_OFF"), "capture rejects unsafe or busy state");
    CHECK(!g_enc_capture_running && !g_enc_capture_count && !g_spi1_state.transfers,
          "rejected capture has no sampler or bus side effects");
  }
  bench_app_fake_reset_all(); (void)trace_app_output();
  command("ENC_CAPTURE_GET 0");
  const char *empty = trace_app_output();
  CHECK(strstr(empty, "ENC_CAPTURE count=0 total=0") && strstr(empty, "start=0 points=0") &&
        strstr(empty, "OK ENC_CAPTURE_GET_OUTPUT_OFF") && !strstr(empty, "ENC_SAMPLE "),
        "empty capture reports zero metadata without fabricating samples");
  command("ENC_CAPTURE_GET 1");
  CHECK(strstr(trace_app_output(), "ERR ENC_CAPTURE_GET_INDEX"), "only index zero can report an empty capture");
  g_gate.mode = BENCH_FAULT; g_gate.faults = BENCH_FAULT_SPEED;
  command("ENC_CAPTURE"); (void)trace_app_output();
  CHECK(g_enc_capture_running && g_gate.faults == BENCH_FAULT_SPEED, "off capture leaves existing fault latched");
  command("STOP"); (void)trace_app_output();
  CHECK(!g_enc_capture_running, "STOP freezes even an unfinished off capture");
}

static void test_encoder_capture_is_wired_to_real_encoder_irq(void) {
  bench_app_fake_reset_all(); (void)trace_app_output();
  command("ENC_CAPTURE"); (void)trace_app_output();
  bench_hw_fake_spi1_push(as5048a_read_command(AS5048A_REG_ANGLE), 0u, false);
  bench_hw_fake_spi1_push(as5048a_read_command(AS5048A_REG_DIAG), as5048a_make_response(7000u), false);
  bench_hw_fake_spi1_push(0u, as5048a_make_response(0x100u), false);
  Bench_EncoderIRQ();
  CHECK(g_enc_valid && g_enc_capture_count == 1u && g_enc_capture[0].angle == 7000u &&
        g_enc_capture[0].time_us == g_enc_us && g_enc_capture[0].diagnostics == 0x100u,
        "real SPI IRQ captures exactly the published valid sample and time");
  bench_hw_fake_spi1_push(as5048a_read_command(AS5048A_REG_ANGLE), 0u, false);
  bench_hw_fake_spi1_push(as5048a_read_command(AS5048A_REG_DIAG), as5048a_make_response(7001u) ^ 0x8000u, false);
  bench_hw_fake_spi1_push(0u, as5048a_make_response(0x100u), false);
  Bench_EncoderIRQ();
  CHECK(!g_enc_valid && g_encoder_errors == 1u && g_enc_capture_count == 2u &&
        g_enc_capture[1].angle == 0xffffu && g_enc_seq == 1u,
        "invalid read is timestamped as invalid, never silently turned into a valid angle");
  CHECK(!g_spi1_state.mismatch_count && g_spi1_state.transfers == 6u && !TIM1->moe,
        "capture preserves real encoder transaction order and output state");
}

static void test_motion_commands_capture_and_completion_freezes(void) {
  const char *commands[] = {"ALIGN", "IQ 10 1", "HAPTIC 0 1"};
  for (unsigned int i = 0u; i < 3u; ++i) {
    bench_app_fake_reset_all(); bench_app_seed_haptic_ready(); (void)trace_app_output();
    if (i == 0u) { g_gate.mode = BENCH_PREPARED; g_gate.calibrated = false; }
    char line[32]; (void)snprintf(line, sizeof(line), "%s", commands[i]); command(line);
    CHECK(strstr(trace_app_output(), "OK "), "motion fixture genuinely arms before capture assertions");
    CHECK(g_enc_capture_running && g_enc_capture_rolling && !g_enc_capture_count,
          "each accepted motion command starts a fresh rolling capture");
    encoder_capture_record(250u, 50u, 0x100u);
    command("ENC_CAPTURE_GET 0");
    CHECK(strstr(trace_app_output(), "ERR ENC_CAPTURE_GET_REQUIRES_FROZEN_OUTPUT_OFF"),
          "active motion cannot dump capture over UART");
    bench_gate_stop(&g_gate);
    bench_app_set_adc_sample(1861u, 1861u, 1861u, 1200u, true);
    CHECK(!g_enc_capture_running && g_enc_capture_count == 1u,
          "ADC output-off path freezes completed/expired motion history");
  }
}


static void test_current_diagnostics_preserve_trip_raw_and_zero(void) {
  bench_app_fake_reset_all(); (void)trace_app_output();
  g_raw[0] = 2100u; g_raw[1] = 1810u; g_raw[2] = 1720u; g_raw[3] = 1112u;
  g_zero[0] = 1830.5f; g_zero[1] = 1831.25f; g_zero[2] = 1832.75f;
  g_adc_count = 42u; g_adc_us = 12345u; g_adc_synchronized = true;
  TIM1->moe = true; bench_hw_fake_set_nfault_input(true);
  latch(BENCH_FAULT_CURRENT);
  CHECK(g_trip_adc.sample == 42u && g_trip_adc.time_us == 12345u && g_trip_adc.synchronized == 1u &&
        g_trip_adc.raw[0] == 2100u && g_trip_adc.raw[3] == 1112u,
        "current stop preserves the causal raw ADC frame rather than disabled-CSA values");
  CHECK(g_trip_zero[0] == 1830.5f && g_trip_zero[1] == 1831.25f && g_trip_zero[2] == 1832.75f &&
        g_trip_outputs == 3u, "fault cache preserves calibration and pre-disable MOE/nFAULT");
  g_raw[0] = 4095u; g_raw[1] = 4095u; g_adc_count = 99u; g_zero[0] = 1900.0f;
  latch(BENCH_FAULT_UART);
  CHECK(g_trip_adc.sample == 42u && g_trip_adc.raw[0] == 2100u && g_trip_zero[0] == 1830.5f,
        "later faults and live ADC updates cannot overwrite the first trip frame");
  command("CURRENT_DIAG"); const char *text = trace_app_output();
  CHECK(strstr(text, "CURRENT_LIVE sample=99") && strstr(text, "raw=4095,4095,1720,1112") &&
        strstr(text, "CURRENT_TRIP sample=42 us=12345") && strstr(text, "raw=2100,1810,1720,1112") &&
        strstr(text, "zero_mc=1830500,1831250,1832750") && strstr(text, "outputs=3"),
        "diagnostics distinguish live invalid offsets from the retained calibrated trip");
  CHECK(strstr(text, "OK CURRENT_DIAG_OUTPUT_OFF") && !g_uart_errors && !TIM1->moe,
        "raw current diagnostics stay output-off and fit UART");
  command("CLEAR"); (void)trace_app_output();
  CHECK(g_trip_adc.sample == 42u, "explicit fault clear retains forensic current evidence");
}

static void test_current_diagnostics_reject_output_and_do_not_publish_health(void) {
  for (unsigned int i = 0u; i < 4u; ++i) {
    bench_app_fake_reset_all(); (void)trace_app_output();
    if (i == 0u) { g_gate.mode = BENCH_ACTIVE; }
    if (i == 1u) { TIM1->moe = true; }
    if (i == 2u) { g_preparing = true; }
    if (i == 3u) { g_tracing = true; }
    command("CURRENT_DIAG");
    CHECK(strstr(trace_app_output(), "ERR CURRENT_DIAG_REQUIRES_OUTPUT_OFF"), "current diagnostic refuses active output/busy state");
    CHECK(!g_zero_valid && !g_drv_ready && !g_gate.calibrated && !g_spi3_state.transfers,
          "current diagnostic never recalibrates or touches the driver");
  }
  bench_app_fake_reset_all(); (void)trace_app_output();
  command("CURRENT_DIAG");
  CHECK(strstr(trace_app_output(), "CURRENT_TRIP sample=0"), "new boot reports no invented fault history");
}


static void test_alignment_failure_retains_causal_snapshot(void) {
  bench_app_fake_reset_all(); bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_ALIGNING; g_gate.calibrated = false;
  g_align_stage = 1u; g_align_origin = 2.1f;
  g_foc.theta_unwrapped_rad = 2.106f; g_foc.velocity_rad_s = 0.3f;
  g_enc_angle = 5492u; g_adc_count = 42u; g_adc_us = 12345u;
  g_raw[0] = 2010u; g_adc_synchronized = true;
  TIM1->moe = true;
  encoder_capture_start(true); encoder_capture_record(12345u, 5492u, 0x01ffu);
  alignment_tick(g_gate.started_us + 4500000u);
  CHECK(g_gate.faults == BENCH_FAULT_ALIGN && !g_gate.calibrated && !TIM1->moe,
        "insufficient displacement still faults and hard-disables output");
  CHECK(g_trip_adc.sample == 42u && g_trip_adc.time_us == 12345u &&
        g_trip_adc.raw[0] == 2010u && g_trip_outputs == 3u,
        "terminal alignment failure retains its pre-disable ADC and output snapshot");
  CHECK(g_trip_encoder == 5492u && fabsf(g_trip_angle - 2.106f) < 1e-6f,
        "terminal alignment failure retains encoder position before idle movement");
  CHECK(!g_enc_capture_running && g_enc_capture_count == 1u && !g_drv_ready && !g_zero_valid,
        "terminal alignment failure immediately freezes capture and invalidates readiness");
  g_foc.theta_unwrapped_rad = 2.3f;
  command("CURRENT_DIAG"); const char *text = trace_app_output();
  CHECK(strstr(text, "fault=8 align_stage=1 align_move_mrad=5") ||
        strstr(text, "fault=8 align_stage=1 align_move_mrad=6"),
        "diagnostics retain fault and alignment displacement rather than later live position");
}

static void test_gate_faults_retain_first_snapshot(void) {
  const uint32_t faults[] = {BENCH_FAULT_HEALTH, BENCH_FAULT_LEASE, BENCH_FAULT_ALIGN};
  for (unsigned int i = 0u; i < 3u; ++i) {
    bench_app_fake_reset_all(); bench_app_seed_haptic_ready();
    const uint32_t now = bench_now_us();
    g_gate.mode = BENCH_ALIGNING; g_gate.calibrated = false;
    g_gate.duration_us = 6000000u; g_gate.started_us = now;
    g_gate.lease_us = now; g_last_observer_seq = g_enc_seq;
    g_foc.theta_unwrapped_rad = 1.25f; g_adc_count = 42u;
    TIM1->moe = true;
    encoder_capture_start(true); encoder_capture_record(now, 100u, 0x01ffu);
    if (i == 0u) { g_enc_valid = false; }
    if (i == 1u) { g_gate.lease_us = now - 100001u; }
    if (i == 2u) { g_gate.started_us = now - 6000000u; }
    bench_app_set_adc_sample(1861u, 1861u, 1861u, 1110u, true);
    CHECK(g_gate.faults == faults[i] && !TIM1->moe && !g_gate.calibrated,
          "health/lease/alignment-timeout faults keep their original safety behavior");
    CHECK(g_trip_adc.sample == 43u && g_trip_adc.raw[3] == 1110u && g_trip_outputs == 3u &&
          fabsf(g_trip_angle - 1.25f) < 1e-6f,
          "gate-origin fault retains first ADC frame and pre-disable output state");
    CHECK(!g_enc_capture_running && !g_drv_ready && !g_zero_valid,
          "gate-origin fault freezes capture and invalidates readiness");
    bench_app_set_adc_sample(4095u, 4095u, 4095u, 1110u, true);
    CHECK(g_trip_adc.sample == 43u && g_trip_adc.raw[0] == 1861u,
          "subsequent off-state ADC frame cannot replace the gate-fault evidence");
  }
}

static void test_adc_fault_snapshot_has_current_frame_timestamp_and_sync(void) {
  for (unsigned int i = 0u; i < 2u; ++i) {
    bench_app_fake_reset_all(); bench_app_seed_haptic_ready();
    const uint32_t now = bench_now_us();
    g_gate.mode = BENCH_ACTIVE; g_gate.duration_us = 1000000u;
    g_adc_count = 42u; g_adc_us = now - (i ? 100u : 50u);
    g_adc_synchronized = true; TIM1->moe = true;
    bench_app_set_adc_sample(1862u, 1863u, 1864u, 1110u, i != 0u);
    CHECK(g_gate.faults == (i ? BENCH_FAULT_DEADLINE : BENCH_FAULT_ADC),
          "ADC sync/gap fault reason stays unchanged");
    CHECK(g_trip_adc.sample == 43u && g_trip_adc.time_us == g_adc_us &&
          g_trip_adc.synchronized == i && g_trip_adc.raw[0] == 1862u,
          "ADC fault timestamp and sync bit describe the same frame as the raw samples");
  }
}

static void test_new_fault_after_clear_replaces_previous_episode(void) {
  bench_app_fake_reset_all(); g_adc_count = 10u; latch(BENCH_FAULT_TEST);
  command("CLEAR");
  CHECK(g_trip_adc.sample == 10u, "CLEAR preserves the previous fault evidence for inspection");
  g_adc_count = 20u; latch(BENCH_FAULT_UART);
  CHECK(g_trip_adc.sample == 20u, "a new fault episode after CLEAR gets its own first snapshot");
}

static void test_alignment_rejects_nonfinite_current_window(void) {
  bench_app_fake_reset_all(); bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_ALIGNING; g_align_stage = 0u;
  g_align_current_count = 1u; g_align_current_sum = NAN; g_align_voltage_sum = 0.2f;
  alignment_tick(g_gate.started_us + 1000000u);
  CHECK(g_gate.faults == BENCH_FAULT_ALIGN && !g_gate.calibrated,
        "nonfinite measured current cannot qualify an alignment voltage sweep");
}

/* Feed actual UART IRQ and bounded foreground parser, not command() directly. */
static void uart_app_byte(uint8_t value) {
  bench_hw_fake_queue_uart_rx(value);
  USART2->ISR |= LL_USART_ISR_RXNE;
  Bench_UartIRQ();
}

static void uart_app_input(const char *text) {
  while (*text) { uart_app_byte((uint8_t)*text++); }
}

static void uart_app_setup(void) {
  bench_app_fake_reset_all();
  (void)trace_app_output();
  g_self_reported = true;
  LL_USART_DisableIT_TXE(USART2);
}

static void test_uart_rx_overflow_cannot_reconstruct_valid_command(void) {
  uart_app_setup();
  /* A 255-byte ring fills before the X in the invalid SELFTESTX command. */
  for (unsigned int i = 0u; i < 247u; ++i) { uart_app_byte('\n'); }
  uart_app_input("SELFTESTX");
  CHECK_EQ_U32(g_uart_errors, 1u, "fixture actually overflowed real RX ring");
  for (unsigned int i = 0u; i < 8u; ++i) { Bench_Loop(); }
  uart_app_byte('\n');
  Bench_Loop();
  CHECK_EQ_U32(g_self_left, 0u, "dropped X cannot turn SELFTESTX into SELFTEST");
  (void)trace_app_output();
  uart_app_input("PING recovered\n");
  Bench_Loop();
  CHECK(strstr(trace_app_output(), "PONG recovered\r\n") != NULL,
        "RX parser recovers at a clean line boundary after overflow");
  CHECK(!TIM1->moe && bench_hw_fake_drv_off_state(), "RX overflow never arms output");
}

static void test_uart_hardware_errors_invalidate_partial_command(void) {
  const uint32_t errors[] = {LL_USART_ISR_ORE, LL_USART_ISR_FE, LL_USART_ISR_NE};
  for (unsigned int i = 0u; i < sizeof(errors) / sizeof(errors[0]); ++i) {
    uart_app_setup();
    uart_app_input("SELF");
    Bench_Loop();
    USART2->ISR |= errors[i];
    Bench_UartIRQ();
    /* Missing/corrupt byte between SELF and TEST must not be silently removed. */
    uart_app_input("TEST\n");
    Bench_Loop();
    CHECK_EQ_U32(g_uart_errors, 1u, "hardware UART error is counted once");
    CHECK_EQ_U32(USART2->ISR & errors[i], 0u, "hardware UART error flag cleared");
    CHECK_EQ_U32(g_self_left, 0u, "ORE/FE/NE invalidates command being assembled");
    (void)trace_app_output();
    /* Supply an explicit boundary even if the corrupt queued newline was purged. */
    uart_app_input("\nPING clean\n");
    Bench_Loop();
    CHECK(strstr(trace_app_output(), "PONG clean\r\n") != NULL,
          "hardware UART error recovery accepts a new complete command");
  }
}

static void test_uart_ping_response_is_atomic_when_tx_is_full(void) {
  const uint16_t spaces[] = {0u, 2u, 4u, 5u, 94u, 95u, 96u, 97u, 98u};
  char input[96], expected[98];
  memcpy(input, "PING ", 5u); memset(input + 5u, 'x', 90u); input[95] = '\0';
  memcpy(expected, "PONG ", 5u); memset(expected + 5u, 'x', 90u);
  memcpy(expected + 95u, "\r\n", 3u);
  for (unsigned int i = 0u; i < sizeof(spaces) / sizeof(spaces[0]); ++i) {
    uart_app_setup();
    const uint16_t start = (uint16_t)(2047u - spaces[i]);
    g_tx_read = 0u;
    g_tx_write = start;
    command(input);
    if (spaces[i] < 97u) {
      CHECK_EQ_U16(g_tx_write, start, "congested PONG cannot enqueue partial prefix/payload/newline");
      CHECK_EQ_U32(g_uart_errors, 1u, "one dropped PONG records one TX error");
    } else {
      CHECK_EQ_U16(g_tx_write, (uint16_t)((start + 97u) & 2047u), "full PONG enqueued once");
      CHECK_EQ_U32(g_uart_errors, 0u, "exact-fit PONG is not dropped");
      g_tx_read = start;
      CHECK(strcmp(trace_app_output(), expected) == 0, "exact-fit PONG is an intact frame");
    }
  }
}

static void test_uart_error_latches_active_output_before_recovery(void) {
  uart_app_setup();
  bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_ACTIVE;
  TIM1->moe = true;
  USART2->ISR |= LL_USART_ISR_FE;
  Bench_UartIRQ();
  CHECK(g_gate.faults & BENCH_FAULT_UART, "active UART error latches UART fault");
  CHECK(!TIM1->moe && bench_hw_fake_drv_off_state(), "active UART error immediately stops hardware");
  CHECK(!g_gate.calibrated && !g_zero_valid, "UART fault requires recalibration");
}

static void test_watchdog_feed_depends_on_adc_progress_not_uart_traffic(void) {
  uart_app_setup();
  Bench_Loop(); /* Establish the existing foreground ADC counter baseline. */
  uint32_t fed = g_iwdg_reload_count;
  for (unsigned int i = 0u; i < 128u; ++i) {
    uart_app_input("PING alive\n");
    Bench_Loop();
    (void)trace_app_output();
  }
  CHECK_EQ_U32(g_iwdg_reload_count, fed, "UART traffic alone cannot feed IWDG without ADC progress");
  ++g_adc_count;
  Bench_Loop();
  CHECK_EQ_U32(g_iwdg_reload_count, fed + 1u, "fresh ADC progress feeds IWDG once");
  Bench_Loop();
  CHECK_EQ_U32(g_iwdg_reload_count, fed + 1u, "reobserving same ADC sample cannot feed IWDG again");
  g_adc_count = UINT32_MAX;
  Bench_Loop();
  fed = g_iwdg_reload_count;
  g_adc_count = 0u;
  Bench_Loop();
  CHECK_EQ_U32(g_iwdg_reload_count, fed + 1u, "ADC counter uint32 rollover is fresh watchdog progress");
  CHECK(!TIM1->moe && bench_hw_fake_drv_off_state(), "watchdog scheduling test never arms output");
}

static void test_initialization_captures_reset_cause_and_starts_disarmed(void) {
  uart_app_setup();
  RCC->CSR = RCC_CSR_IWDGRSTF;
  Bench_Init();
  CHECK_EQ_U32(g_reset_flags, RCC_CSR_IWDGRSTF, "Init captures watchdog reset cause before clearing flags");
  CHECK_EQ_U32(RCC->CSR, 0u, "Init clears hardware reset flags after capture");
  CHECK(g_gate.mode == BENCH_OFF && !g_gate.calibrated && !g_gate.faults,
        "Init does not restore an armed or calibrated state");
  CHECK(!TIM1->moe && bench_hw_fake_drv_off_state(), "Init parks bridge outputs off");
  CHECK_EQ_U32(g_self_left, BENCH_PWM_HZ, "Init requires a fresh algorithm self-test");
  CHECK(strstr(trace_app_output(), "FW=20260908_HAPTIC26_OFFLINE_UNQUALIFIED") != NULL,
        "Init reports the tested firmware identity");
}

static void test_health_freshness_boundaries_across_microsecond_wrap(void) {
  uart_app_setup();
  bench_app_seed_haptic_ready();
  const uint32_t now = 64u;
  g_enc_us = UINT32_MAX - 100u;
  g_adc_us = UINT32_MAX - 10u;
  g_drv_checked_us = UINT32_MAX - 20000u;
  CHECK_EQ_U32(health(now), BENCH_HEALTH_ALL, "fresh ADC/encoder/driver samples survive uint32 time wrap");
  g_enc_us = now - 750u;
  CHECK(health(now) & BENCH_HEALTH_ENC, "750us encoder freshness boundary is inclusive across wrap");
  g_enc_us = now - 751u;
  CHECK(!(health(now) & BENCH_HEALTH_ENC), "751us encoder sample is stale across wrap");
  g_adc_us = now - 100u;
  CHECK(health(now) & BENCH_HEALTH_ADC, "100us ADC freshness boundary is inclusive across wrap");
  g_adc_us = now - 101u;
  CHECK(!(health(now) & BENCH_HEALTH_ADC), "101us ADC sample is stale across wrap");
  g_drv_checked_us = now - 49999u;
  CHECK(health(now) & BENCH_HEALTH_DRV, "49999us driver status remains fresh across wrap");
  g_drv_checked_us = now - 50000u;
  CHECK(!(health(now) & BENCH_HEALTH_DRV), "50000us driver status expires across wrap");
}

static void test_valid_fragmented_commands_at_every_rx_ring_offset(void) {
  for (uint16_t offset = 0u; offset < 256u; ++offset) {
    uart_app_setup();
    g_rx_read = offset;
    g_rx_write = offset;
    uart_app_input("PI");
    Bench_Loop();
    CHECK(strcmp(trace_app_output(), "") == 0, "incomplete command has no response");
    uart_app_input("NG wrap\r\nPING second\n");
    Bench_Loop();
    CHECK(strcmp(trace_app_output(), "PONG wrap\r\nPONG second\r\n") == 0,
          "fragmented CRLF and batched LF commands work at every RX ring offset");
    CHECK_EQ_U32(g_uart_errors, 0u, "valid ring wrap never reports corruption");
    CHECK(!TIM1->moe && bench_hw_fake_drv_off_state(), "valid UART commands never arm output");
  }
}


static void test_vm_window_is_consistent_and_bounded(void) {
  static const float values[] = {8.999f, 9.0f, 12.0f, 13.450639f,
      14.999f, 15.0f, 15.001f, NAN, INFINITY, -INFINITY};
  static const bool accepted[] = {false, true, true, true, true, true,
      false, false, false, false};
  for (unsigned int i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
    trace_app_setup();
    g_vm = values[i];
    CHECK(((health(bench_now_us()) & BENCH_HEALTH_VM) != 0u) == accepted[i],
          "VM health accepts only finite inclusive 9..15 V");
    g_tracing = true;
    g_trace_start_us = bench_now_us();
    g_trace_adc_bad_start = g_adc_bad;
    CHECK(Bench_DriverTraceSafe() == accepted[i],
          "continuous diagnostic guard uses the same finite 9..15 V window");

    trace_app_setup();
    g_vm = values[i];
    const uint16_t status_words[3] = {0x0808u, 0x0800u, 0x0800u};
    bench_app_queue_driver_configure(status_words, status_words);
    command("PREPARE");
    CHECK(g_preparing == accepted[i], "PREPARE uses the qualified 9..15 V window");
    CHECK(!TIM1->moe, "VM acceptance alone never enables PWM");
    if (!accepted[i]) {
      CHECK(g_spi3_state.transfers == 0u, "invalid VM cannot wake/configure driver");
    }

    trace_app_setup();
    g_vm = values[i];
    LL_GPIO_SetOutputPin(DRV_NSLEEP_GPIO_Port, DRV_NSLEEP_Pin);
    command("DRV_LIVE");
    CHECK((g_spi3_state.transfers > 0u) == accepted[i],
          "live driver read has the same VM entry window");
    CHECK(!TIM1->moe && LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin),
          "live read voltage check does not release hard stop");

    trace_app_setup();
    g_vm = values[i];
    command("DRV_TRACE MOTOR_DISCONNECTED");
    CHECK((strstr(trace_app_output(), "ERR DRV_TRACE_REQUIRES") == NULL) == accepted[i],
          "trace command VM entry agrees with continuous trace guard");
    CHECK(!TIM1->moe && g_gate.mode == BENCH_OFF,
          "trace entry cannot enable motor output at any tested voltage");
  }
  CHECK(PHASE_TRIP_A == 0.35f && VOLTAGE_CAP_V == 0.30f,
        "ALIGN experiment does not increase IQ/haptic phase-current or voltage limits");
  CHECK(ALIGN_CURRENT_A == 0.40f,
        "reviewed ALIGN experiment uses 0.40 A instead of the HAPTIC10 0.20 A target");
}


static void test_static_alignment_voltage_irq_and_guards(void) {
  const uint32_t ages[] = {0u, 100000u, 400000u, 800000u, 950000u};
  const float volts[] = {0.0f, 0.0475f, 0.19f, 0.38f, 0.38f};
  /* Nonzero measured q current must NOT be cancelled by a q-current PI. */
  for (unsigned int a = 0u; a < 5u; ++a) {
    for (unsigned int sign = 0u; sign < 2u; ++sign) {
      bench_app_fake_reset_all(); bench_app_seed_haptic_ready();
      const uint32_t now = bench_now_us();
      g_gate.mode = BENCH_ALIGNING; g_gate.calibrated = false;
      g_gate.started_us = now - ages[a]; g_gate.lease_us = now;
      g_gate.duration_us = 6000000u; g_last_observer_seq = g_enc_seq;
      g_foc.integrator_d_v = 0.2f; g_foc.integrator_q_v = -0.1f;
      g_foc.i_d_ref_a = 0.4f; g_foc.i_q_ref_a = 0.1f;
      g_foc.observer_initialized = true;
      bench_hw_fake_set_bench_time_step(0u);
      TIM1->direction = LL_TIM_COUNTERDIRECTION_DOWN; TIM1->CNT = 500u;
      LL_TIM_EnableAllOutputs(TIM1);
      bench_app_set_adc_sample(1861u, sign ? 1811u : 1911u,
                              sign ? 1911u : 1811u, 1110u, true);
      CHECK(g_gate.mode == BENCH_ALIGNING && g_gate.faults == 0u &&
            LL_TIM_IsEnabledAllOutputs(TIM1), "static voltage IRQ remains enabled with healthy inputs");
      CHECK(fabsf(g_foc.i_q_a) > 0.05f, "fixture has a real signed q-current error");
      CHECK(fabsf(g_foc.v_d_v - volts[a]) < 1e-5f && g_foc.v_q_v == 0.0f,
            "static alignment IRQ applies only the bounded d-voltage ramp");
      CHECK(g_foc.i_d_ref_a == 0.0f && g_foc.i_q_ref_a == 0.0f &&
            g_foc.integrator_d_v == 0.0f && g_foc.integrator_q_v == 0.0f,
            "static voltage alignment clears current refs and both PI integrators");
    }
  }
  for (unsigned int failure = 0u; failure < 8u; ++failure) {
    bench_app_fake_reset_all(); bench_app_seed_haptic_ready();
    const uint32_t now = bench_now_us();
    g_gate.mode = BENCH_ALIGNING; g_gate.calibrated = false;
    g_gate.started_us = now - 400000u; g_gate.lease_us = now;
    g_gate.duration_us = 6000000u; g_last_observer_seq = g_enc_seq;
    uint16_t raw[] = {1861u, 1861u, 1861u};
    if (failure < 6u) { raw[failure / 2u] = (failure & 1u) ? 1369u : 2353u; }
    else { g_foc.velocity_rad_s = failure == 6u ? 8.01f : -8.01f; }
    bench_hw_fake_set_bench_time_step(0u);
    TIM1->direction = LL_TIM_COUNTERDIRECTION_DOWN; TIM1->CNT = 500u;
    LL_TIM_EnableAllOutputs(TIM1);
    bench_app_set_adc_sample(raw[0], raw[1], raw[2], 1110u, true);
    CHECK(g_gate.mode == BENCH_FAULT &&
          g_gate.faults == (failure < 6u ? BENCH_FAULT_CURRENT : BENCH_FAULT_SPEED),
          "static voltage stage retains signed phase current and speed thresholds");
    CHECK(!LL_TIM_IsEnabledAllOutputs(TIM1) &&
          LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin) && !g_gate.calibrated,
          "static stage fault hard-disables output without granting calibration");
  }
}

static void test_alignment_only_current_and_voltage_headroom(void) {
  /* Exercise the actual IRQ with independent numeric test points, not values
   * derived from the production limits. Six phase/sign cases in both modes. */
  for (unsigned int align = 0u; align < 2u; ++align) {
    for (unsigned int phase = 0u; phase < 3u; ++phase) {
      for (unsigned int negative = 0u; negative < 2u; ++negative) {
        for (unsigned int above = 0u; above < 2u; ++above) {
          bench_app_fake_reset_all();
          bench_app_seed_haptic_ready();
          const uint32_t now = bench_now_us();
          g_gate.mode = align ? BENCH_ALIGNING : BENCH_ACTIVE;
          g_gate.calibrated = !align;
          g_gate.started_us = now - (align ? 1500000u : 0u);
          g_gate.lease_us = now;
          g_gate.duration_us = align ? 6000000u : 100000u;
          g_align_stage = 1u;
          g_align_voltage = 0.80f; /* Clamp even an out-of-range requested vector. */
          g_foc.integrator_d_v = 0.80f;
          g_foc.observer_initialized = true;
          g_last_observer_seq = g_enc_seq;
          bench_hw_fake_set_bench_time_step(1u);
          TIM1->direction = LL_TIM_COUNTERDIRECTION_DOWN;
          TIM1->CNT = 500u;
          LL_TIM_EnableAllOutputs(TIM1);
          const float amps = align ? (above ? 0.66f : 0.64f) : (above ? 0.36f : 0.34f);
          const int counts = (int)lroundf(amps * 0.6f * 4095.0f / 3.3f);
          uint16_t raw[3] = {1861u, 1861u, 1861u};
          raw[phase] = (uint16_t)(1861 + (negative ? -counts : counts));
          bench_app_set_adc_sample(raw[0], raw[1], raw[2], 1110u, true);
          if (above) {
            CHECK(g_gate.mode == BENCH_FAULT && g_gate.faults == BENCH_FAULT_CURRENT,
                  "each signed phase above its mode-specific threshold trips current protection");
            CHECK(!LL_TIM_IsEnabledAllOutputs(TIM1) &&
                  LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin) && !g_gate.calibrated,
                  "overcurrent always hard-disables output and invalidates calibration");
          } else {
            CHECK(g_gate.faults == 0u && LL_TIM_IsEnabledAllOutputs(TIM1),
                  "each signed phase just below its mode-specific threshold is accepted");
            const float magnitude = hypotf(g_foc.v_d_v, g_foc.v_q_v);
            CHECK(fabsf(magnitude - (align ? 0.60f : 0.30f)) < 1e-5f,
                  "IRQ clamps ALIGN to 0.60 V while IQ/haptics retain 0.30 V");
          }
        }
      }
    }
  }
}

static void test_alignment_average_voltage_headroom_boundaries(void) {
  const float values[] = {0.049f, 0.05f, 0.30f, 0.532f, 0.60f, 0.601f, NAN};
  for (unsigned int i = 0u; i < sizeof(values) / sizeof(values[0]); ++i) {
    bench_app_fake_reset_all();
    bench_app_seed_haptic_ready();
    g_gate.mode = BENCH_ALIGNING;
    g_gate.calibrated = false;
    g_align_current_sum = 0.40f;
    g_align_current_count = 1u;
    g_align_voltage_sum = values[i];
    const bool accepted = i >= 1u && i <= 4u;
    alignment_tick(g_gate.started_us + 1000000u);
    CHECK((g_gate.faults == 0u && g_align_stage == 1u) == accepted,
          "only finite average ALIGN voltage within 0.05 through 0.60 V enters sweep");
    CHECK(!g_gate.calibrated, "voltage qualification alone never grants mechanical calibration");
  }
}


static void test_raw_current_history_freezes_and_preserves_trigger_frame(void) {
  bench_app_fake_reset_all();
  current_capture_start();
  CHECK(sizeof(bench_current_point_t) == 16u, "SWD capture has an explicit 16-byte record layout");
  current_capture_record(10u, true);
  CHECK(g_current_capture_count == 0u, "OFF diagnostics cannot generate motor/current history");
  g_preparing = true; g_drv_ready = true; g_zero_count = 0u;
  g_raw[0] = 1871u; g_raw[1] = 1872u; g_raw[2] = 1873u; g_raw[3] = 1118u;
  current_capture_record(20u, true);
  CHECK(g_current_capture_count == 1u && g_current_capture[0].raw[1] == 1872u,
        "preparation records original ADC codes without scaling or filtering");
  g_zero_count = 512u;
  current_capture_record(30u, true);
  CHECK(g_current_capture_count == 1u, "completed zero window freezes before unrelated idle data");
  g_preparing = false;
  bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_ALIGNING; g_gate.calibrated = false;
  current_capture_start();
  LL_GPIO_SetOutputPin(DRV_CS_GPIO_Port, DRV_CS_Pin);
  LL_TIM_EnableAllOutputs(TIM1);
  for (uint32_t i = 0u; i < 517u; ++i) {
    g_raw[0] = (uint16_t)i;
    current_capture_record(1000u + i * 50u, true);
  }
  CHECK(g_current_capture_count == 512u && g_current_capture_total == 517u &&
        g_current_capture_write == 5u, "raw capture wraps chronologically without exceeding fixed RAM");
  CHECK(g_current_capture[5].raw[0] == 5u && g_current_capture[4].raw[0] == 516u &&
        g_current_capture[4].time_us == 26800u && g_current_capture[4].flags == 39u,
        "ring retains oldest/newest raw samples and pre-fault enabled/synchronized flags");
  latch(BENCH_FAULT_CURRENT);
  g_raw[0] = 4095u; current_capture_record(30000u, true);
  CHECK(g_current_capture_total == 517u && g_current_capture[4].raw[0] == 516u,
        "fault freezes history rather than overwriting it with DRVOFF current-sense values");
  CHECK(!LL_TIM_IsEnabledAllOutputs(TIM1) && g_gate.faults == BENCH_FAULT_CURRENT,
        "recording does not mask the original trip or interfere with hard stop");
  command("STOP"); current_capture_record(31000u, true);
  CHECK(g_current_capture_total == 517u, "STOP also preserves frozen pre-trip history");
  current_capture_start();
  CHECK(!g_current_capture_total && !g_current_capture_count && !g_current_capture_write,
        "a new explicit experiment starts a fresh history");
}

static void test_prepared_capture_freezes_first_raw_outlier_without_output(void) {
  bench_app_fake_reset_all();
  bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_PREPARED; g_gate.calibrated = false;
  current_capture_start();
  LL_GPIO_SetOutputPin(DRV_CS_GPIO_Port, DRV_CS_Pin);
  for (unsigned int i = 0u; i < 3u; ++i) { g_zero[i] = 1872.0f; g_raw[i] = 1872u; }
  current_capture_record(100u, true);
  CHECK(g_current_capture_count == 1u && !g_current_capture_idle_trip,
        "qualified prepared idle records while bridge output stays off");
  g_raw[1] = 1932u; current_capture_record(150u, true);
  CHECK(!g_current_capture_idle_trip, "60 count diagnostic threshold is exclusive");
  LL_GPIO_ResetOutputPin(DRV_CS_GPIO_Port, DRV_CS_Pin);
  g_raw[1] = 1933u; current_capture_record(200u, true);
  CHECK(g_current_capture_idle_trip && g_current_capture_total == 3u &&
        g_current_capture[2].raw[1] == 1933u && (g_current_capture[2].flags & 8u),
        "first output-off outlier and driver chip select state are retained raw");
  g_raw[1] = 1872u; current_capture_record(250u, true);
  CHECK(g_current_capture_total == 3u && g_gate.faults == 0u &&
        g_gate.mode == BENCH_PREPARED && !LL_TIM_IsEnabledAllOutputs(TIM1),
        "diagnostic freeze neither overwrites evidence nor changes gating/calibration");
  current_capture_start();
  CHECK(!g_current_capture_idle_trip && g_current_capture_count == 0u,
        "only an explicit new capture lifecycle clears the diagnostic freeze");
}


static uint32_t g_start_test_epoch;
static void start_test_timer_tick(void) {
  const uint32_t phase = (TIM2->CNT - g_start_test_epoch) % 50u;
  TIM1->CNT = phase <= 25u ? phase * 160u : (50u - phase) * 160u;
  TIM1->direction = phase < 25u ? 0u : LL_TIM_COUNTERDIRECTION_DOWN;
}

static void start_test_ready(unsigned int kind, uint32_t phase) {
  bench_app_fake_reset_all();
  bench_hw_fake_set_bench_time_step(1u);
  g_start_test_epoch = 100000u;
  TIM2->CNT = g_start_test_epoch + phase;
  bench_app_seed_haptic_ready();
  if (kind == 0u) { g_gate.mode = BENCH_PREPARED; g_gate.calibrated = false; }
  start_test_timer_tick();
  g_bench_hw_fake_time_hook = start_test_timer_tick;
  (void)trace_app_output();
}

static void test_output_commands_reserve_startup_phase_and_timeout_without_arming(void) {
  const char *commands[] = {"ALIGN", "IQ 10 20", "HAPTIC 0 250"};
  for (unsigned int kind = 0u; kind < 3u; ++kind) {
    for (uint32_t phase = 0u; phase < 50u; ++phase) {
      start_test_ready(kind, phase);
      const uint32_t started = TIM2->CNT;
      command((char *)commands[kind]);
      const char *reply = trace_app_output();
      const uint32_t gate_phase = (g_gate.started_us - g_start_test_epoch) % 50u;
      const uint32_t arm_phase = (g_arm_diag.time_us - g_start_test_epoch) % 50u;
      CHECK(strstr(reply, "OK ") && !strstr(reply, "ERR "), "startup scheduling admits healthy command");
      CHECK(g_gate.mode == (kind == 0u ? BENCH_ALIGNING : BENCH_ACTIVE) && !g_gate.faults,
            "startup phase scheduling preserves requested gate mode");
      CHECK(gate_phase <= 4u || gate_phase >= 48u, "gate transition waits near TIM1 bottom, never at ADC trigger");
      CHECK(arm_phase <= 12u, "output enable finishes before next ADC trigger, including setup/readback");
      CHECK(TIM2->CNT - started < 100u && !g_bench_hw_fake_primask, "startup wait is bounded and restores IRQs");
      CHECK(g_bench_hw_fake_irq_lock_max_us <= 10u, "startup phase wait is outside the existing short critical section");
      CHECK(bench_hw_fake_tim1_enable_all_outputs_count() == 1u && TIM1->moe,
            "qualified command still enables PWM once, without retries");
    }
    start_test_ready(kind, 20u);
    g_bench_hw_fake_time_hook = NULL; TIM1->CNT = 3500u;
    const uint32_t started = TIM2->CNT;
    const bench_mode_t before = g_gate.mode;
    command((char *)commands[kind]);
    CHECK(strstr(trace_app_output(), "ERR OUTPUT_START_TIMING_WINDOW"), "missing startup window is explicitly rejected");
    CHECK(g_gate.mode == before && !TIM1->moe && !TIM1->CCER &&
          !bench_hw_fake_tim1_enable_all_outputs_count(), "startup timeout never changes gate or arms PWM");
    CHECK(TIM2->CNT - started <= 160u && !g_bench_hw_fake_primask,
          "startup-window timeout remains bounded without masking IRQs");
  }
}


static void start_test_lose_nfault_while_waiting(void) {
  start_test_timer_tick();
  if ((uint32_t)(TIM2->CNT - g_start_test_epoch) > 24u) {
    bench_hw_fake_set_nfault_input(false);
  }
}

static void test_output_start_wrap_irq_state_and_health_recheck(void) {
  start_test_ready(0u, 20u);
  uint32_t mask = 99u;
  __disable_irq();
  const uint32_t before = TIM2->CNT;
  CHECK(!output_start_lock(&mask) && g_bench_hw_fake_primask && TIM2->CNT == before,
        "startup reservation never waits inside an incoming IRQ lock");
  __set_PRIMASK(0u);

  start_test_ready(0u, 20u);
  TIM2->CNT = UINT32_MAX - 3u; g_start_test_epoch = TIM2->CNT - 20u;
  start_test_timer_tick();
  CHECK(output_start_lock(&mask) && g_bench_hw_fake_primask && TIM1->CNT < 400u,
        "startup bottom window is acquired across microsecond timer wrap");
  irq_unlock(mask);
  CHECK(!g_bench_hw_fake_primask && TIM2->CNT < 50u,
        "wrapped successful reservation restores original enabled IRQ state");

  start_test_ready(0u, 20u);
  g_bench_hw_fake_time_hook = NULL; TIM1->CNT = 3500u; TIM2->CNT = UINT32_MAX - 10u;
  CHECK(!output_start_lock(&mask) && !g_bench_hw_fake_primask && TIM2->CNT < 151u,
        "missing startup window times out safely across timer wrap");

  start_test_ready(0u, 20u);
  g_bench_hw_fake_time_hook = start_test_lose_nfault_while_waiting;
  command("ALIGN");
  CHECK(strstr(trace_app_output(), "ERR ALIGN_NOT_READY") && !TIM1->moe &&
        !bench_hw_fake_tim1_enable_all_outputs_count(),
        "health is rechecked after phase wait: changed nFAULT prevents arming");
}

static void test_timing_selftest_covers_three_modes_without_output(void) {
  bench_app_fake_reset_all();
  g_self_left = 3072u;
  g_self_fail = 0u;
  TIM1->direction = LL_TIM_COUNTERDIRECTION_DOWN;
  TIM1->CNT = 1000u;
  while (g_self_left) { selftest_tick(); }
  CHECK(g_self_fail == 0u && g_self_foc.current_ticks == 3072u,
        "no-output profiling exercises valid current/align-current/voltage FOC calls");
  for (unsigned int i = 0u; i < 3u; ++i) {
    CHECK(g_self_timing[i].samples == 1024u && g_self_timing[i].window_min == 1000u,
          "each profile covers every one of the 1024 electrical angle bins");
  }
  CHECK(!TIM1->moe && !bench_hw_fake_tim1_enable_all_outputs_count() &&
        LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin),
        "synthetic profiling never arms outputs");
  (void)trace_app_output();
  command("TIMING_DIAG");
  const char *text = trace_app_output();
  CHECK(strstr(text, "SELF_TIMING kind=0 samples=1024") &&
        strstr(text, "SELF_TIMING kind=1 samples=1024") &&
        strstr(text, "SELF_TIMING kind=2 samples=1024") &&
        strstr(text, "OK TIMING_DIAG_OUTPUT_OFF"), "completed profiles have explicit readable metadata");
  command("SELFTEST");
  CHECK(g_self_left == BENCH_PWM_HZ && g_self_timing[0].samples == 0u &&
        g_self_timing[2].window_min == BENCH_TIMER_ARR, "new selftest resets profiling without output");
  command("TIMING_DIAG");
  CHECK(strstr(trace_app_output(), "ERR TIMING_DIAG_REQUIRES_OUTPUT_OFF"),
        "profile reporting rejects a still-running selftest");
  g_self_left = 0u; g_gate.mode = BENCH_ACTIVE;
  command("TIMING_DIAG");
  CHECK(strstr(trace_app_output(), "ERR TIMING_DIAG_REQUIRES_OUTPUT_OFF"),
        "profile reporting rejects active output");
}

static void test_deadline_profile_retains_first_cause_and_cycle_wrap(void) {
  bench_app_fake_reset_all();
  TIM1->CNT = 123u; TIM1->direction = LL_TIM_COUNTERDIRECTION_DOWN;
  DWT->CYCCNT = 10u;
  deadline_latch(2u, UINT32_MAX - 9u);
  CHECK(g_loop_timing.reason == 2u && g_loop_timing.stop_cycles == 20u &&
        g_loop_timing.stop_counter == 123u && g_loop_timing.stop_down == 1u,
        "PWM-window evidence captures pre-stop elapsed cycles across DWT wrap");
  CHECK(g_trip_fault == BENCH_FAULT_DEADLINE && g_gate.mode == BENCH_FAULT &&
        !TIM1->moe && LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin),
        "profiling does not suppress the real deadline hard stop");
  deadline_latch(3u, 0u);
  CHECK(g_loop_timing.reason == 2u && g_loop_timing.stop_cycles == 20u && g_deadlines == 2u,
        "post-stop budget violation cannot overwrite the first PWM deadline reason");
  const bench_loop_timing_t before = g_loop_timing;
  bench_app_set_adc_sample(1861u,1861u,1861u,1110u,true);
  CHECK(memcmp((const void *)&g_loop_timing,&before,sizeof(before)) == 0,
        "subsequent output-off ADC IRQ cannot overwrite first-deadline timing");
}

static void test_actual_adc_pwm_deadline_is_profiled_without_relaxing_guard(void) {
  bench_app_fake_reset_all(); bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_ACTIVE; g_gate.iq_ma = 10; g_gate.duration_us = 20000u;
  TIM1->CNT = 300u; TIM1->direction = LL_TIM_COUNTERDIRECTION_DOWN;
  LL_TIM_EnableAllOutputs(TIM1);
  bench_app_set_adc_sample(1861u,1861u,1861u,1110u,true);
  CHECK(g_trip_fault == BENCH_FAULT_DEADLINE && g_loop_timing.reason == 2u &&
        g_loop_timing.stage == 5u && g_loop_timing.observer_updated == 1u,
        "real ADC path records stages and refuses the unchanged below-320 PWM window");
  CHECK(g_loop_timing.sample == g_trip_adc.sample && !TIM1->moe,
        "timing evidence identifies the same fault frame and keeps outputs off");
}

static float reference_wrap_fmod(float value) {
  value = fmodf(value, TWO_PI);
  if (value > 0.5f * TWO_PI) { value -= TWO_PI; }
  if (value < -0.5f * TWO_PI) { value += TWO_PI; }
  return value;
}

static void test_bounded_angle_wrap_is_numerically_equivalent(void) {
  float maximum_wrap_error = 0.0f, maximum_duty_error = 0.0f;
  for (int i = -131072; i <= 131072; ++i) {
    const float angle = (float)i * (TWO_PI / 32768.0f);
    const float actual = wrap_pi(angle), expected = reference_wrap_fmod(angle);
    const float error = fabsf(reference_wrap_fmod(actual - expected));
    if (error > maximum_wrap_error) { maximum_wrap_error = error; }
    CHECK(isfinite(actual) && actual >= -0.5f*TWO_PI && actual <= 0.5f*TWO_PI && error < 4.0e-6f,
          "bounded angle reduction matches general fmod modulo one turn");
  }
  for (int k = -8; k <= 8; ++k) {
    const float center = (float)k * 0.5f * TWO_PI;
    const float values[] = {center, nextafterf(center, -INFINITY), nextafterf(center, INFINITY)};
    for (unsigned int j = 0u; j < 3u; ++j) {
      const float actual = wrap_pi(values[j]);
      CHECK(isfinite(actual) && actual >= -0.5f*TWO_PI && actual <= 0.5f*TWO_PI &&
            fabsf(reference_wrap_fmod(actual-reference_wrap_fmod(values[j]))) < 4.0e-6f,
            "one-ULP half/full-turn and fast-path boundaries remain canonical and equivalent");
    }
  }
  const float large[] = {-3.402823466e38f,-1.0e20f,1.0e20f,3.402823466e38f};
  for (unsigned int i = 0u; i < 4u; ++i) {
    CHECK(wrap_pi(large[i]) == reference_wrap_fmod(large[i]),
          "huge finite angles still use terminating general reduction without a long loop");
  }
  for (unsigned int code = 0u; code < 16384u; ++code) {
    const float angle = (float)code * (TWO_PI/16384.0f);
    gl30_foc_state_t actual, expected;
    gl30_foc_init(&actual); gl30_foc_init(&expected);
    gl30_foc_observer_tick_4k(&actual, angle, true);
    expected.observer_initialized = true;
    expected.theta_elec_rad = reference_wrap_fmod(GL30_PHASE_DIRECTION *
        (float)GL30_MOTOR_POLE_PAIRS * (reference_wrap_fmod(angle) - GL30_ELECTRICAL_ZERO_RAD));
    gl30_foc_output_t a = gl30_foc_voltage_tick(&actual,-0.3766612f,0.3222578f,0.0722599f,
        12.0f,1.0f/20000.0f,0.6f,0.4f,0.0f);
    gl30_foc_output_t b = gl30_foc_voltage_tick(&expected,-0.3766612f,0.3222578f,0.0722599f,
        12.0f,1.0f/20000.0f,0.6f,0.4f,0.0f);
    const float error = fmaxf(fabsf(a.duty_a-b.duty_a),fmaxf(fabsf(a.duty_b-b.duty_b),fabsf(a.duty_c-b.duty_c)));
    if (error > maximum_duty_error) { maximum_duty_error = error; }
    CHECK(a.valid && b.valid && error < 1.0e-6f && fabsf(a.i_d_a-b.i_d_a)<4.0e-6f &&
          fabsf(a.i_q_a-b.i_q_a)<4.0e-6f, "every AS5048A code retains equivalent FOC duties and dq current");
  }
  printf("ANGLE_EQUIVALENCE wrap_max_rad=%.9g duty_max=%.9g encoder_codes=16384 bounded_samples=262145\n",
         (double)maximum_wrap_error,(double)maximum_duty_error);
}


static void test_calibrated_angle_and_voltage_projection_equivalence(void) {
  float wrap_max = 0.0f, current_max = 0.0f, duty_max = 0.0f;
  const float zeros[] = {-3.14159265f, -3.0f, -0.687f, 0.22050974f, 3.0f, 3.14159265f};
  for (unsigned int z = 0u; z < 6u; ++z) {
    for (unsigned int code = 0u; code < 16384u; ++code) {
      const float mechanical = reference_wrap_fmod((float)code * (TWO_PI/16384.0f));
      for (int direction = -1; direction <= 1; direction += 2) {
        const float raw = (float)direction * 7.0f * (mechanical - zeros[z]);
        const float reduced = wrap_angle(raw);
        const float error = fabsf(remainderf(reduced - remainderf(raw,TWO_PI),TWO_PI));
        if (error > wrap_max) { wrap_max = error; }
        CHECK(isfinite(reduced) && fabsf(reduced) <= 0.5f*TWO_PI && error < 6.0e-6f,
              "calibrated seven-pole-pair envelope preserves modulo angle");
      }
    }
  }
  for (int i = -32768; i <= 32768; ++i) {
    const float angle = (float)i * (TWO_PI/65536.0f);
    gl30_foc_state_t state; gl30_foc_init(&state);
    state.observer_initialized = true; state.theta_elec_rad = angle;
    const gl30_foc_output_t out = gl30_foc_voltage_tick(&state,
        0.2f,-0.13f,-0.07f,12.0f,0.00005f,0.3f,0.2f,0.1f);
    const float a = 0.995832f*0.2f - 0.028199f*(-0.13f) - 0.014988f*(-0.07f);
    const float b = 0.037737f*0.2f + 1.007723f*(-0.13f) - 0.033757f*(-0.07f);
    const float c = 0.009226f*0.2f + 0.029805f*(-0.13f) + 1.003268f*(-0.07f);
    const float alpha=(2.0f*a-b-c)/3.0f, beta=(b-c)/1.73205080756887729353f;
    const float sn=sinf(angle), cs=cosf(angle);
    const float current_error = fmaxf(fabsf(out.i_d_a-(cs*alpha+sn*beta)),
                                    fabsf(out.i_q_a-(-sn*alpha+cs*beta)));
    const float va=cs*0.2f-sn*0.1f, vb=-0.5f*va+GL30_SQRT3_BY_2*(sn*0.2f+cs*0.1f);
    const float vc=-0.5f*va-GL30_SQRT3_BY_2*(sn*0.2f+cs*0.1f);
    const float cm=-0.5f*(fmaxf(va,fmaxf(vb,vc))+fminf(va,fminf(vb,vc)));
    const float duty_error=fmaxf(fabsf(out.duty_a-(0.5f+(va+cm)/12.0f)),
        fmaxf(fabsf(out.duty_b-(0.5f+(vb+cm)/12.0f)),fabsf(out.duty_c-(0.5f+(vc+cm)/12.0f))));
    if(current_error>current_max){current_max=current_error;}
    if(duty_error>duty_max){duty_max=duty_error;}
    CHECK(out.valid && current_error < 1.0e-7f && duty_error < 1.0e-7f,
          "quarter-turn trig preserves real CSA/Park/inverse-Park/SVPWM values");
  }
  const float boundaries[] = {-3.14159265f,-2.35619449f,-1.57079633f,-0.78539816f,0.0f,
                              0.78539816f,1.57079633f,2.35619449f,3.14159265f};
  for(unsigned int i=0u;i<9u;++i){
    const float values[]={nextafterf(boundaries[i],-INFINITY),boundaries[i],nextafterf(boundaries[i],INFINITY)};
    for(unsigned int j=0u;j<3u;++j){
      gl30_foc_state_t state;gl30_foc_init(&state);state.observer_initialized=true;state.theta_elec_rad=values[j];
      gl30_foc_output_t out=gl30_foc_voltage_tick(&state,0.0f,0.0f,0.0f,12.0f,0.00005f,0.3f,0.2f,0.0f);
      CHECK(out.valid && fabsf(out.v_d_v-0.2f)<1e-7f,"one-ULP trig boundaries remain finite and valid");
    }
  }
  const float huge[]={-3.402823466e38f,-1e20f,1e20f,3.402823466e38f};
  for(unsigned int i=0u;i<4u;++i){
    CHECK(wrap_angle(huge[i])==remainderf(huge[i],TWO_PI),"huge calibrated input terminates with original general reducer");
  }
  printf("CALIBRATED_EQUIVALENCE wrap_max_rad=%.9g current_max_a=%.9g duty_max=%.9g calibrated_cases=196608 voltage_cases=65537\n",
      (double)wrap_max,(double)current_max,(double)duty_max);
}

static void test_selftest_calibrated_first_iq_frames(void) {
  const unsigned int bins[]={0u,1u,257u,512u,767u,1023u};
  for(unsigned int block=1u;block<=4u;++block){
    for(unsigned int n=0u;n<6u;++n){
      bench_app_fake_reset_all();
      const unsigned int left=3u*(bins[n]+1024u*block);
      const float angle=(float)bins[n]*(TWO_PI/1024.0f);
      const float direction=(block&1u)?-1.0f:1.0f;
      const float zero=(block&2u)?3.0f:-3.0f;
      const float electrical=remainderf(direction*7.0f*(reference_wrap_fmod(angle)-zero),TWO_PI);
      gl30_foc_state_t expected;gl30_foc_init(&expected);expected.observer_initialized=true;
      expected.theta_elec_rad=electrical;expected.i_q_ref_a=(left&1u)?0.1f:-0.1f;
      (void)gl30_foc_current_tick(&expected,0.01f,-0.006f,-0.004f,12.0f,0.00005f,0.3f);
      g_self_left=left;g_self_foc.integrator_d_v=7.0f;g_self_foc.integrator_q_v=-8.0f;
      TIM1->direction=LL_TIM_COUNTERDIRECTION_DOWN;TIM1->CNT=1000u;
      selftest_tick();
      CHECK(fabsf(remainderf(g_self_foc.theta_elec_rad-electrical,TWO_PI))<6e-6f,
            "IQ selftest exercises both calibration directions and extreme zero positions");
      CHECK(fabsf(g_self_foc.integrator_d_v-expected.integrator_d_v)<1e-7f &&
            fabsf(g_self_foc.integrator_q_v-expected.integrator_q_v)<1e-7f,
            "IQ selftest measures a fresh current-controller frame, not inherited synthetic integrators");
      CHECK(!TIM1->moe && !bench_hw_fake_tim1_enable_all_outputs_count() && g_self_fail==0u,
            "calibrated IQ selftest remains strictly no-output");
    }
  }
}


static void encoder_irq_sample(uint16_t angle, bool corrupt) {
  bench_hw_fake_spi1_push(as5048a_read_command(AS5048A_REG_ANGLE), 0u, false);
  bench_hw_fake_spi1_push(as5048a_read_command(AS5048A_REG_DIAG),
      as5048a_make_response(angle) ^ (corrupt ? 0x8000u : 0u), false);
  bench_hw_fake_spi1_push(0u, as5048a_make_response(0x100u), false);
  Bench_EncoderIRQ();
}

static void test_encoder_observer_publishes_complete_samples(void) {
  /* Cross both signed-angle and encoder-counter boundaries in both directions. */
  const uint16_t angles[] = {8190u,8191u,8192u,8193u,8192u,8191u,16382u,16383u,0u,1u,0u,16383u};
  bench_app_fake_reset_all();
  gl30_foc_init(&g_encoder_observer[0]); gl30_foc_init(&g_encoder_observer[1]);
  gl30_foc_state_t expected; gl30_foc_init(&expected);
  CHECK(!g_encoder_observer_index && !g_enc_seq, "observer begins unpublished");
  for (unsigned int i = 0u; i < sizeof angles / sizeof angles[0]; ++i) {
    const gl30_foc_state_t before = g_foc;
    const gl30_foc_state_t previous = g_encoder_observer[g_encoder_observer_index];
    const uint8_t old_index = g_encoder_observer_index;
    TIM2->CNT += 250u;
    encoder_irq_sample(angles[i], false);
    gl30_foc_observer_tick_4k(&expected, (float)angles[i] * (TWO_PI / 16384.0f), true);
    CHECK(g_encoder_observer_index == (old_index ^ 1u) && g_enc_seq == i + 1u &&
          g_enc_angle == angles[i] && g_enc_valid, "index, sequence and angle publish together");
    CHECK(g_enc_us == g_test_observer_start_us, "sample timestamp precedes observer computation");
    check_observation(&g_encoder_observer[g_encoder_observer_index], &expected);
    CHECK(!memcmp(&g_encoder_observer[old_index], &previous, sizeof previous), "previous buffer remains immutable");
    CHECK(!memcmp(&g_foc, &before, sizeof before), "encoder IRQ does not own live PI/current/haptic state");
    adc_consumes_published_observation();
    const gl30_foc_state_t consumed = g_foc;
    adc_consumes_published_observation();
    check_observation(&g_foc, &consumed);
  }
  const gl30_foc_state_t published = g_encoder_observer[g_encoder_observer_index];
  const uint8_t old_index = g_encoder_observer_index;
  const uint32_t seq = g_enc_seq, time = g_enc_us;
  const uint16_t old_angle = g_enc_angle;
  encoder_irq_sample(21u, true);
  CHECK(!g_enc_valid && g_enc_seq == seq && g_enc_us == time &&
        g_enc_angle == old_angle && g_encoder_observer_index == old_index,
        "invalid SPI frame cannot publish an angle, timestamp, sequence or observer");
  CHECK(!memcmp(&published, &g_encoder_observer[old_index], sizeof published), "invalid SPI preserves last complete frame");
  CHECK(!g_spi1_state.mismatch_count, "observer uses unchanged real SPI transaction protocol");
  /* Exercise sequence wrap, which must not be confused with no new sample. */
  g_enc_seq = UINT32_MAX; g_last_observer_seq = UINT32_MAX;
  encoder_irq_sample(16382u, false);
  CHECK(g_enc_seq == 0u && g_enc_valid, "sequence wrap publishes a valid frame");
  adc_consumes_published_observation();
}

static void test_encoder_observer_preemption_and_stale_time(void) {
  bench_app_fake_reset_all();
  encoder_irq_sample(20u, false);
  /* ADC runs with the candidate partially written, just before locking, and
   * immediately after publication. The first two must read the prior frame. */
  g_test_observer_preempt = g_test_publish_preempt = g_test_published_preempt = true;
  encoder_irq_sample(21u, false);
  CHECK(g_test_preemptions == 3u, "all three preemption schedules executed");
  CHECK(!g_test_observer_preempt && !g_test_publish_preempt && !g_test_published_preempt,
        "all requested scheduling seams consumed");
  CHECK(g_last_observer_seq == 2u && g_enc_seq == 2u, "post-publication ADC consumes the new frame");
  CHECK(g_bench_hw_fake_irq_lock_max_us == 0u, "observer computation stays outside masked section");

  bench_app_fake_reset_all();
  TIM2->CNT = UINT32_MAX - 400u;
  g_test_observer_delay_us = 800u;
  encoder_irq_sample(0u, false);
  CHECK(g_enc_us == g_test_observer_start_us && (uint32_t)(TIM2->CNT - g_enc_us) == 800u,
        "delayed observer cannot make old encoder data look fresh, including timer wrap");
  CHECK(!(health(TIM2->CNT) & BENCH_HEALTH_ENC), "750us encoder freshness gate rejects delayed publication");
}

static void test_encoder_observer_preserves_control_ownership_and_calibration(void) {
  for (unsigned int d = 0u; d < 2u; ++d) {
    bench_app_fake_reset_all();
    g_foc.integrator_d_v = 0.041f; g_foc.integrator_q_v = -0.032f;
    g_foc.i_d_ref_a = 0.015f; g_foc.i_q_ref_a = -0.025f;
    g_foc.torque_command_nm = 0.007f;
    g_foc.active_command.commandNonce = 73u;
    const gl30_foc_state_t before = g_foc;
    encoder_irq_sample(8191u, false);
    CHECK(!memcmp(&g_foc, &before, sizeof before), "real encoder publication preserves all live control state");
    g_gate.calibrated = true; g_direction = d ? -1.0f : 1.0f; g_zero_angle = -2.9f;
    const gl30_foc_state_t published = g_encoder_observer[g_encoder_observer_index];
    bench_hw_fake_set_adc_samples(1861u,1861u,1861u,2700u); bench_hw_fake_set_adc_jeos(true);
    Bench_AdcIRQ();
    CHECK(g_foc.theta_elec_rad == wrap_angle(g_direction * 7.0f * (published.theta_mech_rad - g_zero_angle)),
          "calibrated direction and zero still override published electrical angle");
    CHECK(g_foc.theta_unwrapped_rad == published.theta_unwrapped_rad &&
          g_foc.velocity_rad_s == published.velocity_rad_s, "calibration does not alter mechanical observer");
    CHECK(g_foc.active_command.commandNonce == 73u, "ADC observer copy never overwrites active haptic command");
    CHECK(!memcmp(&published, &g_encoder_observer[g_encoder_observer_index], sizeof published),
          "ADC calibration and force-zero never write encoder buffers");
  }
}

static void test_encoder_observer_adc_does_not_reset_live_pi(void) {
  bench_app_fake_reset_all(); bench_app_seed_haptic_ready();
  g_gate.mode = BENCH_ACTIVE; g_gate.iq_ma = 10; g_gate.duration_us = 20000u;
  g_foc.integrator_d_v = 0.041f; g_foc.integrator_q_v = -0.032f;
  g_foc.active_command.commandNonce = 73u; g_foc.current_ticks = 99u;
  encoder_irq_sample(0u, false);
  gl30_foc_state_t expected = g_foc;
  expected.theta_elec_rad = wrap_angle(g_direction * 7.0f * (0.0f - g_zero_angle));
  expected.observer_initialized = true;
  expected.i_d_ref_a = 0.0f; expected.i_q_ref_a = 0.010f;
  const gl30_foc_output_t out = gl30_foc_current_tick(&expected, 0.0f, 0.0f, 0.0f,
      1110.0f * BENCH_ADC_SCALE * BENCH_VM_RATIO, 1.0f / (float)BENCH_PWM_HZ, VOLTAGE_CAP_V);
  CHECK(out.valid, "independent current-loop reference is valid");
  TIM1->CNT = 2000u; TIM1->direction = LL_TIM_COUNTERDIRECTION_DOWN;
  LL_TIM_EnableAllOutputs(TIM1);
  bench_app_set_adc_sample(1861u,1861u,1861u,1110u,true);
  CHECK(g_gate.faults == 0u && TIM1->moe, "active ADC fixture actually remains armed");
  CHECK(g_foc.integrator_d_v == expected.integrator_d_v && g_foc.integrator_q_v == expected.integrator_q_v,
        "published observer cannot reset or replace live PI state");
  CHECK(g_foc.current_ticks == expected.current_ticks && g_foc.active_command.commandNonce == 73u,
        "published observer cannot reset current progress or haptic command");
}

int main(void) {
  test_encoder_observer_adc_does_not_reset_live_pi();
  test_encoder_observer_publishes_complete_samples();
  test_encoder_observer_preemption_and_stale_time();
  test_encoder_observer_preserves_control_ownership_and_calibration();
  test_calibrated_angle_and_voltage_projection_equivalence();
  test_selftest_calibrated_first_iq_frames();
  test_bounded_angle_wrap_is_numerically_equivalent();
  test_timing_selftest_covers_three_modes_without_output();
  test_deadline_profile_retains_first_cause_and_cycle_wrap();
  test_actual_adc_pwm_deadline_is_profiled_without_relaxing_guard();
  test_prepared_capture_freezes_first_raw_outlier_without_output();
  test_output_start_wrap_irq_state_and_health_recheck();
  test_output_commands_reserve_startup_phase_and_timeout_without_arming();
  test_raw_current_history_freezes_and_preserves_trigger_frame();
  test_alignment_average_voltage_headroom_boundaries();
  test_static_alignment_voltage_irq_and_guards();
  test_alignment_only_current_and_voltage_headroom();
  test_watchdog_feed_depends_on_adc_progress_not_uart_traffic();
  test_initialization_captures_reset_cause_and_starts_disarmed();
  test_health_freshness_boundaries_across_microsecond_wrap();
  test_vm_window_is_consistent_and_bounded();
  test_valid_fragmented_commands_at_every_rx_ring_offset();
  test_uart_rx_overflow_cannot_reconstruct_valid_command();
  test_uart_hardware_errors_invalidate_partial_command();
  test_uart_ping_response_is_atomic_when_tx_is_full();
  test_uart_error_latches_active_output_before_recovery();
  test_alignment_failure_retains_causal_snapshot();
  test_gate_faults_retain_first_snapshot();
  test_adc_fault_snapshot_has_current_frame_timestamp_and_sync();
  test_new_fault_after_clear_replaces_previous_episode();
  test_alignment_rejects_nonfinite_current_window();
  test_current_diagnostics_preserve_trip_raw_and_zero();
  test_current_diagnostics_reject_output_and_do_not_publish_health();
  test_encoder_capture_one_shot_and_readback();
  test_encoder_capture_rolling_preserves_fault_tail();
  test_encoder_capture_guards_and_empty_read();
  test_encoder_capture_is_wired_to_real_encoder_irq();
  test_motion_commands_capture_and_completion_freezes();
  test_arm_diag_cached_command_and_fault_retention();
  test_arm_diag_rejects_active_or_unsafe_states();
  test_trace_command_never_publishes_ready_and_preserves_cache();
  test_trace_command_guards_and_bad_indexes();
  test_trace_adc_irq_keeps_inputs_low_and_fault_overrides_idle();
  test_1_unsync_while_zeroing_aborts_and_turns_outputs_off();
  test_2_unsync_after_512samples_still_rejected();
  test_3_clean_prepare_with_existing_adc_bad_baseline();
  test_4_failed_stats_or_health_does_not_change_offsets();
  test_5_zero_timeout_keeps_outputs_off();
  test_6_prepare_resets_stats_and_baseline_before_drv_ready();
  test_alignment_tick_success_forward_and_negative();
  test_alignment_tick_voltage_mode_uses_fixed_voltage_and_trip_faults();
  test_alignment_tick_fails_with_bad_current_windows();
  test_alignment_tick_ramps_voltage_and_completes_with_forward_travel();
  test_alignment_tick_rejects_invalid_average_voltage_sample();
  test_latch_preserves_first_motion_snapshot();
  test_haptic_command_requires_ready_health_and_range();
  test_drv_poll_reports_cached_status_and_respects_guards();
  test_iq_and_haptic_stale_reference_behaviors();

  if (g_tests_failed == 0) {
    printf("PASS: %d assertions\n", g_tests_run);
    return 0;
  }
  printf("FAIL: %d / %d assertions\n", g_tests_failed, g_tests_run);
  return 1;
}
