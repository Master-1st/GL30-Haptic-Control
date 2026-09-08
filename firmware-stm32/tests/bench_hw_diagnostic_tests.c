#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t g_enc_us;

#include "bench/NUCLEO_G474RE_FOC/Core/Inc/bench_hw.h"
#include "bench_hw_fake/main.h"

/* Pull production driver logic through test-only source shims. */
#include "drivers/drv8316_spi.c"
#include "drivers/as5048a.c"
#define g_tim1_pwm_pins_parked g_bench_hw_fake_tim1_pwm_pins_parked
#include "bench/NUCLEO_G474RE_FOC/Core/Src/bench_hw.c"

static int g_tests_run = 0;
static int g_tests_failed = 0;
static size_t g_trace_abort_after = SIZE_MAX;
static bool g_trace_guard_enabled = true;

bool Bench_DriverTraceSafe(void) {
  return g_trace_guard_enabled && g_spi3_state.transfers < g_trace_abort_after;
}

#define CHECK(cond, msg) \
  do { \
    ++g_tests_run; \
    if (!(cond)) { \
      ++g_tests_failed; \
      fprintf(stderr, "[FAIL] %s\n", (msg)); \
    } \
  } while (0)

#define CHECK_EQ_U8(a, b, msg) CHECK(((uint8_t)(a)) == ((uint8_t)(b)), (msg))
#define CHECK_EQ_U16(a, b, msg) CHECK(((uint16_t)(a)) == ((uint16_t)(b)), (msg))
#define CHECK_EQ_U32(a, b, msg) CHECK(((uint32_t)(a)) == ((uint32_t)(b)), (msg))

static size_t tx_count_in_transfers(uint16_t frame);

enum {
  STEP_UNLOCK_WRITE = 0u,
  STEP_CFG4_WRITE = 1u,
  STEP_CFG4_READ = 2u,
  STEP_CFG5_WRITE = 3u,
  STEP_CFG5_READ = 4u,
  STEP_CFG6_WRITE = 5u,
  STEP_CFG6_READ = 6u,
  STEP_CFG7_WRITE = 7u,
  STEP_CFG7_READ = 8u,
  STEP_CFG8_WRITE = 9u,
  STEP_CFG8_READ = 10u,
  STEP_CFG12_WRITE = 11u,
  STEP_CFG12_READ = 12u,
  STEP_STATUS_PRE_0 = 13u,
  STEP_STATUS_PRE_1 = 14u,
  STEP_STATUS_PRE_2 = 15u,
  STEP_CLEAR_WRITE = 16u,
  STEP_CLEAR_READ = 17u,
  STEP_LOCK_WRITE = 18u,
  STEP_LOCK_READ = 19u,
  STEP_STATUS_FINAL_0 = 20u,
  STEP_STATUS_FINAL_1 = 21u,
  STEP_STATUS_FINAL_2 = 22u,
  STEP_FINAL_GAIN = 23u,
  STEP_FINAL_BUCK = 24u,
};

static const uint32_t kTim1PwmPinsA = LL_GPIO_PIN_8 | LL_GPIO_PIN_9 | LL_GPIO_PIN_10;
static const uint32_t kTim1PwmPinsB = LL_GPIO_PIN_13 | LL_GPIO_PIN_14 | LL_GPIO_PIN_15;
static const uint8_t kTim1PwmChannels = LL_TIM_CHANNEL_CH1 | LL_TIM_CHANNEL_CH1N |
                                        LL_TIM_CHANNEL_CH2 | LL_TIM_CHANNEL_CH2N |
                                        LL_TIM_CHANNEL_CH3 | LL_TIM_CHANNEL_CH3N;

static uint32_t normalize_status(uint16_t s0, uint16_t s1, uint16_t s2) {
  return gl30_drv8316_status_word_faults(0u, s0) |
         gl30_drv8316_status_word_faults(1u, s1) |
         gl30_drv8316_status_word_faults(2u, s2);
}

static uint16_t expected_tx_for_step(uint8_t step) {
  switch (step) {
    case STEP_UNLOCK_WRITE: return gl30_drv8316_make_frame(false, 3u, 0x03u);
    case STEP_CFG4_WRITE:   return gl30_drv8316_make_frame(false, 4u, 0x68u);
    case STEP_CFG4_READ:    return gl30_drv8316_make_frame(true, 4u, 0x00u);
    case STEP_CFG5_WRITE:   return gl30_drv8316_make_frame(false, 5u, 0x5Fu);
    case STEP_CFG5_READ:    return gl30_drv8316_make_frame(true, 5u, 0x00u);
    case STEP_CFG6_WRITE:   return gl30_drv8316_make_frame(false, 6u, 0x10u);
    case STEP_CFG6_READ:    return gl30_drv8316_make_frame(true, 6u, 0x00u);
    case STEP_CFG7_WRITE:   return gl30_drv8316_make_frame(false, 7u, 0x02u);
    case STEP_CFG7_READ:    return gl30_drv8316_make_frame(true, 7u, 0x00u);
    case STEP_CFG8_WRITE:   return gl30_drv8316_make_frame(false, 8u, 0x10u);
    case STEP_CFG8_READ:    return gl30_drv8316_make_frame(true, 8u, 0x00u);
    case STEP_CFG12_WRITE:  return gl30_drv8316_make_frame(false, 12u, 0x00u);
    case STEP_CFG12_READ:   return gl30_drv8316_make_frame(true, 12u, 0x00u);
    case STEP_STATUS_PRE_0:
    case STEP_STATUS_FINAL_0: return gl30_drv8316_make_frame(true, 0u, 0x00u);
    case STEP_STATUS_PRE_1:
    case STEP_STATUS_FINAL_1: return gl30_drv8316_make_frame(true, 1u, 0x00u);
    case STEP_STATUS_PRE_2:
    case STEP_STATUS_FINAL_2: return gl30_drv8316_make_frame(true, 2u, 0x00u);
    case STEP_CLEAR_WRITE:   return gl30_drv8316_make_frame(false, 4u, 0x69u);
    case STEP_CLEAR_READ:    return gl30_drv8316_make_frame(true, 4u, 0x00u);
    case STEP_LOCK_WRITE:    return gl30_drv8316_make_frame(false, 3u, 0x06u);
    case STEP_LOCK_READ:     return gl30_drv8316_make_frame(true, 3u, 0x00u);
    case STEP_FINAL_GAIN:    return gl30_drv8316_make_frame(true, 7u, 0x00u);
    case STEP_FINAL_BUCK:    return gl30_drv8316_make_frame(true, 8u, 0x00u);
    default: return 0x0000u;
  }
}

static uint16_t expected_rx_for_step(uint8_t step, const uint16_t pre_status[3], const uint16_t final_status[3]) {
  switch (step) {
    case STEP_UNLOCK_WRITE:
    case STEP_CFG4_WRITE:
    case STEP_CFG5_WRITE:
    case STEP_CFG6_WRITE:
    case STEP_CFG7_WRITE:
    case STEP_CFG8_WRITE:
    case STEP_CFG12_WRITE:
    case STEP_CLEAR_WRITE:
    case STEP_LOCK_WRITE:
      return 0x0000u;
    case STEP_CFG4_READ: return 0x0068u;
    case STEP_CFG5_READ: return 0x005Fu;
    case STEP_CFG6_READ: return 0x0010u;
    case STEP_CFG7_READ: return 0x0002u;
    case STEP_CFG8_READ: return 0x0010u;
    case STEP_CFG12_READ:return 0x0000u;
    case STEP_STATUS_PRE_0: return pre_status[0];
    case STEP_STATUS_PRE_1: return pre_status[1];
    case STEP_STATUS_PRE_2: return pre_status[2];
    case STEP_CLEAR_READ: return 0x0068u;
    case STEP_LOCK_READ: return 0x0006u;
    case STEP_STATUS_FINAL_0: return final_status[0];
    case STEP_STATUS_FINAL_1: return final_status[1];
    case STEP_STATUS_FINAL_2: return final_status[2];
    case STEP_FINAL_GAIN: return 0x0002u;
    case STEP_FINAL_BUCK: return 0x0010u;
    default: return 0x0000u;
  }
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

static size_t spi1_tx_count_in_transfers(uint16_t frame) {
  size_t total = 0u;
  for (size_t i = 0u; i < bench_hw_fake_spi1_transfer_count(); ++i) {
    if (bench_hw_fake_spi1_tx_word(i) == frame) { ++total; }
  }
  return total;
}

static size_t spi1_tx_find_first_in_transfers(uint16_t frame) {
  for (size_t i = 0u; i < bench_hw_fake_spi1_transfer_count(); ++i) {
    if (bench_hw_fake_spi1_tx_word(i) == frame) { return i; }
  }
  return (size_t)-1u;
}

static size_t queue_configure_full_plan(const uint16_t pre_status[3], const uint16_t final_status[3]) {
  const bool clear_needed = normalize_status(pre_status[0], pre_status[1], pre_status[2]) == 0x08u;
  bench_hw_fake_spi_queue_clear_spi3();
  for (uint8_t step = STEP_UNLOCK_WRITE; step <= STEP_FINAL_BUCK; ++step) {
    if (!clear_needed && step >= STEP_CLEAR_WRITE && step <= STEP_CLEAR_READ) { continue; }
    bench_hw_fake_spi3_push(expected_tx_for_step(step),
                           expected_rx_for_step(step, pre_status, final_status), false);
  }
  return clear_needed ? 25u : 23u;
}

static size_t queue_configure_failure_plan(uint8_t fail_step, bool fail_timeout, uint16_t fail_rx,
                                          bool cleanup_write_timeout, bool clear_enabled,
                                          const uint16_t pre_status[3], const uint16_t final_status[3]) {
  bench_hw_fake_spi_queue_clear_spi3();
  for (uint8_t step = STEP_UNLOCK_WRITE; step <= STEP_FINAL_BUCK; ++step) {
    if (!clear_enabled && step >= STEP_CLEAR_WRITE && step <= STEP_CLEAR_READ) { continue; }
    uint16_t expected_rx = expected_rx_for_step(step, pre_status, final_status);
    bool timeout = false;
    if (step == fail_step) {
      timeout = fail_timeout;
      expected_rx = fail_rx;
      bench_hw_fake_spi3_push(expected_tx_for_step(step), expected_rx, timeout);
      break;
    }
    bench_hw_fake_spi3_push(expected_tx_for_step(step), expected_rx, false);
  }
  // Always append cleanup lock attempt for configure failures.
  bench_hw_fake_spi3_push(expected_tx_for_step(STEP_LOCK_WRITE), 0x0000u, cleanup_write_timeout);
  if (!cleanup_write_timeout) {
    bench_hw_fake_spi3_push(expected_tx_for_step(STEP_LOCK_READ), 0x0006u, false);
  }
  return (size_t)bench_hw_fake_spi3_queue_len();
}

static size_t queue_status_only_plan(const uint16_t status[3], uint16_t gain, uint16_t buck) {
  bench_hw_fake_spi_queue_clear_spi3();
  bench_hw_fake_spi3_push(expected_tx_for_step(STEP_STATUS_FINAL_0), status[0], false);
  bench_hw_fake_spi3_push(expected_tx_for_step(STEP_STATUS_FINAL_1), status[1], false);
  bench_hw_fake_spi3_push(expected_tx_for_step(STEP_STATUS_FINAL_2), status[2], false);
  bench_hw_fake_spi3_push(expected_tx_for_step(STEP_FINAL_GAIN), gain, false);
  bench_hw_fake_spi3_push(expected_tx_for_step(STEP_FINAL_BUCK), buck, false);
  return 5u;
}

static size_t queue_snapshot_plan(const uint16_t status[3]) {
  bench_hw_fake_spi_queue_clear_spi3();
  for (uint8_t step = STEP_STATUS_FINAL_0; step <= STEP_STATUS_FINAL_2; ++step) {
    bench_hw_fake_spi3_push(expected_tx_for_step(step), expected_rx_for_step(step, status, status), false);
  }
  return 3u;
}

static size_t queue_snapshot_failure_plan(uint8_t fail_step, bool fail_timeout, uint16_t fail_rx,
                                         const uint16_t status[3]) {
  bench_hw_fake_spi_queue_clear_spi3();
  for (uint8_t step = STEP_STATUS_FINAL_0; step <= STEP_STATUS_FINAL_2; ++step) {
    uint16_t tx = expected_tx_for_step(step);
    uint16_t rx = expected_rx_for_step(step, status, status);
    bool timeout = false;
    if (step == fail_step) {
      timeout = fail_timeout;
      rx = fail_rx;
      bench_hw_fake_spi3_push(tx, rx, timeout);
      break;
    }
    bench_hw_fake_spi3_push(tx, rx, timeout);
  }
  return (size_t)bench_hw_fake_spi3_queue_len();
}

static void assert_snapshot_reads_only_status_frames(void) {
  CHECK_EQ_U32((uint32_t)tx_count_in_transfers(expected_tx_for_step(STEP_STATUS_FINAL_0)), 1u, "snapshot uses status frame 0 once");
  CHECK_EQ_U32((uint32_t)tx_count_in_transfers(expected_tx_for_step(STEP_STATUS_FINAL_1)), 1u, "snapshot uses status frame 1 once");
  CHECK_EQ_U32((uint32_t)tx_count_in_transfers(expected_tx_for_step(STEP_STATUS_FINAL_2)), 1u, "snapshot uses status frame 2 once");
  CHECK_EQ_U32((uint32_t)tx_count_in_transfers(expected_tx_for_step(STEP_CFG4_WRITE)), 0u, "snapshot does not write config frame");
  CHECK_EQ_U32((uint32_t)tx_count_in_transfers(expected_tx_for_step(STEP_LOCK_WRITE)), 0u, "snapshot does not write lock frame");
  CHECK_EQ_U32((uint32_t)tx_count_in_transfers(expected_tx_for_step(STEP_LOCK_READ)), 0u, "snapshot does not read lock register");
  CHECK_EQ_U32((uint32_t)tx_count_in_transfers(expected_tx_for_step(STEP_CLEAR_WRITE)), 0u, "snapshot does not write clear frame");
  CHECK_EQ_U32((uint32_t)tx_count_in_transfers(expected_tx_for_step(STEP_CLEAR_READ)), 0u, "snapshot does not read clear frame");
}

static void assert_snapshot_no_wake_clear_relock_frames(void) {
  CHECK_EQ_U32((uint32_t)tx_count_in_transfers(expected_tx_for_step(STEP_UNLOCK_WRITE)), 0u, "snapshot does not touch driver unlock/write");
  CHECK_EQ_U32((uint32_t)tx_count_in_transfers(expected_tx_for_step(STEP_FINAL_GAIN)), 0u, "snapshot does not read final gain");
  CHECK_EQ_U32((uint32_t)tx_count_in_transfers(expected_tx_for_step(STEP_FINAL_BUCK)), 0u, "snapshot does not read final buck");
}

static void assert_masked_u32_eq(uint32_t actual, uint32_t expected, uint32_t mask, const char *msg) {
  CHECK((actual & mask) == (expected & mask), msg);
}

static void assert_drv_cleanup_states(const bench_drv_diag_t *d) {
  CHECK_EQ_U8(d->pre_cleanup.valid, 1u, "failure captures pre-cleanup snapshot");
  CHECK_EQ_U8(d->post_cleanup.valid, 1u, "failure captures post-cleanup snapshot");
  CHECK(!d->pre_cleanup.moe, "pre-cleanup keeps TIM1 MOE disabled");
  CHECK_EQ_U32(d->pre_cleanup.tim1_ccer, 0u, "pre-cleanup preserves TIM1 CCER disabled");
  CHECK((d->pre_cleanup.gpioc_odr & DRV_OFF_Pin) == 0u,
        "pre-cleanup captures DRVOFF deasserted before hard-off");
  CHECK((d->post_cleanup.gpioc_odr & DRV_OFF_Pin) != 0u,
        "post-cleanup captures DRVOFF asserted after hard-off");
  /* Hard-off asserts DRVOFF, not nSLEEP: the latter stays awake for relock.
   * The diagnostic is actual IDR, intentionally independent of commanded ODR. */
  CHECK_EQ_U8(d->post_cleanup.nsleep,
              (d->post_cleanup.gpioc_idr & DRV_NSLEEP_Pin) != 0u,
              "post-cleanup nSLEEP records measured input, not a presumed low");
  CHECK((d->post_cleanup.gpioc_odr & DRV_NSLEEP_Pin) != 0u,
        "hard-off preserves commanded nSLEEP high for SPI relock");
  CHECK(!d->post_cleanup.moe, "post-cleanup keeps TIM1 MOE disabled");
  CHECK_EQ_U32(d->post_cleanup.tim1_ccer, 0u, "post-cleanup preserves TIM1 CCER disabled");
  CHECK_EQ_U32(d->pre_cleanup.pb12_pupdr, GPIOB->PUPDR, "pre-cleanup captures current GPIOB PUPDR");
  CHECK_EQ_U32(d->post_cleanup.pb12_pupdr, GPIOB->PUPDR, "post-cleanup captures current GPIOB PUPDR");
  CHECK((d->pre_cleanup.gpioa_idr & 0x0000FFFFu) == (GPIOA->IDR & 0x0000FFFFu),
        "pre-cleanup preserves GPIOA IDR LSB bits");
  CHECK((d->pre_cleanup.gpiob_idr & 0x0000FFFFu) == (GPIOB->IDR & 0x0000FFFFu),
        "pre-cleanup preserves GPIOB IDR LSB bits");
  CHECK((d->pre_cleanup.gpioc_idr & 0x0000FFFFu) == (GPIOC->IDR & 0x0000FFFFu),
        "pre-cleanup preserves GPIOC IDR LSB bits");
  CHECK((d->pre_cleanup.gpioa_odr & 0x0000F807u) == (GPIOA->ODR & 0x0000F807u),
        "pre-cleanup preserves unaffected GPIOA ODR bits");
  CHECK((d->pre_cleanup.gpiob_odr & 0x1FFFu) == (GPIOB->ODR & 0x1FFFu),
        "pre-cleanup preserves unaffected GPIOB ODR bits");
  CHECK((d->pre_cleanup.gpioc_odr & 0xF8FFu) == (GPIOC->ODR & 0xF8FFu),
        "pre-cleanup preserves unaffected GPIOC ODR bits");
  CHECK((d->post_cleanup.gpioa_idr & 0x0000FFFFu) == (GPIOA->IDR & 0x0000FFFFu),
        "post-cleanup preserves GPIOA IDR LSB bits");
  CHECK((d->post_cleanup.gpiob_idr & 0x0000FFFFu) == (GPIOB->IDR & 0x0000FFFFu),
        "post-cleanup preserves GPIOB IDR LSB bits");
  CHECK((d->post_cleanup.gpioc_idr & 0x0000FFFFu) == (GPIOC->IDR & 0x0000FFFFu),
        "post-cleanup preserves GPIOC IDR LSB bits");
  CHECK((d->post_cleanup.gpioa_odr & 0x0000F807u) == (GPIOA->ODR & 0x0000F807u),
        "post-cleanup preserves unaffected GPIOA ODR bits");
  CHECK((d->post_cleanup.gpiob_odr & 0x1FFFu) == (GPIOB->ODR & 0x1FFFu),
        "post-cleanup preserves untouched GPIOB ODR bits");
  CHECK((d->post_cleanup.gpioc_odr & 0xF8FFu) == (GPIOC->ODR & 0xF8FFu),
        "post-cleanup preserves untouched GPIOC ODR bits");
}

static void assert_tx_count(uint16_t frame, uint32_t expected, const char *msg) {
  uint32_t total = 0u;
  for (uint32_t i = 0u; i < 64u; ++i) {
    if (bench_hw_fake_spi3_tx_word(i) == frame) { ++total; }
  }
  CHECK_EQ_U32(total, expected, msg);
}

static size_t tx_count_in_transfers(uint16_t frame) {
  size_t total = 0u;
  for (size_t i = 0u; i < bench_hw_fake_spi3_transfer_count(); ++i) {
    if (bench_hw_fake_spi3_tx_word(i) == frame) {
      ++total;
    }
  }
  return total;
}

static size_t tx_find_first_in_transfers(uint16_t frame) {
  for (size_t i = 0u; i < bench_hw_fake_spi3_transfer_count(); ++i) {
    if (bench_hw_fake_spi3_tx_word(i) == frame) {
      return i;
    }
  }
  return (size_t)-1u;
}

static void assert_clear_write_read_sequence_exactly_once(void) {
  const uint16_t clear_write = expected_tx_for_step(STEP_CLEAR_WRITE);
  const uint16_t clear_read = expected_tx_for_step(STEP_CLEAR_READ);
  const size_t count = tx_count_in_transfers(clear_write);
  CHECK_EQ_U32((uint32_t)count, 1u, "clear fault-clear write occurs exactly once");
  const size_t idx = tx_find_first_in_transfers(clear_write);
  CHECK(idx != (size_t)-1u, "clear fault-clear write appears in transfer sequence");
  CHECK(idx + 1u < bench_hw_fake_spi3_transfer_count(), "clear write is followed by another transfer");
  if (idx != (size_t)-1u && idx + 1u < bench_hw_fake_spi3_transfer_count()) {
    CHECK_EQ_U16(bench_hw_fake_spi3_tx_word(idx + 1u), clear_read, "clear write is followed by clear read");
  }
}

static void assert_no_clear_write_sequence(void) {
  const size_t count = tx_count_in_transfers(expected_tx_for_step(STEP_CLEAR_WRITE));
  CHECK_EQ_U32((uint32_t)count, 0u, "clear fault-clear write is never attempted");
}

static void assert_queue_consumed(size_t expected, const char *msg) {
  CHECK_EQ_U32(bench_hw_fake_spi3_queue_pos(), expected, msg);
  CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), expected, "no unconsumed SPI queue entries");
}

