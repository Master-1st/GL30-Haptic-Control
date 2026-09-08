#ifndef GL30_AS5048A_H
#define GL30_AS5048A_H

#include <stdbool.h>
#include <stdint.h>

/* AS5048A DS000298 v1-11, SPI read-only. No OTP/programming entry points. */
#define AS5048A_REG_ERROR 0x0001u
#define AS5048A_REG_DIAG  0x3ffdu
#define AS5048A_REG_MAG   0x3ffeu
#define AS5048A_REG_ANGLE 0x3fffu
#define AS5048A_RESPONSE_PARITY 0x01u
#define AS5048A_RESPONSE_EF     0x02u
uint16_t as5048a_read_command(uint16_t address);
/* Keep host-detected parity failure separate from the sensor's latched EF. */
uint8_t as5048a_response_faults(uint16_t response);
bool as5048a_decode(uint16_t response, uint16_t *data);
bool as5048a_diagnostics_ok(uint16_t diagnostics);

#endif
