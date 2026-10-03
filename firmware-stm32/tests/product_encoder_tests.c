/* Exercise the real product encoder and AS5048A protocol code with fake LL I/O. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define GL30_BUILD_ONLY 0
#define __MAIN_H
#define SPI1 1u
#define ENC_CS_MCU_GPIO_Port 2u
#define ENC_CS_MCU_Pin 4u
#define LL_SPI_RX_FIFO_TH_HALF 1u

#include "../drivers/factory_encoder.h"
#include "../drivers/as5048a.h"
#include "../config/board_config.h"

typedef enum {
  FAKE_SPI_NORMAL = 0,
  FAKE_SPI_TXE_STUCK,
  FAKE_SPI_RXNE_STUCK,
  FAKE_SPI_BSY_STUCK,
  FAKE_SPI_RX_FIFO_STUCK,
} fake_spi_mode_t;

static uint32_t g_primask;
static uint64_t g_fake_us;
static uint64_t g_poll_started_us;
static unsigned g_clock_calls;
static unsigned g_delay_calls;
static unsigned g_tx_count;
static unsigned g_rx_count;
static unsigned g_cs_low_count;
static unsigned g_cs_high_count;
static unsigned g_tx_while_irq_masked;
static unsigned g_delay_while_irq_masked;
static unsigned g_time_while_irq_masked;
static bool g_spi_enabled;
static bool g_spi_ovr;
static bool g_spi_modf;
static bool g_spi_fre;
static bool g_rx_ready;
static bool g_cs_high;
static bool g_bad_cs_pin;
static uint16_t g_rx_word;
static uint16_t g_replies[3];
static uint16_t g_tx_words[4];
static fake_spi_mode_t g_spi_mode;
static unsigned g_jump_after_receive;
static uint32_t g_jump_offset_us;
static bool g_clock_jump_done;

static uint32_t __get_PRIMASK(void) { return g_primask; }
static void __disable_irq(void) { g_primask = 1u; }
static void __set_PRIMASK(uint32_t primask) { g_primask = primask; }

uint64_t gl30_timebase_now_us(void) {
  ++g_clock_calls;
  if (g_primask != 0u) {
    ++g_time_while_irq_masked;
  }
  ++g_fake_us;
  return g_fake_us;
}

void gl30_timebase_delay_us(uint32_t delay_us) {
  ++g_delay_calls;
  if (g_primask != 0u) {
    ++g_delay_while_irq_masked;
  }
  g_fake_us += delay_us;
}

static uint32_t fake_spi_enabled(uint32_t spi) {
  return (spi == SPI1 && g_spi_enabled) ? 1u : 0u;
}

static uint32_t fake_spi_ovr(uint32_t spi) {
  return (spi == SPI1 && g_spi_ovr) ? 1u : 0u;
}

static uint32_t fake_spi_modf(uint32_t spi) {
  return (spi == SPI1 && g_spi_modf) ? 1u : 0u;
}

static uint32_t fake_spi_fre(uint32_t spi) {
  return (spi == SPI1 && g_spi_fre) ? 1u : 0u;
}

static uint32_t fake_spi_txe(uint32_t spi) {
  return (spi == SPI1 && g_spi_mode != FAKE_SPI_TXE_STUCK) ? 1u : 0u;
}

static uint32_t fake_spi_rxne(uint32_t spi) {
  if (spi != SPI1 || g_spi_mode == FAKE_SPI_RXNE_STUCK) {
    return 0u;
  }
  return (g_rx_ready || g_spi_mode == FAKE_SPI_RX_FIFO_STUCK) ? 1u : 0u;
}

static uint32_t fake_spi_bsy(uint32_t spi) {
  return (spi == SPI1 && g_spi_mode == FAKE_SPI_BSY_STUCK) ? 1u : 0u;
}

static void fake_spi_enable(uint32_t spi) {
  if (spi == SPI1) {
    g_spi_enabled = true;
  }
}

static void fake_spi_set_rx_threshold(uint32_t spi, uint32_t threshold) {
  (void)spi;
  (void)threshold;
}

static void fake_spi_transmit(uint32_t spi, uint16_t word) {
  if (spi != SPI1) {
    g_bad_cs_pin = true;
  }
  if (g_primask != 0u) {
    ++g_tx_while_irq_masked;
  }
  if (g_tx_count < sizeof(g_tx_words) / sizeof(g_tx_words[0])) {
    g_tx_words[g_tx_count] = word;
  }
  if (g_tx_count < sizeof(g_replies) / sizeof(g_replies[0])) {
    g_rx_word = g_replies[g_tx_count];
  } else {
    g_rx_word = 0u;
  }
  ++g_tx_count;
  g_rx_ready = g_spi_mode != FAKE_SPI_RXNE_STUCK;
}

static uint16_t fake_spi_receive(uint32_t spi) {
  if (spi != SPI1) {
    g_bad_cs_pin = true;
  }
  const uint16_t word = g_rx_word;
  ++g_rx_count;
  if (g_spi_mode != FAKE_SPI_RX_FIFO_STUCK) {
    g_rx_ready = false;
  }
  if (!g_clock_jump_done && g_jump_after_receive != 0u &&
      g_rx_count == g_jump_after_receive) {
    const uint64_t jump_time = g_poll_started_us + g_jump_offset_us;
    if (jump_time > g_fake_us) {
      g_fake_us = jump_time;
    }
    g_clock_jump_done = true;
  }
  return word;
}

static void fake_gpio_set(uint32_t port, uint32_t pin) {
  if (port != ENC_CS_MCU_GPIO_Port || pin != ENC_CS_MCU_Pin) {
    g_bad_cs_pin = true;
  }
  g_cs_high = true;
  ++g_cs_high_count;
}

static void fake_gpio_reset(uint32_t port, uint32_t pin) {
  if (port != ENC_CS_MCU_GPIO_Port || pin != ENC_CS_MCU_Pin) {
    g_bad_cs_pin = true;
  }
  g_cs_high = false;
  ++g_cs_low_count;
}

#define LL_SPI_IsEnabled(spi) fake_spi_enabled((uint32_t)(spi))
#define LL_SPI_IsActiveFlag_OVR(spi) fake_spi_ovr((uint32_t)(spi))
#define LL_SPI_IsActiveFlag_MODF(spi) fake_spi_modf((uint32_t)(spi))
#define LL_SPI_IsActiveFlag_FRE(spi) fake_spi_fre((uint32_t)(spi))
#define LL_SPI_IsActiveFlag_TXE(spi) fake_spi_txe((uint32_t)(spi))
#define LL_SPI_IsActiveFlag_RXNE(spi) fake_spi_rxne((uint32_t)(spi))
#define LL_SPI_IsActiveFlag_BSY(spi) fake_spi_bsy((uint32_t)(spi))
#define LL_SPI_Enable(spi) fake_spi_enable((uint32_t)(spi))
#define LL_SPI_SetRxFIFOThreshold(spi, threshold) \
  fake_spi_set_rx_threshold((uint32_t)(spi), (uint32_t)(threshold))
#define LL_SPI_TransmitData16(spi, word) \
  fake_spi_transmit((uint32_t)(spi), (uint16_t)(word))
#define LL_SPI_ReceiveData16(spi) fake_spi_receive((uint32_t)(spi))
#define LL_GPIO_SetOutputPin(port, pin) \
  fake_gpio_set((uint32_t)(port), (uint32_t)(pin))
#define LL_GPIO_ResetOutputPin(port, pin) \
  fake_gpio_reset((uint32_t)(port), (uint32_t)(pin))

#include "../drivers/as5048a.c"
#include "../drivers/factory_encoder_as5048a.c"

static unsigned g_checks;
static unsigned g_failed;

#define CHECK(condition, message)                                      \
  do {                                                                  \
    ++g_checks;                                                         \
    if (!(condition)) {                                                 \
      ++g_failed;                                                       \
      fprintf(stderr, "[FAIL] %s\n", (message));                       \
    }                                                                   \
  } while (0)

static unsigned parity16(uint16_t value) {
  unsigned parity = 0u;
  while (value != 0u) {
    parity ^= (unsigned)(value & 1u);
    value >>= 1u;
  }
  return parity;
}

/* AS5048A replies are full 16-bit frames: EF, 14 data bits, and even parity. */
static uint16_t make_reply(uint16_t data, bool ef) {
  uint16_t reply = (uint16_t)(data & 0x3fffu);
  if (ef) {
    reply |= 0x4000u;
  }
  if (parity16(reply) != 0u) {
    reply |= 0x8000u;
  }
  return reply;
}

