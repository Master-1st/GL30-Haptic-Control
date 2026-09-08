/* GL30 AMOLED V7 product application.
 *
 * CubeMX owns clock, pin and peripheral register initialization. This file
 * owns the application scheduler and the LL-only run-time control paths.
 * Torque remains fail-closed until the factory encoder protocol and the
 * electrical zero are confirmed on real hardware.
 */

#include "gl30_app.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "main.h"

#include "board_config.h"
#include "drv8316.h"
#include "factory_encoder.h"
#include "foc.h"
#include "haptics.h"
#include "ina228.h"
#include "safety_supervisor.h"
#include "timebase.h"
#include "trace_buffer.h"
#include "v6_protocol.h"
#include "veml7700.h"

#define GL30_UART_RX_BUFFER_SIZE 256u
#define GL30_UART_TX_BUFFER_SIZE 128u

void DMA1_Channel1_IRQHandler(void);
void DMA1_Channel2_IRQHandler(void);

static gl30_safety_t g_safety;
static gl30_foc_state_t g_foc;
static gl30_factory_encoder_sample_t g_encoder;

static __ALIGNED(4) uint8_t g_uart_rx[GL30_UART_RX_BUFFER_SIZE];
static __ALIGNED(4) uint8_t g_uart_tx[GL30_UART_TX_BUFFER_SIZE];
static volatile uint16_t g_uart_rx_consumed;
static volatile bool g_uart_tx_busy;
static volatile bool g_pending_command_ready;
static gl30_haptic_command_t g_pending_command;
static volatile uint32_t g_pending_command_sequence;

static volatile bool g_telemetry_due;
static volatile bool g_slow_telemetry_due;
static gl30_ina228_sample_t g_power_monitor;
static gl30_veml7700_sample_t g_ambient_light;
static uint64_t g_last_ina_config_attempt_us;
static uint64_t g_last_veml_config_attempt_us;
static uint32_t g_slow_sensor_tick_divider;
static volatile bool g_monitor_due;
static volatile uint32_t g_control_progress;
static volatile uint16_t g_last_isr_cycles;
static volatile uint16_t g_encoder_status;
static volatile uint32_t g_dropped_commands;
static volatile uint32_t g_telemetry_drops;
static uint32_t g_tx_sequence;
static uint32_t g_last_rx_sequence;
static bool g_have_rx_sequence;

static volatile uint32_t g_adc_zero_samples;
static volatile uint32_t g_adc_zero_sum_a;
static volatile uint32_t g_adc_zero_sum_b;
static volatile uint32_t g_adc_zero_sum_c;
static volatile bool g_adc_zero_ready;
static float g_adc_zero_a = GL30_ADC_ZERO_DEFAULT_COUNTS;
static float g_adc_zero_b = GL30_ADC_ZERO_DEFAULT_COUNTS;
static float g_adc_zero_c = GL30_ADC_ZERO_DEFAULT_COUNTS;
static volatile float g_vbus_v;
static volatile float g_motor_temperature_c;

static volatile uint32_t g_haptic_tick_divider;
static volatile bool g_trace_frozen;
static uint64_t g_last_driver_config_attempt_us;
static uint64_t g_last_watchdog_toggle_us;
static uint64_t g_last_watchdog_refresh_us;
static uint32_t g_last_watchdog_progress;

