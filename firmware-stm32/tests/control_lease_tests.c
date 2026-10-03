/* Host tests for the real STM32 control-lease state machine and wire codec. */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../control/control_lease.h"
#include "../config/board_config.h"
#include "../safety/safety_supervisor.h"

static unsigned checks;
static unsigned failures;

#define CHECK(condition, message)                                                \
  do {                                                                           \
    ++checks;                                                                    \
    if (!(condition)) {                                                          \
      ++failures;                                                                \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, (message));      \
    }                                                                            \
  } while (0)

static void write_u32_le_for_test(uint8_t *out, uint32_t value) {
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8u);
  out[2] = (uint8_t)(value >> 16u);
  out[3] = (uint8_t)(value >> 24u);
}

static void test_control_lease_wire_golden_and_validation(void) {
  const gl30_control_lease_request_t acquire = {
      .action = GL30_CONTROL_ACQUIRE,
      .zeroNonce = 0x12345678u,
      .currentGeneration = UINT64_C(0x0123456789ABCDEF),
      .nextGeneration = UINT64_C(0xFEDCBA9876543210),
  };
  const uint8_t expected[GL30_CONTROL_LEASE_LEN] = {
      0x01, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
      0xEF, 0xCD, 0xAB, 0x89, 0x67, 0x45, 0x23, 0x01,
      0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
  };
  uint8_t payload[GL30_CONTROL_LEASE_LEN + 1u];
  gl30_control_lease_request_t decoded;

  memset(payload, 0xA5, sizeof(payload));
  CHECK(gl30_encode_control_lease(&acquire, payload, sizeof(payload)) == 0,
        "ACQUIRE payload encodes");
  CHECK(memcmp(payload, expected, sizeof(expected)) == 0,
        "ACQUIRE payload matches the independent 24-byte little-endian golden");
  CHECK(payload[GL30_CONTROL_LEASE_LEN] == 0xA5u,
        "encoder writes exactly the 24-byte request");

  memset(&decoded, 0, sizeof(decoded));
  CHECK(gl30_decode_control_lease(payload, GL30_CONTROL_LEASE_LEN, &decoded) == 0,
        "golden ACQUIRE payload decodes");
  CHECK(decoded.action == acquire.action && decoded.zeroNonce == acquire.zeroNonce &&
            decoded.currentGeneration == acquire.currentGeneration &&
            decoded.nextGeneration == acquire.nextGeneration,
        "decoded request retains both 64-bit generations");

  {
    uint8_t frame[GL30_FRAME_HEADER_BYTES + GL30_CONTROL_LEASE_LEN];
    size_t frame_len = 0u;
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_CONTROL_LEASE, 0u, 7u, 123456u,
                            expected, GL30_CONTROL_LEASE_LEN, frame, sizeof(frame),
                            &frame_len) == 0,
          "framing accepts the exact 24-byte CONTROL_LEASE payload");
    CHECK(frame_len == sizeof(frame), "CONTROL_LEASE frame includes header plus all 24 bytes");
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_CONTROL_LEASE, 0u, 7u, 123456u,
                            expected, GL30_CONTROL_LEASE_LEN - 1u, frame, sizeof(frame),
                            &frame_len) < 0,
          "framing rejects a short CONTROL_LEASE shape");
    CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_CONTROL_LEASE, 0u, 7u, 123456u,
                            expected, GL30_CONTROL_LEASE_LEN + 1u, frame, sizeof(frame),
                            &frame_len) < 0,
          "framing rejects an oversized CONTROL_LEASE shape");
  }

  CHECK(gl30_encode_control_lease(&acquire, payload, GL30_CONTROL_LEASE_LEN - 1u) < 0,
        "undersized output rejects without encoding a partial request");
  CHECK(gl30_encode_control_lease(NULL, payload, sizeof(payload)) < 0,
        "NULL encode input rejects");
  CHECK(gl30_encode_control_lease(&acquire, NULL, sizeof(payload)) < 0,
        "NULL encode output rejects");
  CHECK(gl30_decode_control_lease(payload, GL30_CONTROL_LEASE_LEN - 1u, &decoded) < 0,
        "23-byte request rejects");
  CHECK(gl30_decode_control_lease(payload, GL30_CONTROL_LEASE_LEN + 1u, &decoded) < 0,
        "25-byte request rejects");
  CHECK(gl30_decode_control_lease(NULL, GL30_CONTROL_LEASE_LEN, &decoded) < 0,
        "NULL decode input rejects");
  CHECK(gl30_decode_control_lease(payload, GL30_CONTROL_LEASE_LEN, NULL) < 0,
        "NULL decode output rejects");

  {
    const gl30_control_lease_request_t sentinel = {
        .action = 0xABCDEF01u,
        .zeroNonce = 0x10203040u,
        .currentGeneration = UINT64_C(0x1122334455667788),
        .nextGeneration = UINT64_C(0x8877665544332211),
    };
    uint8_t malformed[GL30_CONTROL_LEASE_LEN];
    gl30_control_lease_request_t before;

    decoded = sentinel;
    memcpy(malformed, expected, sizeof(malformed));
    write_u32_le_for_test(malformed, 3u);
    CHECK(gl30_decode_control_lease(malformed, sizeof(malformed), &decoded) < 0,
          "unknown request action rejects");
    CHECK(decoded.action == sentinel.action && decoded.zeroNonce == sentinel.zeroNonce &&
              decoded.currentGeneration == sentinel.currentGeneration &&
              decoded.nextGeneration == sentinel.nextGeneration,
          "malformed decode leaves caller output unchanged");

    {
      const gl30_control_lease_request_t invalid[] = {
          {GL30_CONTROL_RELEASE, 0u, 7u, 0u},
          {GL30_CONTROL_RELEASE, 1u, 0u, 0u},
          {GL30_CONTROL_RELEASE, 1u, 7u, 1u},
          {GL30_CONTROL_ACQUIRE, 0u, 7u, 8u},
          {GL30_CONTROL_ACQUIRE, 1u, 7u, 0u},
          {GL30_CONTROL_ACQUIRE, 1u, 7u, 7u},
          {GL30_CONTROL_QUERY, 1u, 0u, 0u},
          {GL30_CONTROL_QUERY, 0u, 1u, 0u},
          {GL30_CONTROL_QUERY, 0u, 0u, 1u},
      };
      const size_t invalid_count = sizeof(invalid) / sizeof(invalid[0]);
      for (size_t i = 0u; i < invalid_count; ++i) {
        memset(malformed, 0, sizeof(malformed));
        write_u32_le_for_test(malformed, invalid[i].action);
        write_u32_le_for_test(malformed + 4u, invalid[i].zeroNonce);
        for (unsigned byte = 0u; byte < 8u; ++byte) {
          malformed[8u + byte] = (uint8_t)(invalid[i].currentGeneration >> (8u * byte));
          malformed[16u + byte] = (uint8_t)(invalid[i].nextGeneration >> (8u * byte));
        }
        before = sentinel;
        decoded = sentinel;
        CHECK(gl30_decode_control_lease(malformed, sizeof(malformed), &decoded) < 0,
              "malformed action shape rejects");
        CHECK(decoded.action == before.action && decoded.zeroNonce == before.zeroNonce &&
                  decoded.currentGeneration == before.currentGeneration &&
                  decoded.nextGeneration == before.nextGeneration,
              "invalid action shape does not publish partial fields");
        CHECK(gl30_encode_control_lease(&invalid[i], malformed, sizeof(malformed)) < 0,
              "encoder rejects malformed action shape");
      }
    }
  }
}

