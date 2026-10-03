#include "drv8316.h"

#include <stddef.h>

#include "board_config.h"

#if !GL30_BUILD_ONLY
#include "../cubemx/GL30_AMOLED_V7/Core/Inc/main.h"
#include "../trace/timebase.h"
#endif

/* Frozen first-board register image. These values select 6-PWM, 50 V/us slew,
 * 40 kHz 100%-duty handling, 22 V internal OVP, latched 16 A short-circuit
 * OCP, 0.6 V/A CSA gain, and disabled internal buck. The project 2 A limit has
 * two layers: an external SOA/SOB/SOC window-comparator network wired to TIM1
 * BKIN, plus the 40 kHz ADC/software trip. DRV8316 OCP remains the independent
 * short-circuit layer because its minimum selectable value is 16 A. */
#define GL30_DRV_CTRL2_VALUE 0x68u
#define GL30_DRV_CTRL3_VALUE 0x5Fu
#define GL30_DRV_CTRL4_VALUE 0x10u
#define GL30_DRV_CTRL5_VALUE 0x02u
#define GL30_DRV_CTRL6_VALUE 0x01u
#define GL30_DRV_CTRL10_VALUE 0x00u
#define GL30_DRV8316_SPI_CS_GUARD_US 2u
#define GL30_DRV8316_SPI_MAX_STALE_RX 4u

static volatile bool g_configured;
static volatile bool g_output_enabled;
static volatile bool g_startup_verified;
static volatile uint32_t g_off_generation;
static volatile uint32_t g_power_generation;
static uint8_t g_last_spi_status;

#if !GL30_BUILD_ONLY
static uint32_t irq_save(void) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void irq_restore(uint32_t primask) {
  if (primask == 0u) {
    __enable_irq();
  }
}

static void safe_off_locked(void) {
  ++g_off_generation;
  g_output_enabled = false;
  LL_GPIO_SetOutputPin(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin);
  LL_TIM_DisableIT_BRK(TIM1);
  LL_TIM_DisableAllOutputs(TIM1);
  LL_TIM_OC_SetCompareCH1(TIM1, GL30_TIM1_ARR / 2u);
  LL_TIM_OC_SetCompareCH2(TIM1, GL30_TIM1_ARR / 2u);
  LL_TIM_OC_SetCompareCH3(TIM1, GL30_TIM1_ARR / 2u);
}

static uint32_t power_generation_snapshot(void) {
  const uint32_t primask = irq_save();
  const uint32_t generation = g_power_generation;
  irq_restore(primask);
  return generation;
}

static bool power_epoch_matches(uint32_t expected_power_generation) {
  const uint32_t primask = irq_save();
  const bool matches = g_power_generation == expected_power_generation;
  irq_restore(primask);
  return matches && g_power_generation == expected_power_generation;
}

static bool config_epoch_matches(uint32_t expected_off_generation,
                                 uint32_t expected_power_generation) {
  const uint32_t primask = irq_save();
  const bool matches =
      g_off_generation == expected_off_generation &&
      g_power_generation == expected_power_generation;
  irq_restore(primask);
  return matches && g_off_generation == expected_off_generation &&
         g_power_generation == expected_power_generation;
}

static bool transfer_checkpoint(uint64_t started_us,
                                uint32_t expected_power_generation,
                                bool check_off_generation,
                                uint32_t expected_off_generation) {
  const uint64_t now_us = gl30_timebase_now_us();
  const bool spi_ok =
      LL_SPI_IsEnabled(SPI3) != 0u &&
      LL_SPI_IsActiveFlag_OVR(SPI3) == 0u &&
      LL_SPI_IsActiveFlag_MODF(SPI3) == 0u &&
      LL_SPI_IsActiveFlag_FRE(SPI3) == 0u;
  const bool epoch_ok =
      g_power_generation == expected_power_generation &&
      (!check_off_generation ||
       g_off_generation == expected_off_generation);
  return spi_ok && epoch_ok &&
         (now_us - started_us) <
             (uint64_t)GL30_DRV8316_SPI_TIMEOUT_US;
}

