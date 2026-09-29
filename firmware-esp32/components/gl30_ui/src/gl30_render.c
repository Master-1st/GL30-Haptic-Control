#include "gl30_demo.h"
#include "kk_oled.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifndef GL30_FAST_MENU_RING
#define GL30_FAST_MENU_RING 1
#endif
#ifndef GL30_WRAP_MENU_PHASE
#define GL30_WRAP_MENU_PHASE 1
#endif

extern const uint8_t gl30_font_body[],gl30_font_title[],gl30_font_digits[],gl30_font_small[];
extern const uint16_t gl30_summit[466*466];
static const char *names_en[]={"Timer","Volume","Stopwatch","Alarm","Weather","Haptics","Settings","Calendar","Lighting"};
static const char *names_zh[]={"计时器","音量","秒表","闹钟","天气","手感","设置","日历","灯效调整"};
static const char *effects_en[]={"Context","Solid","Breathe","Flow"};
static const char *effects_zh[]={"随功能","常亮","呼吸","流动"};
static const char *color_names_en[]={"Orange","Gold","Green","Purple","Rose","Lemon"};
static const char *color_names_zh[]={"橙色","金色","绿色","紫色","玫红","浅柠黄"};
static const uint32_t colors[]={0xff9b51,0xffd166,0x91db9f,0xc29bff,0xf390b6,0xe5e99a};
static const uint32_t app_colors[]={0xffad68,0x83dce8,0xe1e986,0xf497aa,0xffd47c,0xb5a1ff,0xb9c9d9,0x91dcab,0xffc77d};
static const uint32_t WHITE=0xf5f1e8, MUTED=0x87939d, BLUE=0x55c9ff;
static int16_t offset_x;
static gl30_render_profile render_profile;
static uint8_t menu_render_mode;
void gl30_render_get_profile(gl30_render_profile *profile) { if(profile) *profile=render_profile; }
void gl30_render_set_menu_mode(uint8_t mode) { menu_render_mode=mode>2U?0U:mode; }
uint8_t gl30_render_get_menu_mode(void) { return menu_render_mode; }
static bool is_zh(const gl30_model *s) { return s->language==GL30_LANGUAGE_CHINESE; }
/* The 34 px body asset contains every Chinese UI glyph. The 40 px title
 * subset is for English headings; measuring missing glyphs cannot detect
 * an incomplete Chinese heading because the font advances by a space. */
static const uint8_t *title_font(const gl30_model *s) { return is_zh(s)?gl30_font_body:gl30_font_title; }
static const char *tr(const gl30_model *s,const char *en,const char *zh) { return is_zh(s)?zh:en; }
static const char *app_name(const gl30_model *s,int app) { return is_zh(s)?names_zh[app]:names_en[app]; }
static const char *effect_name(const gl30_model *s,int effect) { return is_zh(s)?effects_zh[effect]:effects_en[effect]; }
static const char *color_name(const gl30_model *s,int color) { return is_zh(s)?color_names_zh[color]:color_names_en[color]; }
static uint16_t rgb(uint32_t c) { return (uint16_t)(((c>>19)&31)<<11 | ((c>>10)&63)<<5 | ((c>>3)&31)); }
static float bound(float v,float lo,float hi) { return v<lo?lo:v>hi?hi:v; }
static uint32_t dim(uint32_t c,float a) {
    return ((uint32_t)(((c>>16)&255)*a)<<16)|((uint32_t)(((c>>8)&255)*a)<<8)|(uint32_t)((c&255)*a);
}
#define GL30_Q4_SHIFT 4
#define GL30_Q4_ONE (1 << GL30_Q4_SHIFT)
#define GL30_Q4_HALF (GL30_Q4_ONE / 2)
#define GL30_AA_SHIFT 16

/*
 * The application renderer works in Q4 coordinates.  This keeps the
 * sub-pixel position used by the perspective menu while making the inner
 * raster loops integer-only.  The only reciprocal divisions are performed
 * once per primitive, never once per candidate pixel.
 *
 * These helpers are private to the fixed 466x466 scene below.  Current
 * call-sites keep Q4 pixel/endpoint deltas below 8000, so two squared
 * deltas sum to at most 128000000, below INT32_MAX.  Current ring radii
 * (including the AA margin) are below 3600 Q4 units.  AA interpolation is
 * evaluated only between its two edges, so (edge - distance) * inverse is
 * bounded by 255 << GL30_AA_SHIFT.  The bounds are intentionally local to
 * this application layout rather than a promise for arbitrary primitives.
 */
static int32_t q4_round(float value)
{
    return (int32_t)lroundf(value * (float)GL30_Q4_ONE);
}

static int32_t q4_floor(int32_t value)
{
    return value >= 0 ? value / GL30_Q4_ONE
                      : -(((-value) + GL30_Q4_ONE - 1) / GL30_Q4_ONE);
}

static int32_t q4_ceil(int32_t value)
{
    return value >= 0 ? (value + GL30_Q4_ONE - 1) / GL30_Q4_ONE
                      : -((-value) / GL30_Q4_ONE);
}

static int32_t q4_abs(int32_t value)
{
    return value < 0 ? -value : value;
}

static int q4_center_pixel(int32_t center_q4)
{
    return q4_floor(center_q4 + GL30_Q4_HALF);
}

static uint32_t aa_inverse(uint32_t span)
{
    return span == 0U ? 0U :
           ((uint32_t)255U << GL30_AA_SHIFT) / span;
}

static uint8_t aa_fall(uint32_t distance2, uint32_t solid2,
                       uint32_t edge2, uint32_t inverse)
{
    uint32_t value;

    if (distance2 <= solid2) return UINT8_MAX;
    if (distance2 >= edge2 || inverse == 0U) return 0U;
    value = ((edge2 - distance2) * inverse) >> GL30_AA_SHIFT;
    return (uint8_t)(value > UINT8_MAX ? UINT8_MAX : value);
}

static uint8_t aa_rise(uint32_t distance2, uint32_t edge2,
                       uint32_t solid2, uint32_t inverse)
{
    uint32_t value;

    if (distance2 <= edge2 || inverse == 0U) return 0U;
    if (distance2 >= solid2) return UINT8_MAX;
    value = ((distance2 - edge2) * inverse) >> GL30_AA_SHIFT;
    return (uint8_t)(value > UINT8_MAX ? UINT8_MAX : value);
}

static void blend_pixel(int x, int y, uint16_t color, uint8_t alpha)
{
    if (alpha != 0U) {
        OLED_BlendPixelRGB565((int16_t)(x + offset_x), (int16_t)y,
                              color, alpha);
    }
}

static void solid_hline(int left, int right, int y)
{
    if (left <= right) {
        OLED_DrawHLine((int16_t)(left + offset_x), (int16_t)y,
                       (uint16_t)(right - left + 1));
    }
}

static uint32_t distance2_q8(int32_t dx_q4, int32_t dy_q4);

static void blend_hspan(int left, int right, int y, uint16_t color,
                        uint8_t alpha)
{
    int x;

    if (alpha == 0U || left > right) return;
    if (alpha == UINT8_MAX) {
        OLED_SetColor(color, 0U);
        solid_hline(left, right, y);
        return;
    }
    for (x = left; x <= right; ++x) blend_pixel(x, y, color, alpha);
}

static void blend_vspan(int x, int top, int bottom, uint16_t color,
                        uint8_t alpha)
{
    int y;

    if (alpha == 0U || top > bottom) return;
    if (alpha == UINT8_MAX) {
        OLED_SetColor(color, 0U);
        OLED_DrawVLine((int16_t)(x + offset_x), (int16_t)top,
                       (uint16_t)(bottom - top + 1));
        return;
    }
    for (y = top; y <= bottom; ++y) blend_pixel(x, y, color, alpha);
}

/* Same Q4 capsule and AA law as line(), with the projection eliminated for
 * horizontal/vertical icon strokes. The long middle section has one alpha
 * per scanline/column; rounded endpoint tails keep the original distance. */