static uint16_t saturate_u16(uint32_t value) {
  return (value > UINT16_MAX) ? UINT16_MAX : (uint16_t)value;
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

static void publish_fault_line(bool faulted) {
  if ((RCC->AHB2ENR & RCC_AHB2ENR_GPIOCEN) == 0u) {
    return;
  }
  if (faulted) {
    LL_GPIO_ResetOutputPin(SYS_FAULT_N_GPIO_Port, SYS_FAULT_N_Pin);
  } else {
    LL_GPIO_SetOutputPin(SYS_FAULT_N_GPIO_Port, SYS_FAULT_N_Pin);
  }
}

static void hardware_safe_state(void) {
  gl30_drv8316_safe_off();
  if ((RCC->AHB2ENR & RCC_AHB2ENR_GPIOAEN) != 0u) {
    LL_GPIO_ResetOutputPin(BRAKE_FORCE_TEST_GPIO_Port, BRAKE_FORCE_TEST_Pin);
    LL_GPIO_SetOutputPin(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin);
  }
}

static void latch_fault(uint32_t fault_bit) {
  hardware_safe_state();
  gl30_foc_force_zero(&g_foc);
  gl30_safety_latch_fault(&g_safety, fault_bit);
  g_trace_frozen = true;
  publish_fault_line(true);
}

void gl30_app_emergency_off(void) {
  hardware_safe_state();
  publish_fault_line(true);
}

static void configure_dwt_counter(void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void init_runtime_state(uint64_t now_us) {
  memset(&g_foc, 0, sizeof(g_foc));
  memset(&g_encoder, 0, sizeof(g_encoder));
  memset(&g_pending_command, 0, sizeof(g_pending_command));
  memset(&g_power_monitor, 0, sizeof(g_power_monitor));
  memset(&g_ambient_light, 0, sizeof(g_ambient_light));
  gl30_foc_init(&g_foc);
  gl30_safety_init(&g_safety, now_us);
  gl30_frame_parse_init();
  gl30_trace_init();
  gl30_factory_encoder_init();
  gl30_factory_encoder_snapshot(&g_encoder);
  g_encoder_status = (uint16_t)g_encoder.status;
}

static bool adc_enable(ADC_TypeDef *adc) {
  uint64_t started_us;

  if (LL_ADC_IsEnabled(adc) != 0u) {
    return true;
  }

  LL_ADC_StartCalibration(adc, LL_ADC_SINGLE_ENDED);
  started_us = gl30_timebase_now_us();
  while (LL_ADC_IsCalibrationOnGoing(adc) != 0u) {
    if (gl30_timebase_now_us() - started_us > GL30_STARTUP_ADC_TIMEOUT_US) {
      return false;
    }
  }

  LL_ADC_ClearFlag_ADRDY(adc);
  LL_ADC_Enable(adc);
  started_us = gl30_timebase_now_us();
  while (LL_ADC_IsActiveFlag_ADRDY(adc) == 0u) {
    if (gl30_timebase_now_us() - started_us > GL30_STARTUP_ADC_TIMEOUT_US) {
      return false;
    }
  }
  return true;
}

static bool start_adc_sampling(void) {
  if (!adc_enable(ADC1) || !adc_enable(ADC2) || !adc_enable(ADC3)) {
    return false;
  }

  LL_ADC_ClearFlag_JEOS(ADC1);
  LL_ADC_ClearFlag_JEOS(ADC2);
  LL_ADC_ClearFlag_JEOS(ADC3);
  LL_ADC_EnableIT_JEOS(ADC1);
  LL_ADC_INJ_StartConversion(ADC3);
  LL_ADC_INJ_StartConversion(ADC2);
  LL_ADC_INJ_StartConversion(ADC1);
  return true;
}

static void start_control_timers(void) {
  const uint32_t pwm_channels =
      LL_TIM_CHANNEL_CH1 | LL_TIM_CHANNEL_CH1N |
      LL_TIM_CHANNEL_CH2 | LL_TIM_CHANNEL_CH2N |
      LL_TIM_CHANNEL_CH3 | LL_TIM_CHANNEL_CH3N |
      LL_TIM_CHANNEL_CH4;

  LL_TIM_DisableAllOutputs(TIM1);
  LL_TIM_DisableIT_BRK(TIM1);
  LL_TIM_ClearFlag_BRK(TIM1);
  LL_TIM_OC_SetCompareCH1(TIM1, (GL30_TIM1_ARR + 1u) / 2u);
  LL_TIM_OC_SetCompareCH2(TIM1, (GL30_TIM1_ARR + 1u) / 2u);
  LL_TIM_OC_SetCompareCH3(TIM1, (GL30_TIM1_ARR + 1u) / 2u);
  LL_TIM_CC_EnableChannel(TIM1, pwm_channels);
  LL_TIM_GenerateEvent_UPDATE(TIM1);
  LL_TIM_EnableCounter(TIM1);

  LL_TIM_ClearFlag_UPDATE(TIM6);
  LL_TIM_EnableIT_UPDATE(TIM6);
  LL_TIM_EnableCounter(TIM6);
  LL_TIM_ClearFlag_UPDATE(TIM7);
  LL_TIM_EnableIT_UPDATE(TIM7);
  LL_TIM_EnableCounter(TIM7);
}

static void uart_rx_drain(void);

static bool uart_dma_init(void) {
  LL_DMA_InitTypeDef dma = {0};

  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMAMUX1);
  LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_DMA1);

  LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_1);
  dma.PeriphOrM2MSrcAddress = (uint32_t)&USART3->RDR;
  dma.MemoryOrM2MDstAddress = (uint32_t)g_uart_rx;
  dma.Direction = LL_DMA_DIRECTION_PERIPH_TO_MEMORY;
  dma.Mode = LL_DMA_MODE_CIRCULAR;
  dma.PeriphOrM2MSrcIncMode = LL_DMA_PERIPH_NOINCREMENT;
  dma.MemoryOrM2MDstIncMode = LL_DMA_MEMORY_INCREMENT;
  dma.PeriphOrM2MSrcDataSize = LL_DMA_PDATAALIGN_BYTE;
  dma.MemoryOrM2MDstDataSize = LL_DMA_MDATAALIGN_BYTE;
  dma.NbData = GL30_UART_RX_BUFFER_SIZE;
  dma.PeriphRequest = LL_DMAMUX_REQ_USART3_RX;
  dma.Priority = LL_DMA_PRIORITY_HIGH;
  if (LL_DMA_Init(DMA1, LL_DMA_CHANNEL_1, &dma) != SUCCESS) {
    return false;
  }

  LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_2);
  dma.PeriphOrM2MSrcAddress = (uint32_t)&USART3->TDR;
  dma.MemoryOrM2MDstAddress = (uint32_t)g_uart_tx;
  dma.Direction = LL_DMA_DIRECTION_MEMORY_TO_PERIPH;
  dma.Mode = LL_DMA_MODE_NORMAL;
  dma.NbData = 0u;
  dma.PeriphRequest = LL_DMAMUX_REQ_USART3_TX;
  dma.Priority = LL_DMA_PRIORITY_MEDIUM;
  if (LL_DMA_Init(DMA1, LL_DMA_CHANNEL_2, &dma) != SUCCESS) {
    return false;
  }

  LL_DMA_ClearFlag_HT1(DMA1);
  LL_DMA_ClearFlag_TC1(DMA1);
  LL_DMA_ClearFlag_TE1(DMA1);
  LL_DMA_ClearFlag_TC2(DMA1);
  LL_DMA_ClearFlag_TE2(DMA1);
  LL_DMA_EnableIT_HT(DMA1, LL_DMA_CHANNEL_1);
  LL_DMA_EnableIT_TC(DMA1, LL_DMA_CHANNEL_1);
  LL_DMA_EnableIT_TE(DMA1, LL_DMA_CHANNEL_1);
  LL_DMA_EnableIT_TC(DMA1, LL_DMA_CHANNEL_2);
  LL_DMA_EnableIT_TE(DMA1, LL_DMA_CHANNEL_2);

  NVIC_SetPriority(DMA1_Channel1_IRQn,
                   NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 3u, 0u));
  NVIC_SetPriority(DMA1_Channel2_IRQn,
                   NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 3u, 0u));
  NVIC_EnableIRQ(DMA1_Channel1_IRQn);
  NVIC_EnableIRQ(DMA1_Channel2_IRQn);

  g_uart_rx_consumed = 0u;
  LL_USART_ClearFlag_IDLE(USART3);
  LL_USART_ClearFlag_ORE(USART3);
  LL_USART_ClearFlag_NE(USART3);
  LL_USART_ClearFlag_FE(USART3);
  LL_USART_EnableIT_IDLE(USART3);
  LL_USART_EnableIT_ERROR(USART3);
  LL_USART_EnableDMAReq_RX(USART3);
  LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_1);
  return true;
}