static bool transfer(uint16_t tx_word, uint8_t *status, uint8_t *data,
                     uint32_t expected_power_generation,
                     bool check_off_generation,
                     uint32_t expected_off_generation) {
  const uint32_t transfer_power_generation = g_power_generation;
  const uint64_t started_us = gl30_timebase_now_us();
  uint16_t rx_word = 0u;
  uint32_t stale_reads = 0u;

  if (transfer_power_generation != expected_power_generation) {
    goto fail;
  }
  for (; stale_reads < GL30_DRV8316_SPI_MAX_STALE_RX; ++stale_reads) {
    if (!transfer_checkpoint(started_us, expected_power_generation,
                             check_off_generation,
                             expected_off_generation)) {
      goto fail;
    }
    if (LL_SPI_IsActiveFlag_RXNE(SPI3) == 0u) {
      break;
    }
    (void)LL_SPI_ReceiveData16(SPI3);
  }
  if (!transfer_checkpoint(started_us, expected_power_generation,
                           check_off_generation,
                           expected_off_generation) ||
      LL_SPI_IsActiveFlag_RXNE(SPI3) != 0u) {
    goto fail;
  }

  while (LL_SPI_IsActiveFlag_TXE(SPI3) == 0u ||
         LL_SPI_IsActiveFlag_BSY(SPI3) != 0u) {
    if (!transfer_checkpoint(started_us, expected_power_generation,
                             check_off_generation,
                             expected_off_generation)) {
      goto fail;
    }
  }
  if (!transfer_checkpoint(started_us, expected_power_generation,
                           check_off_generation,
                           expected_off_generation)) {
    goto fail;
  }

  gl30_timebase_delay_us(GL30_DRV8316_SPI_CS_GUARD_US);
  if (!transfer_checkpoint(started_us, expected_power_generation,
                           check_off_generation,
                           expected_off_generation) ||
      LL_SPI_IsActiveFlag_RXNE(SPI3) != 0u ||
      LL_SPI_IsActiveFlag_TXE(SPI3) == 0u ||
      LL_SPI_IsActiveFlag_BSY(SPI3) != 0u) {
    goto fail;
  }

  LL_GPIO_ResetOutputPin(DRV8316_NSS_GPIO_Port, DRV8316_NSS_Pin);
  gl30_timebase_delay_us(GL30_DRV8316_SPI_CS_GUARD_US);
  if (!transfer_checkpoint(started_us, expected_power_generation,
                           check_off_generation,
                           expected_off_generation) ||
      LL_SPI_IsActiveFlag_TXE(SPI3) == 0u ||
      LL_SPI_IsActiveFlag_BSY(SPI3) != 0u) {
    goto fail;
  }
  LL_SPI_TransmitData16(SPI3, tx_word);
  while (LL_SPI_IsActiveFlag_RXNE(SPI3) == 0u) {
    if (!transfer_checkpoint(started_us, expected_power_generation,
                             check_off_generation,
                             expected_off_generation)) {
      goto fail;
    }
  }
  if (!transfer_checkpoint(started_us, expected_power_generation,
                           check_off_generation,
                           expected_off_generation)) {
    goto fail;
  }
  rx_word = LL_SPI_ReceiveData16(SPI3);
  while (LL_SPI_IsActiveFlag_BSY(SPI3) != 0u) {
    if (!transfer_checkpoint(started_us, expected_power_generation,
                             check_off_generation,
                             expected_off_generation)) {
      goto fail;
    }
  }
  if (!transfer_checkpoint(started_us, expected_power_generation,
                           check_off_generation,
                           expected_off_generation)) {
    goto fail;
  }
  gl30_timebase_delay_us(GL30_DRV8316_SPI_CS_GUARD_US);
  if (!transfer_checkpoint(started_us, expected_power_generation,
                           check_off_generation,
                           expected_off_generation)) {
    goto fail;
  }
  LL_GPIO_SetOutputPin(DRV8316_NSS_GPIO_Port, DRV8316_NSS_Pin);

  if (!transfer_checkpoint(started_us, expected_power_generation,
                           check_off_generation,
                           expected_off_generation)) {
    return false;
  }
  g_last_spi_status = (uint8_t)(rx_word >> 8u);
  if (status != NULL) {
    *status = g_last_spi_status;
  }
  if (data != NULL) {
    *data = (uint8_t)rx_word;
  }
  return true;

fail:
  LL_GPIO_SetOutputPin(DRV8316_NSS_GPIO_Port, DRV8316_NSS_Pin);
  return false;
}

