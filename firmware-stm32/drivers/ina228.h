#ifndef GL30_INA228_H_
#define GL30_INA228_H_

#include <stdbool.h>
#include <stdint.h>

enum {
  GL30_INA228_REG_CONFIG = 0x00u,
  GL30_INA228_REG_ADC_CONFIG = 0x01u,
  GL30_INA228_REG_SHUNT_CAL = 0x02u,
  GL30_INA228_REG_VSHUNT = 0x04u,
  GL30_INA228_REG_VBUS = 0x05u,
  GL30_INA228_REG_DIETEMP = 0x06u,
  GL30_INA228_REG_CURRENT = 0x07u,
  GL30_INA228_REG_POWER = 0x08u,
  GL30_INA228_REG_ENERGY = 0x09u,
  GL30_INA228_REG_CHARGE = 0x0Au,
  GL30_INA228_REG_DIAG_ALRT = 0x0Bu,
  GL30_INA228_REG_MANUFACTURER_ID = 0x3Eu,
  GL30_INA228_REG_DEVICE_ID = 0x3Fu
};

typedef struct {
  float shunt_voltage_v;
  float bus_voltage_v;
  float die_temperature_c;
  float current_a;
  float power_w;
  float energy_j;
  float charge_c;
  uint16_t diag_alrt;
  bool valid;
} gl30_ina228_sample_t;

typedef struct {
  uint32_t completed_samples;
  uint32_t i2c_errors;
  uint32_t identity_errors;
  uint32_t data_errors;
  bool configured;
} gl30_ina228_counters_t;

/* Pure conversion helper. Inputs are numeric values assembled from the
 * INA228 big-endian register bytes, including the four reserved LSBs in the
 * 24-bit VSHUNT/VBUS/CURRENT registers. */
bool gl30_ina228_decode_raw(
    uint32_t vshunt_raw24,
    uint32_t vbus_raw24,
    uint16_t dietemp_raw16,
    uint32_t current_raw24,
    uint32_t power_raw24,
    uint64_t energy_raw40,
    uint64_t charge_raw40,
    uint16_t diag_alrt,
    gl30_ina228_sample_t *out);

void gl30_ina228_init(void);
bool gl30_ina228_configure(void);
bool gl30_ina228_read_sample(gl30_ina228_sample_t *out);
bool gl30_ina228_is_configured(void);
gl30_ina228_counters_t gl30_ina228_counters(void);

#endif