static bool line_axis_aligned(int32_t x1_q4, int32_t y1_q4,
                              int32_t x2_q4, int32_t y2_q4,
                              int left, int right, int top, int bottom,
                              uint32_t inner2, uint32_t outer2,
                              uint32_t inverse, uint16_t color)
{
    if (y1_q4 == y2_q4) {
        int32_t left_q4 = x1_q4 < x2_q4 ? x1_q4 : x2_q4;
        int32_t right_q4 = x1_q4 < x2_q4 ? x2_q4 : x1_q4;
        int center_left = q4_ceil(left_q4);
        int center_right = q4_floor(right_q4);
        int y;

        if (center_left < left) center_left = left;
        if (center_right > right) center_right = right;
        for (y = top; y <= bottom; ++y) {
            int32_t dy_q4 = (y << GL30_Q4_SHIFT) - y1_q4;
            uint8_t center_alpha = aa_fall((uint32_t)(dy_q4 * dy_q4),
                                           inner2, outer2, inverse);
            int x;

            blend_hspan(center_left, center_right, y, color, center_alpha);
            for (x = left; x < center_left; ++x) {
                uint8_t alpha = aa_fall(distance2_q8((x << GL30_Q4_SHIFT) - left_q4,
                                                     dy_q4),
                                        inner2, outer2, inverse);
                blend_pixel(x, y, color, alpha);
            }
            for (x = center_right + 1; x <= right; ++x) {
                uint8_t alpha = aa_fall(distance2_q8((x << GL30_Q4_SHIFT) - right_q4,
                                                     dy_q4),
                                        inner2, outer2, inverse);
                blend_pixel(x, y, color, alpha);
            }
        }
        return true;
    }
    if (x1_q4 == x2_q4) {
        int32_t top_q4 = y1_q4 < y2_q4 ? y1_q4 : y2_q4;
        int32_t bottom_q4 = y1_q4 < y2_q4 ? y2_q4 : y1_q4;
        int center_top = q4_ceil(top_q4);
        int center_bottom = q4_floor(bottom_q4);
        int x;

        if (center_top < top) center_top = top;
        if (center_bottom > bottom) center_bottom = bottom;
        for (x = left; x <= right; ++x) {
            int32_t dx_q4 = (x << GL30_Q4_SHIFT) - x1_q4;
            uint8_t center_alpha = aa_fall((uint32_t)(dx_q4 * dx_q4),
                                           inner2, outer2, inverse);
            int y;

            blend_vspan(x, center_top, center_bottom, color, center_alpha);
            for (y = top; y < center_top; ++y) {
                uint8_t alpha = aa_fall(distance2_q8(dx_q4,
                                                     (y << GL30_Q4_SHIFT) - top_q4),
                                        inner2, outer2, inverse);
                blend_pixel(x, y, color, alpha);
            }
            for (y = center_bottom + 1; y <= bottom; ++y) {
                uint8_t alpha = aa_fall(distance2_q8(dx_q4,
                                                     (y << GL30_Q4_SHIFT) - bottom_q4),
                                        inner2, outer2, inverse);
                blend_pixel(x, y, color, alpha);
            }
        }
        return true;
    }
    return false;
}

/* Find the horizontal half-span of a circle at one scanline.  Calls for a
 * pair of scanlines move outwards from the centre, so the cursor only ever
 * decrements.  The total work per primitive is therefore linear in radius. */
static bool circle_span_q4(int32_t radius_q4, int32_t dy_q4,
                           int32_t *cursor_q4, int32_t *span_q4)
{
    int32_t x;
    int32_t radius2;
    int32_t dy2;

    if (radius_q4 <= 0 || q4_abs(dy_q4) > radius_q4) {
        *span_q4 = 0;
        return false;
    }
    x = *cursor_q4 > radius_q4 ? radius_q4 : *cursor_q4;
    radius2 = radius_q4 * radius_q4;
    dy2 = dy_q4 * dy_q4;
    while (x > 0 && x * x + dy2 > radius2) {
        --x;
    }
    *cursor_q4 = x;
    *span_q4 = x;
    return true;
}

static void span_bounds(int32_t center_q4, int32_t span_q4,
                        int *left, int *right)
{
    *left = q4_ceil(center_q4 - span_q4);
    *right = q4_floor(center_q4 + span_q4);
    if (*left < 0) *left = 0;
    if (*right > 465) *right = 465;
}

static uint32_t distance2_q8(int32_t dx_q4, int32_t dy_q4)
{
    int32_t dx2 = dx_q4 * dx_q4;
    int32_t dy2 = dy_q4 * dy_q4;
    return (uint32_t)(dx2 + dy2);
}

static void draw_disc_row(int32_t center_q4, int32_t dy_q4,
                          int32_t outer_span_q4, bool inner_visible,
                          int32_t inner_span_q4, uint32_t inner2,
                          uint32_t outer2, uint32_t outer_inverse,
                          int y, uint16_t color)
{
    int outer_left;
    int outer_right;
    int inner_left = 0;
    int inner_right = -1;
    int x;

    span_bounds(center_q4, outer_span_q4, &outer_left, &outer_right);
    if (outer_left > outer_right) return;
    if (inner_visible) {
        span_bounds(center_q4, inner_span_q4, &inner_left, &inner_right);
        solid_hline(inner_left, inner_right, y);
    }
    if (inner_visible) {
        int edge_left = inner_left > outer_left ? inner_left : outer_left;
        int edge_right = inner_right < outer_right ? inner_right : outer_right;

        if (edge_left <= edge_right) {
            for (x = outer_left; x < edge_left; ++x) {
                uint32_t d2 = distance2_q8((x << GL30_Q4_SHIFT) - center_q4,
                                           dy_q4);
                uint8_t alpha = aa_fall(d2, inner2, outer2, outer_inverse);
                blend_pixel(x, y, color, alpha);
            }
            for (x = edge_right + 1; x <= outer_right; ++x) {
                uint32_t d2 = distance2_q8((x << GL30_Q4_SHIFT) - center_q4,
                                           dy_q4);
                uint8_t alpha = aa_fall(d2, inner2, outer2, outer_inverse);
                blend_pixel(x, y, color, alpha);
            }
        } else {
            for (x = outer_left; x <= outer_right; ++x) {
                uint32_t d2 = distance2_q8((x << GL30_Q4_SHIFT) - center_q4,
                                           dy_q4);
                uint8_t alpha = aa_fall(d2, inner2, outer2, outer_inverse);
                blend_pixel(x, y, color, alpha);
            }
        }
    } else {
        for (x = outer_left; x <= outer_right; ++x) {
            uint32_t d2 = distance2_q8((x << GL30_Q4_SHIFT) - center_q4,
                                       dy_q4);
            uint8_t alpha = aa_fall(d2, inner2, outer2, outer_inverse);
            blend_pixel(x, y, color, alpha);
        }
    }
}

static void raster_disc(float cx, float cy, float radius, uint16_t color)
{
    int32_t center_q4 = q4_round(cx);
    int32_t radius_q4 = q4_round(radius);
    int32_t cy_q4 = q4_round(cy);
    int32_t inner_q4;
    int32_t outer_q4;
    int32_t outer_cursor_up;
    int32_t outer_cursor_down;
    int32_t inner_cursor_up;
    int32_t inner_cursor_down;
    uint32_t inner2;
    uint32_t outer2;
    uint32_t outer_inverse;
    int center_y;
    int top;
    int bottom;
    int offset;

    if (radius_q4 <= 0) return;
    inner_q4 = radius_q4 > GL30_Q4_HALF ? radius_q4 - GL30_Q4_HALF : 0;
    outer_q4 = radius_q4 + GL30_Q4_HALF;
    inner2 = (uint32_t)inner_q4 * (uint32_t)inner_q4;
    outer2 = (uint32_t)outer_q4 * (uint32_t)outer_q4;
    outer_inverse = aa_inverse(outer2 - inner2);
    center_y = q4_center_pixel(cy_q4);
    top = (int)floorf(cy - radius - 1.0f);
    bottom = (int)ceilf(cy + radius + 1.0f);
    if (top < 0) top = 0;
    if (bottom > 465) bottom = 465;
    if (top > bottom) return;

    OLED_SetColor(color, 0U);
    outer_cursor_up = outer_q4;
    outer_cursor_down = outer_q4;
    inner_cursor_up = inner_q4;
    inner_cursor_down = inner_q4;
    for (offset = 0; offset <= (center_y - top > bottom - center_y ?
                                center_y - top : bottom - center_y); ++offset) {
        int y_up = center_y - offset;
        int y_down = center_y + offset;
        int32_t dy_q4;
        int32_t outer_span_q4;
        int32_t inner_span_q4;
        bool outer_visible;
        bool inner_visible;

        if (y_up >= top && y_up <= bottom) {
            dy_q4 = (y_up << GL30_Q4_SHIFT) - cy_q4;
            outer_visible = circle_span_q4(outer_q4, dy_q4,
                                           &outer_cursor_up, &outer_span_q4);
            inner_visible = circle_span_q4(inner_q4, dy_q4,
                                           &inner_cursor_up, &inner_span_q4);
            if (outer_visible) {
                draw_disc_row(center_q4, dy_q4, outer_span_q4,
                              inner_visible, inner_span_q4, inner2, outer2,
                              outer_inverse, y_up, color);
            }
        }
        if (y_down != y_up && y_down >= top && y_down <= bottom) {
            dy_q4 = (y_down << GL30_Q4_SHIFT) - cy_q4;
            outer_visible = circle_span_q4(outer_q4, dy_q4,
                                           &outer_cursor_down, &outer_span_q4);
            inner_visible = circle_span_q4(inner_q4, dy_q4,
                                           &inner_cursor_down, &inner_span_q4);
            if (outer_visible) {
                draw_disc_row(center_q4, dy_q4, outer_span_q4,
                              inner_visible, inner_span_q4, inner2, outer2,
                              outer_inverse, y_down, color);
            }
        }
    }
}

