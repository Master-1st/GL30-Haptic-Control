#if defined(GL30_FOC_USE_CORDIC)
#include "bench_cordic.h"
#endif
#include "bench_app.h"
#include "bench_hw.h"
#include "bench_safety.h"
#include "bench_haptics.h"
#include "main.h"
#include "foc.h"
#include "as5048a.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TWO_PI 6.28318530717958647692f
#define ADC_ZERO_SAMPLES 512u
#define PHASE_TRIP_A 0.35f
/* Nominal current qualifies the measured static hold; ALIGN is voltage driven. */
#define ALIGN_CURRENT_A 0.40f
#define ALIGN_HOLD_VOLTAGE_V 0.38f
#define VOLTAGE_CAP_V 0.30f
/* HAPTIC11: ALIGN-only headroom; IQ/haptic limits above stay unchanged. */
#define ALIGN_PHASE_TRIP_A 0.65f
#define ALIGN_VOLTAGE_CAP_V 0.60f
/* 12 V bench: bounded headroom above the observed 13.45 V sample peak. */
#define VM_MIN_V 9.0f
#define VM_MAX_V 15.0f
/* ADC1 two ranks finish just after the PWM peak. Finish before bottom UEV. */
#define ISR_BUDGET_CYCLES 3600u

static volatile bench_gate_t g_gate;
static gl30_foc_state_t g_foc, g_self_foc;
/* TIM6 (priority 2) computes into the inactive buffer with IRQs enabled.
 * ADC (priority 1) only reads the published buffer, never the candidate.
 * Publication shares the existing angle/timestamp/sequence critical section;
 * no observer arithmetic or full-state copy is performed with IRQs masked.
 * Neither observer buffer owns current PI or haptic command state. */
static gl30_foc_state_t g_encoder_observer[2];
static volatile uint8_t g_encoder_observer_index;
static volatile uint32_t g_adc_count, g_adc_us, g_enc_irq_count, g_enc_seq, g_enc_us;
static volatile uint16_t g_raw[4], g_enc_angle, g_enc_diag;
static volatile bool g_enc_valid, g_hw_ready, g_drv_ready, g_zero_valid;
static volatile float g_vm, g_ia, g_ib, g_ic;
static volatile uint32_t g_self_left, g_self_fail, g_self_max, g_isr_max;
static volatile uint32_t g_window_min = BENCH_TIMER_ARR;
/* Diagnostic costs are included in the unchanged ISR/window guards. */
typedef struct {
  uint32_t samples, observer_max, foc_max, total_max, window_min;
} bench_self_timing_t;
static volatile bench_self_timing_t g_self_timing[3];
typedef struct {
  uint32_t sample, time_us, entry_counter, mode, observer_updated, stage;
  uint32_t acquired, observed, gated, trajectory, foc, total;
  uint32_t reason, stop_cycles, stop_counter, stop_down;
} bench_loop_timing_t;
static volatile bench_loop_timing_t g_loop_timing;
static uint32_t g_reset_flags;
static volatile uint32_t g_adc_bad, g_deadlines, g_encoder_errors;
static volatile bool g_adc_synchronized;
static uint32_t g_drv_status = 0xffffffffu;
static volatile uint32_t g_drv_checked_us;
static volatile bool g_rate_ok, g_preparing;
static volatile bool g_tracing;
static uint32_t g_trace_start_us, g_trace_adc_bad_start;
static bench_drv_trace_t g_driver_trace;
static bool g_self_reported;
static volatile uint32_t g_zero_count;
static uint32_t g_zero_adc_bad_start;
static volatile bool g_zero_sync_failed, g_zero_error_pending;
static uint32_t g_zero_sum[3];
static uint16_t g_zero_min[3], g_zero_max[3];
static float g_zero[3] = {1861.0f, 1861.0f, 1861.0f};
static uint32_t g_prepare_us, g_last_observer_seq;
static unsigned int g_align_stage;
static float g_align_origin, g_align_forward, g_zero_angle, g_direction = 1.0f;
static float g_align_current_sum;
static float g_align_voltage_sum, g_align_voltage, g_applied_field;
static uint32_t g_align_current_count;
static float g_trip_velocity, g_trip_angle, g_trip_field;
static uint16_t g_trip_encoder;
static bench_adc_diag_t g_trip_adc;
static float g_trip_zero[3];
static uint32_t g_trip_outputs; /* bit0: pre-stop MOE; bit1: pre-stop nFAULT */
static bool g_trip_captured;
static uint32_t g_trip_fault;
static unsigned int g_trip_align_stage;
static float g_trip_align_move;
/* The haptic calculation runs in foreground; the 20 kHz IRQ consumes one float. */
static gl30_foc_state_t g_haptic_foc;
static volatile bool g_haptic_running;
static volatile float g_haptic_iq;
static volatile uint32_t g_haptic_us;
static bench_haptic_kind_t g_haptic_kind;
static volatile uint8_t g_rx[256];
static volatile uint16_t g_rx_write, g_rx_read;
static volatile bool g_rx_corrupt;
static volatile uint8_t g_tx[2048];
static volatile uint16_t g_tx_write, g_tx_read;
static volatile uint32_t g_uart_errors;
static char g_line[96];
static size_t g_line_len;
static bool g_discard_line;

#define ENC_CAPTURE_POINTS 2048u
#define ENC_CAPTURE_BATCH 16u
typedef struct {
  uint32_t time_us;
  uint16_t angle; /* 0xffff marks an invalid SPI/diagnostic sample. */
  uint16_t diagnostics;
} bench_enc_point_t;
/* Capture at the encoder IRQ, not the low-rate serial STATUS observer. */
static volatile bench_enc_point_t g_enc_capture[ENC_CAPTURE_POINTS];
static volatile uint16_t g_enc_capture_write, g_enc_capture_count;
static volatile uint32_t g_enc_capture_total;
static volatile bool g_enc_capture_running, g_enc_capture_rolling;

/* Call with interrupts locked; only the encoder IRQ appends samples. */
static void encoder_capture_start(bool rolling)
{
  g_enc_capture_write = 0u;
  g_enc_capture_count = 0u;
  g_enc_capture_total = 0u;
  g_enc_capture_rolling = rolling;
  g_enc_capture_running = true;
}

static void encoder_capture_record(uint32_t now, uint16_t angle, uint16_t diag)
{
  if (!g_enc_capture_running) { return; }
  const uint16_t index = g_enc_capture_write;
  g_enc_capture[index].time_us = now;
  g_enc_capture[index].angle = angle;
  g_enc_capture[index].diagnostics = diag;
  g_enc_capture_write = (uint16_t)((index + 1u) & (ENC_CAPTURE_POINTS - 1u));
  if (g_enc_capture_count < ENC_CAPTURE_POINTS) { g_enc_capture_count++; }
  g_enc_capture_total++;
  if (!g_enc_capture_rolling && g_enc_capture_count == ENC_CAPTURE_POINTS) {
    g_enc_capture_running = false;
  }
}

/* Frozen via the existing PREPARE/output lifecycle, read only through SWD
 * while stopped. Retain raw pre-trip history without extra UART IRQ traffic. */
#define CURRENT_CAPTURE_POINTS 512u
typedef struct {
  uint32_t time_us;
  uint16_t raw[4];
  uint16_t timer_count;
  uint16_t flags; /* bit0 sync, bit1 MOE, bit2 nFAULT, bit3 CS low,
                   * bits4..6 gate mode, bit7 SPI3 busy (IRQ read time). */
} bench_current_point_t;
static volatile bench_current_point_t g_current_capture[CURRENT_CAPTURE_POINTS];
static volatile uint16_t g_current_capture_write, g_current_capture_count;
static volatile uint32_t g_current_capture_total;
static volatile bool g_current_capture_idle_trip;

static void current_capture_start(void)
{
  g_current_capture_write = 0u;
  g_current_capture_count = 0u;
  g_current_capture_total = 0u;
  g_current_capture_idle_trip = false;
}

static void current_capture_record(uint32_t now, bool synchronized)
{
  const bool prepared = g_gate.mode == BENCH_PREPARED && g_zero_valid;
  if (g_current_capture_idle_trip ||
      (!(g_preparing && g_drv_ready && g_zero_count < ADC_ZERO_SAMPLES) &&
       !prepared && !bench_gate_output_allowed(&g_gate))) { return; }
  const uint16_t index = g_current_capture_write;
  g_current_capture[index].time_us = now;
  for (unsigned int i = 0u; i < 4u; ++i) { g_current_capture[index].raw[i] = g_raw[i]; }
  g_current_capture[index].timer_count = (uint16_t)LL_TIM_GetCounter(TIM1);
  g_current_capture[index].flags = (uint16_t)((synchronized ? 1u : 0u) |
      (LL_TIM_IsEnabledAllOutputs(TIM1) ? 2u : 0u) | (bench_hw_nfault() ? 4u : 0u) |
      (!LL_GPIO_IsOutputPinSet(DRV_CS_GPIO_Port, DRV_CS_Pin) ? 8u : 0u) |
      ((unsigned int)g_gate.mode << 4u) | (LL_SPI_IsActiveFlag_BSY(SPI3) ? 128u : 0u));
  g_current_capture_write = (uint16_t)((index + 1u) & (CURRENT_CAPTURE_POINTS - 1u));
  if (g_current_capture_count < CURRENT_CAPTURE_POINTS) { g_current_capture_count++; }
  g_current_capture_total++;
  /* Diagnostic freeze only, with all six bridge inputs low. Do not alter
   * output current protection, calibrate around spikes, or filter raw data. */
  if (prepared && (fabsf((float)g_raw[0] - g_zero[0]) > 60.0f ||
                   fabsf((float)g_raw[1] - g_zero[1]) > 60.0f ||
                   fabsf((float)g_raw[2] - g_zero[2]) > 60.0f)) {
    g_current_capture_idle_trip = true;
  }
}

static uint32_t irq_lock(void)
{
  const uint32_t mask = __get_PRIMASK();
  __disable_irq();
  return mask;
}

static void irq_unlock(uint32_t mask) { __set_PRIMASK(mask); }

/* Output commands run in foreground. Reserve TIM1 bottom before the existing
 * start critical section: even a short lock at ADC trigger can make the first
 * current-loop update late. Never wait with IRQs masked or relax the deadline. */
static bool output_start_lock(uint32_t *mask)
{
  if (__get_PRIMASK() != 0u) { return false; }
  const uint32_t start = bench_now_us();
  while ((uint32_t)(bench_now_us() - start) <= 150u) {
    if (LL_TIM_GetCounter(TIM1) >= 400u) { continue; }
    *mask = irq_lock();
    if (LL_TIM_GetCounter(TIM1) < 400u) { return true; }
    irq_unlock(*mask);
  }
  return false;
}

