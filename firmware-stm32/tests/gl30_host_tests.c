#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control/foc.h"
#include "drivers/drv8316.h"
#include "drivers/board_sense.h"
#include "drivers/factory_encoder.h"
#include "drivers/ina228.h"
#include "haptics/haptics.h"
#include "drivers/veml7700.h"
#include "trace/trace_buffer.h"
#include "protocol/v6_protocol.h"
#include "safety/safety_supervisor.h"
#include "board_config.h"

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define CHECK(cond, msg) \
  do { \
    g_tests_run++; \
    if (!(cond)) { \
      g_tests_failed++; \
      fprintf(stderr, "[FAIL] %s\n", (msg)); \
    } \
  } while (0)

typedef struct {
  char name[32];
  uint8_t type;
  uint32_t sequence;
  uint64_t timestamp_us;
  uint16_t flags;
  size_t payload_len;
  size_t frame_len;
  uint32_t crc32c;
  uint8_t *payload;
  uint8_t *frame;
} vector_record_t;

static char *read_file_to_string(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    return NULL;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  long len = ftell(fp);
  if (len < 0) {
    fclose(fp);
    return NULL;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }
  char *buf = (char *)malloc((size_t)len + 1u);
  if (buf == NULL) {
    fclose(fp);
    return NULL;
  }
  size_t n = fread(buf, 1u, (size_t)len, fp);
  fclose(fp);
  buf[n] = '\0';
  return buf;
}

static bool is_in_range(const char *pos, const char *end) {
  return (pos != NULL && (end == NULL || pos < end));
}

static uint16_t read_u16_le(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t read_u32_le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
         ((uint32_t)p[3] << 24u);
}

static uint64_t read_u64_le(const uint8_t *p) {
  return ((uint64_t)read_u32_le(p)) | ((uint64_t)read_u32_le(p + 4u) << 32u);
}

static const char *find_vector_section(const char *json, const char *name) {
  const char marker[] = "\"name\": \"";
  const size_t name_len = strlen(name);
  const char *scan = json;
  while ((scan = strstr(scan, marker)) != NULL) {
    const char *ns = scan + sizeof(marker) - 1u;
    const char *ne = strchr(ns, '"');
    if (ne == NULL) {
      return NULL;
    }
    if ((size_t)(ne - ns) == name_len && strncmp(ns, name, name_len) == 0) {
      return scan;
    }
    scan = ne + 1u;
  }
  return NULL;
}

static bool find_json_value(const char *start, const char *end, const char *key, char *out,
                           size_t out_cap) {
  if (out_cap == 0u) {
    return false;
  }
  char pattern[128];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *p = strstr(start, pattern);
  if (p == NULL) {
    return false;
  }
  if (!is_in_range(p, end)) {
    return false;
  }
  const char *limit = (end == NULL ? strchr(start, '\0') : end);
  const char *colon = strchr(p, ':');
  if (colon == NULL || !is_in_range(colon, limit)) {
    return false;
  }
  const char *value = colon + 1u;
  while (value < limit && isspace((unsigned char)*value)) {
    value++;
  }
  if (value >= limit) {
    return false;
  }
  if (*value == '"') {
    const char *q = value + 1u;
    while (q < limit && *q != '"') {
      q++;
    }
    if (q >= limit) {
      return false;
    }
    const size_t len = (size_t)(q - (value + 1u));
    if (len + 1u > out_cap) {
      return false;
    }
    memcpy(out, value + 1u, len);
    out[len] = '\0';
    return true;
  }
  const char *q = value;
  while (q < limit &&
         (isalnum((unsigned char)*q) || *q == 'x' || *q == 'X' || *q == '+' || *q == '-')) {
    q++;
  }
  const size_t len = (size_t)(q - value);
  if (len == 0u || len + 1u > out_cap) {
    return false;
  }
  memcpy(out, value, len);
  out[len] = '\0';
  return true;
}

static bool parse_u64_field(const char *start, const char *end, const char *key, uint64_t *out) {
  char token[128];
  if (!find_json_value(start, end, key, token, sizeof(token))) {
    return false;
  }
  errno = 0;
  char *tail = NULL;
  const uint64_t value = (uint64_t)strtoull(token, &tail, 0);
  if (errno != 0 || tail == token || (tail != NULL && *tail != '\0')) {
    return false;
  }
  *out = value;
  return true;
}

static bool parse_u32_field(const char *start, const char *end, const char *key, uint32_t *out) {
  uint64_t value = 0u;
  if (!parse_u64_field(start, end, key, &value)) {
    return false;
  }
  *out = (uint32_t)value;
  return true;
}

static bool parse_u16_field(const char *start, const char *end, const char *key, uint16_t *out) {
  uint64_t value = 0u;
  if (!parse_u64_field(start, end, key, &value)) {
    return false;
  }
  *out = (uint16_t)value;
  return true;
}

static bool parse_hex_bytes(const char *text, uint8_t *out, size_t out_cap, size_t *out_len) {
  const size_t len = strlen(text);
  if ((len & 1u) != 0u || (len / 2u) > out_cap) {
    return false;
  }
  for (size_t i = 0u; i < len; i += 2u) {
    const char hi = text[i];
    const char lo = text[i + 1u];
    if (!isxdigit((unsigned char)hi) || !isxdigit((unsigned char)lo)) {
      return false;
    }
    const char pair[3] = {hi, lo, '\0'};
    out[i / 2u] = (uint8_t)strtoul(pair, NULL, 16);
  }
  *out_len = len / 2u;
  return true;
}

static uint32_t encode_signed_20(uint32_t value) {
  return (value & 0xFFFFFu) << 4u;
}

static uint64_t encode_signed_40(uint64_t value) {
  return value & 0xFFFFFFFFFFull;
}

