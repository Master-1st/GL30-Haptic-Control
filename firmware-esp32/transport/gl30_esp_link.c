#include "gl30_esp_link.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define GL30_ESP_LINK_FAST_KEEP_US 100000ULL
#define GL30_ESP_LINK_SEQ_RESET_THRESHOLD (UINT32_MAX / 2u)
#define GL30_ESP_LINK_HAPTIC_WIDTH_EPSILON 1.0e-5f

static gl30_esp_link_t *s_active_link = NULL;
static bool s_parser_initialized = false;

static bool gl30_esp_link_is_finite(float v) {
  return isfinite(v);
}

static bool gl30_esp_link_is_nonnegative(float v) {
  return isfinite(v) && (v >= 0.0f);
}

static bool gl30_esp_link_fast_is_finite(const gl30_motor_state_fast_t *state) {
  return gl30_esp_link_is_finite(state->angleRad) &&
         gl30_esp_link_is_finite(state->velocityRadPerSec) &&
         gl30_esp_link_is_finite(state->accelerationRadPerSec2) &&
         gl30_esp_link_is_finite(state->iqRefA) &&
         gl30_esp_link_is_finite(state->iqMeasA) &&
         gl30_esp_link_is_finite(state->idMeasA) &&
         gl30_esp_link_is_finite(state->torqueCmdNm) &&
         gl30_esp_link_is_finite(state->torqueEstNm) &&
         gl30_esp_link_is_finite(state->busVoltageV) &&
         gl30_esp_link_is_finite(state->busCurrentA) &&
         gl30_esp_link_is_finite(state->subPosition);
}

static bool gl30_esp_link_slow_is_finite(const gl30_motor_state_slow_t *state) {
  return gl30_esp_link_is_finite(state->busVoltageV) &&
         gl30_esp_link_is_finite(state->busCurrentA) &&
         gl30_esp_link_is_finite(state->busPowerW) &&
         gl30_esp_link_is_finite(state->energyJ) &&
         gl30_esp_link_is_finite(state->chargeC) &&
         gl30_esp_link_is_finite(state->inaDieTemperatureC) &&
         gl30_esp_link_is_finite(state->motorTemperatureC) &&
         gl30_esp_link_is_finite(state->ambientLux);
}

static bool gl30_esp_link_haptic_is_valid(const gl30_haptic_state_t *state) {
  if (!gl30_esp_link_is_finite(state->subPosition) ||
      !gl30_esp_link_is_finite(state->detentWidthRad) ||
      state->detentWidthRad < 0.0f ||
      (state->status & ~GL30_HAPTIC_STATE_STATUS_MASK) != 0u) {
    return false;
  }

  /* A zero width is valid for non-detent snapshots. A ready detent must have
     a positive width before its sub-position can be used by the menu. */
  if ((state->status & GL30_HAPTIC_STATE_DETENT_READY) != 0u &&
      state->detentWidthRad <= GL30_ESP_LINK_HAPTIC_WIDTH_EPSILON) {
    return false;
  }
  return true;
}

static bool gl30_esp_link_haptic_is_stale(const gl30_esp_link_t *link,
                                          const gl30_frame_t *frame) {
  if (!link->has_haptic) {
    return false;
  }

  /* Sequence identifies the snapshot. The source timestamp additionally
     protects against an older frame arriving after a newer one. */
  return frame->sequence == link->last_haptic_seq ||
         (uint32_t)(frame->sequence-link->last_haptic_seq)>INT32_MAX ||
         frame->timestamp_us <= link->last_haptic_timestamp_us;
}

static void gl30_esp_link_track_sequence(gl30_esp_link_t *link,
                                        uint32_t seq,
                                        bool *is_duplicate_out,
                                        bool *wrapped_or_restarted_out) {
  bool is_duplicate = false;
  bool wrapped_or_restarted = false;

  if (!link->has_last_seq) {
    link->last_seq = seq;
    link->has_last_seq = true;
    if (is_duplicate_out != NULL) {
      *is_duplicate_out = false;
    }
    if (wrapped_or_restarted_out != NULL) {
      *wrapped_or_restarted_out = false;
    }
    return;
  }

  const uint32_t delta = seq - link->last_seq;
  if (delta == 0u) {
    is_duplicate = true;
    link->stats.sequence_duplicate_frames++;
  } else if (delta > GL30_ESP_LINK_SEQ_RESET_THRESHOLD) {
    wrapped_or_restarted = true;
  } else if (delta != 1u) {
    link->stats.sequence_gap_events += (uint64_t)delta - 1u;
  }

  link->last_seq = seq;

  if (is_duplicate_out != NULL) {
    *is_duplicate_out = is_duplicate;
  }
  if (wrapped_or_restarted_out != NULL) {
    *wrapped_or_restarted_out = wrapped_or_restarted;
  }
}

