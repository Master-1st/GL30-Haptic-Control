#include "factory_encoder.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "as5048a.h"
#include "../config/board_config.h"
#include "../trace/timebase.h"

#if !GL30_BUILD_ONLY
#include "../cubemx/GL30_AMOLED_V7/Core/Inc/main.h"
#endif

#define GL30_AS5048A_ANGLE_SCALE_RAD 0.0003834951969714103f
#define GL30_AS5048A_MAX_STALE_RX_WORDS 4u
#define GL30_AS5048A_CS_INTERVAL_US 1u

static volatile gl30_factory_encoder_sample_t g_factory_encoder_sample = {
    .angle_rad = 0.0f,
    .timestamp_us = 0u,
    .status = GL30_FACTORY_ENCODER_STATUS_INITIALIZING,
    .valid = false,
    .sample_index = 0u,
};

static volatile gl30_factory_encoder_diagnostics_t g_factory_encoder_diag;

static uint32_t encoder_lock(void) {
#if GL30_BUILD_ONLY
  return 0u;
#else
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
#endif
}

static void encoder_unlock(uint32_t primask) {
#if GL30_BUILD_ONLY
  (void)primask;
#else
  __set_PRIMASK(primask);
#endif
}

static void encoder_sample_copy(gl30_factory_encoder_sample_t *out) {
  const uint32_t primask = encoder_lock();
  out->angle_rad = g_factory_encoder_sample.angle_rad;
  out->timestamp_us = g_factory_encoder_sample.timestamp_us;
  out->status = g_factory_encoder_sample.status;
  out->valid = g_factory_encoder_sample.valid;
  out->sample_index = g_factory_encoder_sample.sample_index;
  encoder_unlock(primask);
}

static void encoder_diagnostics_copy(
    gl30_factory_encoder_diagnostics_t *out) {
  const uint32_t primask = encoder_lock();
  out->latest_faults = g_factory_encoder_diag.latest_faults;
  out->successful_samples = g_factory_encoder_diag.successful_samples;
  out->failed_samples = g_factory_encoder_diag.failed_samples;
  out->parity_errors = g_factory_encoder_diag.parity_errors;
  out->transport_errors = g_factory_encoder_diag.transport_errors;
  out->sensor_errors = g_factory_encoder_diag.sensor_errors;
  out->field_errors = g_factory_encoder_diag.field_errors;
  out->last_duration_us = g_factory_encoder_diag.last_duration_us;
  out->raw_angle = g_factory_encoder_diag.raw_angle;
  out->raw_diagnostics = g_factory_encoder_diag.raw_diagnostics;
  encoder_unlock(primask);
}

static void encoder_publish(const gl30_factory_encoder_sample_t *sample,
                            const gl30_factory_encoder_diagnostics_t *diag) {
  const uint32_t primask = encoder_lock();
  g_factory_encoder_sample.angle_rad = sample->angle_rad;
  g_factory_encoder_sample.timestamp_us = sample->timestamp_us;
  g_factory_encoder_sample.status = sample->status;
  g_factory_encoder_sample.valid = sample->valid;
  g_factory_encoder_sample.sample_index = sample->sample_index;

  g_factory_encoder_diag.latest_faults = diag->latest_faults;
  g_factory_encoder_diag.successful_samples = diag->successful_samples;
  g_factory_encoder_diag.failed_samples = diag->failed_samples;
  g_factory_encoder_diag.parity_errors = diag->parity_errors;
  g_factory_encoder_diag.transport_errors = diag->transport_errors;
  g_factory_encoder_diag.sensor_errors = diag->sensor_errors;
  g_factory_encoder_diag.field_errors = diag->field_errors;
  g_factory_encoder_diag.last_duration_us = diag->last_duration_us;
  g_factory_encoder_diag.raw_angle = diag->raw_angle;
  g_factory_encoder_diag.raw_diagnostics = diag->raw_diagnostics;
  encoder_unlock(primask);
}

void gl30_factory_encoder_init(void) {
  const gl30_factory_encoder_sample_t sample = {
      .angle_rad = 0.0f,
      .timestamp_us = 0u,
      .status = GL30_FACTORY_ENCODER_STATUS_INITIALIZING,
      .valid = false,
      .sample_index = 0u,
  };
  const gl30_factory_encoder_diagnostics_t diag = {0};
  encoder_publish(&sample, &diag);

#if !GL30_BUILD_ONLY
  LL_GPIO_SetOutputPin(ENC_CS_MCU_GPIO_Port, ENC_CS_MCU_Pin);
  LL_SPI_SetRxFIFOThreshold(SPI1, LL_SPI_RX_FIFO_TH_HALF);
  LL_SPI_Enable(SPI1);
#endif
}

