#include "factory_encoder.h"

#include <stddef.h>

static gl30_factory_encoder_sample_t g_factory_encoder_sample = {
    .angle_rad = 0.0f,
    .timestamp_us = 0u,
    .status = GL30_FACTORY_ENCODER_STATUS_PENDING_VENDOR,
    .valid = false,
    .sample_index = 0u,
};

void gl30_factory_encoder_init(void) {
  g_factory_encoder_sample = (gl30_factory_encoder_sample_t){
      .angle_rad = 0.0f,
      .timestamp_us = 0u,
      .status = GL30_FACTORY_ENCODER_STATUS_PENDING_VENDOR,
      .valid = false,
      .sample_index = 0u,
  };
}

void gl30_factory_encoder_snapshot(gl30_factory_encoder_sample_t *sample) {
  if (sample != NULL) {
    *sample = g_factory_encoder_sample;
  }
}

bool control_ready(const gl30_factory_encoder_sample_t *sample, uint64_t now_us,
                  uint64_t max_age_us) {
  if (sample == NULL || !sample->valid || sample->status != GL30_FACTORY_ENCODER_STATUS_READY ||
      sample->timestamp_us == 0u || now_us < sample->timestamp_us) {
    return false;
  }
  const uint64_t age_us = now_us - sample->timestamp_us;
  return age_us <= max_age_us;
}
