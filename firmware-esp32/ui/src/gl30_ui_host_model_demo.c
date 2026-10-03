#include <stdbool.h>
#include <stdio.h>

#include "gl30_ui_anim_model.h"

int main(void) {
  gl30_ui_anim_model_t model;
  gl30_ui_anim_input_t input = {0};
  gl30_ui_anim_output_t output;

  gl30_ui_anim_model_init(&model);

  input.volume = GL30_UI_MODE_VOLUME_DEFAULT;
  input.timer_minutes = GL30_UI_TIMER_DEFAULT_MINUTES;
  input.timer_running = false;
  input.mode = GL30_UI_MODE_VOLUME;
  input.wake_signal = true;
  input.off = false;
  input.connected = true;
  input.fault = false;

  gl30_ui_anim_model_tick(&model, &input, 50u, &output);
  printf("mode=%u value_ratio=%.4f wake=%.4f value=%s status=%s\n",
         (unsigned)output.mode,
         (double)output.value_ratio,
         (double)output.wake_level,
         output.value_text,
         output.status_text);

  input.mode = GL30_UI_MODE_TIMER;
  input.timer_minutes = 25.0f;
  input.wake_signal = false;
  gl30_ui_anim_model_tick(&model, &input, 80u, &output);
  printf("mode=%u value_ratio=%.4f wake=%.4f value=%s status=%s\n",
         (unsigned)output.mode,
         (double)output.value_ratio,
         (double)output.wake_level,
         output.value_text,
         output.status_text);

  return 0;
}
