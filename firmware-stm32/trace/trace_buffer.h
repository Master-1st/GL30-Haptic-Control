#ifndef GL30_TRACE_BUFFER_H_
#define GL30_TRACE_BUFFER_H_

#include <stdint.h>

typedef struct {
  uint16_t rows;
  uint16_t cols;
  uint16_t cursor;
  uint32_t total_written;
  uint32_t overflow_count;
} gl30_trace_status_t;

typedef struct {
  uint16_t bins;
  float min;
  float max;
  float mean;
  float rms;
} gl30_trace_summary_scalar_t;

void gl30_trace_init(void);
void gl30_trace_push(const int16_t sample[6]);
gl30_trace_status_t gl30_trace_status(void);
void gl30_trace_read_all(int16_t *out, uint32_t out_count);
void gl30_trace_downsample20(int16_t *min_out, int16_t *max_out,
                            int16_t *mean_out, int16_t *rms_out,
                            uint16_t out_cap, uint16_t *bins_out);

#endif
