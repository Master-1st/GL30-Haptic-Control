/* Exercise the exact product power-preparation, fault, and emergency paths. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../config/board_config.h"
#include "../control/control_lease.h"
#include "../control/current_zero.h"
#include "../control/foc.h"
#include "../drivers/board_sense.h"
#include "../drivers/drv8316.h"
#include "../drivers/factory_encoder.h"
#include "../haptics/haptics.h"
#include "../protocol/v6_protocol.h"
#include "../safety/safety_supervisor.h"

/* The host fixture represents a board whose phase alignment was qualified. */
#undef GL30_ELECTRICAL_ZERO_VALID
#define GL30_ELECTRICAL_ZERO_VALID 1u

#include "product_power_stage_under_test.inc"

#define GPIOA 1
#define GPIOB 2
#define GPIOC 3
#define TIM1 1
#define TIM7 7
#define IWDG 1
#define LL_GPIO_PIN_2 (1u << 2)
#define LL_GPIO_PIN_3 (1u << 3)
#define LL_GPIO_PIN_9 (1u << 9)
#define LL_GPIO_PIN_12 (1u << 12)
#define LL_GPIO_PIN_13 (1u << 13)
#define SYS_FAULT_N_GPIO_Port GPIOC
#define SYS_FAULT_N_Pin LL_GPIO_PIN_13
#define BRAKE_FORCE_TEST_GPIO_Port GPIOA
#define BRAKE_FORCE_TEST_Pin LL_GPIO_PIN_2
#define DRV8316_DRVOFF_GPIO_Port GPIOA
#define DRV8316_DRVOFF_Pin LL_GPIO_PIN_3
#define DRV8316_NSLEEP_GPIO_Port GPIOA
#define DRV8316_NSLEEP_Pin LL_GPIO_PIN_12
#define MOTOR_PWR_EN_GPIO_Port GPIOB
#define MOTOR_PWR_EN_Pin LL_GPIO_PIN_9
#define RCC_AHB2ENR_GPIOAEN (1u << 0u)
#define RCC_AHB2ENR_GPIOBEN (1u << 1u)
#define RCC_AHB2ENR_GPIOCEN (1u << 2u)
#define DWT_CTRL_CYCCNTENA_Msk (1u << 0u)

typedef struct {
  uint32_t AHB2ENR;
} fake_rcc_t;

typedef struct {
  uint32_t CYCCNT;
  uint32_t CTRL;
} fake_dwt_t;

static fake_rcc_t fake_rcc;
static fake_dwt_t fake_dwt;
#define RCC (&fake_rcc)
#define DWT (&fake_dwt)

static uint32_t SystemCoreClock;
static uint32_t fake_primask;
static uint64_t fake_now_us;
static bool fake_advance_time_after_next_read;
static uint64_t fake_time_after_next_read_us;
static unsigned fake_timebase_read_count;
static bool fake_nsleep_command;
static bool fake_nsleep_pad;
static bool fake_power_command;
static bool fake_power_pad;
static bool fake_drvoff_command;
static bool fake_drvoff_pad;
static bool fake_pb12_high;
static bool fake_fault_pin_high;
static bool fake_moe;
static bool fake_driver_configured;
static bool fake_driver_verified;
static bool fake_driver_outputs;
static bool fake_encoder_ready;
static bool fake_config_result;
static bool fake_verify_result;
static bool fake_emergency_during_config;
static bool fake_emergency_during_verify;
static bool fake_emergency_on_irq_restore;
static bool fake_pb12_low_after_verify;
static bool fake_iwdg_reset_flag;
static bool fake_wwdg_reset_flag;
static bool fake_apply_result;
static bool fake_arm_result;
static bool fake_arm_inject_pb12_low_during_release;
static bool fake_arm_keep_pb12_low;
static bool fake_arm_enable_moe_during_release;
static bool fake_arm_enable_driver_outputs_during_release;
static bool fake_ntc_result;
static float fake_ntc_temperature;
static bool fake_tim7_update;
static unsigned fake_configure_calls;
static unsigned fake_verify_calls;
static unsigned fake_invalidate_calls;
static unsigned fake_safe_off_calls;
static unsigned fake_arm_calls;
static unsigned fake_arm_tim7_injections;
static bool fake_arm_guard_seen_at_tim7;
static bool fake_arm_moe_seen_at_tim7;
static bool fake_arm_outputs_seen_at_tim7;
static unsigned fake_masked_io_violations;
static unsigned fake_gpio_set_count;
static unsigned fake_gpio_reset_count;
static unsigned fake_apply_calls;
static unsigned fake_command_arm_calls;
static unsigned fake_force_zero_calls;
static float fake_last_torque_limit;
static unsigned fake_iwdg_reload_calls;
static unsigned fake_reset_flags_clear_calls;
static unsigned fake_ntc_calls;
static unsigned fake_tim7_clear_calls;
static uint32_t fake_off_generation;
static uint32_t fake_config_generation;
static uint32_t fake_verify_generation;
static gl30_power_stage_t fake_stage_at_config;
static gl30_power_stage_t fake_stage_at_verify;
static uint32_t fake_arm_generation;
static gl30_factory_encoder_sample_t fake_encoder_sample;
static gl30_safety_t g_safety;
static gl30_foc_state_t g_foc;
static gl30_current_zero_t g_current_zero;
static gl30_control_lease_t g_control_lease;
static gl30_control_lease_request_t g_pending_control_request;
static volatile bool g_pending_control_request_ready;
static volatile uint64_t g_pending_control_request_received_us;
static volatile float g_vbus_v;
static volatile float g_motor_temperature_c;
static volatile uint16_t g_motor_temperature_raw;
static volatile bool g_motor_temperature_sample_received;
static volatile uint64_t g_last_adc_sample_us;
static volatile bool g_monitor_due;
static volatile gl30_power_stage_t g_power_stage;
static uint64_t g_power_stage_started_us;
static volatile bool g_driver_arm_in_progress;
static uint64_t g_bus_valid_since_us;
static volatile bool g_trace_frozen;
static gl30_haptic_command_t g_pending_command;
static volatile bool g_pending_command_ready;
static volatile uint32_t g_pending_command_sequence;
static volatile uint64_t g_pending_command_received_us;
static uint64_t g_arm_ready_since_us;
static volatile bool g_telemetry_due;
static volatile bool g_slow_telemetry_due;
static volatile bool g_haptic_state_due;
static volatile uint64_t g_haptic_measurement_us;
static volatile uint32_t g_haptic_tick_divider;
static bool g_have_rx_sequence;
static uint32_t g_last_rx_sequence;
static volatile uint32_t g_dropped_commands;
static volatile uint32_t g_control_progress;
static volatile uint32_t g_safety_progress;
static uint64_t g_last_watchdog_refresh_us;
static uint32_t g_last_watchdog_progress;
static uint32_t g_last_watchdog_safety_progress;

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

#define TEST_CONTROL_GENERATION UINT64_C(0x100000001)
#define TEST_CONTROL_NONCE UINT32_C(0x13572468)

static void establish_test_control_lease(void) {
  const gl30_control_lease_request_t acquire = {
      .action = GL30_CONTROL_ACQUIRE,
      .zeroNonce = TEST_CONTROL_NONCE,
      .currentGeneration = 0u,
      .nextGeneration = TEST_CONTROL_GENERATION,
  };
  gl30_haptic_command_t first_zero = {0};
  gl30_control_lease_init(&g_control_lease);
  CHECK(gl30_control_lease_process_request(&g_control_lease, &acquire, false) ==
            GL30_CONTROL_LEASE_ACQUIRE_ACCEPTED,
        "fixture acquires an actual control lease before ordinary commands");
  first_zero.commandNonce = TEST_CONTROL_NONCE;
  first_zero.leaseGeneration = TEST_CONTROL_GENERATION;
  CHECK(gl30_control_lease_command_is_allowed(&g_control_lease, &first_zero),
        "fixture first zero matches the current nonce and generation");
  gl30_control_lease_command_accepted(&g_control_lease, &first_zero);
  CHECK(!g_control_lease.awaiting_first_zero,
        "fixture consumes the first exact zero before positive commands");
}