static void test_haptic_payload_lengths_and_generation_wire(void) {
  const uint64_t generation = UINT64_C(0xFEDCBA9876543210);
  uint8_t command_bytes[GL30_HAPTIC_COMMAND_LEN];
  uint8_t state_bytes[GL30_HAPTIC_STATE_LEN];
  gl30_haptic_command_t command = {0};
  gl30_haptic_command_t command_out;
  gl30_haptic_state_t state = {0};
  gl30_haptic_state_t state_out;

  command.profileId = 9u;
  command.commandNonce = 0x87654321u;
  command.userTorqueLimitNm = 0.125f;
  command.leaseGeneration = generation;
  memset(command_bytes, 0, sizeof(command_bytes));
  CHECK(gl30_encode_haptic_command(&command, command_bytes, sizeof(command_bytes)) == 0,
        "72-byte HAPTIC_COMMAND encodes");
  CHECK(command_bytes[64] == 0x10u && command_bytes[65] == 0x32u &&
            command_bytes[66] == 0x54u && command_bytes[67] == 0x76u &&
            command_bytes[68] == 0x98u && command_bytes[69] == 0xBAu &&
            command_bytes[70] == 0xDCu && command_bytes[71] == 0xFEu,
        "command generation occupies appended little-endian bytes 64..71");
  memset(&command_out, 0, sizeof(command_out));
  CHECK(gl30_decode_haptic_command(command_bytes, sizeof(command_bytes), &command_out) == 0,
        "72-byte HAPTIC_COMMAND decodes");
  CHECK(command_out.profileId == command.profileId &&
            command_out.commandNonce == command.commandNonce &&
            command_out.userTorqueLimitNm == command.userTorqueLimitNm &&
            command_out.leaseGeneration == generation,
        "command round-trip preserves fields and generation above 32 bits");
  command_out.leaseGeneration = UINT64_C(0xAABBCCDDEEFF0011);
  CHECK(gl30_decode_haptic_command(command_bytes, 64u, &command_out) < 0,
        "legacy 64-byte HAPTIC_COMMAND is rejected");
  CHECK(command_out.leaseGeneration == UINT64_C(0xAABBCCDDEEFF0011),
        "short command rejection leaves the prior output intact");
  CHECK(gl30_decode_haptic_command(command_bytes, sizeof(command_bytes) - 1u,
                                   &command_out) < 0,
        "71-byte HAPTIC_COMMAND is rejected");

  state.profileId = 4u;
  state.commandNonce = 0xA1B2C3D4u;
  state.status = GL30_HAPTIC_STATE_CONTROL_RELEASED;
  state.leaseGeneration = generation;
  memset(state_bytes, 0, sizeof(state_bytes));
  CHECK(gl30_encode_haptic_state(&state, state_bytes, sizeof(state_bytes)) == 0,
        "44-byte HAPTIC_STATE encodes");
  CHECK(state_bytes[36] == 0x10u && state_bytes[37] == 0x32u &&
            state_bytes[38] == 0x54u && state_bytes[39] == 0x76u &&
            state_bytes[40] == 0x98u && state_bytes[41] == 0xBAu &&
            state_bytes[42] == 0xDCu && state_bytes[43] == 0xFEu,
        "state generation occupies appended little-endian bytes 36..43");
  memset(&state_out, 0, sizeof(state_out));
  CHECK(gl30_decode_haptic_state(state_bytes, sizeof(state_bytes), &state_out) == 0,
        "44-byte HAPTIC_STATE decodes");
  CHECK(state_out.profileId == state.profileId &&
            state_out.commandNonce == state.commandNonce &&
            state_out.status == state.status && state_out.leaseGeneration == generation,
        "state round-trip preserves status and full generation");

  {
    uint8_t malformed[GL30_HAPTIC_STATE_LEN];
    uint8_t unchanged[GL30_HAPTIC_STATE_LEN];
    uint8_t unchanged_state[sizeof(state_out)];

    memcpy(malformed, state_bytes, sizeof(malformed));
    write_u32_le_for_test(malformed + 32u, 0x10u);
    memset(&state_out, 0xA5, sizeof(state_out));
    memcpy(unchanged_state, &state_out, sizeof(state_out));
    CHECK(gl30_decode_haptic_state(malformed, sizeof(malformed), &state_out) < 0,
          "unknown HAPTIC_STATE status bits reject");
    CHECK(memcmp(&state_out, unchanged_state, sizeof(state_out)) == 0,
          "unknown status rejection leaves caller output unchanged");

    memcpy(malformed, state_bytes, sizeof(malformed));
    write_u32_le_for_test(malformed + 32u,
        GL30_HAPTIC_STATE_CONTROL_RELEASED | GL30_HAPTIC_STATE_CONTROL_WAITING_ZERO);
    memset(&state_out, 0xA5, sizeof(state_out));
    memcpy(unchanged_state, &state_out, sizeof(state_out));
    CHECK(gl30_decode_haptic_state(malformed, sizeof(malformed), &state_out) < 0,
          "RELEASED and WAITING_ZERO status flags are mutually exclusive");
    CHECK(memcmp(&state_out, unchanged_state, sizeof(state_out)) == 0,
          "conflicting status rejection leaves caller output unchanged");

    memset(state_bytes, 0xA5, sizeof(state_bytes));
    memcpy(unchanged, state_bytes, sizeof(unchanged));
    state.status = 0x10u;
    CHECK(gl30_encode_haptic_state(&state, state_bytes, sizeof(state_bytes)) < 0,
          "encoder rejects unknown status bits");
    CHECK(memcmp(state_bytes, unchanged, sizeof(unchanged)) == 0,
          "unknown status encode rejection leaves output bytes unchanged");
    state.status = GL30_HAPTIC_STATE_CONTROL_RELEASED |
                   GL30_HAPTIC_STATE_CONTROL_WAITING_ZERO;
    CHECK(gl30_encode_haptic_state(&state, state_bytes, sizeof(state_bytes)) < 0,
          "encoder rejects conflicting control status flags");
    CHECK(memcmp(state_bytes, unchanged, sizeof(unchanged)) == 0,
          "conflicting status encode rejection leaves output bytes unchanged");
    state.status = GL30_HAPTIC_STATE_CONTROL_RELEASED;
  }

  state_out.leaseGeneration = UINT64_C(0x1122334455667788);
  CHECK(gl30_decode_haptic_state(state_bytes, 36u, &state_out) < 0,
        "legacy 36-byte HAPTIC_STATE is rejected");
  CHECK(state_out.leaseGeneration == UINT64_C(0x1122334455667788),
        "short state rejection leaves the prior output intact");
  CHECK(gl30_decode_haptic_state(state_bytes, sizeof(state_bytes) + 1u, &state_out) < 0,
        "45-byte HAPTIC_STATE is rejected");
}

