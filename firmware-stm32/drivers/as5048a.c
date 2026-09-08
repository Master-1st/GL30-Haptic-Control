#include "as5048a.h"
#include <stddef.h>

static uint16_t parity(uint16_t word)
{
  word ^= word >> 8;
  word ^= word >> 4;
  word ^= word >> 2;
  word ^= word >> 1;
  return word & 1u;
}

uint16_t as5048a_read_command(uint16_t address)
{
  const uint16_t word = (address & 0x3fffu) | 0x4000u;
  return word | (uint16_t)(parity(word) << 15);
}

uint8_t as5048a_response_faults(uint16_t response)
{
  return (uint8_t)((parity(response) ? AS5048A_RESPONSE_PARITY : 0u) |
                  ((response & 0x4000u) ? AS5048A_RESPONSE_EF : 0u));
}

bool as5048a_decode(uint16_t response, uint16_t *data)
{
  if (data == NULL || as5048a_response_faults(response) != 0u) {
    return false;
  }
  *data = response & 0x3fffu;
  return true;
}

bool as5048a_diagnostics_ok(uint16_t diagnostics)
{
  /* OCF=1, COF/COMP_LOW/COMP_HIGH=0; reject a bad magnetic field at bring-up. */
  return (diagnostics & 0x0f00u) == 0x0100u;
}
