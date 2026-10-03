/* CMake extracts the real product ADC qualification helper and ISR. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../config/board_config.h"
#include "../control/current_zero.h"
#include "../control/foc.h"
#include "../safety/safety_supervisor.h"
#include "../trace/trace_buffer.h"

#include "product_power_stage_under_test.inc"

#define ADC1 1
#define ADC2 2
#define ADC3 3
#define ADC_MAX_ID 3
#define LL_ADC_INJ_RANK_1 1
#define LL_ADC_INJ_RANK_2 2
#define GPIOA 1
#define GPIOB 2
#define GPIOC 3
#define TIM1 1
#define DRV8316_NSLEEP_GPIO_Port GPIOA
#define DRV8316_NSLEEP_Pin (1u << 12)
#define DRV8316_DRVOFF_GPIO_Port GPIOA
#define DRV8316_DRVOFF_Pin (1u << 8)
#define MOTOR_PWR_EN_GPIO_Port GPIOB
#define MOTOR_PWR_EN_Pin (1u << 9)
#define DWT (&fake_dwt)
#define __get_PRIMASK() fake_get_primask()
#define __disable_irq() fake_disable_irq()
#define __enable_irq() fake_enable_irq()
#define LL_GPIO_IsInputPinSet(port, pin) fake_gpio_input((port), (pin))
#define LL_GPIO_IsOutputPinSet(port, pin) fake_gpio_output((port), (pin))
#define LL_TIM_IsEnabledAllOutputs(timer) ((void)(timer), fake_moe_enabled)

typedef struct {
  volatile uint32_t CYCCNT;
} fake_dwt_t;

static fake_dwt_t fake_dwt;
static bool adc_jeos[ADC_MAX_ID + 1];
static uint32_t adc_raw[ADC_MAX_ID + 1][3];
static bool fake_driver_configured;
static bool fake_nsleep_command_high;
static bool fake_nsleep_pad_high;
static bool fake_motor_power_command_high;
static bool fake_motor_power_pad_high;
static bool fake_drvoff_command_high;
static bool fake_drvoff_pad_high;
static bool fake_moe_enabled;
static bool fake_torque_allowed;
static uint64_t fake_now_us;
static uint32_t fake_primask;
static bool fake_reset_on_irq_restore;
static unsigned fake_safe_off_calls;
static unsigned fake_fault_latches;
static unsigned fake_trace_pushes;
static unsigned fake_duty_calls;
static unsigned fake_foc_zero_calls;
static uint32_t fake_last_fault;

static gl30_safety_t g_safety;
static gl30_foc_state_t g_foc;
static gl30_current_zero_t g_current_zero;
static volatile float g_vbus_v;
static volatile uint16_t g_motor_temperature_raw;
static volatile bool g_motor_temperature_sample_received;
static volatile uint32_t g_control_progress;
static volatile uint16_t g_last_isr_cycles;
static volatile uint16_t g_encoder_status;
static volatile bool g_trace_frozen;
static volatile uint64_t g_last_adc_sample_us;
static volatile gl30_power_stage_t g_power_stage;
static volatile uint64_t g_bus_valid_since_us;

static void fake_emergency_off(uint32_t fault_bit) {
  fake_safe_off_calls++;
  fake_fault_latches++;
  fake_last_fault = fault_bit;
  g_power_stage = GL30_POWER_FAILED;
  fake_driver_configured = false;
  fake_moe_enabled = false;
  fake_drvoff_command_high = true;
  fake_drvoff_pad_high = true;
  fake_nsleep_command_high = false;
  fake_nsleep_pad_high = false;
  fake_motor_power_command_high = false;
  fake_motor_power_pad_high = false;
  gl30_current_zero_reset(&g_current_zero);
  g_safety.fault_bits |= fault_bit;
  g_safety.startup_state = GL30_STARTUP_FAULT_LATCHED;
  g_safety.arm_requested = false;
  g_trace_frozen = true;
}

static uint32_t fake_get_primask(void) { return fake_primask; }
static void fake_disable_irq(void) { fake_primask = 1u; }
static void fake_enable_irq(void) {
  fake_primask = 0u;
  if (fake_reset_on_irq_restore) {
    fake_reset_on_irq_restore = false;
    fake_emergency_off(GL30_FAULT_STARTUP);
  }
}

static uint32_t LL_ADC_IsActiveFlag_JEOS(int adc) {
  return adc_jeos[adc] ? 1u : 0u;
}

static uint32_t LL_ADC_INJ_ReadConversionData32(int adc, int rank) {
  return adc_raw[adc][rank];
}

static void LL_ADC_ClearFlag_JEOS(int adc) { adc_jeos[adc] = false; }

static bool fake_gpio_input(int port, int pin) {
  if (port == DRV8316_NSLEEP_GPIO_Port && pin == DRV8316_NSLEEP_Pin) {
    return fake_nsleep_pad_high;
  }
  if (port == MOTOR_PWR_EN_GPIO_Port && pin == MOTOR_PWR_EN_Pin) {
    return fake_motor_power_pad_high;
  }
  if (port == DRV8316_DRVOFF_GPIO_Port && pin == DRV8316_DRVOFF_Pin) {
    return fake_drvoff_pad_high;
  }
  return false;
}

static bool fake_gpio_output(int port, int pin) {
  if (port == DRV8316_NSLEEP_GPIO_Port && pin == DRV8316_NSLEEP_Pin) {
    return fake_nsleep_command_high;
  }
  if (port == MOTOR_PWR_EN_GPIO_Port && pin == MOTOR_PWR_EN_Pin) {
    return fake_motor_power_command_high;
  }
  if (port == DRV8316_DRVOFF_GPIO_Port && pin == DRV8316_DRVOFF_Pin) {
    return fake_drvoff_command_high;
  }
  return false;
}

uint64_t gl30_timebase_now_us(void) { return fake_now_us; }

static uint16_t saturate_u16(uint32_t value) {
  return value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}

static int16_t trace_i16(float value, float scale) {
  const float scaled = value * scale;
  if (scaled > 32767.0f) {
    return INT16_MAX;
  }
  if (scaled < -32768.0f) {
    return INT16_MIN;
  }
  return (int16_t)scaled;
}

static void hardware_safe_state(void) {
  fake_safe_off_calls++;
  fake_moe_enabled = false;
  fake_drvoff_command_high = true;
  fake_drvoff_pad_high = true;
}

static void latch_fault(uint32_t fault_bit) {
  fake_emergency_off(fault_bit);
}

bool gl30_drv8316_is_configured(void) { return fake_driver_configured; }
bool gl30_drv8316_outputs_enabled(void) {
  return !fake_drvoff_pad_high && fake_moe_enabled;
}
void gl30_drv8316_set_duty(float a, float b, float c) {
  (void)a;
  (void)b;
  (void)c;
  fake_duty_calls++;
}
void gl30_drv8316_safe_off(void) { hardware_safe_state(); }

void gl30_foc_force_zero(gl30_foc_state_t *state) {
  (void)state;
  fake_foc_zero_calls++;
}
gl30_foc_output_t gl30_foc_current_tick(
    gl30_foc_state_t *state, float ia, float ib, float ic, float vbus,
    float dt, float limit) {
  (void)state;
  (void)ia;
  (void)ib;
  (void)ic;
  (void)vbus;
  (void)dt;
  (void)limit;
  return (gl30_foc_output_t){.valid = true, .overcurrent = false};
}
bool gl30_safety_torque_allowed(const gl30_safety_t *state) {
  (void)state;
  return fake_torque_allowed;
}
bool gl30_safety_fault_latched(const gl30_safety_t *state) {
  return state->fault_bits != 0u;
}
void gl30_safety_latch_fault(gl30_safety_t *state, uint32_t fault_bit) {
  state->fault_bits |= fault_bit;
}
void gl30_trace_push(const int16_t sample[6]) {
  (void)sample;
  fake_trace_pushes++;
}

#include "product_adc_zero_under_test.inc"

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

static void reset_fixture(void) {
  for (unsigned adc = 0u; adc <= ADC_MAX_ID; ++adc) {
    adc_jeos[adc] = false;
    for (unsigned rank = 0u; rank < 3u; ++rank) {
      adc_raw[adc][rank] = 0u;
    }
  }
  fake_dwt.CYCCNT = 0u;
  fake_driver_configured = false;
  fake_nsleep_command_high = false;
  fake_nsleep_pad_high = false;
  fake_motor_power_command_high = false;
  fake_motor_power_pad_high = false;
  fake_drvoff_command_high = true;
  fake_drvoff_pad_high = true;
  fake_moe_enabled = false;
  fake_torque_allowed = false;
  fake_now_us = 0u;
  fake_primask = 0u;
  fake_reset_on_irq_restore = false;
  fake_safe_off_calls = 0u;
  fake_fault_latches = 0u;
  fake_trace_pushes = 0u;
  fake_duty_calls = 0u;
  fake_foc_zero_calls = 0u;
  fake_last_fault = 0u;
  g_last_adc_sample_us = 0u;
  g_power_stage = GL30_POWER_WAIT_LOGIC;
  g_bus_valid_since_us = 0u;
  g_safety = (gl30_safety_t){0};
  g_safety.startup_state = GL30_STARTUP_POWER_CHECK;
  g_foc = (gl30_foc_state_t){0};
  gl30_current_zero_reset(&g_current_zero);
  g_vbus_v = 0.0f;
  g_motor_temperature_raw = 0u;
  g_motor_temperature_sample_received = false;
  g_control_progress = 0u;
  g_last_isr_cycles = 0u;
  g_encoder_status = 0u;
  g_trace_frozen = false;
}

static void set_analog_ready(void) {
  fake_driver_configured = true;
  fake_nsleep_command_high = true;
  fake_nsleep_pad_high = true;
  fake_motor_power_command_high = true;
  fake_motor_power_pad_high = true;
}

static void run_adc_frame(uint16_t a, uint16_t b, uint16_t c, uint16_t vbus,
                          uint16_t temperature) {
  adc_raw[ADC1][LL_ADC_INJ_RANK_1] = a;
  adc_raw[ADC1][LL_ADC_INJ_RANK_2] = vbus;
  adc_raw[ADC2][LL_ADC_INJ_RANK_1] = b;
  adc_raw[ADC2][LL_ADC_INJ_RANK_2] = temperature;
  adc_raw[ADC3][LL_ADC_INJ_RANK_1] = c;
  adc_jeos[ADC1] = true;
  adc_jeos[ADC2] = true;
  adc_jeos[ADC3] = true;
  gl30_app_adc1_2_irq();
}

static void test_zero_requires_power_and_driver(void) {
  reset_fixture();
  for (unsigned i = 0u; i < GL30_ADC_ZERO_CAL_SAMPLES; ++i) {
    fake_now_us = i;
    run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  }
  CHECK(g_current_zero.state == GL30_CURRENT_ZERO_WAITING &&
            g_current_zero.count == 0u,
        "512 ADC frames while asleep/unconfigured cannot make zero ready");

  reset_fixture();
  fake_nsleep_command_high = true;
  fake_nsleep_pad_high = true;
  fake_motor_power_command_high = true;
  fake_motor_power_pad_high = true;
  for (unsigned i = 0u; i < GL30_ADC_ZERO_CAL_SAMPLES; ++i) {
    fake_now_us = i;
    run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  }
  CHECK(g_current_zero.state == GL30_CURRENT_ZERO_WAITING &&
            g_current_zero.count == 0u,
        "512 ADC frames before driver configuration cannot make zero ready");
}

static void test_gpio_pad_levels_qualify_power(void) {
  reset_fixture();
  set_analog_ready();
  fake_nsleep_pad_high = false;
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  CHECK(g_current_zero.state == GL30_CURRENT_ZERO_WAITING &&
            g_current_zero.count == 0u,
        "NSLEEP output-high with a low input pad does not start zero sampling");

  reset_fixture();
  set_analog_ready();
  fake_motor_power_pad_high = false;
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  CHECK(g_current_zero.state == GL30_CURRENT_ZERO_WAITING &&
            g_current_zero.count == 0u,
        "motor-power output-high with a low input pad does not start zero sampling");
}

static void test_overvoltage_is_checked_during_calibration(void) {
  reset_fixture();
  set_analog_ready();
  g_power_stage = GL30_POWER_WAIT_ZERO;
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  fake_now_us = GL30_ADC_ZERO_SETTLE_US;
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  CHECK(g_current_zero.state == GL30_CURRENT_ZERO_COLLECTING &&
            g_current_zero.count == 1u,
        "calibration has begun before the overvoltage sample");

  fake_now_us++;
  run_adc_frame(2048u, 2048u, 2048u, 1900u, 2000u);
  CHECK((g_safety.fault_bits & GL30_FAULT_VBUS) != 0u &&
            fake_last_fault == GL30_FAULT_VBUS,
        "VM at or above 16 V during zero qualification latches VBUS fault");
}

static void test_low_vm_during_powerup_faults_and_timestamps_frame(void) {
  reset_fixture();
  set_analog_ready();
  g_power_stage = GL30_POWER_WAIT_WAKE;
  fake_now_us = 12345u;
  run_adc_frame(2048u, 2048u, 2048u, 800u, 2000u);
  CHECK(g_last_adc_sample_us == fake_now_us,
        "ADC ISR publishes the synchronized frame timestamp");
  CHECK((g_safety.fault_bits & GL30_FAULT_VBUS) != 0u &&
            fake_last_fault == GL30_FAULT_VBUS &&
            g_power_stage == GL30_POWER_FAILED,
        "VM below 9 V during power-up latches VBUS and fails startup");
  CHECK(!fake_driver_configured && !fake_nsleep_command_high &&
            !fake_motor_power_command_high && !fake_moe_enabled &&
            fake_safe_off_calls == 1u,
        "low VM immediately invalidates and powers down the preparation path");
}

static void test_wait_bus_voltage_samples_manage_stability_window(void) {
  reset_fixture();
  set_analog_ready();
  g_power_stage = GL30_POWER_WAIT_BUS;
  g_bus_valid_since_us = 777u;
  fake_now_us = 1234u;
  run_adc_frame(2048u, 2048u, 2048u, 800u, 2000u);
  CHECK(g_power_stage == GL30_POWER_WAIT_BUS &&
            g_bus_valid_since_us == 0u &&
            (g_safety.fault_bits & GL30_FAULT_VBUS) == 0u,
        "WAIT_BUS ADC undervoltage clears the stability window without latching");

  reset_fixture();
  set_analog_ready();
  g_power_stage = GL30_POWER_WAIT_BUS;
  g_bus_valid_since_us = 777u;
  fake_now_us = 1234u;
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  CHECK(g_power_stage == GL30_POWER_WAIT_BUS &&
            g_bus_valid_since_us == 777u &&
            (g_safety.fault_bits & GL30_FAULT_VBUS) == 0u,
        "healthy WAIT_BUS ADC frame preserves the existing stability start");
}

static void test_bridge_off_wait_does_not_repeat_safeoff(void) {
  reset_fixture();
  set_analog_ready();
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  fake_now_us = GL30_ADC_ZERO_TIMEOUT_US - 10000u;
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  fake_now_us++;
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  CHECK(g_current_zero.state == GL30_CURRENT_ZERO_COLLECTING &&
            g_current_zero.count == 2u,
        "zero remains pending during the qualification interval");
  CHECK(fake_safe_off_calls == 0u,
        "zero-not-ready frames with bridge already off do not repeatedly safe-off the driver");
}

static void test_adc_samples_publish_ready_and_sleep_loss_clears_it(void) {
  reset_fixture();
  set_analog_ready();
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  for (unsigned i = 0u; i < GL30_ADC_ZERO_CAL_SAMPLES; ++i) {
    fake_now_us = GL30_ADC_ZERO_SETTLE_US + i * 25u;
    run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  }
  CHECK(g_current_zero.state == GL30_CURRENT_ZERO_READY &&
            g_current_zero.count == GL30_ADC_ZERO_CAL_SAMPLES,
        "real synchronized ADC IRQ frames complete current-zero qualification");
  CHECK(g_current_zero.offset[0] == 2048.0f &&
            g_current_zero.offset[1] == 2048.0f &&
            g_current_zero.offset[2] == 2048.0f,
        "ADC qualification publishes three channel offsets");

  fake_torque_allowed = true;
  fake_moe_enabled = true;
  fake_drvoff_command_high = false;
  fake_drvoff_pad_high = false;
  fake_now_us += 25u;
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  CHECK(fake_duty_calls == 1u,
        "controllable torque-allowed fake exercises the active FOC output path");

  fake_nsleep_pad_high = false;
  fake_now_us += 25u;
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  CHECK(g_current_zero.state == GL30_CURRENT_ZERO_WAITING &&
            g_current_zero.count == 0u,
        "a low NSLEEP input after READY resets current-zero qualification");
  CHECK(fake_safe_off_calls == 1u && !fake_moe_enabled &&
            fake_drvoff_command_high,
        "loss of qualified power safe-offs active outputs in that ADC IRQ");
  CHECK(fake_duty_calls == 1u,
        "sleep-pad loss cannot issue another PWM duty update");
}

static void test_pending_reset_after_irq_restore_cancels_ready_publish(void) {
  reset_fixture();
  set_analog_ready();
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  for (unsigned i = 0u; i < GL30_ADC_ZERO_CAL_SAMPLES - 1u; ++i) {
    fake_now_us = GL30_ADC_ZERO_SETTLE_US + i * 25u;
    run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  }
  CHECK(g_current_zero.state == GL30_CURRENT_ZERO_COLLECTING &&
            g_current_zero.count == GL30_ADC_ZERO_CAL_SAMPLES - 1u,
        "IRQ-restore race is injected on the otherwise-ready final sample");

  fake_reset_on_irq_restore = true;
  fake_now_us = GL30_ADC_ZERO_SETTLE_US +
                (GL30_ADC_ZERO_CAL_SAMPLES - 1u) * 25u;
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  CHECK(g_current_zero.state == GL30_CURRENT_ZERO_WAITING &&
            g_current_zero.count == 0u &&
            (g_safety.fault_bits & GL30_FAULT_STARTUP) != 0u &&
            !fake_driver_configured,
        "a pending fault/reset after IRQ restore cancels ready before publication");
  CHECK(fake_primask == 0u && fake_foc_zero_calls != 0u &&
            fake_safe_off_calls == 1u && !fake_moe_enabled &&
            fake_drvoff_command_high && fake_duty_calls == 0u,
        "restore-race emergency preserves IRQ state, turns outputs off, and blocks PWM");
}

static void test_originally_masked_primask_stays_masked(void) {
  float offsets[3] = {0.0f, 0.0f, 0.0f};
  const uint16_t raw[3] = {2048u, 2048u, 2048u};

  reset_fixture();
  set_analog_ready();
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  for (unsigned i = 0u; i < GL30_ADC_ZERO_CAL_SAMPLES - 1u; ++i) {
    fake_now_us = GL30_ADC_ZERO_SETTLE_US + i * 25u;
    run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  }

  fake_primask = 1u;
  fake_now_us = GL30_ADC_ZERO_SETTLE_US +
                (GL30_ADC_ZERO_CAL_SAMPLES - 1u) * 25u;
  CHECK(update_current_zero(raw, offsets),
        "the real helper returns ready while entering with PRIMASK already set");
  CHECK(fake_primask == 1u && g_current_zero.state == GL30_CURRENT_ZERO_READY,
        "the helper preserves an originally masked PRIMASK value");
}

static void test_bridge_activation_during_zero_is_emergency_fault(void) {
  reset_fixture();
  set_analog_ready();
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  fake_moe_enabled = true;
  fake_now_us = 5000u;
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  CHECK((g_safety.fault_bits & GL30_FAULT_STARTUP) != 0u &&
            fake_last_fault == GL30_FAULT_STARTUP,
        "unexpected MOE during settling latches STARTUP");
  CHECK(!fake_driver_configured && !fake_moe_enabled &&
            fake_drvoff_command_high && !fake_nsleep_command_high &&
            !fake_motor_power_command_high && fake_safe_off_calls == 1u,
        "STARTUP latch performs emergency reset, configuration invalidation, and safe-off");

  reset_fixture();
  set_analog_ready();
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  fake_now_us = GL30_ADC_ZERO_SETTLE_US;
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  CHECK(g_current_zero.state == GL30_CURRENT_ZERO_COLLECTING,
        "the DRVOFF-pad violation is injected during collection");
  fake_drvoff_pad_high = false;
  fake_now_us += 25u;
  run_adc_frame(2048u, 2048u, 2048u, 1500u, 2000u);
  CHECK((g_safety.fault_bits & GL30_FAULT_STARTUP) != 0u &&
            fake_last_fault == GL30_FAULT_STARTUP,
        "DRVOFF output-high with pad-low during collection latches STARTUP");
  CHECK(!fake_driver_configured && !fake_moe_enabled &&
            fake_drvoff_command_high && !fake_nsleep_command_high &&
            !fake_motor_power_command_high && fake_safe_off_calls == 1u,
        "unexpected bridge activation is cleared by the emergency fake");
}

int main(void) {
  test_zero_requires_power_and_driver();
  test_gpio_pad_levels_qualify_power();
  test_overvoltage_is_checked_during_calibration();
  test_low_vm_during_powerup_faults_and_timestamps_frame();
  test_wait_bus_voltage_samples_manage_stability_window();
  test_bridge_off_wait_does_not_repeat_safeoff();
  test_adc_samples_publish_ready_and_sleep_loss_clears_it();
  test_pending_reset_after_irq_restore_cancels_ready_publish();
  test_originally_masked_primask_stays_masked();
  test_bridge_activation_during_zero_is_emergency_fault();
  printf("product ADC zero: %u checks, %u failed\n", checks, failed);
  return failed == 0u ? 0 : 1;
}