static gl30_haptic_command_t fullzero_command(uint64_t generation, uint32_t nonce) {
  gl30_haptic_command_t command = {0};
  command.leaseGeneration = generation;
  command.commandNonce = nonce;
  return command;
}

static gl30_control_lease_request_t make_request(uint32_t action, uint64_t current,
                                                 uint64_t next, uint32_t nonce) {
  gl30_control_lease_request_t request = {0};
  request.action = action;
  request.currentGeneration = current;
  request.nextGeneration = next;
  request.zeroNonce = nonce;
  return request;
}

static void test_fullzero_predicate_covers_command_configuration(void) {
  gl30_haptic_command_t command = fullzero_command(UINT64_C(0x123456789ABCDEF0), 0x55AAu);
  float *float_fields[] = {
      &command.targetPositionRad,
      &command.targetVelocityRadS,
      &command.detentWidthRad,
      &command.detentStrengthNm,
      &command.endstopMinRad,
      &command.endstopMaxRad,
      &command.endstopStrengthNm,
      &command.dampingNmPerRadS,
      &command.inertiaKgM2,
      &command.frictionNm,
      &command.userTorqueLimitNm,
      &command.activeSpeedLimitRadS,
  };
  const size_t float_count = sizeof(float_fields) / sizeof(float_fields[0]);

  CHECK(gl30_control_lease_command_is_fullzero(&command),
        "zero command may carry a nonce and generation outside its configuration");
  for (size_t i = 0u; i < float_count; ++i) {
    *float_fields[i] = 0.25f;
    CHECK(!gl30_control_lease_command_is_fullzero(&command),
          "each configured float must be zero");
    *float_fields[i] = 0.0f;
  }
  command.profileId = 1u;
  CHECK(!gl30_control_lease_command_is_fullzero(&command), "nonzero profile rejects zero shape");
  command.profileId = 0u;
  command.modeFlags = 1u;
  CHECK(!gl30_control_lease_command_is_fullzero(&command), "nonzero mode rejects zero shape");
  command.modeFlags = 0u;
  command.textureId = 1u;
  CHECK(!gl30_control_lease_command_is_fullzero(&command),
        "nonzero texture rejects zero shape");
  command.textureId = 0u;

  for (size_t i = 0u; i < float_count; ++i) {
    *float_fields[i] = NAN;
    CHECK(!gl30_control_lease_command_is_fullzero(&command), "NaN is never a full-zero value");
    *float_fields[i] = INFINITY;
    CHECK(!gl30_control_lease_command_is_fullzero(&command),
          "infinite configuration is never a full-zero value");
    *float_fields[i] = 0.0f;
  }
  CHECK(!gl30_control_lease_command_is_fullzero(NULL), "NULL command is not full zero");
}

