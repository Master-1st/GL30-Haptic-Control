// Unit tests for ESP transport parser and haptic command builder behavior.
// Focused on parser boundaries and regressions required by transport behavior contract.

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "gl30_esp_link.h"

static int g_passed;
static int g_failed;

static void test_fail(const char* name, const char* detail) {
  ++g_failed;
  printf("FAIL %-55s %s\n", name, detail);
}

#define EXPECT_TRUE(expr, name) \
  do { \
    if (!(expr)) { \
      test_fail((name), "condition failed: " #expr); \
      return; \
    } \
  } while (0)

#define EXPECT_INT_EQ(actual, expected, name) \
  do { \
    if ((actual) != (expected)) { \
      char __buf[200]; \
      snprintf(__buf, sizeof(__buf), "expected %lld got %lld", \
               (long long)(expected), (long long)(actual)); \
      test_fail((name), __buf); \
      return; \
    } \
  } while (0)

#define EXPECT_UINT_EQ(actual, expected, name) \
  do { \
    if ((actual) != (expected)) { \
      char __buf[200]; \
      snprintf(__buf, sizeof(__buf), "expected %u got %u", (unsigned)(expected), (unsigned)(actual)); \
      test_fail((name), __buf); \
      return; \
    } \
  } while (0)

#define EXPECT_FLOAT_EQ(actual, expected, eps, name) \
  do { \
    if (!isfinite(actual) || !isfinite(expected) || fabsf((actual) - (expected)) > (eps)) { \
      char __buf[200]; \
      snprintf(__buf, sizeof(__buf), "expected %.8f got %.8f", (float)(expected), (float)(actual)); \
      test_fail((name), __buf); \
      return; \
    } \
  } while (0)

static size_t build_fast_frame(uint8_t *out,
                              size_t out_cap,
                              uint32_t seq,
                              uint64_t timestamp_us,
                              float angle,
                              size_t *out_len) {
  gl30_motor_state_fast_t state = {0};
  state.angleRad = angle;
  state.velocityRadPerSec = 1.0f;
  state.accelerationRadPerSec2 = 2.0f;
  state.iqRefA = 3.0f;
  state.iqMeasA = 4.0f;
  state.idMeasA = 5.0f;
  state.torqueCmdNm = 6.0f;
  state.torqueEstNm = 7.0f;
  state.busVoltageV = 8.0f;
  state.busCurrentA = 9.0f;
  state.logicalPosition = 42;
  state.subPosition = 0.5f;
  state.motorState = 0u;
  state.faultBits = 0u;
  state.warningBits = 0u;
  state.isrCycles = 1u;
  state.encoderStatus = 2u;
  state.droppedCmds = 3u;
  state.reserved = 4u;

  uint8_t payload[GL30_MOTOR_STATE_FAST_LEN];
  if (gl30_encode_motor_state_fast(&state, payload, sizeof(payload)) != 0) {
    return 0u;
  }
  size_t len = 0u;
  if (gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST,
                       0u,
                       seq,
                       timestamp_us,
                       payload,
                       sizeof(payload),
                       out,
                       out_cap,
                       &len) != 0) {
    return 0u;
  }
  if (out_len != NULL) {
    *out_len = len;
  }
  return len;
}

static size_t build_slow_frame(uint8_t *out,
                              size_t out_cap,
                              uint32_t seq,
                              uint64_t timestamp_us,
                              float power,
                              size_t *out_len) {
  gl30_motor_state_slow_t state = {0};
  state.busVoltageV = 11.0f;
  state.busCurrentA = power;
  state.busPowerW = power * 2.0f;
  state.energyJ = 3.0f;
  state.chargeC = 4.0f;
  state.inaDieTemperatureC = 5.0f;
  state.motorTemperatureC = 6.0f;
  state.ambientLux = 7.0f;
  state.uptimeMs = 8u;
  state.inaDiag = 9u;
  state.sensorStatus = 10u;
  state.inaI2cErrors = 11u;
  state.vemlI2cErrors = 12u;
  state.telemetryDrops = 13u;
  state.encoderCrcErrors = 14u;
  state.focDeadlineMisses = 15u;

  uint8_t payload[GL30_MOTOR_STATE_SLOW_LEN];
  if (gl30_encode_motor_state_slow(&state, payload, sizeof(payload)) != 0) {
    return 0u;
  }
  size_t len = 0u;
  if (gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_SLOW,
                       0u,
                       seq,
                       timestamp_us,
                       payload,
                       sizeof(payload),
                       out,
                       out_cap,
                       &len) != 0) {
    return 0u;
  }
  if (out_len != NULL) {
    *out_len = len;
  }
  return len;
}