void gl30_app_emergency_off(void);
void gl30_app_tim7_irq(void);

static uint32_t fake_get_primask(void) { return fake_primask; }
static void fake_disable_irq(void) { fake_primask = 1u; }
static void fake_enable_irq(void) {
  fake_primask = 0u;
  if (fake_emergency_on_irq_restore) {
    fake_emergency_on_irq_restore = false;
    gl30_app_emergency_off();
  }
}

#define __get_PRIMASK() fake_get_primask()
#define __disable_irq() fake_disable_irq()
#define __enable_irq() fake_enable_irq()

static uint32_t fake_tim_is_active(int timer) {
  return timer == TIM7 && fake_tim7_update ? 1u : 0u;
}

static void fake_tim_clear_update(int timer) {
  if (timer == TIM7) {
    fake_tim7_clear_calls++;
    fake_tim7_update = false;
  }
}

static bool fake_gpio_input(int port, int pin) {
  if (port == GPIOB && pin == LL_GPIO_PIN_12) {
    return fake_pb12_high;
  }
  if (port == DRV8316_NSLEEP_GPIO_Port && pin == DRV8316_NSLEEP_Pin) {
    return fake_nsleep_pad;
  }
  if (port == MOTOR_PWR_EN_GPIO_Port && pin == MOTOR_PWR_EN_Pin) {
    return fake_power_pad;
  }
  if (port == DRV8316_DRVOFF_GPIO_Port && pin == DRV8316_DRVOFF_Pin) {
    return fake_drvoff_pad;
  }
  return false;
}

static bool fake_gpio_output(int port, int pin) {
  if (port == DRV8316_NSLEEP_GPIO_Port && pin == DRV8316_NSLEEP_Pin) {
    return fake_nsleep_command;
  }
  if (port == MOTOR_PWR_EN_GPIO_Port && pin == MOTOR_PWR_EN_Pin) {
    return fake_power_command;
  }
  if (port == DRV8316_DRVOFF_GPIO_Port && pin == DRV8316_DRVOFF_Pin) {
    return fake_drvoff_command;
  }
  return false;
}

static void fake_gpio_set(int port, int pin) {
  fake_gpio_set_count++;
  if (port == DRV8316_NSLEEP_GPIO_Port && pin == DRV8316_NSLEEP_Pin) {
    fake_nsleep_command = true;
    fake_nsleep_pad = true;
  } else if (port == MOTOR_PWR_EN_GPIO_Port && pin == MOTOR_PWR_EN_Pin) {
    fake_power_command = true;
    fake_power_pad = true;
  } else if (port == DRV8316_DRVOFF_GPIO_Port && pin == DRV8316_DRVOFF_Pin) {
    fake_drvoff_command = true;
    fake_drvoff_pad = true;
  } else if (port == SYS_FAULT_N_GPIO_Port && pin == SYS_FAULT_N_Pin) {
    fake_fault_pin_high = true;
  }
}

static void fake_gpio_reset(int port, int pin) {
  fake_gpio_reset_count++;
  if (port == DRV8316_NSLEEP_GPIO_Port && pin == DRV8316_NSLEEP_Pin) {
    fake_nsleep_command = false;
    fake_nsleep_pad = false;
  } else if (port == MOTOR_PWR_EN_GPIO_Port && pin == MOTOR_PWR_EN_Pin) {
    fake_power_command = false;
    fake_power_pad = false;
  } else if (port == DRV8316_DRVOFF_GPIO_Port && pin == DRV8316_DRVOFF_Pin) {
    fake_drvoff_command = false;
    fake_drvoff_pad = false;
  } else if (port == SYS_FAULT_N_GPIO_Port && pin == SYS_FAULT_N_Pin) {
    fake_fault_pin_high = false;
  }
}

static uint32_t fake_iwdg_flag(void) { return fake_iwdg_reset_flag ? 1u : 0u; }
static uint32_t fake_wwdg_flag(void) { return fake_wwdg_reset_flag ? 1u : 0u; }
static void fake_clear_reset_flags(void) {
  fake_reset_flags_clear_calls++;
  fake_iwdg_reset_flag = false;
  fake_wwdg_reset_flag = false;
}
static void fake_iwdg_reload(int watchdog) {
  (void)watchdog;
  fake_iwdg_reload_calls++;
}

static void run_slow_sensors(uint64_t now_us) { (void)now_us; }

bool gl30_board_ntc_temperature_c(uint16_t raw_adc, float *temperature_c) {
  (void)raw_adc;
  fake_ntc_calls++;
  if (fake_ntc_result && temperature_c != NULL) {
    *temperature_c = fake_ntc_temperature;
  }
  return fake_ntc_result;
}

void gl30_haptic_tick_2k(gl30_foc_state_t *state, bool torque_allowed) {
  (void)state;
  (void)torque_allowed;
}

#define LL_GPIO_SetOutputPin(port, pin) fake_gpio_set((port), (pin))
#define LL_GPIO_ResetOutputPin(port, pin) fake_gpio_reset((port), (pin))
#define LL_GPIO_IsInputPinSet(port, pin) fake_gpio_input((port), (pin))
#define LL_GPIO_IsOutputPinSet(port, pin) fake_gpio_output((port), (pin))
#define LL_TIM_IsEnabledAllOutputs(timer) ((void)(timer), fake_moe)
#define LL_TIM_IsActiveFlag_UPDATE(timer) fake_tim_is_active((timer))
#define LL_TIM_ClearFlag_UPDATE(timer) fake_tim_clear_update((timer))
#define LL_RCC_IsActiveFlag_IWDGRST() fake_iwdg_flag()
#define LL_RCC_IsActiveFlag_WWDGRST() fake_wwdg_flag()
#define LL_RCC_ClearResetFlags() fake_clear_reset_flags()
#define LL_IWDG_ReloadCounter(watchdog) fake_iwdg_reload((watchdog))

uint64_t gl30_timebase_now_us(void) {
  const uint64_t now_us = fake_now_us;
  fake_timebase_read_count++;
  if (fake_advance_time_after_next_read) {
    fake_advance_time_after_next_read = false;
    fake_now_us = fake_time_after_next_read_us;
  }
  return now_us;
}

void gl30_timebase_delay_us(uint32_t us) {
  if (fake_primask != 0u) {
    fake_masked_io_violations++;
  }
  fake_now_us += us;
}

void gl30_factory_encoder_snapshot(gl30_factory_encoder_sample_t *out) {
  if (out != NULL) {
    *out = fake_encoder_sample;
  }
}

bool control_ready(const gl30_factory_encoder_sample_t *sample,
                   uint64_t now_us, uint64_t max_age_us) {
  (void)now_us;
  (void)max_age_us;
  return sample != NULL && fake_encoder_ready;
}

bool gl30_drv8316_is_configured(void) { return fake_driver_configured; }
bool gl30_drv8316_startup_verified(void) { return fake_driver_verified; }
bool gl30_drv8316_outputs_enabled(void) { return fake_driver_outputs; }
bool gl30_drv8316_read_faults(gl30_drv8316_status_t *out) {
  if (out != NULL) {
    *out = (gl30_drv8316_status_t){
        .n_fault_released = true,
        .configured = fake_driver_configured,
        .output_enabled = fake_driver_outputs,
    };
  }
  return out != NULL;
}
uint32_t gl30_drv8316_off_generation_snapshot(void) {
  return fake_off_generation;
}

