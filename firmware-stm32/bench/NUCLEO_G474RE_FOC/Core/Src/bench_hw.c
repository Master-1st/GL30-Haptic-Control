#if defined(GL30_FOC_USE_CORDIC)
#include "bench_cordic.h"
#endif
#include "bench_hw.h"
#include "bench_app.h"
#include "main.h"
#include "as5048a.h"
#include "drv8316.h"
#include "stm32g4xx_ll_iwdg.h"
#include <math.h>
#include <stddef.h>

#define PWM_CHANNELS (LL_TIM_CHANNEL_CH1 | LL_TIM_CHANNEL_CH1N | \
                      LL_TIM_CHANNEL_CH2 | LL_TIM_CHANNEL_CH2N | \
                      LL_TIM_CHANNEL_CH3 | LL_TIM_CHANNEL_CH3N)

#define DRV8316REVM_CTRL6_BUCK_3V3 (0x10u)

static const uint8_t g_drv_config_regs[][2] = {
  {4u, 0x68u}, {5u, 0x5fu}, {6u, 0x10u}, {7u, 0x02u},
  {8u, DRV8316REVM_CTRL6_BUCK_3V3}, {12u, 0x00u}
};

#define BENCH_TIM1_PWM_PINS_A (LL_GPIO_PIN_8 | LL_GPIO_PIN_9 | LL_GPIO_PIN_10)
#define BENCH_TIM1_PWM_PINS_B (LL_GPIO_PIN_13 | LL_GPIO_PIN_14 | LL_GPIO_PIN_15)

static bench_drv_diag_t g_drv_diag;
static bench_drv_poll_diag_t g_drv_poll_diag;
static bench_spi_error_t g_spi_error;
static volatile bench_enc_diag_t g_enc_first, g_enc_latest;
static bench_enc_clear_diag_t g_enc_clear;
static uint32_t g_enc_samples;
static volatile bool g_tim1_pwm_pins_parked;
static bench_arm_diag_t g_arm_diag;

static bool bench_hw_pwm_channels_disabled(void)
{
  return !LL_TIM_CC_IsEnabledChannel(TIM1, LL_TIM_CHANNEL_CH1) &&
         !LL_TIM_CC_IsEnabledChannel(TIM1, LL_TIM_CHANNEL_CH1N) &&
         !LL_TIM_CC_IsEnabledChannel(TIM1, LL_TIM_CHANNEL_CH2) &&
         !LL_TIM_CC_IsEnabledChannel(TIM1, LL_TIM_CHANNEL_CH2N) &&
         !LL_TIM_CC_IsEnabledChannel(TIM1, LL_TIM_CHANNEL_CH3) &&
         !LL_TIM_CC_IsEnabledChannel(TIM1, LL_TIM_CHANNEL_CH3N);
}

static void bench_hw_restore_pwm_alternate_mode(void)
{
  LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_8, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_9, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_10, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_13, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_14, LL_GPIO_MODE_ALTERNATE);
  LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_15, LL_GPIO_MODE_ALTERNATE);
  g_tim1_pwm_pins_parked = false;
}

uint32_t bench_now_us(void) { return LL_TIM_GetCounter(TIM2); }

void bench_delay_us(uint32_t duration)
{
  const uint32_t start = bench_now_us();
  while ((uint32_t)(bench_now_us() - start) < duration) { }
}

void bench_hw_idle(void)
{
  LL_TIM_DisableAllOutputs(TIM1);
  LL_TIM_CC_DisableChannel(TIM1, PWM_CHANNELS);
  LL_TIM_DisableIT_BRK(TIM1);
  LL_GPIO_ResetOutputPin(GPIOA, BENCH_TIM1_PWM_PINS_A);
  LL_GPIO_ResetOutputPin(GPIOB, BENCH_TIM1_PWM_PINS_B);
  if (g_tim1_pwm_pins_parked) { return; }
  LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_8, LL_GPIO_MODE_OUTPUT);
  LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_9, LL_GPIO_MODE_OUTPUT);
  LL_GPIO_SetPinMode(GPIOA, LL_GPIO_PIN_10, LL_GPIO_MODE_OUTPUT);
  LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_13, LL_GPIO_MODE_OUTPUT);
  LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_14, LL_GPIO_MODE_OUTPUT);
  LL_GPIO_SetPinMode(GPIOB, LL_GPIO_PIN_15, LL_GPIO_MODE_OUTPUT);
  LL_GPIO_SetPinOutputType(GPIOA, BENCH_TIM1_PWM_PINS_A, LL_GPIO_OUTPUT_PUSHPULL);
  LL_GPIO_SetPinOutputType(GPIOB, BENCH_TIM1_PWM_PINS_B, LL_GPIO_OUTPUT_PUSHPULL);
  g_tim1_pwm_pins_parked = true;
}

void bench_hw_off(void)
{
  LL_GPIO_SetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin);
  bench_hw_idle();
}

bool bench_hw_button(void)
{
  return LL_GPIO_IsInputPinSet(USER_BUTTON_GPIO_Port, USER_BUTTON_Pin) != 0u;
}

bool bench_hw_nfault(void)
{
  return LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12) != 0u;
}