static void test_unowned_acquire_first_zero_and_generation_gate(void) {
  const uint64_t generation = UINT64_C(0xFEDCBA9876543210);
  const uint64_t old_generation = UINT64_C(0x0123456789ABCDEF);
  const uint32_t nonce = 0x10293847u;
  gl30_control_lease_t lease;
  gl30_haptic_command_t command;
  gl30_control_lease_request_t request;

  gl30_control_lease_init(&lease);
  CHECK(lease.state == GL30_CONTROL_LEASE_UNOWNED && lease.generation == 0u &&
            lease.zero_nonce == 0u && !lease.awaiting_first_zero && !lease.ack_pending &&
            lease.ack_revision == 0u,
        "initial lease is unowned and has no pending acknowledgement");
  command = fullzero_command(generation, nonce);
  CHECK(!gl30_control_lease_command_is_allowed(&lease, &command),
        "UNOWNED rejects an ordinary zero heartbeat");
  command.userTorqueLimitNm = 0.1f;
  CHECK(!gl30_control_lease_command_is_allowed(&lease, &command),
        "UNOWNED rejects a positive command");

  request = make_request(GL30_CONTROL_ACQUIRE, 0u, generation, nonce);
  CHECK(gl30_control_lease_process_request(&lease, &request, false) ==
            GL30_CONTROL_LEASE_ACQUIRE_ACCEPTED,
        "valid acquisition establishes a new owned generation");
  CHECK(lease.state == GL30_CONTROL_LEASE_OWNED && lease.generation == generation &&
            lease.acquire_from_generation == 0u && lease.zero_nonce == nonce &&
            lease.awaiting_first_zero && lease.ack_pending,
        "acquisition waits for its matching first zero heartbeat");

  command = fullzero_command(generation, nonce);
  command.userTorqueLimitNm = 0.1f;
  CHECK(!gl30_control_lease_command_is_allowed(&lease, &command),
        "positive command cannot skip the acquisition zero heartbeat");
  command = fullzero_command(generation, nonce + 1u);
  CHECK(!gl30_control_lease_command_is_allowed(&lease, &command),
        "first zero heartbeat must match acquisition nonce");
  command = fullzero_command(old_generation, nonce);
  CHECK(!gl30_control_lease_command_is_allowed(&lease, &command),
        "first zero heartbeat must carry the new 64-bit generation");

  command = fullzero_command(generation, nonce);
  CHECK(gl30_control_lease_command_is_allowed(&lease, &command),
        "exact first zero heartbeat is accepted");
  {
    const uint32_t acquire_ack_revision = lease.ack_revision;
    gl30_control_lease_ack_submitted(&lease, acquire_ack_revision);
    CHECK(!lease.ack_pending && lease.awaiting_first_zero,
          "ACQUIRE state acknowledgement does not itself open the command gate");
  }
  gl30_control_lease_command_accepted(&lease, &command);
  CHECK(!lease.awaiting_first_zero && !lease.ack_pending,
        "accepted first ordinary zero opens the gate after a separate ACQUIRE acknowledgement");
  command.userTorqueLimitNm = 0.1f;
  CHECK(gl30_control_lease_command_is_allowed(&lease, &command),
        "current generation may send a positive command after the first zero");
  command.leaseGeneration = old_generation;
  CHECK(!gl30_control_lease_command_is_allowed(&lease, &command),
        "old positive command generation is rejected after acquisition");

  request = make_request(GL30_CONTROL_ACQUIRE, 0u, old_generation, 0x77889900u);
  CHECK(gl30_control_lease_process_request(&lease, &request, true) ==
            GL30_CONTROL_LEASE_REQUEST_REJECTED,
        "another acquisition cannot replace a live owned generation");
  CHECK(lease.generation == generation && !lease.awaiting_first_zero,
        "rejected acquisition leaves the active generation and gate intact");
}

