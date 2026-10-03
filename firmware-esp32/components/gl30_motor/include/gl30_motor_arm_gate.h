#ifndef GL30_MOTOR_ARM_GATE_H
#define GL30_MOTOR_ARM_GATE_H

#include <stdbool.h>
#include <stdint.h>

/* Zero-initialize this value object. Every operation below requires that the
 * caller already holds the lock protecting the gate. */
typedef struct {
    uint64_t stop_generation;
    bool arm_request;
    bool arm_changed;
    bool maintenance_active;
} gl30_motor_arm_gate_t;

typedef struct {
    uint64_t stop_generation;
    bool arm_request;
    bool arm_changed;
} gl30_motor_arm_gate_request_t;

/* Caller holds the gate lock. A stop invalidates every earlier arm token. */
static inline void gl30_motor_arm_gate_stop(gl30_motor_arm_gate_t *gate) {
    ++gate->stop_generation;
    gate->arm_request = false;
    gate->arm_changed = true;
}

/* Caller holds the gate lock. Returns false without changing state for a
 * token captured before the latest stop. */
static inline bool gl30_motor_arm_gate_arm(
    gl30_motor_arm_gate_t *gate, uint64_t captured_stop_generation) {
    if (gate->maintenance_active || captured_stop_generation != gate->stop_generation) return false;
    gate->arm_request = true;
    gate->arm_changed = true;
    return true;
}

/* Caller holds the gate lock. Clearing arm_changed here cannot consume a
 * later stop because stop and take are serialized by that same lock. */
static inline gl30_motor_arm_gate_request_t gl30_motor_arm_gate_take(
    gl30_motor_arm_gate_t *gate) {
    const gl30_motor_arm_gate_request_t request = {
        .stop_generation = gate->stop_generation,
        .arm_request = gate->arm_request,
        .arm_changed = gate->arm_changed,
    };
    gate->arm_changed = false;
    return request;
}

#endif