static void latch(uint32_t fault)
{
  const uint32_t outputs = (LL_TIM_IsEnabledAllOutputs(TIM1) ? 1u : 0u) |
                           (bench_hw_nfault() ? 2u : 0u);
  bench_hw_off();
  g_enc_capture_running = false;
  /* Preserve the first stop, before subsequent idle observations overwrite it. */
  if (!g_trip_captured) {
    g_trip_captured = true;
    g_trip_fault = fault;
    g_trip_align_stage = g_align_stage;
    g_trip_align_move = g_foc.theta_unwrapped_rad - g_align_origin;
    g_trip_velocity = g_foc.velocity_rad_s;
    g_trip_angle = g_foc.theta_unwrapped_rad;
    g_trip_field = g_applied_field;
    g_trip_encoder = g_enc_angle;
    g_trip_adc.sample = g_adc_count;
    g_trip_adc.time_us = g_adc_us;
    g_trip_adc.bad = g_adc_bad;
    g_trip_adc.synchronized = g_adc_synchronized ? 1u : 0u;
    for (unsigned int i = 0u; i < 4u; ++i) { g_trip_adc.raw[i] = g_raw[i]; }
    for (unsigned int i = 0u; i < 3u; ++i) { g_trip_zero[i] = g_zero[i]; }
    g_trip_outputs = outputs;
  }
  g_preparing = false;
  g_tracing = false;
  g_drv_ready = false;
  g_zero_valid = false;
  g_haptic_running = false;
  g_haptic_iq = 0.0f;
  bench_gate_latch(&g_gate, fault);
  gl30_foc_force_zero(&g_foc);
}

static void tx(const char *text);

/* Save pre-stop cost separately: latching and GPIO parking are not FOC time. */
static void deadline_latch(uint32_t reason, uint32_t started)
{
  const uint32_t cycles = DWT->CYCCNT - started;
  const uint32_t counter = LL_TIM_GetCounter(TIM1);
  const uint32_t down = LL_TIM_GetDirection(TIM1) == LL_TIM_COUNTERDIRECTION_DOWN;
  const bool first = !g_trip_captured;
  g_deadlines++;
  latch(BENCH_FAULT_DEADLINE);
  if (first) {
    g_loop_timing.reason = reason;
    g_loop_timing.stop_cycles = cycles;
    g_loop_timing.stop_counter = counter;
    g_loop_timing.stop_down = down;
  }
}

static void timing_diagnostics(void)
{
  if (g_gate.mode == BENCH_ALIGNING || g_gate.mode == BENCH_ACTIVE ||
      g_preparing || g_tracing || g_self_left || LL_TIM_IsEnabledAllOutputs(TIM1)) {
    tx("ERR TIMING_DIAG_REQUIRES_OUTPUT_OFF\r\n"); return;
  }
  char line[320];
  for (unsigned int kind = 0u; kind < 3u; ++kind) {
    const bench_self_timing_t t = g_self_timing[kind];
    (void)snprintf(line, sizeof(line),
        "SELF_TIMING kind=%u samples=%lu observer=%lu foc=%lu total=%lu window=%lu\r\n",
        kind, (unsigned long)t.samples, (unsigned long)t.observer_max,
        (unsigned long)t.foc_max, (unsigned long)t.total_max, (unsigned long)t.window_min);
    tx(line);
  }
  const bench_loop_timing_t t = g_loop_timing;
  (void)snprintf(line, sizeof(line),
      "LOOP_TIMING sample=%lu us=%lu entry=%lu mode=%lu observer=%lu stage=%lu "
      "acquired=%lu observed=%lu gated=%lu trajectory=%lu foc=%lu total=%lu "
      "reason=%lu stop=%lu counter=%lu down=%lu\r\n",
      (unsigned long)t.sample, (unsigned long)t.time_us, (unsigned long)t.entry_counter,
      (unsigned long)t.mode, (unsigned long)t.observer_updated, (unsigned long)t.stage,
      (unsigned long)t.acquired, (unsigned long)t.observed, (unsigned long)t.gated,
      (unsigned long)t.trajectory, (unsigned long)t.foc, (unsigned long)t.total,
      (unsigned long)t.reason, (unsigned long)t.stop_cycles,
      (unsigned long)t.stop_counter, (unsigned long)t.stop_down);
  tx(line);
#if defined(GL30_FOC_USE_CORDIC)
  const bench_cordic_diag_t *math = bench_cordic_diagnostics();
  (void)snprintf(line, sizeof(line),
      "CORDIC_TEST samples=%lu failures=%lu max_cycles=%lu error_ppb=%lu norm_ppb=%lu\r\n",
      (unsigned long)math->samples, (unsigned long)math->failures,
      (unsigned long)math->max_cycles, (unsigned long)(math->max_error * 1.0e9f),
      (unsigned long)(math->max_norm_error * 1.0e9f));
  tx(line);
#endif
  tx("OK TIMING_DIAG_OUTPUT_OFF\r\n");
}

static void self_timing_reset(void)
{
  for (unsigned int i = 0u; i < 3u; ++i) {
    g_self_timing[i] = (bench_self_timing_t){.window_min = BENCH_TIMER_ARR};
  }
}

static float wrap_angle(float angle)
{
  if (angle >= -0.5f * TWO_PI && angle <= 0.5f * TWO_PI) { return angle; }
  /* Canonical mechanical angle minus any canonical calibration zero spans
   * at most one turn; seven pole pairs therefore need at most seven steps. */
  if (angle >= -7.0f * TWO_PI && angle <= 7.0f * TWO_PI) {
    while (angle > 0.5f * TWO_PI) { angle -= TWO_PI; }
    while (angle < -0.5f * TWO_PI) { angle += TWO_PI; }
    return angle;
  }
  return remainderf(angle, TWO_PI);
}

static uint32_t health(uint32_t now)
{
  uint32_t bits = 0u;
  if (g_drv_ready && (uint32_t)(now - g_drv_checked_us) < 50000u) {
    bits |= BENCH_HEALTH_DRV;
  }
  if (g_enc_valid && g_enc_seq != 0u && (uint32_t)(now - g_enc_us) <= 750u) {
    bits |= BENCH_HEALTH_ENC;
  }
  if (g_zero_valid && (uint32_t)(now - g_adc_us) <= 100u) {
    bits |= BENCH_HEALTH_ADC;
  }
  if (isfinite(g_vm) && g_vm >= VM_MIN_V && g_vm <= VM_MAX_V) { bits |= BENCH_HEALTH_VM; }
  if (bench_hw_nfault()) { bits |= BENCH_HEALTH_NFAULT; }
  if (g_hw_ready && g_rate_ok && g_self_left == 0u && g_self_fail == 0u &&
      g_self_max != 0u && g_self_max < ISR_BUDGET_CYCLES && g_deadlines == 0u) {
    bits |= BENCH_HEALTH_TIMING;
  }
  return bits;
}

static void tx(const char *text)
{
  const size_t length = strlen(text);
  const uint16_t free_space = (uint16_t)((g_tx_read - g_tx_write - 1u) & 2047u);
  if (length > free_space) {
    g_uart_errors++;
    if (bench_gate_output_allowed(&g_gate)) {
      uint32_t mask = irq_lock(); latch(BENCH_FAULT_UART); irq_unlock(mask);
    }
    return;
  }
  uint16_t index = g_tx_write;
  while (*text != '\0') {
    g_tx[index] = (uint8_t)*text++;
    index = (uint16_t)((index + 1u) & 2047u);
  }
  __DMB();
  g_tx_write = index;
  LL_USART_EnableIT_TXE(USART2);
}

static void status(void)
{
  char line[832];
  /* Pair freshness checks with the same encoder/self-test snapshot. Otherwise
   * an encoder IRQ after reading now can make unsigned sample age underflow.
   * Keep formatting and UART work outside this short critical section. */
  const uint32_t mask = irq_lock();
  const uint32_t now = bench_now_us();
  const uint32_t health_bits = health(now);
  const uint32_t enc_age_us = now - g_enc_us;
  const uint32_t enc_irq = g_enc_irq_count;
  const uint32_t enc_ok = g_enc_seq;
  const uint32_t enc_err = g_encoder_errors;
  const uint16_t enc_angle = g_enc_angle, enc_diag = g_enc_diag;
  const uint32_t self_left = g_self_left;
  const float angle = g_foc.theta_unwrapped_rad, velocity = g_foc.velocity_rad_s;
  const float align_move = angle - g_align_origin;
  const unsigned int align_stage = g_align_stage;
  const float trip_velocity = g_trip_velocity, trip_angle = g_trip_angle, trip_field = g_trip_field;
  const uint16_t trip_encoder = g_trip_encoder;
  irq_unlock(mask);
  (void)snprintf(line, sizeof(line),
      "STATUS mode=%u fault=%lu health=%lu moe=%lu off=%lu button=%u "
      "vm_mv=%ld ia_ma=%ld ib_ma=%ld ic_ma=%ld iq_ma=%ld id_ma=%ld "
      "enc=%u diag=%u age_us=%lu adc=%lu enc_irq=%lu enc_ok=%lu enc_err=%lu zero=%u "
      "self_left=%lu self_fail=%lu self_max=%lu isr_max=%lu deadline=%lu "
      "adc_bad=%lu uart_err=%lu drv=%lu rate=%u calibrated=%u window_min=%lu iwdg_reset=%u "
      "angle_mrad=%ld vel_mrad_s=%ld align_stage=%u align_move_mrad=%ld "
      "trip_vel_mrad_s=%ld trip_angle_mrad=%ld trip_field_mrad=%ld trip_enc=%u "
      "hap=%u iq_ref_ma=%ld hap_age_us=%lu vd_mv=%ld vq_mv=%ld field_mrad=%ld\r\n",
      (unsigned int)g_gate.mode, (unsigned long)g_gate.faults,
      (unsigned long)health_bits, (unsigned long)LL_TIM_IsEnabledAllOutputs(TIM1),
      (unsigned long)LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin),
      bench_hw_button() ? 1u : 0u, (long)(g_vm * 1000.0f),
      (long)(g_ia * 1000.0f), (long)(g_ib * 1000.0f), (long)(g_ic * 1000.0f),
      (long)(g_foc.i_q_a * 1000.0f), (long)(g_foc.i_d_a * 1000.0f),
      (unsigned int)enc_angle, (unsigned int)enc_diag,
      (unsigned long)enc_age_us,
      (unsigned long)g_adc_count, (unsigned long)enc_irq,
      (unsigned long)enc_ok, (unsigned long)enc_err,
      (unsigned int)g_zero_valid, (unsigned long)self_left,
      (unsigned long)g_self_fail, (unsigned long)g_self_max,
      (unsigned long)g_isr_max, (unsigned long)g_deadlines,
      (unsigned long)g_adc_bad, (unsigned long)g_uart_errors,
      (unsigned long)g_drv_status, (unsigned int)g_rate_ok,
      (unsigned int)g_gate.calibrated, (unsigned long)g_window_min,
      (g_reset_flags & RCC_CSR_IWDGRSTF) ? 1u : 0u,
      (long)(angle * 1000.0f), (long)(velocity * 1000.0f), align_stage,
      (long)(align_move * 1000.0f), (long)(trip_velocity * 1000.0f),
      (long)(trip_angle * 1000.0f), (long)(trip_field * 1000.0f), (unsigned int)trip_encoder,
      g_haptic_running ? (unsigned int)g_haptic_kind + 1u : 0u,
      (long)(g_foc.i_q_ref_a * 1000.0f),
      g_haptic_running ? (unsigned long)(bench_now_us() - g_haptic_us) : 0u,
      (long)(g_foc.v_d_v * 1000.0f), (long)(g_foc.v_q_v * 1000.0f),
      (long)(g_applied_field * 1000.0f));
  tx(line);
}