static uint16_t bad_parity(uint16_t reply) { return (uint16_t)(reply ^ 0x8000u); }

static void fake_reset(void) {
  g_primask = 0u;
  g_fake_us = 1000u;
  g_poll_started_us = 0u;
  g_clock_calls = 0u;
  g_delay_calls = 0u;
  g_tx_count = 0u;
  g_rx_count = 0u;
  g_cs_low_count = 0u;
  g_cs_high_count = 0u;
  g_tx_while_irq_masked = 0u;
  g_delay_while_irq_masked = 0u;
  g_time_while_irq_masked = 0u;
  g_spi_enabled = false;
  g_spi_ovr = false;
  g_spi_modf = false;
  g_spi_fre = false;
  g_rx_ready = false;
  g_cs_high = false;
  g_bad_cs_pin = false;
  g_rx_word = 0u;
  memset(g_replies, 0, sizeof(g_replies));
  memset(g_tx_words, 0, sizeof(g_tx_words));
  g_spi_mode = FAKE_SPI_NORMAL;
  g_jump_after_receive = 0u;
  g_jump_offset_us = 0u;
  g_clock_jump_done = false;
}

static void begin_case(void) {
  fake_reset();
  g_primask = 1u;
  gl30_factory_encoder_init();
  CHECK(g_primask == 1u, "encoder init restores the incoming PRIMASK value");
  g_primask = 0u;
}

