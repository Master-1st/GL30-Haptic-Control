#include "v6_protocol.h"

#include <stdint.h>
#include <string.h>

#include "../config/board_config.h"

#define GL30_PROTOCOL_PARSER_BUFFER_BYTES (GL30_FRAME_MAX_PAYLOAD_BYTES + GL30_FRAME_HEADER_BYTES)

static uint8_t parse_buffer[GL30_PROTOCOL_PARSER_BUFFER_BYTES];
static uint8_t parsed_payload[GL30_FRAME_MAX_PAYLOAD_BYTES];
static size_t parse_length;

static void write_u64_le(uint8_t *out, uint64_t value) {
  for (uint32_t i = 0u; i < 8u; ++i) {
    out[i] = (uint8_t)(value >> (8u * i));
  }
}

static uint64_t read_u64_le(const uint8_t *in) {
  uint64_t value = 0u;
  for (uint32_t i = 0u; i < 8u; ++i) {
    value |= (uint64_t)in[i] << (8u * i);
  }
  return value;
}

static void write_u32_le(uint8_t *out, uint32_t value) {
  out[0u] = (uint8_t)value;
  out[1u] = (uint8_t)(value >> 8u);
  out[2u] = (uint8_t)(value >> 16u);
  out[3u] = (uint8_t)(value >> 24u);
}

static uint32_t read_u32_le(const uint8_t *in) {
  return (uint32_t)in[0u] | ((uint32_t)in[1u] << 8u) |
         ((uint32_t)in[2u] << 16u) | ((uint32_t)in[3u] << 24u);
}

static int control_lease_request_valid(const gl30_control_lease_request_t *in) {
  if (in == NULL) {
    return 0;
  }
  switch (in->action) {
    case GL30_CONTROL_RELEASE:
      return in->zeroNonce != 0u && in->currentGeneration != 0u &&
             in->nextGeneration == 0u;
    case GL30_CONTROL_ACQUIRE:
      return in->zeroNonce != 0u && in->nextGeneration != 0u &&
             in->nextGeneration != in->currentGeneration;
    case GL30_CONTROL_QUERY:
      return in->zeroNonce == 0u && in->currentGeneration == 0u &&
             in->nextGeneration == 0u;
    default:
      return 0;
  }
}

void gl30_crc32c(const uint8_t *data, size_t len, uint32_t *out_crc) {
  static uint32_t table[256];
  static uint8_t table_ready;
  uint32_t crc = 0xFFFFFFFFu;

  if (out_crc == NULL || (data == NULL && len != 0u)) {
    return;
  }

  if (!table_ready) {
    for (uint32_t p = 0u; p < 256u; p++) {
      uint32_t c = p;
      for (uint32_t bit = 0u; bit < 8u; bit++) {
        if ((c & 1u) != 0u) {
          c = (c >> 1u) ^ 0x82F63B78u;
        } else {
          c >>= 1u;
        }
      }
      table[p] = c;
    }
    table_ready = 1u;
  }

  for (size_t i = 0u; i < len; i++) {
    crc = (crc >> 8u) ^ table[(crc ^ (uint32_t)data[i]) & 0xFFu];
  }
  *out_crc = crc ^ 0xFFFFFFFFu;
}

int gl30_encode_motor_state_fast(const gl30_motor_state_fast_t *in, uint8_t *out, size_t out_cap) {
  if (in == NULL || out == NULL || out_cap < GL30_MOTOR_STATE_FAST_LEN) {
    return -1;
  }

  memcpy(out + 0u, &in->angleRad, sizeof(float));
  memcpy(out + 4u, &in->velocityRadPerSec, sizeof(float));
  memcpy(out + 8u, &in->accelerationRadPerSec2, sizeof(float));
  memcpy(out + 12u, &in->iqRefA, sizeof(float));
  memcpy(out + 16u, &in->iqMeasA, sizeof(float));
  memcpy(out + 20u, &in->idMeasA, sizeof(float));
  memcpy(out + 24u, &in->torqueCmdNm, sizeof(float));
  memcpy(out + 28u, &in->torqueEstNm, sizeof(float));
  memcpy(out + 32u, &in->busVoltageV, sizeof(float));
  memcpy(out + 36u, &in->busCurrentA, sizeof(float));
  memcpy(out + 40u, &in->logicalPosition, sizeof(int32_t));
  memcpy(out + 44u, &in->subPosition, sizeof(float));
  memcpy(out + 48u, &in->motorState, sizeof(uint32_t));
  memcpy(out + 52u, &in->faultBits, sizeof(uint32_t));
  memcpy(out + 56u, &in->warningBits, sizeof(uint32_t));
  memcpy(out + 60u, &in->isrCycles, sizeof(uint16_t));
  memcpy(out + 62u, &in->encoderStatus, sizeof(uint16_t));
  memcpy(out + 64u, &in->droppedCmds, sizeof(uint16_t));
  memcpy(out + 66u, &in->reserved, sizeof(uint16_t));
  return 0;
}