void gl30_drv8316_safe_off(void) {
  fake_safe_off_calls++;
  fake_off_generation++;
  fake_driver_outputs = false;
  fake_moe = false;
  fake_drvoff_command = true;
  fake_drvoff_pad = true;
}

void gl30_drv8316_invalidate_configuration(void) {
  fake_invalidate_calls++;
  fake_off_generation++;
  fake_driver_configured = false;
  fake_driver_verified = false;
  fake_driver_outputs = false;
  fake_moe = false;
}

static void refresh_sensor_samples(void) {
  g_motor_temperature_sample_received = true;
  g_last_adc_sample_us = fake_now_us;
  fake_encoder_sample.timestamp_us = fake_now_us;
}

bool gl30_drv8316_configure(uint32_t requested_off_generation) {
  fake_configure_calls++;
  fake_stage_at_config = g_power_stage;
  fake_config_generation = requested_off_generation;
  if (fake_primask != 0u) {
    fake_masked_io_violations++;
  }
  if (requested_off_generation != fake_off_generation) {
    return false;
  }
  gl30_timebase_delay_us(1u);
  refresh_sensor_samples();
  if (!fake_config_result) {
    fake_driver_configured = false;
    return false;
  }
  fake_driver_configured = true;
  if (fake_emergency_during_config) {
    fake_emergency_during_config = false;
    gl30_app_emergency_off();
  }
  return true;
}

bool gl30_drv8316_verify_startup(uint32_t expected_off_generation) {
  fake_verify_calls++;
  fake_stage_at_verify = g_power_stage;
  fake_verify_generation = expected_off_generation;
  if (fake_primask != 0u) {
    fake_masked_io_violations++;
  }
  if (expected_off_generation != fake_off_generation) {
    return false;
  }
  gl30_timebase_delay_us(1u);
  refresh_sensor_samples();
  if (!fake_verify_result) {
    fake_driver_verified = false;
    return false;
  }
  fake_driver_verified = true;
  fake_drvoff_command = false;
  fake_drvoff_pad = false;
  if (fake_emergency_during_verify) {
    fake_emergency_during_verify = false;
    gl30_app_emergency_off();
  }
  if (fake_pb12_low_after_verify) {
    fake_pb12_low_after_verify = false;
    fake_pb12_high = false;
  }
  return true;
}

void gl30_drv8316_set_duty(float a, float b, float c) {
  (void)a;
  (void)b;
  (void)c;
}

void gl30_foc_force_zero(gl30_foc_state_t *state) {
  fake_force_zero_calls++;
  if (state != NULL) {
    state->torque_command_nm = 0.0f;
    state->haptic_torque_nm = 0.0f;
    state->i_d_ref_a = 0.0f;
    state->i_q_ref_a = 0.0f;
    state->integrator_d_v = 0.0f;
    state->integrator_q_v = 0.0f;
    state->v_d_v = 0.0f;
    state->v_q_v = 0.0f;
  }
}

bool gl30_foc_apply_command(gl30_foc_state_t *state,
                            const gl30_haptic_command_t *command) {
  (void)state;
  fake_apply_calls++;
  fake_last_torque_limit = command != NULL ? command->userTorqueLimitNm : 0.0f;
  return fake_apply_result;
}

bool gl30_drv8316_arm(uint32_t expected_off_generation) {
  fake_command_arm_calls++;
  fake_arm_generation = expected_off_generation;
  if (fake_primask != 0u) {
    fake_masked_io_violations++;
  }
  if (expected_off_generation != fake_off_generation || !fake_driver_verified) {
    return false;
  }
  fake_drvoff_command = false;
  fake_drvoff_pad = false;

  if (fake_arm_inject_pb12_low_during_release) {
    fake_arm_inject_pb12_low_during_release = false;
    fake_pb12_high = false;
    fake_moe = fake_arm_enable_moe_during_release;
    fake_driver_outputs = fake_arm_enable_driver_outputs_during_release;
    /* Model a fresh ADC frame so this injection isolates the PB12 arm race. */
    g_last_adc_sample_us = fake_now_us;
    fake_arm_guard_seen_at_tim7 = g_driver_arm_in_progress;
    fake_arm_moe_seen_at_tim7 = fake_moe;
    fake_arm_outputs_seen_at_tim7 = fake_driver_outputs;
    fake_tim7_update = true;
    fake_arm_tim7_injections++;
    gl30_app_tim7_irq();
    if (!fake_arm_keep_pb12_low) {
      fake_pb12_high = true;
    }
  }

  /* A concurrent emergency invalidates both the captured epoch and verify. */
  if (!fake_arm_result || expected_off_generation != fake_off_generation ||
      !fake_driver_verified || !fake_pb12_high) {
    return false;
  }
  fake_driver_outputs = true;
  fake_moe = true;
  return true;
}

#include "product_power_startup_under_test.inc"

static void reset_fixture(void) {
  fake_rcc.AHB2ENR = RCC_AHB2ENR_GPIOAEN | RCC_AHB2ENR_GPIOBEN |
                     RCC_AHB2ENR_GPIOCEN;
  fake_dwt.CYCCNT = 0u;
  fake_dwt.CTRL = DWT_CTRL_CYCCNTENA_Msk;
  SystemCoreClock = GL30_SYSCLK_HZ;
  fake_primask = 0u;
  fake_now_us = 1000u;
  fake_advance_time_after_next_read = false;
  fake_time_after_next_read_us = 0u;
  fake_timebase_read_count = 0u;
  fake_nsleep_command = false;
  fake_nsleep_pad = false;
  fake_power_command = false;
  fake_power_pad = false;
  fake_drvoff_command = true;
  fake_drvoff_pad = true;
  fake_pb12_high = true;
  fake_fault_pin_high = true;
  fake_moe = false;
  fake_driver_configured = false;
  fake_driver_verified = false;
  fake_driver_outputs = false;
  fake_encoder_ready = true;
  fake_config_result = true;
  fake_verify_result = true;
  fake_emergency_during_config = false;
  fake_emergency_during_verify = false;
  fake_emergency_on_irq_restore = false;
  fake_pb12_low_after_verify = false;
  fake_iwdg_reset_flag = false;
  fake_wwdg_reset_flag = false;
  fake_apply_result = true;
  fake_arm_result = true;
  fake_arm_inject_pb12_low_during_release = false;
  fake_arm_keep_pb12_low = false;
  fake_arm_enable_moe_during_release = false;
  fake_arm_enable_driver_outputs_during_release = false;
  fake_ntc_result = true;
  fake_ntc_temperature = 25.0f;
  fake_tim7_update = false;
  fake_configure_calls = 0u;
  fake_verify_calls = 0u;
  fake_invalidate_calls = 0u;
  fake_safe_off_calls = 0u;
  fake_arm_calls = 0u;
  fake_arm_tim7_injections = 0u;
  fake_arm_guard_seen_at_tim7 = false;
  fake_arm_moe_seen_at_tim7 = false;
  fake_arm_outputs_seen_at_tim7 = false;
  fake_masked_io_violations = 0u;
  fake_gpio_set_count = 0u;
  fake_gpio_reset_count = 0u;
  fake_apply_calls = 0u;
  fake_command_arm_calls = 0u;
  fake_force_zero_calls = 0u;
  fake_last_torque_limit = 0.0f;
  fake_iwdg_reload_calls = 0u;
  fake_reset_flags_clear_calls = 0u;
  fake_ntc_calls = 0u;
  fake_tim7_clear_calls = 0u;
  fake_off_generation = 0u;
  fake_config_generation = UINT32_MAX;
  fake_verify_generation = UINT32_MAX;
  fake_arm_generation = UINT32_MAX;
  fake_stage_at_config = GL30_POWER_FAILED;
  fake_stage_at_verify = GL30_POWER_FAILED;
  fake_encoder_sample = (gl30_factory_encoder_sample_t){
      .angle_rad = 0.25f,
      .timestamp_us = fake_now_us,
      .status = GL30_FACTORY_ENCODER_STATUS_READY,
      .valid = true,
      .sample_index = 1u,
  };
  g_safety = (gl30_safety_t){0};
  g_safety.startup_state = GL30_STARTUP_POWER_CHECK;
  g_foc = (gl30_foc_state_t){0};
  gl30_control_lease_init(&g_control_lease);
  g_pending_control_request = (gl30_control_lease_request_t){0};
  g_pending_control_request_ready = false;
  g_pending_control_request_received_us = 0u;
  gl30_current_zero_reset(&g_current_zero);
  g_vbus_v = 13.0f;
  g_motor_temperature_c = 25.0f;
  g_motor_temperature_raw = 2000u;
  g_motor_temperature_sample_received = true;
  g_last_adc_sample_us = fake_now_us;
  g_monitor_due = false;
  g_power_stage = GL30_POWER_WAIT_LOGIC;
  g_driver_arm_in_progress = false;
  g_power_stage_started_us = fake_now_us;
  g_bus_valid_since_us = 0u;
  g_trace_frozen = false;
  g_pending_command = (gl30_haptic_command_t){0};
  g_pending_command_ready = false;
  g_pending_command_sequence = 0u;
  g_pending_command_received_us = 0u;
  g_arm_ready_since_us = 0u;
  g_telemetry_due = false;
  g_slow_telemetry_due = false;
  g_haptic_state_due = false;
  g_haptic_measurement_us = 0u;
  g_haptic_tick_divider = 0u;
  g_have_rx_sequence = false;
  g_last_rx_sequence = 0u;
  g_dropped_commands = 0u;
  g_control_progress = 0u;
  g_safety_progress = 0u;
  g_last_watchdog_refresh_us = 0u;
  g_last_watchdog_progress = 0u;
  g_last_watchdog_safety_progress = 0u;
}