static void set_replies(uint16_t stale, uint16_t angle, uint16_t diagnostics) {
  g_spi_mode = FAKE_SPI_NORMAL;
  g_spi_ovr = false;
  g_spi_modf = false;
  g_spi_fre = false;
  g_rx_ready = false;
  g_rx_word = 0u;
  g_tx_count = 0u;
  g_rx_count = 0u;
  g_cs_high = false;
  g_replies[0] = stale;
  g_replies[1] = angle;
  g_replies[2] = diagnostics;
  memset(g_tx_words, 0, sizeof(g_tx_words));
  g_jump_after_receive = 0u;
  g_jump_offset_us = 0u;
  g_clock_jump_done = false;
}

static void set_healthy_replies(uint16_t angle_data, uint16_t diagnostic_data) {
  /* Stale reply is intentionally bad in both parity and EF; it must be discarded. */
  const uint16_t stale = bad_parity(make_reply(0x1234u, true));
  set_replies(stale, make_reply(angle_data, false), make_reply(diagnostic_data, false));
}

static uint64_t poll_encoder(void) {
  g_poll_started_us = g_fake_us + 1u;
  gl30_factory_encoder_poll_4k();
  return g_poll_started_us;
}

static void snapshot_encoder(gl30_factory_encoder_sample_t *sample,
                             gl30_factory_encoder_diagnostics_t *diag) {
  gl30_factory_encoder_snapshot(sample);
  gl30_factory_encoder_diagnostics_snapshot(diag);
}

