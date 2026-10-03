#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <time.h>
#include <string.h>

#include "gl30_ui_anim_model.h"
#include "gl30_ui_anim_view.h"
#include "lvgl.h"

#define GL30_UI_HOST_CANVAS_SIZE 466

static lv_color_t g_framebuffer[GL30_UI_HOST_CANVAS_SIZE * GL30_UI_HOST_CANVAS_SIZE];
static lv_color_t g_draw_buffer[GL30_UI_HOST_CANVAS_SIZE * GL30_UI_HOST_CANVAS_SIZE];
static bool g_frame_ready = false;

static uint32_t host_tick_get(void) {
  static clock_t start = 0;
  if (start == 0) {
    start = clock();
    return 0u;
  }
  return (uint32_t)(((uint64_t)(clock() - start) * 1000u) / (uint64_t)CLOCKS_PER_SEC);
}

static void gl30_host_flush_cb(lv_display_t* display, const lv_area_t* area, uint8_t* color_p) {
  (void)display;

  if (area == NULL || color_p == NULL) {
    lv_display_flush_ready(display);
    return;
  }

  const int32_t x1 = area->x1 < 0 ? 0 : (area->x1 > (int32_t)GL30_UI_HOST_CANVAS_SIZE - 1 ? (int32_t)GL30_UI_HOST_CANVAS_SIZE - 1 : area->x1);
  const int32_t x2 = area->x2 < 0 ? 0 : (area->x2 > (int32_t)GL30_UI_HOST_CANVAS_SIZE - 1 ? (int32_t)GL30_UI_HOST_CANVAS_SIZE - 1 : area->x2);
  const int32_t y1 = area->y1 < 0 ? 0 : (area->y1 > (int32_t)GL30_UI_HOST_CANVAS_SIZE - 1 ? (int32_t)GL30_UI_HOST_CANVAS_SIZE - 1 : area->y1);
  const int32_t y2 = area->y2 < 0 ? 0 : (area->y2 > (int32_t)GL30_UI_HOST_CANVAS_SIZE - 1 ? (int32_t)GL30_UI_HOST_CANVAS_SIZE - 1 : area->y2);

  if ((x1 > x2) || (y1 > y2)) {
    lv_display_flush_ready(display);
    return;
  }

  const int32_t draw_width = (int32_t)lv_area_get_width(area);
  const lv_color_t* src = (const lv_color_t*)color_p;
  const int32_t dst_stride = GL30_UI_HOST_CANVAS_SIZE;
  for (int32_t y = y1; y <= y2; ++y) {
    const int32_t src_y = y - area->y1;
    const int32_t src_x = x1 - area->x1;
    const int32_t copy_width = x2 - x1 + 1;

    lv_memcpy(
      &g_framebuffer[(size_t)y * dst_stride + x1],
      &src[(size_t)src_y * (size_t)draw_width + src_x],
      (size_t)copy_width * sizeof(lv_color_t)
    );
  }

  g_frame_ready = true;
  lv_display_flush_ready(display);
}

static int write_ppm(const char* path, const lv_color_t* framebuffer) {
  FILE* f = fopen(path, "wb");
  if (f == NULL) {
    return -1;
  }

  fprintf(f, "P6\n%d %d\n255\n", GL30_UI_HOST_CANVAS_SIZE, GL30_UI_HOST_CANVAS_SIZE);
  for (size_t i = 0u; i < (size_t)GL30_UI_HOST_CANVAS_SIZE * (size_t)GL30_UI_HOST_CANVAS_SIZE; ++i) {
    const uint32_t u32 = lv_color_to_u32(framebuffer[i]);
    const uint8_t r = (uint8_t)((u32 >> 16) & 0xFFu);
    const uint8_t g = (uint8_t)((u32 >> 8) & 0xFFu);
    const uint8_t b = (uint8_t)(u32 & 0xFFu);
    fputc((int)r, f);
    fputc((int)g, f);
    fputc((int)b, f);
  }

  if (fclose(f) != 0) {
    return -1;
  }
  return 0;
}

