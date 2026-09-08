#ifndef BENCH_HW_FAKE_MAIN_H
#define BENCH_HW_FAKE_MAIN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define __IO
#define __STATIC_INLINE static inline

typedef struct {
  uint32_t ODR;
  uint32_t IDR;
  uint32_t PUPDR;
  uint32_t MODER;
  uint32_t OTYPER;
  uint32_t AFRL;
  uint32_t AFRH;
} GPIO_TypeDef;

typedef struct { bool ongoing; } ADC_TypeDef;
typedef struct { uint32_t SR; } SPI_TypeDef;
typedef struct { uint32_t DEMCR; } CoreDebug_TypeDef;
typedef struct { uint32_t CTRL; uint32_t CYCCNT; } DWT_TypeDef;

typedef struct {
  uint16_t CCR1;
  uint16_t CCR2;
  uint16_t CCR3;
  uint32_t CNT;
  uint32_t CCER;
  uint32_t SR;
  uint32_t BDTR;
  bool moe;
  bool brk_flag;
  uint8_t enabled_channels;
  uint32_t direction;
} TIM_TypeDef;

typedef struct {
  uint32_t CR1;
  uint32_t TDR;
  uint32_t ISR;
  uint16_t RDR;
} USART_TypeDef;

typedef struct {
  uint32_t APB1RSTR1;
  uint32_t CSR;
} RCC_TypeDef;

typedef struct {
  uint32_t SR;
} ADC_Common_TypeDef;

#define LL_GPIO_PIN_5  (1u << 5u)
#define LL_GPIO_PIN_6  (1u << 6u)
#define LL_GPIO_PIN_7  (1u << 7u)
#define LL_GPIO_PIN_8  (1u << 8u)
#define LL_GPIO_PIN_9  (1u << 9u)
#define LL_GPIO_PIN_10 (1u << 10u)
#define LL_GPIO_PIN_11 (1u << 11u)
#define LL_GPIO_PIN_12 (1u << 12u)
#define LL_GPIO_PIN_13 (1u << 13u)
#define LL_GPIO_PIN_14 (1u << 14u)
#define LL_GPIO_PIN_15 (1u << 15u)

#define LL_GPIO_PULL_UP    (1u << 0u)
#define LL_GPIO_PULL_DOWN  (1u << 1u)
#define LL_GPIO_PULL_NONE  0u
#define LL_GPIO_MODE_INPUT      0u
#define LL_GPIO_MODE_OUTPUT     1u
#define LL_GPIO_MODE_ALTERNATE  2u
#define LL_GPIO_OUTPUT_PUSHPULL 0u

#define LL_TIM_CHANNEL_CH1  (1u << 0u)
#define LL_TIM_CHANNEL_CH1N (1u << 1u)
#define LL_TIM_CHANNEL_CH2  (1u << 2u)
#define LL_TIM_CHANNEL_CH2N (1u << 3u)
#define LL_TIM_CHANNEL_CH3  (1u << 4u)
#define LL_TIM_CHANNEL_CH3N (1u << 5u)
#define LL_TIM_SR_BIF (1u << 7u)
#define LL_TIM_BDTR_MOE (1u << 15u)

#define LL_TIM_BREAK_INPUT_BKIN 0u
#define LL_TIM_BKIN_SOURCE_BKIN 0u
#define LL_DBGMCU_APB1_GRP1_IWDG_STOP 0x01u

#define LL_ADC_SINGLE_ENDED 0u
#define LL_ADC_INJ_RANK_1 1u
#define LL_ADC_INJ_RANK_2 2u
#define LL_USART_DATAWIDTH_8B 0u
#define LL_USART_ISR_RXNE (1u << 0u)
#define LL_USART_ISR_TXE (1u << 1u)
#define LL_USART_ISR_ORE (1u << 2u)
#define LL_USART_ISR_FE (1u << 3u)
#define LL_USART_ISR_NE (1u << 4u)
#define LL_USART_CR1_RXNEIE (1u << 0u)
#define LL_USART_CR1_TXEIE (1u << 1u)
#define LL_USART_CR1_EIE (1u << 2u)
#define LL_USART_ISR_PE  (1u << 5u)
#define LL_USART_ISR_RXFNE    (1u << 6u)
#define LL_TIM_COUNTERDIRECTION_DOWN 0u

#define CoreDebug_DEMCR_TRCENA_Msk (1u << 24u)
#define DWT_CTRL_CYCCNTENA_Msk    (1u << 0u)

#define USER_BUTTON_Pin LL_GPIO_PIN_13
#define USER_BUTTON_GPIO_Port GPIOC
#define DRV_NSLEEP_Pin LL_GPIO_PIN_7
#define DRV_NSLEEP_GPIO_Port GPIOC
#define DRV_OFF_Pin LL_GPIO_PIN_8
#define DRV_OFF_GPIO_Port GPIOC
#define DRV_CS_Pin LL_GPIO_PIN_9
#define DRV_CS_GPIO_Port GPIOC
#define ENC_CS_Pin LL_GPIO_PIN_6
#define ENC_CS_GPIO_Port GPIOB
#define USART2_Pin LL_GPIO_PIN_2
#define USART2_GPIO_Port GPIOA
#define LD2_Pin LL_GPIO_PIN_5
#define LD2_GPIO_Port GPIOA
#define TIM6_DAC_IRQn 54
#define LL_USART_DIRECTION_TX_RX 0u
#define RCC_CSR_IWDGRSTF (1u << 29u)

typedef enum {
  BENCH_HW_MOCK_SPI_OK = 0,
  BENCH_HW_MOCK_SPI_TIMEOUT_RXNE,
} bench_hw_mock_spi_behavior_t;

typedef struct {
  uint32_t Pin;
  uint32_t Mode;
  uint32_t Pull;
  uint32_t Speed;
  uint32_t OutputType;
} LL_GPIO_InitTypeDef;

typedef struct {
  uint16_t expected_tx;
  uint16_t rx_word;
  bool rx_timeout;
} bench_hw_mock_spi_plan_t;

typedef struct {
  size_t queue_len;
  size_t queue_pos;
  size_t tx_pos;
  uint32_t transfers;
  bool active;
  bool active_rx_timeout;
  bool active_never_timed_out;
  bool timed_out;
  uint16_t active_expected_tx;
  uint16_t active_rx_word;
  uint32_t active_start_us;
  size_t mismatch_count;
  bench_hw_mock_spi_plan_t queue[64];
  uint16_t tx_history[64];
  uint16_t rx_history[64];
} bench_hw_mock_spi_state_t;

