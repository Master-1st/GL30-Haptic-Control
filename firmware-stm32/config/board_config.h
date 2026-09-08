#ifndef GL30_BOARD_CONFIG_H_
#define GL30_BOARD_CONFIG_H_

#include <stdint.h>

/* This file is the single current hardware/firmware freeze. Values marked
 * HARDWARE_TUNE_REQUIRED are safe first-board values, not measured evidence. */

#define GL30_HARDWARE_TUNE_REQUIRED 1

/* Clocks. HSE 24 MHz -> PLL -> 160 MHz. */
#define GL30_HSE_HZ                 24000000u
#define GL30_SYSCLK_HZ             160000000u
#define GL30_HCLK_HZ               160000000u
#define GL30_APB1_HZ                80000000u
#define GL30_APB1_TIMER_HZ         160000000u
#define GL30_APB2_HZ               160000000u
#define GL30_ADC_HZ                 40000000u

/* Deterministic task rates. */
#define GL30_PWM_HZ                    40000u
#define GL30_OBSERVER_HZ                4000u
#define GL30_HAPTIC_HZ                  2000u
#define GL30_TELEMETRY_HZ               2000u
#define GL30_COMMAND_HZ                 1000u
#define GL30_MONITOR_HZ                  200u
#define GL30_SLOW_TELEMETRY_HZ           100u
#define GL30_AMBIENT_LIGHT_HZ             10u
#define GL30_TIMEBASE_HZ              1000000u

/* TIM1 center-aligned: f = TIMCLK / (2 * (ARR + 1)). */
#define GL30_TIM1_ARR                    1999u
#define GL30_TIM1_ADC_TRIGGER_TICK       1950u
#define GL30_TIM1_DEADTIME_TICKS           80u /* 500 ns @ 160 MHz; tune on board. */
#define GL30_TIM1_MIN_DUTY                 0.30f
#define GL30_TIM1_MAX_DUTY                 0.70f
#define GL30_TIM1_MIN_CCR_TICK              600u
#define GL30_TIM1_MAX_CCR_TICK             1400u
#define GL30_TIM1_SAMPLE_SETTLE_TICKS       500u /* 3.125 us minimum design allowance. */

#if GL30_TIM1_ADC_TRIGGER_TICK <= \
    (GL30_TIM1_MAX_CCR_TICK + GL30_TIM1_SAMPLE_SETTLE_TICKS)
#error "TIM1 ADC trigger does not leave the frozen low-side CSA settling window"
#endif

/* Links. 5 Mbps is the ESP32-S3 official UART ceiling and divides 80 MHz. */
#define GL30_UART3_BAUD                5000000u
#define GL30_DRV8316_SPI_HZ            5000000u
#define GL30_DRV8316_SPI_TIMEOUT_US        2000u
#define GL30_I2C1_HZ                    400000u
/* I2CCLK=80 MHz, Fast-mode 400 kHz, analog filter enabled, DNF=0,
 * 250 ns rise / 100 ns fall assumption. Verify SCL/SDA on the first board. */
#define GL30_I2C1_TIMINGR             0x10E11F33u

/* Slow-monitor sensors. INA228: A0=A1=GND, 5 mOhm Kelvin shunt,
 * ADCRANGE=0, CURRENT_LSB=50 uA, 150 us per input and AVG=4 gives one
 * complete VBUS/VSHUNT/TEMP update in about 1.8 ms (~556 Hz). VEML7700
 * starts at gain 1/8 and 100 ms to avoid sunlight saturation. */
#define GL30_I2C_TRANSACTION_TIMEOUT_MS        2u
#define GL30_SENSOR_CONFIG_RETRY_US        500000u
#define GL30_INA228_I2C_ADDRESS_7BIT          0x40u
#define GL30_INA228_SHUNT_OHM                 0.005f
#define GL30_INA228_CURRENT_LSB_A             0.000050f
#define GL30_INA228_CONFIG_VALUE              0x0000u
#define GL30_INA228_ADC_CONFIG_VALUE          0xF491u
#define GL30_INA228_SHUNT_CAL_VALUE           3277u
#define GL30_VEML7700_I2C_ADDRESS_7BIT        0x10u
#define GL30_VEML7700_CONFIG_VALUE            0x1000u
#define GL30_VEML7700_SATURATION_COUNTS      60000u

