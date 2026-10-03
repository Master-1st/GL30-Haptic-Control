/* Exercise the actual NVS store against a deterministic in-memory NVS fake. */
#include "esp_err.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "gl30_board.h"
#include "gl30_motor.h"
#include "gl30_preferences.h"
#include "gl30_settings_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FAKE_BLOB_CAPACITY = 64 };
static unsigned checks;
static const char *test_case = "setup";
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "settings-store FAIL [%s] line %d: %s\n", \
                test_case, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static uint8_t fake_blob[FAKE_BLOB_CAPACITY];
static size_t fake_blob_length;
static bool fake_blob_present;
static esp_err_t fake_init_result, fake_open_result, fake_query_read_result;
static esp_err_t fake_record_read_result, fake_set_result, fake_commit_result;
static unsigned fake_init_calls, fake_open_calls, fake_get_calls;
static unsigned fake_set_calls, fake_commit_calls;
static unsigned fake_mode;
static bool fake_motor_held, fake_display_active;
static uint64_t fake_held_token;

bool gl30_motor_maintenance_held(uint64_t token)
{
    return fake_motor_held && token != 0u && token == fake_held_token;
}

bool gl30_board_display_maintenance_active(void)
{
    return fake_display_active;
}

esp_err_t nvs_flash_init_partition(const char *partition)
{
    ++fake_init_calls;
    CHECK(partition != NULL && strcmp(partition, "nvs") == 0);
    return fake_init_result;
}

esp_err_t nvs_open_from_partition(const char *partition, const char *name,
                                  nvs_open_mode_t mode, nvs_handle_t *out)
{
    ++fake_open_calls;
    CHECK(partition != NULL && strcmp(partition, "nvs") == 0);
    CHECK(name != NULL && strcmp(name, "gl30_ui") == 0);
    fake_mode = (unsigned)mode;
    if (fake_open_result == ESP_OK) *out = 0x30u;
    return fake_open_result;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out, size_t *length)
{
    CHECK(handle == 0x30u && key != NULL && strcmp(key, "preferences") == 0);
    CHECK(length != NULL);
    ++fake_get_calls;
    if (fake_get_calls == 1u && fake_query_read_result != ESP_OK)
        return fake_query_read_result;
    if (fake_get_calls == 2u && fake_record_read_result != ESP_OK)
        return fake_record_read_result;
    if (!fake_blob_present) return ESP_ERR_NVS_NOT_FOUND;
    if (out == NULL) {
        *length = fake_blob_length;
        return ESP_OK;
    }
    if (*length < fake_blob_length) {
        *length = fake_blob_length;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out, fake_blob, fake_blob_length);
    *length = fake_blob_length;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value,
                       size_t length)
{
    ++fake_set_calls;
    CHECK(handle == 0x30u && key != NULL && strcmp(key, "preferences") == 0);
    CHECK(value != NULL && length <= sizeof(fake_blob));
    if (fake_set_result != ESP_OK) return fake_set_result;
    memcpy(fake_blob, value, length);
    fake_blob_length = length;
    fake_blob_present = true;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    ++fake_commit_calls;
    CHECK(handle == 0x30u);
    return fake_commit_result;
}

#include "../../../main/gl30_settings_store.c"

static gl30_preferences valid_preferences(void)
{
    return (gl30_preferences){
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
        .calendar_monday_first = 1u,
    };
}

static gl30_preferences sentinel_preferences(void)
{
    return (gl30_preferences){
        .volume = 0xA1u, .light_effect = 0xA2u, .light_color = 0xA3u,
        .light_brightness = 0xA4u, .screen_brightness = 0xA5u, .language = 0xA6u,
        .timer_step_minutes = 0xA7u, .volume_step_percent = 0xA8u,
        .stopwatch_show_centis = 0xA9u, .alarm_enabled = 0xAAu,
        .weather_fahrenheit = 0xABu, .feel_profile = 0xACu,
        .calendar_monday_first = 0xADu,
    };
}

static void reset_fake(void)
{
    memset(fake_blob, 0, sizeof(fake_blob));
    fake_blob_length = 0u;
    fake_blob_present = false;
    fake_init_result = fake_open_result = ESP_OK;
    fake_query_read_result = fake_record_read_result = ESP_OK;
    fake_set_result = fake_commit_result = ESP_OK;
    fake_init_calls = fake_open_calls = fake_get_calls = 0u;
    fake_set_calls = fake_commit_calls = fake_mode = 0u;
    fake_motor_held = fake_display_active = false;
    fake_held_token = UINT64_C(0x123456789ABCDEF0);
    initialized = false;
    available = false;
    handle = 0u;
}

