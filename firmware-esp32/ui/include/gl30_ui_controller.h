#ifndef GL30_UI_CONTROLLER_H
#define GL30_UI_CONTROLLER_H
#include "gl30_ui_app.h"
#include "gl30_esp_link.h"

/* All calls on one UI owner task. UART task queues bytes; it does not touch LVGL.
   No transmit/enable action is implicit in this display controller. */
typedef struct {
  gl30_ui_app_t app;
  gl30_esp_link_t link;
} gl30_ui_controller_t;
void gl30_ui_controller_init(gl30_ui_controller_t* controller);
int gl30_ui_controller_receive(gl30_ui_controller_t* controller, const uint8_t* bytes,
                               size_t length, uint64_t now_us);
void gl30_ui_controller_tick(gl30_ui_controller_t* controller, uint64_t now_us,
                             gl30_ui_anim_output_t* output);
void gl30_ui_controller_action(gl30_ui_controller_t* controller, uint64_t now_us,
                               gl30_ui_action_t action);
#endif
