#ifndef GL30_MOTOR_H
#define GL30_MOTOR_H
#include <stdint.h>
#include "gl30_menu_session.h"
typedef struct {
    gl30_menu_sample sample;
    gl30_motor_feedback feedback;
    uint32_t fast_frames,haptic_frames,rx_errors,tx_frames,tx_errors,max_poll_us;
    uint32_t zero_command_nonce;
    uint64_t stop_generation;
    uint64_t zero_sent_us,zero_feedback_us;
    uint64_t lease_generation,maintenance_token;
    bool connected,armed,zero_confirmed,control_ready,maintenance_ready;
} gl30_motor_status;
bool gl30_motor_init(void);
void gl30_motor_desire_menu(bool active,uint32_t epoch);
/* Bench entry only; token must come from the status snapshot for this intent. */
/* True means this local request passed the control/token fence. The UART owner
 * still rejects unhealthy live feedback; STM independently gates all output.
 * This asynchronous acceptance is never evidence that the motor is active. */
bool gl30_motor_arm(uint64_t captured_stop_generation);
/* Record a token-free stop outside the input queue; the motor owner applies it
 * on its next poll. The final check/TX gap may still submit one built frame;
 * already submitted UART bytes cannot be recalled. This is not a hardware E-stop. */
void gl30_motor_stop(void);
/* UI owner: async request only when unarmed. A nonzero token blocks ARM until
 * finish AND a fresh generation's applied zero handshake complete. */
uint64_t gl30_motor_maintenance_begin(void);
bool gl30_motor_maintenance_claim(uint64_t token);
bool gl30_motor_maintenance_held(uint64_t token);
void gl30_motor_maintenance_end(uint64_t token);
void gl30_motor_snapshot(gl30_motor_status *out);
#endif
