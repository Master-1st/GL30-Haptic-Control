#include "gl30_ui_anim_model.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static float gl30_ui_clampf(float value, float min_value, float max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static bool gl30_ui_is_valid_number(float value) {
  return isfinite(value);
}

static float gl30_ui_smooth_step(float current, float target, uint32_t elapsed_ms, uint32_t tau_ms) {
  const float dt = (float)elapsed_ms;
  const float tau = (float)tau_ms;
  float alpha;
  if (tau <= 0.0001f) {
    return target;
  }
  if (dt <= 0.0f) {
    return current;
  }
  alpha = 1.0f - expf(-dt / tau);
  return current + (target - current) * alpha;
}

static uint32_t gl30_ui_clamp_ms(uint32_t elapsed_ms) {
  return elapsed_ms > GL30_UI_MAX_TICK_MS ? GL30_UI_MAX_TICK_MS : elapsed_ms;
}

static void gl30_ui_get_status_text(const gl30_ui_anim_input_t* input,
                                   char* out_status,
                                   size_t out_status_len) {
  if (input->off) {
    snprintf(out_status, out_status_len, "OFF");
  } else if (input->fault) {
    snprintf(out_status, out_status_len, "FAULT");
  } else if (!input->connected) {
    snprintf(out_status, out_status_len, "DISCONNECTED");
  } else if (input->mode == GL30_UI_MODE_TIMER && input->timer_running) {
    snprintf(out_status, out_status_len, "RUNNING");
  } else if (input->mode == GL30_UI_MODE_TIMER && input->timer_paused) {
    snprintf(out_status, out_status_len, "PAUSED");
  } else if (input->mode == GL30_UI_MODE_TIMER && input->timer_finished) {
    snprintf(out_status, out_status_len, "DONE");
  } else if (input->mode == GL30_UI_MODE_VOLUME && input->muted) {
    snprintf(out_status, out_status_len, "MUTED");
  } else {
    snprintf(out_status, out_status_len, "READY");
  }
}

static void gl30_ui_build_text(const gl30_ui_anim_input_t* input,
                              float output_ratio,
                              char* value_text,
                              size_t value_text_len,
                              char* mode_text,
                              size_t mode_text_len,
                              char* a11y_text,
                              size_t a11y_text_len) {
  if (input->mode == GL30_UI_MODE_TIMER) {
    const uint32_t ms = input->timer_running || input->timer_paused || input->timer_finished
        ? input->timer_remaining_ms : (uint32_t)(input->timer_minutes * 60000.0f);
    const uint32_t seconds = (ms + 999u) / 1000u;
    snprintf(value_text, value_text_len, "%02u:%02u", (unsigned)(seconds / 60u), (unsigned)(seconds % 60u));
    snprintf(mode_text, mode_text_len, "TIMER");
  } else {
    const float volume = output_ratio * GL30_UI_MODE_VOLUME_MAX;
    const int rounded_volume = (int)(volume + 0.5f);
    snprintf(value_text, value_text_len, "%d", rounded_volume);
    snprintf(mode_text, mode_text_len, "VOLUME");
  }

  char status[24];
  gl30_ui_get_status_text(input, status, sizeof(status));
  snprintf(a11y_text, a11y_text_len, "%s %s %s, %s",
           mode_text, value_text, input->mode == GL30_UI_MODE_TIMER ? "minutes:seconds" : "percent", status);
}

void gl30_ui_anim_model_init(gl30_ui_anim_model_t* model) {
  if (model == NULL) {
    return;
  }
  memset(model, 0, sizeof(*model));
  model->smoothed_ratio = GL30_UI_MODE_VOLUME_DEFAULT / GL30_UI_MODE_VOLUME_MAX;
  model->last_target = model->smoothed_ratio;
  model->wake_level = 0.0f;
  model->wake_hold_ms = 0u;
  model->last_mode = GL30_UI_MODE_VOLUME;
  model->initialized = true;
}