static bool adc_start(ADC_TypeDef *adc)
{
  LL_ADC_DisableDeepPowerDown(adc);
  LL_ADC_EnableInternalRegulator(adc);
  bench_delay_us(25u);
  LL_ADC_StartCalibration(adc, LL_ADC_SINGLE_ENDED);
  uint32_t start = bench_now_us();
  while (LL_ADC_IsCalibrationOnGoing(adc)) {
    if ((uint32_t)(bench_now_us() - start) > 2000u) { return false; }
  }
  bench_delay_us(2u);
  LL_ADC_ClearFlag_ADRDY(adc);
  LL_ADC_Enable(adc);
  start = bench_now_us();
  while (!LL_ADC_IsActiveFlag_ADRDY(adc)) {
    if ((uint32_t)(bench_now_us() - start) > 2000u) { return false; }
  }
  LL_ADC_ClearFlag_JEOS(adc);
  LL_ADC_INJ_StartConversion(adc);
  return true;
}

bool bench_hw_init(void)
{
  LL_TIM_EnableCounter(TIM2);
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  bench_hw_off();
  LL_GPIO_ResetOutputPin(DRV_NSLEEP_GPIO_Port, DRV_NSLEEP_Pin);
#if defined(GL30_FOC_USE_CORDIC)
  if (!bench_cordic_startup_test()) { return false; }
#endif
  LL_TIM_EnableBreakInputSource(TIM1, LL_TIM_BREAK_INPUT_BKIN, LL_TIM_BKIN_SOURCE_BKIN);
  LL_SPI_Enable(SPI1);
  LL_SPI_Enable(SPI3);
  if (!adc_start(ADC1) || !adc_start(ADC2) || !adc_start(ADC3)) { return false; }
  LL_ADC_EnableIT_JEOS(ADC1);
  LL_TIM_GenerateEvent_UPDATE(TIM1);
  LL_TIM_EnableCounter(TIM1);
  LL_TIM_ClearFlag_UPDATE(TIM6);
  LL_TIM_EnableIT_UPDATE(TIM6);
  LL_TIM_EnableCounter(TIM6);
  return true;
}

static bool spi_failure(SPI_TypeDef *spi, uint32_t start, uint32_t stage)
{
  g_spi_error.stage = stage;
  g_spi_error.elapsed_us = bench_now_us() - start;
  g_spi_error.sr = spi->SR;
  return false;
}

/* TIM1: 160MHz, centre-aligned ARR=4000; ADC triggers at CNT=3900.
 * Keep DRV edges near bottom (+/-2.5us). The 1us CS setup and 6.4us
 * SPI3 word then finish well before the next ADC trigger. Waiting never
 * masks IRQs; only the phase recheck and CS/start or CS/release are atomic.
 * SPI3 callers are foreground-only; SPI1 encoder IRQ is not scheduled here. */
static bool drv_spi_quiet_lock(uint32_t start, uint32_t *mask)
{
  for (;;) {
    if ((uint32_t)(bench_now_us() - start) > 150u) { return false; }
    *mask = __get_PRIMASK();
    __disable_irq();
    if (LL_TIM_GetCounter(TIM1) < 400u) { return true; }
    __set_PRIMASK(*mask);
  }
}

static bool spi_word(SPI_TypeDef *spi, GPIO_TypeDef *port, uint32_t pin,
                     uint16_t tx, uint16_t *rx)
{
  uint32_t start = bench_now_us();
  /* SPI3: 16 bits at 2.5MHz plus quiet-window/foreground IRQ waits.
   * The encoder IRQ keeps its existing 30us bound. */
  const uint32_t timeout_us = (spi == SPI3) ? 150u : 30u;
  g_spi_error = (bench_spi_error_t){0};
  /* Bounded drain: one outstanding word at most, FIFO can hold two 16b words. */
  for (unsigned int i = 0; i < 4u && LL_SPI_IsActiveFlag_RXNE(spi); ++i) {
    (void)LL_SPI_ReceiveData16(spi);
  }
  LL_SPI_ClearFlag_OVR(spi);
  while (!LL_SPI_IsActiveFlag_TXE(spi) || LL_SPI_IsActiveFlag_BSY(spi)) {
    if ((uint32_t)(bench_now_us() - start) > timeout_us) { return spi_failure(spi, start, 1u); }
  }
  uint32_t mask = 0u;
  if (spi == SPI3 && !drv_spi_quiet_lock(start, &mask)) {
    bench_hw_off();
    return spi_failure(spi, start, 5u);
  }
  LL_GPIO_ResetOutputPin(port, pin);
  bench_delay_us(1u); /* AS5048A tL >=350ns; TI/AS minimum CS high also covered. */
  LL_SPI_TransmitData16(spi, tx);
  if (spi == SPI3) { __set_PRIMASK(mask); }
  while (!LL_SPI_IsActiveFlag_RXNE(spi)) {
    if ((uint32_t)(bench_now_us() - start) > timeout_us || LL_SPI_IsActiveFlag_OVR(spi)) {
      (void)spi_failure(spi, start, LL_SPI_IsActiveFlag_OVR(spi) ? 3u : 2u);
      LL_GPIO_SetOutputPin(port, pin);
      return false;
    }
  }
  *rx = LL_SPI_ReceiveData16(spi);
  while (LL_SPI_IsActiveFlag_BSY(spi)) {
    if ((uint32_t)(bench_now_us() - start) > timeout_us) {
      (void)spi_failure(spi, start, 4u);
      LL_GPIO_SetOutputPin(port, pin);
      return false;
    }
  }
  bench_delay_us(1u);
  if (spi == SPI3 && !drv_spi_quiet_lock(start, &mask)) {
    /* A stuck phase must not strand CS or leave the bridge active. Fault
     * cleanup takes priority over the normal analogue quiet-window rule. */
    bench_hw_off();
    LL_GPIO_SetOutputPin(port, pin);
    return spi_failure(spi, start, 5u);
  }
  LL_GPIO_SetOutputPin(port, pin);
  if (spi == SPI3) { __set_PRIMASK(mask); }
  bench_delay_us(1u);
  return true;
}

