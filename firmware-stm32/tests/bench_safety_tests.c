#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "bench/NUCLEO_G474RE_FOC/Core/Inc/bench_safety.h"
#include "drivers/as5048a.h"

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

static uint16_t parity(uint16_t word) {
  word ^= word >> 8;
  word ^= word >> 4;
  word ^= word >> 2;
  word ^= word >> 1;
  return word & 1u;
}

static uint8_t expected_as5048a_response_faults(uint16_t response) {
  uint8_t ones = 0u;
  for (uint16_t bit = 0u; bit < 16u; ++bit) {
    ones += (uint8_t)((response >> bit) & 1u);
  }
  const uint8_t parity_fault = (uint8_t)((ones & 1u) != 0u);
  const uint8_t ef_fault = ((response & 0x4000u) != 0u ? AS5048A_RESPONSE_EF : 0u);
  return (uint8_t)((parity_fault ? AS5048A_RESPONSE_PARITY : 0u) | ef_fault);
}

static void test_bench_init_off_never_outputs(void) {
  bench_gate_t gate = {0};
  bench_gate_init(&gate);
  CHECK(gate.mode == BENCH_OFF, "bench init leaves mode OFF");
  CHECK(!bench_gate_output_allowed(&gate), "OFF mode never allows output");
}

static void test_bench_prepare_requires_all_health_bits(void) {
  const uint32_t all_bits[] = {BENCH_HEALTH_DRV,  BENCH_HEALTH_ENC,  BENCH_HEALTH_ADC,
                               BENCH_HEALTH_VM,   BENCH_HEALTH_NFAULT, BENCH_HEALTH_TIMING};

  for (size_t i = 0u; i < 6u; i++) {
    bench_gate_t gate = {0};
    bench_gate_init(&gate);
    CHECK(!bench_gate_prepare(&gate, BENCH_HEALTH_ALL & ~all_bits[i]),
          "prepare fails when any health bit is missing");
    CHECK(gate.mode == BENCH_OFF, "prepare keeps OFF mode on missing health");
  }
}

static void test_bench_align_requires_prepare_and_only_success_ready(void) {
  bench_gate_t gate = {0};
  bench_gate_init(&gate);
  CHECK(!bench_gate_align(&gate, 1000u), "align fails without prepare");
  CHECK(gate.mode == BENCH_OFF, "align keeps OFF mode if not prepared");

  CHECK(bench_gate_prepare(&gate, BENCH_HEALTH_ALL), "prepare succeeds only with all health");
  gate.health = BENCH_HEALTH_ALL & ~BENCH_HEALTH_DRV;
  CHECK(!bench_gate_align(&gate, 1000u), "align can still fail with incomplete transition state");
  CHECK(gate.mode == BENCH_PREPARED, "align stays prepared when align request is rejected");
  gate.health = BENCH_HEALTH_ALL;

  CHECK(bench_gate_align(&gate, 1000u), "align succeeds with prepare and health");
  CHECK(gate.mode == BENCH_ALIGNING, "align sets mode ALIGNING");
  CHECK(gate.started_us == 1000u, "align records started_us");

  bench_gate_alignment_done(&gate, false);
  CHECK(gate.mode == BENCH_FAULT, "alignment_done(false) latches fault");
  CHECK(gate.faults == BENCH_FAULT_ALIGN, "alignment_done(false) latches align fault");

  bench_gate_init(&gate);
  CHECK(bench_gate_prepare(&gate, BENCH_HEALTH_ALL), "re-prepare after init");
  CHECK(bench_gate_align(&gate, 1100u), "align again for successful path");
  bench_gate_alignment_done(&gate, true);
  CHECK(gate.mode == BENCH_READY, "alignment_done(true) moves to READY");
  CHECK(gate.calibrated, "alignment_done(true) sets calibrated");

  bench_gate_alignment_done(&gate, true);
  CHECK(gate.mode == BENCH_READY, "alignment_done ignored when not aligning");
}