#if !GL30_BUILD_ONLY
static bool encoder_spi_check(uint64_t started_us, uint32_t *faults) {
  bool ok = true;
  if (LL_SPI_IsEnabled(SPI1) == 0u ||
      LL_SPI_IsActiveFlag_OVR(SPI1) != 0u ||
      LL_SPI_IsActiveFlag_MODF(SPI1) != 0u ||
      LL_SPI_IsActiveFlag_FRE(SPI1) != 0u) {
    *faults |= GL30_ENCODER_FAULT_TRANSPORT;
    ok = false;
  }
  if ((gl30_timebase_now_us() - started_us) >=
      (uint64_t)GL30_ENCODER_SPI_TIMEOUT_US) {
    *faults |= GL30_ENCODER_FAULT_TRANSPORT | GL30_ENCODER_FAULT_TIMEOUT;
    ok = false;
  }
  return ok;
}

static bool encoder_drain_rx(uint64_t started_us, uint32_t *faults) {
  uint32_t reads = 0u;
  while (LL_SPI_IsActiveFlag_RXNE(SPI1) != 0u &&
         reads < GL30_AS5048A_MAX_STALE_RX_WORDS) {
    if (!encoder_spi_check(started_us, faults) ||
        LL_SPI_IsActiveFlag_RXNE(SPI1) == 0u) {
      return false;
    }
    (void)LL_SPI_ReceiveData16(SPI1);
    ++reads;
  }
  if (!encoder_spi_check(started_us, faults)) {
    return false;
  }
  if (LL_SPI_IsActiveFlag_RXNE(SPI1) != 0u) {
    *faults |= GL30_ENCODER_FAULT_TRANSPORT;
    return false;
  }
  return true;
}

static bool encoder_spi_transfer(uint64_t started_us, uint16_t tx_word,
                                 uint16_t *rx_word, bool *received,
                                 uint32_t *faults) {
  *received = false;
  if (!encoder_spi_check(started_us, faults)) {
    return false;
  }

  LL_GPIO_ResetOutputPin(ENC_CS_MCU_GPIO_Port, ENC_CS_MCU_Pin);
  gl30_timebase_delay_us(GL30_AS5048A_CS_INTERVAL_US);
  if (!encoder_spi_check(started_us, faults)) {
    goto fail;
  }

  while (LL_SPI_IsActiveFlag_TXE(SPI1) == 0u) {
    if (!encoder_spi_check(started_us, faults)) {
      goto fail;
    }
  }
  if (!encoder_spi_check(started_us, faults)) {
    goto fail;
  }
  LL_SPI_TransmitData16(SPI1, tx_word);

  while (LL_SPI_IsActiveFlag_RXNE(SPI1) == 0u) {
    if (!encoder_spi_check(started_us, faults)) {
      goto fail;
    }
  }
  if (!encoder_spi_check(started_us, faults)) {
    goto fail;
  }
  *rx_word = LL_SPI_ReceiveData16(SPI1);
  *received = true;
  if (!encoder_spi_check(started_us, faults)) {
    goto fail;
  }

  while (LL_SPI_IsActiveFlag_BSY(SPI1) != 0u) {
    if (!encoder_spi_check(started_us, faults)) {
      goto fail;
    }
  }
  if (!encoder_spi_check(started_us, faults)) {
    goto fail;
  }

  gl30_timebase_delay_us(GL30_AS5048A_CS_INTERVAL_US);
  if (!encoder_spi_check(started_us, faults)) {
    goto fail;
  }
  LL_GPIO_SetOutputPin(ENC_CS_MCU_GPIO_Port, ENC_CS_MCU_Pin);
  gl30_timebase_delay_us(GL30_AS5048A_CS_INTERVAL_US);
  return encoder_spi_check(started_us, faults);

fail:
  LL_GPIO_SetOutputPin(ENC_CS_MCU_GPIO_Port, ENC_CS_MCU_Pin);
  return false;
}

static void encoder_record_response_faults(uint16_t response,
                                          uint32_t *faults) {
  const uint8_t response_faults = as5048a_response_faults(response);
  if ((response_faults & AS5048A_RESPONSE_PARITY) != 0u) {
    *faults |= GL30_ENCODER_FAULT_PARITY;
  }
  if ((response_faults & AS5048A_RESPONSE_EF) != 0u) {
    *faults |= GL30_ENCODER_FAULT_SENSOR;
  }
}
#endif

#if !GL30_BUILD_ONLY
static uint32_t encoder_elapsed_us(uint64_t started_us) {
  const uint64_t elapsed_us = gl30_timebase_now_us() - started_us;
  return elapsed_us > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed_us;
}
#endif