int gl30_decode_motor_state_fast(const uint8_t *in, size_t in_len, gl30_motor_state_fast_t *out) {
  if (in == NULL || out == NULL || in_len != GL30_MOTOR_STATE_FAST_LEN) {
    return -1;
  }

  memcpy(&out->angleRad, in + 0u, sizeof(float));
  memcpy(&out->velocityRadPerSec, in + 4u, sizeof(float));
  memcpy(&out->accelerationRadPerSec2, in + 8u, sizeof(float));
  memcpy(&out->iqRefA, in + 12u, sizeof(float));
  memcpy(&out->iqMeasA, in + 16u, sizeof(float));
  memcpy(&out->idMeasA, in + 20u, sizeof(float));
  memcpy(&out->torqueCmdNm, in + 24u, sizeof(float));
  memcpy(&out->torqueEstNm, in + 28u, sizeof(float));
  memcpy(&out->busVoltageV, in + 32u, sizeof(float));
  memcpy(&out->busCurrentA, in + 36u, sizeof(float));
  memcpy(&out->logicalPosition, in + 40u, sizeof(int32_t));
  memcpy(&out->subPosition, in + 44u, sizeof(float));
  memcpy(&out->motorState, in + 48u, sizeof(uint32_t));
  memcpy(&out->faultBits, in + 52u, sizeof(uint32_t));
  memcpy(&out->warningBits, in + 56u, sizeof(uint32_t));
  memcpy(&out->isrCycles, in + 60u, sizeof(uint16_t));
  memcpy(&out->encoderStatus, in + 62u, sizeof(uint16_t));
  memcpy(&out->droppedCmds, in + 64u, sizeof(uint16_t));
  memcpy(&out->reserved, in + 66u, sizeof(uint16_t));
  return 0;
}

int gl30_encode_motor_state_slow(
    const gl30_motor_state_slow_t *in, uint8_t *out, size_t out_cap) {
  if (in == NULL || out == NULL || out_cap < GL30_MOTOR_STATE_SLOW_LEN) {
    return -1;
  }

  memcpy(out + 0u, &in->busVoltageV, sizeof(float));
  memcpy(out + 4u, &in->busCurrentA, sizeof(float));
  memcpy(out + 8u, &in->busPowerW, sizeof(float));
  memcpy(out + 12u, &in->energyJ, sizeof(float));
  memcpy(out + 16u, &in->chargeC, sizeof(float));
  memcpy(out + 20u, &in->inaDieTemperatureC, sizeof(float));
  memcpy(out + 24u, &in->motorTemperatureC, sizeof(float));
  memcpy(out + 28u, &in->ambientLux, sizeof(float));
  memcpy(out + 32u, &in->uptimeMs, sizeof(uint32_t));
  memcpy(out + 36u, &in->inaDiag, sizeof(uint32_t));
  memcpy(out + 40u, &in->sensorStatus, sizeof(uint32_t));
  memcpy(out + 44u, &in->inaI2cErrors, sizeof(uint32_t));
  memcpy(out + 48u, &in->vemlI2cErrors, sizeof(uint32_t));
  memcpy(out + 52u, &in->telemetryDrops, sizeof(uint32_t));
  memcpy(out + 56u, &in->encoderCrcErrors, sizeof(uint32_t));
  memcpy(out + 60u, &in->focDeadlineMisses, sizeof(uint32_t));
  return 0;
}