static void report_encoder_sample(const char *name, const bench_enc_diag_t *sample)
{
  char line[256];
  (void)snprintf(line, sizeof(line),
      "ENC_%s sample=%lu us=%lu raw=%04X,%04X,%04X mask=%u reason=%u "
      "angle_flags=%u diag_flags=%u spi_stage=%lu spi_us=%lu spi_sr=%04lX\r\n",
      name, (unsigned long)sample->sample, (unsigned long)sample->time_us,
      (unsigned int)sample->raw[0], (unsigned int)sample->raw[1],
      (unsigned int)sample->raw[2], (unsigned int)sample->transfer_mask,
      (unsigned int)sample->reason, (unsigned int)as5048a_response_faults(sample->raw[1]),
      (unsigned int)as5048a_response_faults(sample->raw[2]),
      (unsigned long)sample->spi.stage, (unsigned long)sample->spi.elapsed_us,
      (unsigned long)sample->spi.sr);
  tx(line);
}

static const char *drv_stage_name(bench_drv_stage_t stage)
{
  switch (stage) {
    case BENCH_DRV_STAGE_UNLOCK: return "UNLOCK";
    case BENCH_DRV_STAGE_CONFIG_WRITE: return "CONFIG_WRITE";
    case BENCH_DRV_STAGE_CONFIG_READ: return "CONFIG_READ";
    case BENCH_DRV_STAGE_STATUS_PRE: return "STATUS_PRE";
    case BENCH_DRV_STAGE_CLEAR_WRITE: return "CLEAR_WRITE";
    case BENCH_DRV_STAGE_CLEAR_READ: return "CLEAR_READ";
    case BENCH_DRV_STAGE_LOCK_WRITE: return "LOCK_WRITE";
    case BENCH_DRV_STAGE_LOCK_READ: return "LOCK_READ";
    case BENCH_DRV_STAGE_STATUS_FINAL: return "STATUS_FINAL";
    case BENCH_DRV_STAGE_FINAL_GAIN: return "FINAL_GAIN";
    case BENCH_DRV_STAGE_FINAL_BUCK: return "FINAL_BUCK";
    case BENCH_DRV_STAGE_READY: return "READY";
    case BENCH_DRV_STAGE_NONE: return "NONE";
  }
  return "UNKNOWN";
}

static const char *drv_reason_name(bench_drv_reason_t reason)
{
  switch (reason) {
    case BENCH_DRV_REASON_TRANSPORT: return "TRANSPORT";
    case BENCH_DRV_REASON_READBACK: return "READBACK";
    case BENCH_DRV_REASON_STATUS_FLAGS: return "STATUS_FLAGS";
    case BENCH_DRV_REASON_NFAULT: return "NFAULT";
    case BENCH_DRV_REASON_NONE: return "NONE";
    case BENCH_DRV_REASON_GUARD: return "GUARD";
  }
  return "UNKNOWN";
}

void Bench_AdcDiagnostics(struct bench_adc_diag *out)
{
  /* Called with IRQs masked; no acquisition, waits, formatting or SPI. */
  out->sample = g_adc_count;
  out->time_us = g_adc_us;
  out->bad = g_adc_bad;
  for (unsigned int i = 0u; i < 4u; ++i) { out->raw[i] = g_raw[i]; }
  out->synchronized = g_adc_synchronized ? 1u : 0u;
}

bool Bench_DriverTraceSafe(void)
{
  const uint32_t mask = irq_lock();
  const uint32_t now = bench_now_us();
  const bool safe = g_tracing && g_gate.mode == BENCH_OFF && !g_gate.faults &&
      !g_preparing && !g_drv_ready && !g_zero_valid && g_hw_ready && g_rate_ok &&
      !g_self_left && !g_self_fail && g_self_max > 0u &&
      g_self_max < ISR_BUDGET_CYCLES && !g_deadlines &&
      g_adc_synchronized && g_adc_count && g_adc_bad == g_trace_adc_bad_start &&
      (uint32_t)(now - g_adc_us) <= 100u &&
      (uint32_t)(now - g_trace_start_us) < 50000u &&
      isfinite(g_vm) && g_vm >= VM_MIN_V && g_vm <= VM_MAX_V;
  irq_unlock(mask);
  return safe;
}

static void report_driver_state(const char *name, const bench_drv_state_t *state)
{
  char line[256];
  if (!state->valid) { return; }
  (void)snprintf(line, sizeof(line),
      "DRV_%s us=%lu nfault=%u nsleep=%u drvoff=%u moe=%u ccer=%08lX "
      "pb12_pull=%lu pupdr=%08lX pa=%04lX/%04lX pb=%04lX/%04lX pc=%04lX/%04lX\r\n",
      name, (unsigned long)state->time_us, (unsigned int)state->nfault,
      (unsigned int)state->nsleep, (unsigned int)state->drvoff,
      (unsigned int)state->moe, (unsigned long)state->tim1_ccer,
      (unsigned long)((state->pb12_pupdr >> 24u) & 3u),
      (unsigned long)state->pb12_pupdr,
      (unsigned long)state->gpioa_idr, (unsigned long)state->gpioa_odr,
      (unsigned long)state->gpiob_idr, (unsigned long)state->gpiob_odr,
      (unsigned long)state->gpioc_idr, (unsigned long)state->gpioc_odr);
  tx(line);
  (void)snprintf(line, sizeof(line),
      "DRV_%s_ADC sample=%lu us=%lu age_us=%lu raw=%u,%u,%u,%u sync=%u bad=%lu\r\n",
      name, (unsigned long)state->adc.sample, (unsigned long)state->adc.time_us,
      (unsigned long)(state->time_us - state->adc.time_us),
      (unsigned int)state->adc.raw[0], (unsigned int)state->adc.raw[1],
      (unsigned int)state->adc.raw[2], (unsigned int)state->adc.raw[3],
      (unsigned int)state->adc.synchronized, (unsigned long)state->adc.bad);
  tx(line);
}

static void report_driver_diagnostics(const bench_drv_diag_t *diag)
{
  char line[320];
  if (diag->stage == BENCH_DRV_STAGE_NONE) {
    tx("DRV_DIAG NONE\r\n");
    return;
  }
  (void)snprintf(line, sizeof(line),
      "DRV_DIAG cached=1 stage=%s(%u) reason=%s(%u) expected=%u tx=%04X rx=%04X ok=%u norm=%lu cfg=%u "
      "stat_mask=%u s0=%04X@%lu s1=%04X@%lu s2=%04X@%lu relock=%u/%u relock_rx=%04X raw=%06lX\r\n",
      drv_stage_name(diag->stage), (unsigned int)diag->stage,
      drv_reason_name(diag->reason), (unsigned int)diag->reason,
      (unsigned int)diag->expected, (unsigned int)diag->tx, (unsigned int)diag->rx,
      (unsigned int)diag->transfer_ok, (unsigned long)diag->normalized_status,
      (unsigned int)diag->config_read_mask, (unsigned int)diag->status_read_mask,
      (unsigned int)diag->status_rx[0], (unsigned long)diag->status_us[0],
      (unsigned int)diag->status_rx[1], (unsigned long)diag->status_us[1],
      (unsigned int)diag->status_rx[2], (unsigned long)diag->status_us[2],
      (unsigned int)diag->relock_attempted, (unsigned int)diag->relock_ok,
      (unsigned int)diag->relock_rx, (unsigned long)diag->raw_status);
  tx(line);
  /* Reporting happens only in the main loop after the driver is hard-off. */
  report_driver_state("PRE_CLEANUP", &diag->pre_cleanup);
  report_driver_state("POST_CLEANUP", &diag->post_cleanup);
}

static void driver_diagnostics(void)
{
  if (g_gate.mode != BENCH_OFF || g_preparing || g_self_left || bench_hw_button() ||
      LL_TIM_IsEnabledAllOutputs(TIM1) ||
      !LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin)) {
    tx("ERR DRV_DIAG_REQUIRES_OFF_RELEASE_BUTTON_MOE0_DRVOFF1\r\n");
    return;
  }
  bench_drv_diag_t diag;
  bench_hw_driver_diagnostics(&diag);
  report_driver_diagnostics(&diag);
  tx("OK DRV_DIAG_OUTPUT_OFF\r\n");
}

static void driver_poll_diagnostics(void)
{
  if ((g_gate.mode != BENCH_OFF && g_gate.mode != BENCH_FAULT) ||
      g_preparing || g_self_left || bench_hw_button() ||
      LL_TIM_IsEnabledAllOutputs(TIM1) ||
      !LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin)) {
    tx("ERR DRV_POLL_REQUIRES_OFF_OR_FAULT_RELEASE_BUTTON_MOE0_DRVOFF1\r\n");
    return;
  }
  bench_drv_poll_diag_t poll;
  bench_hw_driver_poll_diagnostics(&poll);
  if (!poll.valid) {
    tx("DRV_POLL NONE\r\n");
  } else {
    const bench_drv_diag_t *diag = &poll.diag;
    char line[320];
    (void)snprintf(line, sizeof(line),
        "DRV_POLL cached=1 stage=%s(%u) reason=%s(%u) expected=%u tx=%04X rx=%04X ok=%u norm=%lu "
        "stat_mask=%u s0=%04X@%lu s1=%04X@%lu s2=%04X@%lu raw=%06lX\r\n",
        drv_stage_name(diag->stage), (unsigned int)diag->stage,
        drv_reason_name(diag->reason), (unsigned int)diag->reason,
        (unsigned int)diag->expected, (unsigned int)diag->tx, (unsigned int)diag->rx,
        (unsigned int)diag->transfer_ok, (unsigned long)diag->normalized_status,
        (unsigned int)diag->status_read_mask,
        (unsigned int)diag->status_rx[0], (unsigned long)diag->status_us[0],
        (unsigned int)diag->status_rx[1], (unsigned long)diag->status_us[1],
        (unsigned int)diag->status_rx[2], (unsigned long)diag->status_us[2],
        (unsigned long)diag->raw_status);
    tx(line);
    report_driver_state("POLL_ENTRY", &poll.entry);
    report_driver_state("POLL_RESULT", &poll.result);
  }
  tx("OK DRV_POLL_OUTPUT_OFF\r\n");
}