static void refresh_inputs(uint64_t now_us, float vbus_v) {
  fake_now_us = now_us;
  g_vbus_v = vbus_v;
  refresh_sensor_samples();
}

static void start_power_stage(void) {
  run_power_startup();
}

static void advance_to_wait_wake(void) {
  refresh_inputs(1000u, 13.0f);
  start_power_stage();
  refresh_inputs(1001u, 13.0f);
  start_power_stage();
  refresh_inputs(11000u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_WAIT_BUS && !fake_nsleep_command,
        "bus stability has not elapsed one microsecond early");
  refresh_inputs(11001u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_WAIT_WAKE && fake_nsleep_command &&
            fake_nsleep_pad,
        "stable VM for 10 ms releases NSLEEP and starts wake interval");
}

static void advance_to_wait_zero(void) {
  advance_to_wait_wake();
  refresh_inputs(21000u, 13.0f);
  start_power_stage();
  CHECK(fake_configure_calls == 0u && g_power_stage == GL30_POWER_WAIT_WAKE,
        "driver stays unconfigured one microsecond before wake deadline");
  refresh_inputs(21001u, 13.0f);
  start_power_stage();
  CHECK(fake_configure_calls == 1u && fake_driver_configured &&
            g_power_stage == GL30_POWER_WAIT_ZERO,
        "driver configures once when wake interval reaches 10 ms");
}

static void assert_failed_is_sticky(void) {
  const unsigned configure_before = fake_configure_calls;
  const unsigned verify_before = fake_verify_calls;
  const unsigned gpio_sets_before = fake_gpio_set_count;
  const unsigned invalidates_before = fake_invalidate_calls;
  refresh_inputs(fake_now_us + 1000u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            fake_configure_calls == configure_before &&
            fake_verify_calls == verify_before &&
            fake_gpio_set_count == gpio_sets_before &&
            fake_invalidate_calls == invalidates_before,
        "FAILED startup neither retries nor raises a power command");
}

static void test_ordered_preparation_stays_unarmed(void) {
  reset_fixture();
  advance_to_wait_zero();
  CHECK(fake_power_command && fake_power_pad && fake_nsleep_command &&
            fake_nsleep_pad && fake_configure_calls == 1u &&
            fake_verify_calls == 0u && !fake_moe && !fake_driver_outputs,
        "startup only prepares logic power, driver sleep, and configuration");
  CHECK(fake_stage_at_config == GL30_POWER_WAIT_WAKE &&
            fake_config_generation == 0u && fake_masked_io_violations == 0u,
        "configure runs interruptible with the captured off generation");

  g_current_zero.state = GL30_CURRENT_ZERO_READY;
  refresh_inputs(71001u, 13.0f); /* 49,999 us after WAIT_ZERO began. */
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_COMPLETE && fake_verify_calls == 1u &&
            fake_driver_verified,
        "ready zero verifies the driver before the 50 ms deadline");
  CHECK(fake_stage_at_verify == GL30_POWER_VERIFY_DRIVER &&
            fake_verify_generation == fake_off_generation && !fake_moe &&
            !fake_driver_outputs && fake_arm_calls == 0u,
        "verification completes preparation without MOE or arm");
  CHECK(fake_configure_calls == 1u && fake_verify_calls == 1u &&
            fake_masked_io_violations == 0u,
        "configuration and verification each run once outside IRQ masking");

  refresh_inputs(72000u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_COMPLETE && !fake_moe &&
            fake_arm_calls == 0u,
        "complete preparation remains unarmed on later monitor calls");
}

static void test_initial_gates_and_bus_timeout(void) {
  reset_fixture();
  g_motor_temperature_sample_received = false;
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_WAIT_LOGIC && !fake_power_command &&
            fake_configure_calls == 0u && fake_invalidate_calls == 0u,
        "missing ADC sample before startup does not energize logic power");

  reset_fixture();
  fake_encoder_ready = false;
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_WAIT_LOGIC && !fake_power_command &&
            fake_configure_calls == 0u && fake_invalidate_calls == 0u,
        "unhealthy encoder before startup does not energize logic power");

  reset_fixture();
  fake_drvoff_pad = false;
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED && !fake_power_command &&
            !fake_nsleep_command && fake_invalidate_calls == 1u,
        "DRVOFF command-pad mismatch fails before power enable");

  reset_fixture();
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_WAIT_BUS && fake_power_command,
        "valid ADC and encoder prepare the first bus wait");
  refresh_inputs(100999u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_WAIT_BUS && fake_invalidate_calls == 0u,
        "bus timeout does not fire one microsecond before 100 ms");
  refresh_inputs(101000u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            (g_safety.fault_bits & GL30_FAULT_VBUS) != 0u &&
            !fake_power_command && !fake_nsleep_command && fake_moe == false,
        "100 ms VM timeout latches VBUS and powers down before NSLEEP");
  assert_failed_is_sticky();
}