static void test_bench_pulse_requires_ready_iq_and_range(void) {
  bench_gate_t gate = {0};
  bench_gate_init(&gate);
  CHECK(!bench_gate_pulse(&gate, 500u, 50, 1u), "pulse fails in OFF mode");

  CHECK(bench_gate_prepare(&gate, BENCH_HEALTH_ALL), "prepare for pulse path");
  CHECK(bench_gate_align(&gate, 600u), "align before pulse");
  bench_gate_alignment_done(&gate, true);

  CHECK(!bench_gate_pulse(&gate, 700u, -101, 1000u), "pulse rejects iq below -100");
  CHECK(!bench_gate_pulse(&gate, 700u, 101, 1000u), "pulse rejects iq above 100");
  CHECK(!bench_gate_pulse(&gate, 700u, 0, 1000u), "pulse rejects iq == 0");
  CHECK(!bench_gate_pulse(&gate, 700u, 1, 0u), "pulse rejects duration == 0ms");
  CHECK(!bench_gate_pulse(&gate, 700u, 1, 2001u), "pulse rejects duration > 2000ms");

  CHECK(bench_gate_pulse(&gate, 700u, 100, 1u), "pulse accepts +100mA");

  bench_gate_tick(&gate, 1700u, BENCH_HEALTH_ALL);
  CHECK(gate.mode == BENCH_READY, "previous pulse timeout returns to READY");
  CHECK(gate.iq_ma == 0, "pulse timeout clears iq command");

  CHECK(bench_gate_pulse(&gate, 1800u, -100, 1u), "pulse accepts -100mA");
}

static void test_bench_tick_faults_latch_and_stick(void) {
  bench_gate_t gate = {0};
  bench_gate_init(&gate);
  CHECK(bench_gate_prepare(&gate, BENCH_HEALTH_ALL), "prepare for fault stick tests");
  CHECK(bench_gate_align(&gate, 1000u), "align for fault stick tests");
  bench_gate_alignment_done(&gate, true);
  CHECK(bench_gate_pulse(&gate, 1000u, 10, 1000u), "start pulse for fault stick tests");

  bench_gate_tick(&gate, 2000u, BENCH_HEALTH_ALL & ~BENCH_HEALTH_DRV);
  CHECK(gate.mode == BENCH_FAULT, "health loss latches FAULT");
  CHECK(gate.faults == BENCH_FAULT_HEALTH, "health loss sets health fault");
  bench_gate_tick(&gate, 2500u, BENCH_HEALTH_ALL);
  CHECK(gate.mode == BENCH_FAULT, "FAULT state does not auto recover from health restore");

  bench_gate_init(&gate);
  CHECK(bench_gate_prepare(&gate, BENCH_HEALTH_ALL), "prepare for explicit current fault");
  CHECK(bench_gate_align(&gate, 500u), "align before explicit fault test");
  bench_gate_alignment_done(&gate, true);
  CHECK(bench_gate_pulse(&gate, 500u, 20, 2000u), "start pulse for current fault test");
  bench_gate_latch(&gate, BENCH_FAULT_CURRENT);
  CHECK(gate.mode == BENCH_FAULT, "current fault can be injected while ACTIVE");
  CHECK(gate.faults == BENCH_FAULT_CURRENT, "latch sets CURRENT fault");
  CHECK(!bench_gate_output_allowed(&gate), "current fault immediately disables output");

  bench_gate_init(&gate);
  CHECK(bench_gate_prepare(&gate, BENCH_HEALTH_ALL), "prepare before lease timeout");
  CHECK(bench_gate_align(&gate, 100u), "align before lease timeout");
  bench_gate_alignment_done(&gate, true);
  CHECK(bench_gate_pulse(&gate, UINT32_MAX - 50u, 20, 2000u), "lease timeout uses wrapped now_us");
  bench_gate_tick(&gate, 100100u, BENCH_HEALTH_ALL);
  CHECK(gate.mode == BENCH_FAULT, "lease timeout with uint32 wrap latches FAULT");
  CHECK(gate.faults == BENCH_FAULT_LEASE, "lease wrap timeout sets lease fault");

  bench_gate_init(&gate);
  CHECK(bench_gate_prepare(&gate, BENCH_HEALTH_ALL), "prepare for align timeout");
  CHECK(bench_gate_align(&gate, 100u), "align for align timeout");
  gate.lease_us = 6050000u;
  bench_gate_tick(&gate, 6100000u, BENCH_HEALTH_ALL);
  CHECK(gate.mode == BENCH_FAULT, "align timeout latches FAULT");
  CHECK(gate.faults == BENCH_FAULT_ALIGN, "align timeout sets align fault");
  bench_gate_alignment_done(&gate, true);
  CHECK(gate.mode == BENCH_FAULT, "alignment_done(true) cannot recover from FAULT");
}

