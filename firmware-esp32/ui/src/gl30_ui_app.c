#include "gl30_ui_app.h"
#include <math.h>
#include <string.h>

void gl30_ui_app_init(gl30_ui_app_t *app) {
  if (!app) return;
  memset(app, 0, sizeof(*app));
  gl30_ui_anim_model_init(&app->animation);
  app->input.volume = 42.0f;
  app->input.timer_minutes = 15.0f;
  app->input.timer_remaining_ms = 900000u;
}

void gl30_ui_app_adjust(gl30_ui_app_t *app, int steps) {
  if (!app || steps == 0 || app->input.fault) return;
  app->input.off = false;
  app->input.wake_signal = true;
  if (app->input.mode == GL30_UI_MODE_TIMER) {
    /* Running/paused countdown is not silently edited by accidental rotation. */
    if (app->input.timer_running || app->input.timer_paused) return;
    double value = (double)app->input.timer_minutes + steps;
    app->input.timer_minutes = (float)fmin(120.0, fmax(0.0, value));
    app->input.timer_remaining_ms = (uint32_t)(app->input.timer_minutes * 60000.0f);
    app->input.timer_finished = false;
  } else {
    double value = (double)app->input.volume + steps;
    app->input.volume = (float)fmin(100.0, fmax(0.0, value));
    app->input.muted = false;
  }
}

void gl30_ui_app_action(gl30_ui_app_t *app, gl30_ui_action_t action) {
  if (!app) return;
  if (action == GL30_UI_ACTION_POWER) {
    app->input.off = !app->input.off;
    app->input.wake_signal = !app->input.off;
    return;
  }
  if (app->input.off) {
    app->input.off = false; /* First touch wakes only. */
    app->input.wake_signal = true;
    return;
  }
  if (app->input.fault) return;
  app->input.wake_signal = true;
  if (action == GL30_UI_ACTION_VOLUME || action == GL30_UI_ACTION_TIMER) {
    app->input.mode = action == GL30_UI_ACTION_VOLUME ? GL30_UI_MODE_VOLUME : GL30_UI_MODE_TIMER;
    app->angle_remainder = 0.0f;
  } else if (action == GL30_UI_ACTION_RESET) {
    app->input.timer_running = app->input.timer_paused = app->input.timer_finished = false;
    app->input.timer_remaining_ms = (uint32_t)(app->input.timer_minutes * 60000.0f);
  } else if (action == GL30_UI_ACTION_PRIMARY) {
    if (app->input.mode == GL30_UI_MODE_VOLUME) app->input.muted = !app->input.muted;
    else if (app->input.timer_running) {
      app->input.timer_running = false;
      app->input.timer_paused = true;
    } else {
      if (!app->input.timer_paused)
        app->input.timer_remaining_ms = (uint32_t)(app->input.timer_minutes * 60000.0f);
      app->input.timer_running = app->input.timer_remaining_ms > 0u;
      app->input.timer_paused = app->input.timer_finished = false;
    }
  }
}

void gl30_ui_app_observe(gl30_ui_app_t *app, float angle_rad, bool connected, bool fault) {
  if (!app) return;
  app->input.connected = connected;
  app->input.fault = fault;
  if (!connected || fault || !isfinite(angle_rad)) {
    app->angle_valid = false;
    app->angle_remainder = 0.0f;
    return;
  }
  if (app->angle_valid) {
    const float delta = angle_rad - app->last_angle;
    /* Display mapping: 6 degrees per unit, continuous angle (not logicalPosition).
       H26 logicalPosition has mode-dependent units and must not be guessed here. */
    if (isfinite(delta) && fabsf(delta) <= 3.1415927f) {
      const float step = 0.104719755f;
      app->angle_remainder += delta;
      int steps = (int)(app->angle_remainder / step);
      app->angle_remainder -= (float)steps * step;
      gl30_ui_app_adjust(app, steps);
    } else app->angle_remainder = 0.0f; /* Discontinuity/reboot: rebase, no jump. */
  }
  app->last_angle = angle_rad;
  app->angle_valid = true;
}

void gl30_ui_app_tick(gl30_ui_app_t *app, uint64_t now_ms, gl30_ui_anim_output_t *output) {
  if (!app || !output) return;
  uint64_t elapsed = app->clock_valid && now_ms >= app->last_ms ? now_ms - app->last_ms : 0;
  /* Keep the high-water mark: a clock rollback must not be charged twice. */
  if (!app->clock_valid || now_ms > app->last_ms) app->last_ms = now_ms;
  app->clock_valid = true;
  /* Countdown uses elapsed monotonic time, not animation's 200 ms clamp. */
  if (app->input.timer_running) {
    if (elapsed >= app->input.timer_remaining_ms) {
      app->input.timer_remaining_ms = 0;
      app->input.timer_running = false;
      app->input.timer_finished = true;
      app->input.off = false;
      app->input.wake_signal = true;
    } else app->input.timer_remaining_ms -= (uint32_t)elapsed;
  }
  gl30_ui_anim_model_tick(&app->animation, &app->input,
      elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed, output);
  app->input.wake_signal = false;
}