static bool parse_vector(const char *json, const char *name, vector_record_t *out) {
  const char *section = find_vector_section(json, name);
  if (section == NULL) {
    return false;
  }
  const char *section_end = strstr(section + 1u, "\"name\": \"");
  if (section_end == NULL) {
    section_end = json + strlen(json);
  }

  snprintf(out->name, sizeof(out->name), "%s", name);

  uint64_t tmp64 = 0u;
  if (!parse_u64_field(section, section_end, "type", &tmp64)) {
    return false;
  }
  out->type = (uint8_t)tmp64;
  uint64_t sequence = 0u;
  if (!parse_u64_field(section, section_end, "sequence", &sequence)) {
    return false;
  }
  out->sequence = (uint32_t)sequence;
  if (!parse_u64_field(section, section_end, "timestampUs", &out->timestamp_us)) {
    return false;
  }
  if (!parse_u16_field(section, section_end, "flags", &out->flags)) {
    return false;
  }

  uint64_t payload_len = 0u;
  if (!parse_u64_field(section, section_end, "payloadBytes", &payload_len)) {
    return false;
  }
  uint64_t frame_len = 0u;
  if (!parse_u64_field(section, section_end, "frameBytes", &frame_len)) {
    return false;
  }
  if (!parse_u32_field(section, section_end, "crc32c", &out->crc32c)) {
    return false;
  }
  out->payload_len = (size_t)payload_len;
  out->frame_len = (size_t)frame_len;
  if (out->payload_len == 0u || out->frame_len == 0u) {
    return false;
  }

  char payload_hex[12289];
  char frame_hex[12289];
  if (!find_json_value(section, section_end, "payloadHex", payload_hex, sizeof(payload_hex))) {
    return false;
  }
  if (!find_json_value(section, section_end, "frameHex", frame_hex, sizeof(frame_hex))) {
    return false;
  }
  out->payload = (uint8_t *)malloc(out->payload_len);
  out->frame = (uint8_t *)malloc(out->frame_len);
  if (out->payload == NULL || out->frame == NULL) {
    return false;
  }
  size_t got_payload = 0u;
  size_t got_frame = 0u;
  if (!parse_hex_bytes(payload_hex, out->payload, out->payload_len, &got_payload)) {
    return false;
  }
  if (!parse_hex_bytes(frame_hex, out->frame, out->frame_len, &got_frame)) {
    return false;
  }
  return got_payload == out->payload_len && got_frame == out->frame_len;
}

static bool parse_crc32_known(const char *json, uint32_t *expected) {
  const char *section = strstr(json, "\"crc32cKnownVector\"");
  if (section == NULL) {
    return false;
  }
  const char *section_end = strstr(section + 1u, "\"vectors\"");
  if (section_end == NULL) {
    section_end = json + strlen(json);
  }
  char token[64];
  if (!find_json_value(section, section_end, "expected", token, sizeof(token))) {
    return false;
  }
  errno = 0;
  char *tail = NULL;
  const uint64_t value = strtoull(token, &tail, 16);
  if (errno != 0 || tail == token || (tail != NULL && *tail != '\0')) {
    return false;
  }
  *expected = (uint32_t)value;
  return true;
}

static void free_vector(vector_record_t *v) {
  free(v->payload);
  free(v->frame);
  v->payload = NULL;
  v->frame = NULL;
  v->payload_len = 0u;
  v->frame_len = 0u;
}

static void test_protocol_vectors(void) {
  CHECK(GL30_FRAME_HEADER_BYTES == 24u, "v6 header must be 24 bytes");
  vector_record_t motor = {0};
  vector_record_t haptic = {0};
  vector_record_t slow = {0};
  uint32_t expected_crc32c_known = 0u;
  const char *path = GL30_V1_JSON_PATH;
  char *json = read_file_to_string(path);
  CHECK(json != NULL, "load v1.json file");
  if (json == NULL) {
    return;
  }

  CHECK(parse_crc32_known(json, &expected_crc32c_known), "parse crc32cKnownVector.expected");
  CHECK(parse_vector(json, "motor-state-fast", &motor), "parse motor-state-fast vector");
  CHECK(parse_vector(json, "haptic-command", &haptic), "parse haptic-command vector");
  CHECK(parse_vector(json, "motor-state-slow", &slow), "parse motor-state-slow vector");

  if (motor.payload != NULL && haptic.payload != NULL && slow.payload != NULL &&
      motor.frame != NULL && haptic.frame != NULL && slow.frame != NULL) {
    const uint8_t test_payload[] = "123456789";
    uint32_t crc_known = 0u;
    gl30_crc32c(test_payload, sizeof(test_payload) - 1u, &crc_known);
    CHECK(crc_known == expected_crc32c_known, "CRC32C known vector == expected");

    const vector_record_t vectors[3] = {motor, slow, haptic};
    for (size_t i = 0u; i < 3u; i++) {
      const vector_record_t *vec = &vectors[i];
      uint8_t encode_buf[8192];
      size_t encoded_len = 0u;
      const int ret = gl30_frame_encode(vec->type, vec->flags, vec->sequence, vec->timestamp_us,
                                       vec->payload, vec->payload_len, encode_buf,
                                       sizeof(encode_buf), &encoded_len);
      CHECK(ret == 0, "gl30_frame_encode returns success");
      CHECK(encoded_len == vec->frame_len, "frame length from frameHex matches encoded");
      if (encoded_len == vec->frame_len) {
        CHECK(memcmp(encode_buf, vec->frame, vec->frame_len) == 0, "frame bytes match golden");
      }
    }

    gl30_frame_parse_init();
    gl30_frame_t parsed = {0};
    size_t consumed = 0u;
    for (size_t i = 0u; i < 3u; i++) {
      const vector_record_t *vec = &vectors[i];
      const gl30_parse_result_t ret_parse = gl30_frame_parse(vec->frame, vec->frame_len, &parsed, &consumed);
      CHECK(ret_parse.status == GL30_PARSE_OK, "parse frame returns OK for gold frame");
      CHECK(parsed.type == vec->type, "parsed type matches golden");
      CHECK(parsed.payload_len == vec->payload_len, "parsed payload_len matches golden");
      CHECK(parsed.flags == vec->flags, "parsed flags matches golden");
      CHECK(parsed.sequence == vec->sequence, "parsed sequence matches golden");
      CHECK(parsed.timestamp_us == vec->timestamp_us, "parsed timestamp matches golden");
      CHECK(parsed.crc32c == vec->crc32c, "parsed crc32 matches vector");
      CHECK(memcmp(parsed.payload, vec->payload, vec->payload_len) == 0, "parsed payload bytes match golden");
    }

    {
      const size_t chunk1_len = motor.frame_len / 2u;
      gl30_frame_parse_init();
      size_t c1 = 0u;
      gl30_parse_result_t r1 = gl30_frame_parse(motor.frame, chunk1_len, &parsed, &c1);
      CHECK(r1.status == GL30_PARSE_NEED_MORE, "chunked parse needs more");

      uint8_t chunk2[8192];
      const size_t chunk2_len = (motor.frame_len - chunk1_len) + haptic.frame_len;
      memcpy(chunk2, motor.frame + chunk1_len, motor.frame_len - chunk1_len);
      memcpy(chunk2 + motor.frame_len - chunk1_len, haptic.frame, haptic.frame_len);
      size_t c2 = 0u;
      gl30_parse_result_t r2 = gl30_frame_parse(chunk2, chunk2_len, &parsed, &c2);
      CHECK(r2.status == GL30_PARSE_OK, "coalesced parse returns first frame OK");
      CHECK(c2 == chunk2_len, "coalesced parse consumes full chunk");
      CHECK(parsed.type == motor.type, "coalesced parse first result is motor frame");

      size_t c3 = 0u;
      gl30_parse_result_t r3 = gl30_frame_parse(NULL, 0u, &parsed, &c3);
      CHECK(r3.status == GL30_PARSE_OK, "flush parse returns second frame OK");
      CHECK(c3 == 0u, "flush parse uses zero input bytes");
      CHECK(parsed.type == haptic.type, "flush parse second result is haptic frame");
    }

    {
      gl30_motor_state_slow_t decoded = {0};
      CHECK(gl30_decode_motor_state_slow(slow.payload, slow.payload_len, &decoded) == 0,
            "decode motor-state-slow payload");
      uint8_t regen_payload[GL30_MOTOR_STATE_SLOW_LEN];
      CHECK(gl30_encode_motor_state_slow(&decoded, regen_payload, sizeof(regen_payload)) == 0,
            "re-encode motor-state-slow payload");
      CHECK(memcmp(regen_payload, slow.payload, slow.payload_len) == 0,
            "motor-state-slow payload roundtrip");
      uint8_t regen_frame[1024];
      size_t regen_len = 0u;
      CHECK(gl30_frame_encode(slow.type, slow.flags, slow.sequence, slow.timestamp_us, slow.payload,
                             slow.payload_len, regen_frame, sizeof(regen_frame), &regen_len) == 0,
            "encode slow frame from vector payload");
      CHECK(regen_len == slow.frame_len, "slow frame length");
      CHECK(memcmp(regen_frame, slow.frame, regen_len) == 0, "slow frame bytes match golden");
      CHECK(gl30_frame_encode(GL30_V6_FRAME_PAYLOAD_MOTOR_STATE_SLOW, 0u, 0u, 1u, slow.payload,
                             slow.payload_len - 1u, regen_frame, sizeof(regen_frame), &regen_len) != 0,
            "motor-state-slow wrong length rejected by frame encoder");
    }

    {
      uint8_t bad_crc_frame[1024];
      memcpy(bad_crc_frame, motor.frame, motor.frame_len);
      bad_crc_frame[21u] ^= 0xFFu;
      gl30_frame_parse_init();
      size_t c = 0u;
      gl30_parse_result_t r = gl30_frame_parse(bad_crc_frame, motor.frame_len, &parsed, &c);
      CHECK(r.status == GL30_PARSE_BAD_CRC, "bad CRC detected");
      CHECK(c == motor.frame_len, "bad CRC consumes full frame length");
    }

    {
      uint8_t bad_payload_len[24] = {0x5Au, 0xA5u, 1u, 0x00u, 0x01u, 0x10u};
      gl30_frame_parse_init();
      size_t c = 0u;
      gl30_parse_result_t r = gl30_frame_parse(bad_payload_len, sizeof(bad_payload_len), &parsed, &c);
      CHECK(r.status == GL30_PARSE_BAD_PAYLOAD_LEN, "oversize payload rejected");
    }

    {
      uint8_t unsupported_version_frame[1024];
      memcpy(unsupported_version_frame, motor.frame, motor.frame_len);
      unsupported_version_frame[2u] = 2u;
      gl30_frame_parse_init();
      size_t c = 0u;
      gl30_parse_result_t r = gl30_frame_parse(unsupported_version_frame, motor.frame_len, &parsed, &c);
      CHECK(r.status == GL30_PARSE_UNSUPPORTED_VERSION, "unsupported version rejected");
    }
  }

  free_vector(&motor);
  free_vector(&haptic);
  free_vector(&slow);
  free(json);
}