static GPIO_TypeDef g_gpioa_obj;
static GPIO_TypeDef g_gpiob_obj;
static GPIO_TypeDef g_gpioc_obj;
static ADC_TypeDef g_adc1_obj;
static ADC_TypeDef g_adc2_obj;
static ADC_TypeDef g_adc3_obj;
static TIM_TypeDef g_tim1_obj;
static TIM_TypeDef g_tim2_obj;
static TIM_TypeDef g_tim6_obj;
static SPI_TypeDef g_spi1_obj;
static SPI_TypeDef g_spi3_obj;
static CoreDebug_TypeDef g_coredebug_obj;
static DWT_TypeDef g_dwt_obj;

#define GPIOA (&g_gpioa_obj)
#define GPIOB (&g_gpiob_obj)
#define GPIOC (&g_gpioc_obj)
#define ADC1 (&g_adc1_obj)
#define ADC2 (&g_adc2_obj)
#define ADC3 (&g_adc3_obj)
#define TIM1 (&g_tim1_obj)
#define TIM2 (&g_tim2_obj)
#define TIM6 (&g_tim6_obj)
#define SPI1 (&g_spi1_obj)
#define SPI3 (&g_spi3_obj)
#define CoreDebug (&g_coredebug_obj)
#define DWT (&g_dwt_obj)

static uint32_t g_bench_hw_fake_tim2_step_us = 1u;
static uint32_t g_bench_hw_fake_tim1_moe_read_delay_us = 0u;
static uint32_t g_bench_hw_fake_tim1_moe_set_time_us;
static void (*g_bench_hw_fake_time_hook)(void);
static uint32_t g_bench_hw_fake_spi1_rxne_delay_us;
static uint32_t g_bench_hw_fake_spi3_rxne_delay_us;
static uint32_t g_bench_hw_fake_spi3_start_timer;
static uint32_t g_bench_hw_fake_spi3_assert_timer, g_bench_hw_fake_spi3_release_timer;
static uint32_t g_bench_hw_fake_spi3_start_primask;
static uint32_t g_bench_hw_fake_irq_lock_start, g_bench_hw_fake_irq_lock_max_us;
static uint32_t g_bench_hw_fake_nsleep_reset_time_us;
static uint32_t g_bench_hw_fake_nsleep_set_time_us;
static uint32_t g_bench_hw_fake_nsleep_last_low_us;
static uint32_t g_bench_hw_fake_nsleep_last_high_us;
static uint32_t g_bench_hw_fake_gpio_setpinmode_calls;
static uint32_t g_bench_hw_fake_gpio_setpinmode_multi_calls;
static volatile bool g_bench_hw_fake_tim1_pwm_pins_parked;
static uint8_t g_bench_hw_fake_drv_off_state;
static uint32_t g_bench_hw_fake_drv_off_set_count;
static uint32_t g_bench_hw_fake_drv_off_reset_count;
static uint32_t g_bench_hw_fake_tim1_disable_all_outputs_count;
static uint32_t g_bench_hw_fake_tim1_enable_all_outputs_count;
static uint32_t g_bench_hw_fake_tim1_cc_disable_count;
static uint32_t g_bench_hw_fake_tim1_cc_enable_count;
static uint32_t g_bench_hw_fake_tim1_brk_disable_count;
static uint32_t g_bench_hw_fake_tim1_brk_enable_count;
static uint32_t g_bench_hw_fake_tim6_disable_irq_count;
static uint32_t g_bench_hw_fake_tim6_enable_irq_count;
static volatile bool g_bench_hw_fake_adc_sync_flags;
static volatile bool g_bench_hw_fake_adc_eocf[3];
static volatile uint16_t g_bench_hw_fake_adc_data_rank1[3];
static volatile uint16_t g_bench_hw_fake_adc_data_rank2;
static volatile uint16_t g_bench_hw_fake_adc_rank1[3];
static volatile uint16_t g_bench_hw_fake_adc_rank2;
static volatile bool g_bench_hw_fake_adc_jeos[3];
static bool g_bench_hw_fake_break_down_triggers_brk;
static uint32_t g_bench_hw_fake_primask;
static void (*g_bench_hw_fake_after_enable_irq)(void);
static uint8_t g_bench_hw_fake_usart2_rx_queue[64];
static size_t g_bench_hw_fake_usart2_rx_head;
static size_t g_bench_hw_fake_usart2_rx_tail;
static uint8_t g_bench_hw_fake_usart2_tx_queue[256];
static size_t g_bench_hw_fake_usart2_tx_head;
static size_t g_bench_hw_fake_usart2_tx_tail;
static size_t g_bench_hw_fake_usart2_tx_count;
static uint32_t g_bench_hw_fake_usart2_rx_error;
static uint32_t g_bench_hw_fake_encoder_timestamp_us;

static bench_hw_mock_spi_state_t g_spi1_state;
static bench_hw_mock_spi_state_t g_spi3_state;

static USART_TypeDef g_usart2_obj;
static RCC_TypeDef g_rcc_obj;
static ADC_Common_TypeDef g_adc_common_obj;
static volatile uint8_t g_bench_hw_fake_usart2_txe_enabled;
static volatile uint8_t g_bench_hw_fake_usart2_error_enabled;

#define USART2 (&g_usart2_obj)
#define RCC (&g_rcc_obj)

static inline uint32_t LL_TIM_GetCounter(TIM_TypeDef *TIMx);
static inline uint32_t bench_hw_fake_elapsed_since(uint32_t start);
static inline uint32_t bench_hw_fake_now_us(void);
static inline void bench_hw_fake_set_adc_samples(uint16_t ch1_rank1, uint16_t ch2_rank1,
                                              uint16_t ch3_rank1, uint16_t ch1_rank2);
static inline void bench_hw_fake_set_adc_sync_flags(bool adc1_jeos, bool adc2_jeos);
static inline void bench_hw_fake_set_adc_jeos(bool synchronized);

static inline void bench_hw_fake_spi_state_reset(bench_hw_mock_spi_state_t *state) {
  if (state == NULL) { return; }
  state->queue_len = 0u;
  state->queue_pos = 0u;
  state->tx_pos = 0u;
  state->transfers = 0u;
  state->active = false;
  state->active_rx_timeout = false;
  state->active_never_timed_out = false;
  state->timed_out = false;
  state->active_expected_tx = 0u;
  state->active_rx_word = 0u;
  state->active_start_us = 0u;
  state->mismatch_count = 0u;
  for (size_t i = 0u; i < 64u; ++i) {
    state->queue[i] = (bench_hw_mock_spi_plan_t){0u, 0u, false};
    state->tx_history[i] = 0u;
    state->rx_history[i] = 0u;
  }
}

