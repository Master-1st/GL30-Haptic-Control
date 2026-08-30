#include "ina228.h"

#include <stddef.h>
#include <string.h>

#include "board_config.h"

#if !GL30_BUILD_ONLY
#include "ll_i2c_bus.h"
#endif

#define GL30_INA228_MANUFACTURER_ID 0x5449u
#define GL30_INA228_DEVICE_ID_MASK  0xFFF0u
#define GL30_INA228_DEVICE_ID_VALUE 0x2280u
#define GL30_INA228_DIAG_MATHOF     (1u << 9u)
#define GL30_INA228_DIAG_MEMSTAT    (1u << 0u)

static gl30_ina228_counters_t g_counters;
static uint32_t g_consecutive_i2c_errors;

static int32_t sign_extend_20(uint32_t value) {
  value &= 0xFFFFFu;
  if ((value & 0x80000u) != 0u) {
    value |= 0xFFF00000u;
  }
  return (int32_t)value;
}

static int64_t sign_extend_40(uint64_t value) {
  value &= 0xFFFFFFFFFFull;
  if ((value & 0x8000000000ull) != 0ull) {
    value |= 0xFFFFFF0000000000ull;
  }
  return (int64_t)value;
}

bool gl30_ina228_decode_raw(
    uint32_t vshunt_raw24,
    uint32_t vbus_raw24,
    uint16_t dietemp_raw16,
    uint32_t current_raw24,
    uint32_t power_raw24,
    uint64_t energy_raw40,
    uint64_t charge_raw40,
    uint16_t diag_alrt,
    gl30_ina228_sample_t *out) {
  if (out == NULL || (vshunt_raw24 & 0xFF000000u) != 0u ||
      (vbus_raw24 & 0xFF000000u) != 0u ||
      (current_raw24 & 0xFF000000u) != 0u ||
      (power_raw24 & 0xFF000000u) != 0u ||
      (energy_raw40 & 0xFFFFFF0000000000ull) != 0ull ||
      (charge_raw40 & 0xFFFFFF0000000000ull) != 0ull) {
    return false;
  }

  const int32_t vshunt_counts = sign_extend_20(vshunt_raw24 >> 4u);
  const uint32_t vbus_counts = (vbus_raw24 >> 4u) & 0xFFFFFu;
  const int32_t current_counts = sign_extend_20(current_raw24 >> 4u);
  const int64_t charge_counts = sign_extend_40(charge_raw40);

  *out = (gl30_ina228_sample_t){
      .shunt_voltage_v = (float)vshunt_counts * 0.0000003125f,
      .bus_voltage_v = (float)vbus_counts * 0.0001953125f,
      .die_temperature_c = (float)(int16_t)dietemp_raw16 * 0.0078125f,
      .current_a = (float)current_counts * GL30_INA228_CURRENT_LSB_A,
      .power_w = (float)power_raw24 *
                 (3.2f * GL30_INA228_CURRENT_LSB_A),
      .energy_j = (float)energy_raw40 *
                  (51.2f * GL30_INA228_CURRENT_LSB_A),
      .charge_c = (float)charge_counts * GL30_INA228_CURRENT_LSB_A,
      .diag_alrt = diag_alrt,
      .valid = (diag_alrt & GL30_INA228_DIAG_MEMSTAT) != 0u &&
               (diag_alrt & GL30_INA228_DIAG_MATHOF) == 0u,
  };
  return true;
}

#if !GL30_BUILD_ONLY
static bool read_bytes(uint8_t reg, uint8_t *data, uint16_t length) {
  if (!gl30_ll_i2c_bus_mem_read_u8(I2C1, GL30_INA228_I2C_ADDRESS_7BIT, reg, data,
                                  (uint8_t)length)) {
    g_counters.i2c_errors++;
    g_consecutive_i2c_errors++;
    if (g_consecutive_i2c_errors >= 3u) {
      g_counters.configured = false;
    }
    return false;
  }
  return true;
}

static bool read_u16(uint8_t reg, uint16_t *value) {
  uint8_t data[2];
  if (value == NULL || !read_bytes(reg, data, sizeof(data))) {
    return false;
  }
  *value = (uint16_t)((uint16_t)data[0] << 8u) | (uint16_t)data[1];
  return true;
}

static bool read_u24(uint8_t reg, uint32_t *value) {
  uint8_t data[3];
  if (value == NULL || !read_bytes(reg, data, sizeof(data))) {
    return false;
  }
  *value = ((uint32_t)data[0] << 16u) |
           ((uint32_t)data[1] << 8u) |
           (uint32_t)data[2];
  return true;
}