static void test_startup_fault_inputs_and_zero_timeout(void) {
  reset_fixture();
  start_power_stage();
  fake_now_us = 1499u;
  g_last_adc_sample_us = 1000u;
  fake_encoder_sample.timestamp_us = fake_now_us;
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_WAIT_BUS &&
            fake_invalidate_calls == 0u,
        "ADC age of 499 us remains fresh during startup");

  reset_fixture();
  start_power_stage();
  refresh_inputs(1500u, 13.0f); /* ADC age is exactly the 500 us limit. */
  g_last_adc_sample_us = 1000u;
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            (g_safety.fault_bits & GL30_FAULT_ADC_SYNC) != 0u &&
            !fake_power_command,
        "stale ADC sample after power enable fails startup closed");
  assert_failed_is_sticky();

  reset_fixture();
  start_power_stage();
  fake_power_pad = false;
  refresh_inputs(1100u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            (g_safety.fault_bits & GL30_FAULT_STARTUP) != 0u &&
            !fake_power_command,
        "motor-power IDR low despite ODR high fails startup");

  reset_fixture();
  start_power_stage();
  fake_encoder_ready = false;
  refresh_inputs(1100u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            (g_safety.fault_bits & GL30_FAULT_ENCODER) != 0u &&
            !fake_power_command && !fake_nsleep_command,
        "encoder loss after power enable fails and powers down startup");

  reset_fixture();
  start_power_stage();
  fake_moe = true;
  refresh_inputs(1100u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            (g_safety.fault_bits & GL30_FAULT_STARTUP) != 0u &&
            !fake_moe && !fake_power_command && !fake_nsleep_command,
        "unexpected MOE during startup forces the emergency off path");

  reset_fixture();
  advance_to_wait_wake();
  fake_nsleep_pad = false;
  refresh_inputs(11002u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            (g_safety.fault_bits & GL30_FAULT_STARTUP) != 0u &&
            !fake_nsleep_command && !fake_power_command,
        "NSLEEP IDR mismatch after wake fails startup closed");

  reset_fixture();
  advance_to_wait_wake();
  refresh_inputs(11002u, 8.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            (g_safety.fault_bits & GL30_FAULT_VBUS) != 0u &&
            !fake_nsleep_command && !fake_power_command,
        "VM below run threshold after wake latches and powers down");

  reset_fixture();
  advance_to_wait_wake();
  refresh_inputs(21001u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_WAIT_ZERO,
        "zero-timeout fixture reaches collection stage");
  refresh_inputs(71002u, 13.0f); /* Exactly 50 ms; readiness loses to timeout. */
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            (g_safety.fault_bits & GL30_FAULT_STARTUP) != 0u &&
            fake_verify_calls == 0u && !fake_nsleep_command,
        "zero qualification at the 50 ms deadline fails before verify");
}

static void test_driver_failures_and_mid_operation_emergency(void) {
  reset_fixture();
  advance_to_wait_wake();
  fake_config_result = false;
  refresh_inputs(21001u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            (g_safety.fault_bits & GL30_FAULT_STARTUP) != 0u &&
            fake_configure_calls == 1u && fake_verify_calls == 0u &&
            fake_invalidate_calls == 1u,
        "configuration failure latches startup fault and invalidates power");
  assert_failed_is_sticky();

  reset_fixture();
  advance_to_wait_zero();
  g_current_zero.state = GL30_CURRENT_ZERO_READY;
  fake_verify_result = false;
  refresh_inputs(22000u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            (g_safety.fault_bits & GL30_FAULT_STARTUP) != 0u &&
            fake_verify_calls == 1u && !fake_moe && fake_invalidate_calls == 1u,
        "verification failure latches and de-energizes the startup stage");
  assert_failed_is_sticky();

  reset_fixture();
  advance_to_wait_wake();
  fake_emergency_during_config = true;
  refresh_inputs(21001u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            !fake_driver_configured && fake_verify_calls == 0u &&
            fake_invalidate_calls == 1u,
        "emergency during configure cannot be overwritten by its success return");
  assert_failed_is_sticky();

  reset_fixture();
  advance_to_wait_zero();
  g_current_zero.state = GL30_CURRENT_ZERO_READY;
  fake_emergency_during_verify = true;
  refresh_inputs(22000u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            !fake_driver_verified && fake_moe == false &&
            fake_invalidate_calls == 1u,
        "emergency during verification cannot publish COMPLETE afterward");
  assert_failed_is_sticky();
}

static void test_irq_restore_and_primask_preservation(void) {
  reset_fixture();
  advance_to_wait_wake();
  fake_emergency_on_irq_restore = true;
  const unsigned sets_before = fake_gpio_set_count;
  refresh_inputs(21001u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            fake_configure_calls == 0u && fake_verify_calls == 0u &&
            fake_gpio_set_count == sets_before && !fake_power_command &&
            !fake_nsleep_command,
        "pending emergency on IRQ restore prevents the driver callback");

  reset_fixture();
  fake_primask = 1u;
  start_power_stage();
  CHECK(fake_primask == 1u && g_power_stage == GL30_POWER_FAILED &&
            fake_configure_calls == 0u && fake_verify_calls == 0u &&
            !fake_power_command && !fake_nsleep_command,
        "masked caller is failed closed without enabling IRQ or running SPI");
}

static void prepare_monitor_ready_inputs(uint64_t now_us) {
  reset_fixture();
  refresh_inputs(now_us, 13.0f);
  fake_power_command = true;
  fake_power_pad = true;
  fake_nsleep_command = true;
  fake_nsleep_pad = true;
  fake_drvoff_command = true;
  fake_drvoff_pad = true;
  fake_driver_configured = true;
  fake_driver_verified = true;
  g_power_stage = GL30_POWER_COMPLETE;
  g_current_zero.state = GL30_CURRENT_ZERO_READY;
  g_monitor_due = true;
}

static void test_real_receive_and_first_ready_timestamp(void) {
  uint8_t payload[GL30_HAPTIC_COMMAND_LEN] = {0};
  gl30_haptic_command_t command = {
      .commandNonce = TEST_CONTROL_NONCE + 1u,
      .userTorqueLimitNm = 0.030f,
      .leaseGeneration = TEST_CONTROL_GENERATION,
  };
  gl30_frame_t frame = {
      .type = GL30_V6_FRAME_PAYLOAD_HAPTIC_COMMAND,
      .payload_len = GL30_HAPTIC_COMMAND_LEN,
      .sequence = 1u,
      .payload = payload,
  };

  CHECK(gl30_encode_haptic_command(&command, payload, sizeof(payload)) == 0,
        "fixture builds a real 72-byte command with the current lease generation");
  prepare_monitor_ready_inputs(5000u);
  establish_test_control_lease();
  handle_parsed_frame(&frame);
  CHECK(g_pending_command_ready && g_pending_command_received_us == 5000u,
        "parsed command is timestamped at its actual local receive time");

  refresh_inputs(6000u, 13.0f);
  g_monitor_due = true;
  run_monitor(6000u);
  CHECK(g_safety.startup_state == GL30_STARTUP_READY &&
            g_arm_ready_since_us == 6000u && fake_ntc_calls == 1u,
        "first real monitor transition to READY publishes its local timestamp");

  fake_now_us = 6500u;
  process_pending_command();
  CHECK(fake_apply_calls == 1u && fake_command_arm_calls == 0u &&
            !g_safety.arm_requested &&
            g_safety.last_valid_command_us == 5000u,
        "a locally received pre-READY command cannot arm after READY publishes");

  fake_now_us = 7000u;
  command.commandNonce++;
  CHECK(gl30_encode_haptic_command(&command, payload, sizeof(payload)) == 0,
        "post-READY command has a distinct nonce and valid current lease");
  frame.sequence = 2u;
  handle_parsed_frame(&frame);
  CHECK(g_pending_command_received_us == 7000u,
        "a later parsed command receives a fresh local timestamp");
  process_pending_command();
  CHECK(fake_apply_calls == 2u && fake_command_arm_calls == 1u &&
            g_safety.startup_state == GL30_STARTUP_ACTIVE && fake_moe,
        "a newly received post-READY command may arm through the normal path");
}