static void test_drv8316_frames(void) {
  const uint16_t base = 0x1234u;
  const uint8_t parity = gl30_drv8316_even_parity_bit(base);
  const uint16_t frame = (uint16_t)(base | ((uint16_t)parity << 8u));
  CHECK(gl30_drv8316_word_has_even_parity(frame), "DRV8316 word parity check matches bit8");

  const uint16_t read_frame = gl30_drv8316_make_frame(true, 0x05u, 0xAAu);
  const uint16_t write_frame = gl30_drv8316_make_frame(false, 0x05u, 0xAAu);
  CHECK(read_frame != write_frame, "DRV8316 read/write flag changes frame");
  CHECK((read_frame & 0x8000u) != 0u, "DRV8316 read frame sets R/W bit");
  CHECK((write_frame & 0x8000u) == 0u, "DRV8316 write frame clears R/W bit");

  const uint16_t masked = gl30_drv8316_make_frame(false, 0x7Fu, 0x01u);
  CHECK(((masked >> 9u) & 0x3Fu) == 0x3Fu, "DRV8316 address is masked to 6 bits");
}

static void test_drv8316_status_word_faults_regression(void) {
  CHECK(gl30_drv8316_status_word_faults(3u, 0x0808u) == UINT32_MAX,
        "DRV8316 status-word faults rejects illegal register 3");
  CHECK(gl30_drv8316_status_word_faults(255u, 0x0808u) == UINT32_MAX,
        "DRV8316 status-word faults rejects illegal register 255");

  CHECK(gl30_drv8316_status_word_faults(0u, 0x0808u) == 0u,
        "DRV8316 status-word faults normal STAT0 no fault");
  CHECK(gl30_drv8316_status_word_faults(1u, 0x0800u) == 0u,
        "DRV8316 status-word faults normal STAT1 no fault");
  CHECK(gl30_drv8316_status_word_faults(2u, 0x0800u) == 0u,
        "DRV8316 status-word faults normal STAT2 no fault");
  CHECK(gl30_drv8316_status_word_faults(0u, 0x0800u) == 0x08u,
        "DRV8316 status-word faults normal STAT0 with NPOR in low byte yields only low-bit fault");

  CHECK(gl30_drv8316_status_word_faults(0u, 0x0000u) != 0u,
        "DRV8316 status-word faults summary NPOR clear on STAT0 is abnormal");
  CHECK(gl30_drv8316_status_word_faults(0u, 0x0008u) != 0u,
        "DRV8316 status-word faults summary NPOR clear on STAT0 with low-byte NPOR set is abnormal");
  CHECK(gl30_drv8316_status_word_faults(1u, 0x0000u) != 0u,
        "DRV8316 status-word faults summary NPOR clear on STAT1 is abnormal");
  CHECK(gl30_drv8316_status_word_faults(2u, 0x0000u) != 0u,
        "DRV8316 status-word faults summary NPOR clear on STAT2 is abnormal");

  CHECK(gl30_drv8316_status_word_faults(0u, 0x0000u) != 0u,
        "DRV8316 status-word faults blocks all-zero frame");
  CHECK(gl30_drv8316_status_word_faults(1u, 0xFFFFu) != 0u,
        "DRV8316 status-word faults blocks all-ones frame");

  CHECK(gl30_drv8316_status_word_faults(0u, 0x0908u) == 0x01u,
        "DRV8316 status-word faults keeps STAT0 fault bit instead of discarding as parity");

  CHECK(gl30_drv8316_status_word_faults(0u, 0x0888u) == 0x80u,
        "DRV8316 status-word faults preserves STAT0 fault bit at 0x80");
  CHECK(gl30_drv8316_status_word_faults(1u, 0x0888u) == 0x8800u,
        "DRV8316 status-word faults preserves STAT1 low-bit3 unchanged");
  CHECK(gl30_drv8316_status_word_faults(2u, 0x0888u) == 0x00080000u,
        "DRV8316 status-word faults ignores STAT2 reserved bit7");

  for (uint8_t address = 0u; address < 3u; ++address) {
    for (uint32_t raw = 0u; raw <= 0xFFFFu; ++raw) {
      const uint16_t reply = (uint16_t)raw;
      const uint8_t summary = (uint8_t)(reply >> 8u);
      uint8_t detail = (uint8_t)reply;
      if (address == 0u) { detail ^= 0x08u; }
      if (address == 2u) { detail &= 0x7Fu; }
      const uint32_t expected = (uint32_t)(summary ^ 0x08u) |
                               ((uint32_t)detail << (address * 8u));
      const uint32_t actual = gl30_drv8316_status_word_faults(address, reply);
      CHECK(actual == expected,
            "DRV8316 status-word faults keeps literal summary+detail mask across all 16-bit replies");
    }
  }

  for (uint8_t bit = 0u; bit < 8u; ++bit) {
    if (bit == 3u) {
      continue;
    }
    const uint16_t reply = (uint16_t)(((uint16_t)(0x08u | (1u << bit)) << 8u) | 0x08u);
    const uint32_t actual = gl30_drv8316_status_word_faults(0u, reply);
    CHECK(actual == (uint32_t)(1u << bit),
          "DRV8316 status-word faults STAT0 preserves summary bits by 1<<bit");
  }
  for (uint8_t bit = 0u; bit < 8u; ++bit) {
    if (bit == 3u) {
      continue;
    }
    const uint16_t reply = (uint16_t)(((uint16_t)(0x08u | (1u << bit)) << 8u) | 0x00u);
    const uint8_t summary = (uint8_t)(reply >> 8u);
    const uint8_t detail = (uint8_t)(reply ^ 0x08u);
    const uint32_t actual = gl30_drv8316_status_word_faults(0u, reply);
    CHECK(actual == ((uint32_t)(summary ^ 0x08u) | (uint32_t)detail),
          "DRV8316 status-word faults preserves STAT0 status bits in summary domain");
  }
  for (uint8_t bit = 0u; bit < 8u; ++bit) {
    if (bit == 3u) {
      continue;
    }
    const uint16_t reply = (uint16_t)(((uint16_t)(0x08u | (1u << bit)) << 8u) | 0x08u);
    const uint32_t expected = (uint32_t)(1u << bit);
    const uint32_t actual = gl30_drv8316_status_word_faults(0u, reply);
    CHECK(actual == expected,
          "DRV8316 status-word faults STAT0 keeps low-byte 0x08 from generating extra faults");
  }
  for (uint8_t bit = 0u; bit < 8u; ++bit) {
    if (bit == 3u) {
      continue;
    }
    const uint16_t reply = (uint16_t)(((uint16_t)(0x08u | (1u << bit)) << 8u) | 0x00u);
    const uint32_t actual = gl30_drv8316_status_word_faults(1u, reply);
    CHECK(actual == (uint32_t)(1u << bit),
          "DRV8316 status-word faults preserves STAT1 summary bits by 1<<bit");
    const uint32_t actual2 = gl30_drv8316_status_word_faults(2u, reply);
    CHECK(actual2 == (uint32_t)(1u << bit),
          "DRV8316 status-word faults preserves STAT2 summary bits by 1<<bit");
  }
  for (uint8_t bit = 0u; bit < 8u; ++bit) {
    if (bit == 3u) {
      continue;
    }
    const uint16_t reply = (uint16_t)(((uint16_t)0x08u << 8u) | (uint16_t)(1u << bit));
    const uint8_t summary = (uint8_t)(reply >> 8u);
    const uint8_t detail = (uint8_t)reply;
    const uint8_t detail_masked = (uint8_t)(detail & 0x7Fu);
    const uint32_t expected1 = (uint32_t)(summary ^ 0x08u) | (uint32_t)((uint32_t)detail << 8u);
    const uint32_t actual = gl30_drv8316_status_word_faults(1u, reply);
    CHECK(actual == expected1,
          "DRV8316 status-word faults preserves STAT1 fault and summary bits");
    const uint32_t expected2 = (uint32_t)(summary ^ 0x08u) |
                               ((uint32_t)detail_masked << 16u);
    const uint32_t actual2 = gl30_drv8316_status_word_faults(2u, reply);
    CHECK(actual2 == expected2,
          "DRV8316 status-word faults preserves STAT2 fault and summary bits");
    CHECK((((uint32_t)detail_masked << 16u) == (actual2 & 0x007F0000u)),
          "DRV8316 status-word faults keeps STAT2 defined low-detail detail bits");
  }

  {
    const uint32_t normal0 = gl30_drv8316_status_word_faults(0u, 0x0808u);
    const uint32_t normal1 = gl30_drv8316_status_word_faults(1u, 0x0800u);
    const uint32_t normal2 = gl30_drv8316_status_word_faults(2u, 0x0800u);
    CHECK((normal0 | normal1 | normal2) == 0u, "DRV8316 status-word faults normal OR of STAT0/1/2 is zero");

    const uint32_t stat1_fault = gl30_drv8316_status_word_faults(1u, 0x0801u);
    const uint32_t stat2_fault = gl30_drv8316_status_word_faults(2u, 0x0802u);
    CHECK((stat1_fault | stat2_fault) ==
               (uint32_t)(0x00000100u | 0x00020000u),
           "DRV8316 status-word faults OR across STAT1 and STAT2 preserves fault bits");
    CHECK(stat1_fault == 0x00000100u, "DRV8316 status-word faults STAT1 single bit remains lane-local");
    CHECK(stat2_fault == 0x00020000u, "DRV8316 status-word faults STAT2 single bit remains lane-local");
  }
}

