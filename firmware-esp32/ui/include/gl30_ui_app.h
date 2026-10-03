#ifndef GL30_UI_APP_H
#define GL30_UI_APP_H
#include "gl30_ui_anim_model.h"

typedef enum {
  GL30_UI_ACTION_PRIMARY, GL30_UI_ACTION_RESET, GL30_UI_ACTION_POWER,
  GL30_UI_ACTION_VOLUME, GL30_UI_ACTION_TIMER
} gl30_ui_action_t;

/* Single UI-task owner. No GPIO, transport or motor permission in this layer. */
typedef struct {
  gl30_ui_anim_input_t input;
  gl30_ui_anim_model_t animation;
  uint64_t last_ms;
  bool clock_valid;
  bool angle_valid;
  float last_angle;
  float angle_remainder;
} gl30_ui_app_t;

void gl30_ui_app_init(gl30_ui_app_t *app);
void gl30_ui_app_action(gl30_ui_app_t *app, gl30_ui_action_t action);
void gl30_ui_app_adjust(gl30_ui_app_t *app, int steps);
void gl30_ui_app_observe(gl30_ui_app_t *app, float angle_rad, bool connected, bool fault);
void gl30_ui_app_tick(gl30_ui_app_t *app, uint64_t now_ms, gl30_ui_anim_output_t *output);
#endif