static void test_tim7_wait_logic_adc_timeout(void) {
  reset_fixture();
  g_motor_temperature_sample_received = false;
  g_power_stage = GL30_POWER_WAIT_LOGIC;
  g_power_stage_started_us = 1000u;
  fake_now_us = 50999u;
  fake_tim7_update = true;
  gl30_app_tim7_irq();
  CHECK(g_power_stage == GL30_POWER_WAIT_LOGIC &&
            (g_safety.fault_bits & GL30_FAULT_ADC_SYNC) == 0u &&
            fake_tim7_clear_calls == 1u,
        "TIM7 does not fault one microsecond before WAIT_LOGIC ADC deadline");

  fake_now_us = 51000u;
  fake_tim7_update = true;
  gl30_app_tim7_irq();
  CHECK(g_power_stage == GL30_POWER_FAILED &&
            (g_safety.fault_bits & GL30_FAULT_ADC_SYNC) != 0u &&
            fake_invalidate_calls == 1u && !fake_power_command &&
            !fake_nsleep_command && !fake_moe && fake_primask == 0u &&
            fake_tim7_clear_calls == 2u,
        "TIM7 latches ADC_SYNC and powers off at the 50 ms WAIT_LOGIC deadline");
}

static void test_idle_fault_line_requires_released_drvoff(void) {
  prepare_monitor_ready_inputs(8000u);
  g_safety.startup_state = GL30_STARTUP_READY;
  fake_drvoff_command = false;
  fake_drvoff_pad = false;
  fake_pb12_high = false;
  fake_moe = false;
  fake_driver_outputs = false;
  fake_tim7_update = true;
  gl30_app_tim7_irq();
  CHECK((g_safety.fault_bits & GL30_FAULT_HARDWARE_BKIN) != 0u &&
            g_power_stage == GL30_POWER_FAILED && !fake_moe &&
            !fake_driver_outputs && fake_invalidate_calls == 1u &&
            !fake_power_command && !fake_nsleep_command &&
            !fake_fault_pin_high,
        "idle TIM7 detects asserted PB12 with DRVOFF released and fails closed");

  prepare_monitor_ready_inputs(9000u);
  g_safety.startup_state = GL30_STARTUP_READY;
  gl30_drv8316_safe_off();
  fake_pb12_high = false;
  g_monitor_due = true;
  run_monitor(9000u);
  CHECK(g_power_stage == GL30_POWER_COMPLETE &&
            (g_safety.fault_bits & GL30_FAULT_HARDWARE_BKIN) == 0u &&
            fake_drvoff_command && fake_drvoff_pad && fake_fault_pin_high,
        "low PB12 while ordinary safe-off holds DRVOFF high is not a fault");

  prepare_monitor_ready_inputs(9500u);
  g_safety.startup_state = GL30_STARTUP_READY;
  fake_drvoff_command = false;
  fake_drvoff_pad = false;
  fake_pb12_high = false;
  g_monitor_due = true;
  run_monitor(9500u);
  CHECK((g_safety.fault_bits & GL30_FAULT_HARDWARE_BKIN) != 0u &&
            g_power_stage == GL30_POWER_FAILED && fake_invalidate_calls == 1u &&
            !fake_power_command && !fake_nsleep_command,
        "foreground COMPLETE monitor latches asserted PB12 when DRVOFF is low");
}

static void test_verify_cannot_publish_complete_with_asserted_pb12(void) {
  reset_fixture();
  refresh_inputs(30000u, 13.0f);
  fake_power_command = true;
  fake_power_pad = true;
  fake_nsleep_command = true;
  fake_nsleep_pad = true;
  fake_driver_configured = true;
  g_current_zero.state = GL30_CURRENT_ZERO_READY;
  g_power_stage = GL30_POWER_WAIT_ZERO;
  g_power_stage_started_us = 10000u;
  fake_pb12_low_after_verify = true;

  run_power_startup();
  CHECK(fake_verify_calls == 1u && fake_verify_result && !fake_driver_verified &&
            !fake_pb12_high &&
            (g_safety.fault_bits & GL30_FAULT_HARDWARE_BKIN) != 0u &&
            g_power_stage == GL30_POWER_FAILED && !fake_moe &&
            !fake_driver_outputs && !fake_power_command &&
            !fake_nsleep_command,
        "PB12 assertion after verify return prevents COMPLETE publication");
}

static void prepare_command_ready_state(uint64_t now_us,
                                        uint64_t arm_ready_since_us,
                                        uint64_t received_us) {
  reset_fixture();
  refresh_inputs(now_us, 13.0f);
  fake_power_command = true;
  fake_power_pad = true;
  fake_nsleep_command = true;
  fake_nsleep_pad = true;
  fake_driver_configured = true;
  fake_driver_verified = true;
  g_pending_command = (gl30_haptic_command_t){
      .commandNonce = TEST_CONTROL_NONCE + 1u,
      .userTorqueLimitNm = 0.030f,
      .leaseGeneration = TEST_CONTROL_GENERATION,
  };
  g_power_stage = GL30_POWER_COMPLETE;
  g_current_zero.state = GL30_CURRENT_ZERO_READY;
  g_safety.startup_state = GL30_STARTUP_READY;
  g_safety.comm_state = GL30_COMM_WAITING;
  g_safety.power_ok = true;
  g_safety.self_test_ok = true;
  g_safety.encoder_ok = true;
  g_safety.driver_ok = true;
  g_safety.adc_zero_ok = true;
  g_safety.alignment_ok = true;
  g_arm_ready_since_us = arm_ready_since_us;
  g_pending_command_ready = true;
  g_pending_command_received_us = received_us;
  g_pending_command_sequence = 1u;
  establish_test_control_lease();
}

static void queue_pending_torque_command(uint32_t sequence,
                                         uint64_t received_us,
                                         float torque_limit_nm) {
  g_pending_command = (gl30_haptic_command_t){
      .commandNonce = TEST_CONTROL_NONCE + sequence,
      .userTorqueLimitNm = torque_limit_nm,
      .leaseGeneration = TEST_CONTROL_GENERATION,
  };
  g_pending_command_ready = true;
  g_pending_command_sequence = sequence;
  g_pending_command_received_us = received_us;
}