static uint8_t ring_alpha(uint32_t distance2, uint32_t inner_edge2,
                          uint32_t inner_solid2, uint32_t outer_solid2,
                          uint32_t outer_edge2, uint32_t inner_inverse,
                          uint32_t outer_inverse)
{
    if (distance2 <= inner_edge2) return 0U;
    if (distance2 < inner_solid2) {
        return aa_rise(distance2, inner_edge2, inner_solid2, inner_inverse);
    }
    if (distance2 <= outer_solid2) return UINT8_MAX;
    return aa_fall(distance2, outer_solid2, outer_edge2, outer_inverse);
}

static void draw_ring_interval(int left, int right, int y, int32_t center_q4,
                               int32_t dy_q4, uint32_t inner_edge2,
                               uint32_t inner_solid2, uint32_t outer_solid2,
                               uint32_t outer_edge2, uint32_t inner_inverse,
                               uint32_t outer_inverse, uint16_t color)
{
    int x;
    int run_start = -1;

    if (left < 0) left = 0;
    if (right > 465) right = 465;
    for (x = left; x <= right; ++x) {
        uint32_t d2 = distance2_q8((x << GL30_Q4_SHIFT) - center_q4, dy_q4);
        uint8_t alpha = ring_alpha(d2, inner_edge2, inner_solid2,
                                   outer_solid2, outer_edge2,
                                   inner_inverse, outer_inverse);
        if (alpha == UINT8_MAX) {
            if (run_start < 0) run_start = x;
        } else {
            if (run_start >= 0) {
                solid_hline(run_start, x - 1, y);
                run_start = -1;
            }
            blend_pixel(x, y, color, alpha);
        }
    }
    if (run_start >= 0) solid_hline(run_start, right, y);
}

static void raster_full_ring(float cx, float cy, float radius, float width,
                             uint16_t color)
{
    int32_t center_q4 = q4_round(cx);
    int32_t cy_q4 = q4_round(cy);
    int32_t radius_q4 = q4_round(radius);
    int32_t half_width_q4 = q4_round(width * 0.5f);
    int32_t inner_edge_q4 = radius_q4 - half_width_q4 - GL30_Q4_HALF;
    int32_t inner_solid_q4 = radius_q4 - half_width_q4 + GL30_Q4_HALF;
    int32_t outer_solid_q4 = radius_q4 + half_width_q4 - GL30_Q4_HALF;
    int32_t outer_edge_q4 = radius_q4 + half_width_q4 + GL30_Q4_HALF;
    int32_t outer_cursor_up;
    int32_t outer_cursor_down;
    int32_t inner_cursor_up;
    int32_t inner_cursor_down;
    uint32_t inner_edge2;
    uint32_t inner_solid2;
    uint32_t outer_solid2;
    uint32_t outer_edge2;
    uint32_t inner_inverse;
    uint32_t outer_inverse;
    int center_y;
    int top;
    int bottom;
    int offset;
    int32_t reference_cy_q4 = cy_q4;

    if (radius_q4 <= 0 || width <= 0.0f) return;
    if (inner_edge_q4 <= 0) {
        raster_disc(cx, cy, radius + width * 0.5f, color);
        return;
    }
    if (inner_solid_q4 < inner_edge_q4) inner_solid_q4 = inner_edge_q4;
    if (outer_solid_q4 < 0) outer_solid_q4 = 0;
    inner_edge2 = (uint32_t)inner_edge_q4 * (uint32_t)inner_edge_q4;
    inner_solid2 = (uint32_t)inner_solid_q4 * (uint32_t)inner_solid_q4;
    outer_solid2 = (uint32_t)outer_solid_q4 * (uint32_t)outer_solid_q4;
    outer_edge2 = (uint32_t)outer_edge_q4 * (uint32_t)outer_edge_q4;
    inner_inverse = aa_inverse(inner_solid2 - inner_edge2);
    outer_inverse = aa_inverse(outer_edge2 - outer_solid2);
    center_y = q4_center_pixel(cy_q4);
    top = (int)floorf(cy - (radius + width * 0.5f) - 1.0f);
    bottom = (int)ceilf(cy + (radius + width * 0.5f) + 1.0f);
    if (top < 0) top = 0;
    if (bottom > 465) bottom = 465;
    if (top > bottom) return;

    OLED_SetColor(color, 0U);
    outer_cursor_up = outer_edge_q4;
    outer_cursor_down = outer_edge_q4;
    inner_cursor_up = inner_edge_q4;
    inner_cursor_down = inner_edge_q4;
    for (offset = 0; offset <= (center_y - top > bottom - center_y ?
                                center_y - top : bottom - center_y); ++offset) {
        int y_up = center_y - offset;
        int y_down = center_y + offset;
        int32_t dy_q4;
        int32_t outer_span_q4;
        int32_t inner_span_q4;
        bool outer_visible;
        bool inner_visible;

        if (y_up >= top && y_up <= bottom) {
            int outer_left;
            int outer_right;
            dy_q4 = (y_up << GL30_Q4_SHIFT) - reference_cy_q4;
            outer_visible = circle_span_q4(outer_edge_q4, dy_q4,
                                           &outer_cursor_up, &outer_span_q4);
            inner_visible = circle_span_q4(inner_edge_q4, dy_q4,
                                           &inner_cursor_up, &inner_span_q4);
            if (outer_visible) {
                span_bounds(center_q4, outer_span_q4, &outer_left, &outer_right);
                if (inner_visible) {
                    int inner_left;
                    int inner_right;
                    span_bounds(center_q4, inner_span_q4,
                                &inner_left, &inner_right);
                    draw_ring_interval(outer_left, inner_left - 1, y_up,
                                       center_q4, dy_q4, inner_edge2,
                                       inner_solid2, outer_solid2, outer_edge2,
                                       inner_inverse, outer_inverse, color);
                    draw_ring_interval(inner_right + 1, outer_right, y_up,
                                       center_q4, dy_q4, inner_edge2,
                                       inner_solid2, outer_solid2, outer_edge2,
                                       inner_inverse, outer_inverse, color);
                } else {
                    draw_ring_interval(outer_left, outer_right, y_up,
                                       center_q4, dy_q4, inner_edge2,
                                       inner_solid2, outer_solid2, outer_edge2,
                                       inner_inverse, outer_inverse, color);
                }
            }
        }
        if (y_down != y_up && y_down >= top && y_down <= bottom) {
            int outer_left;
            int outer_right;
            dy_q4 = (y_down << GL30_Q4_SHIFT) - reference_cy_q4;
            outer_visible = circle_span_q4(outer_edge_q4, dy_q4,
                                           &outer_cursor_down, &outer_span_q4);
            inner_visible = circle_span_q4(inner_edge_q4, dy_q4,
                                           &inner_cursor_down, &inner_span_q4);
            if (outer_visible) {
                span_bounds(center_q4, outer_span_q4, &outer_left, &outer_right);
                if (inner_visible) {
                    int inner_left;
                    int inner_right;
                    span_bounds(center_q4, inner_span_q4,
                                &inner_left, &inner_right);
                    draw_ring_interval(outer_left, inner_left - 1, y_down,
                                       center_q4, dy_q4, inner_edge2,
                                       inner_solid2, outer_solid2, outer_edge2,
                                       inner_inverse, outer_inverse, color);
                    draw_ring_interval(inner_right + 1, outer_right, y_down,
                                       center_q4, dy_q4, inner_edge2,
                                       inner_solid2, outer_solid2, outer_edge2,
                                       inner_inverse, outer_inverse, color);
                } else {
                    draw_ring_interval(outer_left, outer_right, y_down,
                                       center_q4, dy_q4, inner_edge2,
                                       inner_solid2, outer_solid2, outer_edge2,
                                       inner_inverse, outer_inverse, color);
                }
            }
        }
    }
}

/* A rounded AA segment.  Projection uses a precomputed reciprocal and the
 * edge uses squared-distance interpolation, so no divide or sqrt is inside
 * the candidate-pixel loop. */
