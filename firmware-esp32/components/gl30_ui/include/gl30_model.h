#ifndef GL30_MODEL_H
#define GL30_MODEL_H
#include <stdbool.h>
#include <stdint.h>
#include "gl30_motor_feedback.h"
#define GL30_APP_COUNT 9
#define GL30_MENU_APP_COUNT 8
#define GL30_TIMER_MAX_MS 21600000U
typedef enum { GL30_HOME, GL30_MENU, GL30_APP } gl30_page;
typedef enum { GL30_TIMER, GL30_VOLUME, GL30_STOPWATCH, GL30_ALARM,
    GL30_WEATHER, GL30_FEEL, GL30_SETTINGS, GL30_CALENDAR, GL30_LIGHTING } gl30_app;
typedef enum { GL30_SETTING, GL30_RUNNING, GL30_PAUSED, GL30_DONE } gl30_phase;
typedef enum { GL30_LANGUAGE_ENGLISH=0, GL30_LANGUAGE_CHINESE=1 } gl30_language;
typedef enum { GL30_SETTINGS_ROOT, GL30_SETTINGS_DISPLAY, GL30_SETTINGS_LIGHT,
    GL30_SETTINGS_LANGUAGE, GL30_SETTINGS_HELP, GL30_SETTINGS_FUNCTION } gl30_settings_page;
typedef struct {
    gl30_page page;
    gl30_app app;
    gl30_phase phase;
    int menu_index;
    int32_t menu_position;
    float menu_visual;
    int32_t menu_anim_from_q8, menu_anim_to_q8;
    uint32_t menu_anim_started, menu_epoch, motor_menu_session;
    bool menu_animating, motor_menu_valid;
    int motor_menu_origin;
    uint32_t remaining_ms, duration_ms, now_ms, endstop_until;
    uint64_t elapsed_ms, epoch_seconds, stopwatch_ms;
    bool stopwatch_running, muted, off, fault;
    /* Display-only FAST status, independent of the UI's local fault policy. */
    gl30_motor_feedback_state motor_state;
    uint32_t motor_fault_bits;
    int volume, endstop;
    int light_effect, light_color, light_brightness, light_field;
    bool light_editing;
    int screen_brightness;
    gl30_language language;
    gl30_settings_page settings_page;
    int settings_item;
    gl30_app settings_function;
    gl30_app settings_return_app;
    bool settings_from_app;
    bool settings_editing;
    int timer_step_minutes;
    int volume_step_percent;
    bool stopwatch_show_centis;
    bool alarm_enabled;
    bool weather_fahrenheit;
    int feel_profile;
    bool calendar_monday_first;
} gl30_model;
void gl30_model_init(gl30_model *s,uint32_t now_ms,uint64_t epoch_seconds);
void gl30_model_tick(gl30_model *s,uint32_t now_ms);
/* Pure display projection: never confirms input, changes pages, or emits DONE. */
void gl30_model_view(const gl30_model *s,uint32_t display_ms,gl30_model *view);
void gl30_model_rotate(gl30_model *s,int steps);
/* One coherent, confirmed detent sample; q retains STM32's hysteresis.
 * Epoch rejects samples captured before this menu entry. */
bool gl30_model_motor_menu(gl30_model *s,uint32_t epoch,uint32_t session,int32_t q,float fraction,bool valid);
void gl30_model_primary(gl30_model *s);
void gl30_model_back(gl30_model *s);
void gl30_model_open(gl30_model *s,gl30_app app);
void gl30_model_quick_settings(gl30_model *s);
void gl30_model_power(gl30_model *s);
void gl30_model_fault(gl30_model *s,bool fault);
void gl30_model_reset(gl30_model *s);
float gl30_model_angle(const gl30_model *s);
void gl30_format_time(uint64_t ms,bool ceil_seconds,char *out,unsigned capacity);
void gl30_model_leds(const gl30_model *s,uint8_t rgb[24][3]);
#endif