static inline void bench_hw_fake_reset(void) {
  *GPIOA = (GPIO_TypeDef){0};
  *GPIOB = (GPIO_TypeDef){0};
  *GPIOC = (GPIO_TypeDef){0};
  *ADC1 = (ADC_TypeDef){0};
  *ADC2 = (ADC_TypeDef){0};
  *ADC3 = (ADC_TypeDef){0};
  *TIM1 = (TIM_TypeDef){0};
  *TIM2 = (TIM_TypeDef){0};
  *TIM6 = (TIM_TypeDef){0};
  TIM1->direction = LL_TIM_COUNTERDIRECTION_DOWN;
  *SPI1 = (SPI_TypeDef){0};
  *SPI3 = (SPI_TypeDef){0};
  *CoreDebug = (CoreDebug_TypeDef){0};
  *DWT = (DWT_TypeDef){0};
  bench_hw_fake_spi_state_reset(&g_spi1_state);
  bench_hw_fake_spi_state_reset(&g_spi3_state);
  g_bench_hw_fake_tim2_step_us = 1u;
  g_bench_hw_fake_tim1_moe_read_delay_us = 0u;
  g_bench_hw_fake_tim1_moe_set_time_us = 0u;
  g_bench_hw_fake_time_hook = NULL;
  g_bench_hw_fake_spi1_rxne_delay_us = UINT32_MAX;
  g_bench_hw_fake_spi3_rxne_delay_us = UINT32_MAX;
  g_bench_hw_fake_nsleep_reset_time_us = 0u;
  g_bench_hw_fake_nsleep_set_time_us = 0u;
  g_bench_hw_fake_nsleep_last_low_us = 0u;
  g_bench_hw_fake_nsleep_last_high_us = 0u;
  g_bench_hw_fake_tim1_pwm_pins_parked = false;
  g_bench_hw_fake_drv_off_state = 1u;
  g_bench_hw_fake_drv_off_set_count = 0u;
  g_bench_hw_fake_drv_off_reset_count = 0u;
  g_bench_hw_fake_tim1_disable_all_outputs_count = 0u;
  g_bench_hw_fake_tim1_enable_all_outputs_count = 0u;
  g_bench_hw_fake_tim1_cc_disable_count = 0u;
  g_bench_hw_fake_tim1_cc_enable_count = 0u;
  g_bench_hw_fake_tim1_brk_disable_count = 0u;
  g_bench_hw_fake_tim1_brk_enable_count = 0u;
  g_bench_hw_fake_tim6_disable_irq_count = 0u;
  g_bench_hw_fake_tim6_enable_irq_count = 0u;
  g_bench_hw_fake_break_down_triggers_brk = true;
  g_bench_hw_fake_primask = 0u;
  g_bench_hw_fake_irq_lock_start = g_bench_hw_fake_irq_lock_max_us = 0u;
  g_bench_hw_fake_spi3_assert_timer = g_bench_hw_fake_spi3_release_timer = 0u;
  g_bench_hw_fake_spi3_start_timer = g_bench_hw_fake_spi3_start_primask = 0u;
  g_bench_hw_fake_after_enable_irq = NULL;
  g_bench_hw_fake_adc_sync_flags = false;
  g_bench_hw_fake_adc_rank1[0u] = 0u;
  g_bench_hw_fake_adc_rank1[1u] = 0u;
  g_bench_hw_fake_adc_rank1[2u] = 0u;
  g_bench_hw_fake_adc_rank2 = 0u;
  g_bench_hw_fake_adc_jeos[0u] = true;
  g_bench_hw_fake_adc_jeos[1u] = true;
  g_bench_hw_fake_adc_jeos[2u] = true;
  g_bench_hw_fake_usart2_rx_head = 0u;
  g_bench_hw_fake_usart2_rx_tail = 0u;
  g_bench_hw_fake_usart2_tx_head = 0u;
  g_bench_hw_fake_usart2_tx_tail = 0u;
  g_bench_hw_fake_usart2_tx_count = 0u;
  for (size_t i = 0u; i < 64u; ++i) { g_bench_hw_fake_usart2_rx_queue[i] = 0u; }
  for (size_t i = 0u; i < 256u; ++i) { g_bench_hw_fake_usart2_tx_queue[i] = 0u; }
  g_bench_hw_fake_usart2_txe_enabled = 0u;
  g_bench_hw_fake_usart2_error_enabled = 0u;
  *USART2 = (USART_TypeDef){0};
  USART2->ISR = 0u;
  USART2->CR1 = 0u;
  *RCC = (RCC_TypeDef){0};
}

static inline uint32_t bench_hw_fake_bench_time_step_us(void) { return g_bench_hw_fake_tim2_step_us; }
static inline void bench_hw_fake_set_bench_time_step(uint32_t step_us) { g_bench_hw_fake_tim2_step_us = (step_us == 0u) ? 1u : step_us; }
static inline void bench_hw_fake_set_spi3_rxne_delay_us(uint32_t delay_us) { g_bench_hw_fake_spi3_rxne_delay_us = delay_us; }
static inline void bench_hw_fake_set_spi1_rxne_delay_us(uint32_t delay_us) { g_bench_hw_fake_spi1_rxne_delay_us = delay_us; }
static inline void bench_hw_fake_set_tim1_moe_read_delay_us(uint32_t delay_us) { g_bench_hw_fake_tim1_moe_read_delay_us = delay_us; }
static inline bool bench_hw_fake_gpio_is_set(const GPIO_TypeDef *gpio, uint32_t pin) { return (gpio != NULL) && ((gpio->ODR & pin) != 0u); }

static inline bool bench_hw_fake_get_nfault_input(void) { return (GPIOB->IDR & LL_GPIO_PIN_12) != 0u; }
static inline void bench_hw_fake_set_nfault_input(bool asserted) {
  if (asserted) { GPIOB->IDR |= LL_GPIO_PIN_12; }
  else { GPIOB->IDR &= ~LL_GPIO_PIN_12; }
}
static inline void bench_hw_fake_set_adc_samples(uint16_t ch1_rank1, uint16_t ch2_rank1,
                                                uint16_t ch3_rank1, uint16_t ch1_rank2) {
  g_bench_hw_fake_adc_rank1[0u] = ch1_rank1;
  g_bench_hw_fake_adc_rank1[1u] = ch2_rank1;
  g_bench_hw_fake_adc_rank1[2u] = ch3_rank1;
  g_bench_hw_fake_adc_rank2 = ch1_rank2;
}
static inline void bench_hw_fake_set_encoder_timestamp(uint32_t timestamp_us) {
  g_bench_hw_fake_encoder_timestamp_us = timestamp_us;
}
static inline uint32_t bench_hw_fake_encoder_timestamp(void) { return g_bench_hw_fake_encoder_timestamp_us; }
static inline void bench_hw_fake_queue_uart_rx(uint8_t value) {
  const size_t next = (g_bench_hw_fake_usart2_rx_tail + 1u) % 64u;
  if (next == g_bench_hw_fake_usart2_rx_head) {
    g_bench_hw_fake_usart2_rx_error = 1u;
    return;
  }
  g_bench_hw_fake_usart2_rx_queue[g_bench_hw_fake_usart2_rx_tail] = value;
  g_bench_hw_fake_usart2_rx_tail = next;
}
static inline void bench_hw_fake_clear_uart_queues(void) {
  g_bench_hw_fake_usart2_rx_head = 0u;
  g_bench_hw_fake_usart2_rx_tail = 0u;
  g_bench_hw_fake_usart2_tx_head = 0u;
  g_bench_hw_fake_usart2_tx_tail = 0u;
  g_bench_hw_fake_usart2_tx_count = 0u;
  g_bench_hw_fake_usart2_rx_error = 0u;
  g_bench_hw_fake_encoder_timestamp_us = 0u;
}
static inline void bench_hw_fake_set_adc_jeos(bool synchronized) {
  g_bench_hw_fake_adc_jeos[0u] = synchronized;
  g_bench_hw_fake_adc_jeos[1u] = synchronized;
  g_bench_hw_fake_adc_jeos[2u] = synchronized;
}
static inline void bench_hw_fake_set_adc_sync_flags(bool adc1_jeos, bool adc2_jeos) {
  g_bench_hw_fake_adc_jeos[0u] = adc1_jeos;
  g_bench_hw_fake_adc_jeos[1u] = adc2_jeos;
  g_bench_hw_fake_adc_jeos[2u] = adc1_jeos && adc2_jeos;
}
static inline void bench_hw_fake_set_button_input(bool pressed) {
  if (pressed) { GPIOC->IDR |= USER_BUTTON_Pin; } else { GPIOC->IDR &= ~USER_BUTTON_Pin; }
}