static void assert_configure_outputs_frozen(void) {
  CHECK(!bench_hw_fake_tim1_moe(), "configure keeps TIM1 MOE low");
  CHECK_EQ_U8(bench_hw_fake_tim1_channel_mask(), 0u, "configure keeps PWM channels disabled");
  CHECK_EQ_U32(bench_hw_fake_tim1_enable_all_outputs_count(), 0u, "configure does not enable TIM1 all outputs");
  CHECK_EQ_U32(bench_hw_fake_tim1_cc_enable_count(), 0u, "configure does not enable TIM1 channels");
}

static void snapshot_prepare_ready_cache(bench_drv_diag_t *out) {
  const uint16_t pre_status[3] = {0x0808u, 0x0800u, 0x0800u};
  const uint16_t final_status[3] = {0x0808u, 0x0800u, 0x0800u};
  bench_hw_fake_reset();
  queue_configure_full_plan(pre_status, final_status);
  bench_hw_fake_set_nfault_input(true);
  CHECK(bench_hw_driver_configure(), "snapshot fixture configure succeeds");
  bench_hw_off();
  bench_hw_driver_diagnostics(out);
}

static uint32_t snapshot_mask_after_frames(uint8_t frames_read) {
  return frames_read == 0u ? 0u : ((1u << frames_read) - 1u);
}

static uint32_t snapshot_raw_from_frames(const uint16_t status[3], uint8_t frames_read) {
  uint32_t raw = 0u;
  if (frames_read > 0u) { raw |= (uint32_t)(status[0u] & 0xFFu); }
  if (frames_read > 1u) { raw |= (uint32_t)(status[1u] & 0xFFu) << 8u; }
  if (frames_read > 2u) { raw |= (uint32_t)(status[2u] & 0xFFu) << 16u; }
  return raw;
}

static uint32_t snapshot_norm_from_frames(const uint16_t status[3], uint8_t frames_read) {
  uint32_t norm = 0u;
  if (frames_read > 0u) { norm |= gl30_drv8316_status_word_faults(0u, status[0u]); }
  if (frames_read > 1u) { norm |= gl30_drv8316_status_word_faults(1u, status[1u]); }
  if (frames_read > 2u) { norm |= gl30_drv8316_status_word_faults(2u, status[2u]); }
  return norm;
}

static bool gpio_pin_mode_is(const GPIO_TypeDef *port, uint32_t mask, uint32_t expected_mode) {
  if (port == NULL) { return false; }
  for (uint32_t pin = 0u; pin < 16u; ++pin) {
    const uint32_t bit = 1u << pin;
    if ((mask & bit) == 0u) { continue; }
    if (((port->MODER >> (pin * 2u)) & 0x3u) != expected_mode) { return false; }
  }
  return true;
}

static uint32_t gpio_mask_low_count(const GPIO_TypeDef *port, uint32_t mask) {
  if (port == NULL) { return 0u; }
  uint32_t count = 0u;
  for (uint32_t bit = 0u; bit < 16u; ++bit) {
    const uint32_t pin = 1u << bit;
    if ((mask & pin) != 0u && (port->ODR & pin) == 0u) { ++count; }
  }
  return count;
}

static void assert_pwm_channels_low_output_mode_output(void) {
  CHECK(gpio_pin_mode_is(GPIOA, kTim1PwmPinsA, LL_GPIO_MODE_OUTPUT),
        "parked PWM channel A pins are in GPIO output mode");
  CHECK(gpio_pin_mode_is(GPIOB, kTim1PwmPinsB, LL_GPIO_MODE_OUTPUT),
        "parked PWM channel B pins are in GPIO output mode");
  CHECK_EQ_U32(gpio_mask_low_count(GPIOA, kTim1PwmPinsA), 3u,
               "parked PWM channel A pins are all low");
  CHECK_EQ_U32(gpio_mask_low_count(GPIOB, kTim1PwmPinsB), 3u,
               "parked PWM channel B pins are all low");
  CHECK_EQ_U32(GPIOA->OTYPER & kTim1PwmPinsA, 0u,
               "parked PWM channel A pins use push-pull output type");
  CHECK_EQ_U32(GPIOB->OTYPER & kTim1PwmPinsB, 0u,
               "parked PWM channel B pins use push-pull output type");
}

static void assert_pwm_channels_alt_mode_output(void) {
  CHECK(gpio_pin_mode_is(GPIOA, kTim1PwmPinsA, LL_GPIO_MODE_ALTERNATE),
        "armed PWM channel A pins are in alternate mode");
  CHECK(gpio_pin_mode_is(GPIOB, kTim1PwmPinsB, LL_GPIO_MODE_ALTERNATE),
        "armed PWM channel B pins are in alternate mode");
}

static bool pwm_armed_channel_mask_is_expected(void) {
  return (TIM1->enabled_channels == kTim1PwmChannels);
}

static void check_break_test_pupdr_and_outputs(uint32_t initial_pull, uint32_t base_pupdr, bool trigger_brk,
                                              bool expected_result, const char *label) {
  const uint32_t pb12_mask = 3u << (12u * 2u);
  bench_hw_fake_reset();
  GPIOB->PUPDR = base_pupdr;
  bench_hw_fake_set_break_down_brk(trigger_brk);

  LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_12, initial_pull);
  const uint32_t before = GPIOB->PUPDR;

  const bool pass = bench_hw_break_test();
  const uint32_t after = GPIOB->PUPDR;

  CHECK(pass == expected_result, label);
  CHECK_EQ_U32(after & (uint32_t)~pb12_mask, before & (uint32_t)~pb12_mask,
               "break test changes only PB12 PUPDR bits");
  CHECK_EQ_U32(bench_hw_fake_gpio_pull_for_pin(GPIOB, LL_GPIO_PIN_12), initial_pull,
               "break test restores initial PB12 pull mode");
  CHECK(!bench_hw_fake_tim1_moe(), "break test keeps TIM1 MOE disabled");
  CHECK_EQ_U8(bench_hw_fake_tim1_channel_mask(), 0u, "break test keeps PWM channels disabled");
  CHECK(bench_hw_fake_gpio_is_set(DRV_OFF_GPIO_Port, DRV_OFF_Pin), "break test keeps DRVOFF asserted");
}

static void test_bench_hw_break_test_restores_pb12_pull_for_all_initial_states(void) {
  const uint32_t base_pupdr = 0x5AA55AA5u;
  check_break_test_pupdr_and_outputs(LL_GPIO_PULL_UP, base_pupdr, true, true,
                                    "break test with initial PUPDR_UP succeeds and restores settings");
  check_break_test_pupdr_and_outputs(LL_GPIO_PULL_DOWN, base_pupdr, true, true,
                                    "break test with initial PUPDR_DOWN succeeds and restores settings");
  check_break_test_pupdr_and_outputs(LL_GPIO_PULL_NONE, base_pupdr, true, true,
                                    "break test with initial PUPDR_NONE succeeds and restores settings");
}

static void test_bench_hw_break_test_fails_when_down_pull_does_not_raise_brk(void) {
  check_break_test_pupdr_and_outputs(LL_GPIO_PULL_UP, 0xA55AA5A5u, false, false,
                                    "break test fails when BRK is not raised on pull-down");
}

static void dump_diag_state(const char *label, const bench_drv_diag_t *d) {
  fprintf(stderr,
          "[DIAG] %s stage=%d reason=%d tx=0x%04x rx=0x%04x transfer_ok=%u expected=%u relock_attempted=%u relock_ok=%u relock_rx=0x%04x qlen=%zu qpos=%zu t=%zu mismatch=%zu status_read_mask=0x%02x config_read_mask=0x%02x normalized=%u\n",
          label, (int)d->stage, (int)d->reason, d->tx, d->rx, (unsigned)d->transfer_ok,
          (unsigned)d->expected, (unsigned)d->relock_attempted, (unsigned)d->relock_ok, d->relock_rx,
          bench_hw_fake_spi3_queue_len(), bench_hw_fake_spi3_queue_pos(),
          bench_hw_fake_spi3_transfer_count_all(), bench_hw_fake_spi3_mismatch_count(),
          d->status_read_mask, d->config_read_mask, d->normalized_status);
  for (size_t i = 0u; i < 24u; ++i) {
    fprintf(stderr, "[DIAG] tx[%zu]=0x%04x queued[%zu]=0x%04x rx=%04x to=%d\n",
          i, bench_hw_fake_spi3_tx_word(i), i, bench_hw_fake_spi3_queued_tx(i),
          bench_hw_fake_spi3_queued_rx(i), (int)bench_hw_fake_spi3_queued_rx_timeout(i));
  }
}

static void test_bench_hw_driver_configure_happy_path(void) {
  const uint16_t pre_status[3] = {0x0808u, 0x0800u, 0x0800u};
  const uint16_t final_status[3] = {0x0808u, 0x0800u, 0x0800u};
  bench_hw_fake_reset();
  const size_t queued = queue_configure_full_plan(pre_status, final_status);
  bench_hw_fake_set_bench_time_step(1u);
  bench_hw_fake_set_nfault_input(true);

  CHECK(bench_hw_driver_configure(), "healthy 0808/0800/0800 config succeeds");
  bench_drv_diag_t d = {0};
  bench_hw_driver_diagnostics(&d);
  if (d.reason != BENCH_DRV_REASON_NONE || d.stage != BENCH_DRV_STAGE_READY) { dump_diag_state("healthy", &d); }

  CHECK_EQ_U8(d.stage, BENCH_DRV_STAGE_READY, "healthy config reaches READY");
  CHECK_EQ_U8(d.reason, BENCH_DRV_REASON_NONE, "healthy config has no reason");
  CHECK_EQ_U32(d.config_read_mask, 0x3Fu, "healthy config read six config registers");
  CHECK_EQ_U32(d.status_read_mask, 0x07u, "healthy config reads all three status words");
  CHECK_EQ_U32(d.normalized_status, 0u, "healthy pre/final status normalizes to 0");
  CHECK(d.status_us[0u] < d.status_us[1u], "status timestamps increase");
  CHECK(d.status_us[1u] < d.status_us[2u], "status timestamps continue increasing");
  CHECK_EQ_U8(d.relock_attempted, 0u, "healthy path skips relock");
  CHECK_EQ_U8(d.relock_ok, 0u, "healthy path no relock result");
  CHECK(!bench_hw_fake_gpio_is_set(DRV_OFF_GPIO_Port, DRV_OFF_Pin), "configure deasserts DRVOFF to allow PWM startup sequence");
  CHECK_EQ_U32(bench_hw_fake_drv_off_set_count(), 1u, "configure asserts DRVOFF once at start");
  CHECK_EQ_U32(bench_hw_fake_drv_off_reset_count(), 1u, "configure deasserts DRVOFF once before normalizing");
  CHECK_EQ_U32(queued, 23u, "healthy path transfers 23 frames");
  assert_no_clear_write_sequence();
  CHECK_EQ_U32(bench_hw_fake_spi3_mismatch_count(), 0u, "all expected SPI tx words match");
  assert_queue_consumed(queued, "healthy path consumes complete SPI queue");
  assert_configure_outputs_frozen();
}