static size_t build_unknown_payload_frame(uint8_t *out,
                                         size_t out_cap,
                                         uint32_t seq,
                                         uint64_t timestamp_us,
                                         size_t payload_len,
                                         uint8_t fill_byte,
                                         size_t *out_len) {
  if (payload_len == 0u || payload_len > GL30_FRAME_MAX_PAYLOAD_BYTES) {
    return 0u;
  }
  uint8_t payload[GL30_FRAME_MAX_PAYLOAD_BYTES];
  memset(payload, fill_byte, payload_len);
  size_t len = 0u;
  if (gl30_frame_encode(0x7Fu,
                        0u,
                        seq,
                        timestamp_us,
                        payload,
                        payload_len,
                        out,
                        out_cap,
                        &len) != 0) {
    return 0u;
  }
  if (out_len != NULL) {
    *out_len = len;
  }
  return len;
}

static void test_fast_illegal_float_same_seq_then_same_seq_recover_should_refresh(void) {
  const char* name = "naN in fast frame does not freeze next same-seq valid";
  gl30_esp_link_t link;
  gl30_esp_link_init(&link);

  uint8_t valid_seq10[256];
  uint8_t bad_seq11[256];
  uint8_t valid_seq11[256];
  size_t n = 0u;

  EXPECT_TRUE(build_fast_frame(valid_seq10, sizeof(valid_seq10), 10u, 100u, 1.0f, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, valid_seq10, n, 100u), 0, name);
  EXPECT_TRUE(link.has_fast, name);
  EXPECT_UINT_EQ(link.last_fast_us, 100u, name);
  EXPECT_FLOAT_EQ(link.latest_fast.angleRad, 1.0f, 1e-6f, name);

  gl30_motor_state_fast_t bad_state = {0};
  bad_state.angleRad = NAN;
  bad_state.velocityRadPerSec = 1.0f;
  bad_state.accelerationRadPerSec2 = 2.0f;
  bad_state.iqRefA = 3.0f;
  bad_state.iqMeasA = 4.0f;
  bad_state.idMeasA = 5.0f;
  bad_state.torqueCmdNm = 6.0f;
  bad_state.torqueEstNm = 7.0f;
  bad_state.busVoltageV = 8.0f;
  bad_state.busCurrentA = 9.0f;
  bad_state.logicalPosition = 42;
  bad_state.subPosition = 0.5f;
  bad_state.motorState = 0u;
  bad_state.faultBits = 0u;
  bad_state.warningBits = 0u;
  bad_state.isrCycles = 1u;
  bad_state.encoderStatus = 2u;
  bad_state.droppedCmds = 3u;
  bad_state.reserved = 4u;

  uint8_t bad_payload[GL30_MOTOR_STATE_FAST_LEN];
  EXPECT_INT_EQ(gl30_encode_motor_state_fast(&bad_state, bad_payload, sizeof(bad_payload)), 0, name);
  EXPECT_INT_EQ(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST,
                                 0u,
                                 11u,
                                 200u,
                                 bad_payload,
                                 sizeof(bad_payload),
                                 bad_seq11,
                                 sizeof(bad_seq11),
                                 &n), 0, name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, bad_seq11, n, 200u), 0, name);
  EXPECT_UINT_EQ(link.stats.bad_float_frames, 1u, name);
  EXPECT_UINT_EQ(link.last_fast_us, 100u, name);
  EXPECT_FLOAT_EQ(link.latest_fast.angleRad, 1.0f, 1e-6f, name);

  EXPECT_TRUE(build_fast_frame(valid_seq11, sizeof(valid_seq11), 11u, 300u, 3.14f, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, valid_seq11, n, 300u), 0, name);
  EXPECT_UINT_EQ(link.stats.bad_float_frames, 1u, name);
  EXPECT_UINT_EQ(link.last_fast_us, 300u, name);
  EXPECT_FLOAT_EQ(link.latest_fast.angleRad, 3.14f, 1e-6f, name);
}