void gl30_factory_encoder_poll_4k(void) {
#if GL30_BUILD_ONLY
  /* Host-only builds have no physical transport to sample. */
  return;
#else
  const uint64_t started_us = gl30_timebase_now_us();
  uint32_t faults = 0u;
  uint16_t raw_angle = 0u;
  uint16_t raw_diagnostics = 0u;
  uint16_t angle_data = 0u;
  uint16_t diagnostics_data = 0u;
  bool angle_received = false;
  bool diagnostics_received = false;
  bool diagnostics_decoded = false;

  gl30_factory_encoder_sample_t sample;
  gl30_factory_encoder_diagnostics_t diag;
  encoder_sample_copy(&sample);
  encoder_diagnostics_copy(&diag);
  diag.latest_faults = 0u;
  diag.raw_angle = 0u;
  diag.raw_diagnostics = 0u;

  LL_GPIO_SetOutputPin(ENC_CS_MCU_GPIO_Port, ENC_CS_MCU_Pin);
  if (encoder_spi_check(started_us, &faults)) {
    gl30_timebase_delay_us(GL30_AS5048A_CS_INTERVAL_US);
    if (encoder_spi_check(started_us, &faults) &&
        encoder_drain_rx(started_us, &faults)) {
      const uint16_t commands[3] = {
          as5048a_read_command(AS5048A_REG_ANGLE),
          as5048a_read_command(AS5048A_REG_DIAG),
          0u, /* AS5048A NOP: clock out the diagnostic-register response. */
      };
      for (uint32_t frame = 0u; frame < 3u; ++frame) {
        uint16_t response = 0u;
        bool received = false;
        const bool transfer_ok = encoder_spi_transfer(
            started_us, commands[frame], &response, &received, &faults);
        if (frame == 1u && received) {
          raw_angle = response;
          angle_received = true;
        } else if (frame == 2u && received) {
          raw_diagnostics = response;
          diagnostics_received = true;
        }
        if (!transfer_ok) {
          break;
        }
      }
      (void)encoder_spi_check(started_us, &faults);
    }
  }

  if (angle_received) {
    encoder_record_response_faults(raw_angle, &faults);
    (void)as5048a_decode(raw_angle, &angle_data);
  }
  if (diagnostics_received) {
    encoder_record_response_faults(raw_diagnostics, &faults);
    diagnostics_decoded = as5048a_decode(raw_diagnostics, &diagnostics_data);
  }
  if (!angle_received || !diagnostics_received) {
    faults |= GL30_ENCODER_FAULT_TRANSPORT;
  }
  if (diagnostics_decoded && !as5048a_diagnostics_ok(diagnostics_data)) {
    faults |= GL30_ENCODER_FAULT_FIELD;
  }

  if ((gl30_timebase_now_us() - started_us) >=
      (uint64_t)GL30_ENCODER_SPI_TIMEOUT_US) {
    faults |= GL30_ENCODER_FAULT_TRANSPORT | GL30_ENCODER_FAULT_TIMEOUT;
  }

  diag.latest_faults = faults;
  diag.raw_angle = raw_angle;
  diag.raw_diagnostics = raw_diagnostics;
  diag.last_duration_us = encoder_elapsed_us(started_us);

  if (faults == 0u) {
    sample.angle_rad = (float)angle_data * GL30_AS5048A_ANGLE_SCALE_RAD;
    sample.timestamp_us = started_us;
    sample.status = GL30_FACTORY_ENCODER_STATUS_READY;
    sample.valid = true;
    ++sample.sample_index;
    ++diag.successful_samples;
  } else {
    sample.status = GL30_FACTORY_ENCODER_STATUS_INVALID;
    sample.valid = false;
    ++diag.failed_samples;
    if ((faults & GL30_ENCODER_FAULT_TRANSPORT) != 0u) {
      ++diag.transport_errors;
    }
    if ((faults & GL30_ENCODER_FAULT_PARITY) != 0u) {
      ++diag.parity_errors;
    }
    if ((faults & GL30_ENCODER_FAULT_SENSOR) != 0u) {
      ++diag.sensor_errors;
    }
    if ((faults & GL30_ENCODER_FAULT_FIELD) != 0u) {
      ++diag.field_errors;
    }
  }
  encoder_publish(&sample, &diag);
#endif
}

void gl30_factory_encoder_snapshot(gl30_factory_encoder_sample_t *sample) {
  if (sample != NULL) {
    encoder_sample_copy(sample);
  }
}

void gl30_factory_encoder_diagnostics_snapshot(
    gl30_factory_encoder_diagnostics_t *out) {
  if (out != NULL) {
    encoder_diagnostics_copy(out);
  }
}

bool control_ready(const gl30_factory_encoder_sample_t *sample, uint64_t now_us,
                   uint64_t max_age_us) {
  if (sample == NULL || !sample->valid || !isfinite(sample->angle_rad) ||
      sample->status != GL30_FACTORY_ENCODER_STATUS_READY ||
      sample->timestamp_us == 0u || now_us < sample->timestamp_us) {
    return false;
  }
  const uint64_t age_us = now_us - sample->timestamp_us;
  return age_us <= max_age_us;
}