static inline uint32_t bench_hw_fake_nsleep_low_time_us(void) { return g_bench_hw_fake_nsleep_reset_time_us; }
static inline uint32_t bench_hw_fake_nsleep_high_time_us(void) { return g_bench_hw_fake_nsleep_set_time_us; }
static inline uint32_t bench_hw_fake_nsleep_last_low_duration_us(void) { return g_bench_hw_fake_nsleep_last_low_us; }
static inline uint32_t bench_hw_fake_nsleep_last_high_duration_us(void) { return g_bench_hw_fake_nsleep_last_high_us; }
static inline bool bench_hw_fake_tim1_moe(void) { return TIM1->moe; }
static inline uint8_t bench_hw_fake_tim1_channel_mask(void) { return TIM1->enabled_channels; }
static inline uint32_t bench_hw_fake_drv_off_state(void) { return (uint32_t)g_bench_hw_fake_drv_off_state; }
static inline uint32_t bench_hw_fake_drv_off_set_count(void) { return g_bench_hw_fake_drv_off_set_count; }
static inline uint32_t bench_hw_fake_drv_off_reset_count(void) { return g_bench_hw_fake_drv_off_reset_count; }
static inline uint32_t bench_hw_fake_gpio_setpinmode_calls(void) { return g_bench_hw_fake_gpio_setpinmode_calls; }
static inline uint32_t bench_hw_fake_gpio_setpinmode_multi_calls(void) { return g_bench_hw_fake_gpio_setpinmode_multi_calls; }
static inline uint32_t bench_hw_fake_gpio_pull_for_pin(const GPIO_TypeDef *GPIOx, uint32_t Pin) {
  if (GPIOx == NULL || Pin == 0u) { return LL_GPIO_PULL_NONE; }
  for (uint32_t bit = 0u; bit < 16u; ++bit) {
    const uint32_t pin = 1u << bit;
    if ((Pin & pin) != 0u) {
      return (GPIOx->PUPDR >> (bit * 2u)) & 0x3u;
    }
  }
  return LL_GPIO_PULL_NONE;
}
static inline void bench_hw_fake_set_break_down_brk(bool triggers_brk) { g_bench_hw_fake_break_down_triggers_brk = triggers_brk; }
static inline uint32_t bench_hw_fake_tim1_disable_all_outputs_count(void) { return g_bench_hw_fake_tim1_disable_all_outputs_count; }
static inline uint32_t bench_hw_fake_tim1_enable_all_outputs_count(void) { return g_bench_hw_fake_tim1_enable_all_outputs_count; }
static inline uint32_t bench_hw_fake_tim1_cc_disable_count(void) { return g_bench_hw_fake_tim1_cc_disable_count; }
static inline uint32_t bench_hw_fake_tim1_cc_enable_count(void) { return g_bench_hw_fake_tim1_cc_enable_count; }
static inline uint32_t bench_hw_fake_tim1_brk_disable_count(void) { return g_bench_hw_fake_tim1_brk_disable_count; }
static inline uint32_t bench_hw_fake_tim1_brk_enable_count(void) { return g_bench_hw_fake_tim1_brk_enable_count; }
static inline uint32_t bench_hw_fake_tim6_disable_irq_count(void) { return g_bench_hw_fake_tim6_disable_irq_count; }
static inline uint32_t bench_hw_fake_tim6_enable_irq_count(void) { return g_bench_hw_fake_tim6_enable_irq_count; }
#ifndef BENCH_HW_FAKE_USE_REAL_ADC_DIAGNOSTICS
static inline void Bench_AdcDiagnostics(struct bench_adc_diag *out) {
  if (out == NULL) { return; }
  *out = (struct bench_adc_diag){0u, 0u, 0u, {0u, 0u, 0u, 0u}, 0u};
}
#endif

static inline void bench_hw_fake_spi_queue_clear_spi3(void) { bench_hw_fake_spi_state_reset(&g_spi3_state); }
static inline void bench_hw_fake_spi_queue_clear_spi1(void) { bench_hw_fake_spi_state_reset(&g_spi1_state); }

static inline void bench_hw_fake_spi_push(bench_hw_mock_spi_state_t *state, uint16_t expected_tx,
                                         uint16_t rx_word, bool rx_timeout) {
  if (state == NULL || state->queue_len >= 64u) { return; }
  state->queue[state->queue_len] = (bench_hw_mock_spi_plan_t){expected_tx, rx_word, rx_timeout};
  ++state->queue_len;
}

static inline void bench_hw_fake_spi_replace(bench_hw_mock_spi_state_t *state, size_t index,
                                            uint16_t expected_tx, uint16_t rx_word, bool rx_timeout) {
  if (state == NULL || index >= state->queue_len) { return; }
  state->queue[index] = (bench_hw_mock_spi_plan_t){expected_tx, rx_word, rx_timeout};
}

static inline void bench_hw_fake_spi3_push(uint16_t expected_tx, uint16_t rx_word, bool rx_timeout) {
  bench_hw_fake_spi_push(&g_spi3_state, expected_tx, rx_word, rx_timeout);
}

static inline void bench_hw_fake_spi1_push(uint16_t expected_tx, uint16_t rx_word, bool rx_timeout) {
  bench_hw_fake_spi_push(&g_spi1_state, expected_tx, rx_word, rx_timeout);
}