static void test_byte_chunked_input_parses_frame(void) {
  const char* name = "each byte can be fed independently";
  gl30_esp_link_t link;
  gl30_esp_link_init(&link);

  uint8_t frame[512];
  size_t frame_len = 0u;
  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 1u, 10u, 1.0f, &frame_len) > 0u, name);

  for (size_t i = 0u; i < frame_len; ++i) {
    EXPECT_INT_EQ(gl30_esp_link_feed(&link, frame + i, 1u, (uint64_t)i), 0, name);
  }

  EXPECT_INT_EQ(link.stats.parsed_frames, 1u, name);
  EXPECT_TRUE(link.has_fast, name);
  EXPECT_FLOAT_EQ(link.latest_fast.angleRad, 1.0f, 1e-6f, name);
}

static void test_coalesced_frames_in_one_chunk_parse(void) {
  const char* name = "coalesced frames parse in single chunk";
  gl30_esp_link_t link;
  gl30_esp_link_init(&link);

  uint8_t chunk[512];
  size_t off = 0u;
  size_t n = 0u;
  EXPECT_TRUE(build_fast_frame(chunk + off, sizeof(chunk) - off, 1u, 20u, 10.0f, &n) > 0u, name);
  off += n;
  EXPECT_TRUE(build_fast_frame(chunk + off, sizeof(chunk) - off, 2u, 30u, 11.0f, &n) > 0u, name);
  off += n;

  EXPECT_INT_EQ(gl30_esp_link_feed(&link, chunk, off, 100u), 0, name);
  EXPECT_INT_EQ(link.stats.parsed_frames, 2u, name);
  EXPECT_FLOAT_EQ(link.latest_fast.angleRad, 11.0f, 1e-6f, name);
}

static void test_recover_from_bad_crc_version_garbage_in_chunk(void) {
  const char* name = "bad CRC/version/garbage recover before valid same chunk";
  gl30_esp_link_t link;
  gl30_esp_link_init(&link);

  uint8_t chunk[768];
  size_t off = 0u;
  size_t n = 0u;
  uint8_t frame[256];

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 1u, 100u, 3.0f, &n) > 0u, name);
  memcpy(chunk + off, frame, n);
  off += n;

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 2u, 200u, 4.0f, &n) > 0u, name);
  frame[n - 1u] ^= 0x55u;
  memcpy(chunk + off, frame, n);
  off += n;

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 3u, 300u, 5.0f, &n) > 0u, name);
  frame[2u] = 0x99u;
  memcpy(chunk + off, frame, n);
  off += n;

  chunk[off++] = 0xDEu;
  chunk[off++] = 0xADu;
  chunk[off++] = 0xBEu;
  chunk[off++] = 0xEFu;

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 4u, 400u, 6.0f, &n) > 0u, name);
  memcpy(chunk + off, frame, n);
  off += n;

  EXPECT_INT_EQ(gl30_esp_link_feed(&link, chunk, off, 1000u), 0, name);
  EXPECT_INT_EQ(link.stats.parsed_frames, 2u, name);
  EXPECT_INT_EQ(link.stats.bad_float_frames, 0u, name);
  EXPECT_INT_EQ(link.stats.crc_failed_frames, 1u, name);
  EXPECT_INT_EQ(link.stats.version_failed_frames, 1u, name);
  EXPECT_FLOAT_EQ(link.latest_fast.angleRad, 6.0f, 1e-6f, name);
}

