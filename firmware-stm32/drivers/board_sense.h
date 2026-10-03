#ifndef GL30_BOARD_SENSE_H_
#define GL30_BOARD_SENSE_H_

#include <stdbool.h>
#include <stdint.h>

/* Product board: 10k pull-up to the ADC reference domain, 10k/B3950 NTC
 * to ground. Use in the slow monitor, never in the 40 kHz current ISR.
 * Returns false for rail readings or outside the configured plausible
 * temperature range; the caller must inhibit torque/latch a sensor fault.
 * Failure leaves *temperature_c untouched. This is not sensor calibration. */
bool gl30_board_ntc_temperature_c(uint16_t raw_adc, float *temperature_c);

#endif
