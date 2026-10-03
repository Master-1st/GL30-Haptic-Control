#ifndef GL30_UI_ANIM_MODEL_H_
#define GL30_UI_ANIM_MODEL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define GL30_UI_MODE_VOLUME_MIN 0.0f
#define GL30_UI_MODE_VOLUME_MAX 100.0f
#define GL30_UI_MODE_VOLUME_DEFAULT 42.0f

#define GL30_UI_TIMER_MIN_MINUTES 0.0f
#define GL30_UI_TIMER_MAX_MINUTES 120.0f
#define GL30_UI_TIMER_DEFAULT_MINUTES 15.0f

#define GL30_UI_VALUE_SMOOTH_MS 90u
#define GL30_UI_WAKE_SMOOTH_MS 240u
#define GL30_UI_WAKE_HOLD_MS 1300u
#define GL30_UI_MAX_TICK_MS 200u

#define GL30_UI_DIAL_MIN_ANGLE_DEG 135.0f
#define GL30_UI_DIAL_SPAN_ANGLE_DEG 270.0f

typedef enum {
  GL30_UI_MODE_VOLUME = 0u,
  GL30_UI_MODE_TIMER = 1u
} gl30_ui_mode_t;

typedef struct {
  float volume;
  float timer_minutes;
  bool timer_running;
  bool timer_paused;
  bool timer_finished;
  uint32_t timer_remaining_ms;
  bool muted;
  gl30_ui_mode_t mode;
  bool wake_signal;
  bool off;
  bool connected;
  bool fault;
} gl30_ui_anim_input_t;

typedef struct {
  bool off;
  bool lost_connection;
  bool fault;
  gl30_ui_mode_t mode;
  float value_ratio;
  float dial_angle_deg;
  float wake_level;
  bool muted;
  bool timer_running;
  bool timer_paused;
  bool timer_finished;
  float activity;
  float phase;
  float transition;
  int direction;
  char value_text[20];
  float timer_fill; /* Actual remaining fraction; ready hourglass starts full. */
  float completion; /* One-shot finish animation, 0..1 over 900 ms. */
  char mode_text[12];
  char status_text[24];
  char accessibility_text[64];
} gl30_ui_anim_output_t;

typedef struct {
  float smoothed_ratio;
  float wake_level;
  uint32_t wake_hold_ms;
  gl30_ui_mode_t last_mode;
  bool initialized;
  float last_target;
  float activity;
  float phase;
  float transition;
  int direction;
  uint32_t completion_ms;
  bool was_finished;
} gl30_ui_anim_model_t;

void gl30_ui_anim_model_init(gl30_ui_anim_model_t* model);

void gl30_ui_anim_model_tick(gl30_ui_anim_model_t* model,
                            const gl30_ui_anim_input_t* input,
                            uint32_t elapsed_ms,
                            gl30_ui_anim_output_t* output);

#ifdef __cplusplus
}
#endif

#endif