static inline size_t bench_hw_fake_spi1_transfer_count(void) { return g_spi1_state.tx_pos; }
static inline size_t bench_hw_fake_spi1_queue_pos(void) { return g_spi1_state.queue_pos; }
static inline size_t bench_hw_fake_spi1_queue_len(void) { return g_spi1_state.queue_len; }
static inline uint16_t bench_hw_fake_spi1_tx_word(size_t index) { return (index < g_spi1_state.tx_pos) ? g_spi1_state.tx_history[index] : 0u; }
static inline uint16_t bench_hw_fake_spi1_rx_word(size_t index) { return (index < g_spi1_state.tx_pos) ? g_spi1_state.rx_history[index] : 0u; }
static inline uint16_t bench_hw_fake_spi1_queued_tx(size_t index) { return (index < g_spi1_state.queue_len) ? g_spi1_state.queue[index].expected_tx : 0u; }
static inline uint16_t bench_hw_fake_spi1_queued_rx(size_t index) { return (index < g_spi1_state.queue_len) ? g_spi1_state.queue[index].rx_word : 0u; }
static inline bool bench_hw_fake_spi1_queued_rx_timeout(size_t index) {
  return (index < g_spi1_state.queue_len) ? g_spi1_state.queue[index].rx_timeout : false;
}

static inline size_t bench_hw_fake_spi3_transfer_count(void) { return g_spi3_state.tx_pos; }
static inline size_t bench_hw_fake_spi3_queue_pos(void) { return g_spi3_state.queue_pos; }
static inline size_t bench_hw_fake_spi3_queue_len(void) { return g_spi3_state.queue_len; }
static inline size_t bench_hw_fake_spi3_transfer_count_all(void) { return g_spi3_state.transfers; }
static inline size_t bench_hw_fake_spi3_mismatch_count(void) { return g_spi3_state.mismatch_count; }

static inline uint16_t bench_hw_fake_spi3_tx_word(size_t index) { return (index < g_spi3_state.tx_pos) ? g_spi3_state.tx_history[index] : 0u; }
static inline uint16_t bench_hw_fake_spi3_rx_word(size_t index) { return (index < g_spi3_state.tx_pos) ? g_spi3_state.rx_history[index] : 0u; }
static inline uint16_t bench_hw_fake_spi3_queued_tx(size_t index) { return (index < g_spi3_state.queue_len) ? g_spi3_state.queue[index].expected_tx : 0u; }
static inline uint16_t bench_hw_fake_spi3_queued_rx(size_t index) { return (index < g_spi3_state.queue_len) ? g_spi3_state.queue[index].rx_word : 0u; }
static inline bool bench_hw_fake_spi3_queued_rx_timeout(size_t index) {
  return (index < g_spi3_state.queue_len) ? g_spi3_state.queue[index].rx_timeout : false;
}
static inline uint32_t bench_hw_fake_spi3_transfer_index(void) { return g_spi3_state.transfers; }
static inline uint32_t bench_hw_fake_spi3_active_start_us(void) { return g_spi3_state.active_start_us; }
static inline uint32_t bench_hw_fake_spi1_active_start_us(void) { return g_spi1_state.active_start_us; }
static inline uint32_t bench_hw_fake_spi3_active_expected_tx(void) { return g_spi3_state.active_expected_tx; }

static inline void bench_hw_fake_spi3_set_plan(size_t index, uint16_t expected_tx,
                                              uint16_t rx_word, bool rx_timeout) {
  bench_hw_fake_spi_replace(&g_spi3_state, index, expected_tx, rx_word, rx_timeout);
}

static inline void bench_hw_fake_spi1_set_plan(size_t index, uint16_t expected_tx,
                                              uint16_t rx_word, bool rx_timeout) {
  bench_hw_fake_spi_replace(&g_spi1_state, index, expected_tx, rx_word, rx_timeout);
}

static inline uint32_t LL_TIM_GetCounter(TIM_TypeDef *TIMx) {
  if (TIMx == NULL) { return 0u; }
  /* Only TIM2 is the advancing microsecond clock. TIM1 phase is controlled
   * by each fixture (or its time hook), not advanced by reading CNT. */
  if (TIMx == TIM2) {
    TIMx->CNT += g_bench_hw_fake_tim2_step_us;
    if (g_bench_hw_fake_time_hook) { g_bench_hw_fake_time_hook(); }
  }
  return TIMx->CNT;
}

static inline uint32_t bench_hw_fake_now_us(void) { return LL_TIM_GetCounter(TIM2); }

static inline uint32_t bench_hw_fake_elapsed_since(uint32_t start) { return (uint32_t)(bench_hw_fake_now_us() - start); }
static inline void bench_hw_fake_spi_clear(SPI_TypeDef *unused) { (void)unused; }

static inline void LL_GPIO_SetOutputPin(GPIO_TypeDef *GPIOx, uint32_t Pin) {
  if (GPIOx != NULL) { GPIOx->ODR |= Pin; }
  if ((GPIOx == DRV_OFF_GPIO_Port) && (Pin == DRV_OFF_Pin)) {
    ++g_bench_hw_fake_drv_off_set_count;
    g_bench_hw_fake_drv_off_state = 1u;
  }
  if ((GPIOx == DRV_CS_GPIO_Port) && (Pin == DRV_CS_Pin)) {
    g_bench_hw_fake_spi3_release_timer = TIM1->CNT;
    g_spi3_state.active = false;
  }
  if ((GPIOx == ENC_CS_GPIO_Port) && (Pin == ENC_CS_Pin)) { g_spi1_state.active = false; }
  if ((GPIOx == DRV_NSLEEP_GPIO_Port) && (Pin == DRV_NSLEEP_Pin)) {
    g_bench_hw_fake_nsleep_last_low_us = bench_hw_fake_now_us() - g_bench_hw_fake_nsleep_reset_time_us;
    g_bench_hw_fake_nsleep_set_time_us = bench_hw_fake_now_us();
  }
}

static inline void LL_GPIO_ResetOutputPin(GPIO_TypeDef *GPIOx, uint32_t Pin) {
  if (GPIOx != NULL) { GPIOx->ODR &= ~Pin; }
  if ((GPIOx == DRV_CS_GPIO_Port) && (Pin == DRV_CS_Pin)) {
    g_bench_hw_fake_spi3_assert_timer = TIM1->CNT;
  }
  if ((GPIOx == DRV_OFF_GPIO_Port) && (Pin == DRV_OFF_Pin)) {
    ++g_bench_hw_fake_drv_off_reset_count;
    g_bench_hw_fake_drv_off_state = 0u;
  }
  if ((GPIOx == DRV_NSLEEP_GPIO_Port) && (Pin == DRV_NSLEEP_Pin)) {
    g_bench_hw_fake_nsleep_last_high_us = bench_hw_fake_now_us() - g_bench_hw_fake_nsleep_set_time_us;
    g_bench_hw_fake_nsleep_reset_time_us = bench_hw_fake_now_us();
  }
}