static void test_release_reacquire_and_duplicate_requests(void) {
  const uint64_t generation = UINT64_C(0x100000002);
  const uint64_t next_generation = UINT64_C(0x200000003);
  const uint32_t nonce = 0x13572468u;
  const uint32_t release_nonce = 0x24681357u;
  const uint32_t next_nonce = 0xABCDEF01u;
  gl30_control_lease_t lease;
  gl30_control_lease_request_t request;
  gl30_haptic_command_t command;
  uint64_t saved_revision;

  gl30_control_lease_init(&lease);
  request = make_request(GL30_CONTROL_ACQUIRE, 0u, generation, nonce);
  CHECK(gl30_control_lease_process_request(&lease, &request, false) ==
            GL30_CONTROL_LEASE_ACQUIRE_ACCEPTED,
        "fixture acquires first lease");
  command = fullzero_command(generation, nonce);
  CHECK(gl30_control_lease_command_is_allowed(&lease, &command),
        "fixture permits exact first zero");
  gl30_control_lease_command_accepted(&lease, &command);
  command.userTorqueLimitNm = 0.25f;
  CHECK(gl30_control_lease_command_is_allowed(&lease, &command),
        "fixture reaches ordinary owned-command phase");

  request = make_request(GL30_CONTROL_RELEASE, generation + 1u, 0u, release_nonce);
  CHECK(gl30_control_lease_process_request(&lease, &request, true) ==
            GL30_CONTROL_LEASE_REQUEST_REJECTED,
        "release with stale generation is rejected");
  request = make_request(GL30_CONTROL_RELEASE, generation, 0u, release_nonce);
  CHECK(gl30_control_lease_process_request(&lease, &request, false) ==
            GL30_CONTROL_LEASE_REQUEST_REJECTED,
        "release without caller-confirmed safe applied state is rejected");
  CHECK(lease.state == GL30_CONTROL_LEASE_OWNED && lease.generation == generation,
        "rejected release leaves the current owner intact");

  CHECK(gl30_control_lease_process_request(&lease, &request, true) ==
            GL30_CONTROL_LEASE_RELEASE_ACCEPTED,
        "qualified release moves the generation to RELEASED");
  CHECK(lease.state == GL30_CONTROL_LEASE_RELEASED && lease.generation == generation &&
            lease.zero_nonce == release_nonce && !lease.awaiting_first_zero,
        "released state records the release generation and nonce");
  command = fullzero_command(generation, release_nonce);
  CHECK(!gl30_control_lease_command_is_allowed(&lease, &command),
        "RELEASED rejects ordinary zero commands instead of implicitly reacquiring");
  command.userTorqueLimitNm = 0.1f;
  CHECK(!gl30_control_lease_command_is_allowed(&lease, &command),
        "RELEASED rejects ordinary positive commands");

  saved_revision = lease.ack_revision;
  CHECK(gl30_control_lease_process_request(&lease, &request, false) ==
            GL30_CONTROL_LEASE_DUPLICATE_ACK,
        "exact duplicate release requests an idempotent acknowledgement");
  CHECK(lease.state == GL30_CONTROL_LEASE_RELEASED && lease.generation == generation &&
            lease.zero_nonce == release_nonce && lease.ack_revision != saved_revision,
        "duplicate release changes only acknowledgement bookkeeping");
  request = make_request(GL30_CONTROL_RELEASE, generation, 0u, release_nonce + 1u);
  CHECK(gl30_control_lease_process_request(&lease, &request, true) ==
            GL30_CONTROL_LEASE_REQUEST_REJECTED,
        "different nonce cannot masquerade as an already released request");

  request = make_request(GL30_CONTROL_ACQUIRE, generation + 1u, next_generation, next_nonce);
  CHECK(gl30_control_lease_process_request(&lease, &request, false) ==
            GL30_CONTROL_LEASE_REQUEST_REJECTED,
        "acquisition from RELEASED must identify the released generation");
  request = make_request(GL30_CONTROL_ACQUIRE, generation, generation, next_nonce);
  CHECK(gl30_control_lease_process_request(&lease, &request, false) ==
            GL30_CONTROL_LEASE_REQUEST_REJECTED,
        "acquisition cannot reuse the current generation");
  request = make_request(GL30_CONTROL_ACQUIRE, generation, next_generation, release_nonce);
  CHECK(gl30_control_lease_process_request(&lease, &request, false) ==
            GL30_CONTROL_LEASE_REQUEST_REJECTED,
        "new acquisition cannot reuse the release nonce");

  request = make_request(GL30_CONTROL_ACQUIRE, generation, next_generation, next_nonce);
  CHECK(gl30_control_lease_process_request(&lease, &request, false) ==
            GL30_CONTROL_LEASE_ACQUIRE_ACCEPTED,
        "released owner can acquire a distinct generation and nonce");
  CHECK(lease.state == GL30_CONTROL_LEASE_OWNED && lease.generation == next_generation &&
            lease.acquire_from_generation == generation &&
            lease.zero_nonce == next_nonce && lease.awaiting_first_zero,
        "new acquisition starts behind its own first-zero gate");
  command = fullzero_command(generation, release_nonce);
  CHECK(!gl30_control_lease_command_is_allowed(&lease, &command),
        "old-generation zero is rejected after reacquisition");
  command.userTorqueLimitNm = 0.3f;
  CHECK(!gl30_control_lease_command_is_allowed(&lease, &command),
        "old-generation positive command is rejected after reacquisition");
  command = fullzero_command(next_generation, next_nonce);
  command.profileId = 1u;
  CHECK(!gl30_control_lease_command_is_allowed(&lease, &command),
        "first zero gate rejects a configured command even at the new generation");
  command = fullzero_command(next_generation, next_nonce);
  CHECK(gl30_control_lease_command_is_allowed(&lease, &command),
        "new generation requires its matching exact-zero heartbeat");

  saved_revision = lease.ack_revision;
  request = make_request(GL30_CONTROL_ACQUIRE, generation, next_generation, next_nonce);
  CHECK(gl30_control_lease_process_request(&lease, &request, false) ==
            GL30_CONTROL_LEASE_DUPLICATE_ACK,
        "exact repeated acquisition is idempotent while awaiting its first zero");
  CHECK(lease.ack_revision != saved_revision && lease.awaiting_first_zero &&
            lease.generation == next_generation && lease.zero_nonce == next_nonce,
        "duplicate acquisition cannot replace or bypass the pending zero gate");
  gl30_control_lease_command_accepted(&lease, &command);
  CHECK(!lease.awaiting_first_zero,
        "new generation opens only after the accepted exact zero");
  request = make_request(GL30_CONTROL_ACQUIRE, generation, next_generation, next_nonce);
  CHECK(gl30_control_lease_process_request(&lease, &request, false) ==
            GL30_CONTROL_LEASE_REQUEST_REJECTED,
        "repeated acquisition cannot overwrite a profile after first zero");
}

