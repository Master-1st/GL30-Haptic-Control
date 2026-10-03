#ifndef GL30_PREFERENCES_H
#define GL30_PREFERENCES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gl30_model.h"

#define GL30_PREFERENCES_RECORD_BYTES 28u

typedef struct {
    uint8_t volume;
    uint8_t light_effect;
    uint8_t light_color;
    uint8_t light_brightness;
    uint8_t screen_brightness;
    uint8_t language;
    uint8_t timer_step_minutes;
    uint8_t volume_step_percent;
    uint8_t stopwatch_show_centis;
    uint8_t alarm_enabled;
    uint8_t weather_fahrenheit;
    uint8_t feel_profile;
    uint8_t calendar_monday_first;
} gl30_preferences;

/* NULL or invalid input returns false. Failure leaves all outputs unchanged.
 * Apply changes only the 13 preferences, never runtime or authorization state. */
bool gl30_preferences_capture(const gl30_model *model, gl30_preferences *out);
bool gl30_preferences_apply(gl30_model *model, const gl30_preferences *preferences);
bool gl30_preferences_equal(const gl30_preferences *a, const gl30_preferences *b);
bool gl30_preferences_encode(const gl30_preferences *preferences,
                             uint8_t out[GL30_PREFERENCES_RECORD_BYTES]);
bool gl30_preferences_decode(const uint8_t *data, size_t length,
                             gl30_preferences *out);

#endif