static void test_bench_hw_driver_configure_isolated_npor_clear_once(void) {
  const uint16_t pre_status[3] = {0x0000u, 0x0000u, 0x0000u};
  const uint16_t final_status[3] = {0x0808u, 0x0800u, 0x0800u};
  bench_hw_fake_reset();
  const size_t queued = queue_configure_full_plan(pre_status, final_status);
  bench_hw_fake_set_bench_time_step(1u);
  bench_hw_fake_set_nfault_input(true);

  CHECK(bench_hw_driver_configure(), "isolated NPOR pre-status reaches READY");
  bench_drv_diag_t d = {0};
  bench_hw_driver_diagnostics(&d);
  CHECK_EQ_U8(d.stage, BENCH_DRV_STAGE_READY, "isolated NPOR path ends READY");
  CHECK_EQ_U8(d.reason, BENCH_DRV_REASON_NONE, "isolated NPOR path clears without error");
  CHECK_EQ_U32(d.normalized_status, 0u, "isolated NPOR final normalized status is 0");
  CHECK_EQ_U32(normalize_status(pre_status[0], pre_status[1], pre_status[2]), 0x08u, "isolated NPOR pre-status normalizes to 0x08");
  assert_clear_write_read_sequence_exactly_once();
  CHECK_EQ_U32(queued, 25u, "isolated NPOR clear path transfers 25 frames");
  CHECK_EQ_U32(bench_hw_fake_spi3_mismatch_count(), 0u, "all expected SPI tx words match");
  assert_queue_consumed(queued, "isolated NPOR path consumes complete SPI queue");
  assert_configure_outputs_frozen();
}

static void test_bench_hw_driver_configure_final_npor_fails_without_clear(void) {
  const uint16_t pre_status[3] = {0x0808u, 0x0800u, 0x0800u};
  const uint16_t final_status[3] = {0x0000u, 0x0000u, 0x0000u};

  bench_hw_fake_reset();
  const size_t queued = queue_configure_failure_plan(STEP_STATUS_FINAL_2, false, final_status[2], false, false, pre_status, final_status);
  bench_hw_fake_set_nfault_input(true);
  CHECK(!bench_hw_driver_configure(), "final NPOR rejects configure");
  bench_drv_diag_t d = {0};
  bench_hw_driver_diagnostics(&d);
  if (d.stage != BENCH_DRV_STAGE_STATUS_FINAL || d.reason != BENCH_DRV_REASON_STATUS_FLAGS) { dump_diag_state("final_npor", &d); }
  CHECK_EQ_U8(d.stage, BENCH_DRV_STAGE_STATUS_FINAL, "final NPOR fails at STATUS_FINAL stage");
  CHECK_EQ_U8(d.reason, BENCH_DRV_REASON_STATUS_FLAGS, "final NPOR fail reason is status flags");
  CHECK_EQ_U16(d.tx, expected_tx_for_step(STEP_STATUS_FINAL_2), "final NPOR preserves first failing tx");
  CHECK_EQ_U16(d.rx, final_status[2], "final NPOR preserves first failing rx");
  CHECK(d.transfer_ok, "failure transport success for first failure");
  CHECK_EQ_U8(d.expected, 0x00u, "final NPOR expected value is 0");
  assert_no_clear_write_sequence();
  CHECK_EQ_U8(d.relock_attempted, 1u, "failure triggers cleanup attempt");
  CHECK_EQ_U8(d.relock_ok, 1u, "cleanup succeeds for final NPOR path");
  CHECK_EQ_U16(d.relock_rx, 0x0006u, "cleanup returns lock register readback");
  CHECK_EQ_U32(bench_hw_fake_spi3_mismatch_count(), 0u, "all expected SPI tx words match");
  assert_queue_consumed(queued, "final NPOR cleanup does not alter queue accounting");
  assert_configure_outputs_frozen();
}

static void test_bench_hw_driver_configure_status_pre_fault_rejects_without_clear(void) {
  const uint16_t final_status[3] = {0x0808u, 0x0800u, 0x0800u};

  const uint16_t pre_fault01[3] = {0x0801u, 0x0800u, 0x0800u};
  bench_hw_fake_reset();
  const size_t queued01 = queue_configure_failure_plan(STEP_STATUS_PRE_2, false, pre_fault01[2], false, false, pre_fault01, final_status);
  bench_hw_fake_set_nfault_input(true);
  CHECK(!bench_hw_driver_configure(), "pre-status 0801 rejects without clear");
  bench_drv_diag_t d = {0};
  bench_hw_driver_diagnostics(&d);
  if (d.stage != BENCH_DRV_STAGE_STATUS_PRE || d.reason != BENCH_DRV_REASON_STATUS_FLAGS) { dump_diag_state("pre01", &d); }
  CHECK_EQ_U8(d.stage, BENCH_DRV_STAGE_STATUS_PRE, "pre 0801 fails at STATUS_PRE");
  CHECK_EQ_U8(d.reason, BENCH_DRV_REASON_STATUS_FLAGS, "pre 0801 reason is status flags");
  CHECK_EQ_U16(d.tx, expected_tx_for_step(STEP_STATUS_PRE_2), "pre 0801 preserves first failing tx");
  CHECK_EQ_U16(d.rx, pre_fault01[2], "pre 0801 preserves first failing rx");
  CHECK(d.transfer_ok, "pre 0801 failure keeps transfer_ok");
  CHECK_EQ_U8(d.relock_attempted, 1u, "pre 0801 failure triggers cleanup");
  assert_queue_consumed(queued01, "pre 0801 consumes exact failing queue");
  assert_no_clear_write_sequence();
  CHECK_EQ_U32(bench_hw_fake_spi3_mismatch_count(), 0u, "pre 0801 no tx mismatch");
  assert_configure_outputs_frozen();

  const uint16_t pre_fault08[3] = {0x0908u, 0x0800u, 0x0800u};
  const size_t queued08 = queue_configure_failure_plan(STEP_STATUS_PRE_2, false, pre_fault08[2], false, false, pre_fault08, final_status);
  bench_hw_fake_set_nfault_input(true);
  CHECK(!bench_hw_driver_configure(), "pre-status 0908 summary-only rejects without clear");
  bench_hw_driver_diagnostics(&d);
  if (d.stage != BENCH_DRV_STAGE_STATUS_PRE || d.reason != BENCH_DRV_REASON_STATUS_FLAGS) { dump_diag_state("pre08", &d); }
  CHECK_EQ_U8(d.stage, BENCH_DRV_STAGE_STATUS_PRE, "pre 0908 fails at STATUS_PRE");
  CHECK_EQ_U8(d.reason, BENCH_DRV_REASON_STATUS_FLAGS, "pre 0908 reason is status flags");
  CHECK_EQ_U16(d.tx, expected_tx_for_step(STEP_STATUS_PRE_2), "pre 0908 preserves first failing tx");
  CHECK_EQ_U16(d.rx, pre_fault08[2], "pre 0908 preserves first failing rx");
  CHECK(d.transfer_ok, "pre 0908 failure keeps transfer_ok");
  CHECK_EQ_U8(d.relock_attempted, 1u, "pre 0908 failure triggers cleanup");
  CHECK_EQ_U8(d.relock_ok, 1u, "pre 0908 cleanup succeeds");
  assert_queue_consumed(queued08, "pre 0908 consumes exact failing queue");
  assert_no_clear_write_sequence();
  CHECK_EQ_U32(bench_hw_fake_spi3_mismatch_count(), 0u, "pre 0908 no tx mismatch");
  assert_configure_outputs_frozen();

  const uint16_t pre_fault09_stat0_09[3] = {0x0909u, 0x0800u, 0x0800u};
  const size_t queued09 = queue_configure_failure_plan(STEP_STATUS_PRE_2, false, pre_fault09_stat0_09[2], false, false, pre_fault09_stat0_09, final_status);
  bench_hw_fake_set_nfault_input(true);
  CHECK(!bench_hw_driver_configure(), "pre-status 0909 rejects without clear");
  bench_hw_driver_diagnostics(&d);
  if (d.stage != BENCH_DRV_STAGE_STATUS_PRE || d.reason != BENCH_DRV_REASON_STATUS_FLAGS) { dump_diag_state("pre09", &d); }
  CHECK_EQ_U8(d.stage, BENCH_DRV_STAGE_STATUS_PRE, "pre 0909 fails at STATUS_PRE");
  CHECK_EQ_U8(d.reason, BENCH_DRV_REASON_STATUS_FLAGS, "pre 0909 reason is status flags");
  CHECK_EQ_U16(d.tx, expected_tx_for_step(STEP_STATUS_PRE_2), "pre 0909 preserves first failing tx");
  CHECK_EQ_U16(d.rx, pre_fault09_stat0_09[2], "pre 0909 preserves first failing rx");
  CHECK(d.transfer_ok, "pre 0909 failure keeps transfer_ok");
  CHECK_EQ_U8(d.relock_attempted, 1u, "pre 0909 failure triggers cleanup");
  CHECK_EQ_U8(d.relock_ok, 1u, "pre 0909 cleanup succeeds");
  assert_queue_consumed(queued09, "pre 0909 consumes exact failing queue");
  assert_no_clear_write_sequence();
  CHECK_EQ_U32(bench_hw_fake_spi3_mismatch_count(), 0u, "pre 0909 no tx mismatch");
  assert_configure_outputs_frozen();

  for (uint8_t variant = 0u; variant < 14u; ++variant) {
    const uint8_t bit = variant % 7u;
    const uint16_t reserved = variant >= 7u ? 0x80u : 0u;
    const uint16_t pre_fault_stat2[3] = {0x0808u, 0x0800u, (uint16_t)(0x0800u | reserved | (uint16_t)(1u << bit))};
    const size_t queued_bit = queue_configure_failure_plan(STEP_STATUS_PRE_2, false, pre_fault_stat2[2],
                                                          false, false, pre_fault_stat2, final_status);
    bench_hw_fake_set_nfault_input(true);
    CHECK(!bench_hw_driver_configure(), "pre-status with each defined STAT2 detail bit rejects without clear");
    bench_hw_driver_diagnostics(&d);
    if (d.stage != BENCH_DRV_STAGE_STATUS_PRE || d.reason != BENCH_DRV_REASON_STATUS_FLAGS) {
      dump_diag_state("pre_stat2_bit", &d);
    }
    CHECK_EQ_U8(d.stage, BENCH_DRV_STAGE_STATUS_PRE, "pre STAT2 detail-bit fails at STATUS_PRE");
    CHECK_EQ_U8(d.reason, BENCH_DRV_REASON_STATUS_FLAGS, "pre defined STAT2 detail-bit reason is status flags");
    CHECK_EQ_U16(d.tx, expected_tx_for_step(STEP_STATUS_PRE_2), "pre defined STAT2 detail-bit preserves first failing tx");
    CHECK_EQ_U16(d.rx, pre_fault_stat2[2], "pre defined STAT2 detail-bit preserves first failing rx");
    CHECK(d.transfer_ok, "pre defined STAT2 detail-bit failure keeps transfer_ok");
    CHECK_EQ_U8(d.expected, 0x00u, "pre defined STAT2 detail-bit expected value is zero");
    CHECK_EQ_U32(d.status_read_mask, 0x07u, "pre defined STAT2 detail-bit reads all three pre-status words");
    CHECK_EQ_U32(d.raw_status, snapshot_raw_from_frames(pre_fault_stat2, 3u), "pre defined STAT2 detail-bit preserves raw status bytes");
    CHECK_EQ_U16(d.status_rx[2], pre_fault_stat2[2], "pre defined STAT2 detail-bit preserves raw status2");
    CHECK_EQ_U8(d.relock_attempted, 1u, "pre defined STAT2 detail-bit failure triggers cleanup");
    CHECK_EQ_U8(d.relock_ok, 1u, "pre defined STAT2 detail-bit cleanup succeeds");
    assert_drv_cleanup_states(&d);
    assert_queue_consumed(queued_bit, "pre defined STAT2 detail-bit consumes exact failing queue");
    assert_no_clear_write_sequence();
    CHECK_EQ_U32(bench_hw_fake_spi3_mismatch_count(), 0u, "pre defined STAT2 detail-bit no tx mismatch");
    assert_configure_outputs_frozen();
  }
}

static void test_bench_hw_driver_configure_status_pre_reserved_bit7_only_normalizes_and_clears_once(void) {
  const uint16_t pre_status[3] = {0x0808u, 0x0800u, 0x0080u};
  const uint16_t final_status[3] = {0x0808u, 0x0800u, 0x0880u};

  bench_hw_fake_reset();
  const size_t queued = queue_configure_full_plan(pre_status, final_status);
  bench_hw_fake_set_nfault_input(true);
  CHECK(bench_hw_driver_configure(), "pre-status 0x0080 is treated as startup NPOR and accepted");
  bench_drv_diag_t d = {0};
  bench_hw_driver_diagnostics(&d);
  CHECK_EQ_U8(d.stage, BENCH_DRV_STAGE_READY, "pre-status 0x0080 path reaches READY");
  CHECK_EQ_U8(d.reason, BENCH_DRV_REASON_NONE, "pre-status 0x0080 clears startup NPOR with no fault reason");
  CHECK_EQ_U32(d.normalized_status, 0u, "pre-status 0x0080 clears to zero normalized status");
  CHECK_EQ_U8(d.expected, 0x00u, "pre-status 0x0080 expected value is zero");
  CHECK_EQ_U8(d.relock_attempted, 0u, "pre-status 0x0080 success path does not relock");
  CHECK_EQ_U8(d.status_read_mask, 0x07u, "pre-status 0x0080 reads all three pre and final status words");
  CHECK_EQ_U16(d.status_rx[2], final_status[2], "pre-status 0x0080 preserves final status2");
  CHECK_EQ_U32(d.raw_status, snapshot_raw_from_frames(final_status, 3u), "pre-status 0x0080 preserves latest raw status bytes");
  CHECK(d.transfer_ok, "pre-status 0x0080 path completes all transfers");
  assert_clear_write_read_sequence_exactly_once();
  CHECK_EQ_U32(queued, 25u, "pre-status 0x0080 follows one clear and then finalizes");
  CHECK_EQ_U32(bench_hw_fake_spi3_mismatch_count(), 0u, "pre-status 0x0080 has no SPI mismatches");
  CHECK_EQ_U8(d.relock_ok, 0u, "pre-status 0x0080 success path skips cleanup relock");
  assert_queue_consumed(queued, "pre-status 0x0080 consumes exact queue");
  assert_configure_outputs_frozen();
}

static void test_bench_hw_driver_configure_nfault_low_rejects_without_clear(void) {
  const uint16_t pre_status[3] = {0x0808u, 0x0800u, 0x0800u};
  const uint16_t final_status[3] = {0x0808u, 0x0800u, 0x0800u};

  bench_hw_fake_reset();
  const size_t queued = queue_configure_failure_plan(STEP_STATUS_PRE_2, false, pre_status[2], false, false, pre_status, final_status);
  bench_hw_fake_set_nfault_input(false);
  CHECK(!bench_hw_driver_configure(), "nFAULT low rejects configure without clear");
  bench_drv_diag_t d = {0};
  bench_hw_driver_diagnostics(&d);
  if (d.stage != BENCH_DRV_STAGE_STATUS_PRE || d.reason != BENCH_DRV_REASON_NFAULT) { dump_diag_state("nfault", &d); }
  CHECK_EQ_U8(d.stage, BENCH_DRV_STAGE_STATUS_PRE, "nFAULT low fails at STATUS_PRE");
  CHECK_EQ_U8(d.reason, BENCH_DRV_REASON_NFAULT, "nFAULT low reason is nfault");
  CHECK_EQ_U16(d.tx, expected_tx_for_step(STEP_STATUS_PRE_2), "nFAULT low preserves first failing tx");
  CHECK_EQ_U16(d.rx, pre_status[2], "nFAULT low preserves first failing rx");
  CHECK(d.transfer_ok, "nFAULT low failure keeps transfer_ok");
  CHECK_EQ_U8(d.relock_attempted, 1u, "nFAULT low failure triggers cleanup");
  CHECK_EQ_U8(d.relock_ok, 1u, "cleanup succeeds after nFAULT low");
  CHECK_EQ_U8(d.expected, 1u, "nFAULT low expected value is 1");
  assert_queue_consumed(queued, "nFAULT low consumes exact failing queue");
  assert_no_clear_write_sequence();
  CHECK_EQ_U32(bench_hw_fake_spi3_mismatch_count(), 0u, "nFAULT low no tx mismatch");
  assert_configure_outputs_frozen();
}