static void test_query_and_ack_revision_fence(void) {
  gl30_control_lease_t lease;
  gl30_control_lease_request_t query = make_request(GL30_CONTROL_QUERY, 0u, 0u, 0u);
  gl30_control_lease_request_t malformed = make_request(GL30_CONTROL_QUERY, 0u, 0u, 1u);
  uint32_t first_revision;
  uint32_t second_revision;

  gl30_control_lease_init(&lease);
  CHECK(gl30_control_lease_process_request(&lease, &query, false) ==
            GL30_CONTROL_LEASE_QUERY_ACK,
        "all-zero QUERY requests a state acknowledgement");
  first_revision = lease.ack_revision;
  CHECK(first_revision != 0u && lease.ack_pending &&
            lease.state == GL30_CONTROL_LEASE_UNOWNED && lease.generation == 0u &&
            lease.zero_nonce == 0u,
        "QUERY does not create or mutate a lease");
  CHECK(gl30_control_lease_process_request(&lease, &malformed, false) ==
            GL30_CONTROL_LEASE_REQUEST_REJECTED,
        "QUERY with any nonzero field rejects");
  CHECK(lease.ack_revision == first_revision,
        "malformed QUERY does not replace pending acknowledgement");

  gl30_control_lease_ack_submitted(&lease, first_revision);
  CHECK(!lease.ack_pending, "matching successful submit clears its pending ACK");
  CHECK(gl30_control_lease_process_request(&lease, &query, false) ==
            GL30_CONTROL_LEASE_QUERY_ACK,
        "a later QUERY creates a new acknowledgement revision");
  second_revision = lease.ack_revision;
  CHECK(second_revision != first_revision && lease.ack_pending,
        "new acknowledgement is distinguishable from a delayed old submission");
  gl30_control_lease_ack_submitted(&lease, first_revision);
  CHECK(lease.ack_pending && lease.ack_revision == second_revision,
        "late completion of the old ACK cannot clear a newer pending request");
  gl30_control_lease_ack_submitted(&lease, second_revision);
  CHECK(!lease.ack_pending,
        "current ACK revision clears after its own successful submission");

  gl30_control_lease_init(&lease);
  query = make_request(GL30_CONTROL_QUERY, 0u, 0u, 0u);
  CHECK(gl30_control_lease_process_request(&lease, &query, false) ==
            GL30_CONTROL_LEASE_QUERY_ACK,
        "fresh fixture requests a QUERY ACK");
  first_revision = lease.ack_revision;
  {
    gl30_control_lease_request_t acquire =
        make_request(GL30_CONTROL_ACQUIRE, 0u, UINT64_C(0x100000004), 0x44556677u);
    CHECK(gl30_control_lease_process_request(&lease, &acquire, false) ==
              GL30_CONTROL_LEASE_ACQUIRE_ACCEPTED,
          "acquisition may supersede a queued QUERY ACK");
  }
  second_revision = lease.ack_revision;
  CHECK(second_revision != first_revision && lease.ack_pending &&
            lease.awaiting_first_zero,
        "ACQUIRE acknowledgement belongs to its own pending first-zero phase");
  gl30_control_lease_ack_submitted(&lease, first_revision);
  CHECK(lease.ack_pending && lease.ack_revision == second_revision,
        "late QUERY completion cannot consume acquisition ACK");
}