int gl30_decode_motor_state_slow(
    const uint8_t *in, size_t in_len, gl30_motor_state_slow_t *out) {
  if (in == NULL || out == NULL || in_len != GL30_MOTOR_STATE_SLOW_LEN) {
    return -1;
  }

  memcpy(&out->busVoltageV, in + 0u, sizeof(float));
  memcpy(&out->busCurrentA, in + 4u, sizeof(float));
  memcpy(&out->busPowerW, in + 8u, sizeof(float));
  memcpy(&out->energyJ, in + 12u, sizeof(float));
  memcpy(&out->chargeC, in + 16u, sizeof(float));
  memcpy(&out->inaDieTemperatureC, in + 20u, sizeof(float));
  memcpy(&out->motorTemperatureC, in + 24u, sizeof(float));
  memcpy(&out->ambientLux, in + 28u, sizeof(float));
  memcpy(&out->uptimeMs, in + 32u, sizeof(uint32_t));
  memcpy(&out->inaDiag, in + 36u, sizeof(uint32_t));
  memcpy(&out->sensorStatus, in + 40u, sizeof(uint32_t));
  memcpy(&out->inaI2cErrors, in + 44u, sizeof(uint32_t));
  memcpy(&out->vemlI2cErrors, in + 48u, sizeof(uint32_t));
  memcpy(&out->telemetryDrops, in + 52u, sizeof(uint32_t));
  memcpy(&out->encoderCrcErrors, in + 56u, sizeof(uint32_t));
  memcpy(&out->focDeadlineMisses, in + 60u, sizeof(uint32_t));
  return 0;
}

int gl30_encode_haptic_command(const gl30_haptic_command_t *in, uint8_t *out, size_t out_cap) {
  if (in == NULL || out == NULL || out_cap < GL30_HAPTIC_COMMAND_LEN) {
    return -1;
  }

  memcpy(out + 0u, &in->profileId, sizeof(uint32_t));
  memcpy(out + 4u, &in->commandNonce, sizeof(uint32_t));
  memcpy(out + 8u, &in->targetPositionRad, sizeof(float));
  memcpy(out + 12u, &in->targetVelocityRadS, sizeof(float));
  memcpy(out + 16u, &in->detentWidthRad, sizeof(float));
  memcpy(out + 20u, &in->detentStrengthNm, sizeof(float));
  memcpy(out + 24u, &in->endstopMinRad, sizeof(float));
  memcpy(out + 28u, &in->endstopMaxRad, sizeof(float));
  memcpy(out + 32u, &in->endstopStrengthNm, sizeof(float));
  memcpy(out + 36u, &in->dampingNmPerRadS, sizeof(float));
  memcpy(out + 40u, &in->inertiaKgM2, sizeof(float));
  memcpy(out + 44u, &in->frictionNm, sizeof(float));
  memcpy(out + 48u, &in->userTorqueLimitNm, sizeof(float));
  memcpy(out + 52u, &in->activeSpeedLimitRadS, sizeof(float));
  memcpy(out + 56u, &in->modeFlags, sizeof(uint32_t));
  memcpy(out + 60u, &in->textureId, sizeof(uint32_t));
  write_u64_le(out + 64u, in->leaseGeneration);
  return 0;
}

int gl30_decode_haptic_command(const uint8_t *in, size_t in_len, gl30_haptic_command_t *out) {
  if (in == NULL || out == NULL || in_len != GL30_HAPTIC_COMMAND_LEN) {
    return -1;
  }

  memcpy(&out->profileId, in + 0u, sizeof(uint32_t));
  memcpy(&out->commandNonce, in + 4u, sizeof(uint32_t));
  memcpy(&out->targetPositionRad, in + 8u, sizeof(float));
  memcpy(&out->targetVelocityRadS, in + 12u, sizeof(float));
  memcpy(&out->detentWidthRad, in + 16u, sizeof(float));
  memcpy(&out->detentStrengthNm, in + 20u, sizeof(float));
  memcpy(&out->endstopMinRad, in + 24u, sizeof(float));
  memcpy(&out->endstopMaxRad, in + 28u, sizeof(float));
  memcpy(&out->endstopStrengthNm, in + 32u, sizeof(float));
  memcpy(&out->dampingNmPerRadS, in + 36u, sizeof(float));
  memcpy(&out->inertiaKgM2, in + 40u, sizeof(float));
  memcpy(&out->frictionNm, in + 44u, sizeof(float));
  memcpy(&out->userTorqueLimitNm, in + 48u, sizeof(float));
  memcpy(&out->activeSpeedLimitRadS, in + 52u, sizeof(float));
  memcpy(&out->modeFlags, in + 56u, sizeof(uint32_t));
  memcpy(&out->textureId, in + 60u, sizeof(uint32_t));
  out->leaseGeneration = read_u64_le(in + 64u);
  return 0;
}