#if (GL30_HAPTIC_HZ % GL30_SLOW_TELEMETRY_HZ) != 0u || \
    (GL30_MONITOR_HZ % GL30_AMBIENT_LIGHT_HZ) != 0u || \
    (GL30_MONITOR_HZ % GL30_SLOW_TELEMETRY_HZ) != 0u
#error "Slow telemetry and sensor rates must divide their scheduler rates"
#endif

/* Protocol/safety. */
#define GL30_PROTOCOL_VERSION                 1u
#define GL30_PROTOCOL_MAX_PAYLOAD          4096u
#define GL30_COMM_WARN_US                 10000u
#define GL30_COMM_SAFE_ZERO_US            20000u
#define GL30_COMM_LOST_US                100000u
/* Conservative control-readiness default; replace only from vendor timing data. */
#define GL30_ENCODER_STALE_US               250u
#define GL30_ADC_SYNC_BAD_LIMIT                1u
#define GL30_FOC_DEADLINE_CYCLES       3200u /* 20 us at 160 MHz. */
#define GL30_IWDG_NOMINAL_TIMEOUT_MS          64u
#define GL30_STARTUP_ADC_TIMEOUT_US         50000u

/* Motor/control initial values. HARDWARE_TUNE_REQUIRED.
 * The supplier table labels 1.53 ohm / 330 uH as line-to-line values for the
 * star winding. The dq current loop uses phase current and phase voltage, so
 * its first-power PI is based on 0.765 ohm / 165 uH at about 150 Hz bandwidth.
 * Only raise toward 250 Hz (about 0.26 / 1200) after current polarity, ADC
 * timing, R/L, dead time, and low-amplitude step response are measured.
 * Re-identify all three line pairs before changing these values. */
#define GL30_MOTOR_POLE_PAIRS                  7u
#define GL30_MOTOR_KT_NM_PER_A              0.038f
#define GL30_MOTOR_LINE_R_OHM                1.53f
#define GL30_MOTOR_LINE_L_H              0.000330f
#define GL30_MOTOR_PHASE_R_OHM              0.765f
#define GL30_MOTOR_PHASE_L_H             0.000165f
#define GL30_CURRENT_KP_V_PER_A              0.16f
#define GL30_CURRENT_KI_V_PER_AS            720.0f
#define GL30_CURRENT_KAW                       0.2f
/* Supplier ratings are 2.13 A / 0.08 N.m continuous and 7.4 A / 0.28 N.m
 * peak, but neither has been reproduced. Keep the first board below them:
 * 1.6 A -> 0.0608 N.m, 1.75 A -> 0.0665 N.m at the initial Kt. */
#define GL30_CURRENT_SOFT_LIMIT_A              1.60f
#define GL30_CURRENT_HARD_LIMIT_A              1.75f
#define GL30_USER_TORQUE_LIMIT_NM              0.060f
#define GL30_SELF_DRIVE_LIMIT_NM               0.015f
#define GL30_ELECTRICAL_ZERO_RAD                0.0f
#define GL30_ELECTRICAL_ZERO_VALID                0u /* Set to 1 only after phase alignment. */
#define GL30_PHASE_DIRECTION                    1.0f

/* ADC/DRV8316 CSA initial conversion. VREF=3.3 V, bidirectional output zero
 * at VREF/2, CSA_GAIN=0.6 V/A. Per-channel zero is measured at every boot. */
