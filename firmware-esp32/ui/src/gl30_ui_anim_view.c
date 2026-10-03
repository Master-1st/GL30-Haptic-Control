#include "gl30_ui_anim_view.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include "gl30_ui_anim_model.h"

#include "lvgl.h"

#define GL30_UI_VIEW_TEXT_MAX 20
#define GL30_UI_VIEW_MODE_TEXT_MAX 12
#define GL30_UI_VIEW_STATUS_TEXT_MAX 24
#define GL30_UI_VIEW_UNIT_TEXT_MAX 8

struct gl30_ui_anim_view_t {
  lv_obj_t* root;
  lv_obj_t* ring;
  lv_obj_t* arc;
  lv_obj_t* value_label;
  lv_obj_t* unit_label;
  lv_obj_t* mode_label;
  lv_obj_t* status_label;
  lv_obj_t* hero;
  gl30_ui_anim_output_t frame;
  gl30_ui_mode_t mode;
  gl30_ui_action_handler_t action_handler;
  void* action_context;
  uint16_t canvas_size;
  lv_color_t accent_color;
  lv_color_t warn_color;
  lv_color_t lost_color;
  lv_color_t bg_color;
  lv_color_t ring_color;
  lv_color_t ring_warn_color;
  lv_color_t ring_lost_color;
  char value_text[GL30_UI_VIEW_TEXT_MAX];
  char mode_text[GL30_UI_VIEW_MODE_TEXT_MAX];
  char status_text[GL30_UI_VIEW_STATUS_TEXT_MAX];
  char unit_text[GL30_UI_VIEW_UNIT_TEXT_MAX];
};


static void gl30_ui_view_event(lv_event_t* event) {
  gl30_ui_anim_view_t* view = lv_event_get_user_data(event);
  if (!view || !view->action_handler) return;
  const lv_event_code_t code = lv_event_get_code(event);
  if (code == LV_EVENT_SHORT_CLICKED) {
    const gl30_ui_action_t action = lv_event_get_target(event) == view->mode_label
        ? (view->mode == GL30_UI_MODE_VOLUME ? GL30_UI_ACTION_TIMER : GL30_UI_ACTION_VOLUME)
        : GL30_UI_ACTION_PRIMARY;
    view->action_handler(view->action_context, action);
  } else if (code == LV_EVENT_LONG_PRESSED) {
    view->action_handler(view->action_context, GL30_UI_ACTION_RESET);
  }
}

void gl30_ui_view_set_action_handler(gl30_ui_anim_view_t* view,
                                    gl30_ui_action_handler_t handler, void* context) {
  if (!view) return;
  view->action_handler = handler;
  view->action_context = context;
}

static void gl30_ui_view_apply_alpha(gl30_ui_anim_view_t* view, float wake_level, bool off) {
  const float clamped_wake = wake_level < 0.0f ? 0.0f : (wake_level > 1.0f ? 1.0f : wake_level);
  const float wake_visibility = off ? 0.0f : (0.85f + 0.15f * clamped_wake);
  const lv_opa_t opa = (lv_opa_t)(wake_visibility * 255.0f + 0.5f);
  if (view == NULL) {
    return;
  }
  lv_obj_set_style_opa(view->ring, opa, LV_PART_MAIN);
  lv_obj_set_style_opa(view->arc, opa, LV_PART_MAIN);
  lv_obj_set_style_opa(view->arc, opa, LV_PART_INDICATOR);
  lv_obj_set_style_opa(view->arc, opa, LV_PART_KNOB);
  lv_obj_set_style_text_opa(view->value_label, opa, LV_PART_MAIN);
  lv_obj_set_style_text_opa(view->unit_label, opa, LV_PART_MAIN);
  lv_obj_set_style_text_opa(view->mode_label, opa, LV_PART_MAIN);
  lv_obj_set_style_text_opa(view->status_label, opa, LV_PART_MAIN);
  /* Keep vector drawing in the parent layer; off is handled in its draw event. */
  lv_obj_set_style_opa(view->hero, LV_OPA_COVER, 0);
}