static bool drv_register(bool read, uint8_t address, uint8_t value, uint16_t *rx)
{
  g_drv_diag.tx = gl30_drv8316_make_frame(read, address, value);
  g_drv_diag.rx = 0u;
  g_drv_diag.transfer_ok = spi_word(SPI3, DRV_CS_GPIO_Port, DRV_CS_Pin,
                                   g_drv_diag.tx, &g_drv_diag.rx);
  *rx = g_drv_diag.rx;
  return g_drv_diag.transfer_ok;
}

void bench_hw_driver_diagnostics(bench_drv_diag_t *out)
{
  /* Driver SPI and this snapshot are both used only by the main loop. */
  *out = g_drv_diag;
}

void bench_hw_driver_poll_diagnostics(bench_drv_poll_diag_t *out)
{
  /* Both the poll and this getter run only in the main loop. */
  *out = g_drv_poll_diag;
}

static void drv_capture_state(bench_drv_state_t *out)
{
  /* Bounded register/ADC copy only: no SPI, printing, polling or delay.
   * During a periodic poll an IRQ may already have stopped the outputs. */
  const uint32_t mask = __get_PRIMASK();
  __disable_irq();
  out->time_us = bench_now_us();
  out->gpioa_idr = GPIOA->IDR;
  out->gpioa_odr = GPIOA->ODR;
  out->gpiob_idr = GPIOB->IDR;
  out->gpiob_odr = GPIOB->ODR;
  out->gpioc_idr = GPIOC->IDR;
  out->gpioc_odr = GPIOC->ODR;
  out->pb12_pupdr = GPIOB->PUPDR;
  out->tim1_ccer = TIM1->CCER;
  out->nfault = (out->gpiob_idr & LL_GPIO_PIN_12) ? 1u : 0u;
  out->nsleep = (out->gpioc_idr & DRV_NSLEEP_Pin) ? 1u : 0u;
  out->drvoff = (out->gpioc_idr & DRV_OFF_Pin) ? 1u : 0u;
  out->moe = LL_TIM_IsEnabledAllOutputs(TIM1) ? 1u : 0u;
  Bench_AdcDiagnostics(&out->adc);
  out->valid = 1u;
  __set_PRIMASK(mask);
}

static bench_drv_reason_t drv_status_reason(uint32_t status)
{
  return status ? BENCH_DRV_REASON_STATUS_FLAGS : BENCH_DRV_REASON_NONE;
}

static bool drv_status_words(uint32_t *status)
{
  uint32_t flags = 0u;
  g_drv_diag.raw_status = 0u;
  g_drv_diag.status_read_mask = 0u;
  g_drv_diag.normalized_status = 0u;
  g_drv_diag.status_rx[0] = 0u;
  g_drv_diag.status_rx[1] = 0u;
  g_drv_diag.status_rx[2] = 0u;
  g_drv_diag.status_us[0] = 0u;
  g_drv_diag.status_us[1] = 0u;
  g_drv_diag.status_us[2] = 0u;
  for (uint8_t address = 0u; address < 3u; ++address) {
    uint16_t rx = 0u;
    if (!drv_register(true, address, 0u, &rx)) { return false; }
    g_drv_diag.raw_status |= ((uint32_t)(rx & 0xffu)) << (address * 8u);
    g_drv_diag.status_read_mask |= (uint8_t)(1u << address);
    g_drv_diag.status_rx[address] = rx;
    g_drv_diag.status_us[address] = bench_now_us();
    flags |= gl30_drv8316_status_word_faults(address, rx);
    g_drv_diag.normalized_status = flags;
  }
  g_drv_diag.normalized_status = flags;
  *status = flags;
  return true;
}

bool bench_hw_driver_snapshot(bench_drv_diag_t *out)
{
  if (out == NULL) { return false; }
  *out = (bench_drv_diag_t){0};
  if (bench_hw_button() || LL_TIM_IsEnabledAllOutputs(TIM1) ||
      LL_TIM_CC_IsEnabledChannel(TIM1, LL_TIM_CHANNEL_CH1) ||
      LL_TIM_CC_IsEnabledChannel(TIM1, LL_TIM_CHANNEL_CH1N) ||
      LL_TIM_CC_IsEnabledChannel(TIM1, LL_TIM_CHANNEL_CH2) ||
      LL_TIM_CC_IsEnabledChannel(TIM1, LL_TIM_CHANNEL_CH2N) ||
      LL_TIM_CC_IsEnabledChannel(TIM1, LL_TIM_CHANNEL_CH3) ||
      LL_TIM_CC_IsEnabledChannel(TIM1, LL_TIM_CHANNEL_CH3N) ||
      !LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin) ||
      !LL_GPIO_IsOutputPinSet(DRV_NSLEEP_GPIO_Port, DRV_NSLEEP_Pin)) {
    return false;
  }
  /* Main-loop only. Preserve the original configure failure for DRV_DIAG. */
  const bench_drv_diag_t configure_backup = g_drv_diag;
  uint32_t status = 0u;
  g_drv_diag = (bench_drv_diag_t){0};
  g_drv_diag.stage = BENCH_DRV_STAGE_STATUS_PRE;
  const bool complete = drv_status_words(&status);
  g_drv_diag.reason = !complete ? BENCH_DRV_REASON_TRANSPORT :
      drv_status_reason(status);
  *out = g_drv_diag;
  g_drv_diag = configure_backup;
  return complete;
}

