#include "gl30_preferences.h"
#include "v6_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static const char *test_case = "setup";
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "preferences FAIL [%s] line %d: %s\n", \
                test_case, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static const gl30_preferences golden_preferences = {
    .volume = 42u,
    .light_effect = 3u,
    .light_color = 5u,
    .light_brightness = 64u,
    .screen_brightness = 83u,
    .language = 1u,
    .timer_step_minutes = 10u,
    .volume_step_percent = 5u,
    .stopwatch_show_centis = 0u,
    .alarm_enabled = 1u,
    .weather_fahrenheit = 0u,
    .feel_profile = 2u,
    .calendar_monday_first = 1u
};

/* Independently fixed CRC32C golden record (CRC 0xd0b34420, little-endian). */
static const uint8_t golden_record[GL30_PREFERENCES_RECORD_BYTES] = {
    0x47u, 0x4cu, 0x33u, 0x30u, 0x01u, 0x00u, 0x1cu, 0x00u,
    0x2au, 0x03u, 0x05u, 0x40u, 0x53u, 0x01u, 0x0au, 0x05u,
    0x00u, 0x01u, 0x00u, 0x02u, 0x01u, 0x00u, 0x00u, 0x00u,
    0x20u, 0x44u, 0xb3u, 0xd0u
};

static gl30_preferences sentinel_preferences(void)
{
    const gl30_preferences value = {
        0xa1u, 0xa2u, 0xa3u, 0xa4u, 0xa5u, 0xa6u, 0xa7u,
        0xa8u, 0xa9u, 0xaau, 0xabu, 0xacu, 0xadu
    };
    return value;
}

static void store_u32_le(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8u);
    out[2] = (uint8_t)(value >> 16u);
    out[3] = (uint8_t)(value >> 24u);
}

static uint32_t load_u32_le(const uint8_t *in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8u) |
           ((uint32_t)in[2] << 16u) | ((uint32_t)in[3] << 24u);
}

static void refresh_record_crc(uint8_t record[GL30_PREFERENCES_RECORD_BYTES])
{
    uint32_t crc = 0u;
    gl30_crc32c(record, 24u, &crc);
    store_u32_le(&record[24], crc);
}