#define GL30_ADC_FULL_SCALE_COUNTS            4095.0f
#define GL30_ADC_VREF_V                           3.3f
#define GL30_CSA_GAIN_V_PER_A                      0.6f
#define GL30_ADC_ZERO_DEFAULT_COUNTS             2048.0f
#define GL30_ADC_ZERO_CAL_SAMPLES                  512u
#define GL30_ADC_ZERO_MAX_ERROR_COUNTS             350.0f
#define GL30_VBUS_DIVIDER_RATIO                    6.0f
#define GL30_TEMP_SENSOR_MV_AT_25C              750.0f
#define GL30_TEMP_SENSOR_MV_PER_C                10.0f

/* Initial board protection values; the comparator ratios and final limits are
 * frozen only after regulator/TVS/brake characterization. */
#define GL30_VBUS_WARN_V                         13.2f
#define GL30_VBUS_CLAMP_V                        14.4f
#define GL30_VBUS_FAULT_V                        16.0f
/* 3S wireless default.  Validate sag and usable torque at 9.0 V on hardware. */
#define GL30_VBUS_MIN_RUN_V                       9.0f
#define GL30_MOTOR_TEMP_WARN_C                   70.0f
#define GL30_MOTOR_TEMP_FAULT_C                  85.0f

/* Pin table for STM32G474CET6, LQFP48. Numeric AF values are from ST DS12288
 * and STM32CubeG4 v1.6.3. This table is also used for schematic review. */
typedef enum {
  GL30_PIN_AF,
  GL30_PIN_ANALOG,
  GL30_PIN_INPUT,
  GL30_PIN_OUTPUT_PP,
  GL30_PIN_OUTPUT_OD,
  GL30_PIN_POWER,
  GL30_PIN_RESERVED
} gl30_pin_mode_t;

typedef struct {
  uint8_t package_pin;
  char port;
  uint8_t gpio_pin;
  uint8_t af;
  gl30_pin_mode_t mode;
  const char *net;
} gl30_pin_desc_t;