static void test_pipeline_snapshot_and_angle_boundaries(void) {
  begin_case();
  set_healthy_replies(0x1234u, 0x0101u);

  const unsigned transfers_before = g_tx_count;
  const unsigned receives_before = g_rx_count;
  const unsigned clock_before = g_clock_calls;
  const unsigned delays_before = g_delay_calls;
  gl30_factory_encoder_sample_t sample = {0};
  gl30_factory_encoder_diagnostics_t diag = {0};
  g_primask = 1u;
  snapshot_encoder(&sample, &diag);
  CHECK(g_primask == 1u, "snapshot APIs preserve an already-disabled PRIMASK");
  CHECK(g_tx_count == transfers_before && g_rx_count == receives_before,
        "sample and diagnostic snapshots do not access SPI");
  CHECK(g_clock_calls == clock_before && g_delay_calls == delays_before,
        "sample and diagnostic snapshots return without waiting on timebase");
  CHECK(sample.status == GL30_FACTORY_ENCODER_STATUS_INITIALIZING && !sample.valid,
        "init publishes an invalid initializing sample");
  CHECK(diag.latest_faults == 0u && diag.successful_samples == 0u &&
        diag.failed_samples == 0u,
        "init clears encoder diagnostics");
  g_primask = 0u;

  const uint16_t angle_reply = make_reply(0x1234u, false);
  const uint16_t diagnostic_reply = make_reply(0x0101u, false);
  const uint64_t started = poll_encoder();
  snapshot_encoder(&sample, &diag);
  CHECK(g_tx_count == 3u, "one poll clocks exactly three pipeline frames");
  CHECK(g_tx_words[0] == as5048a_read_command(AS5048A_REG_ANGLE),
        "pipeline frame 1 requests ANGLE");
  CHECK(g_tx_words[1] == as5048a_read_command(AS5048A_REG_DIAG),
        "pipeline frame 2 requests DIAG and receives ANGLE");
  CHECK(g_tx_words[2] == 0u, "pipeline frame 3 is NOP and receives DIAG");
  CHECK(sample.status == GL30_FACTORY_ENCODER_STATUS_READY && sample.valid,
        "valid angle and diagnostic replies publish a ready sample");
  CHECK(sample.sample_index == 1u && sample.timestamp_us == started,
        "success increments the index and timestamps the poll start");
  CHECK(fabsf(sample.angle_rad - (0x1234u * 6.2831853071795864769f / 16384.0f)) < 0.000002f,
        "angle data converts from 14-bit counts to radians");
  CHECK(diag.raw_angle == angle_reply && diag.raw_diagnostics == diagnostic_reply,
        "diagnostics retain the complete received words including parity bits");
  CHECK(diag.latest_faults == 0u && diag.successful_samples == 1u &&
        diag.failed_samples == 0u && diag.parity_errors == 0u &&
        diag.sensor_errors == 0u && diag.transport_errors == 0u &&
        diag.field_errors == 0u,
        "bad stale frame 0 does not contaminate the new sample or error counters");
  CHECK(diag.last_duration_us > 0u &&
        diag.last_duration_us < GL30_ENCODER_SPI_TIMEOUT_US,
        "three-frame sample completes within the shared deadline");
  CHECK(diag.last_duration_us == (uint32_t)(g_fake_us - started),
        "diagnostic duration measures the complete poll interval");
  CHECK(control_ready(&sample, g_fake_us, GL30_ENCODER_SPI_TIMEOUT_US),
        "fresh successful sample satisfies control_ready");
  CHECK(g_cs_high && !g_bad_cs_pin && g_cs_low_count == 3u,
        "each frame uses the encoder chip select and exits with CS high");
  CHECK(g_tx_while_irq_masked == 0u && g_delay_while_irq_masked == 0u &&
        g_time_while_irq_masked == 0u,
        "SPI transfers and timing do not run with interrupts masked");
  CHECK(g_primask == 0u, "poll restores the enabled PRIMASK value");

  set_healthy_replies(0u, 0x0100u);
  const uint64_t zero_started = poll_encoder();
  snapshot_encoder(&sample, &diag);
  CHECK(sample.valid && sample.sample_index == 2u && sample.timestamp_us == zero_started,
        "zero angle with OCF set is accepted");
  CHECK(sample.angle_rad == 0.0f, "zero encoder count maps to zero radians");
  CHECK(diag.raw_diagnostics == make_reply(0x0100u, false),
        "0x0100 diagnostic data is parity-encoded as a complete SPI reply");

  set_healthy_replies(16383u, 0x0101u);
  const uint64_t max_started = poll_encoder();
  snapshot_encoder(&sample, &diag);
  const float two_pi = 6.2831853071795864769f;
  CHECK(sample.valid && sample.sample_index == 3u && sample.timestamp_us == max_started,
        "maximum 14-bit angle with AGC value one is accepted");
  CHECK(sample.angle_rad > 0.0f && sample.angle_rad < two_pi,
        "maximum angle remains below 2 pi");
  CHECK(diag.successful_samples == 3u && diag.failed_samples == 0u,
        "the three healthy polls are counted once each");
}