static void driver_live(void)
{
  if (g_gate.mode != BENCH_OFF || g_preparing || g_self_left || bench_hw_button() ||
      LL_TIM_IsEnabledAllOutputs(TIM1) ||
      !LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin)) {
    tx("ERR DRV_LIVE_REQUIRES_OFF_RELEASE_BUTTON_MOE0_DRVOFF1\r\n");
    return;
  }
  const float vm = g_vm;
  if (!isfinite(vm) || vm < VM_MIN_V || vm > VM_MAX_V ||
      !LL_GPIO_IsOutputPinSet(DRV_NSLEEP_GPIO_Port, DRV_NSLEEP_Pin)) {
    tx("ERR DRV_LIVE_REQUIRES_VM9_15V_NSLEEP1_NO_AUTO_WAKE\r\n");
    return;
  }
  bench_drv_diag_t diag;
  const bool complete = bench_hw_driver_snapshot(&diag);
  if (diag.stage == BENCH_DRV_STAGE_NONE) {
    tx("ERR DRV_LIVE_HARDWARE_GUARD_NO_SPI\r\n");
    return;
  }
  const bool nfault = bench_hw_nfault();
  char line[320];
  (void)snprintf(line, sizeof(line),
      "DRV_LIVE transfer_ok=%u reason=%s norm=%lu raw=%06lX stat_mask=%u "
      "s0=%04X@%lu s1=%04X@%lu s2=%04X@%lu nfault=%u nsleep=%u off=%u moe=%u vm_mv=%ld\r\n",
      (unsigned int)complete, drv_reason_name(diag.reason),
      (unsigned long)diag.normalized_status, (unsigned long)diag.raw_status,
      (unsigned int)diag.status_read_mask,
      (unsigned int)diag.status_rx[0], (unsigned long)diag.status_us[0],
      (unsigned int)diag.status_rx[1], (unsigned long)diag.status_us[1],
      (unsigned int)diag.status_rx[2], (unsigned long)diag.status_us[2],
      (unsigned int)nfault,
      (unsigned int)LL_GPIO_IsOutputPinSet(DRV_NSLEEP_GPIO_Port, DRV_NSLEEP_Pin),
      (unsigned int)LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin),
      (unsigned int)LL_TIM_IsEnabledAllOutputs(TIM1), (long)(vm * 1000.0f));
  tx(line);
}

static void driver_trace(void)
{
  if (g_gate.mode != BENCH_OFF || g_gate.faults || g_preparing || g_tracing ||
      g_self_left || !g_hw_ready || !g_rate_ok || g_self_fail ||
      bench_hw_button() || LL_TIM_IsEnabledAllOutputs(TIM1) ||
      !LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin) ||
      !isfinite(g_vm) || g_vm < VM_MIN_V || g_vm > VM_MAX_V) {
    tx("ERR DRV_TRACE_REQUIRES_OFF_SELFTEST_VM9_15V_RELEASE_BUTTON\r\n");
    return;
  }
  uint32_t mask = irq_lock();
  g_drv_ready = false;
  g_zero_valid = false;
  g_zero_count = 0u;
  g_gate.calibrated = false;
  g_trace_start_us = bench_now_us();
  g_trace_adc_bad_start = g_adc_bad;
  g_tracing = true;
  irq_unlock(mask);
  const bool complete = bench_hw_driver_trace(&g_driver_trace);
  mask = irq_lock();
  bench_hw_off();
  g_tracing = false;
  g_drv_ready = false;
  g_zero_valid = false;
  gl30_foc_force_zero(&g_foc);
  irq_unlock(mask);
  char line[192];
  (void)snprintf(line, sizeof(line),
      "DRV_TRACE complete=%u points=%u first_issue=%u clear_attempted=%u reason=%s "
      "relock=%u/%u ready=0 moe=%lu off=%lu\r\n",
      (unsigned int)complete, (unsigned int)g_driver_trace.count,
      (unsigned int)g_driver_trace.first_issue, (unsigned int)g_driver_trace.clear_attempted,
      drv_reason_name(g_driver_trace.last.reason),
      (unsigned int)g_driver_trace.last.relock_attempted,
      (unsigned int)g_driver_trace.last.relock_ok,
      (unsigned long)LL_TIM_IsEnabledAllOutputs(TIM1),
      (unsigned long)LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin));
  tx(line);
  report_driver_diagnostics(&g_driver_trace.last);
  tx("OK DRV_TRACE_OUTPUT_OFF_NOT_READY\r\n");
}

static void driver_trace_get(unsigned int index)
{
  if (g_gate.mode != BENCH_OFF || g_preparing || g_tracing || bench_hw_button() ||
      LL_TIM_IsEnabledAllOutputs(TIM1) ||
      !LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin)) {
    tx("ERR DRV_TRACE_GET_REQUIRES_OFF_RELEASE_BUTTON\r\n"); return;
  }
  if (index >= g_driver_trace.count) { tx("ERR DRV_TRACE_GET_INDEX\r\n"); return; }
  const bench_drv_trace_point_t *point = &g_driver_trace.points[index];
  static const char *const names[] = {"WAKE", "UNLOCK", "CONFIG", "CLEAR"};
  char line[256];
  (void)snprintf(line, sizeof(line),
      "DRV_TRACE_POINT index=%u phase=%s reg=%u mask=%u norm=%lu "
      "s0=%04X@%lu s1=%04X@%lu s2=%04X@%lu\r\n",
      index, names[point->phase], (unsigned int)point->reg, (unsigned int)point->mask,
      (unsigned long)point->normalized_status,
      (unsigned int)point->rx[0], (unsigned long)point->us[0],
      (unsigned int)point->rx[1], (unsigned long)point->us[1],
      (unsigned int)point->rx[2], (unsigned long)point->us[2]);
  tx(line);
  report_driver_state("TRACE_POINT", &point->state);
  tx("OK DRV_TRACE_GET_CACHED_OUTPUT_OFF\r\n");
}

static void arm_diagnostics(void)
{
  if ((g_gate.mode != BENCH_OFF && g_gate.mode != BENCH_FAULT) || g_preparing || g_tracing ||
      LL_TIM_IsEnabledAllOutputs(TIM1) ||
      !LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin)) {
    tx("ERR ARM_DIAG_REQUIRES_OFF_OR_FAULT_MOE0_DRVOFF1\r\n"); return;
  }
  bench_arm_diag_t diag;
  const uint32_t mask = irq_lock();
  bench_hw_arm_diagnostics(&diag);
  irq_unlock(mask);
  char line[256];
  (void)snprintf(line, sizeof(line),
      "ARM_DIAG cached=1 attempt=%lu us=%lu stage=%u guard=%lu sr=%08lX bdtr=%08lX "
      "ccer=%08lX pb_idr=%08lX pc_idr=%08lX pc_odr=%08lX\r\n",
      (unsigned long)diag.attempt, (unsigned long)diag.time_us, (unsigned int)diag.stage,
      (unsigned long)diag.guard_flags, (unsigned long)diag.tim1_sr, (unsigned long)diag.tim1_bdtr,
      (unsigned long)diag.tim1_ccer, (unsigned long)diag.gpiob_idr,
      (unsigned long)diag.gpioc_idr, (unsigned long)diag.gpioc_odr);
  tx(line);
  tx("OK ARM_DIAG_CACHED_OUTPUT_OFF\r\n");
}

static void encoder_diagnostics(bool clear_error)
{
  if (g_gate.mode != BENCH_OFF || g_preparing || g_self_left || bench_hw_button() ||
      LL_TIM_IsEnabledAllOutputs(TIM1) ||
      !LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin)) {
    tx("ERR ENC_DIAG_REQUIRES_OFF_RELEASE_BUTTON\r\n"); return;
  }
  bench_enc_diag_t first, latest;
  bench_enc_clear_diag_t clear;
  /* No bridge operation or interrupt-wide lock; ADC timing remains untouched. */
  NVIC_DisableIRQ(TIM6_DAC_IRQn);
  if (clear_error) { bench_hw_encoder_clear(); }
  bench_hw_encoder_diagnostics(&first, &latest, &clear);
  NVIC_EnableIRQ(TIM6_DAC_IRQn);
  report_encoder_sample("FIRST", &first);
  report_encoder_sample("LATEST", &latest);
  char line[192];
  (void)snprintf(line, sizeof(line),
      "ENC_ERROR_READ count=%lu previous=%04X raw=%04X mask=%u flags=%u "
      "spi_stage=%lu spi_us=%lu spi_sr=%04lX\r\n",
      (unsigned long)clear.count, (unsigned int)clear.previous,
      (unsigned int)clear.error, (unsigned int)clear.transfer_mask,
      (unsigned int)as5048a_response_faults(clear.error),
      (unsigned long)clear.spi.stage, (unsigned long)clear.spi.elapsed_us,
      (unsigned long)clear.spi.sr);
  tx(line);
  if (clear_error && (clear.transfer_mask != 3u ||
      (as5048a_response_faults(clear.error) & AS5048A_RESPONSE_PARITY) != 0u ||
      (clear.error & 0x3ff8u) != 0u)) {
    tx("ERR ENC_CLEAR_READBACK_INVALID\r\n"); return;
  }
  /* This confirms the explicit read/clear attempt, not sustained link recovery. */
  tx(clear_error ? "OK ENC_CLEAR_ATTEMPT_OUTPUT_OFF\r\n" : "OK ENC_DIAG_OUTPUT_OFF\r\n");
}

static void encoder_field(void)
{
  if ((g_gate.mode != BENCH_OFF && g_gate.mode != BENCH_FAULT) ||
      g_preparing || g_tracing || g_self_left ||
      LL_TIM_IsEnabledAllOutputs(TIM1) ||
      !LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin)) {
    tx("ERR ENC_FIELD_REQUIRES_OUTPUT_OFF\r\n"); return;
  }
  bench_enc_field_diag_t field;
  NVIC_DisableIRQ(TIM6_DAC_IRQn);
  const bool ok = bench_hw_encoder_field(&field);
  NVIC_EnableIRQ(TIM6_DAC_IRQn);
  char line[256];
  (void)snprintf(line, sizeof(line),
      "ENC_FIELD us=%lu raw=%04X,%04X,%04X,%04X mask=%u valid=%u "
      "angle=%u diag=%04X agc=%u mag=%u spi_stage=%lu spi_us=%lu\r\n",
      (unsigned long)field.time_us, (unsigned int)field.raw[0],
      (unsigned int)field.raw[1], (unsigned int)field.raw[2], (unsigned int)field.raw[3],
      (unsigned int)field.transfer_mask, (unsigned int)field.valid_mask,
      (unsigned int)field.angle, (unsigned int)field.diagnostics,
      (unsigned int)(field.diagnostics & 0xffu), (unsigned int)field.magnitude,
      (unsigned long)field.spi.stage, (unsigned long)field.spi.elapsed_us);
  tx(line);
  tx(ok ? "OK ENC_FIELD_READ_ONLY_OUTPUT_OFF\r\n" : "ERR ENC_FIELD_READ_INVALID\r\n");
}