static void test_bench_pulse_completes_to_ready(void) {
  bench_gate_t gate = {0};
  bench_gate_init(&gate);
  CHECK(bench_gate_prepare(&gate, BENCH_HEALTH_ALL), "prepare for completion test");
  CHECK(bench_gate_align(&gate, 10u), "align for completion test");
  bench_gate_alignment_done(&gate, true);
  CHECK(gate.mode == BENCH_READY, "ready before normal pulse");
  CHECK(bench_gate_pulse(&gate, 20u, 50, 1u), "start normal short pulse");
  CHECK(gate.mode == BENCH_ACTIVE, "mode enters ACTIVE while pulsing");
  bench_gate_tick(&gate, 1020u, BENCH_HEALTH_ALL);
  CHECK(gate.mode == BENCH_READY, "pulse duration timeout returns to READY");
  CHECK(gate.iq_ma == 0, "iq reset on READY timeout");
}

static void test_bench_stop_keeps_fault_and_clears_calibration(void) {
  bench_gate_t gate = {0};
  bench_gate_init(&gate);
  CHECK(bench_gate_prepare(&gate, BENCH_HEALTH_ALL), "prepare for stop semantics");
  CHECK(bench_gate_align(&gate, 10u), "align for stop semantics");
  bench_gate_alignment_done(&gate, true);
  CHECK(bench_gate_pulse(&gate, 10u, 10, 1000u), "start active before stop");
  bench_gate_latch(&gate, BENCH_FAULT_CURRENT);
  CHECK(gate.mode == BENCH_FAULT && gate.faults == BENCH_FAULT_CURRENT,
        "inject FAULT bit before stop");
  bench_gate_stop(&gate);
  CHECK(gate.mode == BENCH_FAULT, "stop does not clear FAULT mode");
  CHECK(gate.faults == BENCH_FAULT_CURRENT, "stop does not clear fault bits");
  CHECK(!gate.calibrated, "stop invalidates calibration");
  CHECK(gate.iq_ma == 0, "stop clears iq command");
}

static void test_bench_fault_and_alignment_done_not_recover(void) {
  bench_gate_t gate = {0};
  bench_gate_init(&gate);
  CHECK(bench_gate_prepare(&gate, BENCH_HEALTH_ALL), "prepare before fault recovery check");
  bench_gate_latch(&gate, BENCH_FAULT_TEST);
  CHECK(gate.mode == BENCH_FAULT, "fault set by latch");
  bench_gate_alignment_done(&gate, true);
  CHECK(gate.mode == BENCH_FAULT, "alignment_done(true) cannot recover FAULT");
}

static void test_as5048a_commands_and_reading(void) {
  CHECK(as5048a_read_command(0x3fffu) == 0xffffu,
        "AS5048A read 0x3fff returns 0xffff");
  CHECK(as5048a_read_command(0x3ffdu) == 0x7ffdu,
        "AS5048A read 0x3ffd returns 0x7ffd");

  const uint16_t cmd = as5048a_read_command(0x1u);
  CHECK((cmd & 0x4000u) != 0u, "AS5048A command sets READ bit14");
  CHECK(parity(cmd) == 0u, "AS5048A command final parity bit makes response even");
}