static void test_response_parity_and_sensor_errors(void) {
  struct response_case {
    const char *name;
    bool corrupt_angle;
    bool corrupt_parity;
  };
  const struct response_case cases[] = {
      {"ANGLE response parity", true, true},
      {"ANGLE response EF", true, false},
      {"DIAG response parity", false, true},
      {"DIAG response EF", false, false},
  };

  for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    begin_case();
    const uint16_t angle = make_reply(0x2345u, cases[i].corrupt_angle && !cases[i].corrupt_parity);
    const uint16_t diagnostics = make_reply(0x0100u, !cases[i].corrupt_angle && !cases[i].corrupt_parity);
    const uint16_t angle_frame = cases[i].corrupt_angle && cases[i].corrupt_parity
                                     ? bad_parity(angle)
                                     : angle;
    const uint16_t diagnostics_frame = !cases[i].corrupt_angle && cases[i].corrupt_parity
                                           ? bad_parity(diagnostics)
                                           : diagnostics;
    set_replies(0u, angle_frame, diagnostics_frame);
    (void)poll_encoder();

    gl30_factory_encoder_sample_t sample = {0};
    gl30_factory_encoder_diagnostics_t diag = {0};
    snapshot_encoder(&sample, &diag);
    char message[160];
    const uint32_t expected_fault = cases[i].corrupt_parity
                                        ? GL30_ENCODER_FAULT_PARITY
                                        : GL30_ENCODER_FAULT_SENSOR;
    snprintf(message, sizeof(message), "%s rejects the sample", cases[i].name);
    CHECK(!sample.valid && sample.status == GL30_FACTORY_ENCODER_STATUS_INVALID,
          message);
    snprintf(message, sizeof(message), "%s sets its diagnostic fault", cases[i].name);
    CHECK(diag.latest_faults == expected_fault, message);
    snprintf(message, sizeof(message), "%s increments one failed sample", cases[i].name);
    CHECK(diag.failed_samples == 1u && diag.successful_samples == 0u,
          message);
    snprintf(message, sizeof(message), "%s increments only its matching error counter", cases[i].name);
    CHECK(diag.parity_errors == (cases[i].corrupt_parity ? 1u : 0u) &&
          diag.sensor_errors == (cases[i].corrupt_parity ? 0u : 1u),
          message);
    snprintf(message, sizeof(message), "%s raw response remains available", cases[i].name);
    CHECK(diag.raw_angle == (cases[i].corrupt_angle ? angle_frame : angle) &&
          diag.raw_diagnostics == (!cases[i].corrupt_angle ? diagnostics_frame : diagnostics),
          message);
    CHECK(diag.transport_errors == 0u && diag.field_errors == 0u,
          "parity and EF failures are not misclassified as transport or field faults");
  }

  begin_case();
  const uint16_t angle = bad_parity(make_reply(0x2345u, false));
  const uint16_t diagnostics = bad_parity(make_reply(0x0100u, false));
  set_replies(0u, angle, diagnostics);
  (void)poll_encoder();
  gl30_factory_encoder_sample_t sample = {0};
  gl30_factory_encoder_diagnostics_t diag = {0};
  snapshot_encoder(&sample, &diag);
  CHECK(!sample.valid && sample.status == GL30_FACTORY_ENCODER_STATUS_INVALID,
        "bad parity in both response words rejects the sample");
  CHECK(diag.latest_faults == GL30_ENCODER_FAULT_PARITY &&
        diag.parity_errors == 1u && diag.failed_samples == 1u,
        "two parity failures in one poll increment parity_errors once");
  CHECK(diag.raw_angle == angle && diag.raw_diagnostics == diagnostics &&
        diag.sensor_errors == 0u && diag.transport_errors == 0u,
        "both malformed words remain visible without unrelated error counts");
}

