#ifndef GL30_MOTOR_FEEDBACK_H
#define GL30_MOTOR_FEEDBACK_H
#include <stdint.h>

#define GL30_MOTOR_FAST_FRESH_US 20000u
typedef enum {
    GL30_MOTOR_OFFLINE, GL30_MOTOR_CHECKING, GL30_MOTOR_ALIGN,
    GL30_MOTOR_READY, GL30_MOTOR_ACTIVE, GL30_MOTOR_FAULT, GL30_MOTOR_INVALID
} gl30_motor_feedback_state;
typedef struct {
    uint64_t received_us;
    uint32_t fault_bits;
    gl30_motor_feedback_state state;
} gl30_motor_feedback;

/* Age the captured evidence again on snapshot reads. A stalled publisher must
 * never leave an old READY/ACTIVE indication on the screen. This is read-only
 * telemetry; OFFLINE says nothing about the STM32's latched fault state. */
static inline gl30_motor_feedback gl30_motor_feedback_current(gl30_motor_feedback value,uint64_t now_us) {
    if(now_us<value.received_us || now_us-value.received_us>=GL30_MOTOR_FAST_FRESH_US) {
        value.state=GL30_MOTOR_OFFLINE; value.fault_bits=0;
    }
    return value;
}
#endif