static bool uart_tx_start(const uint8_t *data, size_t length) {
  if (data == NULL || length == 0u || length > GL30_UART_TX_BUFFER_SIZE ||
      g_uart_tx_busy) {
    return false;
  }

  LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_2);
  LL_DMA_SetMemoryAddress(DMA1, LL_DMA_CHANNEL_2, (uint32_t)data);
  LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_2, (uint32_t)length);
  LL_DMA_ClearFlag_TC2(DMA1);
  LL_DMA_ClearFlag_TE2(DMA1);
  g_uart_tx_busy = true;
  LL_USART_EnableDMAReq_TX(USART3);
  LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_2);
  return true;
}

static void handle_parsed_frame(const gl30_frame_t *frame) {
  gl30_haptic_command_t decoded;

  if (frame == NULL) {
    return;
  }
  if (frame->type != GL30_V6_FRAME_PAYLOAD_HAPTIC_COMMAND) {
    gl30_safety_on_unknown_type(&g_safety);
    return;
  }
  if (frame->payload_len != GL30_HAPTIC_COMMAND_LEN || frame->payload == NULL) {
    gl30_safety_on_bad_length(&g_safety);
    return;
  }
  if (gl30_decode_haptic_command(frame->payload, frame->payload_len, &decoded) != 0) {
    gl30_safety_on_bad_length(&g_safety);
    return;
  }
  if (g_pending_command_ready) {
    g_dropped_commands++;
    return;
  }
  g_pending_command = decoded;
  g_pending_command_sequence = frame->sequence;
  g_pending_command_ready = true;
}

static void parse_bytes(const uint8_t *data, size_t length) {
  size_t consumed = 0u;
  gl30_frame_t frame;
  gl30_parse_result_t result =
      gl30_frame_parse(data, length, &frame, &consumed);
  (void)consumed;

  for (;;) {
    if (result.status == GL30_PARSE_OK) {
      handle_parsed_frame(&frame);
    } else if (result.status == GL30_PARSE_BAD_CRC) {
      gl30_safety_on_bad_crc(&g_safety);
    } else if (result.status == GL30_PARSE_UNSUPPORTED_VERSION) {
      gl30_safety_on_unknown_version(&g_safety);
    } else if (result.status == GL30_PARSE_BAD_LENGTH ||
               result.status == GL30_PARSE_BAD_PAYLOAD_LEN ||
               result.status == GL30_PARSE_BAD_SYNC) {
      gl30_safety_on_bad_length(&g_safety);
    } else {
      break;
    }
    result = gl30_frame_parse(NULL, 0u, &frame, NULL);
  }
}

static void uart_rx_drain(void) {
  uint16_t produced = (uint16_t)(GL30_UART_RX_BUFFER_SIZE -
                                 LL_DMA_GetDataLength(DMA1, LL_DMA_CHANNEL_1));

  while (g_uart_rx_consumed != produced) {
    uint16_t count;
    if (produced > g_uart_rx_consumed) {
      count = (uint16_t)(produced - g_uart_rx_consumed);
    } else {
      count = (uint16_t)(GL30_UART_RX_BUFFER_SIZE - g_uart_rx_consumed);
    }
    parse_bytes(&g_uart_rx[g_uart_rx_consumed], count);
    g_uart_rx_consumed =
        (uint16_t)((g_uart_rx_consumed + count) % GL30_UART_RX_BUFFER_SIZE);
    produced = (uint16_t)(GL30_UART_RX_BUFFER_SIZE -
                          LL_DMA_GetDataLength(DMA1, LL_DMA_CHANNEL_1));
  }
}