static bool write_register(uint8_t address, uint8_t value,
                           uint32_t expected_power_generation,
                           bool check_off_generation,
                           uint32_t expected_off_generation) {
  return transfer(gl30_drv8316_make_frame(false, address, value), NULL, NULL,
                  expected_power_generation, check_off_generation,
                  expected_off_generation);
}

static bool read_register_with_epoch(uint8_t address, uint8_t *data,
                                     uint32_t expected_power_generation,
                                     bool check_off_generation,
                                     uint32_t expected_off_generation) {
  if (data == NULL || address > 0x3Fu) {
    return false;
  }
  return transfer(gl30_drv8316_make_frame(true, address, 0u), NULL, data,
                  expected_power_generation, check_off_generation,
                  expected_off_generation);
}

static bool read_faults_with_epoch(gl30_drv8316_status_t *out,
                                   uint32_t expected_power_generation,
                                   bool check_off_generation,
                                   uint32_t expected_off_generation) {
  if (out == NULL) {
    return false;
  }
  gl30_drv8316_status_t status = {0};
  uint8_t *const registers[] = {&status.stat0, &status.stat1, &status.stat2};
  for (uint8_t address = GL30_DRV8316_REG_STAT0;
       address <= GL30_DRV8316_REG_STAT2; ++address) {
    if (!read_register_with_epoch(address, registers[address],
                                  expected_power_generation,
                                  check_off_generation,
                                  expected_off_generation)) {
      return false;
    }
    if (check_off_generation
            ? !config_epoch_matches(expected_off_generation,
                                    expected_power_generation)
            : !power_epoch_matches(expected_power_generation)) {
      return false;
    }
    /* NPOR is active-low; healthy STAT0 is 0x08, not zero. Preserve a
     * fault in any reply summary even if a later SPI reply is healthy. */
    const uint16_t reply = (uint16_t)((uint16_t)g_last_spi_status << 8u) |
                           *registers[address];
    status.normalized_faults |=
        gl30_drv8316_status_word_faults(address, reply);
  }
  status.spi_status = g_last_spi_status;
  status.n_fault_released =
      LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12);
  status.configured = g_configured;
  status.output_enabled = g_output_enabled;
  if (check_off_generation
          ? !config_epoch_matches(expected_off_generation,
                                  expected_power_generation)
          : !power_epoch_matches(expected_power_generation)) {
    return false;
  }
  *out = status;
  return true;
}

static bool startup_pwm_pins_low(void) {
  return LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_8) == 0u &&
         LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_9) == 0u &&
         LL_GPIO_IsInputPinSet(GPIOA, LL_GPIO_PIN_10) == 0u &&
         LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_13) == 0u &&
         LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_14) == 0u &&
         LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_15) == 0u;
}

static bool startup_drvoff_low(void) {
  return LL_GPIO_IsInputPinSet(DRV8316_DRVOFF_GPIO_Port,
                               DRV8316_DRVOFF_Pin) == 0u;
}

static bool startup_conditions_locked(uint32_t expected_off_generation,
                                      uint32_t expected_power_generation) {
  return g_configured && !g_output_enabled &&
         g_off_generation == expected_off_generation &&
         g_power_generation == expected_power_generation &&
         LL_TIM_IsEnabledAllOutputs(TIM1) == 0u && startup_pwm_pins_low();
}

