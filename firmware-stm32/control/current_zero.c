#include "current_zero.h"

#include <stddef.h>
#include <string.h>

#include "board_config.h"

void gl30_current_zero_reset(gl30_current_zero_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  memset(ctx, 0, sizeof(*ctx));
  for (size_t channel = 0u; channel < 3u; ++channel) {
    ctx->offset[channel] = GL30_ADC_ZERO_DEFAULT_COUNTS;
  }
  ctx->state = GL30_CURRENT_ZERO_WAITING;
}

void gl30_current_zero_update(gl30_current_zero_t *ctx, uint64_t now_us,
                              bool analog_ready, bool bridge_off,
                              const uint16_t raw[3]) {
  if (ctx == NULL) {
    return;
  }
  if (!analog_ready) {
    gl30_current_zero_reset(ctx);
    return;
  }
  if (ctx->state == GL30_CURRENT_ZERO_READY ||
      ctx->state == GL30_CURRENT_ZERO_FAILED) {
    return;
  }
  if (ctx->state == GL30_CURRENT_ZERO_WAITING) {
    if (bridge_off) {
      ctx->state = GL30_CURRENT_ZERO_SETTLING;
      ctx->started_us = now_us;
      ctx->last_sample_us = now_us;
    }
    return;
  }

  if (ctx->state != GL30_CURRENT_ZERO_SETTLING &&
      ctx->state != GL30_CURRENT_ZERO_COLLECTING) {
    ctx->state = GL30_CURRENT_ZERO_FAILED;
    return;
  }
  if (!bridge_off || now_us < ctx->started_us ||
      now_us < ctx->last_sample_us) {
    ctx->state = GL30_CURRENT_ZERO_FAILED;
    return;
  }

  const uint64_t elapsed_us = now_us - ctx->started_us;
  if (elapsed_us >= (uint64_t)GL30_ADC_ZERO_TIMEOUT_US) {
    ctx->state = GL30_CURRENT_ZERO_FAILED;
    return;
  }
  if (elapsed_us < (uint64_t)GL30_ADC_ZERO_SETTLE_US) {
    if (ctx->count == 0u) {
      ctx->last_sample_us = now_us;
    }
    return;
  }
  ctx->state = GL30_CURRENT_ZERO_COLLECTING;
  if (raw == NULL) {
    if (ctx->count == 0u) {
      ctx->last_sample_us = now_us;
    }
    return;
  }
  if (ctx->count > 0u && now_us == ctx->last_sample_us) {
    return;
  }

  for (size_t channel = 0u; channel < 3u; ++channel) {
    if ((float)raw[channel] > GL30_ADC_FULL_SCALE_COUNTS) {
      ctx->state = GL30_CURRENT_ZERO_FAILED;
      return;
    }
  }

  for (size_t channel = 0u; channel < 3u; ++channel) {
    if (ctx->count == 0u) {
      ctx->min[channel] = raw[channel];
      ctx->max[channel] = raw[channel];
    } else {
      if (raw[channel] < ctx->min[channel]) {
        ctx->min[channel] = raw[channel];
      }
      if (raw[channel] > ctx->max[channel]) {
        ctx->max[channel] = raw[channel];
      }
    }
    ctx->sum[channel] += raw[channel];
  }
  ++ctx->count;
  ctx->last_sample_us = now_us;
  if (ctx->count < GL30_ADC_ZERO_CAL_SAMPLES) {
    return;
  }

  float offsets[3];
  for (size_t channel = 0u; channel < 3u; ++channel) {
    offsets[channel] =
        (float)ctx->sum[channel] / (float)GL30_ADC_ZERO_CAL_SAMPLES;
    const float low =
        GL30_ADC_ZERO_DEFAULT_COUNTS - GL30_ADC_ZERO_MAX_ERROR_COUNTS;
    const float high =
        GL30_ADC_ZERO_DEFAULT_COUNTS + GL30_ADC_ZERO_MAX_ERROR_COUNTS;
    if (offsets[channel] < low || offsets[channel] > high ||
        (uint32_t)(ctx->max[channel] - ctx->min[channel]) >
            GL30_ADC_ZERO_MAX_SPAN_COUNTS) {
      ctx->state = GL30_CURRENT_ZERO_FAILED;
      return;
    }
  }

  for (size_t channel = 0u; channel < 3u; ++channel) {
    ctx->offset[channel] = offsets[channel];
  }
  ctx->state = GL30_CURRENT_ZERO_READY;
}
