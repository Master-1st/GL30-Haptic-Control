#ifndef GL30_DRV8316_H_
#define GL30_DRV8316_H_

#include <stdbool.h>
#include <stdint.h>

enum {
  GL30_DRV8316_REG_STAT0 = 0x00u,
  GL30_DRV8316_REG_STAT1 = 0x01u,
  GL30_DRV8316_REG_STAT2 = 0x02u,
  GL30_DRV8316_REG_CTRL1 = 0x03u,
  GL30_DRV8316_REG_CTRL2 = 0x04u,
  GL30_DRV8316_REG_CTRL3 = 0x05u,
  GL30_DRV8316_REG_CTRL4 = 0x06u,
  GL30_DRV8316_REG_CTRL5 = 0x07u,
  GL30_DRV8316_REG_CTRL6 = 0x08u,
  GL30_DRV8316_REG_CTRL10 = 0x0Cu
};

/* TI SLVSF16B Table 8-15: STAT2 bit 7 is reserved, not a fault flag. */
#define GL30_DRV8316_STAT2_FAULT_MASK 0x7Fu

typedef struct {
  uint8_t stat0;
  uint8_t stat1;
  uint8_t stat2;
  uint8_t spi_status;
  /* OR of all three normalized replies in this read, including each SDO
   * summary. Raw bytes above remain unchanged. App safety owns fault latching. */
  uint32_t normalized_faults;
  bool n_fault_released;
  bool configured;
  bool output_enabled;
} gl30_drv8316_status_t;

/* Pure helpers, shared by embedded code and host unit tests. */
uint8_t gl30_drv8316_even_parity_bit(uint16_t word_without_parity);
uint16_t gl30_drv8316_make_frame(bool read, uint8_t address, uint8_t data);
bool gl30_drv8316_word_has_even_parity(uint16_t word);
uint32_t gl30_drv8316_status_word_faults(uint8_t address, uint16_t reply);

void gl30_drv8316_init(void);
/* Foreground-only: rejects a canceled startup before any SPI transaction. */
bool gl30_drv8316_configure(uint32_t requested_off_generation);
/* Foreground-only startup check; verifies cold safe state and never enables MOE. */
bool gl30_drv8316_verify_startup(uint32_t expected_off_generation);
bool gl30_drv8316_startup_verified(void);
/* Foreground-only, explicit recovery operation; arm never calls this. */
bool gl30_drv8316_clear_faults(void);
bool gl30_drv8316_arm(uint32_t expected_off_generation);
/* ISR-safe invalidation before external driver power is removed. */
void gl30_drv8316_invalidate_configuration(void);
/* ISR-safe; ordinary safe-off preserves a completed register configuration. */
void gl30_drv8316_safe_off(void);
void gl30_drv8316_set_duty(float duty_a, float duty_b, float duty_c);
bool gl30_drv8316_read_register(uint8_t address, uint8_t *data);
bool gl30_drv8316_read_faults(gl30_drv8316_status_t *out);
bool gl30_drv8316_is_configured(void);
bool gl30_drv8316_outputs_enabled(void);
uint32_t gl30_drv8316_off_generation_snapshot(void);

#endif