static bool startup_reply_summary_ok(void) {
  const uint8_t normalized_summary = (uint8_t)(g_last_spi_status ^ 0x08u);
  return normalized_summary == 0u || normalized_summary == 0x08u;
}

static bool startup_write_register(uint8_t address, uint8_t value,
                                   uint32_t expected_power_generation,
                                   bool check_off_generation,
                                   uint32_t expected_off_generation) {
  return write_register(address, value, expected_power_generation,
                        check_off_generation, expected_off_generation) &&
         startup_reply_summary_ok();
}

static bool startup_read_register(uint8_t address, uint8_t *data,
                                  uint32_t expected_power_generation,
                                  bool check_off_generation,
                                  uint32_t expected_off_generation) {
  return read_register_with_epoch(address, data, expected_power_generation,
                                  check_off_generation,
                                  expected_off_generation) &&
         startup_reply_summary_ok();
}

static bool startup_verify_failed(uint32_t expected_power_generation,
                                  bool unlock_attempted) {
  const uint32_t primask = irq_save();
  g_startup_verified = false;
  safe_off_locked();
  irq_restore(primask);
  if (unlock_attempted && power_epoch_matches(expected_power_generation)) {
    (void)startup_write_register(GL30_DRV8316_REG_CTRL1, 0x06u,
                                 expected_power_generation, false, 0u);
  }
  return false;
}

static uint32_t duty_to_ccr(float duty) {
  if (duty < GL30_TIM1_MIN_DUTY) {
    duty = GL30_TIM1_MIN_DUTY;
  } else if (duty > GL30_TIM1_MAX_DUTY) {
    duty = GL30_TIM1_MAX_DUTY;
  }
  return (uint32_t)(duty * (float)(GL30_TIM1_ARR + 1u));
}
#endif

uint32_t gl30_drv8316_off_generation_snapshot(void) {
#if GL30_BUILD_ONLY
  return g_off_generation;
#else
  const uint32_t primask = irq_save();
  const uint32_t generation = g_off_generation;
  irq_restore(primask);
  return generation;
#endif
}


void gl30_drv8316_init(void) {
#if GL30_BUILD_ONLY
  g_configured = false;
  g_output_enabled = false;
  g_startup_verified = false;
  g_last_spi_status = 0u;
  g_off_generation = 0u;
  g_power_generation = 0u;
#else
  const uint32_t primask = irq_save();
  g_configured = false;
  g_output_enabled = false;
  g_startup_verified = false;
  g_last_spi_status = 0u;
  g_off_generation = 0u;
  g_power_generation = 0u;
  LL_TIM_DisableIT_BRK(TIM1);
  LL_TIM_DisableAllOutputs(TIM1);
  LL_GPIO_SetOutputPin(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin);
  LL_GPIO_SetOutputPin(DRV8316_NSS_GPIO_Port, DRV8316_NSS_Pin);
  LL_SPI_Enable(SPI3);
  gl30_drv8316_set_duty(0.5f, 0.5f, 0.5f);
  irq_restore(primask);
#endif
}

bool gl30_drv8316_read_register(uint8_t address, uint8_t *data) {
  if (data == NULL || address > 0x3Fu) {
    return false;
  }
#if GL30_BUILD_ONLY
  *data = 0u;
  return false;
#else
  const uint32_t power_generation = power_generation_snapshot();
  return read_register_with_epoch(address, data, power_generation, false, 0u);
#endif
}

