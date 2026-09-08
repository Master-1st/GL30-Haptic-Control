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

static volatile bool g_configured;
static volatile bool g_output_enabled;
static volatile uint32_t g_off_generation;
static uint8_t g_last_spi_status;

uint32_t gl30_drv8316_off_generation_snapshot(void) {
  return g_off_generation;
}

#if !GL30_BUILD_ONLY
static bool spi_timed_out(uint64_t started_us) {
  return gl30_timebase_now_us() - started_us >=
         GL30_DRV8316_SPI_TIMEOUT_US;
}

static bool transfer(uint16_t tx_word, uint8_t *status, uint8_t *data) {
  uint16_t rx_word = 0u;
  uint64_t started_us = gl30_timebase_now_us();

  if (LL_SPI_IsEnabled(SPI3) == 0u) {
    return false;
  }
  while (LL_SPI_IsActiveFlag_RXNE(SPI3) != 0u) {
    (void)LL_SPI_ReceiveData16(SPI3);
  }
  LL_SPI_ClearFlag_OVR(SPI3);
  while (LL_SPI_IsActiveFlag_TXE(SPI3) == 0u ||
         LL_SPI_IsActiveFlag_BSY(SPI3) != 0u) {
    if (spi_timed_out(started_us)) {
      return false;
    }
  }

  LL_GPIO_ResetOutputPin(DRV8316_NSS_GPIO_Port, DRV8316_NSS_Pin);
  LL_SPI_TransmitData16(SPI3, tx_word);
  while (LL_SPI_IsActiveFlag_RXNE(SPI3) == 0u) {
    if (LL_SPI_IsActiveFlag_OVR(SPI3) != 0u ||
        spi_timed_out(started_us)) {
      LL_SPI_ClearFlag_OVR(SPI3);
      LL_GPIO_SetOutputPin(DRV8316_NSS_GPIO_Port, DRV8316_NSS_Pin);
      return false;
    }
  }
  rx_word = LL_SPI_ReceiveData16(SPI3);
  while (LL_SPI_IsActiveFlag_BSY(SPI3) != 0u) {
    if (LL_SPI_IsActiveFlag_OVR(SPI3) != 0u ||
        spi_timed_out(started_us)) {
      LL_SPI_ClearFlag_OVR(SPI3);
      LL_GPIO_SetOutputPin(DRV8316_NSS_GPIO_Port, DRV8316_NSS_Pin);
      return false;
    }
  }
  LL_GPIO_SetOutputPin(DRV8316_NSS_GPIO_Port, DRV8316_NSS_Pin);
  g_last_spi_status = (uint8_t)(rx_word >> 8u);
  if (status != NULL) {
    *status = g_last_spi_status;
  }
  if (data != NULL) {
    *data = (uint8_t)rx_word;
  }
  return true;
}