bool bench_hw_driver_status(uint32_t *status)
{
  const bench_drv_diag_t configure_backup = g_drv_diag;
  bench_drv_state_t entry = {0};
  drv_capture_state(&entry);
  bool ok = false;
  g_drv_diag = (bench_drv_diag_t){0};
  g_drv_diag.stage = BENCH_DRV_STAGE_STATUS_FINAL;
  if (!drv_status_words(status)) {
    g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
    g_drv_diag.expected = 0u;
    goto done;
  }
  g_drv_diag.normalized_status = *status;
  if (*status != 0u) {
    g_drv_diag.reason = drv_status_reason(*status);
    g_drv_diag.expected = 0x00u;
    goto done;
  }
  uint16_t gain = 0u, buck = 0u;
  g_drv_diag.stage = BENCH_DRV_STAGE_FINAL_GAIN;
  if (!drv_register(true, 7u, 0u, &gain)) {
    g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
    g_drv_diag.expected = 0x02u;
    goto done;
  }
  if ((uint8_t)gain != 0x02u) {
    g_drv_diag.reason = BENCH_DRV_REASON_READBACK;
    g_drv_diag.expected = 0x02u;
    goto done;
  }
  g_drv_diag.stage = BENCH_DRV_STAGE_FINAL_BUCK;
  if (!drv_register(true, 8u, 0u, &buck)) {
    g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
    g_drv_diag.expected = DRV8316REVM_CTRL6_BUCK_3V3;
    goto done;
  }
  if ((uint8_t)buck != DRV8316REVM_CTRL6_BUCK_3V3) {
    g_drv_diag.reason = BENCH_DRV_REASON_READBACK;
    g_drv_diag.expected = DRV8316REVM_CTRL6_BUCK_3V3;
    goto done;
  }
  if (!bench_hw_nfault()) {
    g_drv_diag.reason = BENCH_DRV_REASON_NFAULT;
    g_drv_diag.expected = 1u;
    goto done;
  }
  g_drv_diag.reason = BENCH_DRV_REASON_NONE;
  g_drv_diag.expected = 0u;
  g_drv_diag.stage = BENCH_DRV_STAGE_READY;
  ok = true;

done:
  if (!ok && !g_drv_poll_diag.valid) {
    g_drv_poll_diag.diag = g_drv_diag;
    g_drv_poll_diag.entry = entry;
    drv_capture_state(&g_drv_poll_diag.result);
    g_drv_poll_diag.valid = true;
  }
  g_drv_diag = configure_backup;
  return ok;
}

