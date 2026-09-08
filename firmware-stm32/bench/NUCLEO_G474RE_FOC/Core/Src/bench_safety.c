#include "bench_safety.h"

void bench_gate_init(volatile bench_gate_t *gate)
{
  *gate = (bench_gate_t){0};
}

void bench_gate_latch(volatile bench_gate_t *gate, uint32_t fault)
{
  gate->faults |= fault;
  gate->mode = BENCH_FAULT;
  gate->calibrated = false;
  gate->iq_ma = 0;
}

void bench_gate_stop(volatile bench_gate_t *gate)
{
  gate->mode = gate->faults ? BENCH_FAULT : BENCH_OFF;
  gate->calibrated = false;
  gate->iq_ma = 0;
}

bool bench_gate_prepare(volatile bench_gate_t *gate, uint32_t health)
{
  gate->health = health;
  if (gate->mode != BENCH_OFF || gate->faults || health != BENCH_HEALTH_ALL) {
    return false;
  }
  gate->mode = BENCH_PREPARED;
  return true;
}

bool bench_gate_align(volatile bench_gate_t *gate, uint32_t now_us)
{
  if (gate->mode != BENCH_PREPARED || gate->faults ||
      gate->health != BENCH_HEALTH_ALL) {
    return false;
  }
  gate->started_us = now_us;
  gate->lease_us = now_us;
  gate->duration_us = 6000000u;
  gate->calibrated = false;
  gate->mode = BENCH_ALIGNING;
  return true;
}

void bench_gate_alignment_done(volatile bench_gate_t *gate, bool success)
{
  if (gate->mode != BENCH_ALIGNING) {
    return;
  }
  if (!success) {
    bench_gate_latch(gate, BENCH_FAULT_ALIGN);
  } else {
    gate->calibrated = true;
    gate->mode = BENCH_READY;
  }
}

bool bench_gate_pulse(volatile bench_gate_t *gate, uint32_t now_us,
                      int32_t iq_ma, uint32_t duration_ms)
{
  if (gate->mode != BENCH_READY || !gate->calibrated || gate->faults ||
      gate->health != BENCH_HEALTH_ALL ||
      iq_ma < -100 || iq_ma > 100 || iq_ma == 0 ||
      duration_ms == 0u || duration_ms > 2000u) {
    return false;
  }
  gate->iq_ma = iq_ma;
  gate->duration_us = duration_ms * 1000u;
  gate->started_us = now_us;
  gate->lease_us = now_us;
  gate->mode = BENCH_ACTIVE;
  return true;
}

bool bench_gate_haptic(volatile bench_gate_t *gate, uint32_t now_us,
                       uint32_t duration_ms)
{
  if (gate->mode != BENCH_READY || !gate->calibrated || gate->faults ||
      gate->health != BENCH_HEALTH_ALL ||
      duration_ms == 0u || duration_ms > 10000u) {
    return false;
  }
  gate->iq_ma = 0;
  gate->duration_us = duration_ms * 1000u;
  gate->started_us = now_us;
  gate->lease_us = now_us;
  gate->mode = BENCH_ACTIVE;
  return true;
}

bool bench_gate_output_allowed(const volatile bench_gate_t *gate)
{
  return gate->faults == 0u && gate->health == BENCH_HEALTH_ALL &&
         (gate->mode == BENCH_ALIGNING ||
          (gate->mode == BENCH_ACTIVE && gate->calibrated));
}

void bench_gate_tick(volatile bench_gate_t *gate, uint32_t now_us,
                     uint32_t health)
{
  gate->health = health;
  if (gate->mode == BENCH_OFF || gate->mode == BENCH_FAULT) {
    return;
  }
  if (health != BENCH_HEALTH_ALL) {
    bench_gate_latch(gate, BENCH_FAULT_HEALTH);
    return;
  }
  if (gate->mode != BENCH_ALIGNING && gate->mode != BENCH_ACTIVE) {
    return;
  }
  if ((uint32_t)(now_us - gate->lease_us) > 100000u) {
    bench_gate_latch(gate, BENCH_FAULT_LEASE);
  } else if ((uint32_t)(now_us - gate->started_us) >= gate->duration_us) {
    if (gate->mode == BENCH_ALIGNING) {
      bench_gate_latch(gate, BENCH_FAULT_ALIGN);
    } else {
      gate->mode = BENCH_READY;
      gate->iq_ma = 0;
    }
  }
}
