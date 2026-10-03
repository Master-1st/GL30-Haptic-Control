#ifndef GL30_CURRENT_ZERO_H_
#define GL30_CURRENT_ZERO_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  GL30_CURRENT_ZERO_WAITING = 0,
  GL30_CURRENT_ZERO_SETTLING,
  GL30_CURRENT_ZERO_COLLECTING,
  GL30_CURRENT_ZERO_READY,
  GL30_CURRENT_ZERO_FAILED
} gl30_current_zero_state_t;

typedef struct {
  gl30_current_zero_state_t state;
  uint64_t started_us;
  uint64_t last_sample_us;
  uint32_t count;
  uint32_t sum[3];
  uint16_t min[3];
  uint16_t max[3];
  float offset[3];
} gl30_current_zero_t;

void gl30_current_zero_reset(gl30_current_zero_t *ctx);
void gl30_current_zero_update(gl30_current_zero_t *ctx, uint64_t now_us,
                              bool analog_ready, bool bridge_off,
                              const uint16_t raw[3]);

#endif