static void test_unknown_4096_unknown_then_fast_all_split_points(void) {
  const char* name = "4096-byte unknown + fast parses at all buffer split points";
  uint8_t unknown_frame[GL30_FRAME_MAX_PAYLOAD_BYTES + GL30_FRAME_HEADER_BYTES];
  size_t unknown_len = 0u;
  uint8_t valid_frame[128];
  size_t valid_len = 0u;
  EXPECT_TRUE(build_unknown_payload_frame(unknown_frame, sizeof(unknown_frame), 1u, 500u,
                                         GL30_FRAME_MAX_PAYLOAD_BYTES, 0x5Au, &unknown_len),
              name);
  EXPECT_TRUE(build_fast_frame(valid_frame, sizeof(valid_frame), 2u, 800u, 8.5f, &valid_len) > 0u, name);

  uint8_t both[sizeof(unknown_frame) + sizeof(valid_frame)];
  memcpy(both, unknown_frame, unknown_len);
  memcpy(both + unknown_len, valid_frame, valid_len);
  const size_t both_len = unknown_len + valid_len;

  for (size_t split = 0u; split <= both_len; ++split) {
    gl30_esp_link_t link;
    gl30_esp_link_init(&link);

    if (split > 0u) {
      EXPECT_INT_EQ(gl30_esp_link_feed(&link, both, split, 700u), 0, name);
    }
    if (split < both_len) {
      EXPECT_INT_EQ(gl30_esp_link_feed(&link, both + split, both_len - split, 701u), 0, name);
    }

    EXPECT_INT_EQ(link.stats.parsed_frames, 2u, name);
    EXPECT_INT_EQ(link.stats.unknown_frames, 1u, name);
    EXPECT_INT_EQ(link.stats.fast_frames, 1u, name);
    EXPECT_FLOAT_EQ(link.latest_fast.angleRad, 8.5f, 1e-6f, name);
  }
}

static void test_illegal_float_does_not_refresh_same_seq_frame(void) {
  const char* name = "illegal float is rejected and same-seq valid does not refresh";
  gl30_esp_link_t link;
  gl30_esp_link_init(&link);

  uint8_t valid_first[256];
  uint8_t bad[256];
  uint8_t valid_same_seq[256];
  size_t n = 0u;

  EXPECT_TRUE(build_fast_frame(valid_first, sizeof(valid_first), 77u, 100u, 3.0f, &n) > 0u, name);
  EXPECT_TRUE(build_fast_frame(valid_same_seq, sizeof(valid_same_seq), 77u, 300u, 7.0f, &n) > 0u, name);

  gl30_motor_state_fast_t bad_state = {0};
  bad_state.angleRad = NAN;
  bad_state.velocityRadPerSec = 1.0f;
  bad_state.accelerationRadPerSec2 = 2.0f;
  bad_state.iqRefA = 3.0f;
  bad_state.iqMeasA = 4.0f;
  bad_state.idMeasA = 5.0f;
  bad_state.torqueCmdNm = 6.0f;
  bad_state.torqueEstNm = 7.0f;
  bad_state.busVoltageV = 8.0f;
  bad_state.busCurrentA = 9.0f;
  bad_state.logicalPosition = 42;
  bad_state.subPosition = 0.5f;
  bad_state.motorState = 0u;
  bad_state.faultBits = 0u;
  bad_state.warningBits = 0u;
  bad_state.isrCycles = 1u;
  bad_state.encoderStatus = 2u;
  bad_state.droppedCmds = 3u;
  bad_state.reserved = 4u;

  uint8_t bad_payload[GL30_MOTOR_STATE_FAST_LEN];
  EXPECT_INT_EQ(gl30_encode_motor_state_fast(&bad_state, bad_payload, sizeof(bad_payload)), 0, name);
  EXPECT_INT_EQ(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_FAST,
                                 0u,
                                 77u,
                                 200u,
                                 bad_payload,
                                 sizeof(bad_payload),
                                 bad,
                                 sizeof(bad),
                                 &n), 0, name);

  EXPECT_INT_EQ(gl30_esp_link_feed(&link, valid_first, n, 100u), 0, name);
  EXPECT_UINT_EQ(link.last_fast_us, 100u, name);

  EXPECT_INT_EQ(gl30_esp_link_feed(&link, bad, n, 200u), 0, name);
  EXPECT_UINT_EQ(link.stats.bad_float_frames, 1u, name);
  EXPECT_UINT_EQ(link.last_fast_us, 100u, name);
  EXPECT_FLOAT_EQ(link.latest_fast.angleRad, 3.0f, 1e-6f, name);

  EXPECT_INT_EQ(gl30_esp_link_feed(&link, valid_same_seq, n, 300u), 0, name);
  EXPECT_UINT_EQ(link.stats.bad_float_frames, 1u, name);
  EXPECT_UINT_EQ(link.last_fast_us, 100u, name);
  EXPECT_FLOAT_EQ(link.latest_fast.angleRad, 3.0f, 1e-6f, name);
  EXPECT_TRUE(gl30_esp_link_connected(&link, 350u), name);
}