bool gl30_drv8316_configure(uint32_t requested_off_generation) {
#if GL30_BUILD_ONLY
  (void)requested_off_generation;
  g_startup_verified = false;
  return false;
#else
  static const struct {
    uint8_t address;
    uint8_t value;
  } configuration[] = {
      {GL30_DRV8316_REG_CTRL2, GL30_DRV_CTRL2_VALUE},
      {GL30_DRV8316_REG_CTRL3, GL30_DRV_CTRL3_VALUE},
      {GL30_DRV8316_REG_CTRL4, GL30_DRV_CTRL4_VALUE},
      {GL30_DRV8316_REG_CTRL5, GL30_DRV_CTRL5_VALUE},
      {GL30_DRV8316_REG_CTRL6, GL30_DRV_CTRL6_VALUE},
      {GL30_DRV8316_REG_CTRL10, GL30_DRV_CTRL10_VALUE},
  };

  if (__get_PRIMASK() != 0u) {
    gl30_drv8316_safe_off();
    return false;
  }

  uint32_t primask = irq_save();
  if (requested_off_generation != g_off_generation) {
    irq_restore(primask);
    return false;
  }
  g_configured = false;
  g_startup_verified = false;
  safe_off_locked();
  const uint32_t expected_off_generation = g_off_generation;
  const uint32_t expected_power_generation = g_power_generation;
  irq_restore(primask);

  if (!config_epoch_matches(expected_off_generation,
                            expected_power_generation) ||
      !write_register(GL30_DRV8316_REG_CTRL1, 0x03u,
                      expected_power_generation, true,
                      expected_off_generation) ||
      !config_epoch_matches(expected_off_generation,
                            expected_power_generation)) {
    goto failed;
  }
  for (size_t index = 0u;
       index < sizeof(configuration) / sizeof(configuration[0]); ++index) {
    uint8_t readback = 0u;
    if (!config_epoch_matches(expected_off_generation,
                              expected_power_generation) ||
        !write_register(configuration[index].address,
                        configuration[index].value,
                        expected_power_generation, true,
                        expected_off_generation) ||
        !config_epoch_matches(expected_off_generation,
                              expected_power_generation) ||
        !read_register_with_epoch(configuration[index].address, &readback,
                                  expected_power_generation, true,
                                  expected_off_generation) ||
        !config_epoch_matches(expected_off_generation,
                              expected_power_generation) ||
        readback != configuration[index].value) {
      goto failed;
    }
  }
  if (!config_epoch_matches(expected_off_generation,
                            expected_power_generation) ||
      !write_register(GL30_DRV8316_REG_CTRL1, 0x06u,
                      expected_power_generation, true,
                      expected_off_generation) ||
      !config_epoch_matches(expected_off_generation,
                            expected_power_generation)) {
    goto failed;
  }
  uint8_t lock_readback = 0u;
  if (!read_register_with_epoch(GL30_DRV8316_REG_CTRL1, &lock_readback,
                                expected_power_generation, true,
                                expected_off_generation) ||
      !config_epoch_matches(expected_off_generation,
                            expected_power_generation) ||
      (lock_readback & 0x07u) != 0x06u) {
    goto failed;
  }

  primask = irq_save();
  const bool can_publish =
      g_off_generation == expected_off_generation &&
      g_power_generation == expected_power_generation;
  if (can_publish) {
    g_configured = true;
  }
  irq_restore(primask);
  if (!can_publish) {
    goto failed;
  }
  /* A pending safe-off/invalidation may run while PRIMASK is restored. */
  if (g_off_generation != expected_off_generation ||
      g_power_generation != expected_power_generation || !g_configured) {
    goto failed;
  }
  return true;

failed:
  g_configured = false;
  g_startup_verified = false;
  if (g_power_generation == expected_power_generation) {
    (void)write_register(GL30_DRV8316_REG_CTRL1, 0x06u,
                         expected_power_generation, false, 0u);
  }
  return false;
#endif
}