static void test_bench_hw_driver_configure_final_npor_with_bit7_rejects_without_clear(void) {
  /* The low byte's reserved bit is ignored, but reply 0x0080 still has NPOR low. */
  const uint16_t pre_status[3] = {0x0808u, 0x0800u, 0x0800u};
  const uint16_t final_status[3] = {0x0808u, 0x0800u, 0x0080u};

  bench_hw_fake_reset();
  const size_t queued = queue_configure_failure_plan(STEP_STATUS_FINAL_2, false, final_status[2],
                                                    false, false, pre_status, final_status);
  bench_hw_fake_set_nfault_input(true);
  CHECK(!bench_hw_driver_configure(), "final reply 0x0080 still rejects because summary NPOR is not acknowledged");
  bench_drv_diag_t d = {0};
  bench_hw_driver_diagnostics(&d);
  if (d.stage != BENCH_DRV_STAGE_STATUS_FINAL || d.reason != BENCH_DRV_REASON_STATUS_FLAGS) {
    dump_diag_state("final80", &d);
  }
  CHECK_EQ_U8(d.stage, BENCH_DRV_STAGE_STATUS_FINAL, "final STATUS2=0x80 fails at STATUS_FINAL");
  CHECK_EQ_U8(d.reason, BENCH_DRV_REASON_STATUS_FLAGS, "final STATUS2=0x80 reason is status flags");
  CHECK_EQ_U16(d.tx, expected_tx_for_step(STEP_STATUS_FINAL_2), "final STATUS2=0x80 preserves first failing tx");
  CHECK_EQ_U16(d.rx, final_status[2], "final STATUS2=0x80 preserves first failing rx");
  CHECK(d.transfer_ok, "final STATUS2=0x80 failure keeps transfer_ok");
  CHECK_EQ_U8(d.expected, 0x00u, "final STATUS2=0x80 expected value is zero");
  CHECK_EQ_U8(d.relock_attempted, 1u, "final STATUS2=0x80 failure triggers cleanup");
  CHECK_EQ_U8(d.relock_ok, 1u, "final STATUS2=0x80 cleanup succeeds");
  CHECK_EQ_U32(d.status_read_mask, 0x07u, "final STATUS2=0x80 reads all three status words");
  CHECK_EQ_U32(d.raw_status, snapshot_raw_from_frames(final_status, 3u), "final STATUS2=0x80 preserves raw status bytes");
  CHECK_EQ_U16(d.status_rx[2], final_status[2], "final STATUS2=0x80 preserves raw status2");
  assert_drv_cleanup_states(&d);
  CHECK_EQ_U16(d.relock_rx, 0x0006u, "final STATUS2=0x80 cleanup readback is lock register");
  assert_queue_consumed(queued, "final STATUS2=0x80 consumes exact failing queue");
  CHECK_EQ_U32(bench_hw_fake_spi3_mismatch_count(), 0u, "final STATUS2=0x80 no tx mismatch");
  assert_configure_outputs_frozen();

  for (uint8_t variant = 0u; variant < 14u; ++variant) {
    const uint8_t bit = variant % 7u;
    const uint16_t reserved = variant >= 7u ? 0x80u : 0u;
    const uint16_t final_status_bit[3] = {0x0808u, 0x0800u, (uint16_t)(0x0800u | reserved | (uint16_t)(1u << bit))};
    const size_t queued_bit = queue_configure_failure_plan(STEP_STATUS_FINAL_2, false, final_status_bit[2],
                                                          false, false, pre_status, final_status_bit);
    bench_hw_fake_set_nfault_input(true);
    CHECK(!bench_hw_driver_configure(), "final-status with each defined STAT2 detail bit rejects without clear");
    bench_hw_driver_diagnostics(&d);
    if (d.stage != BENCH_DRV_STAGE_STATUS_FINAL || d.reason != BENCH_DRV_REASON_STATUS_FLAGS) {
      dump_diag_state("final_stat2_bit", &d);
    }
    CHECK_EQ_U8(d.stage, BENCH_DRV_STAGE_STATUS_FINAL, "final defined STAT2 detail-bit fails at STATUS_FINAL");
    CHECK_EQ_U8(d.reason, BENCH_DRV_REASON_STATUS_FLAGS, "final defined STAT2 detail-bit reason is status flags");
    CHECK_EQ_U16(d.tx, expected_tx_for_step(STEP_STATUS_FINAL_2), "final defined STAT2 detail-bit preserves first failing tx");
    CHECK_EQ_U16(d.rx, final_status_bit[2], "final defined STAT2 detail-bit preserves first failing rx");
    CHECK(d.transfer_ok, "final defined STAT2 detail-bit failure keeps transfer_ok");
    CHECK_EQ_U8(d.expected, 0x00u, "final defined STAT2 detail-bit expected value is zero");
    CHECK_EQ_U32(d.status_read_mask, 0x07u, "final defined STAT2 detail-bit reads all three status words");
    CHECK_EQ_U32(d.raw_status, snapshot_raw_from_frames(final_status_bit, 3u), "final defined STAT2 detail-bit preserves raw status bytes");
    CHECK_EQ_U16(d.status_rx[2], final_status_bit[2], "final defined STAT2 detail-bit preserves raw status2");
    CHECK_EQ_U8(d.relock_attempted, 1u, "final defined STAT2 detail-bit failure triggers cleanup");
    CHECK_EQ_U8(d.relock_ok, 1u, "final defined STAT2 detail-bit cleanup succeeds");
    assert_drv_cleanup_states(&d);
    CHECK_EQ_U16(d.relock_rx, 0x0006u, "final defined STAT2 detail-bit cleanup readback is lock register");
    assert_queue_consumed(queued_bit, "final defined STAT2 detail-bit consumes exact failing queue");
    CHECK_EQ_U32(bench_hw_fake_spi3_mismatch_count(), 0u, "final defined STAT2 detail-bit no tx mismatch");
    assert_configure_outputs_frozen();
  }
}

static void test_bench_hw_driver_configure_cfg6_readback_mismatch_keeps_first_failure(void) {
  const uint16_t pre_status[3] = {0x0808u, 0x0800u, 0x0800u};
  const uint16_t final_status[3] = {0x0808u, 0x0800u, 0x0800u};

  bench_hw_fake_reset();
  queue_configure_failure_plan(STEP_CFG6_READ, false, 0x0011u, false, false, pre_status, final_status);
  bench_hw_fake_set_nfault_input(true);
  CHECK(!bench_hw_driver_configure(), "cfg6 readback mismatch rejects configure");
  bench_drv_diag_t d = {0};
  bench_hw_driver_diagnostics(&d);
  if (d.stage != BENCH_DRV_STAGE_CONFIG_READ || d.reason != BENCH_DRV_REASON_READBACK) { dump_diag_state("cfg6", &d); }
  CHECK_EQ_U8(d.stage, BENCH_DRV_STAGE_CONFIG_READ, "cfg6 mismatch fails in CONFIG_READ");
  CHECK_EQ_U8(d.reason, BENCH_DRV_REASON_READBACK, "cfg6 mismatch reason is readback");
  CHECK_EQ_U16(d.tx, expected_tx_for_step(STEP_CFG6_READ), "cfg6 mismatch preserves first failing tx");
  CHECK_EQ_U16(d.rx, 0x0011u, "cfg6 mismatch preserves first failing rx");
  CHECK(d.transfer_ok, "cfg6 mismatch keeps transfer_ok");
  CHECK_EQ_U16(d.expected, 0x10u, "cfg6 expected readback remains 0x10");
  CHECK_EQ_U8(d.relock_attempted, 1u, "cfg6 mismatch triggers cleanup");
  CHECK_EQ_U8(d.relock_ok, 1u, "cfg6 mismatch cleanup succeeds");
  CHECK_EQ_U32(bench_hw_fake_spi3_mismatch_count(), 0u, "cfg6 mismatch no tx mismatch");
  assert_tx_count(expected_tx_for_step(STEP_CFG6_WRITE), 1u, "cfg6 write issued exactly once");
  assert_tx_count(expected_tx_for_step(STEP_CFG6_READ), 1u, "cfg6 read issued exactly once");
  assert_configure_outputs_frozen();
}

static void test_bench_hw_driver_configure_cleanup_write_timeout_has_no_read_retry(void) {
  const uint16_t pre_status[3] = {0x0808u, 0x0800u, 0x0800u};
  const uint16_t final_status[3] = {0x0808u, 0x0800u, 0x0800u};

  bench_hw_fake_reset();
  queue_configure_failure_plan(STEP_STATUS_PRE_2, true, pre_status[2], true, false, pre_status, final_status);
  bench_hw_fake_set_nfault_input(true);
  CHECK(!bench_hw_driver_configure(), "cleanup write timeout keeps configure failed");
  bench_drv_diag_t d = {0};
  bench_hw_driver_diagnostics(&d);
  if (d.reason != BENCH_DRV_REASON_TRANSPORT || d.stage != BENCH_DRV_STAGE_STATUS_PRE) { dump_diag_state("cleanup_timeout", &d); }
  CHECK_EQ_U8(d.stage, BENCH_DRV_STAGE_STATUS_PRE, "cleanup timeout test fails at STATUS_PRE");
  CHECK_EQ_U8(d.reason, BENCH_DRV_REASON_TRANSPORT, "cleanup timeout remains transport timeout at original failure stage");
  CHECK_EQ_U16(d.tx, expected_tx_for_step(STEP_STATUS_PRE_2), "cleanup timeout keeps original failing tx");
  CHECK(!d.transfer_ok, "original timeout fail marks transfer_ok false");
  CHECK_EQ_U8(d.relock_attempted, 1u, "original fail still triggers cleanup");
  CHECK_EQ_U8(d.relock_ok, 0u, "cleanup write timeout leaves relock_ok false");
  CHECK_EQ_U16(d.relock_rx, 0x0000u, "cleanup write timeout skips relock read");
  CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), bench_hw_fake_spi3_queue_pos(), "cleanup write timeout consumes queue exactly");
  assert_tx_count(expected_tx_for_step(STEP_LOCK_WRITE), 1u, "cleanup lock-write is attempted once");
  assert_tx_count(expected_tx_for_step(STEP_LOCK_READ), 0u, "cleanup lock-read is not retried after timeout");
  CHECK_EQ_U32(bench_hw_fake_spi3_mismatch_count(), 0u, "cleanup timeout no tx mismatch");
  assert_configure_outputs_frozen();
}

static void test_bench_hw_driver_configure_unlock_timeout_also_cleanup(void) {
  const uint16_t pre_status[3] = {0x0808u, 0x0800u, 0x0800u};
  const uint16_t final_status[3] = {0x0808u, 0x0800u, 0x0800u};

  bench_hw_fake_reset();
  queue_configure_failure_plan(STEP_UNLOCK_WRITE, true, 0x0000u, false, false, pre_status, final_status);
  bench_hw_fake_set_nfault_input(true);
  CHECK(!bench_hw_driver_configure(), "unlock RX timeout still keeps configure failure path");
  bench_drv_diag_t d = {0};
  bench_hw_driver_diagnostics(&d);
  if (d.stage != BENCH_DRV_STAGE_UNLOCK || d.reason != BENCH_DRV_REASON_TRANSPORT) { dump_diag_state("unlock_timeout", &d); }
  CHECK_EQ_U8(d.stage, BENCH_DRV_STAGE_UNLOCK, "unlock timeout fails at UNLOCK stage");
  CHECK_EQ_U8(d.reason, BENCH_DRV_REASON_TRANSPORT, "unlock timeout reason is transport");
  CHECK_EQ_U8(d.expected, 0x03u, "unlock timeout expected unlock value");
  CHECK(!d.transfer_ok, "unlock timeout transfer_ok false");
  CHECK_EQ_U16(d.tx, expected_tx_for_step(STEP_UNLOCK_WRITE), "unlock timeout keeps failing tx");
  CHECK_EQ_U16(d.rx, 0x0000u, "unlock timeout keeps failing rx");
  CHECK_EQ_U8(d.relock_attempted, 1u, "unlock timeout still cleanup attempt");
  CHECK_EQ_U8(d.relock_ok, 1u, "unlock timeout cleanup succeeds");
  assert_configure_outputs_frozen();
}

static void test_bench_hw_driver_snapshot_success_having_nonzero_faults_keeps_status_flags(void) {
  const uint16_t status[3] = {0x4141u, 0x4100u, 0x4120u};
  const uint32_t expected_norm = 0x200049u;
  const uint16_t status_mask = 0x07u;
  bench_drv_diag_t cache = {0};
  bench_drv_diag_t snapshot = {0};
  bench_drv_diag_t after = {0};

  snapshot_prepare_ready_cache(&cache);
  bench_hw_fake_set_nfault_input(false);
  queue_snapshot_plan(status);
  CHECK(bench_hw_driver_snapshot(&snapshot), "snapshot returns true when transport succeeds even if fault bits are present");
  CHECK_EQ_U8(snapshot.stage, BENCH_DRV_STAGE_STATUS_PRE, "snapshot transport failure-free path ends at STATUS_PRE");
  CHECK_EQ_U8(snapshot.reason, BENCH_DRV_REASON_STATUS_FLAGS, "snapshot marks status-only faults with STATUS_FLAGS");
  CHECK(snapshot.transfer_ok, "snapshot transport succeeded over all three reads");
  CHECK_EQ_U8(snapshot.relock_attempted, 0u, "snapshot does not attempt relock");
  CHECK_EQ_U8(snapshot.relock_ok, 0u, "snapshot relock remains not attempted");
  CHECK_EQ_U16(snapshot.expected, 0u, "snapshot transport success expects no explicit byte");
  CHECK_EQ_U16(snapshot.tx, expected_tx_for_step(STEP_STATUS_FINAL_2), "snapshot keeps final tx");
  CHECK_EQ_U16(snapshot.rx, status[2u], "snapshot keeps final received word");
  CHECK_EQ_U32(snapshot.status_read_mask, status_mask, "snapshot reads all three fault status words");
  CHECK_EQ_U32(snapshot.raw_status, snapshot_raw_from_frames(status, 3u), "snapshot preserves partial raw status bytes");
  CHECK_EQ_U32(snapshot.normalized_status, expected_norm, "snapshot normalizes to combined BUCK_OCP flags");
  CHECK_EQ_U16(snapshot.status_rx[0u], status[0u], "snapshot preserves status word 0");
  CHECK_EQ_U16(snapshot.status_rx[1u], status[1u], "snapshot preserves status word 1");
  CHECK_EQ_U16(snapshot.status_rx[2u], status[2u], "snapshot preserves status word 2");
  CHECK(snapshot.status_us[0u] < snapshot.status_us[1u], "snapshot status timestamps increase on frame 0->1");
  CHECK(snapshot.status_us[1u] < snapshot.status_us[2u], "snapshot status timestamps increase on frame 1->2");
  CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), 3u, "snapshot consumes exactly three status frames");
  assert_snapshot_reads_only_status_frames();
  assert_snapshot_no_wake_clear_relock_frames();
  bench_hw_driver_diagnostics(&after);
  CHECK(memcmp(&after, &cache, sizeof(after)) == 0, "snapshot success does not mutate configure cache");
}

static void test_bench_hw_driver_snapshot_status2_reserved_bit7_masked(void) {
  const uint16_t status[3] = {0x0808u, 0x0800u, 0x0880u};
  bench_drv_diag_t cache = {0};
  bench_drv_diag_t snapshot = {0};
  bench_drv_diag_t after = {0};

  snapshot_prepare_ready_cache(&cache);
  queue_snapshot_plan(status);
  CHECK(bench_hw_driver_snapshot(&snapshot), "snapshot transport succeeds with STAT2 reserved bit7 and a healthy summary");
  CHECK_EQ_U8(snapshot.stage, BENCH_DRV_STAGE_STATUS_PRE, "snapshot status2 0x0880 path stays at STATUS_PRE");
  CHECK_EQ_U8(snapshot.reason, BENCH_DRV_REASON_NONE, "snapshot status2 0x0880 is masked as normal status and reports no fault reason");
  CHECK(snapshot.transfer_ok, "snapshot status2 0x0880 transport succeeds");
  CHECK_EQ_U8(snapshot.relock_attempted, 0u, "snapshot status2 0x0880 does not relock");
  CHECK_EQ_U8(snapshot.relock_ok, 0u, "snapshot status2 0x0880 no relock");
  CHECK_EQ_U16(snapshot.expected, 0u, "snapshot status2 0x0880 expected byte is zero");
  CHECK_EQ_U8(snapshot.status_read_mask, 0x07u, "snapshot status2 0x80 reads all three status frames");
  CHECK_EQ_U16(snapshot.status_rx[0u], status[0u], "snapshot status2 0x0880 preserves status word 0");
  CHECK_EQ_U16(snapshot.status_rx[1u], status[1u], "snapshot status2 0x0880 preserves status word 1");
  CHECK_EQ_U16(snapshot.status_rx[2u], status[2u], "snapshot status2 0x0880 preserves status word 2");
  CHECK_EQ_U32(snapshot.raw_status, snapshot_raw_from_frames(status, 3u), "snapshot status2 0x0880 preserves raw status bytes");
  CHECK(snapshot.status_us[0u] < snapshot.status_us[1u], "snapshot status2 0x0880 status timestamps increase on frame 0->1");
  CHECK(snapshot.status_us[1u] < snapshot.status_us[2u], "snapshot status2 0x0880 status timestamps increase on frame 1->2");
  CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), 3u, "snapshot status2 0x0880 consumes exactly three status frames");
  assert_snapshot_reads_only_status_frames();
  assert_snapshot_no_wake_clear_relock_frames();
  bench_hw_driver_diagnostics(&after);
  CHECK(memcmp(&after, &cache, sizeof(after)) == 0, "snapshot status2 0x0880 does not mutate configure cache");
}

