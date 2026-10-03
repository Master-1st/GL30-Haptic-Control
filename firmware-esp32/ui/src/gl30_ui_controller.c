#include "gl30_ui_controller.h"

void gl30_ui_controller_init(gl30_ui_controller_t* c) {
  if (!c) return;
  gl30_ui_app_init(&c->app);
  gl30_esp_link_init(&c->link);
}

int gl30_ui_controller_receive(gl30_ui_controller_t* c, const uint8_t* bytes,
                               size_t length, uint64_t now_us) {
  if (!c) return -1;
  /* Advance time before adopting input or changing mode. The timer and gesture
     share one monotonic timebase, including frames received between renders. */
  gl30_ui_anim_output_t ignored;
  gl30_ui_controller_tick(c, now_us, &ignored);
  const int result = gl30_esp_link_feed(&c->link, bytes, length, now_us);
  gl30_ui_app_observe(&c->app, c->link.latest_fast.angleRad,
      gl30_esp_link_connected(&c->link, now_us),
      c->link.has_fast && c->link.latest_fast.faultBits != 0u);
  return result;
}

void gl30_ui_controller_tick(gl30_ui_controller_t* c, uint64_t now_us,
                             gl30_ui_anim_output_t* output) {
  if (!c || !output) return;
  gl30_ui_app_observe(&c->app, c->link.latest_fast.angleRad,
      gl30_esp_link_connected(&c->link, now_us),
      c->link.has_fast && c->link.latest_fast.faultBits != 0u);
  gl30_ui_app_tick(&c->app, now_us / 1000u, output);
}

void gl30_ui_controller_action(gl30_ui_controller_t* c, uint64_t now_us,
                               gl30_ui_action_t action) {
  if (!c) return;
  gl30_ui_anim_output_t ignored;
  gl30_ui_controller_tick(c, now_us, &ignored);
  gl30_ui_app_action(&c->app, action);
}
