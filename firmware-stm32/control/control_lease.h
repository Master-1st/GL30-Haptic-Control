#ifndef GL30_CONTROL_LEASE_H_
#define GL30_CONTROL_LEASE_H_

#include <stdbool.h>
#include <stdint.h>

#include "v6_protocol.h"

typedef enum {
  GL30_CONTROL_LEASE_UNOWNED = 0,
  GL30_CONTROL_LEASE_OWNED,
  GL30_CONTROL_LEASE_RELEASED
} gl30_control_lease_state_t;

typedef struct {
  gl30_control_lease_state_t state;
  uint64_t generation;
  uint64_t acquire_from_generation;
  uint32_t zero_nonce;
  bool awaiting_first_zero;
  bool ack_pending;
  uint32_t ack_revision;
} gl30_control_lease_t;

typedef enum {
  GL30_CONTROL_LEASE_REQUEST_REJECTED = 0,
  GL30_CONTROL_LEASE_QUERY_ACK,
  GL30_CONTROL_LEASE_DUPLICATE_ACK,
  GL30_CONTROL_LEASE_RELEASE_ACCEPTED,
  GL30_CONTROL_LEASE_ACQUIRE_ACCEPTED
} gl30_control_lease_result_t;

void gl30_control_lease_init(gl30_control_lease_t *lease);
bool gl30_control_lease_command_is_fullzero(const gl30_haptic_command_t *command);
bool gl30_control_lease_command_is_allowed(
    const gl30_control_lease_t *lease, const gl30_haptic_command_t *command);
void gl30_control_lease_command_accepted(
    gl30_control_lease_t *lease, const gl30_haptic_command_t *command);
gl30_control_lease_result_t gl30_control_lease_process_request(
    gl30_control_lease_t *lease,
    const gl30_control_lease_request_t *request,
    bool release_allowed);
void gl30_control_lease_ack_submitted(
    gl30_control_lease_t *lease, uint32_t submitted_revision);

#endif
