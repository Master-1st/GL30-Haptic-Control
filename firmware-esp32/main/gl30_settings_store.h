#ifndef GL30_SETTINGS_STORE_H
#define GL30_SETTINGS_STORE_H
#include "esp_err.h"
#include "gl30_preferences.h"

typedef enum {
    GL30_SETTINGS_LOADED,
    GL30_SETTINGS_MISSING,
    GL30_SETTINGS_REJECTED,
    GL30_SETTINGS_UNAVAILABLE
} gl30_settings_load_result;

/* Boot owner only, BEFORE display/motor/input initialization. Init/open may
 * write SDK metadata. Missing/invalid records leave out unchanged; never erase. */
gl30_settings_load_result gl30_settings_store_boot_load(gl30_preferences *out);
bool gl30_settings_store_available(void);
/* UI owner only. Both motor lease and display reservations must remain held
 * until this call returns. An error does not guarantee durable rollback. */
esp_err_t gl30_settings_store_write(const gl30_preferences *preferences,
                                   uint64_t maintenance_token);
#endif
