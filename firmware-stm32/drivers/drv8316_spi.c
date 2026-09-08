#include "drv8316.h"

/* Transport-independent SPI framing shared by the product and TI EVM ports. */
uint8_t gl30_drv8316_even_parity_bit(uint16_t word_without_parity) {
  uint16_t word = word_without_parity & (uint16_t)~(1u << 8u);
  uint8_t parity = 0u;
  for (uint8_t bit = 0u; bit < 16u; ++bit) {
    parity ^= (uint8_t)((word >> bit) & 1u);
  }
  return parity;
}

uint16_t gl30_drv8316_make_frame(bool read, uint8_t address, uint8_t data) {
  uint16_t word = (uint16_t)((uint16_t)(read ? 1u : 0u) << 15u) |
                  (uint16_t)((uint16_t)(address & 0x3Fu) << 9u) |
                  (uint16_t)data;
  word |= (uint16_t)gl30_drv8316_even_parity_bit(word) << 8u;
  return word;
}

bool gl30_drv8316_word_has_even_parity(uint16_t word) {
  uint8_t parity = 0u;
  for (uint8_t bit = 0u; bit < 16u; ++bit) {
    parity ^= (uint8_t)((word >> bit) & 1u);
  }
  return parity == 0u;
}

uint32_t gl30_drv8316_status_word_faults(uint8_t address, uint16_t reply) {
  if (address > GL30_DRV8316_REG_STAT2) { return UINT32_MAX; }
  /* TI SLVSF16B Tables 8-10/8-13: SDO has no parity bit. NPOR is
   * active-low in both the status summary and the IC_Status register.
   * Normalize NPOR and exclude only reserved STAT2.7 from fault decisions.
   * Callers retain the unmodified reply for raw diagnostics. */
  const uint8_t summary = (uint8_t)(reply >> 8u) ^ 0x08u;
  uint8_t detail = (uint8_t)reply;
  if (address == GL30_DRV8316_REG_STAT0) { detail ^= 0x08u; }
  if (address == GL30_DRV8316_REG_STAT2) {
    detail &= GL30_DRV8316_STAT2_FAULT_MASK;
  }
  return (uint32_t)summary | ((uint32_t)detail << (address * 8u));
}