bool gl30_drv8316_verify_startup(uint32_t expected_off_generation) {
#if GL30_BUILD_ONLY
  (void)expected_off_generation;
  g_startup_verified = false;
  return false;
#else
  uint32_t expected_power_generation;
  uint32_t primask;
  gl30_drv8316_status_t status;
  uint8_t original_ctrl2 = 0u;
  uint8_t readback = 0u;
  bool unlock_attempted = false;
  bool clear_flt_sent = false;
  bool initial_conditions;

  if (__get_PRIMASK() != 0u) {
    g_startup_verified = false;
    gl30_drv8316_safe_off();
    return false;
  }
  expected_power_generation = power_generation_snapshot();
  primask = irq_save();
  g_startup_verified = false;
  initial_conditions =
      startup_conditions_locked(expected_off_generation,
                               expected_power_generation);
  if (initial_conditions) {
    LL_GPIO_ResetOutputPin(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin);
  }
  irq_restore(primask);
  if (!initial_conditions) {
    goto failed;
  }

  gl30_timebase_delay_us(1000u);
  primask = irq_save();
  initial_conditions =
      startup_conditions_locked(expected_off_generation,
                               expected_power_generation) &&
      startup_drvoff_low();
  irq_restore(primask);
  if (!initial_conditions ||
      !read_faults_with_epoch(&status, expected_power_generation, true,
                              expected_off_generation) ||
      !status.n_fault_released ||
      (status.normalized_faults != 0u &&
       status.normalized_faults != 0x08u)) {
    goto failed;
  }

  if (status.normalized_faults == 0x08u) {
    if (!startup_read_register(GL30_DRV8316_REG_CTRL2, &original_ctrl2,
                               expected_power_generation, true,
                               expected_off_generation) ||
        original_ctrl2 != GL30_DRV_CTRL2_VALUE ||
        !config_epoch_matches(expected_off_generation,
                              expected_power_generation)) {
      goto failed;
    }

    unlock_attempted = true;
    if (!startup_write_register(GL30_DRV8316_REG_CTRL1, 0x03u,
                                expected_power_generation, true,
                                expected_off_generation) ||
        !read_faults_with_epoch(&status, expected_power_generation, true,
                                expected_off_generation) ||
        !status.n_fault_released ||
        (status.normalized_faults != 0u &&
         status.normalized_faults != 0x08u)) {
      goto failed;
    }

    if (status.normalized_faults == 0x08u) {
      if (LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12) == 0u ||
          !config_epoch_matches(expected_off_generation,
                                expected_power_generation) ||
          !startup_write_register(GL30_DRV8316_REG_CTRL2,
                                  (uint8_t)(original_ctrl2 | 0x01u),
                                  expected_power_generation, true,
                                  expected_off_generation)) {
        goto failed;
      }
      clear_flt_sent = true;
    }

    if (!startup_write_register(GL30_DRV8316_REG_CTRL1, 0x06u,
                                expected_power_generation, true,
                                expected_off_generation) ||
        !startup_read_register(GL30_DRV8316_REG_CTRL1, &readback,
                               expected_power_generation, true,
                               expected_off_generation) ||
        (readback & 0x07u) != 0x06u ||
        !config_epoch_matches(expected_off_generation,
                              expected_power_generation)) {
      goto failed;
    }
    unlock_attempted = false;

    if (clear_flt_sent) {
      gl30_timebase_delay_us(1000u);
      if (!config_epoch_matches(expected_off_generation,
                                expected_power_generation) ||
          !startup_read_register(GL30_DRV8316_REG_CTRL2, &readback,
                                 expected_power_generation, true,
                                 expected_off_generation) ||
          readback != original_ctrl2 ||
          !startup_read_register(GL30_DRV8316_REG_CTRL1, &readback,
                                 expected_power_generation, true,
                                 expected_off_generation) ||
          (readback & 0x07u) != 0x06u) {
        goto failed;
      }
    }
  }

  if (!read_faults_with_epoch(&status, expected_power_generation, true,
                              expected_off_generation) ||
      !status.n_fault_released || status.normalized_faults != 0u) {
    goto failed;
  }

  primask = irq_save();
  const bool can_publish =
      startup_conditions_locked(expected_off_generation,
                                expected_power_generation) &&
      startup_drvoff_low() &&
      LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12) != 0u;
  if (can_publish) {
    g_startup_verified = true;
  }
  irq_restore(primask);
  if (!can_publish) {
    goto failed;
  }

  primask = irq_save();
  const bool still_verified =
      g_startup_verified &&
      startup_conditions_locked(expected_off_generation,
                                expected_power_generation) &&
      startup_drvoff_low() &&
      LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12) != 0u;
  irq_restore(primask);
  if (!still_verified || !g_startup_verified || !g_configured ||
      g_off_generation != expected_off_generation ||
      g_power_generation != expected_power_generation || g_output_enabled ||
      LL_TIM_IsEnabledAllOutputs(TIM1) != 0u ||
      !startup_pwm_pins_low() || !startup_drvoff_low() ||
      LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12) == 0u) {
    goto failed;
  }
  return true;