static void test_bench_hw_driver_snapshot_status2_defined_detail_bits_reject_as_status_flags(void) {
  bench_drv_diag_t cache = {0};
  bench_drv_diag_t snapshot = {0};
  bench_drv_diag_t after = {0};

  for (uint8_t variant = 0u; variant < 14u; ++variant) {
    const uint8_t bit = variant % 7u;
    const uint16_t reserved = variant >= 7u ? 0x80u : 0u;
    const uint16_t status[3] = {0x0808u, 0x0800u, (uint16_t)(0x0800u | reserved | (uint16_t)(1u << bit))};
    snapshot_prepare_ready_cache(&cache);
    queue_snapshot_plan(status);
    CHECK(bench_hw_driver_snapshot(&snapshot), "snapshot transport succeeds with a defined STAT2 detail fault");
    CHECK_EQ_U8(snapshot.stage, BENCH_DRV_STAGE_STATUS_PRE, "snapshot defined STAT2 detail-bit stays at STATUS_PRE");
    CHECK_EQ_U8(snapshot.reason, BENCH_DRV_REASON_STATUS_FLAGS, "snapshot defined STAT2 detail-bit maps to STATUS_FLAGS");
    CHECK(snapshot.transfer_ok, "snapshot with defined STAT2 detail-bit transport succeeds");
    CHECK_EQ_U32(snapshot.raw_status, snapshot_raw_from_frames(status, 3u), "snapshot defined STAT2 detail-bit preserves raw status bytes");
    CHECK_EQ_U16(snapshot.status_rx[2], status[2], "snapshot defined STAT2 detail-bit preserves raw status2");
    CHECK_EQ_U32(snapshot.status_read_mask, 0x07u, "snapshot defined STAT2 detail-bit reads all three status frames");
    CHECK(snapshot.status_us[0u] < snapshot.status_us[1u], "snapshot defined STAT2 detail-bit status timestamps increase on frame 0->1");
    CHECK(snapshot.status_us[1u] < snapshot.status_us[2u], "snapshot defined STAT2 detail-bit status timestamps increase on frame 1->2");
    CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), 3u, "snapshot defined STAT2 detail-bit consumes exactly three status frames");
    assert_snapshot_reads_only_status_frames();
    assert_snapshot_no_wake_clear_relock_frames();
    bench_hw_driver_diagnostics(&after);
    CHECK(memcmp(&after, &cache, sizeof(after)) == 0, "snapshot defined STAT2 detail-bit does not mutate configure cache");
  }
}

static void test_bench_hw_driver_snapshot_success_zero_faults_reports_reason_none(void) {
  const uint16_t status[3] = {0x0808u, 0x0800u, 0x0800u};
  bench_drv_diag_t cache = {0};
  bench_drv_diag_t snapshot = {0};
  bench_drv_diag_t after = {0};

  snapshot_prepare_ready_cache(&cache);
  queue_snapshot_plan(status);
  CHECK(bench_hw_driver_snapshot(&snapshot), "snapshot returns true when transport succeeds with no fault flags");
  CHECK_EQ_U8(snapshot.stage, BENCH_DRV_STAGE_STATUS_PRE, "snapshot no-fault path ends at STATUS_PRE");
  CHECK_EQ_U8(snapshot.reason, BENCH_DRV_REASON_NONE, "snapshot with zero normalized status flags reports NONE");
  CHECK(snapshot.transfer_ok, "snapshot transport succeeded over all three reads");
  CHECK_EQ_U8(snapshot.relock_attempted, 0u, "snapshot does not attempt relock");
  CHECK_EQ_U8(snapshot.relock_ok, 0u, "snapshot relock remains not attempted");
  CHECK_EQ_U16(snapshot.expected, 0u, "snapshot transport success expects no explicit byte");
  CHECK_EQ_U16(snapshot.tx, expected_tx_for_step(STEP_STATUS_FINAL_2), "snapshot keeps final tx");
  CHECK_EQ_U16(snapshot.rx, status[2u], "snapshot keeps final received word");
  CHECK_EQ_U32(snapshot.status_read_mask, 0x07u, "snapshot reads all three fault status words");
  CHECK_EQ_U32(snapshot.raw_status, snapshot_raw_from_frames(status, 3u), "snapshot preserves final raw status bytes");
  CHECK_EQ_U32(snapshot.normalized_status, 0u, "snapshot normalizes to zero");
  CHECK_EQ_U16(snapshot.status_rx[0u], status[0u], "snapshot preserves status word 0");
  CHECK_EQ_U16(snapshot.status_rx[1u], status[1u], "snapshot preserves status word 1");
  CHECK_EQ_U16(snapshot.status_rx[2u], status[2u], "snapshot preserves status word 2");
  CHECK(snapshot.status_us[0u] < snapshot.status_us[1u], "snapshot status timestamps increase on frame 0->1");
  CHECK(snapshot.status_us[1u] < snapshot.status_us[2u], "snapshot status timestamps increase on frame 1->2");
  CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), 3u, "snapshot consumes exactly three status frames");
  assert_snapshot_reads_only_status_frames();
  assert_snapshot_no_wake_clear_relock_frames();
  bench_hw_driver_diagnostics(&after);
  CHECK(memcmp(&after, &cache, sizeof(after)) == 0, "snapshot success does not mutate configure cache");
}

static void test_bench_hw_driver_snapshot_transport_timeout_for_each_status_read(void) {
  const uint16_t status[3] = {0x4141u, 0x4100u, 0x4120u};
  for (uint8_t status_step = 0u; status_step < 3u; ++status_step) {
    const uint8_t fail_step = (uint8_t)(STEP_STATUS_FINAL_0 + status_step);
    bench_drv_diag_t cache = {0};
    bench_drv_diag_t snapshot = {0};
    bench_drv_diag_t after = {0};

    snapshot_prepare_ready_cache(&cache);
    queue_snapshot_failure_plan(fail_step, true, 0u, status);
    CHECK(!bench_hw_driver_snapshot(&snapshot), "snapshot transport timeout returns false");
    CHECK_EQ_U8(snapshot.stage, BENCH_DRV_STAGE_STATUS_PRE, "snapshot timeout keeps STATUS_PRE stage");
    CHECK_EQ_U8(snapshot.reason, BENCH_DRV_REASON_TRANSPORT, "snapshot timeout maps to transport reason");
    CHECK(!snapshot.transfer_ok, "snapshot transport timeout marks transfer_ok false");
    CHECK_EQ_U8(snapshot.relock_attempted, 0u, "snapshot timeout never relocks");
    CHECK_EQ_U8(snapshot.relock_ok, 0u, "snapshot timeout keeps relock_ok false");
    CHECK_EQ_U8(snapshot.expected, 0u, "snapshot timeout expected write value is 0");
    CHECK_EQ_U16(snapshot.tx, expected_tx_for_step(fail_step), "snapshot timeout preserves failing tx");
    CHECK_EQ_U16(snapshot.rx, 0u, "snapshot timeout preserves failing rx as zero");
    CHECK_EQ_U32(snapshot.status_read_mask, snapshot_mask_after_frames(status_step), "snapshot timeout keeps already-read status mask");
    CHECK_EQ_U32(snapshot.raw_status, snapshot_raw_from_frames(status, status_step), "snapshot timeout preserves raw status bytes");
    CHECK_EQ_U32(snapshot.normalized_status, snapshot_norm_from_frames(status, status_step), "snapshot timeout keeps normalized prefix");
    if (status_step == 0u) {
      CHECK(!snapshot.status_us[0u], "timeout before first status read leaves all status timestamps zero");
      CHECK(!snapshot.status_us[1u], "timeout before first status read leaves status frame 1 timestamp zero");
      CHECK(!snapshot.status_us[2u], "timeout before first status read leaves status frame 2 timestamp zero");
    } else if (status_step == 1u) {
      CHECK(snapshot.status_us[0u] != 0u, "status frame 0 timestamp is captured before frame 1 timeout");
      CHECK(!snapshot.status_us[1u], "timeout at status frame 1 leaves frame 1 timestamp zero");
      CHECK(!snapshot.status_us[2u], "timeout at status frame 1 leaves frame 2 timestamp zero");
      CHECK(snapshot.status_rx[0u] == status[0u], "status frame 0 readback is retained");
      CHECK(snapshot.status_rx[1u] == 0u, "timed-out frame 1 readback remains zero");
    } else {
      CHECK(snapshot.status_us[0u] < snapshot.status_us[1u], "timeout after frame 1 still keeps frame timestamps ordered");
      CHECK(snapshot.status_us[1u] != 0u, "timeout at status frame 2 keeps frame 1 timestamp");
      CHECK(snapshot.status_rx[0u] == status[0u], "status frame 0 readback is retained");
      CHECK(snapshot.status_rx[1u] == status[1u], "status frame 1 readback is retained");
    }
    CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), (uint32_t)(status_step + 1u), "snapshot timeout transfers exact queued reads");
    assert_snapshot_no_wake_clear_relock_frames();
    bench_hw_driver_diagnostics(&after);
    CHECK(memcmp(&after, &cache, sizeof(after)) == 0, "snapshot timeout does not alter configure cache");
  }
}

static void test_bench_hw_driver_snapshot_fails_on_preconditions_with_no_side_effects(void) {
  bench_drv_diag_t cache = {0};
  bench_drv_diag_t snapshot = {0};
  bench_drv_diag_t after = {0};
  const uint16_t guard_channels[] = {
    LL_TIM_CHANNEL_CH1,
    LL_TIM_CHANNEL_CH1N,
    LL_TIM_CHANNEL_CH2,
    LL_TIM_CHANNEL_CH2N,
    LL_TIM_CHANNEL_CH3,
    LL_TIM_CHANNEL_CH3N,
  };

  snapshot_prepare_ready_cache(&cache);
  bench_hw_fake_spi_queue_clear_spi3();
  bench_hw_fake_set_button_input(true);
  CHECK(!bench_hw_driver_snapshot(&snapshot), "snapshot rejects when button is pressed");
  CHECK(memcmp(&snapshot, &(bench_drv_diag_t){0}, sizeof(snapshot)) == 0, "snapshot precondition failure leaves out zero");
  CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), 0u, "snapshot precondition failure performs no SPI writes");
  CHECK_EQ_U32(bench_hw_fake_spi3_queue_pos(), 0u, "snapshot precondition failure does not consume queue entries");
  CHECK_EQ_U32(bench_hw_fake_spi3_queue_len(), 0u, "snapshot precondition failure does not create queue work");
  bench_hw_driver_diagnostics(&after);
  CHECK(memcmp(&after, &cache, sizeof(after)) == 0, "snapshot precondition failure preserves configure cache");

  snapshot_prepare_ready_cache(&cache);
  bench_hw_fake_spi_queue_clear_spi3();
  LL_TIM_EnableAllOutputs(TIM1);
  CHECK(!bench_hw_driver_snapshot(&snapshot), "snapshot rejects when MOE is enabled");
  CHECK(memcmp(&snapshot, &(bench_drv_diag_t){0}, sizeof(snapshot)) == 0, "snapshot precondition MOE failure leaves out zero");
  CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), 0u, "snapshot precondition MOE failure performs no SPI writes");
  CHECK_EQ_U32(bench_hw_fake_spi3_queue_pos(), 0u, "snapshot precondition MOE failure does not consume queue entries");
  CHECK_EQ_U32(bench_hw_fake_spi3_queue_len(), 0u, "snapshot precondition MOE failure does not create queue work");
  bench_hw_driver_diagnostics(&after);
  CHECK(memcmp(&after, &cache, sizeof(after)) == 0, "snapshot precondition MOE failure preserves configure cache");

  for (uint8_t i = 0u; i < (uint8_t)(sizeof(guard_channels) / sizeof(guard_channels[0])); ++i) {
  snapshot_prepare_ready_cache(&cache);
    bench_hw_fake_spi_queue_clear_spi3();
    LL_TIM_CC_EnableChannel(TIM1, guard_channels[i]);
    CHECK(!bench_hw_driver_snapshot(&snapshot), "snapshot rejects when PWM channel is still enabled");
    CHECK(memcmp(&snapshot, &(bench_drv_diag_t){0}, sizeof(snapshot)) == 0, "snapshot precondition channel failure leaves out zero");
    CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), 0u, "snapshot precondition channel failure performs no SPI writes");
    CHECK_EQ_U32(bench_hw_fake_spi3_queue_pos(), 0u, "snapshot precondition channel failure does not consume queue");
    CHECK_EQ_U32(bench_hw_fake_spi3_queue_len(), 0u, "snapshot precondition channel failure does not create queue work");
    bench_hw_driver_diagnostics(&after);
    CHECK(memcmp(&after, &cache, sizeof(after)) == 0, "snapshot precondition channel failure preserves configure cache");
  }

  snapshot_prepare_ready_cache(&cache);
  bench_hw_fake_spi_queue_clear_spi3();
  LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin);
  CHECK(!bench_hw_driver_snapshot(&snapshot), "snapshot rejects when DRVOFF is deasserted");
  CHECK(memcmp(&snapshot, &(bench_drv_diag_t){0}, sizeof(snapshot)) == 0, "snapshot precondition DRVOFF failure leaves out zero");
  CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), 0u, "snapshot precondition DRVOFF failure performs no SPI writes");
  CHECK_EQ_U32(bench_hw_fake_spi3_queue_pos(), 0u, "snapshot precondition DRVOFF failure does not consume queue entries");
  CHECK_EQ_U32(bench_hw_fake_spi3_queue_len(), 0u, "snapshot precondition DRVOFF failure does not create queue work");
  bench_hw_driver_diagnostics(&after);
  CHECK(memcmp(&after, &cache, sizeof(after)) == 0, "snapshot precondition DRVOFF failure preserves configure cache");

  snapshot_prepare_ready_cache(&cache);
  bench_hw_fake_spi_queue_clear_spi3();
  LL_GPIO_ResetOutputPin(DRV_NSLEEP_GPIO_Port, DRV_NSLEEP_Pin);
  CHECK(!bench_hw_driver_snapshot(&snapshot), "snapshot rejects when nSLEEP is low");
  CHECK(memcmp(&snapshot, &(bench_drv_diag_t){0}, sizeof(snapshot)) == 0, "snapshot precondition nSLEEP failure leaves out zero");
  CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), 0u, "snapshot precondition nSLEEP failure performs no SPI writes");
  CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count_all(), 0u, "snapshot precondition nSLEEP failure performs no SPI transactions");
  CHECK_EQ_U32(bench_hw_fake_spi3_queue_pos(), 0u, "snapshot precondition nSLEEP failure does not consume queue entries");
  CHECK_EQ_U32(bench_hw_fake_spi3_queue_len(), 0u, "snapshot precondition nSLEEP failure does not create queue work");
  bench_hw_driver_diagnostics(&after);
  CHECK(memcmp(&after, &cache, sizeof(after)) == 0, "snapshot precondition nSLEEP failure preserves configure cache");
}

static void test_bench_hw_idle_keeps_drvoff_and_lowers_parked_pins_once(void) {
  bench_hw_fake_reset();
  LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin);
  LL_TIM_EnableAllOutputs(TIM1);
  LL_TIM_CC_EnableChannel(TIM1, kTim1PwmChannels);
  LL_TIM_EnableIT_BRK(TIM1);

  const uint32_t mode_calls_before = bench_hw_fake_gpio_setpinmode_calls();
  const uint32_t multi_calls_before = bench_hw_fake_gpio_setpinmode_multi_calls();

  bench_hw_idle();
  CHECK(!bench_hw_fake_tim1_moe(), "idle holds MOE low");
  CHECK_EQ_U8(bench_hw_fake_tim1_channel_mask(), 0u, "idle disables all PWM channels");
  CHECK(!bench_hw_fake_gpio_is_set(DRV_OFF_GPIO_Port, DRV_OFF_Pin), "idle does not assert DRVOFF");
  CHECK_EQ_U32(bench_hw_fake_tim1_disable_all_outputs_count(), 1u, "idle disables TIM1 all outputs");
  CHECK_EQ_U32(bench_hw_fake_tim1_cc_disable_count(), 1u, "idle disables all PWM channels");
  CHECK_EQ_U32(bench_hw_fake_tim1_brk_disable_count(), 1u, "idle disables TIM1 BRK interrupt");
  assert_pwm_channels_low_output_mode_output();

  const uint32_t mode_calls_after_first = bench_hw_fake_gpio_setpinmode_calls();
  const uint32_t multi_calls_after_first = bench_hw_fake_gpio_setpinmode_multi_calls();
  CHECK_EQ_U32(mode_calls_after_first, mode_calls_before + 6u,
               "first idle reconfigures all six pins exactly once each");
  CHECK_EQ_U32(multi_calls_after_first, multi_calls_before,
               "first idle uses single-pin mode update calls");

  bench_hw_idle();
  CHECK_EQ_U32(bench_hw_fake_gpio_setpinmode_calls(), mode_calls_after_first,
               "second idle does not reprogram pin modes when already parked");
  CHECK_EQ_U32(bench_hw_fake_gpio_setpinmode_multi_calls(), multi_calls_after_first,
               "second idle keeps single-pin mode call style");
}