static void test_diagnostic_field_errors(void) {
  const struct field_case {
    const char *name;
    uint16_t diagnostics;
  } cases[] = {
      {"OCF missing", 0x0000u},
      {"COF set", 0x0300u},
      {"COMP_LOW set", 0x0500u},
      {"COMP_HIGH set", 0x0900u},
  };

  for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    begin_case();
    set_healthy_replies(0x1234u, cases[i].diagnostics);
    (void)poll_encoder();

    gl30_factory_encoder_sample_t sample = {0};
    gl30_factory_encoder_diagnostics_t diag = {0};
    snapshot_encoder(&sample, &diag);
    char message[160];
    snprintf(message, sizeof(message), "diagnostic field case %s rejects the sample", cases[i].name);
    CHECK(!sample.valid && sample.status == GL30_FACTORY_ENCODER_STATUS_INVALID,
          message);
    snprintf(message, sizeof(message), "diagnostic field case %s sets FIELD only", cases[i].name);
    CHECK(diag.latest_faults == GL30_ENCODER_FAULT_FIELD, message);
    snprintf(message, sizeof(message), "diagnostic field case %s increments field error", cases[i].name);
    CHECK(diag.field_errors == 1u && diag.failed_samples == 1u &&
          diag.successful_samples == 0u,
          message);
    snprintf(message, sizeof(message), "diagnostic field case %s preserves the complete raw DIAG word", cases[i].name);
    CHECK(diag.raw_diagnostics == make_reply(cases[i].diagnostics, false), message);
    CHECK(diag.transport_errors == 0u && diag.parity_errors == 0u &&
          diag.sensor_errors == 0u,
          "field failures do not increment unrelated error counters");
  }
}