static void line(float x1, float y1, float x2, float y2,
                 float width, uint32_t c)
{
    float r = width * 0.5f;
    int32_t x1_q4 = q4_round(x1);
    int32_t y1_q4 = q4_round(y1);
    int32_t x2_q4 = q4_round(x2);
    int32_t y2_q4 = q4_round(y2);
    int32_t dx_q4 = x2_q4 - x1_q4;
    int32_t dy_q4 = y2_q4 - y1_q4;
    int32_t len2 = dx_q4 * dx_q4 + dy_q4 * dy_q4;
    int32_t radius_q4 = q4_round(r);
    int32_t inner_q4;
    int32_t outer_q4;
    uint32_t inner2;
    uint32_t outer2;
    uint32_t inverse;
    float inverse_len2;
    uint16_t color;
    int left = (int)floorf(fminf(x1, x2) - r - 1.0f);
    int right = (int)ceilf(fmaxf(x1, x2) + r + 1.0f);
    int top = (int)floorf(fminf(y1, y2) - r - 1.0f);
    int bottom = (int)ceilf(fmaxf(y1, y2) + r + 1.0f);
    int y;

    if (width <= 0.0f || radius_q4 <= 0) return;
    if (len2 == 0) {
        raster_disc(x1, y1, r, rgb(c));
        return;
    }
    if (left < 0) left = 0;
    if (right > 465) right = 465;
    if (top < 0) top = 0;
    if (bottom > 465) bottom = 465;
    if (left > right || top > bottom) return;

    inner_q4 = radius_q4 > GL30_Q4_HALF ? radius_q4 - GL30_Q4_HALF : 0;
    outer_q4 = radius_q4 + GL30_Q4_HALF;
    inner2 = (uint32_t)inner_q4 * (uint32_t)inner_q4;
    outer2 = (uint32_t)outer_q4 * (uint32_t)outer_q4;
    inverse = aa_inverse(outer2 - inner2);
    color = rgb(c);
    if (line_axis_aligned(x1_q4, y1_q4, x2_q4, y2_q4,
                          left, right, top, bottom,
                          inner2, outer2, inverse, color)) {
        return;
    }
    inverse_len2 = 1.0f / (float)len2;
    for (y = top; y <= bottom; ++y) {
        int x;
        int32_t py_q4 = y << GL30_Q4_SHIFT;
        for (x = left; x <= right; ++x) {
            int32_t px_q4 = x << GL30_Q4_SHIFT;
            int32_t rel_x = px_q4 - x1_q4;
            int32_t rel_y = py_q4 - y1_q4;
            int32_t dot = rel_x * dx_q4 + rel_y * dy_q4;
            uint32_t distance2;
            uint8_t alpha;

            if (dot <= 0) {
                distance2 = distance2_q8((int32_t)rel_x, (int32_t)rel_y);
            } else if (dot >= len2) {
                distance2 = distance2_q8((int32_t)(px_q4 - x2_q4),
                                         (int32_t)(py_q4 - y2_q4));
            } else {
                int32_t cross = rel_x * dy_q4 - rel_y * dx_q4;
                float cross_f = (float)cross;
                distance2 = (uint32_t)(cross_f * cross_f * inverse_len2 + 0.5f);
            }
            alpha = aa_fall(distance2, inner2, outer2, inverse);
            blend_pixel(x, y, color, alpha);
        }
    }
}

/* Partial arcs use a continuous incremental rotation.  Full rings use the
 * scanline path below, so this path is limited to icons and rounded caps. */
static void arc(float cx, float cy, float radius, float start, float sweep,
                float width, uint32_t color)
{
    float radius_abs;
    float step;
    float start_rad;
    float sweep_rad;
    float delta;
    float sine;
    float cosine;
    float step_sine;
    float step_cosine;
    float end_sine;
    float end_cosine;
    float px;
    float py;
    int segments;
    int i;

    if (sweep <= 0.0f || radius <= 0.0f || width <= 0.0f) return;
    if (sweep >= 360.0f) {
        raster_full_ring(cx, cy, radius, width, rgb(color));
        return;
    }
    radius_abs = fabsf(radius);
    /* Keep chord sagitta below roughly a quarter pixel.  The step is a
     * continuous float value, so large-radius arcs do not quantize to whole
     * degrees and small icons do not pay for unnecessary segments. */
    step = radius_abs > 1.0f ? sqrtf(2.0f / radius_abs) : 0.35f;
    if (step > 0.35f) step = 0.35f;
    sweep_rad = sweep * 0.01745329252f;
    segments = (int)ceilf(sweep_rad / step);
    if (segments < 1) segments = 1;
    delta = sweep_rad / (float)segments;
    start_rad = start * 0.01745329252f;
    sine = sinf(start_rad);
    cosine = cosf(start_rad);
    step_sine = sinf(delta);
    step_cosine = cosf(delta);
    end_sine = sinf(start_rad + sweep_rad);
    end_cosine = cosf(start_rad + sweep_rad);
    px = cx + radius * sine;
    py = cy - radius * cosine;
    for (i = 0; i < segments; ++i) {
        float next_sine;
        float next_cosine;
        float x;
        float y;

        if (i == segments - 1) {
            next_sine = end_sine;
            next_cosine = end_cosine;
        } else {
            next_sine = sine * step_cosine + cosine * step_sine;
            next_cosine = cosine * step_cosine - sine * step_sine;
        }
        x = cx + radius * next_sine;
        y = cy - radius * next_cosine;
        line(px, py, x, y, width, color);
        px = x;
        py = y;
        sine = next_sine;
        cosine = next_cosine;
    }
}

static void circle(float x, float y, float radius, uint32_t c)
{
    raster_disc(x, y, radius, rgb(c));
}

static void text(int x,int y,const char *str,const uint8_t *font,uint32_t color) {
    OLED_SetColor(rgb(color),0); OLED_SetFont(font); OLED_SetFontPosition(OLED_FONT_POS_TOP);
    OLED_DrawUTF8((int16_t)(x+offset_x),(int16_t)y,str);
}
static void centered(int y,const char *str,const uint8_t *font,uint32_t color) {
    OLED_SetFont(font); int w=OLED_GetUTF8Width(str); text((466-w)/2,y,str,font,color);
}
static void centered_at(int cx,int y,const char *str,const uint8_t *font,uint32_t color) {
    OLED_SetFont(font); int w=OLED_GetUTF8Width(str); text(cx-w/2,y,str,font,color);
}
static void calendar_day_at(int cx,int y,int day,uint32_t color) {
    char buf[12];
    snprintf(buf,sizeof(buf),"%d",day);
    centered_at(cx,y,buf,gl30_font_small,color);
}
static void centered_fit(int y,const char *str,const uint8_t *preferred,const uint8_t *fallback,
                         int max_width,uint32_t color) {
    const uint8_t *font=preferred;
    OLED_SetFont(font);
    int w=OLED_GetUTF8Width(str);
    if(w>max_width && fallback) {
        font=fallback;
        OLED_SetFont(font);
        w=OLED_GetUTF8Width(str);
    }
    /* Callers use short labels chosen to fit max_width even at the fallback
     * size. Do not mutate the strip clip window here: draw_scene owns it. */
    text((466-w)/2,y,str,font,color);
}
static void badge(int y,const char *str,uint32_t color) {
    OLED_SetFont(gl30_font_body); int w=OLED_GetUTF8Width(str)+36;
    if(w>370) w=370;
    OLED_SetColor(rgb(dim(color,0.15f)),0); OLED_DrawRBox((int16_t)((466-w)/2+offset_x),(int16_t)y,(uint16_t)w,48,20);
    centered(y+6,str,gl30_font_body,color);
}
static void settings_card(int y,const char *label,const char *value,bool selected,uint32_t accent) {
    uint32_t fill=selected?dim(accent,0.18f):0x11181d;
    OLED_SetColor(rgb(fill),0);
    /* Keep settings visually card-based, but pull the content away from the
     * round-panel edge.  Ordinary app pages deliberately keep their old
     * non-card visual language. */
    OLED_DrawRBox((int16_t)(78+offset_x),(int16_t)y,310,58,22);
    text(98,y+8,label,gl30_font_body,selected?WHITE:0xc5ccd1);
    if(value && value[0]) {
        OLED_SetFont(gl30_font_body); int w=OLED_GetUTF8Width(value);
        text(368-w,y+8,value,gl30_font_body,selected?accent:MUTED);
    }
}
/* One rounded duotone icon family, in a 24-unit design grid. All sizes use
 * the same paths; depth affects color only. White structure + one app accent
 * replaces the old multicolored discs, dark borders and ambiguous capsule.
 * Filled spans and connected arc strips keep the icon path out of the slow generic
 * per-pixel distance loop. No allocation, bitmap resampling, or frame cache. */
