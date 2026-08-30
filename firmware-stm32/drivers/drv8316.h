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

typedef struct {
  uint8_t stat0;
  uint8_t stat1;
  uint8_t stat2;
  uint8_t spi_status;
  bool n_fault_released;
  bool configured;
  bool output_enabled;
} gl30_drv8316_status_t;

/* Pure helpers, shared by embedded code and host unit tests. */
uint8_t gl30_drv8316_even_parity_bit(uint16_t word_without_parity);
uint16_t gl30_drv8316_make_frame(bool read, uint8_t address, uint8_t data);
bool gl30_drv8316_word_has_even_parity(uint16_t word);

void gl30_drv8316_init(void);
bool gl30_drv8316_configure(void);
bool gl30_drv8316_clear_faults(void);
bool gl30_drv8316_arm(uint32_t expected_off_generation);
void gl30_drv8316_safe_off(void);
void gl30_drv8316_set_duty(float duty_a, float duty_b, float duty_c);
bool gl30_drv8316_read_register(uint8_t address, uint8_t *data);
bool gl30_drv8316_read_faults(gl30_drv8316_status_t *out);
bool gl30_drv8316_is_configured(void);
bool gl30_drv8316_outputs_enabled(void);
uint32_t gl30_drv8316_off_generation_snapshot(void);

#endif