static void process_pending_command(void) {
  gl30_haptic_command_t command;
  uint32_t sequence;
  uint32_t primask;
  uint64_t now_us;
  bool accepted;
  bool encoder_ok;
  bool should_arm;
  uint32_t expected_off_generation;

  if (!g_pending_command_ready) {
    return;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  command = g_pending_command;
  sequence = g_pending_command_sequence;
  g_pending_command_ready = false;
  if (primask == 0u) {
    __enable_irq();
  }

  now_us = gl30_timebase_now_us();
  primask = __get_PRIMASK();
  __disable_irq();
  accepted = gl30_foc_apply_command(&g_foc, &command);
  gl30_factory_encoder_snapshot(&g_encoder);
  g_encoder_status = (uint16_t)g_encoder.status;
  encoder_ok = control_ready(&g_encoder, now_us, GL30_ENCODER_STALE_US);
  expected_off_generation = gl30_drv8316_off_generation_snapshot();
  if (accepted) {
    gl30_safety_on_valid_command(&g_safety, now_us);
    if (encoder_ok) {
      gl30_safety_request_arm(&g_safety);
    } else {
      gl30_safety_disarm(&g_safety);
    }
  }
  should_arm = accepted && encoder_ok &&
               gl30_safety_torque_allowed(&g_safety) &&
               !gl30_drv8316_outputs_enabled();
  if (primask == 0u) {
    __enable_irq();
  }

  if (!accepted) {
    g_dropped_commands++;
    return;
  }
  if (g_have_rx_sequence && sequence > g_last_rx_sequence + 1u) {
    g_dropped_commands += sequence - g_last_rx_sequence - 1u;
  }
  g_have_rx_sequence = true;
  g_last_rx_sequence = sequence;

  if (should_arm && !gl30_drv8316_arm(expected_off_generation)) {
    if (!gl30_safety_fault_latched(&g_safety)) {
      latch_fault(GL30_FAULT_DRIVER_SPI);
    }
    return;
  }
}

static void send_telemetry(void) {
  size_t frame_length = 0u;

  if (!g_telemetry_due && !g_slow_telemetry_due) {
    return;
  }
  if (g_uart_tx_busy) {
    if (g_telemetry_due) {
      g_telemetry_due = false;
      g_telemetry_drops++;
    }
    return;
  }

  if (g_slow_telemetry_due) {
    gl30_ina228_counters_t ina_counters;
    gl30_veml7700_counters_t veml_counters;
    gl30_motor_state_slow_t state;
    uint8_t payload[GL30_MOTOR_STATE_SLOW_LEN];
    uint32_t sensor_status = 0u;
    uint32_t foc_deadline_misses;
    uint32_t primask;

    g_slow_telemetry_due = false;
    if (g_telemetry_due) {
      g_telemetry_due = false;
      g_telemetry_drops++;
    }
    ina_counters = gl30_ina228_counters();
    veml_counters = gl30_veml7700_counters();
    primask = __get_PRIMASK();
    __disable_irq();
    foc_deadline_misses = g_safety.foc_deadline_count;
    if (primask == 0u) {
      __enable_irq();
    }

    if (ina_counters.configured) {
      sensor_status |= GL30_SENSOR_STATUS_INA228_CONFIGURED;
    }
    if (g_power_monitor.valid) {
      sensor_status |= GL30_SENSOR_STATUS_INA228_VALID;
    }
    if (veml_counters.configured) {
      sensor_status |= GL30_SENSOR_STATUS_VEML7700_CONFIGURED;
    }
    if (g_ambient_light.valid) {
      sensor_status |= GL30_SENSOR_STATUS_VEML7700_VALID;
    }
    if (g_ambient_light.saturated) {
      sensor_status |= GL30_SENSOR_STATUS_VEML7700_SATURATED;
    }

    state = (gl30_motor_state_slow_t){
        .busVoltageV = g_power_monitor.bus_voltage_v,
        .busCurrentA = g_power_monitor.current_a,
        .busPowerW = g_power_monitor.power_w,
        .energyJ = g_power_monitor.energy_j,
        .chargeC = g_power_monitor.charge_c,
        .inaDieTemperatureC = g_power_monitor.die_temperature_c,
        .motorTemperatureC = g_motor_temperature_c,
        .ambientLux = g_ambient_light.lux,
        .uptimeMs = (uint32_t)(gl30_timebase_now_us() / 1000u),
        .inaDiag = g_power_monitor.diag_alrt,
        .sensorStatus = sensor_status,
        .inaI2cErrors = ina_counters.i2c_errors,
        .vemlI2cErrors = veml_counters.i2c_errors,
        .telemetryDrops = g_telemetry_drops,
        .encoderCrcErrors = 0u,
        .focDeadlineMisses = foc_deadline_misses,
    };
    if (gl30_encode_motor_state_slow(&state, payload, sizeof(payload)) != 0 ||
        gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_SLOW, 0u,
                          g_tx_sequence++, gl30_timebase_now_us(), payload,
                          sizeof(payload), g_uart_tx, sizeof(g_uart_tx),
                          &frame_length) != 0 ||
        !uart_tx_start(g_uart_tx, frame_length)) {
      g_telemetry_drops++;
    }
    return;
  }

  {
    gl30_foc_state_t foc_snapshot;
    gl30_safety_t safety_snapshot;
    gl30_motor_state_fast_t state;
    uint8_t payload[GL30_MOTOR_STATE_FAST_LEN];
    uint16_t isr_cycles;
    uint16_t encoder_status;
    uint32_t dropped;
    uint32_t primask;

    g_telemetry_due = false;
    primask = __get_PRIMASK();
    __disable_irq();
    foc_snapshot = g_foc;
    safety_snapshot = g_safety;
    isr_cycles = g_last_isr_cycles;
    encoder_status = g_encoder_status;
    dropped = g_dropped_commands + g_telemetry_drops;
    if (primask == 0u) {
      __enable_irq();
    }

    gl30_foc_make_telemetry(&foc_snapshot,
                            (uint32_t)safety_snapshot.startup_state,
                            safety_snapshot.fault_bits,
                            safety_snapshot.warning_bits,
                            isr_cycles, encoder_status,
                            saturate_u16(dropped), &state);
    if (gl30_encode_motor_state_fast(&state, payload, sizeof(payload)) != 0 ||
        gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST, 0u,
                          g_tx_sequence++, gl30_timebase_now_us(), payload,
                          sizeof(payload), g_uart_tx, sizeof(g_uart_tx),
                          &frame_length) != 0 ||
        !uart_tx_start(g_uart_tx, frame_length)) {
      g_telemetry_drops++;
    }
  }
}