static void test_zero_torque_command_stops_and_allows_explicit_rearm(void) {
  prepare_command_ready_state(5000u, 1000u, 2000u);
  g_pending_command.userTorqueLimitNm = 0.0f;
  process_pending_command();
  CHECK(fake_apply_calls == 1u && fake_last_torque_limit == 0.0f &&
            g_safety.last_valid_command_us == 2000u &&
            g_safety.comm_state == GL30_COMM_NORMAL &&
            !g_safety.arm_requested && g_safety.startup_state == GL30_STARTUP_READY &&
            fake_command_arm_calls == 0u,
        "zero-torque command refreshes communications but never arms from READY");
  CHECK(fake_force_zero_calls == 0u && fake_safe_off_calls == 0u &&
            !fake_moe && !fake_driver_outputs && fake_drvoff_command &&
            fake_drvoff_pad && fake_driver_configured && fake_driver_verified &&
            fake_power_command && fake_power_pad && fake_nsleep_command &&
            fake_nsleep_pad,
        "idle zero command preserves neutral FOC, driver preparation, and disabled bridge");

  prepare_command_ready_state(6000u, 1000u, 2000u);
  g_safety.startup_state = GL30_STARTUP_FOC_ALIGN;
  g_safety.alignment_ok = false;
  g_arm_ready_since_us = 0u;
  g_pending_command.userTorqueLimitNm = 0.0f;
  process_pending_command();
  CHECK(!g_safety.arm_requested && g_safety.startup_state == GL30_STARTUP_FOC_ALIGN &&
            fake_command_arm_calls == 0u && fake_force_zero_calls == 0u &&
            fake_safe_off_calls == 0u && !fake_moe && !fake_driver_outputs &&
            fake_driver_configured && fake_driver_verified,
        "zero-torque command cannot bypass FOC alignment and still holds torque off");

  prepare_command_ready_state(7000u, 1000u, 6000u);
  process_pending_command();
  CHECK(fake_command_arm_calls == 1u && g_safety.startup_state == GL30_STARTUP_ACTIVE &&
            fake_moe && fake_driver_outputs,
        "fresh positive-torque command arms through the explicit normal path");

  /* Stand in for the real FOC tick that runs between command acceptance and
   * the following stop command. */
  g_foc.torque_command_nm = 0.025f;
  g_foc.haptic_torque_nm = 0.020f;
  queue_pending_torque_command(2u, 7000u, 0.0f);
  fake_now_us = 7000u;
  process_pending_command();
  CHECK(g_safety.last_valid_command_us == 7000u &&
            g_safety.startup_state == GL30_STARTUP_READY &&
            !g_safety.arm_requested && !fake_moe && !fake_driver_outputs &&
            fake_drvoff_command && fake_drvoff_pad &&
            fake_driver_configured && fake_driver_verified &&
            fake_power_command && fake_power_pad && fake_nsleep_command &&
            fake_nsleep_pad,
        "zero-torque command immediately stops ACTIVE bridge and retains power/config");
  CHECK(fake_force_zero_calls == 1u && fake_safe_off_calls == 1u &&
            g_foc.torque_command_nm == 0.0f,
        "ACTIVE stop zeroes non-neutral FOC state and performs one ordinary safe-off");

  queue_pending_torque_command(3u, 8000u, 0.030f);
  fake_now_us = 8000u;
  process_pending_command();
  CHECK(fake_command_arm_calls == 2u && g_safety.startup_state == GL30_STARTUP_ACTIVE &&
            fake_moe && fake_driver_outputs,
        "fresh positive-torque command can explicitly re-arm after ordinary stop");
}

static void test_pending_command_age_and_ready_generation(void) {
  prepare_command_ready_state(2000u, 1000u, 999u);
  process_pending_command();
  CHECK(fake_apply_calls == 1u && fake_command_arm_calls == 0u &&
            !g_safety.arm_requested &&
            g_safety.startup_state == GL30_STARTUP_READY,
        "command received before READY cannot arm after delayed processing");

  prepare_command_ready_state(2000u, 1000u, 1000u);
  process_pending_command();
  CHECK(fake_apply_calls == 1u && fake_command_arm_calls == 1u &&
            g_safety.arm_requested &&
            g_safety.startup_state == GL30_STARTUP_ACTIVE &&
            fake_driver_outputs && fake_moe,
        "fresh command received at READY may arm after readiness gates pass");
  CHECK(fake_arm_generation == fake_off_generation &&
            fake_masked_io_violations == 0u,
        "arm uses the current off generation outside the masked critical section");

  prepare_command_ready_state(20000u, 10000u, 10000u);
  g_safety.last_valid_command_us = 77u;
  g_safety.comm_state = GL30_COMM_WARN;
  process_pending_command();
  CHECK(fake_apply_calls == 0u && fake_command_arm_calls == 0u &&
            g_safety.last_valid_command_us == 77u &&
            g_safety.comm_state == GL30_COMM_WARN,
        "command at the 10 ms age limit is rejected without refreshing comms");

  prepare_command_ready_state(2000u, 1000u, 2001u);
  g_safety.last_valid_command_us = 88u;
  process_pending_command();
  CHECK(fake_apply_calls == 0u && g_safety.last_valid_command_us == 88u,
        "future command timestamp is rejected without refreshing comms");

  prepare_command_ready_state(2000u, 1000u, 0u);
  g_safety.last_valid_command_us = 99u;
  process_pending_command();
  CHECK(fake_apply_calls == 0u && g_safety.last_valid_command_us == 99u,
        "zero command timestamp is rejected without refreshing comms");

  prepare_command_ready_state(20000u, 10000u, 10001u);
  process_pending_command();
  CHECK(fake_apply_calls == 1u && fake_command_arm_calls == 1u &&
            g_safety.startup_state == GL30_STARTUP_ACTIVE,
        "command age of 9,999 us remains eligible after READY");
}

static void test_pending_command_expires_before_critical_commit(void) {
  prepare_command_ready_state(10999u, 1000u, 1000u);
  g_safety.last_valid_command_us = 4321u;
  g_safety.comm_state = GL30_COMM_WARN;
  fake_timebase_read_count = 0u;
  fake_advance_time_after_next_read = true;
  fake_time_after_next_read_us = 11000u;
  process_pending_command();
  CHECK(fake_timebase_read_count == 2u && !fake_advance_time_after_next_read &&
            fake_now_us == 11000u,
        "age fixture advances from 9,999 to exactly 10,000 us between checks");
  CHECK(fake_apply_calls == 0u && fake_command_arm_calls == 0u &&
            !g_pending_command_ready && g_pending_command_received_us == 0u &&
            g_dropped_commands == 1u &&
            g_safety.last_valid_command_us == 4321u &&
            g_safety.comm_state == GL30_COMM_WARN && fake_primask == 0u,
        "second critical-section age check rejects a command that expires before commit");
}

static void test_warm_arm_pb12_settling_is_not_idle_fault(void) {
  prepare_command_ready_state(7000u, 1000u, 6000u);
  fake_arm_inject_pb12_low_during_release = true;

  process_pending_command();

  CHECK(fake_command_arm_calls == 1u && fake_arm_tim7_injections == 1u &&
            fake_tim7_clear_calls == 1u && fake_arm_guard_seen_at_tim7 &&
            !fake_arm_moe_seen_at_tim7 && !fake_arm_outputs_seen_at_tim7,
        "arm callback injected one update through the real TIM7 handler");
  CHECK(g_power_stage == GL30_POWER_COMPLETE &&
            (g_safety.fault_bits & GL30_FAULT_HARDWARE_BKIN) == 0u &&
            g_safety.startup_state == GL30_STARTUP_ACTIVE && fake_moe &&
            fake_driver_outputs && !fake_drvoff_command && !fake_drvoff_pad &&
            fake_pb12_high,
        "PB12 settling low during DRVOFF release does not abort a healthy arm");
  CHECK(!g_driver_arm_in_progress,
        "arm-in-progress guard is cleared after the driver call returns");
}

static void test_warm_arm_stuck_pb12_fails_closed_and_zero_does_not_recover(void) {
  prepare_command_ready_state(7000u, 1000u, 6000u);
  fake_arm_inject_pb12_low_during_release = true;
  fake_arm_keep_pb12_low = true;
  fake_arm_result = false;

  process_pending_command();

  CHECK(fake_arm_tim7_injections == 1u && !fake_pb12_high &&
            g_power_stage == GL30_POWER_FAILED && g_safety.fault_bits != 0u &&
            !fake_moe && !fake_driver_outputs && !fake_power_command &&
            !fake_nsleep_command && !g_driver_arm_in_progress,
        "failed warm arm with PB12 still low latches a fault and keeps outputs off");

  queue_pending_torque_command(2u, fake_now_us, 0.0f);
  process_pending_command();
  CHECK(g_power_stage == GL30_POWER_FAILED && g_safety.fault_bits != 0u &&
            !fake_moe && !fake_driver_outputs && !fake_power_command &&
            !fake_nsleep_command,
        "zero-torque heartbeat cannot clear a fault or restore power");
}

