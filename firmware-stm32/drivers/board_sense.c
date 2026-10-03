#include "board_sense.h"

#include <math.h>
#include <stddef.h>

#include "board_config.h"

bool gl30_board_ntc_temperature_c(uint16_t raw_adc, float *temperature_c) {
  float resistance_ohm;
  float inverse_kelvin;
  float value_c;

  if (temperature_c == NULL || raw_adc == 0u ||
      (float)raw_adc >= GL30_ADC_FULL_SCALE_COUNTS) {
    return false;
  }
  resistance_ohm = GL30_NTC_PULLUP_OHM * (float)raw_adc /
                   (GL30_ADC_FULL_SCALE_COUNTS - (float)raw_adc);
  inverse_kelvin = (1.0f / 298.15f) +
                   logf(resistance_ohm / GL30_NTC_R25_OHM) / GL30_NTC_BETA_K;
  value_c = (1.0f / inverse_kelvin) - 273.15f;
  if (!isfinite(value_c) || value_c < GL30_NTC_MIN_C ||
      value_c > GL30_NTC_MAX_C) {
    return false;
  }
  *temperature_c = value_c;
  return true;
}
