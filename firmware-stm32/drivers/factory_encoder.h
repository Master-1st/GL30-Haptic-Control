#ifndef GL30_FACTORY_ENCODER_H_
#define GL30_FACTORY_ENCODER_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  GL30_FACTORY_ENCODER_STATUS_INITIALIZING = 0u,
  GL30_FACTORY_ENCODER_STATUS_READY = 1u,
  GL30_FACTORY_ENCODER_STATUS_INVALID = 2u,
} gl30_factory_encoder_status_t;

#define GL30_ENCODER_FAULT_TRANSPORT 1u
#define GL30_ENCODER_FAULT_TIMEOUT   2u
#define GL30_ENCODER_FAULT_PARITY    4u
#define GL30_ENCODER_FAULT_SENSOR    8u
#define GL30_ENCODER_FAULT_FIELD     16u

typedef struct {
  float angle_rad;
  uint64_t timestamp_us;
  gl30_factory_encoder_status_t status;
  bool valid;
  uint32_t sample_index;
} gl30_factory_encoder_sample_t;

typedef struct {
  uint32_t latest_faults;
  uint32_t successful_samples;
  uint32_t failed_samples;
  uint32_t parity_errors;
  uint32_t transport_errors;
  uint32_t sensor_errors;
  uint32_t field_errors;
  uint32_t last_duration_us;
  uint16_t raw_angle;
  uint16_t raw_diagnostics;
} gl30_factory_encoder_diagnostics_t;

void gl30_factory_encoder_init(void);
void gl30_factory_encoder_poll_4k(void);
/* Must be non-blocking and ISR-safe: copy the latest completed sample only. */
void gl30_factory_encoder_snapshot(gl30_factory_encoder_sample_t *sample);
void gl30_factory_encoder_diagnostics_snapshot(
    gl30_factory_encoder_diagnostics_t *out);

bool control_ready(const gl30_factory_encoder_sample_t *sample, uint64_t now_us,
                  uint64_t max_age_us);

#endif
