#ifndef GL30_MENU_SESSION_H
#define GL30_MENU_SESSION_H
#include "gl30_esp_link.h"

#define GL30_MENU_PROFILE_ID 0x4d454e55u
#define GL30_MENU_WIDTH_RAD 0.7853981633974483f
#define GL30_MENU_UI_TIMEOUT_US 500000u
typedef struct {
    uint32_t epoch, session;
    int32_t position;
    float fraction;
    bool valid;
} gl30_menu_sample;
typedef struct {
    gl30_haptic_command_t command;
    uint32_t epoch, nonce;
    uint64_t requested_us, ui_updated_us;
    bool armed, menu, configured, acknowledged, stop_pending;
} gl30_menu_session;

void gl30_menu_session_init(gl30_menu_session *s,uint32_t nonce_seed);
void gl30_menu_session_desire(gl30_menu_session *s,bool menu,uint32_t epoch,uint64_t published_us);
void gl30_menu_session_arm(gl30_menu_session *s,bool arm);
/* A pending zero command is retained until the transport accepts all bytes. */
void gl30_menu_session_sent(gl30_menu_session *s);
/* Single owner, run independently of rendering. A true return publishes the
 * current command, never a backlog. No hardware access or force-clear here. */
bool gl30_menu_session_step(gl30_menu_session *s,const gl30_esp_link_t *link,
                           uint64_t now_us,gl30_menu_sample *sample);
#endif