static void test_ina228_decode_raw(void) {
  gl30_ina228_sample_t sample = {0};

  const int32_t vshunt_count = 12345;
  const int32_t vbus_count = 54321;
  const int32_t current_count = -2500;
  const int32_t neg_vshunt_count = -1111;
  const int32_t neg_current_count = -1111;

  const uint32_t vshunt_raw24 = encode_signed_20((uint32_t)vshunt_count);
  const uint32_t vbus_raw24 = encode_signed_20((uint32_t)vbus_count);
  const uint32_t current_raw24 = encode_signed_20((uint32_t)current_count);
  const uint16_t dietemp_raw16 = 100;
  const uint32_t power_raw24 = 0x000ABC;
  const uint64_t energy_raw40 = 0x000000001234u;
  const int64_t charge_signed = -17;
  const uint64_t charge_raw40 = encode_signed_40((uint64_t)charge_signed);

  CHECK(gl30_ina228_decode_raw(vshunt_raw24, vbus_raw24, dietemp_raw16, current_raw24, power_raw24,
                               energy_raw40, charge_raw40, 0x0001u, &sample),
        "INA228 decode accepts in-range 24/40-bit raw");
  CHECK(fabsf(sample.shunt_voltage_v - ((float)vshunt_count * 0.0000003125f)) < 1e-9f,
        "INA228 decodes signed 20-bit shunt voltage");
  CHECK(fabsf(sample.bus_voltage_v - ((float)vbus_count * 0.0001953125f)) < 1e-9f,
        "INA228 decodes unsigned 20-bit VBUS");
  CHECK(fabsf(sample.die_temperature_c - ((float)(int16_t)dietemp_raw16 * 0.0078125f)) < 1e-9f,
        "INA228 decodes die temperature");
  CHECK(fabsf(sample.current_a - ((float)current_count * GL30_INA228_CURRENT_LSB_A)) < 1e-6f,
        "INA228 decodes signed 20-bit current");
  CHECK(fabsf(sample.power_w - ((float)power_raw24 * (3.2f * GL30_INA228_CURRENT_LSB_A))) < 1e-9f,
        "INA228 decodes power scaling");
  CHECK(fabsf(sample.energy_j - ((float)energy_raw40 * (51.2f * GL30_INA228_CURRENT_LSB_A))) < 1e-6f,
        "INA228 decodes 40-bit energy scaling");
  CHECK(fabsf(sample.charge_c - ((float)charge_signed * GL30_INA228_CURRENT_LSB_A)) < 1e-9f,
        "INA228 decodes signed 40-bit charge scaling");
  CHECK(sample.valid, "INA228 valid when MEMSTAT set and MATHOF clear");

  CHECK(sample.charge_c < 0.0f, "INA228 handles negative charge");
  CHECK(gl30_ina228_decode_raw(vshunt_raw24, vbus_raw24, dietemp_raw16, encode_signed_20(1234u),
                               power_raw24, energy_raw40, charge_raw40, 0x200u, &sample),
        "INA228 decode raw with MATHOF set and without MEMSTAT");
  CHECK(!sample.valid, "INA228 invalid when MATHOF set");
  CHECK(gl30_ina228_decode_raw(vshunt_raw24, vbus_raw24, dietemp_raw16, current_raw24, power_raw24,
                               energy_raw40, charge_raw40, 0x0u, &sample),
        "INA228 raw decode succeeds even when MEMSTAT not set");
  CHECK(!sample.valid, "INA228 valid flag false when MEMSTAT not set");

  CHECK(!gl30_ina228_decode_raw(0x1000000u, vbus_raw24, dietemp_raw16, current_raw24, power_raw24,
                               energy_raw40, charge_raw40, 0x0001u, &sample),
        "INA228 rejects vshunt raw >24-bit");
  CHECK(!gl30_ina228_decode_raw(vshunt_raw24, 0x1000000u, dietemp_raw16, current_raw24, power_raw24,
                               energy_raw40, charge_raw40, 0x0001u, &sample),
        "INA228 rejects vbus raw >24-bit");
  CHECK(!gl30_ina228_decode_raw(vshunt_raw24, vbus_raw24, dietemp_raw16, 0x1000000u, power_raw24,
                               energy_raw40, charge_raw40, 0x0001u, &sample),
        "INA228 rejects current raw >24-bit");
  CHECK(!gl30_ina228_decode_raw(vshunt_raw24, vbus_raw24, dietemp_raw16, current_raw24, 0x1000000u,
                               energy_raw40, charge_raw40, 0x0001u, &sample),
        "INA228 rejects power raw >24-bit");
  CHECK(!gl30_ina228_decode_raw(vshunt_raw24, vbus_raw24, dietemp_raw16, current_raw24, power_raw24,
                               0x100000000000ull, charge_raw40, 0x0001u, &sample),
        "INA228 rejects energy raw >40-bit");
  CHECK(!gl30_ina228_decode_raw(vshunt_raw24, vbus_raw24, dietemp_raw16, current_raw24, power_raw24,
                               energy_raw40, 0x100000000000ull, 0x0001u, &sample),
        "INA228 rejects charge raw >40-bit");

  CHECK(gl30_ina228_decode_raw(encode_signed_20((uint32_t)neg_vshunt_count), vbus_raw24, dietemp_raw16,
                               encode_signed_20((uint32_t)neg_current_count), power_raw24, energy_raw40,
                               charge_raw40, 0x0001u, &sample),
         "INA228 accepts negative shunt and current");
}