static void make_safety_ready(gl30_safety_t *safety, uint64_t now_us) {
  gl30_safety_init(safety, now_us);
  gl30_safety_set_startup_checks(safety, true, true, true, true, true, true);
}

static void test_safety_release_preserves_qualification_faults_and_timeouts(void) {
  const uint64_t command_us = UINT64_C(1000000);
  gl30_safety_t safety;

  make_safety_ready(&safety, 100u);
  CHECK(safety.startup_state == GL30_STARTUP_READY && safety.power_ok &&
            safety.self_test_ok && safety.encoder_ok && safety.driver_ok &&
            safety.adc_zero_ok && safety.alignment_ok,
        "fixture reaches qualified READY state");
  gl30_safety_on_valid_command(&safety, command_us);
  gl30_safety_request_arm(&safety);
  CHECK(safety.startup_state == GL30_STARTUP_ACTIVE && safety.arm_requested,
        "fresh normal command permits explicit safety arm request");
  gl30_safety_set_warning(&safety, GL30_WARNING_COMM_10MS, true);
  gl30_safety_set_warning(&safety, GL30_WARNING_VBUS, true);

  gl30_safety_release_control(&safety);
  CHECK(safety.last_valid_command_us == 0u && safety.comm_state == GL30_COMM_WAITING &&
            !safety.arm_requested && safety.startup_state == GL30_STARTUP_READY,
        "release disarms and returns the communication lease to WAITING");
  CHECK(safety.power_ok && safety.self_test_ok && safety.encoder_ok && safety.driver_ok &&
            safety.adc_zero_ok && safety.alignment_ok && safety.fault_bits == 0u,
        "release retains startup qualifications without introducing a fault");
  CHECK((safety.warning_bits & GL30_WARNING_COMM_10MS) == 0u &&
            (safety.warning_bits & GL30_WARNING_VBUS) != 0u,
        "release clears only the communication-age warning");
  gl30_safety_tick(&safety, command_us + GL30_COMM_LOST_US + 1u);
  CHECK(safety.comm_state == GL30_COMM_WAITING && safety.fault_bits == 0u &&
            safety.startup_state == GL30_STARTUP_READY,
        "released control does not age into COMM_LOST while awaiting a new owner");

  gl30_safety_on_valid_command(&safety, command_us);
  gl30_safety_request_arm(&safety);
  gl30_safety_tick(&safety, command_us + GL30_COMM_WARN_US - 1u);
  CHECK(safety.comm_state == GL30_COMM_NORMAL && safety.startup_state == GL30_STARTUP_ACTIVE,
        "normal communication remains allowed before the 10 ms boundary");
  gl30_safety_tick(&safety, command_us + GL30_COMM_WARN_US);
  CHECK(safety.comm_state == GL30_COMM_WARN &&
            (safety.warning_bits & GL30_WARNING_COMM_10MS) != 0u &&
            gl30_safety_torque_allowed(&safety),
        "10 ms boundary retains the existing warning-only behavior");
  gl30_safety_tick(&safety, command_us + GL30_COMM_SAFE_ZERO_US - 1u);
  CHECK(safety.comm_state == GL30_COMM_WARN && safety.startup_state == GL30_STARTUP_ACTIVE,
        "communication remains in WARN just before the 20 ms boundary");
  gl30_safety_tick(&safety, command_us + GL30_COMM_SAFE_ZERO_US);
  CHECK(safety.comm_state == GL30_COMM_SAFE_ZERO &&
            safety.startup_state == GL30_STARTUP_READY && !safety.arm_requested &&
            !gl30_safety_torque_allowed(&safety),
        "20 ms boundary disarms and commands safe zero");
  gl30_safety_tick(&safety, command_us + GL30_COMM_LOST_US - 1u);
  CHECK(safety.comm_state == GL30_COMM_SAFE_ZERO && safety.fault_bits == 0u,
        "communication remains nonfaulted just before the 100 ms boundary");
  gl30_safety_tick(&safety, command_us + GL30_COMM_LOST_US);
  CHECK(safety.comm_state == GL30_COMM_LOST &&
            (safety.fault_bits & GL30_FAULT_COMM_LOST) != 0u &&
            safety.startup_state == GL30_STARTUP_FAULT_LATCHED,
        "100 ms boundary retains the existing latched COMM_LOST behavior");

  make_safety_ready(&safety, 500u);
  gl30_safety_on_valid_command(&safety, command_us);
  gl30_safety_request_arm(&safety);
  gl30_safety_latch_fault(&safety, GL30_FAULT_ADC_SYNC);
  gl30_safety_set_warning(&safety, GL30_WARNING_VBUS, true);
  const uint32_t latched_faults = safety.fault_bits;
  gl30_safety_release_control(&safety);
  CHECK(safety.fault_bits == latched_faults &&
            safety.startup_state == GL30_STARTUP_FAULT_LATCHED &&
            !safety.arm_requested,
        "release preserves a pre-existing latched fault and fault state");
  CHECK(safety.last_valid_command_us == 0u &&
            (safety.warning_bits & GL30_WARNING_VBUS) != 0u &&
            safety.power_ok && safety.self_test_ok && safety.encoder_ok &&
            safety.driver_ok && safety.adc_zero_ok && safety.alignment_ok,
        "faulted release preserves unrelated warnings and qualification evidence");
}

int main(void) {
  test_control_lease_wire_golden_and_validation();
  test_haptic_payload_lengths_and_generation_wire();
  test_fullzero_predicate_covers_command_configuration();
  test_unowned_acquire_first_zero_and_generation_gate();
  test_release_reacquire_and_duplicate_requests();
  test_query_and_ack_revision_fence();
  test_safety_release_preserves_qualification_faults_and_timeouts();

  if (failures != 0u) {
    fprintf(stderr, "control-lease tests: %u checks, %u failures\n", checks, failures);
    return 1;
  }
  printf("control-lease tests: %u checks passed\n", checks);
  return 0;
}