static void expect_decode_rejects_unchanged(const uint8_t *record, size_t length)
{
    gl30_preferences output = sentinel_preferences();
    const gl30_preferences before = output;
    CHECK(!gl30_preferences_decode(record, length, &output));
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

static void expect_capture_rejects_unchanged(const gl30_model *model)
{
    gl30_preferences output = sentinel_preferences();
    const gl30_preferences before = output;
    CHECK(!gl30_preferences_capture(model, &output));
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

static void set_model_preferences(gl30_model *model, const gl30_preferences *p)
{
    model->volume = (int)p->volume;
    model->light_effect = (int)p->light_effect;
    model->light_color = (int)p->light_color;
    model->light_brightness = (int)p->light_brightness;
    model->screen_brightness = (int)p->screen_brightness;
    model->language = (gl30_language)p->language;
    model->timer_step_minutes = (int)p->timer_step_minutes;
    model->volume_step_percent = (int)p->volume_step_percent;
    model->stopwatch_show_centis = p->stopwatch_show_centis != 0u;
    model->alarm_enabled = p->alarm_enabled != 0u;
    model->weather_fahrenheit = p->weather_fahrenheit != 0u;
    model->feel_profile = (int)p->feel_profile;
    model->calendar_monday_first = p->calendar_monday_first != 0u;
}

static void check_model_preferences_equal(const gl30_model *model,
                                          const gl30_preferences *p)
{
    CHECK(model->volume == (int)p->volume);
    CHECK(model->light_effect == (int)p->light_effect);
    CHECK(model->light_color == (int)p->light_color);
    CHECK(model->light_brightness == (int)p->light_brightness);
    CHECK(model->screen_brightness == (int)p->screen_brightness);
    CHECK(model->language == (gl30_language)p->language);
    CHECK(model->timer_step_minutes == (int)p->timer_step_minutes);
    CHECK(model->volume_step_percent == (int)p->volume_step_percent);
    CHECK(model->stopwatch_show_centis == (p->stopwatch_show_centis != 0u));
    CHECK(model->alarm_enabled == (p->alarm_enabled != 0u));
    CHECK(model->weather_fahrenheit == (p->weather_fahrenheit != 0u));
    CHECK(model->feel_profile == (int)p->feel_profile);
    CHECK(model->calendar_monday_first == (p->calendar_monday_first != 0u));
}

static void check_runtime_fields_unchanged(const gl30_model *actual,
                                           const gl30_model *before)
{
    CHECK(actual->page == before->page && actual->app == before->app &&
          actual->phase == before->phase);
    CHECK(actual->menu_index == before->menu_index &&
          actual->menu_position == before->menu_position &&
          actual->menu_visual == before->menu_visual);
    CHECK(actual->menu_anim_from_q8 == before->menu_anim_from_q8 &&
          actual->menu_anim_to_q8 == before->menu_anim_to_q8 &&
          actual->menu_anim_started == before->menu_anim_started &&
          actual->menu_epoch == before->menu_epoch &&
          actual->menu_animating == before->menu_animating);
    CHECK(actual->motor_menu_session == before->motor_menu_session &&
          actual->motor_menu_valid == before->motor_menu_valid &&
          actual->motor_menu_origin == before->motor_menu_origin);
    CHECK(actual->remaining_ms == before->remaining_ms &&
          actual->duration_ms == before->duration_ms &&
          actual->now_ms == before->now_ms &&
          actual->endstop_until == before->endstop_until);
    CHECK(actual->elapsed_ms == before->elapsed_ms &&
          actual->epoch_seconds == before->epoch_seconds &&
          actual->stopwatch_ms == before->stopwatch_ms &&
          actual->stopwatch_running == before->stopwatch_running);
    CHECK(actual->muted == before->muted && actual->off == before->off &&
          actual->fault == before->fault && actual->endstop == before->endstop);
    CHECK(actual->motor_state == before->motor_state &&
          actual->motor_fault_bits == before->motor_fault_bits);
    CHECK(actual->light_field == before->light_field &&
          actual->light_editing == before->light_editing);
    CHECK(actual->settings_page == before->settings_page &&
          actual->settings_item == before->settings_item &&
          actual->settings_function == before->settings_function &&
          actual->settings_return_app == before->settings_return_app &&
          actual->settings_from_app == before->settings_from_app &&
          actual->settings_editing == before->settings_editing);
}

static void test_golden_record_and_roundtrip(void)
{
    test_case = "golden record";
    uint8_t record[GL30_PREFERENCES_RECORD_BYTES];
    memset(record, 0xa5, sizeof(record));
    CHECK(gl30_preferences_encode(&golden_preferences, record));
    CHECK(memcmp(record, golden_record, sizeof(record)) == 0);

    uint32_t crc = 0u;
    gl30_crc32c(golden_record, 24u, &crc);
    CHECK(crc == 0xd0b34420u);
    CHECK(load_u32_le(&golden_record[24]) == 0xd0b34420u);

    gl30_preferences decoded = {0};
    CHECK(gl30_preferences_decode(golden_record, sizeof(golden_record), &decoded));
    CHECK(gl30_preferences_equal(&decoded, &golden_preferences));
    CHECK(gl30_preferences_equal(&golden_preferences, &decoded));
}

static void test_allowed_boundaries_roundtrip(void)
{
    static const gl30_preferences boundary_values[] = {
        {0u, 0u, 0u, 0u, 10u, 0u, 1u, 1u, 0u, 0u, 0u, 0u, 0u},
        {100u, 3u, 5u, 100u, 100u, 1u, 10u, 10u, 1u, 1u, 1u, 2u, 1u},
        {50u, 2u, 3u, 55u, 60u, 1u, 5u, 5u, 1u, 0u, 1u, 1u, 0u}
    };

    test_case = "allowed boundary roundtrip";
    for (size_t i = 0u; i < sizeof(boundary_values) / sizeof(boundary_values[0]); ++i) {
        uint8_t record[GL30_PREFERENCES_RECORD_BYTES];
        gl30_preferences decoded = {0};
        CHECK(gl30_preferences_encode(&boundary_values[i], record));
        CHECK(gl30_preferences_decode(record, sizeof(record), &decoded));
        CHECK(gl30_preferences_equal(&decoded, &boundary_values[i]));
    }
}

static void test_equal_compares_each_preference(void)
{
    test_case = "field-wise equality";
    gl30_preferences changed = golden_preferences;
    CHECK(gl30_preferences_equal(&golden_preferences, &golden_preferences));
    for (size_t i = 0u; i < 13u; ++i) {
        changed = golden_preferences;
        /* Assign by field index so this check does not assume struct padding. */
        switch (i) {
        case 0u: changed.volume++; break;
        case 1u: changed.light_effect++; break;
        case 2u: changed.light_color++; break;
        case 3u: changed.light_brightness++; break;
        case 4u: changed.screen_brightness++; break;
        case 5u: changed.language++; break;
        case 6u: changed.timer_step_minutes++; break;
        case 7u: changed.volume_step_percent++; break;
        case 8u: changed.stopwatch_show_centis++; break;
        case 9u: changed.alarm_enabled++; break;
        case 10u: changed.weather_fahrenheit++; break;
        case 11u: changed.feel_profile++; break;
        default: changed.calendar_monday_first++; break;
        }
        CHECK(!gl30_preferences_equal(&golden_preferences, &changed));
    }
}

static void test_decode_rejects_bad_records_without_output_changes(void)
{
    uint8_t record[GL30_PREFERENCES_RECORD_BYTES];
    test_case = "bad CRC";
    memcpy(record, golden_record, sizeof(record));
    record[24] ^= 0x01u;
    expect_decode_rejects_unchanged(record, sizeof(record));

    test_case = "short or long record";
    expect_decode_rejects_unchanged(golden_record, sizeof(golden_record) - 1u);
    expect_decode_rejects_unchanged(golden_record, sizeof(golden_record) + 1u);

    test_case = "bad magic with valid CRC";
    memcpy(record, golden_record, sizeof(record));
    record[0] = (uint8_t)'X';
    refresh_record_crc(record);
    expect_decode_rejects_unchanged(record, sizeof(record));

    test_case = "unknown version with valid CRC";
    memcpy(record, golden_record, sizeof(record));
    record[4] = 2u;
    refresh_record_crc(record);
    expect_decode_rejects_unchanged(record, sizeof(record));

    test_case = "wrong declared length with valid CRC";
    memcpy(record, golden_record, sizeof(record));
    record[6] = (uint8_t)(GL30_PREFERENCES_RECORD_BYTES - 1u);
    record[7] = 0u;
    refresh_record_crc(record);
    expect_decode_rejects_unchanged(record, sizeof(record));

    for (size_t i = 21u; i <= 23u; ++i) {
        test_case = "reserved byte with valid CRC";
        memcpy(record, golden_record, sizeof(record));
        record[i] = 1u;
        refresh_record_crc(record);
        expect_decode_rejects_unchanged(record, sizeof(record));
    }
}

static void test_decode_rejects_each_out_of_range_field(void)
{
    static const struct {
        const char *name;
        uint8_t field_offset;
        uint8_t invalid_value;
    } invalid_fields[] = {
        {"volume above maximum", 0u, 101u},
        {"unknown lighting effect", 1u, 4u},
        {"unknown lighting color", 2u, 6u},
        {"light brightness above maximum", 3u, 101u},
        {"screen brightness below minimum", 4u, 9u},
        {"screen brightness above maximum", 4u, 101u},
        {"unknown language", 5u, 2u},
        {"unsupported timer step", 6u, 2u},
        {"unsupported volume step", 7u, 3u},
        {"stopwatch bool outside 0/1", 8u, 2u},
        {"alarm bool outside 0/1", 9u, 2u},
        {"weather bool outside 0/1", 10u, 2u},
        {"unknown feel profile", 11u, 3u},
        {"calendar bool outside 0/1", 12u, 2u}
    };
    uint8_t record[GL30_PREFERENCES_RECORD_BYTES];

    for (size_t i = 0u; i < sizeof(invalid_fields) / sizeof(invalid_fields[0]); ++i) {
        test_case = invalid_fields[i].name;
        memcpy(record, golden_record, sizeof(record));
        record[8u + invalid_fields[i].field_offset] = invalid_fields[i].invalid_value;
        refresh_record_crc(record);
        expect_decode_rejects_unchanged(record, sizeof(record));
    }
}

static gl30_model model_with_preferences(const gl30_preferences *preferences)
{
    gl30_model model = {0};
    set_model_preferences(&model, preferences);
    return model;
}

static void test_capture_rejects_wide_integer_values_without_truncation(void)
{
    static const struct { const char *name; unsigned field; } wide_fields[] = {
        {"volume 256", 0u}, {"effect 256", 1u}, {"color 258", 2u},
        {"light brightness 256", 3u}, {"screen brightness 256", 4u},
        {"language 256", 5u}, {"timer step 257", 6u},
        {"volume step 257", 7u}, {"feel profile 258", 8u}
    };
    const gl30_model baseline = model_with_preferences(&golden_preferences);

    for (size_t i = 0u; i < sizeof(wide_fields) / sizeof(wide_fields[0]); ++i) {
        test_case = wide_fields[i].name;
        gl30_model model = baseline;
        switch (wide_fields[i].field) {
        case 0u: model.volume = 256; break;
        case 1u: model.light_effect = 256; break;
        case 2u: model.light_color = 258; break;
        case 3u: model.light_brightness = 256; break;
        case 4u: model.screen_brightness = 256; break;
        case 5u: model.language = (gl30_language)256; break;
        case 6u: model.timer_step_minutes = 257; break;
        case 7u: model.volume_step_percent = 257; break;
        default: model.feel_profile = 258; break;
        }
        expect_capture_rejects_unchanged(&model);
    }

    test_case = "negative integer input";
    gl30_model negative = baseline;
    negative.volume = -256;
    expect_capture_rejects_unchanged(&negative);
}

static void test_capture_apply_only_preferences(void)
{
    test_case = "capture all thirteen fields";
    gl30_model captured_model = model_with_preferences(&golden_preferences);
    gl30_preferences captured = {0};
    CHECK(gl30_preferences_capture(&captured_model, &captured));
    CHECK(gl30_preferences_equal(&captured, &golden_preferences));

    test_case = "apply preserves runtime state";
    gl30_model model = {0};
    model.page = GL30_APP;
    model.app = GL30_TIMER;
    model.phase = GL30_PAUSED;
    model.menu_index = 4;
    model.menu_position = -13;
    model.menu_visual = 2.75f;
    model.menu_anim_from_q8 = -1024;
    model.menu_anim_to_q8 = 2048;
    model.menu_anim_started = 123456u;
    model.menu_epoch = 77u;
    model.menu_animating = true;
    model.motor_menu_session = 88u;
    model.motor_menu_valid = true;
    model.motor_menu_origin = -5;
    model.remaining_ms = 654321u;
    model.duration_ms = 987654u;
    model.now_ms = 8765u;
    model.endstop_until = 4321u;
    model.elapsed_ms = 123456789u;
    model.epoch_seconds = 1700000000u;
    model.stopwatch_ms = 456789u;
    model.stopwatch_running = true;
    model.muted = true;
    model.off = true;
    model.fault = true;
    model.motor_state = GL30_MOTOR_FAULT;
    model.motor_fault_bits = 0x12345678u;
    model.endstop = -1;
    model.light_field = 2;
    model.light_editing = true;
    model.settings_page = GL30_SETTINGS_FUNCTION;
    model.settings_item = 3;
    model.settings_function = GL30_LIGHTING;
    model.settings_return_app = GL30_TIMER;
    model.settings_from_app = true;
    model.settings_editing = true;
    const gl30_model before = model;

    CHECK(gl30_preferences_apply(&model, &golden_preferences));
    check_model_preferences_equal(&model, &golden_preferences);
    check_runtime_fields_unchanged(&model, &before);

    test_case = "invalid apply is atomic";
    const gl30_model before_invalid = model;
    gl30_preferences invalid = golden_preferences;
    invalid.screen_brightness = 9u;
    CHECK(!gl30_preferences_apply(&model, &invalid));
    gl30_preferences before_prefs = {0}, after_prefs = {0};
    CHECK(gl30_preferences_capture(&before_invalid, &before_prefs));
    CHECK(gl30_preferences_capture(&model, &after_prefs));
    CHECK(gl30_preferences_equal(&before_prefs, &after_prefs));
    check_runtime_fields_unchanged(&model, &before_invalid);
}

static void test_invalid_encode_and_null_arguments(void)
{
    test_case = "invalid encode preserves output";
    uint8_t output[GL30_PREFERENCES_RECORD_BYTES];
    memset(output, 0x5a, sizeof(output));
    uint8_t before[GL30_PREFERENCES_RECORD_BYTES];
    memcpy(before, output, sizeof(before));
    gl30_preferences invalid = golden_preferences;
    invalid.volume = 101u;
    CHECK(!gl30_preferences_encode(&invalid, output));
    CHECK(memcmp(output, before, sizeof(output)) == 0);

    test_case = "NULL parameters";
    gl30_model model = model_with_preferences(&golden_preferences);
    gl30_model model_before = model;
    gl30_preferences prefs = sentinel_preferences();
    gl30_preferences prefs_before = prefs;
    CHECK(!gl30_preferences_capture(NULL, &prefs));
    CHECK(memcmp(&prefs, &prefs_before, sizeof(prefs)) == 0);
    CHECK(!gl30_preferences_capture(&model, NULL));
    CHECK(!gl30_preferences_apply(NULL, &golden_preferences));
    CHECK(!gl30_preferences_apply(&model, NULL));
    check_model_preferences_equal(&model, &golden_preferences);
    check_runtime_fields_unchanged(&model, &model_before);
    CHECK(!gl30_preferences_equal(NULL, &golden_preferences));
    CHECK(!gl30_preferences_equal(&golden_preferences, NULL));
    CHECK(!gl30_preferences_equal(NULL, NULL));
    CHECK(!gl30_preferences_encode(NULL, output));
    CHECK(!gl30_preferences_encode(&golden_preferences, NULL));
    expect_decode_rejects_unchanged(NULL, GL30_PREFERENCES_RECORD_BYTES);
    CHECK(!gl30_preferences_decode(golden_record, sizeof(golden_record), NULL));
}

int main(void)
{
    test_golden_record_and_roundtrip();
    test_allowed_boundaries_roundtrip();
    test_equal_compares_each_preference();
    test_decode_rejects_bad_records_without_output_changes();
    test_decode_rejects_each_out_of_range_field();
    test_capture_rejects_wide_integer_values_without_truncation();
    test_capture_apply_only_preferences();
    test_invalid_encode_and_null_arguments();
    printf("preferences: %u checks passed\n", checks);
    return 0;
}