static void test_bench_hw_off_drvoff_then_idle_keeps_drvoff_asserted(void) {
  bench_hw_fake_reset();
  LL_TIM_EnableAllOutputs(TIM1);
  LL_TIM_CC_EnableChannel(TIM1, kTim1PwmChannels);

  bench_hw_off();
  CHECK(bench_hw_fake_gpio_is_set(DRV_OFF_GPIO_Port, DRV_OFF_Pin), "off asserts DRVOFF high");
  CHECK_EQ_U8(bench_hw_fake_drv_off_set_count(), 1u, "off raises DRVOFF exactly once");
  CHECK_EQ_U8(bench_hw_fake_drv_off_reset_count(), 0u, "off does not deassert DRVOFF");
  CHECK(!bench_hw_fake_tim1_moe(), "off parks outputs with MOE low");
  CHECK_EQ_U8(bench_hw_fake_tim1_channel_mask(), 0u, "off disables PWM channels");
  CHECK_EQ_U32(bench_hw_fake_tim1_brk_disable_count(), 1u, "off disables BRK through idle path");
  assert_pwm_channels_low_output_mode_output();

  const uint32_t set_count = bench_hw_fake_drv_off_set_count();
  bench_hw_idle();
  CHECK_EQ_U8(bench_hw_fake_drv_off_set_count(), set_count, "idle after off does not reassert DRVOFF");
  CHECK(bench_hw_fake_gpio_is_set(DRV_OFF_GPIO_Port, DRV_OFF_Pin), "idle after off keeps DRVOFF high");
}

static void test_bench_hw_driver_configure_failure_keeps_drvoff_and_low_parked_pins(void) {
  const uint16_t pre_status[3] = {0x0808u, 0x0800u, 0x0800u};
  const uint16_t final_status[3] = {0x0808u, 0x0800u, 0x0800u};

  bench_hw_fake_reset();
  const size_t queued = queue_configure_failure_plan(STEP_CFG4_READ, false, 0x0041u,
                                                   false, false, pre_status, final_status);
  bench_hw_fake_set_nfault_input(true);
  CHECK(!bench_hw_driver_configure(), "configure failure path returns false");
  CHECK(bench_hw_fake_gpio_is_set(DRV_OFF_GPIO_Port, DRV_OFF_Pin), "configure failure hard-offs DRVOFF in cleanup");
  CHECK(!bench_hw_fake_tim1_moe(), "configure failure disables MOE");
  CHECK_EQ_U32(bench_hw_fake_spi3_queue_pos(), queued, "failure path consumes planned transfers");
  CHECK_EQ_U8(bench_hw_fake_tim1_channel_mask(), 0u, "configure failure leaves channels disabled");
  assert_pwm_channels_low_output_mode_output();
}

static void test_bench_hw_arm_requires_preconditions_and_enables_outputs_once(void) {
  bench_hw_fake_reset();
  bench_hw_fake_set_button_input(false);
  bench_hw_fake_set_nfault_input(true);
  LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin);
  CHECK(!bench_hw_arm(), "arm fails while PWM pins are not parked");
  CHECK_EQ_U32(bench_hw_fake_drv_off_set_count(), 1u, "arm failure hard-offs DRVOFF");
  CHECK(bench_hw_fake_gpio_is_set(DRV_OFF_GPIO_Port, DRV_OFF_Pin), "arm failure leaves DRVOFF asserted");
  CHECK(!bench_hw_fake_tim1_moe(), "arm failure does not enable MOE");
  CHECK_EQ_U8(bench_hw_fake_tim1_channel_mask(), 0u, "arm failure keeps PWM channels disabled");
  CHECK_EQ_U32(bench_hw_fake_tim1_cc_enable_count(), 0u, "arm failure does not enable PWM channels");

  bench_hw_fake_reset();
  bench_hw_fake_set_button_input(false);
  bench_hw_fake_set_nfault_input(true);
  bench_hw_idle();
  LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin);

  const uint32_t mode_calls_before = bench_hw_fake_gpio_setpinmode_calls();
  const uint32_t brk_enable_before = bench_hw_fake_tim1_brk_enable_count();
  const uint32_t cc_enable_before = bench_hw_fake_tim1_cc_enable_count();
  const uint32_t moe_enable_before = bench_hw_fake_tim1_enable_all_outputs_count();

  CHECK(bench_hw_arm(), "arm succeeds only when all preconditions are met");
  CHECK(bench_hw_fake_tim1_moe(), "arm enables TIM1 MOE");
  CHECK(pwm_armed_channel_mask_is_expected(), "arm enables all six PWM channels");
  CHECK_EQ_U32(bench_hw_fake_tim1_brk_enable_count(), brk_enable_before + 1u,
               "arm enables BRK interrupt exactly once");
  CHECK_EQ_U32(bench_hw_fake_tim1_cc_enable_count(), cc_enable_before + 1u,
               "arm enables all PWM channels exactly once");
  CHECK_EQ_U32(bench_hw_fake_tim1_enable_all_outputs_count(), moe_enable_before + 1u,
               "arm enables MOE exactly once");
  assert_pwm_channels_alt_mode_output();
  CHECK_EQ_U32(bench_hw_fake_gpio_setpinmode_calls(), mode_calls_before + 6u,
               "arm restores all six alternate pin modes exactly once");

  CHECK(!bench_hw_fake_gpio_is_set(DRV_OFF_GPIO_Port, DRV_OFF_Pin), "arm keeps DRVOFF deasserted");
  CHECK_EQ_U8(bench_hw_fake_drv_off_set_count(), 0u, "arm does not reassert DRVOFF");
}

static void arm_test_setup(void) {
  bench_hw_fake_reset();
  g_arm_diag = (bench_arm_diag_t){0};
  bench_hw_fake_set_button_input(false);
  bench_hw_fake_set_nfault_input(true);
  bench_hw_idle();
  LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin);
}

static void test_arm_waits_for_moe_readback_and_keeps_snapshot(void) {
  arm_test_setup();
  bench_hw_fake_set_tim1_moe_read_delay_us(2u);
  CHECK(bench_hw_arm(), "ARM_SYNC: delayed MOE readback succeeds after synchronization wait");
  CHECK_EQ_U32(bench_hw_fake_tim1_enable_all_outputs_count(), 1u,
               "ARM_SYNC: MOE is written only once, no retry loop");
  CHECK((uint32_t)(TIM2->CNT - g_bench_hw_fake_tim1_moe_set_time_us) >= 2u,
        "ARM_SYNC: elapsed timer time meets the configured readback delay");
  bench_arm_diag_t before, after;
  bench_hw_arm_diagnostics(&before);
  CHECK(before.attempt == 1u && before.stage == BENCH_ARM_STAGE_READY && !before.guard_flags,
        "ARM_SYNC: successful attempt recorded");
  CHECK((before.tim1_bdtr & LL_TIM_BDTR_MOE) && before.tim1_ccer == kTim1PwmChannels,
        "ARM_SYNC: ready snapshot captures enabled hardware");
  bench_hw_off();
  bench_hw_arm_diagnostics(&after);
  CHECK(memcmp(&before, &after, sizeof(before)) == 0, "ARM_SYNC: hard off preserves cached snapshot");
  CHECK(!TIM1->moe && !TIM1->CCER && bench_hw_fake_gpio_is_set(DRV_OFF_GPIO_Port, DRV_OFF_Pin),
        "ARM_SYNC: output state is safely off after snapshot retrieval");
  CHECK(!g_spi1_state.transfers && !g_spi3_state.transfers, "ARM_SYNC: arm diagnostics performs no SPI");
}

static bool g_arm_test_keep_moe;
static void arm_test_break_during_wait(void) {
  if (!g_bench_hw_fake_tim1_enable_all_outputs_count) { return; }
  TIM1->brk_flag = true;
  TIM1->SR |= LL_TIM_SR_BIF;
  bench_hw_fake_set_nfault_input(false);
  if (!g_arm_test_keep_moe) {
    TIM1->moe = false;
    TIM1->BDTR &= (uint32_t)~LL_TIM_BDTR_MOE;
  }
}

static void test_arm_readback_failure_and_break_still_hard_off(void) {
  for (unsigned int failure = 0u; failure < 3u; ++failure) {
    arm_test_setup();
    if (failure == 0u) { bench_hw_fake_set_tim1_moe_read_delay_us(UINT32_MAX); }
    else {
      g_arm_test_keep_moe = failure == 2u;
      g_bench_hw_fake_time_hook = arm_test_break_during_wait;
    }
    CHECK(!bench_hw_arm(), "ARM_SYNC: missing MOE or break during settling rejects arm");
    g_bench_hw_fake_time_hook = NULL;
    bench_arm_diag_t diag;
    bench_hw_arm_diagnostics(&diag);
    CHECK(diag.stage == BENCH_ARM_STAGE_ENABLE && !diag.guard_flags,
          "ARM_SYNC: enable failure distinguished from precondition failure");
    CHECK(diag.tim1_ccer == kTim1PwmChannels && !(diag.gpioc_odr & DRV_OFF_Pin),
          "ARM_SYNC: failed enable snapshot is captured before off cleanup");
    if (failure != 0u) {
      CHECK((diag.tim1_sr & LL_TIM_SR_BIF) && !(diag.gpiob_idr & LL_GPIO_PIN_12),
            "ARM_SYNC: snapshot preserves the break signal and flag");
      CHECK((TIM1->SR & LL_TIM_SR_BIF) != 0u, "ARM_SYNC: hard off never clears break evidence");
    }
    CHECK(!TIM1->moe && !TIM1->CCER && bench_hw_fake_gpio_is_set(DRV_OFF_GPIO_Port, DRV_OFF_Pin),
          "ARM_SYNC: failed enable always leaves hardware hard off");
    CHECK_EQ_U32(bench_hw_fake_tim1_enable_all_outputs_count(), 1u,
                 "ARM_SYNC: failed enable never retries MOE");
    assert_pwm_channels_low_output_mode_output();
  }
}

static void test_arm_precondition_snapshot_precedes_cleanup(void) {
  arm_test_setup();
  bench_hw_fake_set_nfault_input(false);
  CHECK(!bench_hw_arm(), "ARM_SYNC: nFAULT low rejects arm");
  bench_arm_diag_t diag;
  bench_hw_arm_diagnostics(&diag);
  CHECK(diag.stage == BENCH_ARM_STAGE_GUARD && diag.guard_flags == BENCH_ARM_GUARD_NFAULT,
        "ARM_SYNC: guard bitmap identifies nFAULT as the rejected condition");
  CHECK(!(diag.gpioc_odr & DRV_OFF_Pin) && (GPIOC->ODR & DRV_OFF_Pin),
        "ARM_SYNC: guard snapshot also precedes DRVOFF assertion");
  CHECK_EQ_U32(bench_hw_fake_tim1_enable_all_outputs_count(), 0u,
               "ARM_SYNC: guard rejection cannot write MOE");
  bench_hw_idle();
  LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin);
  bench_hw_fake_set_nfault_input(true);
  bench_hw_fake_set_button_input(false);
  CHECK(bench_hw_arm(), "ARM_SYNC: next explicitly requested valid attempt succeeds");
  bench_hw_arm_diagnostics(&diag);
  CHECK(diag.attempt == 2u && !diag.guard_flags && diag.stage == BENCH_ARM_STAGE_READY,
        "ARM_SYNC: a new attempt replaces the snapshot without stale guard flags");
  bench_hw_off();
}

static void test_bench_hw_driver_snapshot_null_pointer_is_rejected(void) {
  bench_drv_diag_t cache = {0};
  bench_drv_diag_t after = {0};

  snapshot_prepare_ready_cache(&cache);
  bench_hw_fake_spi_queue_clear_spi3();
  CHECK(!bench_hw_driver_snapshot(NULL), "snapshot NULL output pointer rejected");
  CHECK_EQ_U32(bench_hw_fake_spi3_transfer_count(), 0u, "snapshot NULL pointer does no SPI writes");
  CHECK_EQ_U32(bench_hw_fake_spi3_queue_pos(), 0u, "snapshot NULL pointer does not consume queue entries");
  bench_hw_driver_diagnostics(&after);
  CHECK(memcmp(&after, &cache, sizeof(after)) == 0, "snapshot NULL pointer preserves configure cache");
}

static void test_bench_hw_driver_status_preserves_configure_cache() {
  const uint16_t pre_status[3] = {0x0808u, 0x0800u, 0x0800u};
  const uint16_t final_status[3] = {0x0808u, 0x0800u, 0x0800u};
  bench_hw_fake_reset();
  bench_hw_fake_set_nfault_input(true);
  queue_configure_full_plan(pre_status, final_status);
  CHECK(bench_hw_driver_configure(), "runtime cache setup through configure");

  bench_drv_diag_t config_cache = {0};
  bench_hw_driver_diagnostics(&config_cache);
  const uint32_t moeen_before = bench_hw_fake_tim1_enable_all_outputs_count();
  const uint32_t ccen_before = bench_hw_fake_tim1_cc_enable_count();
  const uint32_t off_set_before = bench_hw_fake_drv_off_set_count();

  const size_t status_q = queue_status_only_plan(final_status, 0x0002u, 0x0010u);
  uint32_t status = UINT32_MAX;
  CHECK(bench_hw_driver_status(&status), "runtime status returns true when healthy");
  CHECK_EQ_U32(status, 0u, "runtime status normalizes 0");
  bench_drv_diag_t status_cache = {0};
  bench_hw_driver_diagnostics(&status_cache);
  CHECK(memcmp(&status_cache, &config_cache, sizeof(status_cache)) == 0, "runtime status success preserves configure diagnostics");
  CHECK_EQ_U32(bench_hw_fake_tim1_enable_all_outputs_count(), moeen_before, "runtime status success no MOE enable");
  CHECK_EQ_U32(bench_hw_fake_tim1_cc_enable_count(), ccen_before, "runtime status success no CC enable");
  CHECK_EQ_U32(bench_hw_fake_drv_off_set_count(), off_set_before, "runtime status success no extra DRVOFF set");
  CHECK_EQ_U32(bench_hw_fake_spi3_queue_pos(), status_q, "status success consumes exact status queue");

  bench_hw_fake_set_nfault_input(false);
  queue_status_only_plan(final_status, 0x0002u, 0x0010u);
  CHECK(!bench_hw_driver_status(&status), "runtime status fails when nFAULT low");
  CHECK_EQ_U32(status, 0u, "runtime status failure still exposes status value");
  memset(&status_cache, 0, sizeof(status_cache));
  bench_hw_driver_diagnostics(&status_cache);
  CHECK(memcmp(&status_cache, &config_cache, sizeof(status_cache)) == 0, "runtime status failure preserves configure diagnostics");
}

