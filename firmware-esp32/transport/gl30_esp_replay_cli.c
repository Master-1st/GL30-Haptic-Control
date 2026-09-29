#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "gl30_esp_link.h"

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <raw_frame_dump.bin>\n", argv[0]);
    return 1;
  }

  FILE *fp = fopen(argv[1], "rb");
  if (fp == NULL) {
    perror("open");
    return 1;
  }

  gl30_esp_link_t link;
  gl30_esp_link_init(&link);

  uint8_t buffer[4096];
  size_t read_bytes;
  uint64_t now_us = 0u;
  int status = 0;
  while ((read_bytes = fread(buffer, 1u, sizeof(buffer), fp)) != 0u) {
    now_us += (uint64_t)read_bytes;
    status = gl30_esp_link_feed(&link, buffer, read_bytes, now_us);
    if (status != 0) {
      fprintf(stderr, "feed failed with status=%d\n", status);
      break;
    }
  }

  if (ferror(fp) != 0) {
    perror("read");
    status = 1;
  }

  if (status == 0) {
    status = gl30_esp_link_feed(&link, NULL, 0u, now_us);
    if (status != 0) {
      fprintf(stderr, "drain failed with status=%d\n", status);
    }
  }

  fclose(fp);

  const gl30_esp_link_stats_t *stats = &link.stats;
  const uint64_t bad_count = stats->crc_failed_frames + stats->length_failed_frames +
                             stats->version_failed_frames + stats->bad_float_frames +
                             stats->payload_len_mismatch_frames;

  printf("file: %s\n", argv[1]);
  printf("clock: synthetic (bytes-read timebase, not wire timestamp)\n");
  printf("bytes: %" PRIu64 "\n", stats->total_bytes_processed);
  printf("parsed_frames: %" PRIu64 "\n", stats->parsed_frames);
  printf("fast_frames: %" PRIu64 "\n", stats->fast_frames);
  printf("slow_frames: %" PRIu64 "\n", stats->slow_frames);
  printf("unknown_frames: %" PRIu64 "\n", stats->unknown_frames);
  printf("errors: %" PRIu64 "\n", bad_count);
  printf("crc_fail: %" PRIu64 "\n", stats->crc_failed_frames);
  printf("length_fail: %" PRIu64 "\n", stats->length_failed_frames);
  printf("version_fail: %" PRIu64 "\n", stats->version_failed_frames);
  if (link.has_fast) {
    printf("last_angle_rad: %.9f\n", link.latest_fast.angleRad);
    printf("last_fast_age_us: %" PRIu64 "\n", now_us - link.last_fast_us);
  } else {
    printf("last_angle_rad: n/a\n");
  }
  printf("connected(local 100ms): %s\n", gl30_esp_link_connected(&link, now_us) ? "yes" : "no");
  printf("seq_gap_events: %" PRIu64 ", dup_frames: %" PRIu64 "\n",
         stats->sequence_gap_events, stats->sequence_duplicate_frames);

  return status;
}
