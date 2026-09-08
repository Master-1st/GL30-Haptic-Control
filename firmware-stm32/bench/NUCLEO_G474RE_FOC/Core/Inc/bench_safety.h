#ifndef BENCH_SAFETY_H
#define BENCH_SAFETY_H

#include <stdbool.h>
#include <stdint.h>

/* Deliberately restricted commissioning modes, not a product haptics API. */
typedef enum { BENCH_OFF, BENCH_PREPARED, BENCH_ALIGNING, BENCH_READY,
               BENCH_ACTIVE, BENCH_FAULT } bench_mode_t;
enum { BENCH_HEALTH_ALL = 0x3fu, BENCH_HEALTH_DRV = 1u,
       BENCH_HEALTH_ENC = 2u, BENCH_HEALTH_ADC = 4u,
       BENCH_HEALTH_VM = 8u, BENCH_HEALTH_NFAULT = 16u,
       BENCH_HEALTH_TIMING = 32u };
enum { BENCH_FAULT_HEALTH = 1u,
       BENCH_FAULT_LEASE = 4u, BENCH_FAULT_ALIGN = 8u,
       BENCH_FAULT_CURRENT = 16u, BENCH_FAULT_DEADLINE = 32u,
       BENCH_FAULT_BREAK = 64u, BENCH_FAULT_UART = 128u,
       BENCH_FAULT_NUMERIC = 256u, BENCH_FAULT_ADC = 512u,
       BENCH_FAULT_TEST = 1024u, BENCH_FAULT_SPEED = 2048u };
typedef struct {
  bench_mode_t mode;
  uint32_t faults;
  uint32_t health;
  uint32_t started_us;
  uint32_t lease_us;
  uint32_t duration_us;
  int32_t iq_ma;
  bool calibrated;
} bench_gate_t;

void bench_gate_init(volatile bench_gate_t *gate);
void bench_gate_latch(volatile bench_gate_t *gate, uint32_t fault);
void bench_gate_stop(volatile bench_gate_t *gate);
bool bench_gate_prepare(volatile bench_gate_t *gate, uint32_t health);
bool bench_gate_align(volatile bench_gate_t *gate, uint32_t now_us);
void bench_gate_alignment_done(volatile bench_gate_t *gate, bool success);
bool bench_gate_pulse(volatile bench_gate_t *gate, uint32_t now_us,
                      int32_t iq_ma, uint32_t duration_ms);
bool bench_gate_haptic(volatile bench_gate_t *gate, uint32_t now_us,
                       uint32_t duration_ms);
void bench_gate_tick(volatile bench_gate_t *gate, uint32_t now_us,
                     uint32_t health);
bool bench_gate_output_allowed(const volatile bench_gate_t *gate);

#endif