static void gl30_esp_link_record_parse_status(gl30_esp_link_t *link, gl30_parse_status_t status) {
  switch (status) {
    case GL30_PARSE_BAD_LENGTH:
      link->stats.length_failed_frames++;
      break;
    case GL30_PARSE_BAD_CRC:
      link->stats.crc_failed_frames++;
      break;
    case GL30_PARSE_BAD_PAYLOAD_LEN:
      link->stats.payload_len_mismatch_frames++;
      break;
    case GL30_PARSE_UNSUPPORTED_VERSION:
      link->stats.version_failed_frames++;
      break;
    case GL30_PARSE_BAD_SYNC:
    case GL30_PARSE_NEED_MORE:
    default:
      break;
  }
}

static int gl30_esp_link_process_fast(gl30_esp_link_t *link, const gl30_frame_t *frame, uint64_t now_us) {
  if (frame->payload_len != GL30_MOTOR_STATE_FAST_LEN) {
    link->stats.payload_len_mismatch_frames++;
    return 0;
  }

  gl30_motor_state_fast_t candidate;
  if (gl30_decode_motor_state_fast(frame->payload, frame->payload_len, &candidate) != 0 ||
      !gl30_esp_link_fast_is_finite(&candidate)) {
    link->stats.bad_float_frames++;
    return 0;
  }

  bool is_duplicate = false;
  bool wrapped_or_restarted = false;
  gl30_esp_link_track_sequence(link, frame->sequence, &is_duplicate, &wrapped_or_restarted);
  if(wrapped_or_restarted && link->has_fast && frame->timestamp_us<link->last_fast_timestamp_us) {
    /* A source reboot invalidates the old detent epoch. The session still
     * requires a new nonce and explicit arm before accepting it again. */
    link->has_haptic=false;
  }

  if (!is_duplicate) {
    memcpy(&link->latest_fast, &candidate, sizeof(candidate));
    link->has_fast = true;
    link->last_fast_us = now_us;
    link->last_fast_timestamp_us = frame->timestamp_us;
    link->stats.fast_frames++;
  }

  return 0;
}

static int gl30_esp_link_process_slow(gl30_esp_link_t *link, const gl30_frame_t *frame, uint64_t now_us) {
  if (frame->payload_len != GL30_MOTOR_STATE_SLOW_LEN) {
    link->stats.payload_len_mismatch_frames++;
    return 0;
  }

  gl30_motor_state_slow_t candidate;
  if (gl30_decode_motor_state_slow(frame->payload, frame->payload_len, &candidate) != 0 ||
      !gl30_esp_link_slow_is_finite(&candidate)) {
    link->stats.bad_float_frames++;
    return 0;
  }

  bool is_duplicate = false;
  bool wrapped_or_restarted = false;
  gl30_esp_link_track_sequence(link, frame->sequence, &is_duplicate, &wrapped_or_restarted);
  (void)wrapped_or_restarted;

  if (!is_duplicate) {
    memcpy(&link->latest_slow, &candidate, sizeof(candidate));
    link->has_slow = true;
    link->last_slow_us = now_us;
    link->stats.slow_frames++;
  }

  return 0;
}