static inline bool LL_GPIO_IsInputPinSet(const GPIO_TypeDef *GPIOx, uint32_t Pin) {
  return (GPIOx != NULL) && ((GPIOx->IDR & Pin) != 0u);
}

static inline bool LL_GPIO_IsOutputPinSet(const GPIO_TypeDef *GPIOx, uint32_t Pin) {
  return (GPIOx != NULL) && ((GPIOx->ODR & Pin) != 0u);
}
static inline void LL_GPIO_TogglePin(GPIO_TypeDef *GPIOx, uint32_t Pin) {
  if (GPIOx == NULL || Pin == 0u) { return; }
  if ((GPIOx->ODR & Pin) == 0u) { LL_GPIO_SetOutputPin(GPIOx, Pin); }
  else { LL_GPIO_ResetOutputPin(GPIOx, Pin); }
}

static inline void LL_GPIO_SetPinMode(GPIO_TypeDef *GPIOx, uint32_t Pin, uint32_t Mode) {
  ++g_bench_hw_fake_gpio_setpinmode_calls;
  if (GPIOx == NULL || Pin == 0u) { return; }
  if ((Pin & (Pin - 1u)) != 0u) {
    ++g_bench_hw_fake_gpio_setpinmode_multi_calls;
    return;
  }
  uint32_t p = 0u;
  for (uint32_t bit = 0u; bit < 16u; ++bit) {
    if ((Pin & (1u << bit)) != 0u) {
      p = bit;
      break;
    }
  }
  GPIOx->MODER &= ~(3u << (p * 2u));
  GPIOx->MODER |= (Mode & 0x3u) << (p * 2u);
}

static inline void LL_GPIO_SetPinOutputType(GPIO_TypeDef *GPIOx, uint32_t Pin, uint32_t OutputType) {
  if (GPIOx == NULL || Pin == 0u) { return; }
  const uint32_t output_type_mask = (OutputType == 0u) ? 0u : Pin;
  GPIOx->OTYPER = (GPIOx->OTYPER & ~Pin) | (output_type_mask & Pin);
}

static inline void LL_GPIO_SetPinPull(GPIO_TypeDef *GPIOx, uint32_t Pin, uint32_t Pull) {
  if (GPIOx == NULL || Pin == 0u || Pull > LL_GPIO_PULL_DOWN) { return; }
  for (uint32_t bit = 0u; bit < 16u; ++bit) {
    const uint32_t pin = 1u << bit;
    if ((Pin & pin) == 0u) { continue; }
    GPIOx->PUPDR &= ~(3u << (bit * 2u));
    GPIOx->PUPDR |= ((Pull & 0x3u) << (bit * 2u));
    if (Pull == LL_GPIO_PULL_UP) {
      GPIOx->IDR |= pin;
    } else if (Pull == LL_GPIO_PULL_DOWN) {
      GPIOx->IDR &= ~pin;
    }
  }
  if ((GPIOx == GPIOB) && ((Pin & LL_GPIO_PIN_12) != 0u)) {
    const bool brk_active = (Pull == LL_GPIO_PULL_DOWN) && g_bench_hw_fake_break_down_triggers_brk;
    TIM1->brk_flag = brk_active;
    if (brk_active) { TIM1->SR |= LL_TIM_SR_BIF; }
    else { TIM1->SR &= (uint32_t)~LL_TIM_SR_BIF; }
    if (Pull == LL_GPIO_PULL_UP) {
      GPIOB->IDR |= LL_GPIO_PIN_12;
    } else if (Pull == LL_GPIO_PULL_DOWN) {
      GPIOB->IDR &= ~LL_GPIO_PIN_12;
    }
  }
}

static inline uint32_t LL_GPIO_GetPinPull(GPIO_TypeDef *GPIOx, uint32_t Pin) {
  return bench_hw_fake_gpio_pull_for_pin(GPIOx, Pin);
}

static inline void LL_TIM_DisableAllOutputs(TIM_TypeDef *TIMx) {
  if (TIMx != NULL) {
    ++g_bench_hw_fake_tim1_disable_all_outputs_count;
    TIMx->moe = false;
    TIMx->BDTR &= (uint32_t)~LL_TIM_BDTR_MOE;
  }
}

static inline void LL_TIM_EnableAllOutputs(TIM_TypeDef *TIMx) {
  if (TIMx != NULL) {
    ++g_bench_hw_fake_tim1_enable_all_outputs_count;
    TIMx->BDTR |= LL_TIM_BDTR_MOE;
    TIMx->moe = TIMx != TIM1 || g_bench_hw_fake_tim1_moe_read_delay_us == 0u;
    if (TIMx == TIM1) { g_bench_hw_fake_tim1_moe_set_time_us = TIM2->CNT; }
  }
}
static inline bool LL_TIM_IsEnabledAllOutputs(TIM_TypeDef *TIMx) {
  if (TIMx == NULL) { return false; }
  if (TIMx == TIM1 && (TIMx->BDTR & LL_TIM_BDTR_MOE) != 0u) {
    if ((TIMx->moe == false) &&
        ((uint32_t)(TIM2->CNT - g_bench_hw_fake_tim1_moe_set_time_us) >= g_bench_hw_fake_tim1_moe_read_delay_us)) {
      TIMx->moe = true;
    }
    if (g_bench_hw_fake_tim1_moe_read_delay_us == 0u) { TIMx->moe = true; }
  }
  return TIMx->moe;
}