int gl30_encode_haptic_state(const gl30_haptic_state_t *in, uint8_t *out, size_t out_cap) {
  if (in == NULL || out == NULL || out_cap < GL30_HAPTIC_STATE_LEN) {
    return -1;
  }
  if ((in->status & ~(uint32_t)GL30_HAPTIC_STATE_STATUS_MASK) != 0u ||
      (in->status & (GL30_HAPTIC_STATE_CONTROL_RELEASED |
                     GL30_HAPTIC_STATE_CONTROL_WAITING_ZERO)) ==
          (GL30_HAPTIC_STATE_CONTROL_RELEASED |
           GL30_HAPTIC_STATE_CONTROL_WAITING_ZERO)) {
    return -1;
  }

  memcpy(out + 0u, &in->profileId, sizeof(uint32_t));
  memcpy(out + 4u, &in->commandNonce, sizeof(uint32_t));
  memcpy(out + 8u, &in->modeFlags, sizeof(uint32_t));
  memcpy(out + 12u, &in->logicalPosition, sizeof(int32_t));
  memcpy(out + 16u, &in->subPosition, sizeof(float));
  memcpy(out + 20u, &in->detentWidthRad, sizeof(float));
  memcpy(out + 24u, &in->motorState, sizeof(uint32_t));
  memcpy(out + 28u, &in->faultBits, sizeof(uint32_t));
  memcpy(out + 32u, &in->status, sizeof(uint32_t));
  write_u64_le(out + 36u, in->leaseGeneration);
  return 0;
}

int gl30_decode_haptic_state(const uint8_t *in, size_t in_len, gl30_haptic_state_t *out) {
  uint32_t status;
  if (in == NULL || out == NULL || in_len != GL30_HAPTIC_STATE_LEN) {
    return -1;
  }
  memcpy(&status, in + 32u, sizeof(status));
  if ((status & ~(uint32_t)GL30_HAPTIC_STATE_STATUS_MASK) != 0u ||
      (status & (GL30_HAPTIC_STATE_CONTROL_RELEASED |
                 GL30_HAPTIC_STATE_CONTROL_WAITING_ZERO)) ==
          (GL30_HAPTIC_STATE_CONTROL_RELEASED |
           GL30_HAPTIC_STATE_CONTROL_WAITING_ZERO)) {
    return -1;
  }

  memcpy(&out->profileId, in + 0u, sizeof(uint32_t));
  memcpy(&out->commandNonce, in + 4u, sizeof(uint32_t));
  memcpy(&out->modeFlags, in + 8u, sizeof(uint32_t));
  memcpy(&out->logicalPosition, in + 12u, sizeof(int32_t));
  memcpy(&out->subPosition, in + 16u, sizeof(float));
  memcpy(&out->detentWidthRad, in + 20u, sizeof(float));
  memcpy(&out->motorState, in + 24u, sizeof(uint32_t));
  memcpy(&out->faultBits, in + 28u, sizeof(uint32_t));
  memcpy(&out->status, in + 32u, sizeof(uint32_t));
  out->leaseGeneration = read_u64_le(in + 36u);
  return 0;
}

int gl30_encode_control_lease(
    const gl30_control_lease_request_t *in, uint8_t *out, size_t out_cap) {
  if (in == NULL || out == NULL || out_cap < GL30_CONTROL_LEASE_LEN ||
      !control_lease_request_valid(in)) {
    return -1;
  }
  write_u32_le(out + 0u, in->action);
  write_u32_le(out + 4u, in->zeroNonce);
  write_u64_le(out + 8u, in->currentGeneration);
  write_u64_le(out + 16u, in->nextGeneration);
  return 0;
}

int gl30_decode_control_lease(
    const uint8_t *in, size_t in_len, gl30_control_lease_request_t *out) {
  gl30_control_lease_request_t decoded;
  if (in == NULL || out == NULL || in_len != GL30_CONTROL_LEASE_LEN) {
    return -1;
  }
  decoded = (gl30_control_lease_request_t){
      .action = read_u32_le(in + 0u),
      .zeroNonce = read_u32_le(in + 4u),
      .currentGeneration = read_u64_le(in + 8u),
      .nextGeneration = read_u64_le(in + 16u),
  };
  if (!control_lease_request_valid(&decoded)) {
    return -1;
  }
  *out = decoded;
  return 0;
}