static void current_diagnostics(void)
{
  if (g_gate.mode == BENCH_ACTIVE || g_gate.mode == BENCH_ALIGNING ||
      g_preparing || g_tracing || LL_TIM_IsEnabledAllOutputs(TIM1)) {
    tx("ERR CURRENT_DIAG_REQUIRES_OUTPUT_OFF\r\n"); return;
  }
  const uint32_t mask = irq_lock();
  const bench_adc_diag_t live = {
    .sample = g_adc_count, .time_us = g_adc_us, .bad = g_adc_bad,
    .raw = {g_raw[0], g_raw[1], g_raw[2], g_raw[3]},
    .synchronized = g_adc_synchronized ? 1u : 0u
  };
  const bench_adc_diag_t trip = g_trip_adc;
  const float zero[3] = {g_zero[0], g_zero[1], g_zero[2]};
  const float trip_zero[3] = {g_trip_zero[0], g_trip_zero[1], g_trip_zero[2]};
  const unsigned int zero_valid = g_zero_valid ? 1u : 0u;
  const uint32_t outputs = g_trip_outputs, fault = g_trip_fault;
  const unsigned int align_stage = g_trip_align_stage;
  const float align_move = g_trip_align_move;
  irq_unlock(mask);
  char line[256];
  (void)snprintf(line, sizeof(line),
      "CURRENT_LIVE sample=%lu us=%lu raw=%u,%u,%u,%u zero_mc=%ld,%ld,%ld valid=%u sync=%u bad=%lu\r\n",
      (unsigned long)live.sample, (unsigned long)live.time_us,
      live.raw[0], live.raw[1], live.raw[2], live.raw[3],
      (long)(zero[0] * 1000.0f), (long)(zero[1] * 1000.0f), (long)(zero[2] * 1000.0f),
      zero_valid, (unsigned int)live.synchronized, (unsigned long)live.bad);
  tx(line);
  (void)snprintf(line, sizeof(line),
      "CURRENT_TRIP sample=%lu us=%lu raw=%u,%u,%u,%u zero_mc=%ld,%ld,%ld sync=%u bad=%lu outputs=%lu fault=%lu align_stage=%u align_move_mrad=%ld\r\n",
      (unsigned long)trip.sample, (unsigned long)trip.time_us,
      trip.raw[0], trip.raw[1], trip.raw[2], trip.raw[3],
      (long)(trip_zero[0] * 1000.0f), (long)(trip_zero[1] * 1000.0f), (long)(trip_zero[2] * 1000.0f),
      (unsigned int)trip.synchronized, (unsigned long)trip.bad, (unsigned long)outputs,
      (unsigned long)fault, align_stage, (long)(align_move * 1000.0f));
  tx(line);
  tx("OK CURRENT_DIAG_OUTPUT_OFF\r\n");
}

static void encoder_capture(void)
{
  if ((g_gate.mode != BENCH_OFF && g_gate.mode != BENCH_FAULT) ||
      !g_hw_ready || g_preparing || g_tracing || g_self_left ||
      LL_TIM_IsEnabledAllOutputs(TIM1) ||
      !LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin)) {
    tx("ERR ENC_CAPTURE_REQUIRES_OUTPUT_OFF\r\n"); return;
  }
  const uint32_t mask = irq_lock();
  encoder_capture_start(false);
  irq_unlock(mask);
  tx("OK ENC_CAPTURE_OUTPUT_OFF\r\n");
}

static void encoder_capture_get(unsigned int first)
{
  if (g_enc_capture_running || bench_gate_output_allowed(&g_gate) ||
      g_preparing || g_tracing || g_self_left || LL_TIM_IsEnabledAllOutputs(TIM1) ||
      (TIM1->CCER & (LL_TIM_CHANNEL_CH1 | LL_TIM_CHANNEL_CH1N |
                     LL_TIM_CHANNEL_CH2 | LL_TIM_CHANNEL_CH2N |
                     LL_TIM_CHANNEL_CH3 | LL_TIM_CHANNEL_CH3N)) != 0u) {
    tx("ERR ENC_CAPTURE_GET_REQUIRES_FROZEN_OUTPUT_OFF\r\n"); return;
  }
  /* Frozen data cannot change in an IRQ; serial commands run in foreground. */
  const unsigned int count = g_enc_capture_count;
  if (first >= count && !(first == 0u && count == 0u)) {
    tx("ERR ENC_CAPTURE_GET_INDEX\r\n"); return;
  }
  const unsigned int points = count - first < ENC_CAPTURE_BATCH ? count - first : ENC_CAPTURE_BATCH;
  const unsigned int oldest = count == ENC_CAPTURE_POINTS ? g_enc_capture_write : 0u;
  char line[128];
  (void)snprintf(line, sizeof(line),
      "ENC_CAPTURE count=%u total=%lu rolling=%u start=%u points=%u\r\n",
      count, (unsigned long)g_enc_capture_total, (unsigned int)g_enc_capture_rolling, first, points);
  tx(line);
  for (unsigned int i = first; i < first + points; ++i) {
    const unsigned int index = (oldest + i) & (ENC_CAPTURE_POINTS - 1u);
    (void)snprintf(line, sizeof(line), "ENC_SAMPLE index=%u us=%lu angle=%u diag=%04X\r\n",
        i, (unsigned long)g_enc_capture[index].time_us,
        (unsigned int)g_enc_capture[index].angle, (unsigned int)g_enc_capture[index].diagnostics);
    tx(line);
  }
  tx("OK ENC_CAPTURE_GET_OUTPUT_OFF\r\n");
}

static bool parse_integer(const char *s, long *number, const char **end)
{
  bool negative = false;
  long value = 0;
  if (*s == '-' || *s == '+') { negative = *s == '-'; s++; }
  const char *digits = s;
  while (*s >= '0' && *s <= '9') {
    if (value > 100000L) { *end = s; return false; }
    value = value * 10L + (long)(*s++ - '0');
  }
  *end = s;
  *number = negative ? -value : value;
  return s != digits;
}

