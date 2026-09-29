#ifndef GL30_DEMO_H
#define GL30_DEMO_H
#include <stdbool.h>
#include <stdint.h>
#include "gl30_model.h"
bool gl30_demo_init(uint32_t now_ms,uint64_t unix_seconds);
void gl30_demo_sample(uint32_t capture_ms);
bool gl30_demo_animation_active(void);
/* Poll/submit an already-rendered frame without advancing or drawing UI state. */
void gl30_demo_service_display(uint32_t now_ms);
void gl30_demo_render(uint32_t display_ms);
void gl30_demo_rotate(int16_t steps);
void gl30_demo_motor_menu(uint32_t epoch,uint32_t session,int32_t q,float fraction,bool valid,uint32_t now_ms);
typedef struct {
    uint32_t frames, last_draw_us, last_update_us, p95_update_us, max_update_us;
} gl30_demo_metrics;
typedef struct {
    uint32_t frame_id,capture_ms,render_ms;
    uint64_t begin_us,draw_begin_us,draw_end_us;
    uint32_t clear_us,compare_us;
} gl30_frame_metrics;
typedef struct {
    uint32_t menu_layout_us,menu_disc_us,menu_ring_us,menu_icon_us,menu_label_us;
} gl30_render_profile;
void gl30_demo_get_frame_metrics(gl30_frame_metrics *out);
/* UI-owner raster count; scheduling must not sort the diagnostic P95 window. */
uint32_t gl30_demo_raster_count(void);
void gl30_demo_get_metrics(gl30_demo_metrics *metrics);
void gl30_render_get_profile(gl30_render_profile *profile);
bool gl30_demo_display_ok(void);
/* Monotonic platform clock, implemented by the host/ESP display adapter. */
uint64_t gl30_platform_time_us(void);
/* Consume display results on the UI owner, including while the screen is off. */
void gl30_platform_display_poll(void);
void gl30_demo_button(bool pressed,uint32_t now_ms);
void gl30_demo_cancel_input(void);
void gl30_demo_touch(int16_t x,int16_t y,bool pressed,uint32_t now_ms);
/* Shortcuts: 0 timer, 1 volume, 2 reset current timer/stopwatch, 3 power. */
void gl30_demo_shortcut(int command);
void gl30_demo_set_clock(uint64_t unix_seconds);
void gl30_demo_set_fault(bool fault);
const gl30_model *gl30_demo_state(void);
const char *gl30_demo_json(void);
/* Menu raster modes: 0 full chrome, 1 carousel+selected label, 2 carousel only. */
void gl30_render_set_menu_mode(uint8_t mode);
uint8_t gl30_render_get_menu_mode(void);
void gl30_draw_scene(const gl30_model *s,int16_t x_offset,int16_t clip_x,uint16_t clip_width);
#endif