static bool encoder_is_healthy(uint64_t now_us) {
  gl30_factory_encoder_snapshot(&g_encoder);
  g_encoder_status = (uint16_t)g_encoder.status;
  return control_ready(&g_encoder, now_us, GL30_ENCODER_STALE_US);
}

static bool adc_zero_is_healthy(void) {
  return g_adc_zero_ready &&
         fabsf(g_adc_zero_a - GL30_ADC_ZERO_DEFAULT_COUNTS) <=
             GL30_ADC_ZERO_MAX_ERROR_COUNTS &&
         fabsf(g_adc_zero_b - GL30_ADC_ZERO_DEFAULT_COUNTS) <=
             GL30_ADC_ZERO_MAX_ERROR_COUNTS &&
         fabsf(g_adc_zero_c - GL30_ADC_ZERO_DEFAULT_COUNTS) <=
             GL30_ADC_ZERO_MAX_ERROR_COUNTS;
}

static void run_slow_sensors(uint64_t now_us) {
  if (!gl30_ina228_is_configured() &&
      now_us - g_last_ina_config_attempt_us >= GL30_SENSOR_CONFIG_RETRY_US) {
    g_last_ina_config_attempt_us = now_us;
    (void)gl30_ina228_configure();
  }
  if (!gl30_veml7700_is_configured() &&
      now_us - g_last_veml_config_attempt_us >= GL30_SENSOR_CONFIG_RETRY_US) {
    g_last_veml_config_attempt_us = now_us;
    (void)gl30_veml7700_configure();
  }

  g_slow_sensor_tick_divider++;
  if (gl30_ina228_is_configured() &&
      (g_slow_sensor_tick_divider %
       (GL30_MONITOR_HZ / GL30_SLOW_TELEMETRY_HZ)) == 0u) {
    gl30_ina228_sample_t sample = g_power_monitor;
    if (gl30_ina228_read_sample(&sample)) {
      g_power_monitor = sample;
    } else {
      g_power_monitor.valid = false;
    }
  }
  if (gl30_veml7700_is_configured() &&
      (g_slow_sensor_tick_divider %
       (GL30_MONITOR_HZ / GL30_AMBIENT_LIGHT_HZ)) == 0u) {
    gl30_veml7700_sample_t sample = g_ambient_light;
    if (gl30_veml7700_read_sample(&sample)) {
      g_ambient_light = sample;
    } else {
      g_ambient_light.valid = false;
    }
  }
}

