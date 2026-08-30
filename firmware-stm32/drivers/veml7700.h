#ifndef GL30_VEML7700_H_
#define GL30_VEML7700_H_

#include <stdbool.h>
#include <stdint.h>

enum {
  GL30_VEML7700_REG_ALS_CONFIG = 0x00u,
  GL30_VEML7700_REG_ALS = 0x04u,
  GL30_VEML7700_REG_WHITE = 0x05u
};

typedef struct {
  uint16_t als_counts;
  uint16_t white_counts;
  float lux;
  bool saturated;
  bool valid;
} gl30_veml7700_sample_t;

typedef struct {
  uint32_t completed_samples;
  uint32_t i2c_errors;
  uint32_t configuration_errors;
  bool configured;
} gl30_veml7700_counters_t;

/* Pure conversion helper using the gain/integration encoding from ALS_CONF. */
float gl30_veml7700_counts_to_lux(uint16_t counts, uint16_t config);

void gl30_veml7700_init(void);
bool gl30_veml7700_configure(void);
bool gl30_veml7700_read_sample(gl30_veml7700_sample_t *out);
bool gl30_veml7700_is_configured(void);
gl30_veml7700_counters_t gl30_veml7700_counters(void);

#endif