bool bench_hw_driver_configure(void)
{
  /* TI SLVSF16B Tables 8-5/8-23: CTRL6=0x10 for the confirmed EVM L1=47uH
   * and 3.3V (BUCK_PS_DIS=1, BUCK_CL=0, SEL=0, DIS=0).
   * Keep the buck enabled: this EVM's REF3130 depends on its supply. */
  bench_drv_diag_t first_failure = (bench_drv_diag_t){0};
  uint16_t rx = 0u;
  uint32_t status = 0u;
  bool unlock_attempted = false;
  bool relock_ok = false;
  uint16_t relock_rx = 0u;
  g_drv_diag = (bench_drv_diag_t){0};
  g_drv_poll_diag = (bench_drv_poll_diag_t){0};
  bench_hw_off();
  LL_GPIO_ResetOutputPin(DRV_NSLEEP_GPIO_Port, DRV_NSLEEP_Pin);
  bench_delay_us(2000u);
  bench_hw_idle();
  LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin);
  LL_GPIO_SetOutputPin(DRV_NSLEEP_GPIO_Port, DRV_NSLEEP_Pin);
  bench_delay_us(10000u);
  g_drv_diag.stage = BENCH_DRV_STAGE_UNLOCK;
  unlock_attempted = true;
  if (!drv_register(false, 3u, 3u, &rx)) {
    g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
    g_drv_diag.expected = 0x03u;
    g_drv_diag.relock_attempted = 0u;
    goto cleanup;
  }
  for (size_t i = 0u; i < sizeof(g_drv_config_regs) / sizeof(g_drv_config_regs[0]); ++i) {
    const uint8_t *reg = g_drv_config_regs[i];
    g_drv_diag.stage = BENCH_DRV_STAGE_CONFIG_WRITE;
    if (!drv_register(false, reg[0], reg[1], &rx)) {
      g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
      g_drv_diag.expected = reg[1];
      goto cleanup;
    }
    g_drv_diag.stage = BENCH_DRV_STAGE_CONFIG_READ;
    if (!drv_register(true, reg[0], 0u, &rx)) {
      g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
      g_drv_diag.expected = reg[1];
      goto cleanup;
    }
    if ((uint8_t)rx != reg[1]) {
      g_drv_diag.reason = BENCH_DRV_REASON_READBACK;
      g_drv_diag.expected = reg[1];
      goto cleanup;
    }
    g_drv_diag.config_read_mask |= (uint8_t)(1u << i);
  }
  /* A full sleep/wake leaves NPOR latched low. Acknowledge only that
   * startup indication once, after reading all fault words. Never clear
   * other fault indications or retry automatically.
   * TI Table 8-19: CLR_FLT is bit 0 and self-clears; 0x68 is unchanged. */
  g_drv_diag.stage = BENCH_DRV_STAGE_STATUS_PRE;
  if (!drv_status_words(&status)) {
    g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
    g_drv_diag.expected = 0u;
    goto cleanup;
  }
  g_drv_diag.normalized_status = status;
  if ((status & ~0x08u) != 0u) {
    g_drv_diag.reason = drv_status_reason(status);
    g_drv_diag.expected = 0x00u;
    goto cleanup;
  }
  if (!bench_hw_nfault()) {
    g_drv_diag.reason = BENCH_DRV_REASON_NFAULT;
    g_drv_diag.expected = 1u;
    goto cleanup;
  }
  if ((status & 0x08u) != 0u) {
    g_drv_diag.stage = BENCH_DRV_STAGE_CLEAR_WRITE;
    if (!drv_register(false, 4u, 0x69u, &rx)) {
      g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
      g_drv_diag.expected = 0x69u;
      goto cleanup;
    }
    g_drv_diag.stage = BENCH_DRV_STAGE_CLEAR_READ;
    if (!drv_register(true, 4u, 0u, &rx)) {
      g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
      g_drv_diag.expected = 0x68u;
      goto cleanup;
    }
    if ((uint8_t)rx != 0x68u) {
      g_drv_diag.reason = BENCH_DRV_REASON_READBACK;
      g_drv_diag.expected = 0x68u;
      goto cleanup;
    }
  }
  g_drv_diag.stage = BENCH_DRV_STAGE_LOCK_WRITE;
  if (!drv_register(false, 3u, 6u, &rx)) {
    g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
    g_drv_diag.expected = 0x06u;
    goto cleanup;
  }
  g_drv_diag.stage = BENCH_DRV_STAGE_LOCK_READ;
  if (!drv_register(true, 3u, 0u, &rx)) {
    g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
    g_drv_diag.expected = 0x06u;
    goto cleanup;
  }
  if ((uint8_t)(rx & 0x07u) != 0x06u) {
    g_drv_diag.reason = BENCH_DRV_REASON_READBACK;
    g_drv_diag.expected = 0x06u;
    goto cleanup;
  }
  g_drv_diag.stage = BENCH_DRV_STAGE_STATUS_FINAL;
  if (!drv_status_words(&status)) {
    g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
    g_drv_diag.expected = 0u;
    goto cleanup;
  }
  g_drv_diag.normalized_status = status;
  if (status != 0u) {
    g_drv_diag.reason = drv_status_reason(status);
    g_drv_diag.expected = 0x00u;
    goto cleanup;
  }
  if (!bench_hw_nfault()) {
    g_drv_diag.reason = BENCH_DRV_REASON_NFAULT;
    g_drv_diag.expected = 1u;
    goto cleanup;
  }
  g_drv_diag.stage = BENCH_DRV_STAGE_FINAL_GAIN;
  if (!drv_register(true, 7u, 0u, &rx)) {
    g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
    g_drv_diag.expected = 0x02u;
    goto cleanup;
  }
  if ((uint8_t)rx != 0x02u) {
    g_drv_diag.reason = BENCH_DRV_REASON_READBACK;
    g_drv_diag.expected = 0x02u;
    goto cleanup;
  }
  g_drv_diag.stage = BENCH_DRV_STAGE_FINAL_BUCK;
  if (!drv_register(true, 8u, 0u, &rx)) {
    g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
    g_drv_diag.expected = DRV8316REVM_CTRL6_BUCK_3V3;
    goto cleanup;
  }
  if ((uint8_t)rx != DRV8316REVM_CTRL6_BUCK_3V3) {
    g_drv_diag.reason = BENCH_DRV_REASON_READBACK;
    g_drv_diag.expected = DRV8316REVM_CTRL6_BUCK_3V3;
    goto cleanup;
  }
  g_drv_diag.stage = BENCH_DRV_STAGE_READY;
  g_drv_diag.reason = BENCH_DRV_REASON_NONE;
  g_drv_diag.expected = 0u;
  return true;

cleanup:
  drv_capture_state(&g_drv_diag.pre_cleanup);
  bench_hw_off();
  drv_capture_state(&g_drv_diag.post_cleanup);
  first_failure = g_drv_diag;
  if (unlock_attempted) {
    relock_ok = drv_register(false, 3u, 6u, &relock_rx);
    if (relock_ok) { relock_ok = drv_register(true, 3u, 0u, &relock_rx); }
  }
  if (unlock_attempted) {
    relock_ok = relock_ok && ((uint8_t)(relock_rx & 0x07u) == 0x06u);
  } else {
    relock_ok = false;
    relock_rx = 0u;
  }
  first_failure.relock_attempted = unlock_attempted ? 1u : 0u;
  first_failure.relock_ok = relock_ok ? 1u : 0u;
  first_failure.relock_rx = relock_rx;
  g_drv_diag = first_failure;
  return false;
}

static bool drv_trace_safe(void)
{
  if (!Bench_DriverTraceSafe() || bench_hw_button() ||
      LL_TIM_IsEnabledAllOutputs(TIM1) || !bench_hw_pwm_channels_disabled() ||
      !g_tim1_pwm_pins_parked ||
      (GPIOA->ODR & BENCH_TIM1_PWM_PINS_A) ||
      (GPIOB->ODR & BENCH_TIM1_PWM_PINS_B)) {
    g_drv_diag.reason = BENCH_DRV_REASON_GUARD;
    return false;
  }
  return true;
}

static bool drv_trace_register(bool read, uint8_t reg, uint8_t value, uint16_t *rx)
{
  if (!drv_trace_safe()) { return false; }
  if (!drv_register(read, reg, value, rx)) {
    g_drv_diag.reason = BENCH_DRV_REASON_TRANSPORT;
    return false;
  }
  return drv_trace_safe();
}