static void run_monitor(uint64_t now_us) {
  bool power_ok;
  bool adc_ok;
  bool driver_ok;
  bool alignment_ok;
  bool self_test_ok;
  bool encoder_ok;
  bool was_active;
  uint32_t primask;

  if (!g_monitor_due) {
    return;
  }
  g_monitor_due = false;
  run_slow_sensors(now_us);

  power_ok = g_vbus_v >= GL30_VBUS_MIN_RUN_V &&
             g_vbus_v < GL30_VBUS_FAULT_V;
  adc_ok = adc_zero_is_healthy();
  driver_ok = gl30_drv8316_is_configured();
  alignment_ok = GL30_ELECTRICAL_ZERO_VALID != 0u;

  if (power_ok && !driver_ok &&
      now_us - g_last_driver_config_attempt_us >= 100000u) {
    g_last_driver_config_attempt_us = now_us;
    driver_ok = gl30_drv8316_configure();
  }

  if (gl30_drv8316_outputs_enabled()) {
    gl30_drv8316_status_t driver_status;
    if (!gl30_drv8316_read_faults(&driver_status)) {
      latch_fault(GL30_FAULT_DRIVER_SPI);
      return;
    }
    if (!driver_status.n_fault_released || driver_status.stat0 != 0u ||
        driver_status.stat1 != 0u ||
        (driver_status.stat2 & GL30_DRV8316_STAT2_FAULT_MASK) != 0u) {
      latch_fault(GL30_FAULT_HARDWARE_BKIN);
      return;
    }
  }

  self_test_ok = SystemCoreClock == GL30_SYSCLK_HZ &&
                 (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u;
  encoder_ok = encoder_is_healthy(now_us);
  primask = __get_PRIMASK();
  __disable_irq();
  was_active = g_safety.startup_state == GL30_STARTUP_ACTIVE;
  gl30_safety_set_startup_checks(&g_safety, power_ok, self_test_ok,
                                 encoder_ok, driver_ok, adc_ok,
                                 alignment_ok);
  gl30_safety_set_warning(&g_safety, GL30_WARNING_VBUS,
                          g_vbus_v >= GL30_VBUS_WARN_V);
  gl30_safety_set_warning(&g_safety, GL30_WARNING_TEMPERATURE,
                          g_motor_temperature_c >= GL30_MOTOR_TEMP_WARN_C);
  gl30_safety_set_warning(&g_safety, GL30_WARNING_ENCODER, !encoder_ok);
  if (primask == 0u) {
    __enable_irq();
  }

  if (was_active && !encoder_ok) {
    latch_fault(GL30_FAULT_ENCODER);
  } else if (was_active && !power_ok) {
    latch_fault(GL30_FAULT_VBUS);
  } else if (g_motor_temperature_c >= GL30_MOTOR_TEMP_FAULT_C) {
    latch_fault(GL30_FAULT_TEMPERATURE);
  }
}

void gl30_app_init(void) {
  uint64_t now_us;

  /* CubeMX currently emits push-pull for these labels. Override the two
   * fail-safe nets to their schematic open-drain contract. */
  LL_GPIO_SetPinOutputType(SYS_FAULT_N_GPIO_Port, SYS_FAULT_N_Pin,
                           LL_GPIO_OUTPUT_OPENDRAIN);
  LL_GPIO_SetPinOutputType(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin,
                           LL_GPIO_OUTPUT_OPENDRAIN);
  LL_GPIO_SetOutputPin(SYS_FAULT_N_GPIO_Port, SYS_FAULT_N_Pin);
  LL_GPIO_SetOutputPin(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin);

  configure_dwt_counter();
  gl30_timebase_init();
  now_us = gl30_timebase_now_us();
  init_runtime_state(now_us);
  gl30_drv8316_init();
  hardware_safe_state();
  publish_fault_line(false);
  LL_IWDG_ReloadCounter(IWDG);

  if (!uart_dma_init() || !start_adc_sampling()) {
    latch_fault(GL30_FAULT_STARTUP);
  }
  start_control_timers();

  gl30_ina228_init();
  gl30_veml7700_init();
  (void)gl30_ina228_configure();
  LL_IWDG_ReloadCounter(IWDG);
  (void)gl30_veml7700_configure();
  LL_IWDG_ReloadCounter(IWDG);

  /* PENDING_VENDOR is a non-armed startup state, not fabricated encoder
   * evidence. A runtime loss after ACTIVE is promoted to a latched fault. */
  gl30_safety_set_warning(&g_safety, GL30_WARNING_ENCODER,
                          !control_ready(&g_encoder, now_us,
                                         GL30_ENCODER_STALE_US));
  g_last_watchdog_progress = g_control_progress;
  g_last_watchdog_refresh_us = gl30_timebase_now_us();
}

void gl30_app_run_once(void) {
  const uint64_t now_us = gl30_timebase_now_us();
  bool faulted;

  process_pending_command();
  run_monitor(now_us);
  send_telemetry();

  faulted = gl30_safety_fault_latched(&g_safety);
  publish_fault_line(faulted);
  if (now_us - g_last_watchdog_refresh_us >= 10000u &&
      g_control_progress != g_last_watchdog_progress) {
    g_last_watchdog_progress = g_control_progress;
    g_last_watchdog_refresh_us = now_us;
    LL_IWDG_ReloadCounter(IWDG);
  }
  if (now_us - g_last_watchdog_toggle_us >= 10000u) {
    g_last_watchdog_toggle_us = now_us;
    LL_GPIO_TogglePin(EXT_WATCHDOG_WDI_DNP_GPIO_Port,
                      EXT_WATCHDOG_WDI_DNP_Pin);
  }
}

void gl30_app_adc1_2_irq(void) {
  uint32_t cycle_start;
  bool adc2_ready;
  bool adc3_ready;
  uint32_t raw_a;
  uint32_t raw_b;
  uint32_t raw_c;
  uint32_t raw_vbus;
  uint32_t raw_temp;
  float raw_to_volts;
  float current_a;
  float current_b;
  float current_c;

  if (LL_ADC_IsActiveFlag_JEOS(ADC1) == 0u) {
    return;
  }
  LL_GPIO_SetOutputPin(SCOPE_TP_GPIO_Port, SCOPE_TP_Pin);
  cycle_start = DWT->CYCCNT;
  adc2_ready = LL_ADC_IsActiveFlag_JEOS(ADC2) != 0u;
  adc3_ready = LL_ADC_IsActiveFlag_JEOS(ADC3) != 0u;
  if (!adc2_ready || !adc3_ready) {
    LL_ADC_ClearFlag_JEOS(ADC1);
    g_safety.adc_sync_error_count++;
    if (g_safety.adc_sync_error_count >= GL30_ADC_SYNC_BAD_LIMIT) {
      latch_fault(GL30_FAULT_ADC_SYNC);
    } else {
      hardware_safe_state();
    }
    LL_GPIO_ResetOutputPin(SCOPE_TP_GPIO_Port, SCOPE_TP_Pin);
    return;
  }

  raw_a = LL_ADC_INJ_ReadConversionData32(ADC1, LL_ADC_INJ_RANK_1);
  raw_vbus = LL_ADC_INJ_ReadConversionData32(ADC1, LL_ADC_INJ_RANK_2);
  raw_b = LL_ADC_INJ_ReadConversionData32(ADC2, LL_ADC_INJ_RANK_1);
  raw_temp = LL_ADC_INJ_ReadConversionData32(ADC2, LL_ADC_INJ_RANK_2);
  raw_c = LL_ADC_INJ_ReadConversionData32(ADC3, LL_ADC_INJ_RANK_1);
  LL_ADC_ClearFlag_JEOS(ADC1);
  LL_ADC_ClearFlag_JEOS(ADC2);
  LL_ADC_ClearFlag_JEOS(ADC3);

  raw_to_volts = GL30_ADC_VREF_V / GL30_ADC_FULL_SCALE_COUNTS;
  g_vbus_v = (float)raw_vbus * raw_to_volts * GL30_VBUS_DIVIDER_RATIO;
  g_motor_temperature_c = 25.0f +
      (((float)raw_temp * raw_to_volts * 1000.0f) -
       GL30_TEMP_SENSOR_MV_AT_25C) / GL30_TEMP_SENSOR_MV_PER_C;

  if (!g_adc_zero_ready) {
    g_adc_zero_sum_a += raw_a;
    g_adc_zero_sum_b += raw_b;
    g_adc_zero_sum_c += raw_c;
    g_adc_zero_samples++;
    if (g_adc_zero_samples >= GL30_ADC_ZERO_CAL_SAMPLES) {
      const float denominator = (float)g_adc_zero_samples;
      g_adc_zero_a = (float)g_adc_zero_sum_a / denominator;
      g_adc_zero_b = (float)g_adc_zero_sum_b / denominator;
      g_adc_zero_c = (float)g_adc_zero_sum_c / denominator;
      g_adc_zero_ready = true;
    }
    hardware_safe_state();
    g_control_progress++;
    LL_GPIO_ResetOutputPin(SCOPE_TP_GPIO_Port, SCOPE_TP_Pin);
    return;
  }

  current_a = ((float)raw_a - g_adc_zero_a) *
              raw_to_volts / GL30_CSA_GAIN_V_PER_A;
  current_b = ((float)raw_b - g_adc_zero_b) *
              raw_to_volts / GL30_CSA_GAIN_V_PER_A;
  current_c = ((float)raw_c - g_adc_zero_c) *
              raw_to_volts / GL30_CSA_GAIN_V_PER_A;

  if (!gl30_safety_torque_allowed(&g_safety) ||
      !gl30_drv8316_outputs_enabled()) {
    gl30_foc_force_zero(&g_foc);
    if (gl30_drv8316_outputs_enabled()) {
      gl30_drv8316_set_duty(0.5f, 0.5f, 0.5f);
    }
  } else {
    const gl30_foc_output_t output = gl30_foc_current_tick(
        &g_foc, current_a, current_b, current_c, g_vbus_v,
        1.0f / (float)GL30_PWM_HZ, g_vbus_v);
    if (output.overcurrent) {
      latch_fault(GL30_FAULT_OVERCURRENT);
    } else if (!output.valid) {
      hardware_safe_state();
    } else {
      gl30_drv8316_set_duty(output.duty_a, output.duty_b, output.duty_c);
    }
  }

  if (!g_trace_frozen) {
    const int16_t trace_sample[6] = {
        trace_i16(current_a, 1000.0f),
        trace_i16(current_b, 1000.0f),
        trace_i16(current_c, 1000.0f),
        trace_i16(g_foc.i_q_ref_a, 1000.0f),
        trace_i16(g_vbus_v, 1000.0f),
        (int16_t)g_encoder_status,
    };
    gl30_trace_push(trace_sample);
  }
  if (g_vbus_v >= GL30_VBUS_FAULT_V) {
    latch_fault(GL30_FAULT_VBUS);
  }
  {
    const uint32_t elapsed_cycles = DWT->CYCCNT - cycle_start;
    g_last_isr_cycles = saturate_u16(elapsed_cycles);
    if (elapsed_cycles > GL30_FOC_DEADLINE_CYCLES) {
      g_safety.foc_deadline_count++;
      latch_fault(GL30_FAULT_FOC_DEADLINE);
    }
  }
  g_control_progress++;
  LL_GPIO_ResetOutputPin(SCOPE_TP_GPIO_Port, SCOPE_TP_Pin);
}

void gl30_app_tim1_break_irq(void) {
  if (LL_TIM_IsActiveFlag_BRK(TIM1) != 0u) {
    LL_TIM_ClearFlag_BRK(TIM1);
    latch_fault(GL30_FAULT_HARDWARE_BKIN);
  }
}

void gl30_app_tim2_irq(void) {
  if (LL_TIM_IsActiveFlag_UPDATE(TIM2) != 0u) {
    LL_TIM_ClearFlag_UPDATE(TIM2);
    gl30_timebase_on_tim2_overflow();
  }
}

void gl30_app_tim6_irq(void) {
  uint64_t now_us;
  bool encoder_ok;
  bool was_active;

  if (LL_TIM_IsActiveFlag_UPDATE(TIM6) == 0u) {
    return;
  }
  LL_TIM_ClearFlag_UPDATE(TIM6);
  gl30_factory_encoder_snapshot(&g_encoder);
  now_us = gl30_timebase_now_us();
  encoder_ok = control_ready(&g_encoder, now_us, GL30_ENCODER_STALE_US);
  g_encoder_status = (uint16_t)g_encoder.status;
  was_active = g_safety.startup_state == GL30_STARTUP_ACTIVE;
  if (was_active && !encoder_ok) {
    latch_fault(GL30_FAULT_ENCODER);
    return;
  }
  gl30_foc_observer_tick_4k(&g_foc, g_encoder.angle_rad, encoder_ok);
}

void gl30_app_tim7_irq(void) {
  uint64_t now_us;
  bool faulted_before_tick;

  if (LL_TIM_IsActiveFlag_UPDATE(TIM7) == 0u) {
    return;
  }
  LL_TIM_ClearFlag_UPDATE(TIM7);
  now_us = gl30_timebase_now_us();
  faulted_before_tick = gl30_safety_fault_latched(&g_safety);
  gl30_safety_tick(&g_safety, now_us);
  if (!faulted_before_tick && gl30_safety_fault_latched(&g_safety)) {
    latch_fault(g_safety.fault_bits);
    return;
  }
  gl30_haptic_tick_2k(&g_foc, gl30_safety_torque_allowed(&g_safety));
  if (!gl30_safety_torque_allowed(&g_safety) &&
      gl30_drv8316_outputs_enabled()) {
    hardware_safe_state();
  }
  g_haptic_tick_divider++;
  g_telemetry_due = true;
  if ((g_haptic_tick_divider %
       (GL30_HAPTIC_HZ / GL30_SLOW_TELEMETRY_HZ)) == 0u) {
    g_slow_telemetry_due = true;
  }
  if ((g_haptic_tick_divider %
       (GL30_HAPTIC_HZ / GL30_MONITOR_HZ)) == 0u) {
    g_monitor_due = true;
  }
}

void gl30_app_dma1_channel1_irq(void) {
  bool drain = false;

  if (LL_DMA_IsActiveFlag_HT1(DMA1) != 0u) {
    LL_DMA_ClearFlag_HT1(DMA1);
    drain = true;
  }
  if (LL_DMA_IsActiveFlag_TC1(DMA1) != 0u) {
    LL_DMA_ClearFlag_TC1(DMA1);
    drain = true;
  }
  if (LL_DMA_IsActiveFlag_TE1(DMA1) != 0u) {
    LL_DMA_ClearFlag_TE1(DMA1);
    LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_1);
    LL_DMA_SetDataLength(DMA1, LL_DMA_CHANNEL_1,
                         GL30_UART_RX_BUFFER_SIZE);
    g_uart_rx_consumed = 0u;
    g_dropped_commands++;
    LL_DMA_EnableChannel(DMA1, LL_DMA_CHANNEL_1);
    return;
  }
  if (drain) {
    uart_rx_drain();
  }
}