static void test_slow_decode_does_not_refresh_fast_freshness(void) {
  const char* name = "complete slow frame decodes and does not refresh fast freshness";
  gl30_esp_link_t link;
  gl30_esp_link_init(&link);

  uint8_t frame[256];
  size_t n = 0u;

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 1u, 101u, 2.5f, &n), name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, frame, n, 101u), 0, name);
  EXPECT_TRUE(link.has_fast, name);
  EXPECT_UINT_EQ(link.last_fast_us, 101u, name);

  EXPECT_TRUE(build_slow_frame(frame, sizeof(frame), 2u, 202u, 10.0f, &n), name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, frame, n, 202u), 0, name);
  EXPECT_TRUE(link.has_slow, name);
  EXPECT_UINT_EQ(link.stats.slow_frames, 1u, name);
  EXPECT_FLOAT_EQ(link.latest_fast.angleRad, 2.5f, 1e-6f, name);
  EXPECT_UINT_EQ(link.last_fast_us, 101u, name);
  EXPECT_INT_EQ(link.stats.parsed_frames, 2u, name);
}

static void test_duplicate_sequence_does_not_refresh(void) {
  const char* name = "duplicate sequence is ignored and should not refresh fresh timestamp";
  gl30_esp_link_t link;
  gl30_esp_link_init(&link);

  uint8_t frame[256];
  size_t n = 0u;
  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 77u, 1000u, 21.0f, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, frame, n, 1000u), 0, name);
  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 77u, 200000u, 22.0f, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, frame, n, 200000u), 0, name);

  EXPECT_INT_EQ(link.stats.parsed_frames, 2u, name);
  EXPECT_INT_EQ(link.stats.sequence_duplicate_frames, 1u, name);
  EXPECT_UINT_EQ(link.last_fast_us, 1000u, name);
  EXPECT_TRUE(!gl30_esp_link_connected(&link, 200001u), name);
}

static void test_connected_times_out_after_100ms(void) {
  const char* name = "connection expires after 100ms without fast frame";
  gl30_esp_link_t link;
  gl30_esp_link_init(&link);

  uint8_t frame[256];
  size_t n = 0u;
  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 1u, 0u, 30.0f, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, frame, n, 1000u), 0, name);

  EXPECT_TRUE(gl30_esp_link_connected(&link, 1000u + 50000u), name);
  EXPECT_TRUE(!gl30_esp_link_connected(&link, 1000u + 150000u), name);
}

static void test_sequence_wrap_and_backward_gap_is_bounded(void) {
  const char* name = "sequence wrap/backward remains bounded and not huge";
  gl30_esp_link_t link;
  gl30_esp_link_init(&link);

  uint8_t frame[256];
  size_t n = 0u;
  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 100u, 111u, 1.0f, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, frame, n, 111u), 0, name);

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), UINT32_MAX, 122u, 2.0f, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, frame, n, 122u), 0, name);

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 0u, 133u, 3.0f, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, frame, n, 133u), 0, name);

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 10u, 144u, 4.0f, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, frame, n, 144u), 0, name);

  EXPECT_INT_EQ(link.stats.sequence_gap_events, 9u, name);
  EXPECT_INT_EQ(link.stats.parsed_frames, 4u, name);
}

static void test_sequence_wrap_small_and_backward_gap_zero(void) {
  const char* name = "UINT32_MAX->0->1 wrap is gap0 and 1->0 does not create huge gap";
  gl30_esp_link_t link;
  gl30_esp_link_init(&link);
  uint8_t frame[256];
  size_t n = 0u;

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), UINT32_MAX, 100u, 1.0f, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, frame, n, 100u), 0, name);

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 0u, 200u, 2.0f, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, frame, n, 200u), 0, name);

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 1u, 300u, 3.0f, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, frame, n, 300u), 0, name);
  EXPECT_INT_EQ(link.stats.sequence_gap_events, 0u, name);

  EXPECT_TRUE(build_fast_frame(frame, sizeof(frame), 0u, 400u, 4.0f, &n) > 0u, name);
  EXPECT_INT_EQ(gl30_esp_link_feed(&link, frame, n, 400u), 0, name);
  EXPECT_INT_EQ(link.stats.sequence_gap_events, 0u, name);
}

