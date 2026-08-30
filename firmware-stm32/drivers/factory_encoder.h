#ifndef GL30_FACTORY_ENCODER_H_
#define GL30_FACTORY_ENCODER_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  GL30_FACTORY_ENCODER_STATUS_PENDING_VENDOR = 0u,
  GL30_FACTORY_ENCODER_STATUS_READY = 1u,
  GL30_FACTORY_ENCODER_STATUS_INVALID = 2u,
} gl30_factory_encoder_status_t;

typedef struct {
  float angle_rad;
  uint64_t timestamp_us;
  gl30_factory_encoder_status_t status;
  bool valid;
  uint32_t sample_index;
} gl30_factory_encoder_sample_t;

void gl30_factory_encoder_init(void);
/* Must be non-blocking and ISR-safe: copy the latest completed sample only. */
void gl30_factory_encoder_snapshot(gl30_factory_encoder_sample_t *sample);

bool control_ready(const gl30_factory_encoder_sample_t *sample, uint64_t now_us,
                  uint64_t max_age_us);

#endif