static void test_transport_failures_are_bounded_and_release_cs(void) {
  const struct transport_case {
    const char *name;
    fake_spi_mode_t mode;
    bool overrun;
    bool mode_fault;
    bool disabled;
    bool stale_fifo;
    uint32_t expected_faults;
    unsigned expected_transfers;
  } cases[] = {
      {"TXE wait", FAKE_SPI_TXE_STUCK, false, false, false, false,
       GL30_ENCODER_FAULT_TRANSPORT | GL30_ENCODER_FAULT_TIMEOUT, 0u},
      {"RXNE wait", FAKE_SPI_RXNE_STUCK, false, false, false, false,
       GL30_ENCODER_FAULT_TRANSPORT | GL30_ENCODER_FAULT_TIMEOUT, 1u},
      {"BSY wait", FAKE_SPI_BSY_STUCK, false, false, false, false,
       GL30_ENCODER_FAULT_TRANSPORT | GL30_ENCODER_FAULT_TIMEOUT, 1u},
      {"stale RX FIFO", FAKE_SPI_RX_FIFO_STUCK, false, false, false, true,
       GL30_ENCODER_FAULT_TRANSPORT, 0u},
      {"OVR", FAKE_SPI_NORMAL, true, false, false, false,
       GL30_ENCODER_FAULT_TRANSPORT, 0u},
      {"MODF", FAKE_SPI_NORMAL, false, true, false, false,
       GL30_ENCODER_FAULT_TRANSPORT, 0u},
      {"disabled SPI", FAKE_SPI_NORMAL, false, false, true, false,
       GL30_ENCODER_FAULT_TRANSPORT, 0u},
  };

  for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    begin_case();
    set_healthy_replies(0x1234u, 0x0100u);
    g_spi_mode = cases[i].mode;
    g_spi_ovr = cases[i].overrun;
    g_spi_modf = cases[i].mode_fault;
    if (cases[i].disabled) {
      g_spi_enabled = false;
    }
    if (cases[i].stale_fifo) {
      g_rx_ready = true;
      g_rx_word = 0xffffu;
    }
    (void)poll_encoder();

    gl30_factory_encoder_sample_t sample = {0};
    gl30_factory_encoder_diagnostics_t diag = {0};
    snapshot_encoder(&sample, &diag);
    char message[160];
    snprintf(message, sizeof(message), "%s failure invalidates the sample", cases[i].name);
    CHECK(!sample.valid && sample.status == GL30_FACTORY_ENCODER_STATUS_INVALID,
          message);
    snprintf(message, sizeof(message), "%s reports the expected fault bits", cases[i].name);
    CHECK(diag.latest_faults == cases[i].expected_faults, message);
    snprintf(message, sizeof(message), "%s increments transport and failed counters", cases[i].name);
    CHECK(diag.transport_errors == 1u && diag.failed_samples == 1u &&
          diag.successful_samples == 0u,
          message);
    snprintf(message, sizeof(message), "%s makes only the expected number of transfers", cases[i].name);
    CHECK(g_tx_count == cases[i].expected_transfers, message);
    snprintf(message, sizeof(message), "%s exits with chip select high", cases[i].name);
    CHECK(g_cs_high && !g_bad_cs_pin, message);
    if (cases[i].stale_fifo) {
      CHECK(g_rx_count <= 4u && g_rx_count == 4u,
            "stuck stale RX FIFO is drained at most four words before failure");
    }
    snprintf(message, sizeof(message), "%s duration is bounded (observed %u us)",
             cases[i].name, (unsigned)diag.last_duration_us);
    CHECK(diag.last_duration_us <= GL30_ENCODER_SPI_TIMEOUT_US + 4u,
          message);
  }

  begin_case();
  set_healthy_replies(0x1234u, 0x0100u);
  g_spi_fre = true;
  (void)poll_encoder();
  gl30_factory_encoder_sample_t sample = {0};
  gl30_factory_encoder_diagnostics_t diag = {0};
  snapshot_encoder(&sample, &diag);
  CHECK(!sample.valid && sample.status == GL30_FACTORY_ENCODER_STATUS_INVALID &&
        diag.latest_faults == GL30_ENCODER_FAULT_TRANSPORT,
        "FRE framing error rejects the sample as a transport fault");
  CHECK(diag.transport_errors == 1u && g_tx_count == 0u,
        "FRE is detected before transmitting a command");
  CHECK(g_cs_high && !g_bad_cs_pin,
        "FRE failure leaves the encoder chip select high");
}

static void test_shared_deadline_and_final_reply_timeout(void) {
  begin_case();
  set_healthy_replies(0x1234u, 0x0100u);
  g_jump_after_receive = 1u;
  g_jump_offset_us = GL30_ENCODER_SPI_TIMEOUT_US - 8u;
  (void)poll_encoder();

  gl30_factory_encoder_sample_t sample = {0};
  gl30_factory_encoder_diagnostics_t diag = {0};
  snapshot_encoder(&sample, &diag);
  CHECK(!sample.valid && sample.status == GL30_FACTORY_ENCODER_STATUS_INVALID,
        "one poll cannot recover a new timeout for each SPI frame");
  CHECK(diag.latest_faults == (GL30_ENCODER_FAULT_TRANSPORT | GL30_ENCODER_FAULT_TIMEOUT),
        "shared three-frame deadline reports a timeout transport fault");
  CHECK(g_tx_count == 1u,
        "shared deadline stops before starting the next frame after the budget expires");
  CHECK(diag.last_duration_us >= GL30_ENCODER_SPI_TIMEOUT_US,
        "shared deadline elapsed across frames is reflected in diagnostics");
  CHECK(g_cs_high, "shared-deadline failure releases chip select");

  begin_case();
  set_healthy_replies(0x1234u, 0x0100u);
  g_jump_after_receive = 3u;
  g_jump_offset_us = GL30_ENCODER_SPI_TIMEOUT_US + 1u;
  (void)poll_encoder();
  snapshot_encoder(&sample, &diag);
  CHECK(g_tx_count == 3u && g_rx_count == 3u,
        "final ANGLE and DIAG replies are received before the elapsed-time check");
  CHECK(!sample.valid && sample.status == GL30_FACTORY_ENCODER_STATUS_INVALID,
        "a valid final reply cannot publish READY after the shared deadline expires");
  CHECK(diag.latest_faults == (GL30_ENCODER_FAULT_TRANSPORT | GL30_ENCODER_FAULT_TIMEOUT),
        "late final reply is reported as a transport timeout");
  CHECK(diag.raw_angle == make_reply(0x1234u, false) &&
        diag.raw_diagnostics == make_reply(0x0100u, false),
        "late valid replies remain available in diagnostics");
  CHECK(diag.successful_samples == 0u && diag.failed_samples == 1u &&
        diag.transport_errors == 1u,
        "late final reply counts once as a failed transport sample");
  CHECK(g_cs_high, "late-final-reply failure releases chip select");
}