static void command(char *line)
{
  if (strcmp(line, "TIMING_DIAG") == 0) { timing_diagnostics(); return; }
  if (strcmp(line, "STATUS") == 0) { status(); return; }
  if (strcmp(line, "ARM_DIAG") == 0) { arm_diagnostics(); return; }
  if (strcmp(line, "DRV_DIAG") == 0) { driver_diagnostics(); return; }
  if (strcmp(line, "DRV_POLL") == 0) { driver_poll_diagnostics(); return; }
  if (strcmp(line, "DRV_LIVE") == 0) { driver_live(); return; }
  if (strcmp(line, "DRV_TRACE MOTOR_DISCONNECTED") == 0) { driver_trace(); return; }
  if (strncmp(line, "DRV_TRACE_GET ", 14u) == 0) {
    long index = 0;
    const char *end = NULL;
    if (!parse_integer(line + 14, &index, &end) || *end || index < 0 ||
        index >= (long)BENCH_DRV_TRACE_POINTS) {
      tx("ERR DRV_TRACE_GET_INDEX\r\n"); return;
    }
    driver_trace_get((unsigned int)index); return;
  }
  if (strcmp(line, "ENC_DIAG") == 0) { encoder_diagnostics(false); return; }
  if (strcmp(line, "ENC_CLEAR") == 0) { encoder_diagnostics(true); return; }
  if (strcmp(line, "ENC_FIELD") == 0) { encoder_field(); return; }
  if (strcmp(line, "CURRENT_DIAG") == 0) { current_diagnostics(); return; }
  if (strcmp(line, "ENC_CAPTURE") == 0) { encoder_capture(); return; }
  if (strncmp(line, "ENC_CAPTURE_GET ", 16u) == 0) {
    long index = 0;
    const char *end = NULL;
    if (!parse_integer(line + 16, &index, &end) || *end || index < 0 ||
        index >= (long)ENC_CAPTURE_POINTS) {
      tx("ERR ENC_CAPTURE_GET_INDEX\r\n"); return;
    }
    encoder_capture_get((unsigned int)index); return;
  }
  if (strncmp(line, "PING ", 5u) == 0) {
    char response[sizeof(g_line) + 2u];
    (void)snprintf(response, sizeof(response), "PONG %s\r\n", line + 5);
    tx(response); return;
  }
  if (strcmp(line, "HELP") == 0) {
    tx("COMMANDS STATUS ARM_DIAG DRV_DIAG DRV_POLL DRV_LIVE DRV_TRACE MOTOR_DISCONNECTED DRV_TRACE_GET <index> ENC_DIAG ENC_CLEAR ENC_FIELD CURRENT_DIAG ENC_CAPTURE ENC_CAPTURE_GET <index> PING <nonce> STOP CLEAR PREPARE ALIGN KEEPALIVE IQ <signed_mA> <ms> HAPTIC <0..7> <ms> SELFTEST BREAKTEST FAULTTEST\r\n");
    return;
  }
  if (strcmp(line, "STOP") == 0) {
    uint32_t mask = irq_lock();
    bench_hw_off();
    bench_gate_stop(&g_gate);
    g_enc_capture_running = false;
    g_haptic_running = false;
    g_haptic_iq = 0.0f;
    g_preparing = false;
    g_tracing = false;
    g_drv_ready = false;
    g_zero_valid = false;
    gl30_foc_force_zero(&g_foc);
    irq_unlock(mask);
    tx("OK STOP\r\n");
    return;
  }
  if (strcmp(line, "KEEPALIVE") == 0) {
    uint32_t mask = irq_lock();
    if (bench_gate_output_allowed(&g_gate)) { g_gate.lease_us = bench_now_us(); }
    irq_unlock(mask);
    return;
  }
  if (strcmp(line, "CLEAR") == 0) {
    if (bench_hw_button() || bench_gate_output_allowed(&g_gate) || g_preparing) {
      tx("ERR CLEAR_RELEASE_BUTTON_AND_STOP\r\n"); return;
    }
    uint32_t mask = irq_lock();
    bench_hw_off();
    bench_gate_init(&g_gate);
    /* Keep the cache readable, but allow the next fault episode to replace it. */
    g_trip_captured = false;
    g_enc_capture_running = false;
    g_haptic_running = false;
    g_haptic_iq = 0.0f;
    gl30_foc_force_zero(&g_foc);
    g_zero_valid = false;
    g_drv_ready = false;
    irq_unlock(mask);
    tx("OK CLEAR_REQUIRES_PREPARE_AND_ALIGNMENT\r\n");
    return;
  }
  if (strcmp(line, "SELFTEST") == 0) {
    if (g_gate.mode != BENCH_OFF || g_preparing || g_self_left) {
      tx("ERR SELFTEST_REQUIRES_OFF\r\n"); return;
    }
    uint32_t mask = irq_lock();
    bench_hw_off();
    gl30_foc_init(&g_self_foc);
    self_timing_reset();
    g_self_fail = 0u;
    g_self_max = 0u;
    g_self_left = BENCH_PWM_HZ;
    g_self_reported = false;
    irq_unlock(mask);
    tx("OK SELFTEST_NO_OUTPUT\r\n");
    return;
  }
  if (strcmp(line, "FAULTTEST") == 0) {
    if (g_gate.mode != BENCH_OFF || g_preparing) { tx("ERR FAULTTEST_REQUIRES_OFF\r\n"); return; }
    uint32_t mask = irq_lock();
    latch(BENCH_FAULT_TEST);
    irq_unlock(mask);
    tx("OK FAULT_LATCHED_NO_OUTPUT\r\n"); return;
  }
  if (strcmp(line, "BREAKTEST") == 0) {
    /* This is a bare-NUCLEO self-test, not an EVM nFAULT stimulus.
     * These guards reject a powered/awake EVM; physical disconnection is
     * still required and cannot be established from ADC voltage alone. */
    if (g_gate.mode != BENCH_OFF || g_preparing || g_drv_ready ||
        !isfinite(g_vm) || g_vm >= 1.0f ||
        LL_GPIO_IsOutputPinSet(DRV_NSLEEP_GPIO_Port, DRV_NSLEEP_Pin)) {
      tx("ERR BREAKTEST_BARE_NUCLEO_DISCONNECT_EVM_POWER_OFF\r\n"); return;
    }
    tx(bench_hw_break_test() ? "OK BREAKTEST_INTERNAL_PULL_NO_OUTPUT\r\n" : "ERR BREAKTEST\r\n");
    return;
  }
  if (strcmp(line, "PREPARE") == 0) {
    if (g_gate.mode != BENCH_OFF || g_gate.faults || g_preparing || g_self_left ||
        !g_hw_ready || !g_rate_ok || g_self_fail || bench_hw_button() ||
        !isfinite(g_vm) || g_vm < VM_MIN_V || g_vm > VM_MAX_V) {
      tx("ERR PREPARE_NEEDS_OFF_SELFTEST_AND_VM9_15V\r\n"); return;
    }
    /* The ADC IRQ must preserve six-input-low idle throughout the wake/SPI
     * sequence, not reassert DRVOFF while the driver is being checked. */
    uint32_t mask = irq_lock();
    current_capture_start();
    g_preparing = true;
    g_drv_ready = false;
    g_zero_valid = false;
    g_zero_count = 0u;
    g_prepare_us = bench_now_us();
    irq_unlock(mask);
    /* Prevent the encoder ISR from interleaving the explicit clear-error pipeline. */
    NVIC_DisableIRQ(TIM6_DAC_IRQn);
    bench_hw_encoder_clear();
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
    const bool configured = bench_hw_driver_configure();
    g_drv_checked_us = bench_now_us();
    if (!configured) {
      mask = irq_lock();
      bench_hw_off();
      g_preparing = false;
      irq_unlock(mask);
      bench_drv_diag_t diag;
      char report[96];
      bench_hw_driver_diagnostics(&diag);
      report_driver_diagnostics(&diag);
      (void)snprintf(report, sizeof(report), "ERR DRV_CONFIG stage=%s reason=%s\r\n",
          drv_stage_name(diag.stage), drv_reason_name(diag.reason));
      tx(report);
      return;
    }
    mask = irq_lock();
    if (g_gate.faults || !g_preparing) {
      bench_hw_off();
      g_drv_ready = false;
      g_preparing = false;
      irq_unlock(mask);
      tx("ERR PREPARE_ABORTED\r\n"); return;
    }
    for (unsigned int i = 0; i < 3u; ++i) {
      g_zero_sum[i] = 0u; g_zero_min[i] = 4095u; g_zero_max[i] = 0u;
    }
    g_zero_count = 0u;
    g_zero_adc_bad_start = g_adc_bad;
    g_zero_sync_failed = false;
    g_zero_error_pending = false;
    g_zero_valid = false;
    g_preparing = true;
    g_prepare_us = bench_now_us();
    /* Publish readiness only after the calibration accumulator is reset. */
    g_drv_ready = true;
    irq_unlock(mask);
    tx("OK PREPARE_STARTED_OUTPUT_OFF\r\n"); return;
  }
  if (strcmp(line, "ALIGN") == 0) {
    uint32_t mask;
    if (!output_start_lock(&mask)) {
      tx("ERR OUTPUT_START_TIMING_WINDOW\r\n"); return;
    }
    g_gate.health = health(bench_now_us());
    bool ok = bench_gate_align(&g_gate, bench_now_us());
    if (ok) {
      gl30_foc_force_zero(&g_foc);
      g_align_stage = 0u;
      g_align_current_sum = 0.0f;
      g_align_voltage_sum = 0.0f;
      g_align_voltage = 0.0f;
      g_applied_field = 0.0f;
      g_align_current_count = 0u;
      g_align_origin = g_foc.theta_unwrapped_rad;
      g_align_forward = 0.0f;
      g_trip_velocity = 0.0f;
      g_trip_angle = 0.0f;
      g_trip_field = 0.0f;
      g_trip_encoder = 0u;
      g_trip_adc = (bench_adc_diag_t){0};
      g_trip_outputs = 0u;
      g_trip_captured = false;
      g_trip_fault = 0u;
      g_trip_align_stage = 0u;
      g_trip_align_move = 0.0f;
      for (unsigned int i = 0u; i < 3u; ++i) { g_trip_zero[i] = 0.0f; }
      current_capture_start();
      encoder_capture_start(true);
      if (!bench_hw_arm()) { latch(BENCH_FAULT_BREAK); ok = false; }
    }
    irq_unlock(mask);
    tx(ok ? "OK ALIGN_KEEPALIVE\r\n" : "ERR ALIGN_NOT_READY\r\n"); return;
  }
  if (strncmp(line, "IQ ", 3u) == 0) {
    long ma = 0, ms = 0;
    const char *end = NULL;
    bool parsed = parse_integer(line + 3, &ma, &end) && *end == ' ';
    if (parsed) { parsed = parse_integer(end + 1, &ms, &end) && *end == '\0'; }
    if (!parsed || ma < -100 || ma > 100 || ms < 1 || ms > 2000) {
      tx("ERR IQ_RANGE_MA_100_MS_2000\r\n"); return;
    }
    uint32_t mask;
    if (!output_start_lock(&mask)) {
      tx("ERR OUTPUT_START_TIMING_WINDOW\r\n"); return;
    }
    g_gate.health = health(bench_now_us());
    bool ok = bench_gate_pulse(&g_gate, bench_now_us(), (int32_t)ma, (uint32_t)ms);
    if (ok) {
      gl30_foc_force_zero(&g_foc);
      current_capture_start();
      encoder_capture_start(true);
      if (!bench_hw_arm()) { latch(BENCH_FAULT_BREAK); ok = false; }
    }
    irq_unlock(mask);
    tx(ok ? "OK IQ_KEEPALIVE\r\n" : "ERR IQ_NEEDS_ALIGNMENT\r\n"); return;
  }
  if (strncmp(line, "HAPTIC ", 7u) == 0) {
    long kind = 0, ms = 0;
    const char *end = NULL;
    bool parsed = parse_integer(line + 7, &kind, &end) && *end == ' ';
    if (parsed) { parsed = parse_integer(end + 1, &ms, &end) && *end == '\0'; }
    if (!parsed || kind < 0 || kind > 7 || ms < 1 || ms > 10000) {
      tx("ERR HAPTIC_RANGE_KIND_0_7_MS_10000\r\n"); return;
    }
    if (g_gate.mode != BENCH_READY || g_haptic_running) {
      tx("ERR HAPTIC_NEEDS_ALIGNMENT\r\n"); return;
    }
    const float origin = g_foc.theta_unwrapped_rad;
    if (!bench_haptic_configure(&g_haptic_foc, (bench_haptic_kind_t)kind, origin)) {
      tx("ERR HAPTIC_INVALID_PROFILE\r\n"); return;
    }
    uint32_t mask;
    if (!output_start_lock(&mask)) {
      tx("ERR OUTPUT_START_TIMING_WINDOW\r\n"); return;
    }
    const uint32_t now = bench_now_us();
    g_gate.health = health(now);
    bool ok = bench_gate_haptic(&g_gate, now, (uint32_t)ms);
    if (ok) {
      gl30_foc_force_zero(&g_foc);
      g_haptic_kind = (bench_haptic_kind_t)kind;
      g_haptic_iq = 0.0f;
      g_haptic_us = now;
      g_haptic_running = true;
      current_capture_start();
      encoder_capture_start(true);
      if (!bench_hw_arm()) { latch(BENCH_FAULT_BREAK); ok = false; }
    }
    irq_unlock(mask);
    tx(ok ? "OK HAPTIC_KEEPALIVE\r\n" : "ERR HAPTIC_NEEDS_ALIGNMENT\r\n"); return;
  }
  tx("ERR UNKNOWN_COMMAND\r\n");
}

void Bench_UartIRQ(void)
{
  if (LL_USART_IsActiveFlag_ORE(USART2) || LL_USART_IsActiveFlag_FE(USART2) || LL_USART_IsActiveFlag_NE(USART2)) {
    LL_USART_ClearFlag_ORE(USART2); LL_USART_ClearFlag_FE(USART2); LL_USART_ClearFlag_NE(USART2);
    g_uart_errors++;
    g_rx_corrupt = true;
    if (bench_gate_output_allowed(&g_gate)) { latch(BENCH_FAULT_UART); }
  }
  if (LL_USART_IsActiveFlag_RXNE(USART2)) {
    uint8_t ch = LL_USART_ReceiveData8(USART2);
    uint16_t next = (uint16_t)((g_rx_write + 1u) & 255u);
    if (next == g_rx_read) {
      g_uart_errors++;
      g_rx_corrupt = true;
      if (bench_gate_output_allowed(&g_gate)) { latch(BENCH_FAULT_UART); }
    } else { g_rx[g_rx_write] = ch; g_rx_write = next; }
  }
  if (LL_USART_IsEnabledIT_TXE(USART2) && LL_USART_IsActiveFlag_TXE(USART2)) {
    if (g_tx_read == g_tx_write) { LL_USART_DisableIT_TXE(USART2); }
    else {
      LL_USART_TransmitData8(USART2, g_tx[g_tx_read]);
      g_tx_read = (uint16_t)((g_tx_read + 1u) & 2047u);
    }
  }
}

void Bench_EncoderIRQ(void)
{
  g_enc_irq_count++;
  if (!g_hw_ready) { return; }
  uint16_t angle = 0u, diag = 0u;
  const bool valid = bench_hw_encoder_read(&angle, &diag);
  const uint32_t sampled_us = bench_now_us();
  const uint8_t next = g_encoder_observer_index ^ 1u;
  if (valid) {
    g_encoder_observer[next] = g_encoder_observer[g_encoder_observer_index];
    gl30_foc_observer_tick_4k(&g_encoder_observer[next],
        (float)angle * (TWO_PI / 16384.0f), true);
  }
  uint32_t mask = irq_lock();
  g_enc_valid = valid;
  g_enc_diag = diag;
  if (valid) {
    g_enc_angle = angle;
    g_enc_us = sampled_us;
    g_encoder_observer_index = next;
    g_enc_seq++;
  } else { g_encoder_errors++; }
  encoder_capture_record(sampled_us, valid ? angle : 0xffffu, diag);
  irq_unlock(mask);
}