static void seed_valid_record(void)
{
    const gl30_preferences preferences = valid_preferences();
    CHECK(gl30_preferences_encode(&preferences, fake_blob));
    fake_blob_length = GL30_PREFERENCES_RECORD_BYTES;
    fake_blob_present = true;
}

static void expect_load_failure_unchanged(gl30_settings_load_result expected)
{
    gl30_preferences output = sentinel_preferences();
    const gl30_preferences before = output;
    CHECK(gl30_settings_store_boot_load(&output) == expected);
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

static void test_boot_load_and_one_shot_initialization(void)
{
    gl30_preferences output;
    gl30_preferences before;

    test_case = "boot missing";
    reset_fake();
    output = sentinel_preferences();
    before = output;
    CHECK(gl30_settings_store_boot_load(&output) == GL30_SETTINGS_MISSING);
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    CHECK(gl30_settings_store_available());
    CHECK(fake_init_calls == 1u && fake_open_calls == 1u && fake_get_calls == 1u);
    CHECK(fake_mode == (unsigned)NVS_READWRITE);
    CHECK(gl30_settings_store_boot_load(&output) == GL30_SETTINGS_UNAVAILABLE);
    CHECK(fake_init_calls == 1u && fake_open_calls == 1u);

    test_case = "boot valid";
    reset_fake();
    seed_valid_record();
    output = sentinel_preferences();
    {
        const gl30_preferences expected = valid_preferences();
    CHECK(gl30_settings_store_boot_load(&output) == GL30_SETTINGS_LOADED);
        CHECK(memcmp(&output, &expected, sizeof(output)) == 0);
    }
    CHECK(fake_get_calls == 2u && gl30_settings_store_available());

    test_case = "boot null output";
    reset_fake();
    CHECK(gl30_settings_store_boot_load(NULL) == GL30_SETTINGS_UNAVAILABLE);
    CHECK(fake_init_calls == 0u && fake_open_calls == 0u);
}

static void test_invalid_or_unavailable_records_do_not_mutate_or_erase(void)
{
    gl30_preferences output;
    const gl30_preferences before = sentinel_preferences();

    test_case = "boot corrupt CRC";
    reset_fake();
    seed_valid_record();
    fake_blob[27] ^= 0x80u;
    output = before;
    CHECK(gl30_settings_store_boot_load(&output) == GL30_SETTINGS_REJECTED);
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    CHECK(fake_set_calls == 0u && fake_commit_calls == 0u);

    test_case = "boot strict record length";
    reset_fake();
    seed_valid_record();
    fake_blob_length = GL30_PREFERENCES_RECORD_BYTES - 1u;
    output = before;
    CHECK(gl30_settings_store_boot_load(&output) == GL30_SETTINGS_REJECTED);
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    CHECK(fake_get_calls == 1u && fake_set_calls == 0u && fake_commit_calls == 0u);

    test_case = "boot invalid preference value";
    reset_fake();
    seed_valid_record();
    fake_blob[8] = 101u;
    {
        uint32_t crc = 0u;
        gl30_crc32c(fake_blob, 24u, &crc);
        fake_blob[24] = (uint8_t)crc;
        fake_blob[25] = (uint8_t)(crc >> 8u);
        fake_blob[26] = (uint8_t)(crc >> 16u);
        fake_blob[27] = (uint8_t)(crc >> 24u);
    }
    output = before;
    CHECK(gl30_settings_store_boot_load(&output) == GL30_SETTINGS_REJECTED);
    CHECK(memcmp(&output, &before, sizeof(output)) == 0);
    CHECK(fake_set_calls == 0u && fake_commit_calls == 0u);

    test_case = "NVS init failure";
    reset_fake();
    fake_init_result = ESP_FAIL;
    expect_load_failure_unchanged(GL30_SETTINGS_UNAVAILABLE);
    CHECK(!gl30_settings_store_available() && fake_open_calls == 0u);
    CHECK(gl30_settings_store_write(&(gl30_preferences){0}, fake_held_token) ==
          ESP_ERR_INVALID_STATE);
    CHECK(fake_set_calls == 0u && fake_commit_calls == 0u);

    test_case = "NVS open failure";
    reset_fake();
    fake_open_result = ESP_FAIL;
    expect_load_failure_unchanged(GL30_SETTINGS_UNAVAILABLE);
    CHECK(!gl30_settings_store_available() && fake_open_calls == 1u);

    test_case = "NVS read-size failure";
    reset_fake();
    seed_valid_record();
    fake_query_read_result = ESP_FAIL;
    expect_load_failure_unchanged(GL30_SETTINGS_UNAVAILABLE);
    CHECK(fake_get_calls == 1u);

    test_case = "NVS read-record failure";
    reset_fake();
    seed_valid_record();
    fake_record_read_result = ESP_FAIL;
    expect_load_failure_unchanged(GL30_SETTINGS_UNAVAILABLE);
    CHECK(fake_get_calls == 2u);
}

static void test_write_requires_both_live_claims_and_valid_preferences(void)
{
    const uint64_t token = UINT64_C(0x123456789ABCDEF0);
    gl30_preferences preferences = valid_preferences();

    test_case = "write before boot";
    reset_fake();
    fake_motor_held = fake_display_active = true;
    CHECK(gl30_settings_store_write(&preferences, token) == ESP_ERR_INVALID_STATE);
    CHECK(fake_set_calls == 0u && fake_commit_calls == 0u);

    test_case = "write without motor claim";
    reset_fake();
    CHECK(gl30_settings_store_boot_load(&(gl30_preferences){0}) == GL30_SETTINGS_MISSING);
    fake_display_active = true;
    fake_motor_held = false;
    CHECK(gl30_settings_store_write(&preferences, token) == ESP_ERR_INVALID_STATE);
    CHECK(fake_set_calls == 0u && fake_commit_calls == 0u);

    test_case = "write without display claim";
    fake_motor_held = true;
    fake_display_active = false;
    CHECK(gl30_settings_store_write(&preferences, token) == ESP_ERR_INVALID_STATE);
    CHECK(fake_set_calls == 0u && fake_commit_calls == 0u);

    test_case = "write with stale motor token";
    fake_display_active = true;
    CHECK(gl30_settings_store_write(&preferences, token + 1u) == ESP_ERR_INVALID_STATE);
    CHECK(fake_set_calls == 0u && fake_commit_calls == 0u);

    test_case = "write invalid preference range";
    fake_motor_held = true;
    preferences.screen_brightness = 9u;
    CHECK(gl30_settings_store_write(&preferences, token) == ESP_ERR_INVALID_ARG);
    CHECK(fake_set_calls == 0u && fake_commit_calls == 0u);
    CHECK(gl30_settings_store_write(NULL, token) == ESP_ERR_INVALID_ARG);
    CHECK(fake_set_calls == 0u && fake_commit_calls == 0u);

    test_case = "successful write";
    preferences = valid_preferences();
    CHECK(gl30_settings_store_write(&preferences, token) == ESP_OK);
    CHECK(fake_set_calls == 1u && fake_commit_calls == 1u && fake_blob_present);
    CHECK(fake_blob_length == GL30_PREFERENCES_RECORD_BYTES);
    {
        gl30_preferences decoded = {0};
        CHECK(gl30_preferences_decode(fake_blob, fake_blob_length, &decoded));
        CHECK(memcmp(&decoded, &preferences, sizeof(decoded)) == 0);
    }
}

static void test_nvs_write_errors_are_reported_without_rollback_claim(void)
{
    const uint64_t token = UINT64_C(0x123456789ABCDEF0);
    const gl30_preferences preferences = valid_preferences();

    reset_fake();
    CHECK(gl30_settings_store_boot_load(&(gl30_preferences){0}) == GL30_SETTINGS_MISSING);
    fake_motor_held = fake_display_active = true;
    fake_set_result = ESP_FAIL;
    test_case = "NVS set failure";
    CHECK(gl30_settings_store_write(&preferences, token) == ESP_FAIL);
    CHECK(fake_set_calls == 1u && fake_commit_calls == 0u);

    fake_set_result = ESP_OK;
    fake_commit_result = ESP_FAIL;
    test_case = "NVS commit failure";
    CHECK(gl30_settings_store_write(&preferences, token) == ESP_FAIL);
    CHECK(fake_set_calls == 2u && fake_commit_calls == 1u);
    CHECK(fake_blob_present && fake_blob_length == GL30_PREFERENCES_RECORD_BYTES);
    /* The fake persists set_blob before commit: a commit error does not prove rollback. */
}

int main(void)
{
    test_boot_load_and_one_shot_initialization();
    test_invalid_or_unavailable_records_do_not_mutate_or_erase();
    test_write_requires_both_live_claims_and_valid_preferences();
    test_nvs_write_errors_are_reported_without_rollback_claim();
    printf("settings store production source: %u checks passed\n", checks);
    return 0;
}
