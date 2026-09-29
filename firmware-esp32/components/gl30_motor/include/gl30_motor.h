#ifndef GL30_MOTOR_H
#define GL30_MOTOR_H
#include "gl30_menu_session.h"
typedef struct {
    gl30_menu_sample sample;
    uint32_t fast_frames,haptic_frames,rx_errors,tx_frames,tx_errors,max_poll_us;
    bool connected,armed;
} gl30_motor_status;
bool gl30_motor_init(void);
void gl30_motor_desire_menu(bool active,uint32_t epoch);
/* Default builds reject enable requests and never send motor commands.
 * Experimental bench entry requires CONFIG_GL30_EXPERIMENTAL_MOTOR_CONTROL
 * plus paired STM32 development firmware emitting HAPTIC_STATE (0x06).
 * Boot, link loss and faults always leave this disarmed. */
void gl30_motor_arm(bool enabled);
void gl30_motor_snapshot(gl30_motor_status *out);
#endif