static void selftest_tick(void)
{
  if (!g_self_left) { return; }
  uint32_t started = DWT->CYCCNT;
  const uint32_t kind = g_self_left % 3u;
  const float angle = (float)((g_self_left / 3u) & 1023u) * (TWO_PI / 1024.0f);
  gl30_foc_observer_tick_4k(&g_self_foc, angle, true);
  /* IQ profiling includes the calibrated observer overwrite and fresh PI
   * start at both directions/extreme zero positions, not just an ideal angle. */
  if (kind == 0u) {
    const uint32_t block = g_self_left / 3072u;
    const float direction = (block & 1u) ? -1.0f : 1.0f;
    const float zero = (block & 2u) ? 3.0f : -3.0f;
    g_self_foc.theta_elec_rad = wrap_angle(direction * 7.0f * (g_self_foc.theta_mech_rad - zero));
    g_self_foc.integrator_d_v = 0.0f;
    g_self_foc.integrator_q_v = 0.0f;
  } else {
    g_self_foc.theta_elec_rad = angle > 0.5f * TWO_PI ? angle - TWO_PI : angle;
  }
  const uint32_t observed = DWT->CYCCNT;
  /* Exercise all electrical quadrants in each real FOC mode, outputs hard off. */
  g_self_foc.i_d_ref_a = kind == 1u ? ALIGN_CURRENT_A : 0.0f;
  g_self_foc.i_q_ref_a = kind == 0u ? ((g_self_left & 1u) ? 0.1f : -0.1f) : 0.0f;
  gl30_foc_output_t out = kind == 2u ?
      gl30_foc_voltage_tick(&g_self_foc, -0.3766612f, 0.3222578f, 0.0722599f,
          12.0f, 1.0f / (float)BENCH_PWM_HZ, ALIGN_VOLTAGE_CAP_V, 0.4f, 0.0f) :
      gl30_foc_current_tick(&g_self_foc, 0.01f, -0.006f, -0.004f, 12.0f,
          1.0f / (float)BENCH_PWM_HZ, kind == 1u ? ALIGN_VOLTAGE_CAP_V : VOLTAGE_CAP_V);
  const uint32_t finished = DWT->CYCCNT;
  const uint32_t cycles = finished - started;
  const uint32_t window = LL_TIM_GetCounter(TIM1);
  volatile bench_self_timing_t *profile = &g_self_timing[kind];
  profile->samples++;
  if (observed - started > profile->observer_max) { profile->observer_max = observed - started; }
  if (finished - observed > profile->foc_max) { profile->foc_max = finished - observed; }
  if (cycles > profile->total_max) { profile->total_max = cycles; }
  if (window < profile->window_min) { profile->window_min = window; }
  if (window < g_window_min) { g_window_min = window; }
  if (cycles > g_self_max) { g_self_max = cycles; }
  if (!out.valid || !isfinite(out.duty_a) || out.duty_a < 0.3f || out.duty_a > 0.7f ||
      LL_TIM_GetDirection(TIM1) != LL_TIM_COUNTERDIRECTION_DOWN || window < 320u ||
      LL_TIM_IsEnabledAllOutputs(TIM1) || !LL_GPIO_IsOutputPinSet(DRV_OFF_GPIO_Port, DRV_OFF_Pin)) {
    g_self_fail++;
    bench_hw_off();
  }
  g_self_left--;
}

static void alignment_tick(uint32_t now)
{
  const uint32_t elapsed = now - g_gate.started_us;
  float field = 0.0f;
  if (elapsed < 1000000u) {
    if (elapsed > 900000u) {
      g_align_current_sum += g_foc.i_d_a;
      g_align_voltage_sum += g_foc.v_d_v;
      g_align_current_count++;
    }
    g_align_voltage = ALIGN_HOLD_VOLTAGE_V *
        (elapsed < 800000u ? (float)elapsed / 800000.0f : 1.0f);
  } else if (elapsed < 4000000u) {
    if (g_align_stage == 0u) {
      const float measured = g_align_current_count ? g_align_current_sum / (float)g_align_current_count : 0.0f;
      if (!isfinite(measured) || measured < 0.7f * ALIGN_CURRENT_A || measured > 1.3f * ALIGN_CURRENT_A) {
        latch(BENCH_FAULT_ALIGN); return;
      }
      g_align_voltage = g_align_voltage_sum / (float)g_align_current_count;
      if (!isfinite(g_align_voltage) || g_align_voltage < 0.05f ||
          g_align_voltage > ALIGN_VOLTAGE_CAP_V) {
        latch(BENCH_FAULT_ALIGN); return;
      }
      g_align_origin = g_foc.theta_unwrapped_rad;
      g_align_stage = 1u;
    }
    const float progress = (float)(elapsed - 1000000u) / 3000000.0f;
    /* Zero field velocity at both ends avoids an abrupt start or reversal. */
    field = TWO_PI * progress * progress * (3.0f - 2.0f * progress);
  } else if (elapsed < 4500000u) {
    field = TWO_PI;
  } else {
    g_align_forward = g_foc.theta_unwrapped_rad - g_align_origin;
    const float travel = fabsf(g_align_forward);
    const float expected = TWO_PI / 7.0f;
    const bool ok = g_align_stage == 1u && travel > 0.8f * expected && travel < 1.2f * expected;
    if (ok) {
      g_direction = g_align_forward > 0.0f ? 1.0f : -1.0f;
      /* After one full electrical revolution the final field is again zero. */
      g_zero_angle = g_foc.theta_mech_rad;
      g_align_stage = 2u;
    }
    if (!ok) { latch(BENCH_FAULT_ALIGN); return; }
    bench_hw_idle();
    gl30_foc_force_zero(&g_foc);
    bench_gate_alignment_done(&g_gate, true);
    return;
  }
  /* This trajectory generates only [0, 2*pi]; avoid a general remainder
   * operation in every current IRQ while preserving the same field angle. */
  g_foc.theta_elec_rad = field > 0.5f * TWO_PI ? field - TWO_PI : field;
  g_foc.i_d_ref_a = 0.0f;
  g_foc.i_q_ref_a = 0.0f;
}

void Bench_AdcIRQ(void)
{
  const uint32_t started = DWT->CYCCNT;
  const uint32_t now = bench_now_us();
  const bool timing = bench_gate_output_allowed(&g_gate) && !g_trip_captured;
  if (timing) {
    g_loop_timing.sample = g_adc_count + 1u; g_loop_timing.time_us = now;
    g_loop_timing.entry_counter = LL_TIM_GetCounter(TIM1);
    g_loop_timing.mode = g_gate.mode; g_loop_timing.observer_updated = 0u;
    g_loop_timing.stage = 0u; g_loop_timing.reason = 0u;
  }
  const bool synchronized = LL_ADC_IsActiveFlag_JEOS(ADC2) && LL_ADC_IsActiveFlag_JEOS(ADC3);
  LL_ADC_ClearFlag_JEOS(ADC1); LL_ADC_ClearFlag_JEOS(ADC2); LL_ADC_ClearFlag_JEOS(ADC3);
  g_raw[0] = (uint16_t)LL_ADC_INJ_ReadConversionData12(ADC1, LL_ADC_INJ_RANK_1);
  g_raw[1] = (uint16_t)LL_ADC_INJ_ReadConversionData12(ADC2, LL_ADC_INJ_RANK_1);
  g_raw[2] = (uint16_t)LL_ADC_INJ_ReadConversionData12(ADC3, LL_ADC_INJ_RANK_1);
  g_raw[3] = (uint16_t)LL_ADC_INJ_ReadConversionData12(ADC1, LL_ADC_INJ_RANK_2);
  g_adc_count++;
  const uint32_t adc_gap = g_adc_us != 0u ? now - g_adc_us : 0u;
  /* Publish this frame before any early fault snapshots it. */
  g_adc_us = now;
  g_adc_synchronized = synchronized;
  if (!synchronized) {
    g_adc_bad++;
    if (g_preparing && g_drv_ready) {
      g_zero_sync_failed = true;
      g_zero_error_pending = true;
      latch(BENCH_FAULT_ADC);
    } else if (g_zero_valid || bench_gate_output_allowed(&g_gate)) {
      latch(BENCH_FAULT_ADC);
    }
  }
  if (adc_gap > 65u && bench_gate_output_allowed(&g_gate)) {
    deadline_latch(1u, started);
  }
  current_capture_record(now, synchronized);
  g_vm = (float)g_raw[3] * BENCH_ADC_SCALE * BENCH_VM_RATIO;
  g_ia = ((float)g_raw[0] - g_zero[0]) * BENCH_ADC_SCALE / BENCH_CSA_V_PER_A;
  g_ib = ((float)g_raw[1] - g_zero[1]) * BENCH_ADC_SCALE / BENCH_CSA_V_PER_A;
  g_ic = ((float)g_raw[2] - g_zero[2]) * BENCH_ADC_SCALE / BENCH_CSA_V_PER_A;
  if (synchronized && g_preparing && g_drv_ready && !g_zero_sync_failed &&
      g_zero_count < ADC_ZERO_SAMPLES) {
    for (unsigned int i = 0u; i < 3u; ++i) {
      g_zero_sum[i] += g_raw[i];
      if (g_raw[i] < g_zero_min[i]) { g_zero_min[i] = g_raw[i]; }
      if (g_raw[i] > g_zero_max[i]) { g_zero_max[i] = g_raw[i]; }
    }
    g_zero_count++;
  }
  if (timing) { g_loop_timing.acquired = DWT->CYCCNT - started; g_loop_timing.stage = 1u; }
  if (!g_hw_ready) { return; }
  /* Consume one complete 4 kHz observation. TIM6 cannot preempt ADC, and its
   * index/angle/time/sequence publication is atomic. Leave synthetic profiles
   * on the other slots so they do not overlap this calibrated-angle copy. */
  if (g_enc_valid && g_enc_seq != g_last_observer_seq) {
    const gl30_foc_state_t *observation = &g_encoder_observer[g_encoder_observer_index];
    g_foc.theta_elec_rad = observation->theta_elec_rad;
    g_foc.theta_mech_rad = observation->theta_mech_rad;
    g_foc.theta_unwrapped_rad = observation->theta_unwrapped_rad;
    g_foc.previous_angle_rad = observation->previous_angle_rad;
    g_foc.velocity_rad_s = observation->velocity_rad_s;
    g_foc.acceleration_rad_s2 = observation->acceleration_rad_s2;
    g_foc.observer_initialized = observation->observer_initialized;
    if (g_gate.calibrated) {
      g_foc.theta_elec_rad = wrap_angle(g_direction * 7.0f * (g_foc.theta_mech_rad - g_zero_angle));
    }
    g_last_observer_seq = g_enc_seq;
    if (timing) { g_loop_timing.observer_updated = 1u; }
  } else {
    selftest_tick();
  }
  if (timing) { g_loop_timing.observed = DWT->CYCCNT - started; g_loop_timing.stage = 2u; }
  bench_gate_tick(&g_gate, now, health(now));
  if (timing) { g_loop_timing.gated = DWT->CYCCNT - started; g_loop_timing.stage = 3u; }
  /* Gate health/lease/timeout faults also need the hardware stop and first frame. */
  if (g_gate.faults != 0u && !g_trip_captured) { latch(g_gate.faults); }
  if (bench_gate_output_allowed(&g_gate)) {
    const float phase_trip = g_gate.mode == BENCH_ALIGNING ? ALIGN_PHASE_TRIP_A : PHASE_TRIP_A;
    const float voltage_cap = g_gate.mode == BENCH_ALIGNING ? ALIGN_VOLTAGE_CAP_V : VOLTAGE_CAP_V;
    if (!synchronized || fabsf(g_ia) > phase_trip || fabsf(g_ib) > phase_trip || fabsf(g_ic) > phase_trip) {
      latch(BENCH_FAULT_CURRENT);
    } else if (fabsf(g_foc.velocity_rad_s) > 8.0f) {
      latch(BENCH_FAULT_SPEED);
    } else {
      if (g_gate.mode == BENCH_ALIGNING) {
        alignment_tick(now);
      } else {
        g_foc.i_d_ref_a = 0.0f;
        g_foc.i_q_ref_a = g_haptic_running ?
            ((uint32_t)(now - g_haptic_us) <= 5000u ? g_haptic_iq : 0.0f) :
            (float)g_gate.iq_ma * 0.001f;
      }
      if (timing) { g_loop_timing.trajectory = DWT->CYCCNT - started; g_loop_timing.stage = 4u; }
      if (bench_gate_output_allowed(&g_gate)) {
        /* Both alignment stages use bounded voltage with Vq=0, rather than
         * forcing measured q current to zero during unknown rotor alignment.
         * The static measured-current qualification and all trips still apply. */
        gl30_foc_output_t out = g_gate.mode == BENCH_ALIGNING ?
            gl30_foc_voltage_tick(&g_foc, g_ia, g_ib, g_ic, g_vm,
                1.0f / (float)BENCH_PWM_HZ, voltage_cap, g_align_voltage, 0.0f) :
            gl30_foc_current_tick(&g_foc, g_ia, g_ib, g_ic, g_vm,
                1.0f / (float)BENCH_PWM_HZ, voltage_cap);
        if (timing) { g_loop_timing.foc = DWT->CYCCNT - started; g_loop_timing.stage = 5u; }
        if (!out.valid || out.overcurrent) { latch(BENCH_FAULT_NUMERIC); }
        else if (!LL_TIM_IsEnabledAllOutputs(TIM1)) { latch(BENCH_FAULT_BREAK); }
        else if (LL_TIM_GetDirection(TIM1) != LL_TIM_COUNTERDIRECTION_DOWN || LL_TIM_GetCounter(TIM1) < 320u) {
          deadline_latch(2u, started);
        } else {
          g_applied_field = g_foc.theta_elec_rad;
          bench_hw_duty(out.duty_a, out.duty_b, out.duty_c);
        }
      }
    }
  }
  if (!bench_gate_output_allowed(&g_gate)) {
    if (g_enc_capture_rolling) { g_enc_capture_running = false; }
    g_haptic_running = false;
    g_haptic_iq = 0.0f;
    if (g_gate.faults == 0u && g_gate.mode != BENCH_FAULT &&
        (g_preparing || g_tracing || g_gate.mode == BENCH_PREPARED || g_gate.mode == BENCH_READY)) {
      /* DRVOFF can assert nFAULT and invalidate the CSA zero. Keep all six
       * inputs physically low instead; this never releases a hard stop. */
      bench_hw_idle();
    } else {
      bench_hw_off();
      if (g_gate.mode == BENCH_FAULT || g_gate.faults) {
        g_preparing = false;
        g_tracing = false;
        g_drv_ready = false;
        g_zero_valid = false;
      }
    }
    gl30_foc_force_zero(&g_foc);
  }
  const uint32_t cycles = DWT->CYCCNT - started;
  if (timing) { g_loop_timing.total = cycles; }
  if (cycles > g_isr_max) { g_isr_max = cycles; }
  if (cycles > ISR_BUDGET_CYCLES) {
    if (bench_gate_output_allowed(&g_gate)) { deadline_latch(3u, started); }
    else { g_deadlines++; }
  }
}

