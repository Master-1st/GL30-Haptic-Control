#include "gl30_settings_store.h"
#include "gl30_board.h"
#include "gl30_motor.h"
#include "nvs.h"
#include "nvs_flash.h"

static nvs_handle_t handle;
static bool initialized,available;

gl30_settings_load_result gl30_settings_store_boot_load(gl30_preferences *out) {
    if(initialized || out==NULL) return GL30_SETTINGS_UNAVAILABLE;
    initialized=true;
    if(nvs_flash_init_partition("nvs")!=ESP_OK ||
       nvs_open_from_partition("nvs","gl30_ui",NVS_READWRITE,&handle)!=ESP_OK)
        return GL30_SETTINGS_UNAVAILABLE;
    available=true;
    size_t length=0;
    esp_err_t error=nvs_get_blob(handle,"preferences",NULL,&length);
    if(error==ESP_ERR_NVS_NOT_FOUND) return GL30_SETTINGS_MISSING;
    if(error!=ESP_OK) return GL30_SETTINGS_UNAVAILABLE;
    if(length!=GL30_PREFERENCES_RECORD_BYTES) return GL30_SETTINGS_REJECTED;
    uint8_t record[GL30_PREFERENCES_RECORD_BYTES];
    error=nvs_get_blob(handle,"preferences",record,&length);
    if(error!=ESP_OK) return GL30_SETTINGS_UNAVAILABLE;
    return gl30_preferences_decode(record,length,out)?
        GL30_SETTINGS_LOADED:GL30_SETTINGS_REJECTED;
}
bool gl30_settings_store_available(void) { return available; }
esp_err_t gl30_settings_store_write(const gl30_preferences *preferences,
                                   uint64_t maintenance_token) {
    uint8_t record[GL30_PREFERENCES_RECORD_BYTES];
    if(!available || !gl30_motor_maintenance_held(maintenance_token) ||
       !gl30_board_display_maintenance_active()) return ESP_ERR_INVALID_STATE;
    if(!gl30_preferences_encode(preferences,record)) return ESP_ERR_INVALID_ARG;
    esp_err_t error=nvs_set_blob(handle,"preferences",record,sizeof(record));
    if(error!=ESP_OK) return error;
    return nvs_commit(handle);
}