int gl30_frame_encode(uint8_t type, uint16_t flags, uint32_t sequence, uint64_t timestamp_us,
                      const uint8_t *payload, size_t payload_len,
                      uint8_t *out_frame, size_t out_cap, size_t *out_len) {
  const size_t frame_len = GL30_FRAME_HEADER_BYTES + payload_len;

  if (out_frame == NULL || out_len == NULL ||
      (payload == NULL && payload_len != 0u) ||
      payload_len > GL30_FRAME_MAX_PAYLOAD_BYTES || payload_len > 0xFFFFu ||
      frame_len > out_cap) {
    return -1;
  }
  if (type == GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST && payload_len != GL30_MOTOR_STATE_FAST_LEN) {
    return -1;
  }
  if (type == GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_SLOW && payload_len != GL30_MOTOR_STATE_SLOW_LEN) {
    return -1;
  }
  if (type == GL30_V6_FRAME_PAYLOAD_HAPTIC_STATE && payload_len != GL30_HAPTIC_STATE_LEN) {
    return -1;
  }
  if (type == GL30_V6_FRAME_PAYLOAD_HAPTIC_COMMAND && payload_len != GL30_HAPTIC_COMMAND_LEN) {
    return -1;
  }
  if (type == GL30_V6_FRAME_PAYLOAD_CONTROL_LEASE && payload_len != GL30_CONTROL_LEASE_LEN) {
    return -1;
  }

  out_frame[0u] = (uint8_t)(GL30_FRAME_SYNC & 0xFFu);
  out_frame[1u] = (uint8_t)(GL30_FRAME_SYNC >> 8u);
  out_frame[2u] = GL30_FRAME_VERSION;
  out_frame[3u] = type;
  out_frame[4u] = (uint8_t)(payload_len & 0xFFu);
  out_frame[5u] = (uint8_t)(payload_len >> 8u);
  out_frame[6u] = (uint8_t)(flags & 0xFFu);
  out_frame[7u] = (uint8_t)(flags >> 8u);
  out_frame[8u] = (uint8_t)(sequence & 0xFFu);
  out_frame[9u] = (uint8_t)(sequence >> 8u);
  out_frame[10u] = (uint8_t)(sequence >> 16u);
  out_frame[11u] = (uint8_t)(sequence >> 24u);
  out_frame[12u] = (uint8_t)(timestamp_us & 0xFFu);
  out_frame[13u] = (uint8_t)(timestamp_us >> 8u);
  out_frame[14u] = (uint8_t)(timestamp_us >> 16u);
  out_frame[15u] = (uint8_t)(timestamp_us >> 24u);
  out_frame[16u] = (uint8_t)(timestamp_us >> 32u);
  out_frame[17u] = (uint8_t)(timestamp_us >> 40u);
  out_frame[18u] = (uint8_t)(timestamp_us >> 48u);
  out_frame[19u] = (uint8_t)(timestamp_us >> 56u);

  if (payload_len > 0u && payload != NULL) {
    memcpy(out_frame + 20u, payload, payload_len);
  }

  {
    uint32_t crc = 0u;
    gl30_crc32c(out_frame + 2u, 18u + payload_len, &crc);
    out_frame[20u + payload_len + 0u] = (uint8_t)(crc & 0xFFu);
    out_frame[20u + payload_len + 1u] = (uint8_t)(crc >> 8u);
    out_frame[20u + payload_len + 2u] = (uint8_t)(crc >> 16u);
    out_frame[20u + payload_len + 3u] = (uint8_t)(crc >> 24u);
  }

  *out_len = frame_len;
  return 0;
}

void gl30_frame_parse_init(void) {
  memset(parse_buffer, 0u, sizeof(parse_buffer));
  memset(parsed_payload, 0u, sizeof(parsed_payload));
  parse_length = 0u;
}

static void parser_discard(size_t count) {
  if (count >= parse_length) {
    parse_length = 0u;
    return;
  }
  memmove(parse_buffer, parse_buffer + count, parse_length - count);
  parse_length -= count;
}