static const gl30_pin_desc_t GL30_PINMAP[] = {
  { 1u, '-', 0u, 0u, GL30_PIN_POWER,     "VBAT" },
  { 2u, 'C',13u, 0u, GL30_PIN_OUTPUT_OD, "SYS_FAULT_N_TO_ESP_GP1" },
  { 3u, 'C',14u, 0u, GL30_PIN_RESERVED,  "RSV_ENC_AUX" },
  { 4u, 'C',15u, 0u, GL30_PIN_OUTPUT_PP, "EXT_WATCHDOG_WDI_DNP" },
  { 5u, 'F', 0u, 0u, GL30_PIN_RESERVED,  "HSE_IN_24MHZ" },
  { 6u, 'F', 1u, 0u, GL30_PIN_RESERVED,  "HSE_OUT_24MHZ" },
  { 7u, '-', 0u, 0u, GL30_PIN_RESERVED,  "NRST_SWD_EXT_WDOG" },
  { 8u, 'A', 0u, 0u, GL30_PIN_ANALOG,    "DRV_SOA_ADC1_IN1" },
  { 9u, 'A', 1u, 0u, GL30_PIN_ANALOG,    "DRV_SOB_ADC2_IN2" },
  {10u, 'A', 2u, 0u, GL30_PIN_OUTPUT_PP, "BRAKE_FORCE_TEST" },
  {11u, 'A', 3u, 0u, GL30_PIN_OUTPUT_OD, "DRV_DRVOFF" },
  {12u, 'A', 4u, 0u, GL30_PIN_RESERVED,  "RSV_ENC_0" },
  {13u, 'A', 5u, 0u, GL30_PIN_RESERVED,  "RSV_ENC_1" },
  {14u, 'A', 6u, 0u, GL30_PIN_RESERVED,  "RSV_ENC_2" },
  {15u, 'A', 7u, 0u, GL30_PIN_RESERVED,  "RSV_ENC_3" },
  {16u, 'B', 0u, 0u, GL30_PIN_ANALOG,    "DRV_SOC_ADC3_IN12" },
  {17u, 'B', 1u, 0u, GL30_PIN_ANALOG,    "VBUS_ADC1_IN12" },
  {18u, 'B', 2u, 0u, GL30_PIN_ANALOG,    "MOTOR_TEMP_ADC2_IN12" },
  {19u, '-', 0u, 0u, GL30_PIN_POWER,     "VSSA" },
  {20u, '-', 0u, 0u, GL30_PIN_POWER,     "VREF_PLUS_3V3A" },
  {21u, '-', 0u, 0u, GL30_PIN_POWER,     "VDDA_3V3A" },
  {22u, 'B',10u, 7u, GL30_PIN_AF,        "USART3_TX_TO_ESP_J1_12_RX" },
  {23u, '-', 0u, 0u, GL30_PIN_POWER,     "VSS" },
  {24u, '-', 0u, 0u, GL30_PIN_POWER,     "VDD_3V3" },
  {25u, 'B',11u, 7u, GL30_PIN_AF,        "USART3_RX_FROM_ESP_J1_11_TX" },
  {26u, 'B',12u, 6u, GL30_PIN_AF,        "HARD_FAULT_N_TIM1_BKIN" },
  {27u, 'B',13u, 6u, GL30_PIN_AF,        "DRV_INLA_TIM1_CH1N" },
  {28u, 'B',14u, 6u, GL30_PIN_AF,        "DRV_INLB_TIM1_CH2N" },
  {29u, 'B',15u, 4u, GL30_PIN_AF,        "DRV_INLC_TIM1_CH3N" },
  {30u, 'A', 8u, 6u, GL30_PIN_AF,        "DRV_INHA_TIM1_CH1" },
  {31u, 'A', 9u, 6u, GL30_PIN_AF,        "DRV_INHB_TIM1_CH2" },
  {32u, 'A',10u, 6u, GL30_PIN_AF,        "DRV_INHC_TIM1_CH3" },
  {33u, 'A',11u,11u, GL30_PIN_AF,        "ADC_SAMPLE_TRIG_TIM1_CH4" },
  {34u, 'A',12u, 0u, GL30_PIN_RESERVED,  "USB_DP_DNP" },
  {35u, '-', 0u, 0u, GL30_PIN_POWER,     "VSS" },
  {36u, '-', 0u, 0u, GL30_PIN_POWER,     "VDD_3V3" },
  {37u, 'A',13u, 0u, GL30_PIN_RESERVED,  "SWDIO" },
  {38u, 'A',14u, 0u, GL30_PIN_RESERVED,  "SWCLK" },
  {39u, 'A',15u, 4u, GL30_PIN_AF,        "I2C1_SCL_INA228_VEML7700" },
  {40u, 'B', 3u, 6u, GL30_PIN_AF,        "DRV8316_SPI3_SCK" },
  {41u, 'B', 4u, 6u, GL30_PIN_AF,        "DRV8316_SPI3_MISO" },
  {42u, 'B', 5u, 6u, GL30_PIN_AF,        "DRV8316_SPI3_MOSI" },
  {43u, 'B', 6u, 0u, GL30_PIN_OUTPUT_PP, "DRV8316_CS_N" },
  {44u, 'B', 7u, 4u, GL30_PIN_AF,        "I2C1_SDA_INA228_VEML7700" },
  {45u, 'B', 8u, 0u, GL30_PIN_INPUT,     "BOOT0_STRAP" },
  {46u, 'B', 9u, 0u, GL30_PIN_OUTPUT_PP, "SYNC_SCOPE_TP" },
  {47u, '-', 0u, 0u, GL30_PIN_POWER,     "VSS" },
  {48u, '-', 0u, 0u, GL30_PIN_POWER,     "VDD_3V3" }
};

#define GL30_PINMAP_COUNT ((uint16_t)(sizeof(GL30_PINMAP) / sizeof(GL30_PINMAP[0])))

#endif
