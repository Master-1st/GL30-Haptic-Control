#include "gl30_menu_session.h"
#include "foc.h"
#include "safety_supervisor.h"
#include <math.h>
#include <string.h>

static uint32_t next_nonce(gl30_menu_session *s) {
    if(++s->nonce==0) ++s->nonce;
    return s->nonce;
}
static void zero_command(gl30_menu_session *s) {
    memset(&s->command,0,sizeof(s->command));
    s->command.commandNonce=next_nonce(s);
    s->configured=false; s->acknowledged=false;
}
void gl30_menu_session_init(gl30_menu_session *s,uint32_t nonce_seed) {
    memset(s,0,sizeof(*s)); s->nonce=nonce_seed; zero_command(s);
}
void gl30_menu_session_desire(gl30_menu_session *s,bool menu,uint32_t epoch,uint64_t published_us) {
    s->ui_updated_us=published_us;
    if(s->menu==menu && s->epoch==epoch) return;
    s->menu=menu; s->epoch=epoch;
    s->stop_pending=s->stop_pending || s->armed;
    zero_command(s);
}
void gl30_menu_session_arm(gl30_menu_session *s,bool arm) {
    if(s->armed==arm) return;
    s->stop_pending=s->armed && !arm;
    s->armed=arm; zero_command(s);
}
void gl30_menu_session_sent(gl30_menu_session *s) { s->stop_pending=false; }
static bool fresh(uint64_t now,uint64_t at) { return now>=at && now-at<20000u; }
bool gl30_menu_session_step(gl30_menu_session *s,const gl30_esp_link_t *link,
                           uint64_t now_us,gl30_menu_sample *sample) {
    *sample=(gl30_menu_sample){.epoch=s->epoch,.session=s->command.commandNonce};
    bool healthy=now_us>=s->ui_updated_us && now_us-s->ui_updated_us<GL30_MENU_UI_TIMEOUT_US &&
        link->has_fast && fresh(now_us,link->last_fast_us) &&
        (link->latest_fast.motorState==GL30_STARTUP_READY || link->latest_fast.motorState==GL30_STARTUP_ACTIVE) &&
        link->latest_fast.encoderStatus==1u && !link->latest_fast.faultBits &&
        isfinite(link->latest_fast.angleRad);
    if(s->armed && !healthy) gl30_menu_session_arm(s,false);
    if(!s->armed) {
        return s->stop_pending;
    }
    if(!s->menu) { s->stop_pending=false; return true; }
    if(!s->configured) {
        /* Anchor once on entry, never chase the painted angle. Menu has no
         * endstop or position servo. 20 mNm detents, 30 mNm combined cap. */
        s->command=(gl30_haptic_command_t){
            .profileId=GL30_MENU_PROFILE_ID,.commandNonce=next_nonce(s),
            .targetPositionRad=link->latest_fast.angleRad,
            .detentWidthRad=GL30_MENU_WIDTH_RAD,.detentStrengthNm=0.020f,
            .dampingNmPerRadS=0.001f,.userTorqueLimitNm=0.030f,
            .activeSpeedLimitRadS=12.5663706f,.modeFlags=GL30_HAPTIC_DETENT
        };
        s->configured=true; s->requested_us=now_us; s->stop_pending=false;
    }
    const gl30_haptic_state_t *h=&link->latest_haptic;
    bool confirmed=link->has_haptic && fresh(now_us,link->last_haptic_us) &&
        h->profileId==s->command.profileId && h->commandNonce==s->command.commandNonce &&
        h->modeFlags==GL30_HAPTIC_DETENT && h->status==GL30_HAPTIC_STATE_STATUS_MASK && !h->faultBits &&
        (h->motorState==GL30_STARTUP_READY || h->motorState==GL30_STARTUP_ACTIVE) &&
        isfinite(h->subPosition) && fabsf(h->subPosition)<=0.56f &&
        fabsf(h->detentWidthRad-GL30_MENU_WIDTH_RAD)<0.000001f;
    if(confirmed) {
        s->acknowledged=true;
        *sample=(gl30_menu_sample){s->epoch,s->command.commandNonce,
            h->logicalPosition,h->subPosition,true};
    } else if(s->acknowledged || now_us<s->requested_us || now_us-s->requested_us>=100000u) {
        /* Lost/mismatched feedback never silently resumes torque. */
        gl30_menu_session_arm(s,false); return true;
    }
    return true;
}