static void gl30_ui_view_set_colors(gl30_ui_anim_view_t* view, const gl30_ui_anim_output_t* output) {
  lv_color_t accent = view->accent_color;
  lv_color_t warn = view->warn_color;
  lv_color_t lost = view->lost_color;

  if (output->fault) {
    lv_obj_set_style_arc_color(view->arc, warn, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(view->arc, lv_color_mix(view->bg_color, warn, 240), LV_PART_MAIN);
    lv_obj_set_style_border_color(view->ring, view->ring_warn_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(view->mode_label, warn, LV_PART_MAIN);
    lv_obj_set_style_text_color(view->status_label, warn, LV_PART_MAIN);
    lv_obj_set_style_text_color(view->unit_label, warn, LV_PART_MAIN);
    lv_obj_set_style_text_color(view->value_label, warn, LV_PART_MAIN);
  } else if (output->lost_connection) {
    lv_obj_set_style_arc_color(view->arc, lost, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(view->arc, lv_color_mix(view->bg_color, lost, 220), LV_PART_MAIN);
    lv_obj_set_style_border_color(view->ring, view->ring_lost_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(view->mode_label, lost, LV_PART_MAIN);
    lv_obj_set_style_text_color(view->status_label, lost, LV_PART_MAIN);
    lv_obj_set_style_text_color(view->unit_label, lost, LV_PART_MAIN);
    lv_obj_set_style_text_color(view->value_label, lost, LV_PART_MAIN);
  } else {
    lv_obj_set_style_arc_color(view->arc, accent, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(view->arc, lv_color_mix(view->bg_color, accent, 200), LV_PART_MAIN);
    lv_obj_set_style_border_color(view->ring, view->ring_color, LV_PART_MAIN);
    lv_obj_set_style_text_color(view->mode_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(view->status_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(view->unit_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(view->value_label, lv_color_white(), LV_PART_MAIN);
  }
}


/* Original vector artwork. Motion references: Nest's functional icons and
   X-Knob's short page transitions; no vendor art or animation assets copied. */
typedef struct {
  lv_layer_t* layer;
  float x, y, scale, tilt;
  lv_color_t color;
} hero_draw_t;

static lv_point_precise_t hero_point(const hero_draw_t* d, float x, float y) {
  const float c = cosf(d->tilt), s = sinf(d->tilt);
  return (lv_point_precise_t){d->x + d->scale * (x*c-y*s), d->y + d->scale * (x*s+y*c)};
}

static void hero_line(const hero_draw_t* d, float x1, float y1, float x2, float y2,
                      int width, lv_opa_t opa) {
  lv_draw_line_dsc_t line;
  lv_draw_line_dsc_init(&line);
  line.p1 = hero_point(d, x1, y1);
  line.p2 = hero_point(d, x2, y2);
  line.color = d->color;
  line.width = (int32_t)fmaxf(1, width*d->scale);
  line.opa = opa;
  line.round_start = line.round_end = 1;
  lv_draw_line(d->layer, &line);
}

static void hero_triangle(const hero_draw_t* d, float x1, float y1, float x2, float y2,
                          float x3, float y3, lv_opa_t opa) {
  lv_draw_triangle_dsc_t triangle;
  lv_draw_triangle_dsc_init(&triangle);
  triangle.p[0] = hero_point(d, x1, y1);
  triangle.p[1] = hero_point(d, x2, y2);
  triangle.p[2] = hero_point(d, x3, y3);
  triangle.bg_color = d->color;
  triangle.bg_opa = opa;
  lv_draw_triangle(d->layer, &triangle);
}

static void gl30_ui_hero_draw(lv_event_t* event) {
  gl30_ui_anim_view_t* view = lv_event_get_user_data(event);
  const gl30_ui_anim_output_t* f = &view->frame;
  if (f->off) return;
  lv_area_t area;
  lv_obj_get_coords(view->hero, &area);
  const float activity = f->fault || f->lost_connection ? 0.0f : f->activity;
  hero_draw_t d = {lv_event_get_layer(event), (area.x1+area.x2)*0.5f, (area.y1+area.y2)*0.5f,
      view->canvas_size / 466.0f, 0,
      f->fault ? view->warn_color : (f->lost_connection ? view->lost_color : view->accent_color)};
  if (f->mode == GL30_UI_MODE_VOLUME) {
    /* It settles when interaction stops; this is not an audio spectrum. */
    d.tilt = f->direction * activity * 0.08f;
    d.scale *= 1.0f + activity * 0.07f * sinf(f->phase * 3);
    const float body[][2] = {{-48,-14},{-27,-14},{-5,-34},{-5,34},{-27,14},{-48,14},{-48,-14}};
    hero_triangle(&d, -26,-13,-7,-30,-7,30, 65);
    for (unsigned i=1; i<7; ++i)
      hero_line(&d, body[i-1][0],body[i-1][1],body[i][0],body[i][1],4,255);
    if (f->muted) {
      hero_line(&d, 18,-16,48,16,4,255);
      hero_line(&d, 48,-16,18,16,4,255);
    } else {
      for (unsigned i=0; i<3; ++i) {
        lv_draw_arc_dsc_t wave;
        lv_draw_arc_dsc_init(&wave);
        const lv_point_precise_t center = hero_point(&d,-15,0);
        wave.center = (lv_point_t){(int32_t)center.x,(int32_t)center.y};
        wave.radius = (uint16_t)((38+i*18+f->value_ratio*8+activity*5)*d.scale);
        wave.start_angle = 318 + d.tilt*57.29578f;
        wave.end_angle = 42 + d.tilt*57.29578f;
        wave.width = (int32_t)fmaxf(1,4*d.scale);
        wave.rounded = 1;
        wave.color = d.color;
        wave.opa = (lv_opa_t)(255*fminf(1,fmaxf(0.12f,f->value_ratio*3-i+0.12f)));
        lv_draw_arc(d.layer,&wave);
      }
    }
    return;
  }

  if (f->timer_finished) {
    /* One finish burst, then a quiet checkmark. */
    const float p = f->completion;
    for (unsigned i=0; i<8 && p<1; ++i) {
      const float a = i*0.78539816f;
      const float r = 32+25*p;
      hero_line(&d,cosf(a)*r,sinf(a)*r,cosf(a)*(r+7*(1-p)),sinf(a)*(r+7*(1-p)),
          3,(lv_opa_t)(255*(1-p)));
    }
    hero_line(&d,-22,0,-6,17,6,255);
    hero_line(&d,-6,17,26,-20,6,255);
    return;
  }

  const float glass[][2] = {{-34,-42},{34,-42},{34,-32},{7,0},{34,32},{34,42},
      {-34,42},{-34,32},{-7,0},{-34,-32},{-34,-42}};
  for (unsigned i=1;i<11;++i)
    hero_line(&d,glass[i-1][0],glass[i-1][1],glass[i][0],glass[i][1],3,185);
  hero_line(&d,-39,-47,39,-47,5,255);
  hero_line(&d,-39,47,39,47,5,255);
  const float upper = sqrtf(f->timer_fill), lower = sqrtf(1-f->timer_fill);
  if (upper > 0.001f)
    hero_triangle(&d,-27*upper,-4-29*upper,27*upper,-4-29*upper,0,-4,240);
  if (lower > 0.001f)
    hero_triangle(&d,-27*lower,34,27*lower,34,0,34-29*lower,240);
  if ((f->timer_running || f->timer_paused) && f->timer_fill > 0) {
    /* Uses countdown phase, not render time: pause freezes each grain exactly. */
    for (unsigned i=0;i<3;++i) {
      const float p = fmodf(f->phase * 9.5492966f + i/3.0f,1.0f);
      const float y = 1 + p*(30-29*lower);
      hero_line(&d,0,y,0,y+1,2,220);
    }
  }
  if (f->timer_paused) {
    hero_line(&d,51,-8,51,8,4,255);
    hero_line(&d,62,-8,62,8,4,255);
  }
}

static void gl30_ui_view_set_fonts(gl30_ui_anim_view_t* view) {
  lv_obj_set_style_text_font(view->mode_label, LV_FONT_DEFAULT, LV_PART_MAIN);
  lv_obj_set_style_text_font(view->status_label, LV_FONT_DEFAULT, LV_PART_MAIN);

#if defined(LV_FONT_MONTSERRAT_48) && LV_FONT_MONTSERRAT_48
  lv_obj_set_style_text_font(view->value_label, &lv_font_montserrat_48, LV_PART_MAIN);
#else
  lv_obj_set_style_text_font(view->value_label, LV_FONT_DEFAULT, LV_PART_MAIN);
#endif

#if defined(LV_FONT_MONTSERRAT_32) && LV_FONT_MONTSERRAT_32
  lv_obj_set_style_text_font(view->unit_label, &lv_font_montserrat_32, LV_PART_MAIN);
#else
  lv_obj_set_style_text_font(view->unit_label, LV_FONT_DEFAULT, LV_PART_MAIN);
#endif
}

gl30_ui_anim_view_t* gl30_ui_view_create(void* parent_obj, uint16_t canvas_size) {
  gl30_ui_anim_view_t* view = NULL;
  lv_obj_t* root;
  lv_obj_t* ring;
  lv_obj_t* arc;
  lv_obj_t* value;
  lv_obj_t* unit;
  lv_obj_t* mode;
  lv_obj_t* status;
  lv_obj_t* parent = (lv_obj_t*)(parent_obj == NULL ? lv_scr_act() : parent_obj);

  if (canvas_size == 0u) {
    return NULL;
  }

  view = (gl30_ui_anim_view_t*)lv_malloc(sizeof(*view));
  if (view == NULL) {
    return NULL;
  }
  memset(view, 0, sizeof(*view));

  root = lv_obj_create(parent);
  view->root = root;
  if (root == NULL) {
    gl30_ui_view_destroy(view);
    return NULL;
  }
  ring = lv_obj_create(root);
  arc = lv_arc_create(root);
  value = lv_label_create(root);
  unit = lv_label_create(root);
  mode = lv_label_create(root);
  status = lv_label_create(root);

  if (!root || !ring || !arc || !value || !unit || !mode || !status) {
    gl30_ui_view_destroy(view);
    return NULL;
  }

  view->root = root;
  view->ring = ring;
  view->arc = arc;
  view->value_label = value;
  view->unit_label = unit;
  view->mode_label = mode;
  view->status_label = status;
  view->canvas_size = canvas_size;
  view->accent_color = lv_color_hex(0x2D9DFF);
  view->warn_color = lv_color_hex(0xFF5B5B);
  view->lost_color = lv_color_hex(0xF2BF56);
  view->bg_color = lv_color_hex(0x0A2946);
  view->ring_color = lv_color_darken(view->accent_color, 152);
  view->ring_warn_color = lv_color_darken(view->warn_color, 152);
  view->ring_lost_color = lv_color_darken(view->lost_color, 152);

  lv_obj_set_size(root, canvas_size, canvas_size);
  lv_obj_set_style_bg_opa(root, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(root, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_radius(root, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_border_width(root, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(root, 0, LV_PART_MAIN);
  lv_obj_set_style_outline_width(root, 0, LV_PART_MAIN);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(root, gl30_ui_view_event, LV_EVENT_SHORT_CLICKED, view);
  lv_obj_add_event_cb(root, gl30_ui_view_event, LV_EVENT_LONG_PRESSED, view);
  lv_obj_center(root);

  lv_obj_set_size(ring, (int32_t)(canvas_size - (canvas_size / 8u)), (int32_t)(canvas_size - (canvas_size / 8u)));
  lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(ring, 2, LV_PART_MAIN);
  lv_obj_set_style_pad_all(ring, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_border_opa(ring, LV_OPA_60, LV_PART_MAIN);
  lv_obj_clear_flag(ring, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_center(ring);

  lv_arc_set_range(arc, 0, 1000);
  lv_arc_set_bg_angles(arc, 135, 405);
  lv_arc_set_value(arc, 0);
  lv_obj_set_size(
      arc,
      (int32_t)(canvas_size - (canvas_size / 5u)),
      (int32_t)(canvas_size - (canvas_size / 5u)));
  lv_obj_center(arc);
  lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_arc_color(arc, lv_color_darken(view->bg_color, 40), LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_width(arc, 9, LV_PART_INDICATOR);
  lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_opa(arc, LV_OPA_COVER, LV_PART_KNOB);
  lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(arc, NULL, LV_PART_KNOB);

  /* One bounded vector object; no per-frame bitmap/sprite allocations here. */
  view->hero = lv_obj_create(root);
  lv_obj_remove_style_all(view->hero);
  lv_obj_set_size(view->hero, canvas_size * 180 / 466, canvas_size * 130 / 466);
  lv_obj_align(view->hero, LV_ALIGN_CENTER, 0, -(int32_t)(canvas_size * 70u / 466u));
  lv_obj_remove_flag(view->hero, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(view->hero, gl30_ui_hero_draw, LV_EVENT_DRAW_MAIN, view);

  lv_obj_set_style_text_color(mode, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_text_color(status, lv_color_white(), LV_PART_MAIN);
  lv_label_set_long_mode(mode, LV_LABEL_LONG_CLIP);
  lv_label_set_long_mode(status, LV_LABEL_LONG_CLIP);
  lv_label_set_long_mode(unit, LV_LABEL_LONG_CLIP);
  lv_label_set_long_mode(value, LV_LABEL_LONG_CLIP);
  lv_obj_set_style_text_align(mode, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_align(unit, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_width(mode, canvas_size - 24);
  lv_obj_set_width(status, canvas_size - 24);
  lv_obj_set_width(unit, canvas_size - 24);
  lv_obj_set_width(value, canvas_size - 24);

  lv_obj_align(value, LV_ALIGN_CENTER, 0, canvas_size * 42 / 466);
  lv_obj_align(unit, LV_ALIGN_CENTER, 0, canvas_size * 84 / 466);
  lv_obj_align(mode, LV_ALIGN_CENTER, 0, canvas_size * 133 / 466);
  lv_obj_align(status, LV_ALIGN_CENTER, 0, canvas_size * 157 / 466);
  gl30_ui_view_set_fonts(view);
  lv_obj_set_style_text_font(unit, LV_FONT_DEFAULT, 0);
  lv_obj_add_flag(mode, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(mode, gl30_ui_view_event, LV_EVENT_SHORT_CLICKED, view);
  gl30_ui_view_set_colors(view, &((gl30_ui_anim_output_t){.fault=false,.lost_connection=false}));

  snprintf(view->value_text, sizeof(view->value_text), "42");
  snprintf(view->unit_text, sizeof(view->unit_text), "%%");
  snprintf(view->mode_text, sizeof(view->mode_text), "VOLUME");
  snprintf(view->status_text, sizeof(view->status_text), "READY");
  lv_label_set_text_static(view->value_label, view->value_text);
  lv_label_set_text_static(view->unit_label, view->unit_text);
  lv_label_set_text_static(view->mode_label, view->mode_text);
  lv_label_set_text_static(view->status_label, view->status_text);
  gl30_ui_view_apply_alpha(view, 1.0f, false);
  return view;
}

void gl30_ui_view_destroy(gl30_ui_anim_view_t* view) {
  if (view == NULL) {
    return;
  }
  if (view->root != NULL) {
    lv_obj_del(view->root);
  }
  lv_free(view);
}

void gl30_ui_view_render(gl30_ui_anim_view_t* view, const gl30_ui_anim_output_t* output) {
  int32_t arc_value;

  if (view == NULL || output == NULL) {
    return;
  }
  view->mode = output->mode;

  lv_obj_set_style_bg_color(view->root, lv_color_black(), LV_PART_MAIN);

  if (output->off) {
    arc_value = 0;
    snprintf(view->value_text, sizeof(view->value_text), "0");
    snprintf(view->unit_text, sizeof(view->unit_text), "");
    snprintf(view->mode_text, sizeof(view->mode_text), "OFF");
    snprintf(view->status_text, sizeof(view->status_text), "OFF");
  } else {
    arc_value = (int32_t)(output->value_ratio * 1000.0f + 0.5f);
    if (arc_value < 0) {
      arc_value = 0;
    } else if (arc_value > 1000) {
      arc_value = 1000;
    }

    snprintf(view->value_text, sizeof(view->value_text), "%s", output->value_text);
    if (output->mode == GL30_UI_MODE_TIMER) {
      snprintf(view->unit_text, sizeof(view->unit_text), "%s",
          output->timer_running ? "PAUSE" : (output->timer_paused ? "RESUME" : "START"));
      snprintf(view->mode_text, sizeof(view->mode_text), "TIMER");
    } else {
      snprintf(view->unit_text, sizeof(view->unit_text), "%s", output->muted ? "UNMUTE" : "MUTE");
      snprintf(view->mode_text, sizeof(view->mode_text), "VOLUME");
    }
    if (output->fault) {
      snprintf(view->status_text, sizeof(view->status_text), "FAULT");
    } else if (output->lost_connection) {
      snprintf(view->status_text, sizeof(view->status_text), "DISCONNECTED");
    } else {
      snprintf(view->status_text, sizeof(view->status_text), "%s", output->status_text);
    }
  }

  if (!output->off) {
    gl30_ui_view_set_colors(view, output);
  } else {
    lv_obj_set_style_text_color(view->mode_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(view->status_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(view->value_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(view->unit_label, lv_color_white(), LV_PART_MAIN);
  }

  gl30_ui_view_apply_alpha(view, output->wake_level, output->off);

  view->frame = *output;
  lv_obj_invalidate(view->hero);
  lv_obj_set_style_border_width(view->ring, output->timer_finished ? 4 : 2, 0);
  lv_obj_align(view->value_label, LV_ALIGN_CENTER,
      (int32_t)(output->transition * 18.0f), view->canvas_size * 42 / 466);

  lv_arc_set_value(view->arc, arc_value);
  lv_label_set_text_static(view->value_label, view->value_text);
  lv_label_set_text_static(view->unit_label, view->unit_text);
  lv_label_set_text_static(view->mode_label, view->mode_text);
  lv_label_set_text_static(view->status_label, view->status_text);
}