static bool drv_trace_point(bench_drv_trace_t *out, bench_drv_trace_phase_t phase, uint8_t reg)
{
  if (!drv_trace_safe() || out->count >= BENCH_DRV_TRACE_POINTS) { return false; }
  uint32_t status = 0u;
  g_drv_diag.stage = phase == BENCH_DRV_TRACE_CLEAR ?
      BENCH_DRV_STAGE_STATUS_FINAL : BENCH_DRV_STAGE_STATUS_PRE;
  g_drv_diag.expected = 0u;
  const bool complete = drv_status_words(&status);
  g_drv_diag.reason = complete ? drv_status_reason(status) : BENCH_DRV_REASON_TRANSPORT;
  bench_drv_trace_point_t *point = &out->points[out->count++];
  point->phase = phase;
  point->reg = reg;
  point->mask = g_drv_diag.status_read_mask;
  point->normalized_status = g_drv_diag.normalized_status;
  for (unsigned int i = 0u; i < 3u; ++i) {
    point->rx[i] = g_drv_diag.status_rx[i];
    point->us[i] = g_drv_diag.status_us[i];
  }
  drv_capture_state(&point->state);
  if (!out->first_issue && (!complete || (status & ~0x08u))) {
    out->first_issue = out->count;
  }
  if (!complete || !drv_trace_safe()) { return false; }
  if (!bench_hw_nfault()) {
    g_drv_diag.reason = BENCH_DRV_REASON_NFAULT;
    return false;
  }
  return true;
}

bool bench_hw_driver_trace(bench_drv_trace_t *out)
{
  if (out == NULL) { return false; }
  *out = (bench_drv_trace_t){0};
  /* Reject before any GPIO or SPI change. The caller has explicitly attested
   * motor isolation; software cannot detect whether phases are disconnected. */
  if (!Bench_DriverTraceSafe() || bench_hw_button() ||
      LL_TIM_IsEnabledAllOutputs(TIM1) || !bench_hw_pwm_channels_disabled() ||
      !LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin)) {
    out->last.reason = BENCH_DRV_REASON_GUARD;
    return false;
  }
  const bench_drv_diag_t saved = g_drv_diag;
  bool unlock_attempted = false, complete = false;
  uint8_t ctrl2 = 0x60u; /* TI Table 8-19 reset value, including reserved bits. */
  uint16_t rx = 0u;
  g_drv_diag = (bench_drv_diag_t){0};
  bench_hw_off();
  LL_GPIO_ResetOutputPin(DRV_NSLEEP_GPIO_Port, DRV_NSLEEP_Pin);
  bench_delay_us(2000u);
  /* Do not release an IRQ hard stop between the guard and wake GPIO writes. */
  const uint32_t wake_mask = __get_PRIMASK();
  __disable_irq();
  const bool wake_safe = drv_trace_safe();
  if (wake_safe) {
    LL_GPIO_ResetOutputPin(DRV_OFF_GPIO_Port, DRV_OFF_Pin);
    LL_GPIO_SetOutputPin(DRV_NSLEEP_GPIO_Port, DRV_NSLEEP_Pin);
  }
  __set_PRIMASK(wake_mask);
  if (!wake_safe) { goto done; }
  bench_delay_us(10000u);
  if (!drv_trace_point(out, BENCH_DRV_TRACE_WAKE, 0u)) { goto done; }
  if (g_drv_diag.normalized_status & ~0x08u) { goto done; }

  g_drv_diag.stage = BENCH_DRV_STAGE_UNLOCK;
  unlock_attempted = true;
  if (!drv_trace_register(false, 3u, 3u, &rx) ||
      !drv_trace_register(true, 3u, 0u, &rx)) { goto done; }
  if ((uint8_t)rx != 3u) {
    g_drv_diag.reason = BENCH_DRV_REASON_READBACK;
    g_drv_diag.expected = 3u;
    goto done;
  }
  if (!drv_trace_point(out, BENCH_DRV_TRACE_UNLOCK, 3u)) { goto done; }
  if (g_drv_diag.normalized_status & ~0x08u) { goto done; }
  for (size_t i = 0u; i < sizeof(g_drv_config_regs) / sizeof(g_drv_config_regs[0]); ++i) {
    const uint8_t *reg = g_drv_config_regs[i];
    g_drv_diag.stage = BENCH_DRV_STAGE_CONFIG_WRITE;
    g_drv_diag.expected = reg[1];
    if (!drv_trace_register(false, reg[0], reg[1], &rx)) { goto done; }
    g_drv_diag.stage = BENCH_DRV_STAGE_CONFIG_READ;
    if (!drv_trace_register(true, reg[0], 0u, &rx)) { goto done; }
    if ((uint8_t)rx != reg[1]) {
      g_drv_diag.reason = BENCH_DRV_REASON_READBACK;
      goto done;
    }
    if (reg[0] == 4u) { ctrl2 = reg[1]; }
    g_drv_diag.config_read_mask |= (uint8_t)(1u << i);
    if (!drv_trace_point(out, BENCH_DRV_TRACE_CONFIG, reg[0])) { goto done; }
    if (g_drv_diag.normalized_status & ~0x08u) { goto done; }
  }

  /* Acknowledge only startup NPOR once. STAT2.7 is excluded by the shared
   * decoder; raw words remain unchanged. Never clear a protection fault or
   * grant readiness from this diagnostic sequence. */
  if (g_drv_diag.normalized_status == 0u) { complete = true; goto done; }
  g_drv_diag.stage = BENCH_DRV_STAGE_CLEAR_READ;
  g_drv_diag.expected = ctrl2;
  if (!drv_trace_register(true, 4u, 0u, &rx)) { goto done; }
  if ((uint8_t)rx != ctrl2) { g_drv_diag.reason = BENCH_DRV_REASON_READBACK; goto done; }
  /* nFAULT must still be released immediately before the single reset write. */
  if (!bench_hw_nfault()) { g_drv_diag.reason = BENCH_DRV_REASON_NFAULT; goto done; }
  g_drv_diag.stage = BENCH_DRV_STAGE_CLEAR_WRITE;
  out->clear_attempted = 1u;
  if (!drv_trace_register(false, 4u, (uint8_t)(ctrl2 | 1u), &rx)) { goto done; }
  bench_delay_us(1000u);
  g_drv_diag.stage = BENCH_DRV_STAGE_CLEAR_READ;
  if (!drv_trace_register(true, 4u, 0u, &rx)) { goto done; }
  if ((uint8_t)rx != ctrl2) { g_drv_diag.reason = BENCH_DRV_REASON_READBACK; goto done; }
  complete = drv_trace_point(out, BENCH_DRV_TRACE_CLEAR, 4u);