static int gl30_esp_link_process_haptic(gl30_esp_link_t *link,
                                        const gl30_frame_t *frame,
                                        uint64_t now_us) {
  if (frame->payload_len != GL30_HAPTIC_STATE_LEN) {
    link->stats.payload_len_mismatch_frames++;
    return 0;
  }

  gl30_haptic_state_t candidate;
  if (gl30_decode_haptic_state(frame->payload, frame->payload_len, &candidate) != 0 ||
      !gl30_esp_link_haptic_is_valid(&candidate)) {
    link->stats.bad_float_frames++;
    return 0;
  }

  /* Do this before touching the shared sequence tracker so an old haptic
     sample cannot move the receive epoch backwards for fast/slow frames. */
  if (gl30_esp_link_haptic_is_stale(link, frame)) {
    if (frame->sequence == link->last_haptic_seq) {
      link->stats.sequence_duplicate_frames++;
    }
    return 0;
  }

  bool is_duplicate = false;
  bool wrapped_or_restarted = false;
  gl30_esp_link_track_sequence(link, frame->sequence, &is_duplicate,
                                &wrapped_or_restarted);
  (void)wrapped_or_restarted;

  if (!is_duplicate) {
    memcpy(&link->latest_haptic, &candidate, sizeof(candidate));
    link->has_haptic = true;
    link->last_haptic_us = now_us;
    link->last_haptic_seq = frame->sequence;
    link->last_haptic_timestamp_us = frame->timestamp_us;
    link->stats.haptic_frames++;
  }

  return 0;
}

static int gl30_esp_link_process_ok_frame(gl30_esp_link_t *link, const gl30_frame_t *frame, uint64_t now_us) {
  link->stats.parsed_frames++;

  switch (frame->type) {
    case GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST:
      return gl30_esp_link_process_fast(link, frame, now_us);
    case GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_SLOW:
      return gl30_esp_link_process_slow(link, frame, now_us);
    case GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE:
      return gl30_esp_link_process_haptic(link, frame, now_us);
    default:
      link->stats.unknown_frames++;
      return 0;
  }
}

static int gl30_esp_link_drain_pending(gl30_esp_link_t *link, uint64_t now_us, bool *need_more_out) {
  gl30_frame_t frame = {0};
  if (need_more_out != NULL) {
    *need_more_out = false;
  }

  while (1) {
    size_t consumed = 0u;
    gl30_parse_result_t parse_result = gl30_frame_parse(NULL, 0u, &frame, &consumed);
    if (consumed != 0u) {
      return -3;
    }

    if (parse_result.status == GL30_PARSE_NEED_MORE) {
      if (need_more_out != NULL) {
        *need_more_out = true;
      }
      return 0;
    }

    if (need_more_out != NULL) {
      *need_more_out = false;
    }

    if (parse_result.status != GL30_PARSE_OK) {
      gl30_esp_link_record_parse_status(link, parse_result.status);
      continue;
    }

    if (gl30_esp_link_process_ok_frame(link, &frame, now_us) != 0) {
      return -4;
    }
  }
}

void gl30_esp_link_init(gl30_esp_link_t *link) {
  if (link == NULL) {
    return;
  }
  memset(link, 0, sizeof(*link));
  gl30_frame_parse_init();
  s_active_link = link;
  s_parser_initialized = true;
}

int gl30_esp_link_feed(gl30_esp_link_t *link, const uint8_t *bytes, size_t len, uint64_t now_us) {
  if (!s_parser_initialized || link == NULL || s_active_link != link || (bytes == NULL && len != 0u)) {
    return -1;
  }

  if (now_us < link->now_us) {
    /* Partial bytes from the old receive epoch cannot create a fresh snapshot. */
    gl30_frame_parse_init();
    link->has_fast = false;
    link->has_slow = false;
    link->has_haptic = false;
    link->has_last_seq = false;
    link->last_seq = 0u;
    link->last_fast_us = 0u;
    link->last_fast_timestamp_us = 0u;
    link->last_slow_us = 0u;
    memset(&link->latest_haptic, 0, sizeof(link->latest_haptic));
    link->last_haptic_us = 0u;
    link->last_haptic_seq = 0u;
    link->last_haptic_timestamp_us = 0u;
  }
  link->now_us = now_us;

  if (bytes == NULL && len == 0u) {
    bool need_more = false;
    const int status = gl30_esp_link_drain_pending(link, now_us, &need_more);
    if (status != 0) {
      return status;
    }
    return 0;
  }

  if (bytes != NULL) {
    link->stats.total_bytes_processed += (uint64_t)len;
  }

  size_t offset = 0u;
  while (offset < len) {
    /* Shared parser does not expose free capacity. Byte admission guarantees
       a full-size frame never overflows when the UART read splits its tail.
       NEED_MORE returns directly; only completed/error frames need draining. */
    const size_t chunk = 1u;

    size_t consumed = 0u;
    gl30_frame_t frame = {0};
    gl30_parse_result_t parse_result = gl30_frame_parse(bytes + offset, chunk, &frame, &consumed);
    if (consumed != chunk) {
      if (consumed == 0u) {
        gl30_esp_link_record_parse_status(link, parse_result.status);
      }
      return -2;
    }
    offset += consumed;
    if (parse_result.status == GL30_PARSE_NEED_MORE) continue;

    if (parse_result.status == GL30_PARSE_OK) {
      if (gl30_esp_link_process_ok_frame(link, &frame, now_us) != 0) {
        return -4;
      }
    } else if (parse_result.status != GL30_PARSE_NEED_MORE) {
      gl30_esp_link_record_parse_status(link, parse_result.status);
    }

    bool need_more = false;
    const int drain_status = gl30_esp_link_drain_pending(link, now_us, &need_more);
    if (drain_status != 0) {
      return drain_status;
    }
  }

  bool final_need_more = false;
  const int final_drain_status = gl30_esp_link_drain_pending(link, now_us, &final_need_more);
  if (final_drain_status != 0) {
    return final_drain_status;
  }

  return 0;
}