static void icon_segment(float x0,float y0,float x1,float y1,float width,uint32_t color)
{
    float dx=x1-x0,dy=y1-y0,length=sqrtf(dx*dx+dy*dy),half=width*0.5f;
    if(length>0.01f) {
        float nx=-dy*half/length,ny=dx*half/length;
        int16_t ax=(int16_t)lroundf(x0+nx+offset_x),ay=(int16_t)lroundf(y0+ny);
        int16_t bx=(int16_t)lroundf(x1+nx+offset_x),by=(int16_t)lroundf(y1+ny);
        int16_t cx=(int16_t)lroundf(x1-nx+offset_x),cy=(int16_t)lroundf(y1-ny);
        int16_t dxp=(int16_t)lroundf(x0-nx+offset_x),dyp=(int16_t)lroundf(y0-ny);
        OLED_SetColor(rgb(color),0);
        OLED_DrawFilledTriangle(ax,ay,bx,by,cx,cy);
        OLED_DrawFilledTriangle(ax,ay,cx,cy,dxp,dyp);
    }
    raster_disc(x0,y0,half,rgb(color));
    raster_disc(x1,y1,half,rgb(color));
}
static void icon_curve(float x,float y,float radius,float start,float sweep,
                       float width,uint32_t color)
{
    if(sweep>=359.5f) {
        raster_full_ring(x,y,radius,width,rgb(color));
        return;
    }
    /* Fill a connected annular strip: adjacent midpoint circles leave
     * pinholes on diagonals when stacked to make a thick arc. Chord sagitta
     * stays below 0.2 px; trig is evaluated per arc, never per pixel. */
    float start_rad=start*0.01745329252f,sweep_rad=sweep*0.01745329252f;
    float outer=radius+width*0.5f,inner=radius-width*0.5f;
    if(inner<0) inner=0;
    float step=sqrtf(1.6f/(outer>1?outer:1));
    if(step>0.35f) step=0.35f;
    int segments=(int)ceilf(sweep_rad/step);
    if(segments<1) segments=1;
    float delta=sweep_rad/segments,cs=cosf(delta),sn=sinf(delta);
    float u=sinf(start_rad),v=-cosf(start_rad);
    float end_u=sinf(start_rad+sweep_rad),end_v=-cosf(start_rad+sweep_rad);
    OLED_SetColor(rgb(color),0);
    for(int i=0;i<segments;i++) {
        float nu=i+1==segments?end_u:u*cs-v*sn;
        float nv=i+1==segments?end_v:v*cs+u*sn;
        int16_t ax=(int16_t)lroundf(x+outer*u+offset_x),ay=(int16_t)lroundf(y+outer*v);
        int16_t bx=(int16_t)lroundf(x+outer*nu+offset_x),by=(int16_t)lroundf(y+outer*nv);
        int16_t cx=(int16_t)lroundf(x+inner*nu+offset_x),cy=(int16_t)lroundf(y+inner*nv);
        int16_t dx=(int16_t)lroundf(x+inner*u+offset_x),dy=(int16_t)lroundf(y+inner*v);
        OLED_DrawFilledTriangle(ax,ay,bx,by,cx,cy);
        OLED_DrawFilledTriangle(ax,ay,cx,cy,dx,dy);
        u=nu;v=nv;
    }
    raster_disc(x+radius*sinf(start_rad),y-radius*cosf(start_rad),width*0.5f,rgb(color));
    raster_disc(x+radius*end_u,y+radius*end_v,width*0.5f,rgb(color));
}
static void icon_box(float x,float y,float w,float h,float r,float width,uint32_t color)
{
    icon_segment(x+r,y,x+w-r,y,width,color);
    icon_segment(x+w,y+r,x+w,y+h-r,width,color);
    icon_segment(x+w-r,y+h,x+r,y+h,width,color);
    icon_segment(x,y+h-r,x,y+r,width,color);
    icon_curve(x+r,y+r,r,270,90,width,color);
    icon_curve(x+w-r,y+r,r,0,90,width,color);
    icon_curve(x+w-r,y+h-r,r,90,90,width,color);
    icon_curve(x+r,y+h-r,r,180,90,width,color);
}
static void app_icon(int app,float x,float y,float diameter_px,float depth)
{
    if(app<0 || app>=GL30_APP_COUNT || diameter_px<4.0f) return;
    /* Extent includes the common 1.7-unit stroke; two screen pixels reserve
     * rounding/AA. Diameter continues to mean visible glyph, not a tile. */
    static const float extent[GL30_APP_COUNT]={21.7f,20.7f,21.7f,21.7f,23.2f,22.8f,20.72f,21.7f,21.9f};
    static const float origin[GL30_APP_COUNT][2]={{12,12},{12.5f,12},{12,12},{12,12},
        {11.75f,10},{12,11.45f},{12,12},{12,12},{12,10.9f}};
    float scale=(diameter_px-2.0f)/extent[app];
    float width=1.7f*scale;
    if(width<1.5f) width=1.5f;
    depth=bound(depth,0.0f,1.0f);
    uint32_t ink=dim(0xe6edf3,0.62f+0.38f*depth);
    uint32_t accent=dim(app_colors[app],0.72f+0.28f*depth);
#define IX(a) (x+((a)-origin[app][0])*scale)
#define IY(b) (y+((b)-origin[app][1])*scale)
#define IL(a,b,c,d,col) icon_segment(IX(a),IY(b),IX(c),IY(d),width,(col))
#define IA(a,b,r,start,sweep,col) icon_curve(IX(a),IY(b),(r)*scale,(start),(sweep),width,(col))
#define IB(a,b,w,h,r,col) icon_box(IX(a),IY(b),(w)*scale,(h)*scale,(r)*scale,width,(col))
#define ID(a,b,r,col) raster_disc(IX(a),IY(b),(r)*scale,rgb(col))
#define IT(a,b,c,d,e,f,col) do { OLED_SetColor(rgb(col),0); \
    OLED_DrawFilledTriangle((int16_t)lroundf(IX(a)+offset_x),(int16_t)lroundf(IY(b)), \
        (int16_t)lroundf(IX(c)+offset_x),(int16_t)lroundf(IY(d)), \
        (int16_t)lroundf(IX(e)+offset_x),(int16_t)lroundf(IY(f))); } while(0)
    switch(app) {
    case GL30_TIMER:
        /* Countdown is an hourglass, deliberately not another stopwatch. */
        IL(5,2,19,2,ink); IL(5,22,19,22,ink);
        IL(6,3,6,6,ink); IL(6,6,18,18,ink); IL(18,18,18,21,ink);
        IL(18,3,18,6,ink); IL(18,6,6,18,ink); IL(6,18,6,21,ink);
        IT(8.7f,6,15.3f,6,12,9.3f,accent);
        IT(8.3f,19,15.7f,19,12,15.3f,accent);
        break;
    case GL30_VOLUME:
        IL(3,9,7.5f,9,ink); IL(7.5f,9,13,4.5f,ink);
        IL(13,4.5f,13,19.5f,ink); IL(13,19.5f,7.5f,15,ink);
        IL(7.5f,15,3,15,ink); IL(3,15,3,9,ink);
        IA(12,12,6,50,80,accent); IA(12,12,10,50,80,accent);
        break;
    case GL30_STOPWATCH:
        IL(9,2,15,2,ink); IL(12,2,12,5,ink);
        IL(18.7f,5.2f,20.5f,3.4f,ink);
        IA(12,13.5f,8.5f,0,360,ink);
        IL(12,13.5f,12,8.5f,accent); ID(12,13.5f,1.0f,accent);
        break;
    case GL30_ALARM:
        IA(5,5,3,270,140,ink); IA(19,5,3,310,140,ink);
        IA(12,12,7.5f,0,360,ink);
        IL(7,19,5,22,ink); IL(17,19,19,22,ink);
        IL(12,12,12,8,accent); IL(12,12,16,14,accent);
        break;
    case GL30_WEATHER:
        /* Sun behind one continuous cloud outline, not overlapping discs. */
        IA(7,7,3.2f,215,245,accent);
        IL(7,1,7,2,accent); IL(1,7,2,7,accent);
        IL(2.1f,2.1f,3.0f,3.0f,accent); IL(11,3,12,2,accent);
        IA(14,12,5,270,180,ink); IA(19,15.5f,3.5f,0,180,ink);
        IL(19,19,6,19,ink); IA(6,15,4,180,180,ink); IL(6,11,9,11,ink);
        break;
    case GL30_FEEL:
        /* Pointing finger, bent thumb and palm; two feedback arcs above it.
         * A recognisable hand replaces the old skin-coloured capsule. */
        IA(11.7f,8,4.3f,300,120,accent); IA(11.7f,8,7.1f,300,120,accent);
        IL(10,8,10,16,ink); IA(11.7f,8,1.7f,270,180,ink);
        IL(13.4f,8,13.4f,12,ink); IL(13.4f,12,15.6f,11.7f,ink);
        IL(15.6f,11.7f,18,12.4f,ink); IL(18,12.4f,20,14,ink);
        IL(20,14,20,18,ink); IL(20,18,18,22,ink); IL(18,22,10,22,ink);
        IL(10,22,4,16,ink); IL(4,16,4,14,ink); IL(4,14,6,13.6f,ink);
        IL(6,13.6f,10,16,ink);
        break;
    case GL30_SETTINGS: {
        /* Six real teeth with recessed valleys, not a ring of flower petals.
         * Fixed vertices avoid trigonometry for each tooth on every frame. */
        static const float gear[][2]={
            {10.210f,4.820f},{9.629f,2.491f},{14.371f,2.491f},{13.790f,4.820f},
            {17.323f,6.860f},{19.050f,5.192f},{21.421f,9.629f},{19.114f,10.210f},
            {19.114f,13.790f},{21.421f,14.371f},{19.050f,18.808f},{17.323f,17.140f},
            {13.790f,19.180f},{14.371f,21.509f},{9.629f,21.509f},{10.210f,19.180f},
            {6.677f,17.140f},{4.950f,18.808f},{2.579f,14.371f},{4.886f,13.790f},
            {4.886f,10.210f},{2.579f,9.629f},{4.950f,5.192f},{6.677f,6.860f}};
        for(unsigned i=0;i<sizeof(gear)/sizeof(*gear);i++) {
            unsigned j=(i+1U)%(sizeof(gear)/sizeof(*gear));
            IL(gear[i][0],gear[i][1],gear[j][0],gear[j][1],ink);
        }
        IA(12,12,3.2f,0,360,accent);
        break;
    }
    case GL30_CALENDAR:
        IB(3,5,18,17,2.5f,ink);
        IL(7,2,7,7,ink); IL(17,2,17,7,ink); IL(3.5f,10,20.5f,10,ink);
        /* One large date mark survives the rear row; no tiny 31-cell grid. */
        IL(9,14,16,14,accent); IL(16,14,11.8f,19,accent);
        break;
    case GL30_LIGHTING:
        IA(12,9,6,225,270,ink);
        IL(7.757f,13.243f,9,16,ink); IL(9,16,9,18,ink);
        IL(9,18,15,18,ink); IL(15,18,15,16,ink); IL(15,16,16.243f,13.243f,ink);
        IL(10,21,14,21,ink); IL(12,11.5f,12,15.5f,accent);
        IL(12,0.8f,12,1.5f,accent); IL(3,4,4.2f,4.8f,accent);
        IL(19.8f,4.8f,21,4,accent); IL(2,10,3,10,accent); IL(21,10,22,10,accent);
        break;
    }