static void test_veml7700_counts_to_lux(void) {
  const uint16_t gain_config = 0x1000u;
  const float no_lux = gl30_veml7700_counts_to_lux(1234u, gain_config);
  const float base_lux = (float)1234 * 0.0672f * (100.0f / 100.0f) / 0.125f;
  CHECK(fabsf(no_lux - base_lux) < 1e-3f, "VEML7700 1/8-gain 100ms conversion");

  const float invalid = gl30_veml7700_counts_to_lux(1234u, 0x0100u);
  CHECK(invalid == 0.0f, "VEML7700 invalid integration returns 0");

  const float high_count = 2000u;
  const float uncorrected = (float)high_count * 0.0672f * (100.0f / 100.0f) / 0.125f;
  const float corrected = gl30_veml7700_counts_to_lux(high_count, gain_config);
  CHECK(isfinite(corrected), "VEML7700 high-light-corrected lux is finite");
  CHECK(corrected > uncorrected, "VEML7700 high-light correction increases very bright values");
}

static gl30_haptic_command_t make_valid_command(uint32_t nonce) {
  return (gl30_haptic_command_t){
      .profileId = 1u,
      .commandNonce = nonce,
      .targetPositionRad = 0.0f,
      .targetVelocityRadS = 0.0f,
      .detentWidthRad = 0.1f,
      .detentStrengthNm = 0.2f,
      .endstopMinRad = -0.5f,
      .endstopMaxRad = 0.5f,
      .endstopStrengthNm = 3.0f,
      .dampingNmPerRadS = 0.4f,
      .inertiaKgM2 = 0.0f,
      .frictionNm = 0.1f,
      .userTorqueLimitNm = 0.1f,
      .activeSpeedLimitRadS = 0.0f,
      .modeFlags = 0xFFFFFFFFu,
  };
}

