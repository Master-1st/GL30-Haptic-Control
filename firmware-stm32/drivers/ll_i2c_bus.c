#include "ll_i2c_bus.h"

#if !GL30_BUILD_ONLY

#include <stddef.h>

#include "timebase.h"

static bool timed_out(uint64_t started_us) {
  const uint64_t timeout_us =
      (uint64_t)GL30_I2C_TRANSACTION_TIMEOUT_MS * 1000u;
  return gl30_timebase_now_us() - started_us >= timeout_us;
}

static bool had_error(I2C_TypeDef *i2c) {
  return LL_I2C_IsActiveFlag_NACK(i2c) != 0u ||
         LL_I2C_IsActiveFlag_BERR(i2c) != 0u ||
         LL_I2C_IsActiveFlag_ARLO(i2c) != 0u ||
         LL_I2C_IsActiveFlag_OVR(i2c) != 0u;
}

static void clear_status(I2C_TypeDef *i2c) {
  LL_I2C_ClearFlag_NACK(i2c);
  LL_I2C_ClearFlag_BERR(i2c);
  LL_I2C_ClearFlag_ARLO(i2c);
  LL_I2C_ClearFlag_OVR(i2c);
  LL_I2C_ClearFlag_STOP(i2c);
}

static void abort_transfer(I2C_TypeDef *i2c, uint64_t started_us) {
  if (LL_I2C_IsActiveFlag_BUSY(i2c) != 0u) {
    LL_I2C_GenerateStopCondition(i2c);
    while (LL_I2C_IsActiveFlag_STOP(i2c) == 0u && !timed_out(started_us)) {
      __NOP();
    }
  }
  clear_status(i2c);
}

static bool prepare_transfer(I2C_TypeDef *i2c, uint64_t started_us) {
  if (i2c == NULL || LL_I2C_IsEnabled(i2c) == 0u) {
    return false;
  }
  while (LL_I2C_IsActiveFlag_BUSY(i2c) != 0u) {
    if (timed_out(started_us)) {
      abort_transfer(i2c, started_us);
      return false;
    }
  }
  clear_status(i2c);
  return true;
}

static bool wait_txis(I2C_TypeDef *i2c, uint64_t started_us) {
  while (LL_I2C_IsActiveFlag_TXIS(i2c) == 0u) {
    if (had_error(i2c) || timed_out(started_us)) {
      abort_transfer(i2c, started_us);
      return false;
    }
  }
  return true;
}

static bool wait_rxne(I2C_TypeDef *i2c, uint64_t started_us) {
  while (LL_I2C_IsActiveFlag_RXNE(i2c) == 0u) {
    if (had_error(i2c) || timed_out(started_us)) {
      abort_transfer(i2c, started_us);
      return false;
    }
  }
  return true;
}

static bool wait_tc(I2C_TypeDef *i2c, uint64_t started_us) {
  while (LL_I2C_IsActiveFlag_TC(i2c) == 0u) {
    if (had_error(i2c) || LL_I2C_IsActiveFlag_STOP(i2c) != 0u ||
        timed_out(started_us)) {
      abort_transfer(i2c, started_us);
      return false;
    }
  }
  return true;
}

static bool wait_stop(I2C_TypeDef *i2c, uint64_t started_us) {
  while (LL_I2C_IsActiveFlag_STOP(i2c) == 0u) {
    if (had_error(i2c) || timed_out(started_us)) {
      abort_transfer(i2c, started_us);
      return false;
    }
  }
  LL_I2C_ClearFlag_STOP(i2c);
  return true;
}

bool gl30_ll_i2c_bus_mem_write_u8(I2C_TypeDef *i2c,
                                  uint8_t device_address_7bit,
                                  uint8_t reg,
                                  const uint8_t *data,
                                  uint8_t length) {
  uint8_t sent = 0u;
  const uint64_t started_us = gl30_timebase_now_us();

  if (data == NULL || length == 0u || device_address_7bit > 0x7Fu ||
      !prepare_transfer(i2c, started_us)) {
    return false;
  }

  LL_I2C_HandleTransfer(i2c, (uint32_t)device_address_7bit << 1u,
                        LL_I2C_ADDRSLAVE_7BIT, (uint32_t)length + 1u,
                        LL_I2C_MODE_AUTOEND, LL_I2C_GENERATE_START_WRITE);
  if (!wait_txis(i2c, started_us)) {
    return false;
  }
  LL_I2C_TransmitData8(i2c, reg);
  while (sent < length) {
    if (!wait_txis(i2c, started_us)) {
      return false;
    }
    LL_I2C_TransmitData8(i2c, data[sent]);
    sent++;
  }
  return wait_stop(i2c, started_us);
}

bool gl30_ll_i2c_bus_mem_read_u8(I2C_TypeDef *i2c,
                                 uint8_t device_address_7bit,
                                 uint8_t reg,
                                 uint8_t *data,
                                 uint8_t length) {
  uint8_t received = 0u;
  const uint64_t started_us = gl30_timebase_now_us();

  if (data == NULL || length == 0u || device_address_7bit > 0x7Fu ||
      !prepare_transfer(i2c, started_us)) {
    return false;
  }

  LL_I2C_HandleTransfer(i2c, (uint32_t)device_address_7bit << 1u,
                        LL_I2C_ADDRSLAVE_7BIT, 1u, LL_I2C_MODE_SOFTEND,
                        LL_I2C_GENERATE_START_WRITE);
  if (!wait_txis(i2c, started_us)) {
    return false;
  }
  LL_I2C_TransmitData8(i2c, reg);
  if (!wait_tc(i2c, started_us)) {
    return false;
  }

  LL_I2C_HandleTransfer(i2c, (uint32_t)device_address_7bit << 1u,
                        LL_I2C_ADDRSLAVE_7BIT, length, LL_I2C_MODE_AUTOEND,
                        LL_I2C_GENERATE_RESTART_7BIT_READ);
  while (received < length) {
    if (!wait_rxne(i2c, started_us)) {
      return false;
    }
    data[received] = LL_I2C_ReceiveData8(i2c);
    received++;
  }
  return wait_stop(i2c, started_us);
}

#endif