static void test_as5048a_decode_and_diagnostics_guardrails(void) {
  uint16_t out = 0x5a5au;
  CHECK(!as5048a_decode(0x0001u, &out), "AS5048A decode rejects bad parity");
  CHECK(out == 0x5a5au, "AS5048A decode parity fail does not overwrite output");

  out = 0x5a5au;
  CHECK(!as5048a_decode(0xC000u, &out), "AS5048A decode rejects EF flag set");
  CHECK(out == 0x5a5au, "AS5048A decode EF fail does not overwrite output");

  CHECK(!as5048a_decode(0x0000u, NULL), "AS5048A decode rejects NULL pointer");

  out = 0x5a5au;
  CHECK(as5048a_decode(0x0000u, &out), "AS5048A decode accepts zero-angle raw response");
  CHECK(out == 0x0000u, "AS5048A decode preserves zero-angle value");

  CHECK(!as5048a_diagnostics_ok(0x0000u), "AS5048A diagnostics OCF8 must be set");
  CHECK(as5048a_diagnostics_ok(0x0100u), "AS5048A diagnostics pass when only OCF8 is set");
  CHECK(!as5048a_diagnostics_ok(0x0200u), "AS5048A diagnostics reject COF bit set");
  CHECK(!as5048a_diagnostics_ok(0x0400u), "AS5048A diagnostics reject COMP_LOW bit set");
  CHECK(!as5048a_diagnostics_ok(0x0800u), "AS5048A diagnostics reject COMP_HIGH bit set");
}

static void test_as5048a_response_faults_and_decode_contract(void) {
  uint16_t out = 0x5a5au;
  CHECK(AS5048A_RESPONSE_PARITY == 1u, "AS5048A parity fault bit remains value 1");
  CHECK(AS5048A_RESPONSE_EF == 2u, "AS5048A EF fault bit remains value 2");
  CHECK(as5048a_response_faults(0x0000u) == 0u,
        "AS5048A response faults are zero for clean frame 0x0000");
  CHECK(as5048a_response_faults(0x0001u) == 1u,
        "AS5048A response faults flags parity-only condition");
  CHECK(as5048a_response_faults(0x4001u) == 2u,
        "AS5048A response faults flags EF-only condition");
  CHECK(as5048a_response_faults(0x4003u) == 3u,
        "AS5048A response faults flags parity and EF together");

  for (uint32_t response = 0u; response <= 0xFFFFu; ++response) {
    const uint16_t frame = (uint16_t)response;
    const uint8_t expected = expected_as5048a_response_faults(frame);
    const uint8_t actual = as5048a_response_faults(frame);
    CHECK(actual == expected, "AS5048A response faults follow parity/EF contract");

    out = 0x5a5au;
    const bool decode_ok = as5048a_decode(frame, &out);
    CHECK((expected == 0u) == decode_ok,
          "AS5048A decode result tracks response fault bits");

    if (expected == 0u) {
      CHECK(out == (frame & 0x3fffu), "AS5048A decode writes low14 when no faults");
    } else {
      CHECK(out == 0x5a5au, "AS5048A decode does not overwrite output on faulted frame");
    }

    CHECK(!as5048a_decode(frame, NULL), "AS5048A decode rejects NULL output pointer");
  }
}

int main(void) {
  test_bench_init_off_never_outputs();
  test_bench_prepare_requires_all_health_bits();
  test_bench_align_requires_prepare_and_only_success_ready();
  test_bench_pulse_requires_ready_iq_and_range();
  test_bench_tick_faults_latch_and_stick();
  test_bench_pulse_completes_to_ready();
  test_bench_stop_keeps_fault_and_clears_calibration();
  test_bench_fault_and_alignment_done_not_recover();
  test_as5048a_commands_and_reading();
  test_as5048a_decode_and_diagnostics_guardrails();
  test_as5048a_response_faults_and_decode_contract();

  if (g_tests_failed == 0) {
    printf("PASS: %d tests\n", g_tests_run);
    return 0;
  }
  printf("FAIL: %d / %d tests failed\n", g_tests_failed, g_tests_run);
  return 1;
}