static void test_foc_behavior(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);
  CHECK(state.command_torque_limit_nm > 0.0f, "FOC init sets torque limit");

  gl30_haptic_command_t valid = make_valid_command(1u);
  CHECK(gl30_foc_apply_command(&state, &valid), "FOC accepts valid command");
  CHECK(state.active_command.detentStrengthNm <= 0.06f + 1e-6f, "FOC clamps detentStrength");
  CHECK(state.active_command.endstopStrengthNm <= 2.0f + 1e-6f, "FOC clamps endstop strength");
  CHECK(state.active_command.dampingNmPerRadS <= 0.20f + 1e-6f, "FOC clamps damping");
  CHECK(state.active_command.frictionNm <= 0.015f + 1e-6f, "FOC clamps friction");
  CHECK(state.active_command.userTorqueLimitNm <= 0.060f + 1e-6f, "FOC clamps user torque limit");

  valid.endstopMinRad = 1.0f;
  valid.endstopMaxRad = -1.0f;
  const uint32_t before_rejected = state.rejected_commands;
  CHECK(!gl30_foc_apply_command(&state, &valid), "FOC rejects invalid range");
  CHECK(state.rejected_commands == before_rejected + 1u, "FOC rejects increments counter");

  state.torque_command_nm = 1.23f;
  state.haptic_torque_nm = 1.23f;
  state.integrator_d_v = 1.2f;
  state.integrator_q_v = 1.2f;
  state.v_d_v = 1.0f;
  state.v_q_v = 1.0f;
  gl30_foc_force_zero(&state);
  CHECK(state.torque_command_nm == 0.0f, "FOC force_zero clears torque");
  CHECK(state.haptic_torque_nm == 0.0f, "FOC force_zero clears haptic torque");
  CHECK(state.integrator_d_v == 0.0f && state.integrator_q_v == 0.0f, "FOC integrators cleared");

  state.observer_initialized = true;
  state.theta_elec_rad = 0.0f;
  state.i_d_ref_a = 100.0f;
  state.i_q_ref_a = -100.0f;
  gl30_foc_output_t out =
      gl30_foc_current_tick(&state, 0.2f, 0.2f, 0.2f, 5.0f, 1.0f / (float)GL30_PWM_HZ, 5.0f);
  CHECK(out.valid, "FOC output valid for finite currents");
  CHECK(out.duty_a >= GL30_TIM1_MIN_DUTY && out.duty_a <= GL30_TIM1_MAX_DUTY,
        "FOC duty A within board macro bounds");
  CHECK(out.duty_b >= GL30_TIM1_MIN_DUTY && out.duty_b <= GL30_TIM1_MAX_DUTY,
        "FOC duty B within board macro bounds");
  CHECK(out.duty_c >= GL30_TIM1_MIN_DUTY && out.duty_c <= GL30_TIM1_MAX_DUTY,
        "FOC duty C within board macro bounds");

  gl30_foc_output_t oc =
      gl30_foc_current_tick(&state, 3.0f, 3.0f, 3.0f, 12.0f, 1.0f / (float)GL30_PWM_HZ, 12.0f);
  CHECK(oc.overcurrent, "FOC overcurrent flag set");
  CHECK(!oc.valid, "FOC overcurrent path is not valid");

  state.i_d_ref_a = 25.0f;
  state.i_q_ref_a = -25.0f;
  for (int i = 0; i < 200; i++) {
    (void)gl30_foc_current_tick(&state, 0.0f, 0.0f, 0.0f, 12.0f, 1.0f / (float)GL30_PWM_HZ, 12.0f);
  }
  CHECK(isfinite(state.integrator_d_v) && isfinite(state.integrator_q_v), "FOC integrators remain finite");
  const float voltage_limit = 0.57735026918962576451f * 12.0f * 0.95f;
  CHECK(fabsf(state.integrator_d_v) <= voltage_limit + 1.0e-3f, "FOC d integrator is bounded");
  CHECK(fabsf(state.integrator_q_v) <= voltage_limit + 1.0e-3f, "FOC q integrator is bounded");
}

static void test_foc_voltage_extreme_request_limits_pwm_window(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);

  state.observer_initialized = true;
  state.theta_elec_rad = 0.0f;
  state.i_d_ref_a = 250.0f;
  state.i_q_ref_a = -250.0f;

  gl30_foc_output_t out =
      gl30_foc_current_tick(&state, 0.01f, 0.02f, -0.01f, 12.0f, 1.0f / (float)GL30_PWM_HZ, 12.0f);
  CHECK(isfinite(out.duty_a) && isfinite(out.duty_b) && isfinite(out.duty_c),
        "FOC duty values are finite under extreme command");
  CHECK(out.duty_a >= GL30_TIM1_MIN_DUTY && out.duty_a <= GL30_TIM1_MAX_DUTY,
        "FOC extreme duty A within board macro bounds");
  CHECK(out.duty_b >= GL30_TIM1_MIN_DUTY && out.duty_b <= GL30_TIM1_MAX_DUTY,
        "FOC extreme duty B within board macro bounds");
  CHECK(out.duty_c >= GL30_TIM1_MIN_DUTY && out.duty_c <= GL30_TIM1_MAX_DUTY,
        "FOC extreme duty C within board macro bounds");
}

static void test_haptics_tick(void) {
  gl30_foc_state_t state = {0};
  gl30_foc_init(&state);

  state.observer_initialized = true;
  state.active_command = make_valid_command(11u);
  state.command_torque_limit_nm = 0.2f;
  state.theta_unwrapped_rad = 0.1f;
  state.velocity_rad_s = 0.05f;
  state.acceleration_rad_s2 = 0.0f;

  gl30_haptic_tick_2k(&state, true);
  CHECK(state.haptic_torque_nm >= -state.command_torque_limit_nm &&
            state.haptic_torque_nm <= state.command_torque_limit_nm,
        "haptics tick clamps haptic torque into command torque limit");

  state.torque_command_nm = 1.23f;
  state.haptic_torque_nm = 1.23f;
  gl30_haptic_tick_2k(&state, false);
  CHECK(state.torque_command_nm == 0.0f, "haptics tick disables torque when not allowed");
  CHECK(state.haptic_torque_nm == 0.0f, "haptics tick clears haptic torque when not allowed");
}