static bool render_state_and_capture(gl30_ui_anim_model_t* model,
                                    gl30_ui_anim_view_t* view,
                                    gl30_ui_anim_input_t* input,
                                    const char* out_file,
                                    uint32_t dt_ms) {
  gl30_ui_anim_output_t output;
  g_frame_ready = false;

  gl30_ui_anim_model_tick(model, input, dt_ms, &output);
  gl30_ui_view_render(view, &output);

  lv_refr_now(NULL);

  if (!g_frame_ready) {
    fprintf(stderr, "no frame rendered for %s\n", out_file);
    return false;
  }
  size_t lit_pixels = 0u;
  size_t blue_pixels = 0u;
  for (size_t i = 0; i < GL30_UI_HOST_CANVAS_SIZE * GL30_UI_HOST_CANVAS_SIZE; ++i) {
    const lv_color_t c = g_framebuffer[i];
    if (c.red || c.green || c.blue) ++lit_pixels;
    if (c.blue > c.red && c.blue > c.green) ++blue_pixels;
  }
  if ((input->off && lit_pixels != 0u) ||
      (!input->off && (lit_pixels == 0u || blue_pixels == 0u))) {
    fprintf(stderr, "unexpected pixels for %s\n", out_file);
    return false;
  }
  if (write_ppm(out_file, g_framebuffer) != 0) {
    fprintf(stderr, "failed to write %s\n", out_file);
    return false;
  }
  printf("PASS %s: lit=%zu blue=%zu\n", out_file, lit_pixels, blue_pixels);
  return true;
}

int main(void) {
  gl30_ui_anim_model_t model;
  gl30_ui_anim_input_t input;
  lv_display_t* display;
  gl30_ui_anim_view_t* view;

  gl30_ui_anim_model_init(&model);
  memset(&input, 0, sizeof(input));
  input.volume = GL30_UI_MODE_VOLUME_DEFAULT;
  input.timer_minutes = GL30_UI_TIMER_DEFAULT_MINUTES;
  input.timer_running = false;
  input.mode = GL30_UI_MODE_VOLUME;
  input.wake_signal = false;
  input.off = false;
  input.connected = true;
  input.fault = false;

  lv_init();
  lv_tick_set_cb(host_tick_get);

  display = lv_display_create(GL30_UI_HOST_CANVAS_SIZE, GL30_UI_HOST_CANVAS_SIZE);
  if (display == NULL) {
    fprintf(stderr, "lv_display_create failed\n");
    return 1;
  }
  /* lv_color_t is BGR888 in LVGL 9; do not interpret packed RGB565 as it. */
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
  lv_display_set_flush_cb(display, gl30_host_flush_cb);
  lv_display_set_buffers(display,
                        g_draw_buffer,
                        NULL,
                        (uint32_t)sizeof(g_draw_buffer),
                        LV_DISPLAY_RENDER_MODE_FULL);
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_black(), 0);

  view = gl30_ui_view_create(NULL, GL30_UI_HOST_CANVAS_SIZE);
  if (view == NULL) {
    fprintf(stderr, "gl30_ui_view_create failed\n");
    return 2;
  }

  input.wake_signal = true;
  memset(g_framebuffer, 0, sizeof(g_framebuffer));
  const bool awake_ok = render_state_and_capture(&model, view, &input, "gl30_ui_host_awake.ppm", 50u);

  input.volume = 85;
  const bool loud_ok = render_state_and_capture(&model, view, &input, "gl30_ui_host_adjust.ppm", 80u);
  input.muted = true;
  const bool mute_ok = render_state_and_capture(&model, view, &input, "gl30_ui_host_muted.ppm", 200u);
  input.mode = GL30_UI_MODE_TIMER;
  input.timer_minutes = 1;
  input.timer_running = true;
  input.timer_remaining_ms = 30000;
  const bool timer_ok = render_state_and_capture(&model, view, &input, "gl30_ui_host_timer.ppm", 80u);
  input.timer_running = false;
  input.timer_paused = true;
  const bool paused_ok = render_state_and_capture(&model, view, &input, "gl30_ui_host_paused.ppm", 200u);
  input.timer_paused = false;
  input.timer_finished = true;
  input.timer_remaining_ms = 0;
  const bool done_ok = render_state_and_capture(&model, view, &input, "gl30_ui_host_done.ppm", 80u);

  input.off = true;
  input.wake_signal = false;
  const bool off_ok = render_state_and_capture(&model, view, &input, "gl30_ui_host_off.ppm", 50u);

  gl30_ui_view_destroy(view);
  return awake_ok && loud_ok && mute_ok && timer_ok && paused_ok && done_ok && off_ok ? 0 : 3;
}