static void test_bench_hw_encoder_field_successful_pipeline_and_masks(void) {
  const uint16_t cmds[4] = {
    as5048a_read_command(AS5048A_REG_ANGLE),
    as5048a_read_command(AS5048A_REG_DIAG),
    as5048a_read_command(AS5048A_REG_MAG),
    0u
  };
  const uint16_t raw[4] = {
    as5048a_make_response(0x0123u),
    as5048a_make_response(0x0456u),
    as5048a_make_response(0x0100u),
    as5048a_make_response(0x0Au),
  };
  bench_hw_fake_reset();
  bench_hw_fake_spi_queue_clear_spi1();
  for (uint8_t i = 0u; i < 4u; ++i) {
    bench_hw_fake_spi1_push(cmds[i], raw[i], false);
  }

  bench_enc_field_diag_t out = {0};
  CHECK(bench_hw_encoder_field(&out), "encoder_field accepts fully healthy 4-frame read");
  CHECK_EQ_U32(bench_hw_fake_spi1_queue_len(), 4u, "encoder_field plans exactly 4 SPI1 frames");
  CHECK_EQ_U32(bench_hw_fake_spi1_queue_pos(), 4u, "encoder_field consumes exact queued SPI1 plan");
  CHECK_EQ_U32(bench_hw_fake_spi1_transfer_count(), 4u, "encoder_field issues exactly 4 SPI1 transfers");
  CHECK_EQ_U32(spi1_tx_count_in_transfers(cmds[0]), 1u, "encoder_field first frame is ANGLE request");
  CHECK_EQ_U32(spi1_tx_count_in_transfers(cmds[1]), 1u, "encoder_field second frame is DIAG request");
  CHECK_EQ_U32(spi1_tx_count_in_transfers(cmds[2]), 1u, "encoder_field third frame is MAG request");
  CHECK_EQ_U32(spi1_tx_count_in_transfers(cmds[3]), 1u, "encoder_field fourth frame is NOP");
  CHECK_EQ_U32(spi1_tx_find_first_in_transfers(cmds[0]), 0u, "encoder_field ANGLE request starts frame sequence");
  CHECK_EQ_U32(spi1_tx_find_first_in_transfers(cmds[1]), 1u, "encoder_field DIAG request follows ANGLE");
  CHECK_EQ_U32(spi1_tx_find_first_in_transfers(cmds[2]), 2u, "encoder_field MAG request follows DIAG");
  CHECK_EQ_U32(spi1_tx_find_first_in_transfers(cmds[3]), 3u, "encoder_field NOP completes frame sequence");

  CHECK_EQ_U16(out.transfer_mask, 0x0Fu, "encoder_field marks all frames transferred");
  CHECK_EQ_U16(out.valid_mask, 0x07u, "encoder_field marks all payload responses valid");
  CHECK_EQ_U16(out.raw[0], raw[0], "encoder_field stores pipelined reply 0");
  CHECK_EQ_U16(out.raw[1], raw[1], "encoder_field stores angle-pipe reply 1");
  CHECK_EQ_U16(out.raw[2], raw[2], "encoder_field stores diag-pipe reply 2");
  CHECK_EQ_U16(out.raw[3], raw[3], "encoder_field stores mag-pipe reply 3");
  CHECK_EQ_U16(out.angle, 0x0456u, "encoder_field decodes angle from pipe-advanced reply");
  CHECK_EQ_U16(out.diagnostics, 0x0100u, "encoder_field decodes diagnostics payload as-reported");
  CHECK_EQ_U16(out.magnitude, 0x000Au, "encoder_field decodes magnitude from last reply");
  CHECK_EQ_U16(out.spi.stage, 0u, "healthy pipeline leaves SPI error stage zero");
}

static void test_bench_hw_encoder_field_rejects_bad_payload_and_preserves_masks(void) {
  const uint16_t cmds[4] = {
    as5048a_read_command(AS5048A_REG_ANGLE),
    as5048a_read_command(AS5048A_REG_DIAG),
    as5048a_read_command(AS5048A_REG_MAG),
    0u
  };
  const uint16_t bad_diag = (uint16_t)(as5048a_make_response(0x00ABu) ^ 0x8000u);
  const uint16_t ef_mag = as5048a_make_response(0x0Au | 0x4000u);

  bench_hw_fake_reset();
  bench_hw_fake_spi_queue_clear_spi1();
  bench_hw_fake_spi1_push(cmds[0], as5048a_make_response(0x0123u), false);
  bench_hw_fake_spi1_push(cmds[1], as5048a_make_response(0x00FFu), false);
  bench_hw_fake_spi1_push(cmds[2], bad_diag, false);
  bench_hw_fake_spi1_push(cmds[3], ef_mag, false);

  bench_enc_field_diag_t out = {0};
  CHECK(!bench_hw_encoder_field(&out), "encoder_field rejects parity/EF contaminated payload");
  CHECK_EQ_U16(out.transfer_mask, 0x0Fu, "encoder_field still marks all frames transferred before decode");
  CHECK_EQ_U16(out.valid_mask, 0x01u, "parity/EF rejects only contaminated payloads and preserves previous angle");
  CHECK_EQ_U16(out.angle, 0x00FFu, "encoder_field decodes angle before bad diagnostics payload");
  CHECK_EQ_U16(out.diagnostics, 0x0000u, "bad payload does not advance diagnostics decode");
  CHECK_EQ_U16(out.magnitude, 0x0000u, "EF bit on magnitude payload prevents magnitude decode");
  CHECK_EQ_U16(out.spi.stage, 0u, "decode failure keeps transport stage clear");
}

static void test_bench_hw_encoder_field_timeout_stops_further_decodes_and_masks(void) {
  const uint16_t cmds[2] = {
    as5048a_read_command(AS5048A_REG_ANGLE),
    as5048a_read_command(AS5048A_REG_DIAG)
  };
  const uint16_t raw0 = as5048a_make_response(0x0000u);

  bench_hw_fake_reset();
  bench_hw_fake_set_spi1_rxne_delay_us(UINT32_MAX);
  bench_hw_fake_spi_queue_clear_spi1();
  bench_hw_fake_spi1_push(cmds[0], raw0, false);
  bench_hw_fake_spi1_push(cmds[1], 0x0000u, true);

  bench_enc_field_diag_t out = {0};
  CHECK(!bench_hw_encoder_field(&out), "encoder_field fails and stops on SPI timeout");
  CHECK_EQ_U16(out.transfer_mask, 0x01u, "timeout after second frame preserves prior transfer mask");
  CHECK_EQ_U16(out.valid_mask, 0x00u, "timeout before decode stage yields no payload-valid bits");
  CHECK_EQ_U32(g_spi_error.stage, 2u, "timeout path reports RX-timeout stage");
  CHECK(g_spi_error.elapsed_us >= 30u, "timeout duration meets SPI1 timeout floor");
  CHECK_EQ_U32(bench_hw_fake_spi1_transfer_count(), 2u, "timeout path transfers only queued successful frames");
}

static void test_bench_hw_encoder_field_null_pointer_is_rejected(void) {
  bench_hw_fake_reset();
  bench_hw_fake_spi_queue_clear_spi1();
  CHECK(!bench_hw_encoder_field(NULL), "encoder_field rejects null output pointer");
  CHECK_EQ_U32(bench_hw_fake_spi1_transfer_count(), 0u, "encoder_field null pointer does not use SPI");
  CHECK_EQ_U32(bench_hw_fake_spi1_queue_pos(), 0u, "encoder_field null pointer does not consume queue entries");
}

static void test_spi_word_spi3_delayed_rx_within_150us_succeeds(void) {
  const uint16_t tx = gl30_drv8316_make_frame(false, 4u, 0x68u);
  uint16_t rx = 0u;

  bench_hw_fake_reset();
  bench_hw_fake_set_spi3_rxne_delay_us(100u);
  bench_hw_fake_spi3_push(tx, 0x55AAu, true);
  LL_GPIO_SetOutputPin(DRV_CS_GPIO_Port, DRV_CS_Pin);
  CHECK(bench_hw_fake_gpio_is_set(DRV_CS_GPIO_Port, DRV_CS_Pin), "SPI3 timeout-path test starts with DRV CS deasserted (idle high)");

  CHECK(spi_word(SPI3, DRV_CS_GPIO_Port, DRV_CS_Pin, tx, &rx), "SPI3 delayed RXNE within 150us succeeds");
  CHECK_EQ_U16(rx, 0x55AAu, "SPI3 delayed receive returns queued word");
  CHECK(bench_hw_fake_gpio_is_set(DRV_CS_GPIO_Port, DRV_CS_Pin), "SPI3 delayed receive returns DRV CS deasserted/idle");
  CHECK(bench_hw_fake_spi3_transfer_count() == 1u, "SPI3 delayed receive performs one queued transfer");
  CHECK(bench_hw_fake_elapsed_since(bench_hw_fake_spi3_active_start_us()) >= 100u, "SPI3 delayed receive waits at least configured 100us");
  CHECK(bench_hw_fake_elapsed_since(bench_hw_fake_spi3_active_start_us()) < 150u, "SPI3 delayed receive returns before 150us timeout");
}

static void test_spi_word_spi3_timeout_without_rxne_releases_cs_around_150us(void) {
  const uint16_t tx = gl30_drv8316_make_frame(false, 4u, 0x68u);
  uint16_t rx = 0u;
  uint32_t elapsed_us;

  bench_hw_fake_reset();
  bench_hw_fake_set_spi3_rxne_delay_us(UINT32_MAX);
  bench_hw_fake_spi3_push(tx, 0x55AAu, true);
  LL_GPIO_SetOutputPin(DRV_CS_GPIO_Port, DRV_CS_Pin);
  CHECK(bench_hw_fake_gpio_is_set(DRV_CS_GPIO_Port, DRV_CS_Pin), "SPI3 no-RXNE path starts with DRV CS deasserted (idle high)");
  const uint32_t spi3_timeout_us = 150u;
  const uint32_t tim_step_us = bench_hw_fake_bench_time_step_us();

  CHECK(!spi_word(SPI3, DRV_CS_GPIO_Port, DRV_CS_Pin, tx, &rx), "SPI3 no RXNE path returns false");
  CHECK_EQ_U32(g_spi_error.stage, 2u, "SPI3 no-RXNE path records timeout stage 2");
  CHECK(bench_hw_fake_gpio_is_set(DRV_CS_GPIO_Port, DRV_CS_Pin), "SPI3 no RXNE path returns DRV CS deasserted/idle");
  CHECK_EQ_U16(rx, 0x0000u, "SPI3 no RXNE path leaves receive word zero");

  elapsed_us = g_spi_error.elapsed_us;
  CHECK(elapsed_us >= spi3_timeout_us + 1u, "SPI3 no RXNE timeout exceeds 150us");
  CHECK(elapsed_us <= spi3_timeout_us + (tim_step_us * 2u), "SPI3 no RXNE timeout is bounded by timeout-plus-step window");
  CHECK(elapsed_us >= 145u, "SPI3 no-RXNE timeout remains above 145us");
}

static void test_spi_word_spi1_timeout_without_rxne_releases_cs_with_30us(void) {
  const uint16_t tx = as5048a_read_command(AS5048A_REG_ANGLE);
  uint16_t rx = 0u;
  uint32_t elapsed_us;

  bench_hw_fake_reset();
  bench_hw_fake_set_spi1_rxne_delay_us(UINT32_MAX);
  bench_hw_fake_spi1_push(tx, 0x1234u, true);
  LL_GPIO_SetOutputPin(ENC_CS_GPIO_Port, ENC_CS_Pin);
  CHECK(bench_hw_fake_gpio_is_set(ENC_CS_GPIO_Port, ENC_CS_Pin), "SPI1 no-RXNE path starts with ENC CS deasserted (idle high)");
  const uint32_t spi1_timeout_us = 30u;
  const uint32_t tim_step_us = bench_hw_fake_bench_time_step_us();

  CHECK(!spi_word(SPI1, ENC_CS_GPIO_Port, ENC_CS_Pin, tx, &rx), "SPI1 no RXNE path returns false");
  CHECK_EQ_U32(g_spi_error.stage, 2u, "SPI1 no-RXNE path records timeout stage 2");
  CHECK(bench_hw_fake_gpio_is_set(ENC_CS_GPIO_Port, ENC_CS_Pin), "SPI1 no RXNE path returns ENC CS deasserted/idle");
  CHECK_EQ_U16(rx, 0x0000u, "SPI1 no RXNE path leaves receive word zero");

  elapsed_us = g_spi_error.elapsed_us;
  CHECK(elapsed_us >= spi1_timeout_us + 1u, "SPI1 no RXNE timeout exceeds 30us");
  CHECK(elapsed_us <= spi1_timeout_us + (tim_step_us * 2u), "SPI1 no RXNE timeout is bounded by timeout-plus-small-step window");
  CHECK(elapsed_us < 50u, "SPI1 no-RXNE timeout remains clearly below 50us");
}

static void trace_queue_word(bool read, uint8_t reg, uint8_t value, uint16_t rx) {
  bench_hw_fake_spi3_push(gl30_drv8316_make_frame(read, reg, value), rx, false);
}

static void trace_queue_status(uint8_t fault2, bool cleared) {
  trace_queue_word(true, 0u, 0u, cleared ? 0x0808u : 0u);
  trace_queue_word(true, 1u, 0u, cleared ? 0x0800u : 0u);
  trace_queue_word(true, 2u, 0u, (cleared ? 0x0800u : 0u) | fault2);
}

/* Independent wire transcript: point 0=wake, 1=unlock, 2..7=config writes. */
static void trace_queue_plan(int issue_point, uint8_t fault2, uint8_t after_clear) {
  static const uint8_t regs[][2] = {
    {4u, 0x68u}, {5u, 0x5fu}, {6u, 0x10u}, {7u, 0x02u}, {8u, 0x10u}, {12u, 0u}
  };
  uint8_t ctrl2 = 0x60u;
  trace_queue_status(issue_point == 0 ? fault2 : 0u, false);
  const bool defined_fault = (fault2 & 0x7Fu) != 0u;
  if (issue_point == 0 && defined_fault) { return; }
  trace_queue_word(false, 3u, 3u, 0u);
  trace_queue_word(true, 3u, 0u, 3u);
  trace_queue_status(issue_point <= 1 && issue_point >= 0 ? fault2 : 0u, false);
  if (issue_point < 0 || issue_point > 1 || !defined_fault) {
    for (unsigned int i = 0u; i < 6u; ++i) {
      trace_queue_word(false, regs[i][0], regs[i][1], 0u);
      trace_queue_word(true, regs[i][0], 0u, regs[i][1]);
      if (i == 0u) { ctrl2 = 0x68u; }
      trace_queue_status(issue_point == (int)i + 2 ? fault2 : 0u, false);
      if (issue_point == (int)i + 2 && defined_fault) { break; }
    }
  }
  if (issue_point < 0 || !defined_fault) {
    trace_queue_word(true, 4u, 0u, ctrl2);
    trace_queue_word(false, 4u, (uint8_t)(ctrl2 | 1u), ctrl2);
    trace_queue_word(true, 4u, 0u, ctrl2);
    trace_queue_status(after_clear, true);
  }
  trace_queue_word(false, 3u, 6u, 3u);
  trace_queue_word(true, 3u, 0u, 6u);
}

static void trace_reset(void) {
  bench_hw_fake_reset();
  g_trace_guard_enabled = true;
  g_trace_abort_after = SIZE_MAX;
  bench_hw_fake_set_nfault_input(true);
  bench_hw_off();
}

static void trace_assert_off(void) {
  CHECK(!TIM1->moe && !TIM1->CCER, "trace finishes with MOE and channels disabled");
  CHECK(LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin), "trace always returns DRVOFF high");
  CHECK_EQ_U32(bench_hw_fake_tim1_enable_all_outputs_count(), 0u, "trace never enables MOE");
  CHECK_EQ_U32(bench_hw_fake_tim1_cc_enable_count(), 0u, "trace never enables timer channels");
  CHECK(!(GPIOA->ODR & kTim1PwmPinsA) && !(GPIOB->ODR & kTim1PwmPinsB), "all six PWM pins remain low");
}

static void test_trace_bit7_at_every_point_and_clear_outcomes(void) {
  for (int first = -1; first < 8; ++first) {
    for (unsigned int persists = 0u; persists < 2u; ++persists) {
      trace_reset();
      trace_queue_plan(first, 0x80u, persists ? 0x80u : 0u);
      const size_t planned = g_spi3_state.queue_len;
      const bench_drv_diag_t saved = g_drv_diag;
      bench_drv_trace_t trace;
      CHECK(bench_hw_driver_trace(&trace), "trace transcript completes without granting readiness");
      CHECK_EQ_U32(g_spi3_state.transfers, planned, "trace consumes exact independent transcript");
      CHECK_EQ_U32(g_spi3_state.mismatch_count, 0u, "all trace frame values and ordering match");
      CHECK_EQ_U8(trace.clear_attempted, 1u, "single explicit clear attempted");
      CHECK_EQ_U32(tx_count_in_transfers(gl30_drv8316_make_frame(false, 4u, 0x69u)),
                   1u, "exactly one CLR_FLT write preserves current CTRL2");
      CHECK_EQ_U8(trace.first_issue, 0u, "first anomaly location stays none for bit7-only status");
      CHECK_EQ_U8(trace.count, 9u, "bit7 does not skip any of the six configuration checkpoints");
      CHECK_EQ_U8(trace.last.config_read_mask, 0x3Fu, "all six register readbacks are verified despite bit7");
      CHECK_EQ_U8(trace.last.relock_ok, 1u, "trace relocks control registers");
      CHECK_EQ_U8(trace.last.reason, BENCH_DRV_REASON_NONE, "bit7-only trace path reports no status flags after clear");
      CHECK_EQ_U16(trace.points[8].rx[2], persists ? 0x0880u : 0x0800u,
                   "last trace checkpoint preserves the unmasked reserved bit");
      if (first >= 0) {
        CHECK_EQ_U16(trace.points[first].rx[2], 0x0080u, "pre-clear reserved evidence never overwritten");
      }
      for (unsigned int i = 0u; i < trace.count; ++i) {
        CHECK_EQ_U8(trace.points[i].mask, 7u, "every complete checkpoint has three raw words");
        CHECK(trace.points[i].us[0] < trace.points[i].us[1] && trace.points[i].us[1] < trace.points[i].us[2],
              "checkpoint captures ordered per-frame timestamps");
        CHECK(!trace.points[i].state.moe && !trace.points[i].state.tim1_ccer, "checkpoint proves no PWM enables");
      }
      CHECK_EQ_U32(g_drv_diag.raw_status, saved.raw_status, "trace preserves PREPARE diagnostic cache");
      CHECK_EQ_U16(g_drv_diag.rx, saved.rx, "trace preserves prior failing reply");
      trace_assert_off();
    }
  }
}