gl30_parse_result_t gl30_frame_parse(
    const uint8_t *chunk,
    size_t chunk_len,
    gl30_frame_t *frame_out,
    size_t *consumed) {
  gl30_parse_result_t result = {
    .status = GL30_PARSE_NEED_MORE,
    .error = NULL,
    .frame = {0}
  };

  if (consumed != NULL) {
    *consumed = 0u;
  }

  if (chunk_len != 0u) {
    if (chunk == NULL || chunk_len > sizeof(parse_buffer) - parse_length) {
      parse_length = 0u;
      result.status = GL30_PARSE_BAD_LENGTH;
      result.error = "parser buffer overflow";
      return result;
    }
    memcpy(parse_buffer + parse_length, chunk, chunk_len);
    parse_length += chunk_len;
    if (consumed != NULL) {
      *consumed = chunk_len;
    }
  }

  size_t sync_index = (size_t)-1;
  for (size_t index = 0u; index + 1u < parse_length; ++index) {
    if (parse_buffer[index] == 0x5Au && parse_buffer[index + 1u] == 0xA5u) {
      sync_index = index;
      break;
    }
  }
  if (sync_index == (size_t)-1) {
    if (parse_length != 0u && parse_buffer[parse_length - 1u] == 0x5Au) {
      parse_buffer[0] = 0x5Au;
      parse_length = 1u;
    } else {
      parse_length = 0u;
    }
    return result;
  }
  if (sync_index != 0u) {
    parser_discard(sync_index);
  }
  if (parse_length < GL30_FRAME_HEADER_BYTES) {
    return result;
  }

  const uint16_t payload_len =
      (uint16_t)parse_buffer[4] | (uint16_t)((uint16_t)parse_buffer[5] << 8u);
  if (payload_len > GL30_FRAME_MAX_PAYLOAD_BYTES) {
    parser_discard(1u);
    result.status = GL30_PARSE_BAD_PAYLOAD_LEN;
    result.error = "payload length exceeds local maximum";
    return result;
  }
  const size_t frame_len = GL30_FRAME_HEADER_BYTES + (size_t)payload_len;
  if (parse_length < frame_len) {
    return result;
  }

  const uint8_t frame_version = parse_buffer[2];
  if (frame_version != GL30_FRAME_VERSION) {
    parser_discard(frame_len);
    result.status = GL30_PARSE_UNSUPPORTED_VERSION;
    result.error = "unsupported frame version";
    return result;
  }

  const size_t crc_offset = 20u + payload_len;
  const uint32_t received_crc = (uint32_t)parse_buffer[crc_offset] |
      ((uint32_t)parse_buffer[crc_offset + 1u] << 8u) |
      ((uint32_t)parse_buffer[crc_offset + 2u] << 16u) |
      ((uint32_t)parse_buffer[crc_offset + 3u] << 24u);
  uint32_t calculated_crc = 0u;
  gl30_crc32c(parse_buffer + 2u, 18u + payload_len, &calculated_crc);
  if (received_crc != calculated_crc) {
    parser_discard(frame_len);
    result.status = GL30_PARSE_BAD_CRC;
    result.error = "crc mismatch";
    return result;
  }

  if (payload_len != 0u) {
    memcpy(parsed_payload, parse_buffer + 20u, payload_len);
  }
  result.frame = (gl30_frame_t){
      .sync = GL30_FRAME_SYNC,
      .version = frame_version,
      .type = parse_buffer[3],
      .payload_len = payload_len,
      .flags = (uint16_t)parse_buffer[6] |
               (uint16_t)((uint16_t)parse_buffer[7] << 8u),
      .sequence = (uint32_t)parse_buffer[8] |
                  ((uint32_t)parse_buffer[9] << 8u) |
                  ((uint32_t)parse_buffer[10] << 16u) |
                  ((uint32_t)parse_buffer[11] << 24u),
      .timestamp_us = (uint64_t)parse_buffer[12] |
                      ((uint64_t)parse_buffer[13] << 8u) |
                      ((uint64_t)parse_buffer[14] << 16u) |
                      ((uint64_t)parse_buffer[15] << 24u) |
                      ((uint64_t)parse_buffer[16] << 32u) |
                      ((uint64_t)parse_buffer[17] << 40u) |
                      ((uint64_t)parse_buffer[18] << 48u) |
                      ((uint64_t)parse_buffer[19] << 56u),
      .payload = parsed_payload,
      .crc32c = received_crc,
  };
  if (frame_out != NULL) {
    *frame_out = result.frame;
  }
  parser_discard(frame_len);
  result.status = GL30_PARSE_OK;
  return result;
}
