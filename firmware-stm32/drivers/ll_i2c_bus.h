#ifndef GL30_LL_I2C_BUS_H_
#define GL30_LL_I2C_BUS_H_

#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"

#if !GL30_BUILD_ONLY

#include "stm32g4xx_ll_i2c.h"

bool gl30_ll_i2c_bus_mem_read_u8(I2C_TypeDef *i2c,
                                 uint8_t device_address_7bit,
                                 uint8_t reg,
                                 uint8_t *data,
                                 uint8_t length);
bool gl30_ll_i2c_bus_mem_write_u8(I2C_TypeDef *i2c,
                                  uint8_t device_address_7bit,
                                  uint8_t reg,
                                  const uint8_t *data,
                                  uint8_t length);

#endif

#endif