failed:
  return startup_verify_failed(expected_power_generation, unlock_attempted);
#endif
}

bool gl30_drv8316_startup_verified(void) {
#if GL30_BUILD_ONLY
  return false;
#else
  return g_startup_verified;
#endif
}

bool gl30_drv8316_clear_faults(void) {
#if GL30_BUILD_ONLY
  return false;
#else
  const uint32_t power_generation = power_generation_snapshot();
  bool lock_written = false;
  if (!power_epoch_matches(power_generation) ||
      !write_register(GL30_DRV8316_REG_CTRL1, 0x03u,
                      power_generation, false, 0u) ||
      !power_epoch_matches(power_generation) ||
      !write_register(GL30_DRV8316_REG_CTRL2,
                      (uint8_t)(GL30_DRV_CTRL2_VALUE | 0x01u),
                      power_generation, false, 0u) ||
      !power_epoch_matches(power_generation) ||
      !write_register(GL30_DRV8316_REG_CTRL1, 0x06u,
                      power_generation, false, 0u) ||
      !power_epoch_matches(power_generation)) {
    goto failed;
  }
  lock_written = true;
  gl30_timebase_delay_us(1000u);
  gl30_drv8316_status_t status;
  if (!power_epoch_matches(power_generation) ||
      !read_faults_with_epoch(&status, power_generation, false, 0u)) {
    goto failed;
  }
  return status.n_fault_released && status.normalized_faults == 0u;

failed:
  if (!lock_written && power_epoch_matches(power_generation)) {
    (void)write_register(GL30_DRV8316_REG_CTRL1, 0x06u,
                         power_generation, false, 0u);
  }
  return false;
#endif
}