void gl30_ui_anim_model_tick(gl30_ui_anim_model_t* model,
                            const gl30_ui_anim_input_t* input,
                            uint32_t elapsed_ms,
                            gl30_ui_anim_output_t* output) {
  gl30_ui_anim_input_t safe;
  float target_ratio;
  float wake_target;
  uint32_t clamped_ms;

  if (model == NULL || output == NULL) {
    return;
  }
  if (!model->initialized) {
    gl30_ui_anim_model_init(model);
  }

  if (input == NULL) {
    memset(&safe, 0, sizeof(safe));
    safe.volume = GL30_UI_MODE_VOLUME_DEFAULT;
    safe.timer_minutes = GL30_UI_TIMER_DEFAULT_MINUTES;
    safe.mode = GL30_UI_MODE_VOLUME;
    safe.connected = false;
  } else {
    safe = *input;
  }

  if (safe.mode != GL30_UI_MODE_VOLUME && safe.mode != GL30_UI_MODE_TIMER) {
    safe.mode = model->last_mode;
  }

  if (!gl30_ui_is_valid_number(safe.volume)) {
    safe.volume = GL30_UI_MODE_VOLUME_DEFAULT;
  }
  if (!gl30_ui_is_valid_number(safe.timer_minutes)) {
    safe.timer_minutes = GL30_UI_TIMER_DEFAULT_MINUTES;
  }
  safe.volume = gl30_ui_clampf(safe.volume, GL30_UI_MODE_VOLUME_MIN, GL30_UI_MODE_VOLUME_MAX);
  safe.timer_minutes = gl30_ui_clampf(safe.timer_minutes, GL30_UI_TIMER_MIN_MINUTES, GL30_UI_TIMER_MAX_MINUTES);
  if (safe.timer_remaining_ms > 7200000u) safe.timer_remaining_ms = 7200000u;
  clamped_ms = gl30_ui_clamp_ms(elapsed_ms);

  if (safe.mode == GL30_UI_MODE_VOLUME) {
    target_ratio = safe.muted ? 0.0f : safe.volume / GL30_UI_MODE_VOLUME_MAX;
  } else {
    target_ratio = (safe.timer_running || safe.timer_paused || safe.timer_finished)
        ? (safe.timer_minutes > 0.0f ? safe.timer_remaining_ms / (safe.timer_minutes * 60000.0f) : 0.0f)
        : safe.timer_minutes / GL30_UI_TIMER_MAX_MINUTES;
  }

  if (!isfinite(target_ratio)) {
    target_ratio = 0.0f;
  }
  target_ratio = gl30_ui_clampf(target_ratio, 0.0f, 1.0f);
  const bool mode_changed = safe.mode != model->last_mode;
  if (mode_changed) {
    model->transition = 1.0f;
    model->smoothed_ratio = target_ratio; /* Never morph unrelated units. */
  }
  if (target_ratio != model->last_target) {
    model->direction = target_ratio > model->last_target ? 1 : -1;
    model->activity = gl30_ui_clampf(model->activity + fabsf(target_ratio - model->last_target) * 12.0f, 0.0f, 1.0f);
  }
  model->last_target = target_ratio;
  model->activity = gl30_ui_smooth_step(model->activity, 0.0f, clamped_ms, 320u);
  model->transition = gl30_ui_smooth_step(model->transition, 0.0f, clamped_ms, 90u);
  if (safe.mode == GL30_UI_MODE_TIMER) {
    /* Actual countdown seconds, frozen while paused; independent of render cadence. */
    model->phase = (float)((7200000u - safe.timer_remaining_ms) % 60000u) * 0.000104719755f;
  } else {
    model->phase = fmodf(model->phase + (float)clamped_ms * 0.0062831853f, 6.2831853f);
  }

  model->smoothed_ratio = gl30_ui_smooth_step(model->smoothed_ratio, target_ratio, clamped_ms, GL30_UI_VALUE_SMOOTH_MS);
  model->smoothed_ratio = gl30_ui_clampf(model->smoothed_ratio, 0.0f, 1.0f);

  if (safe.off) {
    wake_target = 0.0f;
    model->wake_hold_ms = 0u;
  } else if (safe.wake_signal) {
    wake_target = 1.0f;
    model->wake_hold_ms = GL30_UI_WAKE_HOLD_MS;
  } else if (model->wake_hold_ms > 0u) {
    if (model->wake_hold_ms > clamped_ms) {
      model->wake_hold_ms -= clamped_ms;
      wake_target = 1.0f;
    } else {
      model->wake_hold_ms = 0u;
      wake_target = 0.0f;
    }
  } else {
    wake_target = 0.0f;
  }

  model->wake_level = gl30_ui_smooth_step(model->wake_level, wake_target, clamped_ms, GL30_UI_WAKE_SMOOTH_MS);
  model->wake_level = gl30_ui_clampf(model->wake_level, 0.0f, 1.0f);

  if (safe.mode == GL30_UI_MODE_VOLUME) {
    model->last_mode = GL30_UI_MODE_VOLUME;
  } else if (safe.mode == GL30_UI_MODE_TIMER) {
    model->last_mode = GL30_UI_MODE_TIMER;
  }

  memset(output, 0, sizeof(*output));
  output->mode = safe.mode;
  output->off = safe.off;
  output->lost_connection = !safe.connected;
  output->fault = safe.fault;
  output->muted = safe.muted;
  output->timer_running = safe.timer_running;
  output->timer_paused = safe.timer_paused;
  output->timer_finished = safe.timer_finished;
  output->timer_fill = safe.timer_minutes <= 0.0f ? 0.0f :
      (safe.timer_running || safe.timer_paused || safe.timer_finished
          ? gl30_ui_clampf(safe.timer_remaining_ms / (safe.timer_minutes * 60000.0f), 0.0f, 1.0f)
          : 1.0f);
  if (!safe.timer_finished || !model->was_finished) model->completion_ms = 0u;
  else if (model->completion_ms < 900u) {
    model->completion_ms += clamped_ms;
    if (model->completion_ms > 900u) model->completion_ms = 900u;
  }
  model->was_finished = safe.timer_finished;
  output->completion = model->completion_ms / 900.0f;
  output->activity = safe.off || safe.fault || !safe.connected ? 0.0f : model->activity;
  output->phase = model->phase;
  output->transition = model->transition;
  output->direction = model->direction;

  output->value_ratio = safe.off ? 0.0f : model->smoothed_ratio;
  output->dial_angle_deg = GL30_UI_DIAL_MIN_ANGLE_DEG +
                           GL30_UI_DIAL_SPAN_ANGLE_DEG * output->value_ratio;
  output->wake_level = safe.off ? 0.0f : model->wake_level;
  if (output->wake_level < 0.01f) {
    output->wake_level = 0.0f;
  } else if (output->wake_level > 0.999f) {
    output->wake_level = 1.0f;
  }

  gl30_ui_get_status_text(&safe, output->status_text, sizeof(output->status_text));
  gl30_ui_build_text(&safe,
                     output->value_ratio,
                     output->value_text,
                     sizeof(output->value_text),
                     output->mode_text,
                     sizeof(output->mode_text),
                     output->accessibility_text,
                     sizeof(output->accessibility_text));
}