done:
  drv_capture_state(&g_drv_diag.pre_cleanup);
  bench_hw_off();
  drv_capture_state(&g_drv_diag.post_cleanup);
  out->last = g_drv_diag;
  if (unlock_attempted) {
    out->last.relock_attempted = 1u;
    bool locked = drv_trace_register(false, 3u, 6u, &rx);
    if (locked) { locked = drv_trace_register(true, 3u, 0u, &rx); }
    out->last.relock_rx = rx;
    out->last.relock_ok = locked && ((uint8_t)rx == 6u);
    complete = complete && out->last.relock_ok;
  }
  g_drv_diag = saved; /* Existing DRV_DIAG remains the PREPARE failure cache. */
  return complete;
}

void bench_hw_encoder_clear(void)
{
  /* Explicit command only. Preserve the pipelined error reply before clearing.
   * EF may still be set in this reply: report raw data, do not discard via decode. */
  const uint32_t count = g_enc_clear.count + 1u;
  g_enc_clear = (bench_enc_clear_diag_t){0};
  g_enc_clear.count = count;
  if (spi_word(SPI1, ENC_CS_GPIO_Port, ENC_CS_Pin,
               as5048a_read_command(AS5048A_REG_ERROR), &g_enc_clear.previous)) {
    g_enc_clear.transfer_mask = 1u;
    if (spi_word(SPI1, ENC_CS_GPIO_Port, ENC_CS_Pin, 0u, &g_enc_clear.error)) {
      g_enc_clear.transfer_mask = 3u;
      /* The previous first failure remains available until this explicit action. */
      g_enc_first = (bench_enc_diag_t){0};
    }
  }
  g_enc_clear.spi = g_spi_error;
}

bool bench_hw_encoder_field(bench_enc_field_diag_t *out)
{
  if (out == NULL) { return false; }
  *out = (bench_enc_field_diag_t){0};
  const uint16_t commands[4] = {as5048a_read_command(AS5048A_REG_ANGLE),
      as5048a_read_command(AS5048A_REG_DIAG),
      as5048a_read_command(AS5048A_REG_MAG), 0u};
  for (unsigned int i = 0u; i < 4u; ++i) {
    if (!spi_word(SPI1, ENC_CS_GPIO_Port, ENC_CS_Pin, commands[i], &out->raw[i])) {
      out->spi = g_spi_error;
      break;
    }
    out->transfer_mask |= (uint8_t)(1u << i);
  }
  /* The first reply belongs to the previous request. Read flags even when
   * the magnetic diagnostics are bad; this command never grants readiness. */
  if ((out->transfer_mask & 2u) && as5048a_decode(out->raw[1], &out->angle)) {
    out->valid_mask |= 1u;
  }
  if ((out->transfer_mask & 4u) && as5048a_decode(out->raw[2], &out->diagnostics)) {
    out->valid_mask |= 2u;
  }
  if ((out->transfer_mask & 8u) && as5048a_decode(out->raw[3], &out->magnitude)) {
    out->valid_mask |= 4u;
  }
  out->time_us = bench_now_us();
  return out->valid_mask == 7u;
}

void bench_hw_encoder_diagnostics(bench_enc_diag_t *first, bench_enc_diag_t *latest,
                                  bench_enc_clear_diag_t *clear)
{
  *first = g_enc_first;
  *latest = g_enc_latest;
  *clear = g_enc_clear;
}

bool bench_hw_encoder_read(uint16_t *angle, uint16_t *diagnostics)
{
  bench_enc_diag_t sample = {0};
  sample.sample = ++g_enc_samples;
  /* Pipeline: angle request -> diagnostic request returns angle -> NOP returns diag. */
  const uint16_t commands[3] = {as5048a_read_command(AS5048A_REG_ANGLE),
                                as5048a_read_command(AS5048A_REG_DIAG), 0u};
  for (unsigned int i = 0u; i < 3u; ++i) {
    if (!spi_word(SPI1, ENC_CS_GPIO_Port, ENC_CS_Pin, commands[i], &sample.raw[i])) {
      sample.reason = 1u;
      sample.spi = g_spi_error;
      break;
    }
    sample.transfer_mask |= (uint8_t)(1u << i);
  }
  if (sample.reason == 0u) {
    if (!as5048a_decode(sample.raw[1], angle)) { sample.reason = 2u; }
    else if (!as5048a_decode(sample.raw[2], diagnostics)) { sample.reason = 3u; }
    else if (!as5048a_diagnostics_ok(*diagnostics)) { sample.reason = 4u; }
  }
  sample.time_us = bench_now_us();
  g_enc_latest = sample;
  if (sample.reason != 0u && g_enc_first.sample == 0u) { g_enc_first = sample; }
  return sample.reason == 0u;
}

