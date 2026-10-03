#ifndef GL30_ESP_LINK_H_
#define GL30_ESP_LINK_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "v6_protocol.h"

// Single-instance, single-thread transport state for GL30 v6 frames.
// This module keeps one process-global parser state from v6_protocol.c.
typedef struct {
  uint64_t total_bytes_processed;
  uint64_t parsed_frames;
  uint64_t fast_frames;
  uint64_t slow_frames;
  uint64_t haptic_frames;
  uint64_t unknown_frames;
  uint64_t crc_failed_frames;
  uint64_t length_failed_frames;
  uint64_t version_failed_frames;
  uint64_t bad_float_frames;
  uint64_t payload_len_mismatch_frames;
  uint64_t sequence_gap_events;
  uint64_t sequence_duplicate_frames;
} gl30_esp_link_stats_t;

typedef struct {
  gl30_motor_state_fast_t latest_fast;
  gl30_motor_state_slow_t latest_slow;
  gl30_haptic_state_t latest_haptic;
  uint64_t last_fast_us;
  uint64_t last_fast_timestamp_us;
  uint64_t last_slow_us;
  uint64_t last_haptic_us;
  uint32_t last_haptic_seq;
  uint64_t last_haptic_timestamp_us;
  bool has_fast;
  bool has_slow;
  bool has_haptic;
  bool has_last_seq;
  uint32_t last_seq;
  uint64_t now_us;
  gl30_esp_link_stats_t stats;
} gl30_esp_link_t;

void gl30_esp_link_init(gl30_esp_link_t *link);

int gl30_esp_link_feed(gl30_esp_link_t *link, const uint8_t *bytes, size_t len, uint64_t now_us);

bool gl30_esp_link_connected(const gl30_esp_link_t *link, uint64_t now_us);

int gl30_esp_link_build_haptic_command_frame(
    const gl30_haptic_command_t *command,
    uint16_t flags,
    uint32_t sequence,
    uint64_t timestamp_us,
    uint8_t *out_frame,
    size_t out_cap,
    size_t *out_len);

#endif  // GL30_ESP_LINK_H_