static inline void LL_TIM_CC_DisableChannel(TIM_TypeDef *TIMx, uint32_t Channels) {
  if (TIMx != NULL) {
    ++g_bench_hw_fake_tim1_cc_disable_count;
    TIMx->CCER &= ~Channels;
    TIMx->enabled_channels &= (uint8_t)(~((uint8_t)Channels));
  }
}
static inline void LL_TIM_CC_EnableChannel(TIM_TypeDef *TIMx, uint32_t Channels) {
  if (TIMx != NULL) {
    ++g_bench_hw_fake_tim1_cc_enable_count;
    TIMx->CCER |= Channels;
    TIMx->enabled_channels |= (uint8_t)Channels;
  }
}
static inline void LL_TIM_EnableBreakInputSource(TIM_TypeDef *TIMx, uint32_t BreakInput, uint32_t Source) { (void)TIMx; (void)BreakInput; (void)Source; }
static inline void LL_TIM_DisableIT_BRK(TIM_TypeDef *TIMx) {
  if (TIMx != NULL) { ++g_bench_hw_fake_tim1_brk_disable_count; }
}
static inline void LL_TIM_EnableIT_BRK(TIM_TypeDef *TIMx) {
  if (TIMx != NULL) { ++g_bench_hw_fake_tim1_brk_enable_count; }
}
static inline void LL_TIM_GenerateEvent_UPDATE(TIM_TypeDef *TIMx) { (void)TIMx; }
static inline void LL_TIM_EnableCounter(TIM_TypeDef *TIMx) { (void)TIMx; }
static inline void LL_TIM_DisableCounter(TIM_TypeDef *TIMx) { (void)TIMx; }
static inline void LL_TIM_ClearFlag_UPDATE(TIM_TypeDef *TIMx) { (void)TIMx; }
static inline void LL_TIM_EnableIT_UPDATE(TIM_TypeDef *TIMx) { (void)TIMx; }
static inline bool LL_TIM_IsActiveFlag_BRK(TIM_TypeDef *TIMx) { return (TIMx != NULL) && ((TIMx->SR & LL_TIM_SR_BIF) != 0u); }
static inline void LL_TIM_ClearFlag_BRK(TIM_TypeDef *TIMx) {
  if (TIMx != NULL) {
    TIMx->brk_flag = false;
    TIMx->SR &= (uint32_t)~LL_TIM_SR_BIF;
  }
}
static inline bool LL_TIM_CC_IsEnabledChannel(const TIM_TypeDef *TIMx, uint32_t Channel) {
  return (TIMx != NULL) && ((TIMx->enabled_channels & (uint8_t)Channel) == (uint8_t)Channel);
}
static inline void LL_TIM_OC_DisablePreload(TIM_TypeDef *TIMx, uint32_t Channel) { (void)TIMx; (void)Channel; }
static inline void LL_TIM_OC_EnablePreload(TIM_TypeDef *TIMx, uint32_t Channel) { (void)TIMx; (void)Channel; }
static inline void LL_TIM_OC_SetCompareCH1(TIM_TypeDef *TIMx, uint32_t val) { if (TIMx != NULL) TIMx->CCR1 = (uint16_t)val; }
static inline void LL_TIM_OC_SetCompareCH2(TIM_TypeDef *TIMx, uint32_t val) { if (TIMx != NULL) TIMx->CCR2 = (uint16_t)val; }
static inline void LL_TIM_OC_SetCompareCH3(TIM_TypeDef *TIMx, uint32_t val) { if (TIMx != NULL) TIMx->CCR3 = (uint16_t)val; }

static inline void LL_ADC_DisableDeepPowerDown(ADC_TypeDef *ADCx) { (void)ADCx; }
static inline void LL_ADC_EnableInternalRegulator(ADC_TypeDef *ADCx) { (void)ADCx; }
static inline void LL_ADC_StartCalibration(ADC_TypeDef *ADCx, uint32_t mode) { (void)ADCx; (void)mode; }
static inline bool LL_ADC_IsCalibrationOnGoing(ADC_TypeDef *ADCx) { (void)ADCx; return false; }
static inline bool LL_ADC_IsActiveFlag_ADRDY(ADC_TypeDef *ADCx) { (void)ADCx; return true; }
static inline void LL_ADC_ClearFlag_ADRDY(ADC_TypeDef *ADCx) { (void)ADCx; }
static inline bool LL_ADC_IsActiveFlag_JEOS(ADC_TypeDef *ADCx) {
  return g_bench_hw_fake_adc_jeos[ADCx == ADC1 ? 0u : ADCx == ADC2 ? 1u : 2u];
}
static inline void LL_ADC_ClearFlag_JEOS(ADC_TypeDef *ADCx) {
  g_bench_hw_fake_adc_jeos[ADCx == ADC1 ? 0u : ADCx == ADC2 ? 1u : 2u] = false;
}
static inline void LL_ADC_EnableIT_JEOS(ADC_TypeDef *ADCx) { (void)ADCx; }
static inline void LL_ADC_Enable(ADC_TypeDef *ADCx) { (void)ADCx; }
static inline void LL_ADC_INJ_StartConversion(ADC_TypeDef *ADCx) { (void)ADCx; }
static inline uint16_t LL_ADC_INJ_ReadConversionData12(ADC_TypeDef *ADCx, uint32_t rank) {
  if (ADCx == ADC1 || ADCx == NULL) {
    if (rank == LL_ADC_INJ_RANK_1) { return g_bench_hw_fake_adc_rank1[0u]; }
    return g_bench_hw_fake_adc_rank2;
  }
  if (ADCx == ADC2) { return g_bench_hw_fake_adc_rank1[1u]; }
  if (ADCx == ADC3) { return g_bench_hw_fake_adc_rank1[2u]; }
  return 0u;
}
static inline bool LL_ADC_IsActiveFlag_EOC(ADC_TypeDef *ADCx, uint32_t rank) {
  (void)ADCx;
  return (rank < 3u) ? g_bench_hw_fake_adc_eocf[rank] : false;
}

static inline bool LL_SPI_IsActiveFlag_RXNE(SPI_TypeDef *SPIx) {
  bench_hw_mock_spi_state_t *state = (SPIx == SPI1) ? &g_spi1_state : &g_spi3_state;
  if (!state->active) { return false; }
  if (!state->active_rx_timeout) { return true; }
  if (state->active_never_timed_out) { return false; }
  const uint32_t delay_us = (SPIx == SPI1) ? g_bench_hw_fake_spi1_rxne_delay_us : g_bench_hw_fake_spi3_rxne_delay_us;
  const uint32_t elapsed_us = bench_hw_fake_elapsed_since(state->active_start_us);
  if (elapsed_us < delay_us) { state->timed_out = false; return false; }
  state->timed_out = false;
  return true;
}
static inline bool LL_SPI_IsActiveFlag_TXE(SPI_TypeDef *SPIx) { (void)SPIx; return true; }
static inline bool LL_SPI_IsActiveFlag_BSY(SPI_TypeDef *SPIx) { (void)SPIx; return false; }
static inline bool LL_SPI_IsActiveFlag_OVR(SPI_TypeDef *SPIx) { (void)SPIx; return false; }
static inline void LL_SPI_ClearFlag_OVR(SPI_TypeDef *SPIx) { (void)SPIx; }
static inline void LL_SPI_Enable(SPI_TypeDef *SPIx) { (void)SPIx; }

static inline void LL_SPI_TransmitData16(SPI_TypeDef *SPIx, uint16_t tx) {
  if (SPIx == SPI3) {
    g_bench_hw_fake_spi3_start_timer = TIM1->CNT;
    g_bench_hw_fake_spi3_start_primask = g_bench_hw_fake_primask;
  }
  bench_hw_mock_spi_state_t *state = (SPIx == SPI1) ? &g_spi1_state : &g_spi3_state;
  const bench_hw_mock_spi_plan_t plan = (state->queue_pos < state->queue_len) ? state->queue[state->queue_pos]
                                                                           : (bench_hw_mock_spi_plan_t){tx, 0u, true};
  if (tx != plan.expected_tx) { ++state->mismatch_count; }
  state->active_expected_tx = plan.expected_tx;
  state->active_rx_word = plan.rx_word;
  state->active_rx_timeout = plan.rx_timeout;
  state->active_never_timed_out = plan.rx_timeout && (SPIx == SPI1 ? g_bench_hw_fake_spi1_rxne_delay_us : g_bench_hw_fake_spi3_rxne_delay_us) == UINT32_MAX;
  state->active_start_us = bench_hw_fake_now_us();
  state->timed_out = false;
  state->active = true;
  if (state->queue_pos < state->queue_len) { ++state->queue_pos; }
  state->transfers += 1u;
  if (state->tx_pos < 64u) { state->tx_history[state->tx_pos++] = tx; }
}