static void test_bad_version_no_wrong_reject(void) {
  const char* name = "bad version increments version_failed without corrupting fast parse state";
  gl30_esp_link_t link;
  gl30_esp_link_init(&link);

  uint8_t bad_frame[256];
  uint8_t good_frame[256];
  uint8_t both[512];
  size_t bad_len = 0u;
  size_t good_len = 0u;

  EXPECT_TRUE(build_fast_frame(good_frame, sizeof(good_frame), 1u, 100u, 10.0f, &good_len), name);
  EXPECT_TRUE(build_fast_frame(bad_frame, sizeof(bad_frame), 2u, 200u, 11.0f, &bad_len), name);

  bad_frame[2u] = 0xFFu;
  memcpy(both, good_frame, good_len);
  memcpy(both + good_len, bad_frame, bad_len);

  EXPECT_INT_EQ(gl30_esp_link_feed(&link, both, good_len + bad_len, 1000u), 0, name);
  EXPECT_INT_EQ(link.stats.parsed_frames, 1u, name);
  EXPECT_INT_EQ(link.stats.version_failed_frames, 1u, name);
  EXPECT_INT_EQ(link.stats.bad_float_frames, 0u, name);
  EXPECT_FLOAT_EQ(link.latest_fast.angleRad, 10.0f, 1e-6f, name);
}

static void test_negative_target_endstop_command_build_is_allowed(void) {
  const char* name = "haptic command builder allows negative target/endstop";
  gl30_haptic_command_t command = {0};
  command.profileId = 123u;
  command.commandNonce = 7u;
  command.targetPositionRad = -0.5f;
  command.targetVelocityRadS = -2.0f;
  command.detentWidthRad = 0.8f;
  command.detentStrengthNm = 0.1f;
  command.endstopMinRad = -1.0f;
  command.endstopMaxRad = 1.0f;
  command.endstopStrengthNm = 0.2f;
  command.dampingNmPerRadS = 0.3f;
  command.inertiaKgM2 = 0.4f;
  command.frictionNm = 0.5f;
  command.userTorqueLimitNm = 0.6f;
  command.activeSpeedLimitRadS = 0.7f;
  command.modeFlags = 1u;
  command.textureId = 2u;

  uint8_t frame[512];
  size_t len = 0u;
  int status = gl30_esp_link_build_haptic_command_frame(&command, 0x20u, 5u, 999u, frame, sizeof(frame), &len);
  EXPECT_INT_EQ(status, 0, name);
  EXPECT_UINT_EQ((unsigned)len, GL30_FRAME_HEADER_BYTES + GL30_HAPTIC_COMMAND_LEN, name);
}

typedef void (*test_fn_t)(void);

typedef struct {
  const char* name;
  test_fn_t fn;
} unit_test_t;

int main(void) {
  int local_failures = 0;
  unit_test_t tests[] = {
    {"byte_chunks", test_byte_chunked_input_parses_frame},
    {"coalesced", test_coalesced_frames_in_one_chunk_parse},
    {"bad_crc_version_garbage", test_recover_from_bad_crc_version_garbage_in_chunk},
    {"unknown_4096_split", test_unknown_4096_unknown_then_fast_all_split_points},
    {"illegal_float_same_seq_no_refresh", test_illegal_float_does_not_refresh_same_seq_frame},
    {"slow", test_slow_decode_does_not_refresh_fast_freshness},
    {"duplicate", test_duplicate_sequence_does_not_refresh},
    {"conn_100ms", test_connected_times_out_after_100ms},
    {"seq_wrap", test_sequence_wrap_and_backward_gap_is_bounded},
    {"seq_wrap_small", test_sequence_wrap_small_and_backward_gap_zero},
  {"illegal_float_then_recover_same_seq", test_fast_illegal_float_same_seq_then_same_seq_recover_should_refresh},
    {"version", test_bad_version_no_wrong_reject},
    {"neg_target", test_negative_target_endstop_command_build_is_allowed},
  };

  for (size_t i = 0u; i < sizeof(tests) / sizeof(tests[0]); ++i) {
    g_failed = 0;
    tests[i].fn();
    if (g_failed == 0) {
      ++g_passed;
      printf("PASS %-55s\n", tests[i].name);
    } else {
      ++local_failures;
    }
    g_failed = 0;
  }

  printf("\n%d passed, %d failed\n", g_passed, local_failures);
  return local_failures == 0 ? 0 : 1;
}