static void test_safety_behavior(void) {
  gl30_safety_t safety = {0};
  gl30_safety_init(&safety, 1000u);
  CHECK(safety.startup_state == GL30_STARTUP_POWER_CHECK, "SAFETY starts at power check");
  CHECK(safety.comm_state == GL30_COMM_WAITING, "SAFETY starts waiting comm");

  gl30_safety_set_startup_checks(&safety, true, true, true, true, true, true);
  CHECK(safety.startup_state == GL30_STARTUP_READY, "SAFETY startup checks can reach READY");
  gl30_safety_on_valid_command(&safety, 2000u);
  CHECK(safety.comm_state == GL30_COMM_NORMAL, "SAFETY valid frame enters NORMAL");
  CHECK(safety.applied_command_count == 1u, "SAFETY increments applied command count");

  gl30_safety_request_arm(&safety);
  CHECK(safety.startup_state == GL30_STARTUP_ACTIVE, "explicit arm enters ACTIVE");
  CHECK(safety.arm_requested, "startup remains arm requested in ACTIVE");

  gl30_safety_tick(&safety, 2000u + GL30_COMM_WARN_US);
  CHECK(safety.comm_state == GL30_COMM_WARN, "10ms timeout becomes WARN");
  CHECK((safety.warning_bits & GL30_WARNING_COMM_10MS) != 0u, "WARN latch sets warning bit");

  gl30_safety_tick(&safety, 2000u + GL30_COMM_SAFE_ZERO_US);
  CHECK(safety.comm_state == GL30_COMM_SAFE_ZERO, "20ms timeout becomes SAFE_ZERO");
  CHECK(!safety.arm_requested, "SAFE_ZERO disarms");

  gl30_safety_tick(&safety, 2000u + GL30_COMM_LOST_US);
  CHECK(safety.comm_state == GL30_COMM_LOST, "100ms timeout becomes COMM_LOST");
  CHECK(gl30_safety_fault_latched(&safety), "COMM_LOST latches safety fault");
  CHECK(safety.fault_bits == GL30_FAULT_COMM_LOST, "COMM_LOST sets comm-lost fault");

  CHECK(safety.unknown_type_count == 0u, "unknown type count baseline");
  gl30_safety_on_unknown_type(&safety);
  CHECK(safety.unknown_type_count == 1u, "unknown type count increments");
  gl30_safety_on_unknown_version(&safety);
  CHECK(safety.unknown_version_count == 1u, "unknown version count increments");
}

