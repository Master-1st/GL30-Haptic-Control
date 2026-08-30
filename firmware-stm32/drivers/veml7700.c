#include "veml7700.h"

#include <stddef.h>
#include <string.h>

#include "board_config.h"

#if !GL30_BUILD_ONLY
#include "ll_i2c_bus.h"
#endif

static gl30_veml7700_counters_t g_counters;
static uint32_t g_consecutive_i2c_errors;

static float gain_factor(uint16_t config) {
  switch ((config >> 11u) & 0x03u) {
    case 0u:
      return 1.0f;
    case 1u:
      return 2.0f;
    case 2u:
      return 0.125f;
    default:
      return 0.25f;
  }
}

static float integration_time_ms(uint16_t config) {
  switch ((config >> 6u) & 0x0Fu) {
    case 0x0u:
      return 100.0f;
    case 0x1u:
      return 200.0f;
    case 0x2u:
      return 400.0f;
    case 0x3u:
      return 800.0f;
    case 0x8u:
      return 50.0f;
    case 0xCu:
      return 25.0f;
    default:
      return 0.0f;
  }
}

float gl30_veml7700_counts_to_lux(uint16_t counts, uint16_t config) {
  const float integration_ms = integration_time_ms(config);
  const float gain = gain_factor(config);
  if (integration_ms <= 0.0f || gain <= 0.0f) {
    return 0.0f;
  }

  /* Vishay AN84323 Rev. 06-Mar-2025: 0.0672 lx/count at gain 1,
   * 100 ms. Apply its high-light polynomial above 1000 lx. */
  const float uncorrected =
      (float)counts * 0.0672f * (100.0f / integration_ms) / gain;
  if (uncorrected <= 1000.0f) {
    return uncorrected;
  }
  return uncorrected *
         (((6.0135e-13f * uncorrected - 9.3924e-9f) *
           uncorrected + 8.1488e-5f) * uncorrected + 1.0023f);
}

#if !GL30_BUILD_ONLY
static bool read_u16(uint8_t reg, uint16_t *value) {
  uint8_t data[2];
  if (value == NULL ||
      !gl30_ll_i2c_bus_mem_read_u8(I2C1, GL30_VEML7700_I2C_ADDRESS_7BIT, reg,
                                  data, sizeof(data))) {
    g_counters.i2c_errors++;
    g_consecutive_i2c_errors++;
    if (g_consecutive_i2c_errors >= 3u) {
      g_counters.configured = false;
    }
    return false;
  }
  /* VEML7700 word protocol sends the low data byte first. */
  *value = (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8u);
  return true;
}

static bool write_u16(uint8_t reg, uint16_t value) {
  uint8_t data[2] = {(uint8_t)value, (uint8_t)(value >> 8u)};
  if (!gl30_ll_i2c_bus_mem_write_u8(I2C1, GL30_VEML7700_I2C_ADDRESS_7BIT, reg,
                                   data, sizeof(data))) {
    g_counters.i2c_errors++;
    g_consecutive_i2c_errors++;
    if (g_consecutive_i2c_errors >= 3u) {
      g_counters.configured = false;
    }
    return false;
  }
  return true;
}
#endif

void gl30_veml7700_init(void) {
  memset(&g_counters, 0, sizeof(g_counters));
  g_consecutive_i2c_errors = 0u;
}

bool gl30_veml7700_configure(void) {
#if GL30_BUILD_ONLY
  return false;
#else
  uint16_t readback = 0u;
  g_counters.configured = false;
  if (!write_u16(GL30_VEML7700_REG_ALS_CONFIG,
                 GL30_VEML7700_CONFIG_VALUE) ||
      !read_u16(GL30_VEML7700_REG_ALS_CONFIG, &readback) ||
      readback != GL30_VEML7700_CONFIG_VALUE) {
    g_counters.configuration_errors++;
    return false;
  }
  g_counters.configured = true;
  g_consecutive_i2c_errors = 0u;
  return true;
#endif
}

bool gl30_veml7700_read_sample(gl30_veml7700_sample_t *out) {
  if (out == NULL || !g_counters.configured) {
    return false;
  }
#if GL30_BUILD_ONLY
  return false;
#else
  uint16_t als = 0u;
  uint16_t white = 0u;
  if (!read_u16(GL30_VEML7700_REG_ALS, &als) ||
      !read_u16(GL30_VEML7700_REG_WHITE, &white)) {
    return false;
  }
  *out = (gl30_veml7700_sample_t){
      .als_counts = als,
      .white_counts = white,
      .lux = gl30_veml7700_counts_to_lux(
          als, GL30_VEML7700_CONFIG_VALUE),
      .saturated = als >= GL30_VEML7700_SATURATION_COUNTS,
      .valid = true,
  };
  g_counters.completed_samples++;
  g_consecutive_i2c_errors = 0u;
  return true;
#endif
}

bool gl30_veml7700_is_configured(void) {
  return g_counters.configured;
}

gl30_veml7700_counters_t gl30_veml7700_counters(void) {
  return g_counters;
}