void gl30_app_dma1_channel2_irq(void) {
  if (LL_DMA_IsActiveFlag_TE2(DMA1) != 0u) {
    LL_DMA_ClearFlag_TE2(DMA1);
    LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_2);
    LL_USART_DisableDMAReq_TX(USART3);
    g_uart_tx_busy = false;
    g_telemetry_drops++;
    return;
  }
  if (LL_DMA_IsActiveFlag_TC2(DMA1) != 0u) {
    LL_DMA_ClearFlag_TC2(DMA1);
    LL_DMA_DisableChannel(DMA1, LL_DMA_CHANNEL_2);
    LL_USART_DisableDMAReq_TX(USART3);
    g_uart_tx_busy = false;
  }
}

void gl30_app_usart3_irq(void) {
  if (LL_USART_IsActiveFlag_IDLE(USART3) != 0u) {
    LL_USART_ClearFlag_IDLE(USART3);
    uart_rx_drain();
  }
  if (LL_USART_IsActiveFlag_ORE(USART3) != 0u) {
    LL_USART_ClearFlag_ORE(USART3);
    g_dropped_commands++;
  }
  if (LL_USART_IsActiveFlag_NE(USART3) != 0u) {
    LL_USART_ClearFlag_NE(USART3);
    g_dropped_commands++;
  }
  if (LL_USART_IsActiveFlag_FE(USART3) != 0u) {
    LL_USART_ClearFlag_FE(USART3);
    g_dropped_commands++;
  }
}

/* USART3 DMA is configured at run time rather than by CubeMX. Keep these
 * vector wrappers in application-owned code so CubeMX regeneration cannot
 * remove them from stm32g4xx_it.c. */
void DMA1_Channel1_IRQHandler(void) {
  gl30_app_dma1_channel1_irq();
}

void DMA1_Channel2_IRQHandler(void) {
  gl30_app_dma1_channel2_irq();
}