#undef IT
#undef ID
#undef IB
#undef IA
#undef IL
#undef IY
#undef IX
}
static void get_date(const gl30_model *s,struct tm *result) {
    /* The product demo uses Beijing time; the API always accepts real Unix time. */
    time_t seconds=(time_t)(s->epoch_seconds+s->elapsed_ms/1000+8*3600);
#ifdef _WIN32
    gmtime_s(result,&seconds);
#else
    gmtime_r(&seconds,result);
#endif
}
static void photo(void) {
    OLED_BlitRGB565(offset_x,0,466,466,gl30_summit);
}
static void home(const gl30_model *s) {
    char buf[80]; struct tm date; get_date(s,&date); photo();
    if(s->epoch_seconds) {
        snprintf(buf,sizeof(buf),"%02d:%02d",date.tm_hour,date.tm_min);
        centered(68,buf,gl30_font_digits,WHITE);
        if(is_zh(s)) {
            static const char *week[]={"日","一","二","三","四","五","六"};
            snprintf(buf,sizeof(buf),"周%s · %d/%d",week[date.tm_wday],date.tm_mon+1,date.tm_mday);
            centered_fit(150,buf,gl30_font_body,NULL,340,0xdce5ed);
        } else {
            static const char *week[]={"SUN","MON","TUE","WED","THU","FRI","SAT"};
            snprintf(buf,sizeof(buf),"%s · %d/%d",week[date.tm_wday],date.tm_mon+1,date.tm_mday);
            centered_fit(150,buf,title_font(s),gl30_font_body,340,0xdce5ed);
        }
    } else {
        centered(68,"--:--",gl30_font_digits,WHITE);
        centered_fit(150,tr(s,"SET TIME","待校时"),title_font(s),gl30_font_body,320,MUTED);
    }
    /* Keep the wallpaper visually dominant: weather stays as a light text row
     * instead of a large icon/badge over the mountain. */
    bool timer_active=s->phase==GL30_RUNNING || s->phase==GL30_PAUSED;
    centered_fit(timer_active?350:382,tr(s,"18° · SUNNY","18° · 晴"),gl30_font_body,NULL,300,0xdce5ed);
    if(timer_active) {
        char t[32]; gl30_format_time(s->remaining_ms,true,t,sizeof(t));
        snprintf(buf,sizeof(buf),"%s %s",s->phase==GL30_RUNNING?tr(s,"TIMER","计时中"):tr(s,"PAUSED","暂停"),t);
        centered_fit(386,buf,gl30_font_body,NULL,320,0xffd166);
    }
}
static void menu(const gl30_model *s) {
    uint64_t section_us=gl30_platform_time_us();
    render_profile=(gl30_render_profile){0};
    /* Let the circular motion own the page. The old English header cost a
     * surprisingly large fraction of every animation frame and competed with
     * the icons for attention; the selected app name below is sufficient. */
    /* A subtle ellipse is the depth cue. The front-facing icon centres bow
     * out a little farther to separate wide outlines from their neighbours;
     * the side/rear positions stay safely inside the circular panel. */
    const int orbit_radius_x=178;
    OLED_SetColor(rgb(0x182329),0);
    OLED_DrawEllipse((int16_t)(233+offset_x),171,(uint16_t)orbit_radius_x,92);
    /* An ellipse in perspective: back objects are smaller and drawn first. */
    typedef struct { int id; float x,y,diameter,depth; } item;
    item items[GL30_MENU_APP_COUNT];
#if GL30_WRAP_MENU_PHASE
    /* Keep carousel phase bounded so libm range reduction stays cheap. */
    float menu_phase=s->menu_visual;
    int32_t revolutions=(int32_t)(menu_phase/(float)GL30_MENU_APP_COUNT);
    menu_phase-=(float)revolutions*(float)GL30_MENU_APP_COUNT;
    if(menu_phase>GL30_MENU_APP_COUNT/2.0f) menu_phase-=GL30_MENU_APP_COUNT;
    else if(menu_phase< -GL30_MENU_APP_COUNT/2.0f) menu_phase+=GL30_MENU_APP_COUNT;
#else
    float menu_phase=s->menu_visual;
#endif
    /* Eight independent sin/cos pairs cost several milliseconds on ESP32-S3.
     * Evaluate the fractional phase once, then rotate by the fixed 45 degree
     * menu step.  Error accumulation over only eight items is negligible. */
    const float step_sin=0.70710678118f,step_cos=0.70710678118f;
    float phase=-menu_phase*0.78539816339f;
    float item_sin=sinf(phase),item_cos=cosf(phase);
    for(int i=0;i<GL30_MENU_APP_COUNT;i++) {
        float z=(item_cos+1.0f)*0.5f;
        float z2=z*z, z4=z2*z2, z8=z4*z4, z14=z8*z4*z2;
        /* The 466 px, 1.32 inch active diameter is 13.90 px/mm. The actual
         * glyph extent is 14 mm at the front, about 6 mm at its two snapped
         * neighbours, and at least 3 mm at the rear. Normalize each icon's
         * geometry, so the old tile diameter cannot silently change its size. */
        /* Rear designs reserve 0.2 mm for raster rounding/stroke margins;
         * their visible extents therefore remain at least 3 mm. */
        float diameter_mm=3.2f+2.17f*z+8.63f*z14;
        float diameter_px=diameter_mm*(466.0f/(1.32f*25.4f));
        items[i]=(item){i,233+(orbit_radius_x+24.0f*item_cos)*item_sin,
                         171+92*item_cos,diameter_px,z};
        float next_sin=item_sin*step_cos+item_cos*step_sin;
        float next_cos=item_cos*step_cos-item_sin*step_sin;
        item_sin=next_sin; item_cos=next_cos;
    }
    for(int i=0;i<GL30_MENU_APP_COUNT-1;i++) for(int j=i+1;j<GL30_MENU_APP_COUNT;j++) if(items[i].depth>items[j].depth) { item t=items[i]; items[i]=items[j]; items[j]=t; }
    render_profile.menu_layout_us=(uint32_t)(gl30_platform_time_us()-section_us);
    for(int i=0;i<GL30_MENU_APP_COUNT;i++) {
        item p=items[i];
        section_us=gl30_platform_time_us();
        /* All apps share a clean floating outline; no capsule spotlight. */
        render_profile.menu_disc_us+=(uint32_t)(gl30_platform_time_us()-section_us);
        section_us=gl30_platform_time_us();
        render_profile.menu_ring_us+=(uint32_t)(gl30_platform_time_us()-section_us);
        section_us=gl30_platform_time_us();
        app_icon(p.id,p.x,p.y,p.diameter,p.depth);
        render_profile.menu_icon_us+=(uint32_t)(gl30_platform_time_us()-section_us);
    }
    section_us=gl30_platform_time_us();
    if(menu_render_mode<=1U) centered_fit(382,app_name(s,s->menu_index),title_font(s),gl30_font_body,330,WHITE);
    circle(233,365,3.2f,BLUE);
    render_profile.menu_label_us=(uint32_t)(gl30_platform_time_us()-section_us);
}
static void value_rings(const gl30_model *s) {
    float turns=gl30_model_angle(s)/360;
    for(int i=0;i<6;i++) {
        float radius=216-i*7.5f;
        arc(233,233,radius,0,360,2,dim(colors[i],0.09f));
        float fill=bound(turns-i,0,1);
        if(fill>0) arc(233,233,radius,0,fill*360,3.5f,colors[i]);
    }
    int index=(int)ceilf(turns)-1; if(index<0) index=0; if(index>5) index=5;
    float a=fmodf(turns,1)*6.2831853f,r=216-index*7.5f;
    circle(233+r*sinf(a),233-r*cosf(a),4.5f,BLUE);
}
static void timer(const gl30_model *s) {
    char t[32]; value_rings(s);
    centered(104,tr(s,"TIMER","计时器"),gl30_font_body,MUTED);
    app_icon(GL30_TIMER,233,170,64.0f,1.0f);
    gl30_format_time(s->remaining_ms,true,t,sizeof(t));
    centered(214,t,gl30_font_digits,WHITE);
    const char *label=s->phase==GL30_RUNNING?tr(s,"Running","运行中"):s->phase==GL30_PAUSED?tr(s,"Paused","暂停"):s->phase==GL30_DONE?tr(s,"Done","计时结束"):tr(s,"Ready","准备");
    centered(305,label,gl30_font_body,s->phase==GL30_DONE?BLUE:MUTED);
}
static void volume(const gl30_model *s) {
    char buf[16]; value_rings(s);
    centered(104,tr(s,"VOLUME","音量"),gl30_font_body,MUTED);
    app_icon(GL30_VOLUME,233,178,86.0f,s->muted?0.45f:1.0f);
    snprintf(buf,sizeof(buf),"%d%%",s->volume); centered(248,buf,gl30_font_digits,WHITE);
    centered(338,s->muted?tr(s,"Muted","静音"):tr(s,"On","开启"),gl30_font_body,s->muted?MUTED:BLUE);
}
static void stopwatch(const gl30_model *s) {
    char buf[40]; centered(72,tr(s,"STOPWATCH","秒表"),gl30_font_body,MUTED);
    arc(233,233,210,0,360,2,0x1c2428);
    arc(233,233,210,0,(s->stopwatch_ms%60000)*0.006f,4,colors[2]);
    app_icon(GL30_STOPWATCH,233,158,72.0f,1.0f);
    gl30_format_time(s->stopwatch_ms,false,buf,sizeof(buf)); centered(224,buf,gl30_font_digits,WHITE);
    if(s->stopwatch_show_centis) { snprintf(buf,sizeof(buf),".%02u",(unsigned)(s->stopwatch_ms/10%100)); centered(296,buf,title_font(s),colors[2]); }
    centered(356,s->stopwatch_running?tr(s,"Running","运行中"):tr(s,"Paused","暂停"),gl30_font_body,s->stopwatch_running?colors[2]:MUTED);
}
static void lighting(const gl30_model *s) {
    centered(42,tr(s,"Lighting","灯效"),title_font(s),WHITE);
    float phase=(s->now_ms%4000)*6.2831853f/4000;
    uint32_t c=dim(colors[s->light_color],s->light_brightness/100.0f*(s->light_effect==2?0.25f+0.375f*(1-cosf(phase)):1));
    float start=s->light_effect==3?fmodf(s->now_ms/30.0f,360):0;
    arc(233,150,42,0,360,8,0x161e22);
    arc(233,150,42,start,s->light_effect==3?90.0f:360.0f,8,c);
    circle(233+42*sinf(start*0.01745329252f),150-42*cosf(start*0.01745329252f),4,s->light_brightness?BLUE:0x161e22);
    char brightness[16]; snprintf(brightness,sizeof(brightness),"%d%%",s->light_brightness);
    const char *values[]={effect_name(s,s->light_effect),color_name(s,s->light_color),brightness};
    const char *fields_en[]={"Effect","Color","Level"};
    const char *fields_zh[]={"效果","颜色","亮度"};
    const char **fields=is_zh(s)?fields_zh:fields_en;
    for(int i=0;i<3;i++) {
        int y=220+i*68;
        settings_card(y,fields[i],values[i],s->light_field==i,s->light_editing?colors[2]:BLUE);
    }
}
static void weather(const gl30_model *s) {
    photo();
    centered_fit(52,tr(s,"Weather","天气"),title_font(s),gl30_font_body,320,WHITE);
    circle(233,154,63,dim(0x279cf4,0.045f));
    app_icon(GL30_WEATHER,233,154,96.0f,1.0f);
    char temp[16]; snprintf(temp,sizeof(temp),"%d",s->weather_fahrenheit?64:18);
    /* The large digit subset has no degree glyph. Reserve its real width and
     * draw the unit with the complete body asset rather than a blank space. */
    OLED_SetFont(gl30_font_digits); int number_width=OLED_GetUTF8Width(temp);
    OLED_SetFont(gl30_font_body); int unit_width=OLED_GetUTF8Width("°");
    int number_x=(466-number_width-unit_width-2)/2;
    text(number_x,230,temp,gl30_font_digits,WHITE);
    text(number_x+number_width+2,230,"°",gl30_font_body,WHITE);
    centered_fit(312,tr(s,"Sunny · Breeze","晴 · 微风"),title_font(s),gl30_font_body,340,WHITE);
    centered_fit(366,tr(s,"Humidity 42%","湿度 42%"),gl30_font_body,NULL,340,0xdce5ed);
}
static void calendar(const gl30_model *s) {
    struct tm date; get_date(s,&date); char buf[40];
    centered_fit(30,tr(s,"Calendar","日历"),title_font(s),gl30_font_body,300,WHITE);
    if(!s->epoch_seconds) {
        app_icon(GL30_CALENDAR,233,190,70.0f,1.0f);
        centered_fit(280,tr(s,"Set time","待校时"),title_font(s),gl30_font_body,300,MUTED);
        return;
    }

    int year=date.tm_year+1900;
    int month=date.tm_mon;
    static const char *months_en[]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
    if(is_zh(s)) {
        snprintf(buf,sizeof(buf),"%d年%d月",year,month+1);
        centered_fit(88,buf,gl30_font_body,NULL,280,app_colors[GL30_CALENDAR]);
    } else {
        snprintf(buf,sizeof(buf),"%s %d",months_en[month],year);
        centered_fit(82,buf,title_font(s),gl30_font_body,280,app_colors[GL30_CALENDAR]);
    }

    static const char *week_en_sun[]={"S","M","T","W","T","F","S"};
    static const char *week_en_mon[]={"M","T","W","T","F","S","S"};
    static const char *week_zh_sun[]={"日","一","二","三","四","五","六"};
    static const char *week_zh_mon[]={"一","二","三","四","五","六","日"};
    const char **week=is_zh(s)?(s->calendar_monday_first?week_zh_mon:week_zh_sun)
                              :(s->calendar_monday_first?week_en_mon:week_en_sun);
    const int x0=92, dx=47;
    for(int c=0;c<7;c++) centered_at(x0+c*dx,143,week[c],gl30_font_small,MUTED);
    OLED_SetColor(rgb(0x182228),0);
    OLED_DrawLine((int16_t)(82+offset_x),174,(int16_t)(384+offset_x),174);

    int first_wday=(date.tm_wday-((date.tm_mday-1)%7)+7)%7;
    int first_col=s->calendar_monday_first?(first_wday+6)%7:first_wday;
    static const int month_days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int days=month_days[month];
    bool leap=(year%4==0 && (year%100!=0 || year%400==0));
    if(month==1 && leap) days=29;

    for(int day=1;day<=days;day++) {
        int slot=first_col+day-1;
        int row=slot/7, col=slot%7;
        int y=183+row*39;
        int cx=x0+col*dx;
        if(day==date.tm_mday) {
            circle((float)cx,(float)(y+12),16.0f,dim(app_colors[GL30_CALENDAR],0.82f));
            calendar_day_at(cx,y,day,WHITE);
        } else {
            calendar_day_at(cx,y,day,0xc5ccd1);
        }
    }
}
static void settings_root(const gl30_model *s) {
    static const char *labels_en[]={"Display","Lighting","Language","Guide","Functions"};
    static const char *labels_zh[]={"显示","灯效","语言","教程","功能"};
    static const uint32_t accents[]={0x74c8ff,0xffcf66,0x83dce8,0x97dda1,0xcaa8ff};
    const char **labels=is_zh(s)?labels_zh:labels_en;
    centered_fit(22,tr(s,"Settings","系统设置"),title_font(s),gl30_font_body,300,WHITE);
    app_icon(GL30_SETTINGS,233,82,58.0f,1.0f);
    for(int i=0;i<5;i++) settings_card(132+i*58,labels[i],NULL,s->settings_item==i,accents[i]);
}
static void settings_display(const gl30_model *s) {
    char b[20]; snprintf(b,sizeof(b),"%d%%",s->screen_brightness);
    centered_fit(44,tr(s,"Display","显示与亮度"),title_font(s),gl30_font_body,320,WHITE);
    centered(168,b,gl30_font_digits,WHITE);
    OLED_SetColor(rgb(0x17242d),0); OLED_DrawRBox((int16_t)(93+offset_x),292,280,24,12);
    int fill=(int)(280*s->screen_brightness/100.0f);
    OLED_SetColor(rgb(BLUE),0); OLED_DrawRBox((int16_t)(93+offset_x),292,(uint16_t)fill,24,12);
}
static void settings_language(const gl30_model *s) {
    centered_fit(44,tr(s,"Language","语言"),title_font(s),gl30_font_body,320,WHITE);
    settings_card(160,"English",NULL,s->language==GL30_LANGUAGE_ENGLISH,0x83dce8);
    settings_card(230,"中文",NULL,s->language==GL30_LANGUAGE_CHINESE,0x83dce8);
    centered_fit(330,tr(s,"ROTATE · BACK","旋转选择 · 返回"),gl30_font_body,NULL,320,MUTED);
}
static void settings_help(const gl30_model *s) {
    static const char *title_en[]={"Rotate","Press","Double press","Long press"};
    static const char *desc_en[]={"Select / adjust","Confirm / pause","Back","App settings"};
    static const char *title_zh[]={"旋转","单击","双击","长按"};
    static const char *desc_zh[]={"选择 / 调整","确认 / 暂停","返回","当前功能设置"};
    const char **title=is_zh(s)?title_zh:title_en;
    const char **desc=is_zh(s)?desc_zh:desc_en;
    centered_fit(44,tr(s,"Controls","操作教程"),title_font(s),gl30_font_body,320,WHITE);
    centered_fit(168,title[s->settings_item],title_font(s),gl30_font_body,320,BLUE);
    centered_fit(252,desc[s->settings_item],gl30_font_body,NULL,340,WHITE);
    for(int i=0;i<4;i++) circle(191.0f+i*28.0f,366.0f,4.5f,i==s->settings_item?BLUE:0x263038);
}
static void settings_function(const gl30_model *s) {
    char value[48]={0}; const char *label=tr(s,"Setting","设置");
    switch(s->settings_function) {
    case GL30_TIMER: label=tr(s,"Step","步进"); snprintf(value,sizeof(value),is_zh(s)?"%d 分钟":"%d min",s->timer_step_minutes); break;
    case GL30_VOLUME: label=tr(s,"Step","步进"); snprintf(value,sizeof(value),"%d%%",s->volume_step_percent); break;
    case GL30_STOPWATCH: label=tr(s,"1/100 s","百分秒"); snprintf(value,sizeof(value),"%s",s->stopwatch_show_centis?tr(s,"On","开启"):tr(s,"Off","关闭")); break;
    case GL30_ALARM: label=tr(s,"Reminder","提醒"); snprintf(value,sizeof(value),"%s",s->alarm_enabled?tr(s,"On","开启"):tr(s,"Off","关闭")); break;
    case GL30_WEATHER: label=tr(s,"Units","单位"); snprintf(value,sizeof(value),"%s",is_zh(s)?(s->weather_fahrenheit?"华氏":"摄氏"):(s->weather_fahrenheit?"°F":"°C")); break;
    case GL30_FEEL: {
        static const char *feel_en[]={"Soft","Standard","Crisp"};
        static const char *feel_zh[]={"轻柔","标准","清晰"};
        label=tr(s,"Mode","手感"); snprintf(value,sizeof(value),"%s",is_zh(s)?feel_zh[s->feel_profile]:feel_en[s->feel_profile]); break;
    }
    case GL30_CALENDAR: label=tr(s,"Week","周起始"); snprintf(value,sizeof(value),"%s",s->calendar_monday_first?tr(s,"Monday","周一"):tr(s,"Sunday","周日")); break;
    default: break;
    }
    centered_fit(44,tr(s,"Functions","功能设置"),title_font(s),gl30_font_body,320,WHITE);
    centered_fit(126,app_name(s,s->settings_function),title_font(s),gl30_font_body,320,app_colors[s->settings_function]);
    settings_card(246,label,value,true,s->settings_editing?colors[2]:BLUE);
}
static void settings(const gl30_model *s) {
    if(s->settings_page==GL30_SETTINGS_ROOT) settings_root(s);
    else if(s->settings_page==GL30_SETTINGS_DISPLAY) settings_display(s);
    else if(s->settings_page==GL30_SETTINGS_LIGHT) lighting(s);
    else if(s->settings_page==GL30_SETTINGS_LANGUAGE) settings_language(s);
    else if(s->settings_page==GL30_SETTINGS_HELP) settings_help(s);
    else settings_function(s);
}
static void utility(const gl30_model *s) {
    if(s->app==GL30_SETTINGS) { settings(s); return; }
    centered_fit(58,app_name(s,s->app),title_font(s),gl30_font_body,330,WHITE);
    arc(233,235,198,0,360,2,0x172127);
    circle(233,174,58,dim(app_colors[s->app],0.055f));
    app_icon(s->app,233,174,90.0f,1.0f);
    if(s->app==GL30_ALARM) centered_fit(290,s->alarm_enabled?tr(s,"On","开启"):tr(s,"Off","关闭"),title_font(s),gl30_font_body,300,s->alarm_enabled?BLUE:MUTED);
    else {
        static const char *feel_en[]={"Soft","Standard","Crisp"};
        static const char *feel_zh[]={"轻柔","标准","清晰"};
        centered_fit(290,is_zh(s)?feel_zh[s->feel_profile]:feel_en[s->feel_profile],title_font(s),gl30_font_body,300,BLUE);
    }
}
void gl30_draw_scene(const gl30_model *s,int16_t x_offset,int16_t clip_x,uint16_t clip_width) {
    offset_x=x_offset;
    OLED_SetClipWindow(clip_x,0,clip_width,OLED_GetHeight());
    OLED_SetColor(rgb(WHITE),0); OLED_SetDrawMode(OLED_DRAW_SET);
    OLED_SetBackgroundMode(OLED_BG_TRANSPARENT);
    if(s->off) return;
    if(s->page==GL30_HOME) home(s);
    else if(s->page==GL30_MENU) menu(s);
    else switch(s->app) {
    case GL30_TIMER: timer(s); break;
    case GL30_VOLUME: volume(s); break;
    case GL30_STOPWATCH: stopwatch(s); break;
    case GL30_WEATHER: weather(s); break;
    case GL30_CALENDAR: calendar(s); break;
    case GL30_LIGHTING: lighting(s); break;
    default: utility(s); break;
    }
    if(s->endstop) badge(383,s->endstop<0?tr(s,"Minimum","已到下限"):tr(s,"Maximum","已到上限"),BLUE);
    if(s->fault) badge(383,"FAULT",0xff677b);
    OLED_SetColor(rgb(WHITE),0);
}