bool gl30_esp_link_connected(const gl30_esp_link_t *link, uint64_t now_us) {
  if (link == NULL || !link->has_fast) {
    return false;
  }

  if (now_us < link->last_fast_us) {
    return false;
  }
  return (now_us - link->last_fast_us) <= GL30_ESP_LINK_FAST_KEEP_US;
}

int gl30_esp_link_build_haptic_command_frame(
    const gl30_haptic_command_t *command,
    uint16_t flags,
    uint32_t sequence,
    uint64_t timestamp_us,
    uint8_t *out_frame,
    size_t out_cap,
    size_t *out_len) {
  if (out_len != NULL) *out_len = 0u;
  if (command == NULL || out_frame == NULL || out_len == NULL) {
    return -1;
  }

  if (!gl30_esp_link_is_finite(command->targetPositionRad) ||
      !gl30_esp_link_is_finite(command->targetVelocityRadS) ||
      !gl30_esp_link_is_finite(command->detentWidthRad) ||
      !gl30_esp_link_is_finite(command->detentStrengthNm) ||
      !gl30_esp_link_is_finite(command->endstopMinRad) ||
      !gl30_esp_link_is_finite(command->endstopMaxRad) ||
      !gl30_esp_link_is_finite(command->endstopStrengthNm) ||
      !gl30_esp_link_is_finite(command->dampingNmPerRadS) ||
      !gl30_esp_link_is_finite(command->inertiaKgM2) ||
      !gl30_esp_link_is_finite(command->frictionNm) ||
      !gl30_esp_link_is_finite(command->userTorqueLimitNm) ||
      !gl30_esp_link_is_finite(command->activeSpeedLimitRadS)) {
    return -2;
  }

  if (command->endstopMinRad > command->endstopMaxRad ||
      !gl30_esp_link_is_nonnegative(command->detentWidthRad) ||
      !gl30_esp_link_is_nonnegative(command->detentStrengthNm) ||
      !gl30_esp_link_is_nonnegative(command->endstopStrengthNm) ||
      !gl30_esp_link_is_nonnegative(command->dampingNmPerRadS) ||
      !gl30_esp_link_is_nonnegative(command->inertiaKgM2) ||
      !gl30_esp_link_is_nonnegative(command->frictionNm) ||
      !gl30_esp_link_is_nonnegative(command->userTorqueLimitNm) ||
      !gl30_esp_link_is_nonnegative(command->activeSpeedLimitRadS)) {
    return -3;
  }

  uint8_t payload[GL30_HAPTIC_COMMAND_LEN];
  const int encode_payload = gl30_encode_haptic_command(command, payload, sizeof(payload));
  if (encode_payload != 0) {
    return encode_payload;
  }

  return gl30_frame_encode(
      GL30_V6_FRAME_PAYLOAD_HAPTIC_COMMAND,
      flags,
      sequence,
      timestamp_us,
      payload,
      sizeof(payload),
      out_frame,
      out_cap,
      out_len);
}