static void test_fault_preserves_last_good_sample_and_recovers(void) {
  begin_case();
  set_healthy_replies(1000u, 0x0100u);
  const uint64_t first_started = poll_encoder();
  gl30_factory_encoder_sample_t good = {0};
  gl30_factory_encoder_diagnostics_t diag = {0};
  snapshot_encoder(&good, &diag);
  CHECK(good.valid && good.sample_index == 1u,
        "first healthy sample becomes the last good sample");

  set_healthy_replies(2000u, 0x0300u);
  (void)poll_encoder();
  gl30_factory_encoder_sample_t failed = {0};
  snapshot_encoder(&failed, &diag);
  CHECK(!failed.valid && failed.status == GL30_FACTORY_ENCODER_STATUS_INVALID,
        "a diagnostic field fault invalidates the current sample");
  CHECK(failed.timestamp_us == good.timestamp_us && failed.timestamp_us == first_started,
        "a failed poll preserves the previous successful timestamp");
  CHECK(failed.sample_index == good.sample_index && failed.angle_rad == good.angle_rad,
        "a failed poll preserves the last successful angle and index");
  CHECK(!control_ready(&failed, failed.timestamp_us + 1u, GL30_ENCODER_SPI_TIMEOUT_US),
        "an invalid sample is not control-ready despite its retained timestamp");
  CHECK(diag.latest_faults == GL30_ENCODER_FAULT_FIELD &&
        diag.successful_samples == 1u && diag.failed_samples == 1u &&
        diag.field_errors == 1u,
        "diagnostics record the failed poll without erasing success history");

  set_healthy_replies(8192u, 0x0101u);
  const uint64_t recovered_started = poll_encoder();
  gl30_factory_encoder_sample_t recovered = {0};
  snapshot_encoder(&recovered, &diag);
  CHECK(recovered.valid && recovered.status == GL30_FACTORY_ENCODER_STATUS_READY,
        "a genuinely healthy follow-up poll restores driver readiness");
  CHECK(recovered.sample_index == good.sample_index + 1u &&
        recovered.timestamp_us == recovered_started &&
        recovered.timestamp_us > good.timestamp_us,
        "recovery timestamps the new poll and advances only the successful index");
  CHECK(diag.latest_faults == 0u && diag.successful_samples == 2u &&
        diag.failed_samples == 1u && diag.field_errors == 1u,
        "recovery clears the latest fault and retains cumulative diagnostics");
  CHECK(control_ready(&recovered, g_fake_us, GL30_ENCODER_SPI_TIMEOUT_US),
        "recovered healthy sample satisfies control_ready");
  CHECK(g_cs_high, "recovery finishes with chip select high");
}

int main(void) {
  test_pipeline_snapshot_and_angle_boundaries();
  test_response_parity_and_sensor_errors();
  test_diagnostic_field_errors();
  test_transport_failures_are_bounded_and_release_cs();
  test_shared_deadline_and_final_reply_timeout();
  test_fault_preserves_last_good_sample_and_recovers();
  printf("product encoder: %u checks, %u failed\n", g_checks, g_failed);
  return g_failed == 0u ? 0 : 1;
}