void bench_hw_duty(float a, float b, float c)
{
  LL_TIM_OC_SetCompareCH1(TIM1, (uint32_t)(a * (float)BENCH_TIMER_ARR));
  LL_TIM_OC_SetCompareCH2(TIM1, (uint32_t)(b * (float)BENCH_TIMER_ARR));
  LL_TIM_OC_SetCompareCH3(TIM1, (uint32_t)(c * (float)BENCH_TIMER_ARR));
}

static void arm_snapshot(bench_arm_stage_t stage)
{
  g_arm_diag.stage = stage;
  g_arm_diag.time_us = bench_now_us();
  g_arm_diag.tim1_sr = TIM1->SR;
  g_arm_diag.tim1_bdtr = TIM1->BDTR;
  g_arm_diag.tim1_ccer = TIM1->CCER;
  g_arm_diag.gpiob_idr = GPIOB->IDR;
  g_arm_diag.gpioc_idr = GPIOC->IDR;
  g_arm_diag.gpioc_odr = GPIOC->ODR;
}

void bench_hw_arm_diagnostics(bench_arm_diag_t *out)
{
  if (out != NULL) { *out = g_arm_diag; }
}

bool bench_hw_arm(void)
{
  /* Called only under a short interrupt lock after the application gate passes. */
  ++g_arm_diag.attempt;
  g_arm_diag.guard_flags = 0u;
  if (!bench_hw_nfault()) { g_arm_diag.guard_flags |= BENCH_ARM_GUARD_NFAULT; }
  if (LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin)) {
    g_arm_diag.guard_flags |= BENCH_ARM_GUARD_DRVOFF;
  }
  if (!g_tim1_pwm_pins_parked) { g_arm_diag.guard_flags |= BENCH_ARM_GUARD_PINS; }
  if (!bench_hw_pwm_channels_disabled()) { g_arm_diag.guard_flags |= BENCH_ARM_GUARD_CHANNELS; }
  if (LL_TIM_IsEnabledAllOutputs(TIM1)) { g_arm_diag.guard_flags |= BENCH_ARM_GUARD_MOE; }
  if (g_arm_diag.guard_flags != 0u) {
    arm_snapshot(BENCH_ARM_STAGE_GUARD);
    bench_hw_off();
    return false;
  }
  LL_GPIO_ResetOutputPin(GPIOA, BENCH_TIM1_PWM_PINS_A);
  LL_GPIO_ResetOutputPin(GPIOB, BENCH_TIM1_PWM_PINS_B);
  bench_hw_restore_pwm_alternate_mode();
  LL_TIM_OC_DisablePreload(TIM1, LL_TIM_CHANNEL_CH1);
  LL_TIM_OC_DisablePreload(TIM1, LL_TIM_CHANNEL_CH2);
  LL_TIM_OC_DisablePreload(TIM1, LL_TIM_CHANNEL_CH3);
  bench_hw_duty(0.5f, 0.5f, 0.5f);
  LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH1);
  LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH2);
  LL_TIM_OC_EnablePreload(TIM1, LL_TIM_CHANNEL_CH3);
  LL_TIM_ClearFlag_BRK(TIM1);
  LL_TIM_CC_EnableChannel(TIM1, PWM_CHANNELS);
  LL_TIM_EnableIT_BRK(TIM1);
  LL_TIM_EnableAllOutputs(TIM1);
  /* RM0440 Rev 9, 29.3.18: MOE writes act asynchronously, but reads are
   * resynchronized. Allow a short fixed delay before readback. Hardware break
   * remains enabled throughout; never retry the MOE write. */
  bench_delay_us(2u);
  if (!LL_TIM_IsEnabledAllOutputs(TIM1) || LL_TIM_IsActiveFlag_BRK(TIM1)) {
    arm_snapshot(BENCH_ARM_STAGE_ENABLE);
    bench_hw_off();
    return false;
  }
  arm_snapshot(BENCH_ARM_STAGE_READY);
  return true;
}

bool bench_hw_break_test(void)
{
  bench_hw_off();
  const uint32_t original_pull = LL_GPIO_GetPinPull(GPIOB, LL_GPIO_PIN_12);
  LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_12, LL_GPIO_PULL_UP);
  bench_delay_us(10u);
  LL_TIM_ClearFlag_BRK(TIM1);
  bench_delay_us(10u);
  bool pass = bench_hw_nfault() && !LL_TIM_IsActiveFlag_BRK(TIM1);
  LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_12, LL_GPIO_PULL_DOWN);
  bench_delay_us(10u);
  pass = pass && !bench_hw_nfault() && LL_TIM_IsActiveFlag_BRK(TIM1);
  LL_GPIO_SetPinPull(GPIOB, LL_GPIO_PIN_12, original_pull);
  bench_delay_us(10u);
  LL_TIM_ClearFlag_BRK(TIM1);
  return pass;
}

void bench_hw_watchdog_start(void)
{
  /* Never allow a debugger halt to freeze the watchdog with a bridge active. */
  LL_DBGMCU_APB1_GRP1_UnFreezePeriph(LL_DBGMCU_APB1_GRP1_IWDG_STOP);
  /* Prescaler/reload/start belong to CubeMX MX_IWDG_Init. */
  LL_IWDG_ReloadCounter(IWDG);
}

void bench_hw_watchdog_feed(void) { LL_IWDG_ReloadCounter(IWDG); }
