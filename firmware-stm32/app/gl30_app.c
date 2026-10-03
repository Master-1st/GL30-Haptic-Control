/* GL30 AMOLED V7 product application.
 *
 * CubeMX owns clock, pin and peripheral register initialization. This file
 * owns the application scheduler and the LL-only run-time control paths.
 * Torque remains fail-closed until the product power sequence, encoder
 * electrical interface and electrical zero are qualified on real hardware.
 */

#include "gl30_app.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "main.h"

#include "board_config.h"
#include "board_sense.h"
#include "control_lease.h"
#include "current_zero.h"
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

static __ALIGNED(4) uint8_t g_uart_rx[GL30_UART_RX_BUFFER_SIZE];
static __ALIGNED(4) uint8_t g_uart_tx[GL30_UART_TX_BUFFER_SIZE];
static volatile uint16_t g_uart_rx_consumed;
static volatile bool g_uart_tx_busy;
static volatile bool g_pending_command_ready;
static gl30_haptic_command_t g_pending_command;
static volatile uint32_t g_pending_command_sequence;
static volatile uint64_t g_pending_command_received_us;
static volatile bool g_pending_control_request_ready;
static gl30_control_lease_request_t g_pending_control_request;
static volatile uint64_t g_pending_control_request_received_us;
static gl30_control_lease_t g_control_lease;
static uint64_t g_arm_ready_since_us;

static volatile bool g_telemetry_due;
static volatile bool g_slow_telemetry_due;
static volatile bool g_haptic_state_due;
static volatile uint64_t g_haptic_measurement_us;
static bool g_prefer_haptic_when_tied;
static gl30_ina228_sample_t g_power_monitor;
static gl30_veml7700_sample_t g_ambient_light;
static uint64_t g_last_ina_config_attempt_us;
static uint64_t g_last_veml_config_attempt_us;
static uint32_t g_slow_sensor_tick_divider;
static volatile bool g_monitor_due;
static volatile bool g_driver_arm_in_progress;
static volatile uint32_t g_control_progress;
static volatile uint32_t g_safety_progress;
static volatile uint16_t g_last_isr_cycles;
static volatile uint16_t g_encoder_status;
static volatile uint32_t g_dropped_commands;
static volatile uint32_t g_telemetry_drops;
static uint32_t g_tx_sequence;
static uint32_t g_last_rx_sequence;
static bool g_have_rx_sequence;

/* ADC and foreground timeout checks share this state only under PRIMASK. */
static gl30_current_zero_t g_current_zero;
static volatile float g_vbus_v;
static volatile float g_motor_temperature_c;
static volatile uint16_t g_motor_temperature_raw;
static volatile bool g_motor_temperature_sample_received;
static volatile uint64_t g_last_adc_sample_us;

typedef enum {
  GL30_POWER_WAIT_LOGIC,
  GL30_POWER_WAIT_BUS,
  GL30_POWER_WAIT_WAKE,
  GL30_POWER_WAIT_ZERO,
  GL30_POWER_VERIFY_DRIVER,
  GL30_POWER_COMPLETE,
  GL30_POWER_FAILED
} gl30_power_stage_t;

/* Foreground advances preparation; emergency-off may only move it to FAILED.
 * Preparation never enables PWM. READY still needs a new explicit command. */
static volatile gl30_power_stage_t g_power_stage;
static uint64_t g_power_stage_started_us;
static volatile uint64_t g_bus_valid_since_us;

static volatile uint32_t g_haptic_tick_divider;
static volatile bool g_trace_frozen;
static uint64_t g_last_watchdog_refresh_us;
static uint32_t g_last_watchdog_progress;
static uint32_t g_last_watchdog_safety_progress;

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
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  gl30_app_emergency_off();
  gl30_foc_force_zero(&g_foc);
  gl30_safety_latch_fault(&g_safety, fault_bit);
  g_trace_frozen = true;
  publish_fault_line(true);
  if (primask == 0u) {
    __enable_irq();
  }
}

void gl30_app_emergency_off(void) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  /* Sleep/power loss resets the driver's registers. Cancel in-flight
   * preparation before removing power; normal bridge-only off retains them. */
  gl30_drv8316_invalidate_configuration();
  gl30_current_zero_reset(&g_current_zero);
  g_power_stage = GL30_POWER_FAILED;
  g_arm_ready_since_us = 0u;
  /* Bridge off first. The independent regeneration clamp must remain on
   * MOTOR_BUS downstream of the upstream isolation switch. This hard-off
   * path is distinct from normal zero torque, which only calls the bridge
   * safe-state helper above. */
  if ((RCC->AHB2ENR & RCC_AHB2ENR_GPIOAEN) != 0u) {
    LL_GPIO_ResetOutputPin(BRAKE_FORCE_TEST_GPIO_Port, BRAKE_FORCE_TEST_Pin);
    LL_GPIO_ResetOutputPin(DRV8316_NSLEEP_GPIO_Port, DRV8316_NSLEEP_Pin);
  }
  if ((RCC->AHB2ENR & RCC_AHB2ENR_GPIOBEN) != 0u) {
    LL_GPIO_ResetOutputPin(MOTOR_PWR_EN_GPIO_Port, MOTOR_PWR_EN_Pin);
  }
  publish_fault_line(true);
  if (primask == 0u) {
    __enable_irq();
  }
}