static void test_trace_known_faults_do_not_clear_or_continue_config(void) {
  for (unsigned int variant = 0u; variant < 14u; ++variant) {
    const unsigned int bit = variant % 7u;
    const uint8_t fault2 = (uint8_t)((1u << bit) | (variant >= 7u ? 0x80u : 0u));
    for (int point = 0; point < 8; ++point) {
      trace_reset();
      trace_queue_plan(point, fault2, 0u);
      const size_t planned = g_spi3_state.queue_len;
      bench_drv_trace_t trace;
      CHECK(!bench_hw_driver_trace(&trace), "documented protection fault aborts diagnostic sequence");
      CHECK_EQ_U8(trace.clear_attempted, 0u, "documented fault never cleared by reserved-bit diagnostic");
      CHECK_EQ_U32(g_spi3_state.transfers, planned, "no extra configuration after first protection fault");
      CHECK_EQ_U32(g_spi3_state.mismatch_count, 0u, "known fault abort has exact transcript");
      trace_assert_off();
    }
  }
}

static void test_trace_guards_reject_without_spi_and_abort_midsequence(void) {
  CHECK(!bench_hw_driver_trace(NULL), "null trace destination rejected");
  for (unsigned int failure = 0u; failure < 10u; ++failure) {
    trace_reset();
    if (failure == 0u) { g_trace_guard_enabled = false; }
    if (failure == 1u) { bench_hw_fake_set_button_input(true); }
    if (failure == 2u) { TIM1->moe = true; }
    if (failure == 3u) { LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin); }
    if (failure >= 4u) { TIM1->enabled_channels = (uint8_t)(1u << (failure - 4u)); }
    bench_drv_trace_t trace;
    CHECK(!bench_hw_driver_trace(&trace), "unsafe initial state rejected");
    CHECK_EQ_U32(g_spi3_state.transfers, 0u, "entry guard sends no SPI");
    CHECK_EQ_U32(trace.last.reason, BENCH_DRV_REASON_GUARD, "entry guard is explicit");
  }
  for (size_t abort_after = 3u; abort_after < 44u; ++abort_after) {
    trace_reset();
    trace_queue_plan(-1, 0u, 0u);
    g_trace_abort_after = abort_after;
    bench_drv_trace_t trace;
    CHECK(!bench_hw_driver_trace(&trace), "health failure during trace aborts");
    CHECK(g_spi3_state.transfers <= abort_after + 2u, "at most unfinished read-only status group completes after guard drop");
    CHECK_EQ_U32(trace.last.reason, BENCH_DRV_REASON_GUARD, "midsequence abort retains guard reason");
    trace_assert_off();
  }
}

static void test_trace_timeout_each_frame_is_bounded_and_keeps_outputs_off(void) {
  trace_reset();
  trace_queue_plan(-1, 0u, 0u);
  bench_hw_mock_spi_plan_t good[64];
  const size_t count = g_spi3_state.queue_len;
  memcpy(good, g_spi3_state.queue, sizeof(good));
  for (size_t failed = 0u; failed < count; ++failed) {
    trace_reset();
    for (size_t i = 0u; i <= failed; ++i) {
      bench_hw_fake_spi3_push(good[i].expected_tx, good[i].rx_word, i == failed);
    }
    const bool during_lock = failed >= count - 2u;
    if (failed >= 3u && !during_lock) {
      trace_queue_word(false, 3u, 6u, 0u);
      trace_queue_word(true, 3u, 0u, 6u);
    }
    bench_drv_trace_t trace;
    CHECK(!bench_hw_driver_trace(&trace), "transport timeout cannot report completed sequence");
    CHECK(g_spi3_state.transfers <= failed + 3u, "timeout only permits one bounded cleanup relock");
    CHECK_EQ_U32(g_spi3_state.mismatch_count, 0u, "timeout cleanup has no configuration retry");
    CHECK(trace.clear_attempted <= 1u, "timeout cannot cause repeated CLR_FLT");
    trace_assert_off();
  }
}

static void test_trace_readback_mismatches_and_nfault_low_stop_without_retry(void) {
  /* Readback-bearing frame positions in the independent all-config plan. */
  static const size_t readbacks[] = {4u, 9u, 14u, 19u, 24u, 29u, 34u, 38u, 40u, 45u};
  trace_reset();
  trace_queue_plan(-1, 0u, 0u);
  bench_hw_mock_spi_plan_t good[64];
  memcpy(good, g_spi3_state.queue, sizeof(good));
  for (unsigned int r = 0u; r < sizeof(readbacks) / sizeof(readbacks[0]); ++r) {
    const size_t failed = readbacks[r];
    trace_reset();
    for (size_t i = 0u; i <= failed; ++i) {
      bench_hw_fake_spi3_push(good[i].expected_tx,
          i == failed ? (uint16_t)(good[i].rx_word ^ 0x40u) : good[i].rx_word, false);
    }
    if (failed < 44u) {
      trace_queue_word(false, 3u, 6u, 0u);
      trace_queue_word(true, 3u, 0u, 6u);
    }
    bench_drv_trace_t trace;
    CHECK(!bench_hw_driver_trace(&trace), "readback mismatch cannot complete trace");
    CHECK(!g_spi3_state.mismatch_count, "readback failure has only expected cleanup frames");
    CHECK(g_spi3_state.transfers <= failed + 3u, "no retry or later configuration after readback mismatch");
    CHECK(trace.clear_attempted <= 1u, "readback failure cannot repeat a clear write");
    if (failed < 40u) { CHECK(!trace.clear_attempted, "preclear readback failure prevents reset command"); }
    trace_assert_off();
  }
  trace_reset();
  bench_hw_fake_set_nfault_input(false);
  trace_queue_status(0x80u, false);
  bench_drv_trace_t trace;
  CHECK(!bench_hw_driver_trace(&trace), "low nFAULT prevents clear even with only reserved status bit");
  CHECK_EQ_U32(g_spi3_state.transfers, 3u, "low nFAULT only permits initial read group");
  CHECK(!trace.clear_attempted && trace.last.reason == BENCH_DRV_REASON_NFAULT,
        "low nFAULT is an explicit stop, not forced high or masked");
  trace_assert_off();
}

static void quiet_test_timer_tick(void) {
  const uint32_t phase = TIM2->CNT % 50u;
  TIM1->CNT = phase <= 25u ? phase * 160u : (50u - phase) * 160u;
}

static void test_spi3_reserves_adc_quiet_window_and_times_out_closed(void) {
  bench_hw_fake_reset();
  LL_GPIO_SetOutputPin(DRV_CS_GPIO_Port, DRV_CS_Pin);
  TIM2->CNT = 20u; quiet_test_timer_tick();
  g_bench_hw_fake_time_hook = quiet_test_timer_tick;
  bench_hw_fake_spi3_push(0x8000u, 0x0808u, false);
  uint16_t rx = 0u;
  CHECK(spi_word(SPI3, DRV_CS_GPIO_Port, DRV_CS_Pin, 0x8000u, &rx),
        "driver SPI still returns the exact word after reserving a quiet window");
  CHECK(g_bench_hw_fake_spi3_start_timer < 800u,
        "driver SPI starts only near PWM bottom, never around ADC peak trigger");
  CHECK(rx == 0x0808u && __get_PRIMASK() == 0u &&
        LL_GPIO_IsOutputPinSet(DRV_CS_GPIO_Port, DRV_CS_Pin),
        "quiet scheduling preserves payload, interrupts and CS release");

  bench_hw_fake_reset();
  LL_GPIO_SetOutputPin(DRV_CS_GPIO_Port, DRV_CS_Pin);
  TIM1->CNT = 2000u;
  bench_hw_fake_spi3_push(0x8000u, 0x0808u, false);
  const uint32_t start = TIM2->CNT;
  CHECK(!spi_word(SPI3, DRV_CS_GPIO_Port, DRV_CS_Pin, 0x8000u, &rx),
        "no quiet timer window rejects transaction instead of clocking through ADC");
  CHECK(g_spi_error.stage == 5u && g_spi3_state.transfers == 0u &&
        (uint32_t)(TIM2->CNT-start) < 175u && __get_PRIMASK() == 0u,
        "quiet wait is bounded, starts no transfer, reports stage and restores IRQ state");
  CHECK(LL_GPIO_IsOutputPinSet(DRV_CS_GPIO_Port, DRV_CS_Pin) &&
        !LL_TIM_IsEnabledAllOutputs(TIM1), "window timeout cannot enable bridge or leave CS asserted");

  bench_hw_fake_reset(); TIM1->CNT = 2000u;
  bench_hw_fake_spi1_push(0xffffu, 0x1234u, false);
  CHECK(spi_word(SPI1, ENC_CS_GPIO_Port, ENC_CS_Pin, 0xffffu, &rx) && rx == 0x1234u,
        "encoder SPI IRQ remains independent of foreground driver quiet scheduling");
}

static void quiet_test_stall_after_start(void) {
  if (g_spi3_state.transfers) { TIM1->CNT = 2000u; }
}

static void test_spi3_quiet_all_phases_release_timeout_and_wrap(void) {
  for (uint32_t phase = 0u; phase < 50u; ++phase) {
    bench_hw_fake_reset();
    TIM2->CNT = phase; quiet_test_timer_tick();
    g_bench_hw_fake_time_hook = quiet_test_timer_tick;
    /* Model the 6.4us hardware word as a conservative 7us receive delay. */
    bench_hw_fake_set_spi3_rxne_delay_us(7u);
    bench_hw_fake_spi3_push(0x8100u, 0x0808u, true);
    uint16_t rx = 0u;
    const uint32_t start = TIM2->CNT;
    CHECK(spi_word(SPI3, DRV_CS_GPIO_Port, DRV_CS_Pin, 0x8100u, &rx),
          "DRV transfer succeeds for every starting microsecond of PWM period");
    CHECK(g_bench_hw_fake_spi3_assert_timer < 400u &&
          g_bench_hw_fake_spi3_start_timer < 800u &&
          g_bench_hw_fake_spi3_release_timer < 400u,
          "both CS edges and autonomous SPI start avoid ADC peak");
    CHECK(g_bench_hw_fake_spi3_start_primask == 1u &&
          g_bench_hw_fake_irq_lock_max_us <= 3u && __get_PRIMASK() == 0u,
          "start is atomic; phase/word/completion waits do not hold IRQ mask");
    CHECK(rx == 0x0808u && g_spi3_state.transfers == 1u &&
          TIM2->CNT - start <= 150u && g_spi3_state.mismatch_count == 0u,
          "quiet transfer preserves data and bounded single-word contract");
  }

  bench_hw_fake_reset();
  LL_TIM_EnableAllOutputs(TIM1);
  LL_TIM_CC_EnableChannel(TIM1, PWM_CHANNELS);
  LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin);
  g_bench_hw_fake_time_hook = quiet_test_stall_after_start;
  bench_hw_fake_spi3_push(0x8100u, 0x0808u, false);
  uint16_t rx = 0u;
  CHECK(!spi_word(SPI3, DRV_CS_GPIO_Port, DRV_CS_Pin, 0x8100u, &rx) &&
        g_spi_error.stage == 5u && g_spi3_state.transfers == 1u,
        "timer stall after transmission fails rather than releasing normally near ADC");
  CHECK(!LL_TIM_IsEnabledAllOutputs(TIM1) && bench_hw_pwm_channels_disabled() &&
        LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin) &&
        LL_GPIO_IsOutputPinSet(DRV_CS_GPIO_Port, DRV_CS_Pin) && __get_PRIMASK() == 0u,
        "release timeout hard-disables bridge and releases CS without trapping IRQ mask");

  bench_hw_fake_reset(); TIM2->CNT = UINT32_MAX - 10u;
  g_bench_hw_fake_time_hook = quiet_test_timer_tick;
  bench_hw_fake_spi3_push(0x8100u, 0x0808u, false);
  CHECK(spi_word(SPI3, DRV_CS_GPIO_Port, DRV_CS_Pin, 0x8100u, &rx) && rx == 0x0808u,
        "quiet scheduling timeout arithmetic survives TIM2 wrap");

  bench_hw_fake_reset(); __disable_irq();
  bench_hw_fake_spi3_push(0x8100u, 0x0808u, false);
  CHECK(spi_word(SPI3, DRV_CS_GPIO_Port, DRV_CS_Pin, 0x8100u, &rx) && __get_PRIMASK() == 1u,
        "quiet edge critical sections restore the incoming IRQ state, not force-enable it");
  __set_PRIMASK(0u);
}

int main(void) {
  test_spi3_quiet_all_phases_release_timeout_and_wrap();
  test_spi3_reserves_adc_quiet_window_and_times_out_closed();
  test_arm_waits_for_moe_readback_and_keeps_snapshot();
  test_arm_readback_failure_and_break_still_hard_off();
  test_arm_precondition_snapshot_precedes_cleanup();
  test_trace_readback_mismatches_and_nfault_low_stop_without_retry();
  test_trace_bit7_at_every_point_and_clear_outcomes();
  test_trace_known_faults_do_not_clear_or_continue_config();
  test_trace_guards_reject_without_spi_and_abort_midsequence();
  test_trace_timeout_each_frame_is_bounded_and_keeps_outputs_off();
  test_bench_hw_break_test_restores_pb12_pull_for_all_initial_states();
  test_bench_hw_break_test_fails_when_down_pull_does_not_raise_brk();
  test_bench_hw_driver_configure_happy_path();
  test_bench_hw_driver_configure_isolated_npor_clear_once();
  test_bench_hw_driver_configure_final_npor_fails_without_clear();
  test_bench_hw_driver_configure_status_pre_fault_rejects_without_clear();
  test_bench_hw_driver_configure_status_pre_reserved_bit7_only_normalizes_and_clears_once();
  test_bench_hw_driver_configure_final_npor_with_bit7_rejects_without_clear();
  test_bench_hw_driver_configure_nfault_low_rejects_without_clear();
  test_bench_hw_driver_snapshot_status2_reserved_bit7_masked();
  test_bench_hw_driver_snapshot_status2_defined_detail_bits_reject_as_status_flags();
  test_bench_hw_driver_configure_cfg6_readback_mismatch_keeps_first_failure();
  test_bench_hw_driver_configure_cleanup_write_timeout_has_no_read_retry();
  test_bench_hw_driver_configure_unlock_timeout_also_cleanup();
  test_bench_hw_driver_snapshot_null_pointer_is_rejected();
  test_bench_hw_driver_snapshot_success_zero_faults_reports_reason_none();
  test_bench_hw_driver_snapshot_success_having_nonzero_faults_keeps_status_flags();
  test_bench_hw_driver_snapshot_transport_timeout_for_each_status_read();
  test_bench_hw_driver_snapshot_fails_on_preconditions_with_no_side_effects();
  test_bench_hw_encoder_field_successful_pipeline_and_masks();
  test_bench_hw_encoder_field_rejects_bad_payload_and_preserves_masks();
  test_bench_hw_encoder_field_timeout_stops_further_decodes_and_masks();
  test_bench_hw_encoder_field_null_pointer_is_rejected();
  test_bench_hw_idle_keeps_drvoff_and_lowers_parked_pins_once();
  test_bench_hw_off_drvoff_then_idle_keeps_drvoff_asserted();
  test_bench_hw_driver_configure_failure_keeps_drvoff_and_low_parked_pins();
  test_bench_hw_arm_requires_preconditions_and_enables_outputs_once();
  test_bench_hw_driver_status_preserves_configure_cache();
  test_spi_word_spi3_delayed_rx_within_150us_succeeds();
  test_spi_word_spi3_timeout_without_rxne_releases_cs_around_150us();
  test_spi_word_spi1_timeout_without_rxne_releases_cs_with_30us();

  if (g_tests_failed == 0) {
    printf("PASS: %d assertions\n", g_tests_run);
    return 0;
  }
  printf("FAIL: %d / %d assertions\n", g_tests_failed, g_tests_run);
  return 1;
}