static bool read_u40(uint8_t reg, uint64_t *value) {
  uint8_t data[5];
  if (value == NULL || !read_bytes(reg, data, sizeof(data))) {
    return false;
  }
  *value = ((uint64_t)data[0] << 32u) |
           ((uint64_t)data[1] << 24u) |
           ((uint64_t)data[2] << 16u) |
           ((uint64_t)data[3] << 8u) |
           (uint64_t)data[4];
  return true;
}

static bool write_u16(uint8_t reg, uint16_t value) {
  uint8_t data[2] = {(uint8_t)(value >> 8u), (uint8_t)value};
  if (!gl30_ll_i2c_bus_mem_write_u8(I2C1, GL30_INA228_I2C_ADDRESS_7BIT, reg,
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

void gl30_ina228_init(void) {
  memset(&g_counters, 0, sizeof(g_counters));
  g_consecutive_i2c_errors = 0u;
}

bool gl30_ina228_configure(void) {
#if GL30_BUILD_ONLY
  return false;
#else
  uint16_t manufacturer_id = 0u;
  uint16_t device_id = 0u;
  uint16_t readback = 0u;
  g_counters.configured = false;

  if (!read_u16(GL30_INA228_REG_MANUFACTURER_ID, &manufacturer_id) ||
      !read_u16(GL30_INA228_REG_DEVICE_ID, &device_id) ||
      manufacturer_id != GL30_INA228_MANUFACTURER_ID ||
      (device_id & GL30_INA228_DEVICE_ID_MASK) !=
          GL30_INA228_DEVICE_ID_VALUE) {
    g_counters.identity_errors++;
    return false;
  }

  /* Stop conversions while installing calibration, then enable continuous
   * VBUS/VSHUNT/temperature measurements at ~556 complete updates/s. */
  if (!write_u16(GL30_INA228_REG_ADC_CONFIG, 0x0000u) ||
      !write_u16(GL30_INA228_REG_CONFIG, GL30_INA228_CONFIG_VALUE) ||
      !write_u16(GL30_INA228_REG_SHUNT_CAL, GL30_INA228_SHUNT_CAL_VALUE) ||
      !write_u16(GL30_INA228_REG_ADC_CONFIG,
                 GL30_INA228_ADC_CONFIG_VALUE) ||
      !read_u16(GL30_INA228_REG_CONFIG, &readback) ||
      readback != GL30_INA228_CONFIG_VALUE ||
      !read_u16(GL30_INA228_REG_SHUNT_CAL, &readback) ||
      readback != GL30_INA228_SHUNT_CAL_VALUE ||
      !read_u16(GL30_INA228_REG_ADC_CONFIG, &readback) ||
      readback != GL30_INA228_ADC_CONFIG_VALUE) {
    return false;
  }

  g_counters.configured = true;
  g_consecutive_i2c_errors = 0u;
  return true;
#endif
}

bool gl30_ina228_read_sample(gl30_ina228_sample_t *out) {
  if (out == NULL || !g_counters.configured) {
    return false;
  }
#if GL30_BUILD_ONLY
  return false;
#else
  uint32_t vshunt = 0u;
  uint32_t vbus = 0u;
  uint16_t dietemp = 0u;
  uint32_t current = 0u;
  uint32_t power = 0u;
  uint64_t energy = 0u;
  uint64_t charge = 0u;
  uint16_t diag = 0u;

  if (!read_u24(GL30_INA228_REG_VSHUNT, &vshunt) ||
      !read_u24(GL30_INA228_REG_VBUS, &vbus) ||
      !read_u16(GL30_INA228_REG_DIETEMP, &dietemp) ||
      !read_u24(GL30_INA228_REG_CURRENT, &current) ||
      !read_u24(GL30_INA228_REG_POWER, &power) ||
      !read_u40(GL30_INA228_REG_ENERGY, &energy) ||
      !read_u40(GL30_INA228_REG_CHARGE, &charge) ||
      !read_u16(GL30_INA228_REG_DIAG_ALRT, &diag) ||
      !gl30_ina228_decode_raw(vshunt, vbus, dietemp, current, power,
                             energy, charge, diag, out)) {
    g_counters.data_errors++;
    return false;
  }
  g_counters.completed_samples++;
  g_consecutive_i2c_errors = 0u;
  if (!out->valid) {
    g_counters.data_errors++;
  }
  return out->valid;
#endif
}

bool gl30_ina228_is_configured(void) {
  return g_counters.configured;
}

gl30_ina228_counters_t gl30_ina228_counters(void) {
  return g_counters;
}