static void configure_dwt_counter(void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0u;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void init_runtime_state(uint64_t now_us) {
  gl30_factory_encoder_sample_t encoder;
  memset(&g_foc, 0, sizeof(g_foc));
  memset(&g_pending_command, 0, sizeof(g_pending_command));
  memset(&g_pending_control_request, 0, sizeof(g_pending_control_request));
  g_pending_command_ready = false;
  g_pending_command_received_us = 0u;
  g_pending_control_request_ready = false;
  g_pending_control_request_received_us = 0u;
  gl30_control_lease_init(&g_control_lease);
  g_arm_ready_since_us = 0u;
  g_safety_progress = 0u;
  g_driver_arm_in_progress = false;
  memset(&g_power_monitor, 0, sizeof(g_power_monitor));
  memset(&g_ambient_light, 0, sizeof(g_ambient_light));
  g_telemetry_due = false;
  g_slow_telemetry_due = false;
  g_haptic_state_due = false;
  g_haptic_measurement_us = 0u;
  g_motor_temperature_sample_received = false;
  g_last_adc_sample_us = 0u;
  g_power_stage = GL30_POWER_WAIT_LOGIC;
  g_power_stage_started_us = now_us;
  g_bus_valid_since_us = 0u;
  g_prefer_haptic_when_tied = false;
  gl30_foc_init(&g_foc);
  gl30_current_zero_reset(&g_current_zero);
  gl30_safety_init(&g_safety, now_us);
  gl30_frame_parse_init();
  gl30_trace_init();
  gl30_factory_encoder_init();
  gl30_factory_encoder_snapshot(&encoder);
  g_encoder_status = (uint16_t)encoder.status;
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
  if (frame->type == GL30_V6_FRAME_PAYLOAD_CONTROL_LEASE) {
    gl30_control_lease_request_t request;
    if (frame->payload_len != GL30_CONTROL_LEASE_LEN || frame->payload == NULL ||
        gl30_decode_control_lease(frame->payload, frame->payload_len, &request) != 0) {
      gl30_safety_on_bad_length(&g_safety);
      return;
    }
    if (g_pending_control_request_ready) {
      g_dropped_commands++;
      return;
    }
    g_pending_control_request = request;
    g_pending_control_request_received_us = gl30_timebase_now_us();
    g_pending_control_request_ready = true;
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
  g_pending_command_received_us = gl30_timebase_now_us();
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
  /* NDTR may be zero at circular reload. Normalize the end position, and
   * drain only this entry snapshot so a continuous sender cannot pin this
   * IRQ in a moving-producer loop. Later HT/TC/IDLE events drain new data. */
  const uint16_t produced = (uint16_t)((GL30_UART_RX_BUFFER_SIZE -
      LL_DMA_GetDataLength(DMA1, LL_DMA_CHANNEL_1)) % GL30_UART_RX_BUFFER_SIZE);

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
  }
}

static void process_pending_control_request(void) {
  gl30_control_lease_request_t request;
  gl30_foc_state_t acquire_candidate = {0};
  uint64_t received_us;
  uint64_t now_us;
  uint32_t primask;
  bool release_allowed;
  bool acquire_candidate_valid = true;
  gl30_control_lease_result_t result;

  if (!g_pending_control_request_ready) {
    return;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  request = g_pending_control_request;
  received_us = g_pending_control_request_received_us;
  now_us = gl30_timebase_now_us();
  if (primask == 0u) {
    __enable_irq();
  }
  if (received_us == 0u || now_us < received_us ||
      now_us - received_us >= GL30_COMM_WARN_US) {
    primask = __get_PRIMASK();
    __disable_irq();
    g_pending_control_request_ready = false;
    g_pending_control_request_received_us = 0u;
    if (primask == 0u) {
      __enable_irq();
    }
    g_dropped_commands++;
    return;
  }

  primask = __get_PRIMASK();
  __disable_irq();
  now_us = gl30_timebase_now_us();
  if (now_us < received_us || now_us - received_us >= GL30_COMM_WARN_US) {
    g_pending_control_request_ready = false;
    g_pending_control_request_received_us = 0u;
    g_dropped_commands++;
    if (primask == 0u) {
      __enable_irq();
    }
    return;
  }
  g_pending_control_request_ready = false;
  g_pending_control_request_received_us = 0u;

  release_allowed =
      gl30_control_lease_command_is_fullzero(&g_foc.active_command) &&
      g_foc.active_command.leaseGeneration == request.currentGeneration &&
      g_foc.active_command.commandNonce == request.zeroNonce &&
      !g_safety.arm_requested && !gl30_drv8316_outputs_enabled() &&
      !g_driver_arm_in_progress;

  if (request.action == GL30_CONTROL_ACQUIRE) {
    gl30_haptic_command_t zero_command = {0};
    zero_command.commandNonce = request.zeroNonce;
    zero_command.leaseGeneration = request.nextGeneration;
    acquire_candidate = g_foc;
    acquire_candidate_valid =
        gl30_foc_apply_command(&acquire_candidate, &zero_command);
  }

  result = acquire_candidate_valid
      ? gl30_control_lease_process_request(&g_control_lease, &request,
                                           release_allowed)
      : GL30_CONTROL_LEASE_REQUEST_REJECTED;

  if (result == GL30_CONTROL_LEASE_RELEASE_ACCEPTED) {
    g_pending_command_ready = false;
    g_pending_command_received_us = 0u;
    gl30_safety_release_control(&g_safety);
    gl30_foc_force_zero(&g_foc);
    hardware_safe_state();
  } else if (result == GL30_CONTROL_LEASE_ACQUIRE_ACCEPTED) {
    g_pending_command_ready = false;
    g_pending_command_received_us = 0u;
    gl30_safety_release_control(&g_safety);
    hardware_safe_state();
    g_foc = acquire_candidate;
    gl30_foc_force_zero(&g_foc);
  } else if (result == GL30_CONTROL_LEASE_REQUEST_REJECTED) {
    g_dropped_commands++;
  }
  if (primask == 0u) {
    __enable_irq();
  }
}

static void process_pending_command(void) {
  gl30_haptic_command_t command;
  gl30_factory_encoder_sample_t encoder;
  uint32_t sequence;
  uint32_t primask;
  uint64_t now_us;
  uint64_t received_us;
  bool accepted;
  bool lease_allowed;
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
  received_us = g_pending_command_received_us;
  if (primask == 0u) {
    __enable_irq();
  }

  now_us = gl30_timebase_now_us();
  if (received_us == 0u || now_us < received_us ||
      now_us - received_us >= GL30_COMM_WARN_US) {
    primask = __get_PRIMASK();
    __disable_irq();
    g_pending_command_ready = false;
    g_pending_command_received_us = 0u;
    if (primask == 0u) {
      __enable_irq();
    }
    g_dropped_commands++;
    return;
  }
  primask = __get_PRIMASK();
  __disable_irq();
  now_us = gl30_timebase_now_us();
  if (received_us == 0u || now_us < received_us ||
      now_us - received_us >= GL30_COMM_WARN_US) {
    g_pending_command_ready = false;
    g_pending_command_received_us = 0u;
    g_dropped_commands++;
    if (primask == 0u) {
      __enable_irq();
    }
    return;
  }
  if (g_pending_control_request_ready) {
    if (primask == 0u) {
      __enable_irq();
    }
    return;
  }
  g_pending_command_ready = false;
  g_pending_command_received_us = 0u;
  lease_allowed = gl30_control_lease_command_is_allowed(&g_control_lease, &command);
  accepted = lease_allowed && gl30_foc_apply_command(&g_foc, &command);
  if (accepted) {
    gl30_control_lease_command_accepted(&g_control_lease, &command);
  }
  gl30_factory_encoder_snapshot(&encoder);
  encoder_ok = control_ready(&encoder, gl30_timebase_now_us(), GL30_ENCODER_STALE_US);
  expected_off_generation = gl30_drv8316_off_generation_snapshot();
  if (accepted) {
    gl30_safety_on_valid_command(&g_safety, received_us);
    if (command.userTorqueLimitNm == 0.0f) {
      /* A zero heartbeat must not cancel idle driver setup or reset an
       * already-neutral FOC phase. Only stop hardware when it is enabled. */
      gl30_safety_disarm(&g_safety);
      if (g_foc.torque_command_nm != 0.0f || g_foc.haptic_torque_nm != 0.0f ||
          g_foc.i_d_ref_a != 0.0f || g_foc.i_q_ref_a != 0.0f ||
          g_foc.integrator_d_v != 0.0f || g_foc.integrator_q_v != 0.0f ||
          g_foc.v_d_v != 0.0f || g_foc.v_q_v != 0.0f) {
        gl30_foc_force_zero(&g_foc);
      }
      if (gl30_drv8316_outputs_enabled() ||
          LL_TIM_IsEnabledAllOutputs(TIM1) != 0u) {
        hardware_safe_state();
      }
    } else if (encoder_ok && g_current_zero.state == GL30_CURRENT_ZERO_READY &&
        g_power_stage == GL30_POWER_COMPLETE && gl30_drv8316_startup_verified() &&
        g_arm_ready_since_us != 0u && received_us >= g_arm_ready_since_us) {
      gl30_safety_request_arm(&g_safety);
    } else {
      gl30_safety_disarm(&g_safety);
    }
  }
  should_arm = accepted && encoder_ok &&
               gl30_safety_torque_allowed(&g_safety) &&
               !gl30_drv8316_outputs_enabled();
  g_driver_arm_in_progress = should_arm;
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

  if (should_arm) {
    const bool armed = gl30_drv8316_arm(expected_off_generation);
    primask = __get_PRIMASK();
    __disable_irq();
    g_driver_arm_in_progress = false;
    if (primask == 0u) {
      __enable_irq();
    }
    if (!armed) {
      if (!gl30_safety_fault_latched(&g_safety)) {
        latch_fault(GL30_FAULT_DRIVER_SPI);
      }
      return;
    }
  }
}

static void send_telemetry(void) {
  size_t frame_length = 0u;
  bool send_slow = false;
  bool send_haptic = false;
  bool control_ack;
  const uint32_t lease_primask = __get_PRIMASK();
  __disable_irq();
  const gl30_control_lease_t lease_snapshot = g_control_lease;
  const bool lease_released =
      lease_snapshot.state == GL30_CONTROL_LEASE_RELEASED;
  control_ack = lease_snapshot.ack_pending;
  if (lease_released) {
    g_telemetry_due = false;
    g_slow_telemetry_due = false;
    g_haptic_state_due = false;
  }
  if (lease_primask == 0u) {
    __enable_irq();
  }

  if (lease_released && !control_ack) {
    return;
  }
  if (!lease_released && !control_ack &&
      !g_telemetry_due && !g_slow_telemetry_due && !g_haptic_state_due) {
    return;
  }
  if (g_uart_tx_busy) {
    if (!lease_released && g_telemetry_due) {
      g_telemetry_due = false;
      g_telemetry_drops++;
    }
    return;
  }

  if (lease_released || control_ack) {
    send_haptic = true;
  } else if (g_slow_telemetry_due && g_haptic_state_due) {
    send_haptic = g_prefer_haptic_when_tied;
    send_slow = !send_haptic;
    g_prefer_haptic_when_tied = !g_prefer_haptic_when_tied;
  } else if (g_slow_telemetry_due) {
    send_slow = true;
  } else if (g_haptic_state_due) {
    send_haptic = true;
  }

  if (send_slow) {
    gl30_factory_encoder_diagnostics_t encoder_diag;
    gl30_ina228_counters_t ina_counters;
    gl30_veml7700_counters_t veml_counters;
    gl30_motor_state_slow_t state;
    uint8_t payload[GL30_MOTOR_STATE_SLOW_LEN];
    uint32_t sensor_status = 0u;
    uint32_t foc_deadline_misses;
    uint32_t primask;

    ina_counters = gl30_ina228_counters();
    veml_counters = gl30_veml7700_counters();
    gl30_factory_encoder_diagnostics_snapshot(&encoder_diag);
    primask = __get_PRIMASK();
    __disable_irq();
    g_slow_telemetry_due = false;
    if (g_telemetry_due) {
      g_telemetry_due = false;
      g_telemetry_drops++;
    }
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
        /* Existing wire field counts AS5048A parity failures on this board. */
        .encoderCrcErrors = encoder_diag.parity_errors,
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

  if (send_haptic) {
    gl30_foc_state_t foc_snapshot;
    gl30_safety_t safety_snapshot;
    gl30_haptic_state_t state;
    uint8_t payload[GL30_HAPTIC_STATE_LEN];
    uint16_t encoder_status;
    uint64_t measurement_us;
    int32_t logical_position = 0;
    float sub_position = 0.0f;
    uint32_t status = 0u;
    uint32_t primask;
    bool encoder_valid;
    bool detent_ready;

    primask = __get_PRIMASK();
    __disable_irq();
    foc_snapshot = g_foc;
    safety_snapshot = g_safety;
    encoder_status = g_encoder_status;
    measurement_us = g_haptic_measurement_us;
    g_haptic_state_due = false;
    if (primask == 0u) {
      __enable_irq();
    }

    encoder_valid = encoder_status ==
                        (uint16_t)GL30_FACTORY_ENCODER_STATUS_READY &&
                    foc_snapshot.observer_initialized &&
                    safety_snapshot.encoder_ok;
    detent_ready = foc_snapshot.detent_initialized &&
                   (foc_snapshot.active_command.modeFlags & GL30_HAPTIC_DETENT) != 0u;
    if (encoder_valid) {
      status |= GL30_HAPTIC_STATE_ENCODER_VALID;
    }
    if ((foc_snapshot.active_command.modeFlags & GL30_HAPTIC_DETENT) != 0u) {
      if (detent_ready) {
        status |= GL30_HAPTIC_STATE_DETENT_READY;
        logical_position = foc_snapshot.detent_position;
        if (isfinite(foc_snapshot.active_command.detentWidthRad) &&
            foc_snapshot.active_command.detentWidthRad > 0.0f) {
          sub_position = foc_snapshot.detent_fraction;
        }
      }
    } else {
      gl30_motor_state_fast_t telemetry_snapshot;
      gl30_foc_make_telemetry(&foc_snapshot,
                              (uint32_t)safety_snapshot.startup_state,
                              safety_snapshot.fault_bits,
                              safety_snapshot.warning_bits,
                              0u, encoder_status, 0u, &telemetry_snapshot);
      logical_position = telemetry_snapshot.logicalPosition;
      sub_position = telemetry_snapshot.subPosition;
    }

    if (lease_released) {
      status |= GL30_HAPTIC_STATE_CONTROL_RELEASED;
    } else if (lease_snapshot.state == GL30_CONTROL_LEASE_OWNED &&
               lease_snapshot.awaiting_first_zero) {
      status |= GL30_HAPTIC_STATE_CONTROL_WAITING_ZERO;
    }

    state = (gl30_haptic_state_t){
        .profileId = foc_snapshot.active_command.profileId,
        .commandNonce = foc_snapshot.active_command.commandNonce,
        .modeFlags = foc_snapshot.active_command.modeFlags,
        .logicalPosition = logical_position,
        .subPosition = sub_position,
        .detentWidthRad = foc_snapshot.active_command.detentWidthRad,
        .motorState = (uint32_t)safety_snapshot.startup_state,
        .faultBits = safety_snapshot.fault_bits,
        .status = status,
        .leaseGeneration = lease_snapshot.generation,
    };
    if (gl30_encode_haptic_state(&state, payload, sizeof(payload)) != 0 ||
        gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE, 0u,
                          g_tx_sequence++, measurement_us, payload,
                          sizeof(payload), g_uart_tx, sizeof(g_uart_tx),
                          &frame_length) != 0) {
      g_telemetry_drops++;
    } else if (!uart_tx_start(g_uart_tx, frame_length)) {
      g_telemetry_drops++;
    } else if (control_ack) {
      const uint32_t ack_primask = __get_PRIMASK();
      __disable_irq();
      gl30_control_lease_ack_submitted(&g_control_lease,
                                       lease_snapshot.ack_revision);
      if (ack_primask == 0u) {
        __enable_irq();
      }
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

    primask = __get_PRIMASK();
    __disable_irq();
    foc_snapshot = g_foc;
    safety_snapshot = g_safety;
    isr_cycles = g_last_isr_cycles;
    encoder_status = g_encoder_status;
    dropped = g_dropped_commands + g_telemetry_drops;
    g_telemetry_due = false;
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

static bool encoder_is_healthy(void) {
  gl30_factory_encoder_sample_t encoder;
  gl30_factory_encoder_snapshot(&encoder);
  /* Slow I2C may have run since the monitor task's entry timestamp. */
  return control_ready(&encoder, gl30_timebase_now_us(), GL30_ENCODER_STALE_US);
}

static bool update_current_zero(const uint16_t raw[3], float offsets[3]) {
  bool ready;
  bool failed;
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const bool analog_ready = !gl30_safety_fault_latched(&g_safety) &&
      gl30_drv8316_is_configured() &&
      LL_GPIO_IsOutputPinSet(DRV8316_NSLEEP_GPIO_Port, DRV8316_NSLEEP_Pin) &&
      LL_GPIO_IsInputPinSet(DRV8316_NSLEEP_GPIO_Port, DRV8316_NSLEEP_Pin) &&
      LL_GPIO_IsOutputPinSet(MOTOR_PWR_EN_GPIO_Port, MOTOR_PWR_EN_Pin) &&
      LL_GPIO_IsInputPinSet(MOTOR_PWR_EN_GPIO_Port, MOTOR_PWR_EN_Pin) &&
      g_vbus_v >= GL30_VBUS_MIN_RUN_V && g_vbus_v < GL30_VBUS_FAULT_V;
  const bool bridge_off = LL_TIM_IsEnabledAllOutputs(TIM1) == 0u &&
      LL_GPIO_IsOutputPinSet(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin) &&
      LL_GPIO_IsInputPinSet(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin);
  gl30_current_zero_update(&g_current_zero, gl30_timebase_now_us(),
                           analog_ready, bridge_off, raw);
  ready = g_current_zero.state == GL30_CURRENT_ZERO_READY;
  failed = g_current_zero.state == GL30_CURRENT_ZERO_FAILED;
  if (ready && offsets != NULL) {
    offsets[0] = g_current_zero.offset[0];
    offsets[1] = g_current_zero.offset[1];
    offsets[2] = g_current_zero.offset[2];
  }
  if (failed) {
    latch_fault(GL30_FAULT_STARTUP);
  }
  if (primask == 0u) {
    __enable_irq();
  }
  /* An interrupt released above may have invalidated the completed window. */
  return ready && g_current_zero.state == GL30_CURRENT_ZERO_READY;
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

/* Call with PRIMASK held: the ADC publishes a 64-bit timestamp on Cortex-M4. */
static bool adc_sample_is_fresh(uint64_t now_us) {
  return g_motor_temperature_sample_received &&
      now_us >= g_last_adc_sample_us &&
      now_us - g_last_adc_sample_us < GL30_ADC_SAMPLE_STALE_US;
}

static bool shared_driver_fault_asserted_locked(void) {
  /* Ordinary re-arm releases DRVOFF and waits for nFAULT with MOE still off.
   * The driver checks the line before enabling PWM; keep monitoring once
   * either the actual MOE or the driver's output state is enabled. */
  if (g_driver_arm_in_progress && LL_TIM_IsEnabledAllOutputs(TIM1) == 0u &&
      !gl30_drv8316_outputs_enabled()) {
    return false;
  }
  /* DRVOFF high may itself pull nFAULT low. Use the actual released pad,
   * not an old TIM1 break flag, before interpreting shared HARD_FAULT_N. */
  return !LL_GPIO_IsInputPinSet(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin) &&
      !LL_GPIO_IsInputPinSet(GPIOB, LL_GPIO_PIN_12);
}

static uint32_t power_startup_fault_locked(uint64_t now_us) {
  if (!adc_sample_is_fresh(now_us)) {
    return GL30_FAULT_ADC_SYNC;
  }
  if (SystemCoreClock != GL30_SYSCLK_HZ ||
      (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0u) {
    return GL30_FAULT_STARTUP;
  }
  if (!encoder_is_healthy()) {
    return GL30_FAULT_ENCODER;
  }
  if (!LL_GPIO_IsOutputPinSet(MOTOR_PWR_EN_GPIO_Port, MOTOR_PWR_EN_Pin) ||
      !LL_GPIO_IsInputPinSet(MOTOR_PWR_EN_GPIO_Port, MOTOR_PWR_EN_Pin)) {
    return GL30_FAULT_STARTUP;
  }
  if (g_power_stage == GL30_POWER_WAIT_BUS) {
    if (LL_GPIO_IsOutputPinSet(DRV8316_NSLEEP_GPIO_Port, DRV8316_NSLEEP_Pin) ||
        LL_GPIO_IsInputPinSet(DRV8316_NSLEEP_GPIO_Port, DRV8316_NSLEEP_Pin)) {
      return GL30_FAULT_STARTUP;
    }
  } else {
    if (!LL_GPIO_IsOutputPinSet(DRV8316_NSLEEP_GPIO_Port, DRV8316_NSLEEP_Pin) ||
        !LL_GPIO_IsInputPinSet(DRV8316_NSLEEP_GPIO_Port, DRV8316_NSLEEP_Pin)) {
      return GL30_FAULT_STARTUP;
    }
    if (!(g_vbus_v >= GL30_VBUS_MIN_RUN_V && g_vbus_v < GL30_VBUS_FAULT_V)) {
      return GL30_FAULT_VBUS;
    }
  }
  if (g_power_stage < GL30_POWER_COMPLETE) {
    if (LL_TIM_IsEnabledAllOutputs(TIM1) != 0u ||
        gl30_drv8316_outputs_enabled()) {
      return GL30_FAULT_STARTUP;
    }
    if (g_power_stage != GL30_POWER_VERIFY_DRIVER &&
        (!LL_GPIO_IsOutputPinSet(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin) ||
         !LL_GPIO_IsInputPinSet(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin))) {
      return GL30_FAULT_STARTUP;
    }
  } else if (LL_TIM_IsEnabledAllOutputs(TIM1) != 0u &&
             !gl30_drv8316_outputs_enabled()) {
    return GL30_FAULT_STARTUP;
  }
  if (g_power_stage == GL30_POWER_COMPLETE &&
      shared_driver_fault_asserted_locked()) {
    return GL30_FAULT_HARDWARE_BKIN;
  }
  return 0u;
}

static void run_power_startup(void) {
  bool configure = false;
  bool verify = false;
  uint32_t expected_off_generation = 0u;
  uint32_t primask = __get_PRIMASK();
  if (primask != 0u) {
    /* Foreground-only: never let a masked caller enter synchronous SPI. */
    latch_fault(GL30_FAULT_STARTUP);
    return;
  }
  __disable_irq();
  const uint64_t now_us = gl30_timebase_now_us();
  if (gl30_safety_fault_latched(&g_safety) || g_power_stage == GL30_POWER_FAILED) {
    goto unlock;
  }

  if (g_power_stage == GL30_POWER_WAIT_LOGIC) {
    /* No command is consumed here: this only prepares an idle power stage.
     * ESP waits for READY before accepting its explicit MOTOR ARM operation. */
    if (SystemCoreClock != GL30_SYSCLK_HZ ||
        (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) == 0u) {
      latch_fault(GL30_FAULT_STARTUP);
      goto unlock;
    }
    if (!adc_sample_is_fresh(now_us) || !encoder_is_healthy()) {
      goto unlock;
    }
    if (LL_TIM_IsEnabledAllOutputs(TIM1) != 0u ||
        gl30_drv8316_outputs_enabled() ||
        !LL_GPIO_IsOutputPinSet(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin) ||
        !LL_GPIO_IsInputPinSet(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin) ||
        LL_GPIO_IsOutputPinSet(DRV8316_NSLEEP_GPIO_Port, DRV8316_NSLEEP_Pin) ||
        LL_GPIO_IsInputPinSet(DRV8316_NSLEEP_GPIO_Port, DRV8316_NSLEEP_Pin)) {
      latch_fault(GL30_FAULT_STARTUP);
      goto unlock;
    }
    g_power_stage = GL30_POWER_WAIT_BUS;
    g_power_stage_started_us = now_us;
    g_bus_valid_since_us = 0u;
    LL_GPIO_SetOutputPin(MOTOR_PWR_EN_GPIO_Port, MOTOR_PWR_EN_Pin);
    goto unlock;
  }

  const uint32_t fault = power_startup_fault_locked(now_us);
  if (fault != 0u) {
    latch_fault(fault);
    goto unlock;
  }
  /* Keep the default failure branch for an invalid stored state value. */
  switch ((uint32_t)g_power_stage) {
    case GL30_POWER_WAIT_BUS:
      if (now_us - g_power_stage_started_us >= GL30_STARTUP_BUS_TIMEOUT_US) {
        latch_fault(GL30_FAULT_VBUS);
      } else if (!(g_vbus_v >= GL30_VBUS_MIN_RUN_V && g_vbus_v < GL30_VBUS_FAULT_V)) {
        g_bus_valid_since_us = 0u;
      } else if (g_bus_valid_since_us == 0u) {
        g_bus_valid_since_us = now_us;
      } else if (now_us - g_bus_valid_since_us >= GL30_STARTUP_BUS_STABLE_US) {
        g_power_stage = GL30_POWER_WAIT_WAKE;
        g_power_stage_started_us = now_us;
        LL_GPIO_SetOutputPin(DRV8316_NSLEEP_GPIO_Port, DRV8316_NSLEEP_Pin);
      }
      break;
    case GL30_POWER_WAIT_WAKE:
      configure = now_us - g_power_stage_started_us >= GL30_STARTUP_DRIVER_WAKE_US;
      if (configure) {
        expected_off_generation = gl30_drv8316_off_generation_snapshot();
      }
      break;
    case GL30_POWER_WAIT_ZERO:
      if (!gl30_drv8316_is_configured()) {
        latch_fault(GL30_FAULT_STARTUP);
      } else if (now_us - g_power_stage_started_us >= GL30_ADC_ZERO_TIMEOUT_US) {
        latch_fault(GL30_FAULT_STARTUP);
      } else if (g_current_zero.state == GL30_CURRENT_ZERO_READY) {
        g_power_stage = GL30_POWER_VERIFY_DRIVER;
        expected_off_generation = gl30_drv8316_off_generation_snapshot();
        verify = true;
      }
      break;
    case GL30_POWER_COMPLETE:
      if (!gl30_drv8316_is_configured() || !gl30_drv8316_startup_verified() ||
          g_current_zero.state != GL30_CURRENT_ZERO_READY) {
        latch_fault(GL30_FAULT_STARTUP);
      }
      break;
    case GL30_POWER_WAIT_LOGIC:
    case GL30_POWER_VERIFY_DRIVER:
    case GL30_POWER_FAILED:
    default:
      latch_fault(GL30_FAULT_STARTUP);
      break;
  }
unlock:
  if (primask == 0u) {
    __enable_irq();
  }

  /* Bounded SPI and delays must remain interruptible. A fault during either
   * operation sets FAILED; no foreground completion may overwrite it. */
  if (configure || verify) {
    if (gl30_safety_fault_latched(&g_safety) || g_power_stage == GL30_POWER_FAILED) {
      return;
    }
    const bool ok = configure ? gl30_drv8316_configure(expected_off_generation) :
        gl30_drv8316_verify_startup(expected_off_generation);
    primask = __get_PRIMASK();
    __disable_irq();
    if (!gl30_safety_fault_latched(&g_safety) && g_power_stage != GL30_POWER_FAILED) {
      uint32_t completion_fault = power_startup_fault_locked(gl30_timebase_now_us());
      if (verify && completion_fault == 0u &&
          shared_driver_fault_asserted_locked()) {
        completion_fault = GL30_FAULT_HARDWARE_BKIN;
      }
      if (!ok || completion_fault != 0u) {
        latch_fault(completion_fault != 0u ? completion_fault : GL30_FAULT_STARTUP);
      } else {
        g_power_stage = configure ? GL30_POWER_WAIT_ZERO : GL30_POWER_COMPLETE;
        g_power_stage_started_us = gl30_timebase_now_us();
      }
    }
    if (primask == 0u) {
      __enable_irq();
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
  /* Also expire an attempt when the ADC stops delivering frames. */
  (void)update_current_zero(NULL, NULL);
  /* The beta equation uses logf. Run it at the 200 Hz monitor cadence,
   * outside the current-loop ISR. No sample is not a valid temperature. */
  if (!g_motor_temperature_sample_received) {
    hardware_safe_state();
    return;
  }
  {
    float temperature_c;
    if (!gl30_board_ntc_temperature_c(g_motor_temperature_raw, &temperature_c)) {
      latch_fault(GL30_FAULT_TEMPERATURE);
      return;
    }
    g_motor_temperature_c = temperature_c;
    if (temperature_c >= GL30_MOTOR_TEMP_FAULT_C) {
      latch_fault(GL30_FAULT_TEMPERATURE);
      return;
    }
  }
  run_power_startup();
  if (gl30_safety_fault_latched(&g_safety)) {
    return;
  }
  run_slow_sensors(now_us);

  power_ok = g_vbus_v >= GL30_VBUS_MIN_RUN_V &&
             g_vbus_v < GL30_VBUS_FAULT_V;
  driver_ok = g_power_stage == GL30_POWER_COMPLETE &&
              gl30_drv8316_is_configured() && gl30_drv8316_startup_verified();
  alignment_ok = GL30_ELECTRICAL_ZERO_VALID != 0u;

  if (gl30_drv8316_outputs_enabled()) {
    gl30_drv8316_status_t driver_status;
    if (!gl30_drv8316_read_faults(&driver_status)) {
      latch_fault(GL30_FAULT_DRIVER_SPI);
      return;
    }
    if (!driver_status.n_fault_released || driver_status.normalized_faults != 0u) {
      latch_fault(GL30_FAULT_HARDWARE_BKIN);
      return;
    }
  }

  self_test_ok = SystemCoreClock == GL30_SYSCLK_HZ &&
                 (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0u;
  encoder_ok = encoder_is_healthy();
  primask = __get_PRIMASK();
  __disable_irq();
  was_active = g_safety.startup_state == GL30_STARTUP_ACTIVE;
  adc_ok = g_current_zero.state == GL30_CURRENT_ZERO_READY;
  const bool was_ready = g_safety.startup_state == GL30_STARTUP_READY || was_active;
  gl30_safety_set_startup_checks(&g_safety, power_ok, self_test_ok,
                                 encoder_ok, driver_ok, adc_ok,
                                 alignment_ok);
  if (g_safety.startup_state == GL30_STARTUP_READY && !was_ready) {
    g_arm_ready_since_us = gl30_timebase_now_us();
  } else if (g_safety.startup_state != GL30_STARTUP_READY &&
             g_safety.startup_state != GL30_STARTUP_ACTIVE) {
    g_arm_ready_since_us = 0u;
  }
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
  }
}

static void check_watchdog_reset(void) {
  const bool watchdog_reset = LL_RCC_IsActiveFlag_IWDGRST() != 0u ||
                              LL_RCC_IsActiveFlag_WWDGRST() != 0u;
  LL_RCC_ClearResetFlags();
  if (watchdog_reset) {
    latch_fault(GL30_FAULT_STARTUP);
  }
}

static void service_watchdog(uint64_t now_us) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const bool safely_latched = gl30_safety_fault_latched(&g_safety) &&
      g_power_stage == GL30_POWER_FAILED &&
      LL_TIM_IsEnabledAllOutputs(TIM1) == 0u && !gl30_drv8316_outputs_enabled() &&
      LL_GPIO_IsOutputPinSet(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin) &&
      LL_GPIO_IsInputPinSet(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin) &&
      !LL_GPIO_IsOutputPinSet(DRV8316_NSLEEP_GPIO_Port, DRV8316_NSLEEP_Pin) &&
      !LL_GPIO_IsInputPinSet(DRV8316_NSLEEP_GPIO_Port, DRV8316_NSLEEP_Pin) &&
      !LL_GPIO_IsOutputPinSet(MOTOR_PWR_EN_GPIO_Port, MOTOR_PWR_EN_Pin) &&
      !LL_GPIO_IsInputPinSet(MOTOR_PWR_EN_GPIO_Port, MOTOR_PWR_EN_Pin);
  if (now_us >= g_last_watchdog_refresh_us &&
      now_us - g_last_watchdog_refresh_us >= 10000u &&
      g_safety_progress != g_last_watchdog_safety_progress &&
      (safely_latched || (g_control_progress != g_last_watchdog_progress &&
                          adc_sample_is_fresh(now_us)))) {
    g_last_watchdog_progress = g_control_progress;
    g_last_watchdog_safety_progress = g_safety_progress;
    g_last_watchdog_refresh_us = now_us;
    LL_IWDG_ReloadCounter(IWDG);
  }
  if (primask == 0u) {
    __enable_irq();
  }
}

void gl30_app_init(void) {
  uint64_t now_us;

  /* Reinforce reset-off before starting the idle preparation sequence.
   * Preparation may request VM and wake the driver, but never enables MOE. */
  LL_GPIO_SetPinOutputType(SYS_FAULT_N_GPIO_Port, SYS_FAULT_N_Pin,
                           LL_GPIO_OUTPUT_PUSHPULL);
  LL_GPIO_SetPinOutputType(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin,
                           LL_GPIO_OUTPUT_OPENDRAIN);
  LL_GPIO_ResetOutputPin(SYS_FAULT_N_GPIO_Port, SYS_FAULT_N_Pin);
  LL_GPIO_ResetOutputPin(MOTOR_PWR_EN_GPIO_Port, MOTOR_PWR_EN_Pin);
  LL_GPIO_ResetOutputPin(DRV8316_NSLEEP_GPIO_Port, DRV8316_NSLEEP_Pin);
  LL_GPIO_SetOutputPin(DRV8316_DRVOFF_GPIO_Port, DRV8316_DRVOFF_Pin);

  configure_dwt_counter();
  gl30_timebase_init();
  now_us = gl30_timebase_now_us();
  init_runtime_state(now_us);
  gl30_drv8316_init();
  hardware_safe_state();
  publish_fault_line(true);
  check_watchdog_reset();
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

  /* Encoder health alone cannot qualify power preparation or electrical zero.
   * A runtime loss after ACTIVE is promoted to a latched fault. */
  gl30_safety_set_warning(&g_safety, GL30_WARNING_ENCODER,
                          !encoder_is_healthy());
  g_last_watchdog_progress = g_control_progress;
  g_last_watchdog_safety_progress = g_safety_progress;
  g_last_watchdog_refresh_us = gl30_timebase_now_us();
}

void gl30_app_run_once(void) {
  const uint64_t now_us = gl30_timebase_now_us();
  uint32_t primask;
  bool faulted;

  process_pending_control_request();
  process_pending_command();
  run_monitor(now_us);
  send_telemetry();

  /* An IRQ fault must not be overwritten by a stale foreground ready state. */
  primask = __get_PRIMASK();
  __disable_irq();
  faulted = gl30_safety_fault_latched(&g_safety);
  publish_fault_line(faulted ||
      (g_safety.startup_state != GL30_STARTUP_READY &&
       g_safety.startup_state != GL30_STARTUP_ACTIVE));
  if (primask == 0u) {
    __enable_irq();
  }
  service_watchdog(gl30_timebase_now_us());
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
  float zero[3];

  if (LL_ADC_IsActiveFlag_JEOS(ADC1) == 0u) {
    return;
  }
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
  g_motor_temperature_raw = (uint16_t)raw_temp;
  g_motor_temperature_sample_received = true;
  g_last_adc_sample_us = gl30_timebase_now_us();

  /* A dip between monitor visits must restart the bus-settling window. */
  if (g_power_stage == GL30_POWER_WAIT_BUS && g_vbus_v < GL30_VBUS_MIN_RUN_V) {
    g_bus_valid_since_us = 0u;
  }

  /* Never hide bus protection behind calibration's early return. */
  if (g_vbus_v >= GL30_VBUS_FAULT_V) {
    latch_fault(GL30_FAULT_VBUS);
    g_control_progress++;
    return;
  }
  if (g_power_stage >= GL30_POWER_WAIT_WAKE &&
      g_power_stage <= GL30_POWER_COMPLETE && g_vbus_v < GL30_VBUS_MIN_RUN_V) {
    latch_fault(GL30_FAULT_VBUS);
    g_control_progress++;
    return;
  }
  {
    const uint16_t raw[3] = {(uint16_t)raw_a, (uint16_t)raw_b, (uint16_t)raw_c};
    if (!update_current_zero(raw, zero)) {
      gl30_foc_force_zero(&g_foc);
      if (gl30_drv8316_outputs_enabled() ||
          LL_TIM_IsEnabledAllOutputs(TIM1) != 0u) {
        hardware_safe_state();
      }
      goto finish;
    }
  }

  current_a = ((float)raw_a - zero[0]) *
              raw_to_volts / GL30_CSA_GAIN_V_PER_A;
  current_b = ((float)raw_b - zero[1]) *
              raw_to_volts / GL30_CSA_GAIN_V_PER_A;
  current_c = ((float)raw_c - zero[2]) *
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
finish:
  {
    const uint32_t elapsed_cycles = DWT->CYCCNT - cycle_start;
    g_last_isr_cycles = saturate_u16(elapsed_cycles);
    if (elapsed_cycles > GL30_FOC_DEADLINE_CYCLES) {
      g_safety.foc_deadline_count++;
      latch_fault(GL30_FAULT_FOC_DEADLINE);
    }
  }
  g_control_progress++;
}

void gl30_app_tim1_break_irq(void) {
  if (LL_TIM_IsActiveFlag_BRK(TIM1) != 0u) {
    LL_TIM_ClearFlag_BRK(TIM1);
    latch_fault(GL30_FAULT_HARDWARE_BKIN);
  }
}

void gl30_app_tim2_irq(void) {
  gl30_timebase_on_tim2_overflow();
}

void gl30_app_tim6_irq(void) {
  gl30_factory_encoder_sample_t encoder;
  uint64_t now_us;
  bool encoder_ok;
  bool was_active;

  if (LL_TIM_IsActiveFlag_UPDATE(TIM6) == 0u) {
    return;
  }
  LL_TIM_ClearFlag_UPDATE(TIM6);
  gl30_factory_encoder_poll_4k();
  gl30_factory_encoder_snapshot(&encoder);
  now_us = gl30_timebase_now_us();
  encoder_ok = control_ready(&encoder, now_us, GL30_ENCODER_STALE_US);
  g_encoder_status = (uint16_t)encoder.status;
  was_active = g_safety.startup_state == GL30_STARTUP_ACTIVE;
  if (was_active && !encoder_ok) {
    latch_fault(GL30_FAULT_ENCODER);
    return;
  }
  gl30_foc_observer_tick_4k(&g_foc, encoder.angle_rad, encoder_ok);
}

void gl30_app_tim7_irq(void) {
  uint64_t now_us;
  bool faulted_before_tick;

  if (LL_TIM_IsActiveFlag_UPDATE(TIM7) == 0u) {
    return;
  }
  LL_TIM_ClearFlag_UPDATE(TIM7);
  ++g_safety_progress;
  {
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    /* ADC priority is higher: sample time and observation time must be taken
     * under the same mask to avoid treating a newer IRQ sample as future. */
    now_us = gl30_timebase_now_us();
    const bool adc_required =
        (g_power_stage >= GL30_POWER_WAIT_BUS && g_power_stage <= GL30_POWER_COMPLETE) ||
        (g_power_stage == GL30_POWER_WAIT_LOGIC &&
         now_us - g_power_stage_started_us >= GL30_STARTUP_ADC_TIMEOUT_US);
    if (adc_required && !adc_sample_is_fresh(now_us)) {
      latch_fault(GL30_FAULT_ADC_SYNC);
    }
    if (g_power_stage == GL30_POWER_COMPLETE &&
        shared_driver_fault_asserted_locked()) {
      latch_fault(GL30_FAULT_HARDWARE_BKIN);
    }
    if (primask == 0u) {
      __enable_irq();
    }
  }
  faulted_before_tick = gl30_safety_fault_latched(&g_safety);
  gl30_safety_tick(&g_safety, now_us);
  if (!faulted_before_tick && gl30_safety_fault_latched(&g_safety)) {
    latch_fault(g_safety.fault_bits);
    return;
  }
  gl30_haptic_tick_2k(&g_foc, gl30_safety_torque_allowed(&g_safety));
  g_haptic_measurement_us = now_us;
  if (!gl30_safety_torque_allowed(&g_safety) &&
      gl30_drv8316_outputs_enabled()) {
    hardware_safe_state();
  }
  g_haptic_tick_divider++;
  g_telemetry_due = true;
  if ((g_haptic_tick_divider %
       (GL30_HAPTIC_HZ / GL30_SLOW_TELEMETRY_HZ)) == 0u) {
    g_slow_telemetry_due = true;
    g_haptic_state_due = true;
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