static inline uint16_t LL_SPI_ReceiveData16(SPI_TypeDef *SPIx) {
  bench_hw_mock_spi_state_t *state = (SPIx == SPI1) ? &g_spi1_state : &g_spi3_state;
  if (!state->active) { return 0u; }
  if ((state->tx_pos > 0u) && ((state->tx_pos - 1u) < 64u)) {
    state->rx_history[state->tx_pos - 1u] = state->active_rx_word;
  }
  state->active = false;
  return state->active_rx_word;
}
static inline bool LL_USART_IsActiveFlag_ORE(const USART_TypeDef *USARTx) { return (USARTx->ISR & LL_USART_ISR_ORE) != 0u; }
static inline bool LL_USART_IsActiveFlag_FE(const USART_TypeDef *USARTx) { return (USARTx->ISR & LL_USART_ISR_FE) != 0u; }
static inline bool LL_USART_IsActiveFlag_NE(const USART_TypeDef *USARTx) { return (USARTx->ISR & LL_USART_ISR_NE) != 0u; }
static inline bool LL_USART_IsActiveFlag_RXNE(const USART_TypeDef *USARTx) {
  return (USARTx != NULL) && ((USARTx->ISR & LL_USART_ISR_RXNE) != 0u) && (g_bench_hw_fake_usart2_rx_head != g_bench_hw_fake_usart2_rx_tail);
}
static inline uint8_t LL_USART_ReceiveData8(USART_TypeDef *USARTx) {
  (void)USARTx;
  if (g_bench_hw_fake_usart2_rx_head == g_bench_hw_fake_usart2_rx_tail) { return 0u; }
  const uint8_t value = g_bench_hw_fake_usart2_rx_queue[g_bench_hw_fake_usart2_rx_head];
  g_bench_hw_fake_usart2_rx_head = (g_bench_hw_fake_usart2_rx_head + 1u) % 64u;
  if (g_bench_hw_fake_usart2_rx_head == g_bench_hw_fake_usart2_rx_tail) {
    USARTx->ISR &= (uint32_t)~LL_USART_ISR_RXNE;
  }
  return value;
}
static inline void LL_USART_TransmitData8(USART_TypeDef *USARTx, uint8_t Value) {
  (void)USARTx;
  if (g_bench_hw_fake_usart2_tx_count >= 256u) { return; }
  g_bench_hw_fake_usart2_tx_queue[g_bench_hw_fake_usart2_tx_tail] = Value;
  g_bench_hw_fake_usart2_tx_tail = (g_bench_hw_fake_usart2_tx_tail + 1u) % 256u;
  if (g_bench_hw_fake_usart2_tx_count < 256u) { g_bench_hw_fake_usart2_tx_count++; }
}
static inline bool LL_USART_IsEnabledIT_TXE(USART_TypeDef *USARTx) { (void)USARTx; return g_bench_hw_fake_usart2_txe_enabled != 0u; }
static inline void LL_USART_EnableIT_TXE(USART_TypeDef *USARTx) { (void)USARTx; g_bench_hw_fake_usart2_txe_enabled = 1u; }
static inline void LL_USART_DisableIT_TXE(USART_TypeDef *USARTx) { (void)USARTx; g_bench_hw_fake_usart2_txe_enabled = 0u; }
static inline void LL_USART_EnableIT_RXNE(USART_TypeDef *USARTx) { (void)USARTx; USARTx->CR1 |= LL_USART_CR1_RXNEIE; }
static inline void LL_USART_EnableIT_ERROR(USART_TypeDef *USARTx) { (void)USARTx; USARTx->CR1 |= LL_USART_CR1_EIE; g_bench_hw_fake_usart2_error_enabled = 1u; }
static inline void LL_USART_DisableIT_ERROR(USART_TypeDef *USARTx) { (void)USARTx; USARTx->CR1 &= ~LL_USART_CR1_EIE; g_bench_hw_fake_usart2_error_enabled = 0u; }
static inline void LL_USART_ClearFlag_ORE(USART_TypeDef *USARTx) { USARTx->ISR &= (uint32_t)~LL_USART_ISR_ORE; }
static inline void LL_USART_ClearFlag_FE(USART_TypeDef *USARTx) { USARTx->ISR &= (uint32_t)~LL_USART_ISR_FE; }
static inline void LL_USART_ClearFlag_NE(USART_TypeDef *USARTx) { USARTx->ISR &= (uint32_t)~LL_USART_ISR_NE; }

static inline void LL_DBGMCU_APB1_GRP1_UnFreezePeriph(uint32_t Periph) { (void)Periph; }
static inline void __DMB(void) { }
static inline bool LL_USART_IsActiveFlag_TXE(const USART_TypeDef *USARTx) { (void)USARTx; return true; }
static inline uint32_t LL_TIM_GetDirection(const TIM_TypeDef *TIMx) { return TIMx->direction; }
static inline uint32_t __get_PRIMASK(void) { return g_bench_hw_fake_primask; }
static inline void __set_PRIMASK(uint32_t primask) {
  if (g_bench_hw_fake_primask && !(primask & 1u)) {
    const uint32_t dt = TIM2->CNT - g_bench_hw_fake_irq_lock_start;
    if (dt > g_bench_hw_fake_irq_lock_max_us) { g_bench_hw_fake_irq_lock_max_us = dt; }
  }
  g_bench_hw_fake_primask = primask & 1u;
}
static inline void __disable_irq(void) {
  if (!g_bench_hw_fake_primask) { g_bench_hw_fake_irq_lock_start = TIM2->CNT; }
  g_bench_hw_fake_primask = 1u;
}
static inline void __enable_irq(void) {
  g_bench_hw_fake_primask = 0u;
  if (g_bench_hw_fake_after_enable_irq != NULL) {
    g_bench_hw_fake_after_enable_irq();
    g_bench_hw_fake_after_enable_irq = NULL;
  }
}

static inline void NVIC_EnableIRQ(uint32_t irqn) {
  if (irqn == TIM6_DAC_IRQn) { ++g_bench_hw_fake_tim6_enable_irq_count; }
}
static inline void NVIC_DisableIRQ(uint32_t irqn) {
  if (irqn == TIM6_DAC_IRQn) { ++g_bench_hw_fake_tim6_disable_irq_count; }
}
static inline void LL_RCC_ClearResetFlags(void) { if (RCC != NULL) RCC->CSR = 0u; }

#endif
