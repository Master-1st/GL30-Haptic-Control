#include "trace_buffer.h"

#include <math.h>
#include <limits.h>
#include <string.h>

static int16_t g_trace[4096][6];
static uint16_t g_cursor = 0u;
static uint32_t g_total_written = 0u;
static uint32_t g_overflow_count = 0u;

void gl30_trace_init(void) {
  memset(g_trace, 0, sizeof(g_trace));
  g_cursor = 0u;
  g_total_written = 0u;
  g_overflow_count = 0u;
}

void gl30_trace_push(const int16_t sample[6]) {
  for (uint16_t c = 0u; c < 6u; c++) {
    g_trace[g_cursor][c] = sample[c];
  }
  g_cursor = (uint16_t)((g_cursor + 1u) % 4096u);
  if (g_total_written < 4096u) {
    g_total_written += 1u;
  } else {
    g_overflow_count += 1u;
  }
}

gl30_trace_status_t gl30_trace_status(void) {
  return (gl30_trace_status_t){
    .rows = 4096u,
    .cols = 6u,
    .cursor = g_cursor,
    .total_written = g_total_written,
    .overflow_count = g_overflow_count
  };
}

void gl30_trace_read_all(int16_t *out, uint32_t out_count) {
  uint32_t used = g_total_written;
  if (used > 4096u) {
    used = 4096u;
  }
  if (out == NULL || out_count < used * 6u) {
    return;
  }

  uint16_t start = 0u;
  if (g_total_written >= 4096u) {
    start = g_cursor;
  }

  for (uint32_t i = 0u; i < used; i++) {
    uint16_t row = (start + (uint16_t)i) % 4096u;
    for (uint32_t c = 0u; c < 6u; c++) {
      out[i * 6u + c] = g_trace[row][c];
    }
  }
}

void gl30_trace_downsample20(int16_t *min_out, int16_t *max_out,
                            int16_t *mean_out, int16_t *rms_out,
                            uint16_t out_cap, uint16_t *bins_out) {
  const uint16_t window = 20u;
  uint32_t used = g_total_written;
  if (used > 4096u) {
    used = 4096u;
  }
  const uint16_t bins = (uint16_t)((used + window - 1u) / window);
  if (bins_out != NULL) {
    *bins_out = bins;
  }
  if (min_out == NULL || max_out == NULL || mean_out == NULL || rms_out == NULL || out_cap < bins) {
    return;
  }

  uint16_t start = (uint16_t)(g_total_written >= 4096u ? g_cursor : 0u);

  for (uint16_t b = 0u; b < bins; b++) {
    int count = 0;
    float acc[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    float acc_sq[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    for (uint16_t c = 0u; c < 6u; c++) {
        min_out[b * 6u + c] = INT16_MAX;
        max_out[b * 6u + c] = INT16_MIN;
    }

    for (uint16_t i = 0u; i < window; i++) {
      const uint32_t idx = (uint32_t)b * window + i;
      if (idx >= used) {
        break;
      }
      const uint16_t row = (uint16_t)((start + idx) % 4096u);
      for (uint16_t c = 0u; c < 6u; c++) {
        const int16_t v = g_trace[row][c];
        if (v < min_out[b * 6u + c]) {
          min_out[b * 6u + c] = v;
        }
        if (v > max_out[b * 6u + c]) {
          max_out[b * 6u + c] = v;
        }
        acc[c] += (float)v;
        acc_sq[c] += (float)v * (float)v;
      }
      count += 1;
    }

    for (uint16_t c = 0u; c < 6u; c++) {
      if (count > 0) {
        const float mean = acc[c] / (float)count;
        const float rms = sqrtf(acc_sq[c] / (float)count);
        mean_out[b * 6u + c] = (int16_t)llroundf(mean);
        rms_out[b * 6u + c] = (int16_t)llroundf(rms);
      } else {
        min_out[b * 6u + c] = 0;
        max_out[b * 6u + c] = 0;
        mean_out[b * 6u + c] = 0;
        rms_out[b * 6u + c] = 0;
      }
    }
  }
}
