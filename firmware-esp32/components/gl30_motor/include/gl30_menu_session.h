#ifndef GL30_MENU_SESSION_H
#define GL30_MENU_SESSION_H
#include "gl30_esp_link.h"
#include "gl30_motor_feedback.h"

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

/* True when every command configuration field except commandNonce is zero. */
static inline bool gl30_menu_session_command_is_zero(
    const gl30_haptic_command_t *command) {
    return command != NULL && command->profileId == 0u &&
        command->targetPositionRad == 0.0f && command->targetVelocityRadS == 0.0f &&
        command->detentWidthRad == 0.0f && command->detentStrengthNm == 0.0f &&
        command->endstopMinRad == 0.0f && command->endstopMaxRad == 0.0f &&
        command->endstopStrengthNm == 0.0f && command->dampingNmPerRadS == 0.0f &&
        command->inertiaKgM2 == 0.0f && command->frictionNm == 0.0f &&
        command->userTorqueLimitNm == 0.0f && command->activeSpeedLimitRadS == 0.0f &&
        command->modeFlags == 0u && command->textureId == 0u;
}

void gl30_menu_session_init(gl30_menu_session *s,uint32_t nonce_seed);
void gl30_menu_session_desire(gl30_menu_session *s,bool menu,uint32_t epoch,uint64_t published_us);
void gl30_menu_session_arm(gl30_menu_session *s,bool arm);
/* Apply an explicit stop intent, giving the zero command a fresh nonce. */
void gl30_menu_session_stop(gl30_menu_session *s);
/* A pending zero command is retained until the transport accepts all bytes. */
void gl30_menu_session_sent(gl30_menu_session *s);
/* Session-level echo condition only; the owner must also fence the live arm
 * gate under lock, and callers should read published status via
 * gl30_motor_snapshot. This is not global authorization or proof of physical
 * bridge state. */
bool gl30_menu_session_zero_confirmed(const gl30_menu_session *s,
                                      const gl30_esp_link_t *link,
                                      uint32_t sent_nonce,
                                      uint64_t sent_us,
                                      uint64_t now_us);
/* Single owner, run independently of rendering. A true return publishes the
 * current command, never a backlog. No hardware access or force-clear here. */
bool gl30_menu_session_step(gl30_menu_session *s,const gl30_esp_link_t *link,
                           uint64_t now_us,gl30_menu_sample *sample);
/* Project FAST evidence for display without changing the link or arm lease. */
gl30_motor_feedback gl30_menu_session_feedback(const gl30_esp_link_t *link,uint64_t now_us);
#endif
