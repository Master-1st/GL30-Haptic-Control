#include "control_lease.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static bool request_is_valid(const gl30_control_lease_request_t *request) {
  if (request == NULL) {
    return false;
  }
  switch (request->action) {
    case GL30_CONTROL_RELEASE:
      return request->zeroNonce != 0u && request->currentGeneration != 0u &&
             request->nextGeneration == 0u;
    case GL30_CONTROL_ACQUIRE:
      return request->zeroNonce != 0u && request->nextGeneration != 0u &&
             request->nextGeneration != request->currentGeneration;
    case GL30_CONTROL_QUERY:
      return request->zeroNonce == 0u && request->currentGeneration == 0u &&
             request->nextGeneration == 0u;
    default:
      return false;
  }
}

static void request_ack(gl30_control_lease_t *lease) {
  lease->ack_pending = true;
  lease->ack_revision++;
}

void gl30_control_lease_init(gl30_control_lease_t *lease) {
  if (lease != NULL) {
    memset(lease, 0, sizeof(*lease));
  }
}

bool gl30_control_lease_command_is_fullzero(const gl30_haptic_command_t *command) {
  if (command == NULL || command->profileId != 0u || command->modeFlags != 0u ||
      command->textureId != 0u) {
    return false;
  }

  return isfinite(command->targetPositionRad) && command->targetPositionRad == 0.0f &&
         isfinite(command->targetVelocityRadS) && command->targetVelocityRadS == 0.0f &&
         isfinite(command->detentWidthRad) && command->detentWidthRad == 0.0f &&
         isfinite(command->detentStrengthNm) && command->detentStrengthNm == 0.0f &&
         isfinite(command->endstopMinRad) && command->endstopMinRad == 0.0f &&
         isfinite(command->endstopMaxRad) && command->endstopMaxRad == 0.0f &&
         isfinite(command->endstopStrengthNm) && command->endstopStrengthNm == 0.0f &&
         isfinite(command->dampingNmPerRadS) && command->dampingNmPerRadS == 0.0f &&
         isfinite(command->inertiaKgM2) && command->inertiaKgM2 == 0.0f &&
         isfinite(command->frictionNm) && command->frictionNm == 0.0f &&
         isfinite(command->userTorqueLimitNm) && command->userTorqueLimitNm == 0.0f &&
         isfinite(command->activeSpeedLimitRadS) && command->activeSpeedLimitRadS == 0.0f;
}

bool gl30_control_lease_command_is_allowed(
    const gl30_control_lease_t *lease, const gl30_haptic_command_t *command) {
  if (lease == NULL || command == NULL ||
      lease->state != GL30_CONTROL_LEASE_OWNED ||
      command->leaseGeneration != lease->generation) {
    return false;
  }
  if (!lease->awaiting_first_zero) {
    return true;
  }
  return command->commandNonce == lease->zero_nonce &&
         gl30_control_lease_command_is_fullzero(command);
}

void gl30_control_lease_command_accepted(
    gl30_control_lease_t *lease, const gl30_haptic_command_t *command) {
  if (gl30_control_lease_command_is_allowed(lease, command) &&
      lease->awaiting_first_zero) {
    lease->awaiting_first_zero = false;
  }
}

gl30_control_lease_result_t gl30_control_lease_process_request(
    gl30_control_lease_t *lease,
    const gl30_control_lease_request_t *request,
    bool release_allowed) {
  if (lease == NULL || !request_is_valid(request)) {
    return GL30_CONTROL_LEASE_REQUEST_REJECTED;
  }

  if (request->action == GL30_CONTROL_QUERY) {
    request_ack(lease);
    return GL30_CONTROL_LEASE_QUERY_ACK;
  }

  if (request->action == GL30_CONTROL_RELEASE) {
    if (lease->state == GL30_CONTROL_LEASE_RELEASED &&
        request->currentGeneration == lease->generation &&
        request->zeroNonce == lease->zero_nonce) {
      request_ack(lease);
      return GL30_CONTROL_LEASE_DUPLICATE_ACK;
    }
    if (lease->state != GL30_CONTROL_LEASE_OWNED || !release_allowed ||
        request->currentGeneration != lease->generation) {
      return GL30_CONTROL_LEASE_REQUEST_REJECTED;
    }
    lease->state = GL30_CONTROL_LEASE_RELEASED;
    lease->zero_nonce = request->zeroNonce;
    lease->awaiting_first_zero = false;
    request_ack(lease);
    return GL30_CONTROL_LEASE_RELEASE_ACCEPTED;
  }

  if (lease->state == GL30_CONTROL_LEASE_OWNED &&
      lease->awaiting_first_zero &&
      request->nextGeneration == lease->generation &&
      request->currentGeneration == lease->acquire_from_generation &&
      request->zeroNonce == lease->zero_nonce) {
    request_ack(lease);
    return GL30_CONTROL_LEASE_DUPLICATE_ACK;
  }

  if (request->action != GL30_CONTROL_ACQUIRE ||
      request->nextGeneration == lease->generation) {
    return GL30_CONTROL_LEASE_REQUEST_REJECTED;
  }
  if (lease->state == GL30_CONTROL_LEASE_UNOWNED) {
    if (lease->generation != 0u || request->currentGeneration != 0u) {
      return GL30_CONTROL_LEASE_REQUEST_REJECTED;
    }
  } else if (lease->state == GL30_CONTROL_LEASE_RELEASED) {
    if (request->currentGeneration != lease->generation ||
        request->zeroNonce == lease->zero_nonce) {
      return GL30_CONTROL_LEASE_REQUEST_REJECTED;
    }
  } else {
    return GL30_CONTROL_LEASE_REQUEST_REJECTED;
  }

  lease->state = GL30_CONTROL_LEASE_OWNED;
  lease->acquire_from_generation = request->currentGeneration;
  lease->generation = request->nextGeneration;
  lease->zero_nonce = request->zeroNonce;
  lease->awaiting_first_zero = true;
  request_ack(lease);
  return GL30_CONTROL_LEASE_ACQUIRE_ACCEPTED;
}

void gl30_control_lease_ack_submitted(
    gl30_control_lease_t *lease, uint32_t submitted_revision) {
  if (lease != NULL && lease->ack_pending &&
      lease->ack_revision == submitted_revision) {
    lease->ack_pending = false;
  }
}