bool gl30_drv8316_arm(uint32_t expected_off_generation) {
#if GL30_BUILD_ONLY
  (void)expected_off_generation;
  return false;
#else
  uint32_t primask;
  bool ready;
  bool bkin_released;
  bool brk_active;
  uint32_t expected_power_generation;
  gl30_drv8316_status_t status;

  if (__get_PRIMASK() != 0u) {
    gl30_drv8316_safe_off();
    return false;
  }
  primask = irq_save();
  expected_power_generation = g_power_generation;
  if (!g_startup_verified ||
      !startup_conditions_locked(expected_off_generation,
                                 expected_power_generation)) {
    safe_off_locked();
    irq_restore(primask);
    return false;
  }
  gl30_drv8316_set_duty(0.5f, 0.5f, 0.5f);
  LL_GPIO_ResetOutputPin(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin);
  irq_restore(primask);
  if (!config_epoch_matches(expected_off_generation,
                            expected_power_generation) ||
      !g_startup_verified) {
    return false;
  }

  gl30_timebase_delay_us(1000u);
  if (!config_epoch_matches(expected_off_generation,
                            expected_power_generation) ||
      !g_startup_verified) {
    return false;
  }
  if (!LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12)) {
    gl30_drv8316_safe_off();
    return false;
  }
  if (!read_faults_with_epoch(&status, expected_power_generation, true,
                              expected_off_generation) ||
      !status.n_fault_released || status.normalized_faults != 0u ||
      !g_startup_verified ||
      !config_epoch_matches(expected_off_generation,
                            expected_power_generation)) {
    gl30_drv8316_safe_off();
    return false;
  }

  LL_TIM_ClearFlag_BRK(TIM1);

  primask = irq_save();
  ready = g_startup_verified &&
          startup_conditions_locked(expected_off_generation,
                                    expected_power_generation) &&
          startup_drvoff_low();
  bkin_released = LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12);
  brk_active = LL_TIM_IsActiveFlag_BRK(TIM1) != 0u;
  if (!ready || !bkin_released || brk_active) {
    if (g_off_generation == expected_off_generation) {
      safe_off_locked();
    }
    irq_restore(primask);
    return false;
  }
  LL_TIM_EnableIT_BRK(TIM1);
  LL_TIM_EnableAllOutputs(TIM1);
  brk_active = LL_TIM_IsActiveFlag_BRK(TIM1) != 0u;
  if (brk_active || !LL_TIM_IsEnabledAllOutputs(TIM1)) {
    safe_off_locked();
    irq_restore(primask);
    return false;
  }
  g_output_enabled = true;
  irq_restore(primask);

  ready = g_configured &&
          g_startup_verified &&
          g_off_generation == expected_off_generation &&
          g_power_generation == expected_power_generation &&
          g_output_enabled && LL_TIM_IsEnabledAllOutputs(TIM1) &&
          LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12) &&
          LL_TIM_IsActiveFlag_BRK(TIM1) == 0u;
  if (!ready && g_off_generation == expected_off_generation) {
    gl30_drv8316_safe_off();
  }
  if (!ready || !g_configured || !g_startup_verified ||
      g_off_generation != expected_off_generation ||
      g_power_generation != expected_power_generation ||
      !g_output_enabled || !LL_TIM_IsEnabledAllOutputs(TIM1) ||
      LL_TIM_IsActiveFlag_BRK(TIM1) != 0u ||
      !LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12)) {
    if (g_off_generation == expected_off_generation) {
      gl30_drv8316_safe_off();
    }
    return false;
  }
  return true;
#endif
}

void gl30_drv8316_safe_off(void) {
#if GL30_BUILD_ONLY
  g_off_generation++;
  g_output_enabled = false;
#else
  const uint32_t primask = irq_save();
  safe_off_locked();
  irq_restore(primask);
#endif
}

void gl30_drv8316_invalidate_configuration(void) {
#if GL30_BUILD_ONLY
  g_configured = false;
  g_startup_verified = false;
  ++g_power_generation;
  ++g_off_generation;
  g_output_enabled = false;
#else
  const uint32_t primask = irq_save();
  g_configured = false;
  g_startup_verified = false;
  ++g_power_generation;
  safe_off_locked();
  LL_GPIO_SetOutputPin(DRV8316_NSS_GPIO_Port, DRV8316_NSS_Pin);
  irq_restore(primask);
#endif
}

void gl30_drv8316_set_duty(float duty_a, float duty_b, float duty_c) {
#if GL30_BUILD_ONLY
  (void)duty_a;
  (void)duty_b;
  (void)duty_c;
#else
  LL_TIM_OC_SetCompareCH1(TIM1, duty_to_ccr(duty_a));
  LL_TIM_OC_SetCompareCH2(TIM1, duty_to_ccr(duty_b));
  LL_TIM_OC_SetCompareCH3(TIM1, duty_to_ccr(duty_c));
#endif
}

bool gl30_drv8316_read_faults(gl30_drv8316_status_t *out) {
  if (out == NULL) {
    return false;
  }
  *out = (gl30_drv8316_status_t){0};
#if GL30_BUILD_ONLY
  return false;
#else
  gl30_drv8316_status_t status;
  const uint32_t power_generation = power_generation_snapshot();
  if (!read_faults_with_epoch(&status, power_generation, false, 0u)) {
    return false;
  }
  *out = status;
  return true;
#endif
}

bool gl30_drv8316_is_configured(void) {
  return g_configured;
}

bool gl30_drv8316_outputs_enabled(void) {
  return g_output_enabled;
}