static void test_arm_monitoring_resumes_when_outputs_enable(void) {
  prepare_command_ready_state(9000u, 1000u, 8000u);
  fake_arm_inject_pb12_low_during_release = true;
  fake_arm_keep_pb12_low = true;
  fake_arm_enable_moe_during_release = true;
  process_pending_command();
  CHECK(fake_arm_guard_seen_at_tim7 && fake_arm_moe_seen_at_tim7 &&
            !fake_arm_outputs_seen_at_tim7 &&
            (g_safety.fault_bits & GL30_FAULT_HARDWARE_BKIN) != 0u &&
            g_power_stage == GL30_POWER_FAILED && !fake_moe &&
            !fake_driver_outputs && !g_driver_arm_in_progress,
        "PB12 low is latched during arm once TIM1 MOE becomes active");

  prepare_command_ready_state(10000u, 1000u, 9000u);
  fake_arm_inject_pb12_low_during_release = true;
  fake_arm_keep_pb12_low = true;
  fake_arm_enable_driver_outputs_during_release = true;
  process_pending_command();
  CHECK(fake_arm_guard_seen_at_tim7 && !fake_arm_moe_seen_at_tim7 &&
            fake_arm_outputs_seen_at_tim7 &&
            (g_safety.fault_bits & GL30_FAULT_HARDWARE_BKIN) != 0u &&
            g_power_stage == GL30_POWER_FAILED && !fake_moe &&
            !fake_driver_outputs && !g_driver_arm_in_progress,
        "PB12 low is latched during arm once driver outputs report enabled");
}

static void test_drvoff_pad_low_is_not_hidden_by_high_odr(void) {
  prepare_command_ready_state(8000u, 1000u, 7000u);
  fake_drvoff_command = true;
  fake_drvoff_pad = false;
  fake_pb12_high = false;
  fake_moe = false;
  fake_driver_outputs = false;
  fake_tim7_update = true;

  gl30_app_tim7_irq();

  CHECK((g_safety.fault_bits & GL30_FAULT_HARDWARE_BKIN) != 0u &&
            g_power_stage == GL30_POWER_FAILED && !fake_moe &&
            !fake_driver_outputs && fake_invalidate_calls == 1u &&
            !fake_power_command && !fake_nsleep_command,
        "TIM7 fails closed when DRVOFF ODR is high but its pad and PB12 are low");
}

static void prepare_latched_safe_state(void) {
  g_power_stage = GL30_POWER_FAILED;
  g_safety.fault_bits = GL30_FAULT_STARTUP;
  fake_moe = false;
  fake_driver_outputs = false;
  fake_drvoff_command = true;
  fake_drvoff_pad = true;
  fake_nsleep_command = false;
  fake_nsleep_pad = false;
  fake_power_command = false;
  fake_power_pad = false;
  g_motor_temperature_sample_received = false;
}

static void test_watchdog_reset_latches_and_prevents_retry(void) {
  reset_fixture();
  fake_iwdg_reset_flag = true;
  check_watchdog_reset();
  CHECK(fake_reset_flags_clear_calls == 1u && !fake_iwdg_reset_flag &&
            !fake_wwdg_reset_flag,
        "watchdog reset cause is sampled and reset flags are cleared");
  CHECK((g_safety.fault_bits & GL30_FAULT_STARTUP) != 0u &&
            g_power_stage == GL30_POWER_FAILED &&
            !fake_power_command && !fake_nsleep_command && !fake_moe &&
            fake_invalidate_calls == 1u,
        "watchdog reset latches startup failure and holds power off");
  refresh_inputs(5000u, 13.0f);
  start_power_stage();
  CHECK(g_power_stage == GL30_POWER_FAILED && fake_configure_calls == 0u &&
            fake_verify_calls == 0u && !fake_power_command &&
            !fake_nsleep_command,
        "watchdog reset state cannot automatically restart preparation");
}

static void test_watchdog_requires_progress_and_safe_state(void) {
  reset_fixture();
  prepare_latched_safe_state();
  g_last_watchdog_refresh_us = 0u;
  g_control_progress = g_last_watchdog_progress;
  g_safety_progress = g_last_watchdog_safety_progress + 1u;
  g_last_adc_sample_us = 0u;
  g_motor_temperature_sample_received = false;
  service_watchdog(10000u);
  CHECK(fake_iwdg_reload_calls == 1u,
        "latched safe-off keeps watchdog serviced while ADC is stopped and TIM7 progresses");

  reset_fixture();
  prepare_latched_safe_state();
  g_last_watchdog_refresh_us = 0u;
  g_safety_progress = g_last_watchdog_safety_progress;
  g_control_progress = g_last_watchdog_progress;
  service_watchdog(10000u);
  CHECK(fake_iwdg_reload_calls == 0u,
        "watchdog is not fed when both TIM7 and control progress stop");

  reset_fixture();
  prepare_latched_safe_state();
  fake_drvoff_pad = false;
  g_last_watchdog_refresh_us = 0u;
  g_safety_progress = g_last_watchdog_safety_progress + 1u;
  g_control_progress = g_last_watchdog_progress;
  g_motor_temperature_sample_received = false;
  service_watchdog(10000u);
  CHECK(fake_iwdg_reload_calls == 0u,
        "watchdog is not fed for a latched fault with DRVOFF pad still low");

  reset_fixture();
  g_power_stage = GL30_POWER_COMPLETE;
  g_last_watchdog_refresh_us = 0u;
  g_safety_progress = g_last_watchdog_safety_progress + 1u;
  g_control_progress = g_last_watchdog_progress + 1u;
  refresh_inputs(10000u, 13.0f);
  service_watchdog(10000u);
  CHECK(fake_iwdg_reload_calls == 1u,
        "healthy operation feeds watchdog only with TIM7, ADC, and control progress");

  reset_fixture();
  prepare_latched_safe_state();
  g_last_watchdog_refresh_us = 5000u;
  g_safety_progress = g_last_watchdog_safety_progress + 1u;
  service_watchdog(14999u);
  CHECK(fake_iwdg_reload_calls == 0u,
        "watchdog refresh interval remains at least 10 ms");
}

int main(void) {
  reset_fixture();
  test_ordered_preparation_stays_unarmed();
  test_initial_gates_and_bus_timeout();
  test_startup_fault_inputs_and_zero_timeout();
  test_driver_failures_and_mid_operation_emergency();
  test_irq_restore_and_primask_preservation();
  test_real_receive_and_first_ready_timestamp();
  test_tim7_wait_logic_adc_timeout();
  test_idle_fault_line_requires_released_drvoff();
  test_verify_cannot_publish_complete_with_asserted_pb12();
  test_zero_torque_command_stops_and_allows_explicit_rearm();
  test_pending_command_age_and_ready_generation();
  test_pending_command_expires_before_critical_commit();
  test_warm_arm_pb12_settling_is_not_idle_fault();
  test_warm_arm_stuck_pb12_fails_closed_and_zero_does_not_recover();
  test_arm_monitoring_resumes_when_outputs_enable();
  test_drvoff_pad_low_is_not_hidden_by_high_odr();
  test_watchdog_reset_latches_and_prevents_retry();
  test_watchdog_requires_progress_and_safe_state();
  printf("product power startup: %u checks, %u failed\n", checks, failed);
  return failed == 0u ? 0 : 1;
}