static bool write_register(uint8_t address, uint8_t value) {
  return transfer(gl30_drv8316_make_frame(false, address, value), NULL, NULL);
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


void gl30_drv8316_init(void) {
  g_configured = false;
  g_output_enabled = false;
  g_last_spi_status = 0u;
  g_off_generation = 0u;
#if !GL30_BUILD_ONLY
  LL_TIM_DisableIT_BRK(TIM1);
  LL_TIM_DisableAllOutputs(TIM1);
  LL_GPIO_SetOutputPin(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin);
  LL_GPIO_SetOutputPin(DRV8316_NSS_GPIO_Port, DRV8316_NSS_Pin);
  LL_SPI_Enable(SPI3);
  gl30_drv8316_set_duty(0.5f, 0.5f, 0.5f);
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
  return transfer(gl30_drv8316_make_frame(true, address, 0u), NULL, data);
#endif
}

bool gl30_drv8316_configure(void) {
#if GL30_BUILD_ONLY
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

  g_configured = false;
  gl30_drv8316_safe_off();
  if (!write_register(GL30_DRV8316_REG_CTRL1, 0x03u)) {
    return false;
  }
  for (size_t index = 0u;
       index < sizeof(configuration) / sizeof(configuration[0]); ++index) {
    uint8_t readback = 0u;
    if (!write_register(configuration[index].address, configuration[index].value) ||
        !gl30_drv8316_read_register(configuration[index].address, &readback) ||
        readback != configuration[index].value) {
      return false;
    }
  }
  if (!write_register(GL30_DRV8316_REG_CTRL1, 0x06u)) {
    return false;
  }
  uint8_t lock_readback = 0u;
  if (!gl30_drv8316_read_register(GL30_DRV8316_REG_CTRL1, &lock_readback) ||
      (lock_readback & 0x07u) != 0x06u) {
    return false;
  }
  g_configured = true;
  return true;
#endif
}

bool gl30_drv8316_clear_faults(void) {
#if GL30_BUILD_ONLY
  return false;
#else
  if (!write_register(GL30_DRV8316_REG_CTRL1, 0x03u) ||
      !write_register(GL30_DRV8316_REG_CTRL2,
                      (uint8_t)(GL30_DRV_CTRL2_VALUE | 0x01u)) ||
      !write_register(GL30_DRV8316_REG_CTRL1, 0x06u)) {
    return false;
  }
  gl30_timebase_delay_us(1000u);
  gl30_drv8316_status_t status;
  return gl30_drv8316_read_faults(&status) && status.n_fault_released &&
         status.stat0 == 0u && status.stat1 == 0u &&
         (status.stat2 & GL30_DRV8316_STAT2_FAULT_MASK) == 0u;
#endif
}

bool gl30_drv8316_arm(uint32_t expected_off_generation) {
#if GL30_BUILD_ONLY
  (void)expected_off_generation;
  return false;
#else
  uint32_t primask;
  bool generation_match;
  bool bkin_released;
  bool brk_active;

  if (!g_configured) {
    return false;
  }
  if (g_off_generation != expected_off_generation) {
    return false;
  }
  gl30_drv8316_set_duty(0.5f, 0.5f, 0.5f);
  if (g_off_generation != expected_off_generation) {
    return false;
  }
  LL_GPIO_ResetOutputPin(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin);
  gl30_timebase_delay_us(1000u);
  if (g_off_generation != expected_off_generation) {
    return false;
  }
  if (!LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12) &&
      !gl30_drv8316_clear_faults()) {
    gl30_drv8316_safe_off();
    return false;
  }
  if (g_off_generation != expected_off_generation) {
    return false;
  }
  if (!LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12)) {
    gl30_drv8316_safe_off();
    return false;
  }

  LL_TIM_ClearFlag_BRK(TIM1);

  primask = __get_PRIMASK();
  __disable_irq();
  generation_match = (g_off_generation == expected_off_generation);
  bkin_released = LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12);
  brk_active = LL_TIM_IsActiveFlag_BRK(TIM1) != 0u;
  if (!generation_match || !bkin_released || brk_active) {
    if (primask == 0u) {
      __enable_irq();
    }
    if (generation_match) {
      gl30_drv8316_safe_off();
    }
    return false;
  }
  LL_TIM_EnableIT_BRK(TIM1);
  LL_TIM_EnableAllOutputs(TIM1);
  brk_active = LL_TIM_IsActiveFlag_BRK(TIM1) != 0u;
  if (brk_active || !LL_TIM_IsEnabledAllOutputs(TIM1)) {
    if (primask == 0u) {
      __enable_irq();
    }
    gl30_drv8316_safe_off();
    return false;
  }
  g_output_enabled = true;
  if (primask == 0u) {
    __enable_irq();
  }
  return true;
#endif
}

void gl30_drv8316_safe_off(void) {
  g_off_generation++;
  g_output_enabled = false;
#if !GL30_BUILD_ONLY
  LL_GPIO_SetOutputPin(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin);
  LL_TIM_DisableIT_BRK(TIM1);
  LL_TIM_DisableAllOutputs(TIM1);
  LL_TIM_OC_SetCompareCH1(TIM1, GL30_TIM1_ARR / 2u);
  LL_TIM_OC_SetCompareCH2(TIM1, GL30_TIM1_ARR / 2u);
  LL_TIM_OC_SetCompareCH3(TIM1, GL30_TIM1_ARR / 2u);
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
  if (!gl30_drv8316_read_register(GL30_DRV8316_REG_STAT0, &out->stat0) ||
      !gl30_drv8316_read_register(GL30_DRV8316_REG_STAT1, &out->stat1) ||
      !gl30_drv8316_read_register(GL30_DRV8316_REG_STAT2, &out->stat2)) {
    return false;
  }
  out->spi_status = g_last_spi_status;
  out->n_fault_released =
      LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12);
  out->configured = g_configured;
  out->output_enabled = g_output_enabled;
  return true;
#endif
}

bool gl30_drv8316_is_configured(void) {
  return g_configured;
}

bool gl30_drv8316_outputs_enabled(void) {
  return g_output_enabled;
}