static void check_downsample_bin(
    const int16_t *read_all,
    uint16_t window,
    uint16_t b,
    const int16_t *actual_min,
    const int16_t *actual_max,
    const int16_t *actual_mean,
    const int16_t *actual_rms) {
  int32_t mn[6] = {INT16_MAX, INT16_MAX, INT16_MAX, INT16_MAX, INT16_MAX, INT16_MAX};
  int32_t mx[6] = {INT16_MIN, INT16_MIN, INT16_MIN, INT16_MIN, INT16_MIN, INT16_MIN};
  float sum[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  float sumsq[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
  int cnt = 0;
  for (uint16_t i = 0u; i < window; i++) {
    uint16_t idx = b * window + i;
    if (idx >= 4096u) {
      break;
    }
    for (uint16_t c = 0u; c < 6u; c++) {
      const int16_t v = read_all[(size_t)idx * 6u + c];
      if (v < mn[c]) {
        mn[c] = v;
      }
      if (v > mx[c]) {
        mx[c] = v;
      }
      sum[c] += (float)v;
      sumsq[c] += (float)v * (float)v;
    }
    cnt++;
  }
  for (uint16_t c = 0u; c < 6u; c++) {
    const float mean = sum[c] / (float)cnt;
    const float rms = sqrtf(sumsq[c] / (float)cnt);
      CHECK(actual_min[c] == (int16_t)mn[c], "downsample min matches readback window");
      CHECK(actual_max[c] == (int16_t)mx[c], "downsample max matches readback window");
      CHECK(actual_mean[c] == (int16_t)llroundf(mean), "downsample mean matches readback window");
      CHECK(actual_rms[c] == (int16_t)llroundf(rms), "downsample rms matches readback window");
    }
}

static void test_trace_ring(void) {
  gl30_trace_init();
  int16_t sample[6];
  for (int i = 0; i < 4096; i++) {
    for (int c = 0; c < 6; c++) {
      sample[c] = (int16_t)i;
    }
    gl30_trace_push(sample);
  }
  for (int i = 0; i < 20; i++) {
    for (int c = 0; c < 6; c++) {
      sample[c] = (int16_t)(4096 + i);
    }
    gl30_trace_push(sample);
  }

  gl30_trace_status_t status = gl30_trace_status();
  CHECK(status.rows == 4096u, "trace rows is 4096");
  CHECK(status.total_written == 4096u, "trace total_written saturates at 4096");
  CHECK(status.overflow_count == 20u, "trace overflow count increments after wrap");
  CHECK(status.cursor == 20u, "trace cursor is 20 after 4096 + 20 writes");

  int16_t read_all[4096 * 6];
  gl30_trace_read_all(read_all, (uint32_t)(sizeof(read_all) / sizeof(read_all[0])));
  CHECK(read_all[0] == 20, "trace read_all oldest sample starts at i=20");
  CHECK(read_all[(4095u * 6u) + 3u] == 4115, "trace read_all newest sample is i=4115");

  uint16_t bins = 0u;
  int16_t min_out[205 * 6];
  int16_t max_out[205 * 6];
  int16_t mean_out[205 * 6];
  int16_t rms_out[205 * 6];
  gl30_trace_downsample20(min_out, max_out, mean_out, rms_out, 205u, &bins);
  CHECK(bins == 205u, "trace downsample20 produces 205 bins");

  const uint16_t window = 20u;
  for (uint16_t i = 0u; i < 2u; i++) {
    const uint16_t bin = (i == 0u) ? 0u : 204u;
    check_downsample_bin(read_all,
                         window,
                         bin,
                         &min_out[(size_t)bin * 6u],
                         &max_out[(size_t)bin * 6u],
                         &mean_out[(size_t)bin * 6u],
                         &rms_out[(size_t)bin * 6u]);
  }
}

static void test_factory_encoder_init_and_snapshot(void) {
  gl30_factory_encoder_sample_t sample = {0};
  gl30_factory_encoder_diagnostics_t diag = {0};
  gl30_factory_encoder_init();
  gl30_factory_encoder_snapshot(&sample);

  CHECK(sample.angle_rad == 0.0f, "factory encoder init sets angle 0");
  CHECK(sample.timestamp_us == 0u, "factory encoder init sets timestamp 0");
  CHECK(sample.status == GL30_FACTORY_ENCODER_STATUS_INITIALIZING,
        "factory encoder init sets initializing status");
  CHECK(sample.valid == false, "factory encoder init sets valid false");
  CHECK(sample.sample_index == 0u, "factory encoder init sets sample index 0");

  gl30_factory_encoder_poll_4k();
  gl30_factory_encoder_snapshot(&sample);
  gl30_factory_encoder_diagnostics_snapshot(&diag);
  CHECK(sample.status == GL30_FACTORY_ENCODER_STATUS_INITIALIZING && !sample.valid,
        "host-only encoder polling stays initializing and invalid");
  CHECK(diag.latest_faults == 0u && diag.successful_samples == 0u &&
        diag.failed_samples == 0u,
        "host-only encoder polling does not fabricate transport diagnostics");
}

static void test_control_ready(void) {
  gl30_factory_encoder_sample_t sample = {
      .angle_rad = 1.0f,
      .timestamp_us = 1234u,
      .status = GL30_FACTORY_ENCODER_STATUS_READY,
      .valid = true,
      .sample_index = 5u,
  };

  CHECK(!control_ready(NULL, 999u, 1000u), "control_ready returns false on NULL");

  sample.status = GL30_FACTORY_ENCODER_STATUS_INITIALIZING;
  CHECK(!control_ready(&sample, 2000u, 100u), "control_ready returns false while initializing");

  sample.status = GL30_FACTORY_ENCODER_STATUS_INVALID;
  CHECK(!control_ready(&sample, 2000u, 100u), "control_ready returns false for invalid status");

  sample.status = GL30_FACTORY_ENCODER_STATUS_READY;
  sample.valid = false;
  CHECK(!control_ready(&sample, 2000u, 100u), "control_ready returns false for invalid sample valid flag");

  sample.valid = true;
  sample.timestamp_us = 0u;
  CHECK(!control_ready(&sample, 2000u, 100u), "control_ready returns false for zero timestamp");

  sample.timestamp_us = 3000u;
  CHECK(!control_ready(&sample, 2500u, 100u), "control_ready returns false for future sample");

  sample.timestamp_us = 1000u;
  CHECK(!control_ready(&sample, 2000u, 999u), "control_ready returns false for expired sample");

  sample.timestamp_us = 1000u;
  CHECK(control_ready(&sample, 2000u, 1000u), "control_ready true at max age boundary");
  CHECK(control_ready(&sample, 1500u, 600u), "control_ready true for ready/valid and not expired");
  sample.angle_rad = NAN;
  CHECK(!control_ready(&sample, 1500u, 600u), "control_ready rejects NaN angle despite ready flags");
  sample.angle_rad = INFINITY;
  CHECK(!control_ready(&sample, 1500u, 600u), "control_ready rejects positive infinity angle");
  sample.angle_rad = -INFINITY;
  CHECK(!control_ready(&sample, 1500u, 600u), "control_ready rejects negative infinity angle");
}

static void test_product_schematic_contract(void) {
  CHECK(GL30_PINMAP_COUNT == 48u, "product pin table covers LQFP48");
  CHECK(GL30_PINMAP[1].mode == GL30_PIN_OUTPUT_PP,
        "PC13 drives the buffered ready input high against its default pull-down");
  CHECK(GL30_PINMAP[3].mode == GL30_PIN_RESERVED,
        "PC15 remains unallocated, not an unsolicited watchdog clock");
  CHECK(strcmp(GL30_PINMAP[33].net, "DRV8316_NSLEEP") == 0 &&
        GL30_PINMAP[33].mode == GL30_PIN_OUTPUT_PP,
        "PA12 owns nSLEEP, not the obsolete USB reserve");
  CHECK(strcmp(GL30_PINMAP[45].net, "MOTOR_PWR_EN") == 0 &&
        GL30_PINMAP[45].mode == GL30_PIN_OUTPUT_PP,
        "PB9 owns motor power permission, never scope pulses or LED data");
  /* J1 VM -> 100k + 100k -> ADC node -> 20k -> GND. The separate
   * comparator divider remains 6:1 and must not define ADC scaling. */
  const float adc_at_16v = (16.0f / 11.0f) / GL30_ADC_VREF_V *
                         GL30_ADC_FULL_SCALE_COUNTS;
  const float measured_v = adc_at_16v * GL30_ADC_VREF_V /
                           GL30_ADC_FULL_SCALE_COUNTS * GL30_VBUS_DIVIDER_RATIO;
  CHECK(fabsf(measured_v - 16.0f) < 0.001f,
        "16 V on the selected ADC divider must not be reported as 8.73 V");
}

static void test_product_ntc_conversion(void) {
  float temperature_c = 1234.0f;
  CHECK(!gl30_board_ntc_temperature_c(2048u, NULL), "NTC rejects null output");
  const uint16_t invalid[] = {0u, 1u, 20u, 4090u, 4094u, 4095u, 65535u};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    CHECK(!gl30_board_ntc_temperature_c(invalid[i], &temperature_c),
          "NTC rejects open, short and implausible endpoint readings");
    CHECK(temperature_c == 1234.0f, "failed NTC conversion leaves output unchanged");
  }
  CHECK(gl30_board_ntc_temperature_c(2048u, &temperature_c) &&
        fabsf(temperature_c - 25.0f) < 0.02f,
        "equal 10k resistances produce approximately 25 C, not TMP36 temperature");
  /* Independent double-precision resistor synthesis -> integer ADC ->
   * production single-precision conversion. Includes both protection levels. */
  const double samples_c[] = {-39.0, -20.0, 0.0, 25.0, 50.0, 70.0, 84.5, 85.5, 100.0, 124.0};
  for (size_t i = 0; i < sizeof(samples_c) / sizeof(samples_c[0]); ++i) {
    const double resistance = 10000.0 * exp(3950.0 *
        (1.0 / (samples_c[i] + 273.15) - 1.0 / 298.15));
    const uint16_t raw = (uint16_t)lround(4095.0 * resistance / (10000.0 + resistance));
    CHECK(gl30_board_ntc_temperature_c(raw, &temperature_c), "NTC valid reference point");
    CHECK(fabs((double)temperature_c - samples_c[i]) < 0.2,
          "NTC reference point within ADC quantization allowance");
    if (samples_c[i] == 84.5) {
      CHECK(temperature_c < GL30_MOTOR_TEMP_FAULT_C, "NTC below thermal fault boundary");
    } else if (samples_c[i] == 85.5) {
      CHECK(temperature_c > GL30_MOTOR_TEMP_FAULT_C, "NTC above thermal fault boundary");
    }
  }
  float previous = 1000.0f;
  unsigned valid_count = 0u;
  for (uint16_t raw = 1u; raw < 4095u; ++raw) {
    if (gl30_board_ntc_temperature_c(raw, &temperature_c)) {
      CHECK(isfinite(temperature_c) && temperature_c < previous,
            "NTC conversion decreases monotonically as pull-down resistance rises");
      previous = temperature_c;
      ++valid_count;
    }
  }
  CHECK(valid_count > 3800u, "NTC accepts its broad plausible measurement interval");
}

int main(void) {
  test_product_schematic_contract();
  test_product_ntc_conversion();
  test_protocol_vectors();
  test_drv8316_frames();
  test_drv8316_status_word_faults_regression();
  test_ina228_decode_raw();
  test_veml7700_counts_to_lux();
  test_factory_encoder_init_and_snapshot();
  test_control_ready();
  test_foc_behavior();
  test_foc_voltage_extreme_request_limits_pwm_window();
  test_haptics_tick();
  test_safety_behavior();
  test_trace_ring();

  if (g_tests_failed == 0) {
    printf("PASS: %d tests\n", g_tests_run);
    return 0;
  }
  printf("FAIL: %d / %d tests failed\n", g_tests_failed, g_tests_run);
  return 1;
}