void Bench_BreakIRQ(void)
{
  latch(BENCH_FAULT_BREAK);
  LL_TIM_ClearFlag_BRK(TIM1);
}

void Bench_Fatal(void)
{
  __disable_irq();
  bench_hw_off();
  for (;;) { } /* IWDG resets after init; output remains disabled before then. */
}

void Bench_Init(void)
{
  g_rx_corrupt = false;
  g_trip_adc = (bench_adc_diag_t){0};
  g_trip_outputs = 0u;
  g_trip_captured = false;
  g_trip_fault = 0u;
  g_trip_align_stage = 0u;
  g_trip_align_move = 0.0f;
  g_trip_velocity = 0.0f;
  g_trip_angle = 0.0f;
  g_trip_field = 0.0f;
  g_trip_encoder = 0u;
  for (unsigned int i = 0u; i < 3u; ++i) { g_trip_zero[i] = 0.0f; }
  g_enc_capture_running = false;
  g_enc_capture_rolling = false;
  g_enc_capture_count = 0u;
  g_enc_capture_write = 0u;
  g_enc_capture_total = 0u;
  g_reset_flags = RCC->CSR;
  LL_RCC_ClearResetFlags();
  bench_gate_init(&g_gate);
  gl30_foc_init(&g_foc);
  gl30_foc_init(&g_self_foc);
  gl30_foc_init(&g_encoder_observer[0]);
  gl30_foc_init(&g_encoder_observer[1]);
  g_encoder_observer_index = 0u;
  self_timing_reset();
  g_loop_timing = (bench_loop_timing_t){0};
  g_self_left = BENCH_PWM_HZ;
  g_hw_ready = bench_hw_init();
  LL_USART_EnableIT_RXNE(USART2);
  LL_USART_EnableIT_ERROR(USART2);
  bench_hw_watchdog_start();
  if (!g_hw_ready) { latch(BENCH_FAULT_ADC); }
  tx("BOOT NUCLEO_G474RE_FOC TI_DRV8316REVM AS5048A 160MHZ PWM20K ENC4K OUTPUT_OFF FW=20260908_HAPTIC26_OFFLINE_UNQUALIFIED\r\n");
}

static void haptic_tick(uint32_t now)
{
  if (!g_haptic_running || (uint32_t)(now - g_haptic_us) < 500u) { return; }
  uint32_t mask = irq_lock();
  const float position = g_foc.theta_unwrapped_rad;
  const float velocity = g_foc.velocity_rad_s;
  const float acceleration = g_foc.acceleration_rad_s2;
  const float direction = g_direction;
  irq_unlock(mask);
  const float iq = bench_haptic_sample(&g_haptic_foc, position, velocity, acceleration, direction);
  mask = irq_lock();
  if (g_haptic_running && g_gate.mode == BENCH_ACTIVE && bench_gate_output_allowed(&g_gate)) {
    if (!isfinite(iq) || fabsf(iq) > 0.10001f) {
      latch(BENCH_FAULT_NUMERIC);
    } else {
      g_haptic_iq = iq;
      g_haptic_us = now;
    }
  }
  irq_unlock(mask);
}

void Bench_Loop(void)
{
  static uint32_t last_slow, last_rate, rate_adc, rate_enc, fed_adc, last_fault;
  static bench_mode_t previous_mode;
  uint32_t now = bench_now_us();
  if (g_adc_count != fed_adc) { fed_adc = g_adc_count; bench_hw_watchdog_feed(); }
  /* A bounded amount per loop prevents RX flooding from starving health work. */
  for (unsigned int n = 0u; n < 32u; ++n) {
    if (g_rx_corrupt) {
      /* Lost/corrupt bytes must not splice a malformed line into a command.
       * Purge queued bytes atomically, then resync on a new line boundary. */
      const uint32_t mask = irq_lock();
      g_rx_read = g_rx_write;
      g_rx_corrupt = false;
      g_line_len = 0u;
      g_discard_line = true;
      irq_unlock(mask);
      break;
    }
    if (g_rx_read == g_rx_write) { break; }
    const uint8_t ch = g_rx[g_rx_read];
    g_rx_read = (uint16_t)((g_rx_read + 1u) & 255u);
    /* The RX IRQ can report corruption while this byte is being dequeued. */
    if (g_rx_corrupt) { continue; }
    if (ch == '\r') { continue; }
    if (ch == '\n') {
      if (g_discard_line) { tx("ERR LINE_TOO_LONG_OR_BINARY\r\n"); }
      else if (g_line_len) { g_line[g_line_len] = '\0'; command(g_line); }
      g_line_len = 0u; g_discard_line = false;
    } else if (ch < 32u || ch > 126u || g_line_len >= sizeof(g_line) - 1u) {
      g_discard_line = true;
    } else if (!g_discard_line) { g_line[g_line_len++] = (char)ch; }
  }
  now = bench_now_us();
  haptic_tick(now);
  if ((uint32_t)(now - last_rate) >= 1000000u) {
    const uint32_t dt = now - last_rate;
    const uint32_t count = g_adc_count - rate_adc;
    const uint32_t enc_count = g_enc_irq_count - rate_enc;
    g_rate_ok = count > dt / 51u && count < dt / 49u &&
                enc_count > dt / 255u && enc_count < dt / 245u;
    rate_adc = g_adc_count; rate_enc = g_enc_irq_count; last_rate = now;
    LL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
  }
  if ((uint32_t)(now - last_slow) >= 10000u) {
    last_slow = now;
    if (g_drv_ready) {
      g_drv_ready = bench_hw_driver_status(&g_drv_status);
      g_drv_checked_us = bench_now_us();
    }
    if (g_preparing && g_zero_count >= ADC_ZERO_SAMPLES && (uint32_t)(now - g_prepare_us) > 30000u) {
      bool ok = true;
      float mean[3];
      for (unsigned int i = 0u; i < 3u; ++i) {
        mean[i] = (float)g_zero_sum[i] / (float)ADC_ZERO_SAMPLES;
        ok = ok && mean[i] > 1613.0f && mean[i] < 2110.0f &&
             (uint32_t)(g_zero_max[i] - g_zero_min[i]) <= 60u;
      }
      uint32_t mask = irq_lock();
      /* An ADC IRQ may have aborted the attempt while means were computed.
       * Reject new errors, not the lifetime total from earlier attempts. */
      ok = ok && g_preparing && g_drv_ready && !g_gate.faults &&
           !g_zero_sync_failed && g_zero_count == ADC_ZERO_SAMPLES &&
           g_adc_bad == g_zero_adc_bad_start;
      g_zero_valid = ok;
      g_preparing = false;
      bool ready = ok && bench_gate_prepare(&g_gate, health(bench_now_us()));
      if (ready) {
        for (unsigned int i = 0u; i < 3u; ++i) { g_zero[i] = mean[i]; }
      } else {
        bench_hw_off();
        g_drv_ready = false;
        g_zero_valid = false;
      }
      irq_unlock(mask);
      tx(ready ? "OK PREPARED_NO_OUTPUT\r\n" : "ERR PREPARE_HEALTH_CHECK\r\n");
    }
    if (g_preparing && (uint32_t)(now - g_prepare_us) > 200000u) {
      uint32_t mask = irq_lock();
      bench_hw_off();
      g_preparing = false; g_zero_valid = false; g_drv_ready = false;
      irq_unlock(mask);
      tx("ERR ADC_ZERO_TIMEOUT\r\n");
    }
  }
  if (g_zero_error_pending) {
    const uint32_t mask = irq_lock();
    g_zero_error_pending = false;
    irq_unlock(mask);
    tx("ERR ADC_ZERO_UNSYNCHRONIZED_REQUIRES_CLEAR_AND_PREPARE\r\n");
  }
  if (g_self_left == 0u && !g_self_reported) {
    g_self_reported = true;
    tx(g_self_fail == 0u && g_self_max < ISR_BUDGET_CYCLES && g_deadlines == 0u ?
       "OK SELFTEST_ALGORITHM_ONLY_OUTPUT_OFF\r\n" : "ERR SELFTEST_OR_TIMING\r\n");
  }
  if (g_gate.faults != last_fault) {
    last_fault = g_gate.faults;
    if (last_fault) { tx("EVENT FAULT_LATCHED\r\n"); }
  }
  if (g_gate.mode != previous_mode) {
    if (g_gate.mode == BENCH_READY && previous_mode == BENCH_ALIGNING) {
      tx("OK ALIGNED_RAM_ONLY\r\n");
    }
    if (g_gate.mode == BENCH_READY && previous_mode == BENCH_ACTIVE) {
      tx("OK PULSE_COMPLETE_OUTPUT_OFF\r\n");
    }
    previous_mode = g_gate.mode;
  }
}
