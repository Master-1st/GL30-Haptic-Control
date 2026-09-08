#ifndef BENCH_HW_H
#define BENCH_HW_H

#include <stdbool.h>
#include <stdint.h>

#define BENCH_PWM_HZ 20000u
#define BENCH_TIMER_ARR 4000u
#define BENCH_ADC_SCALE (3.3f / 4095.0f)
#define BENCH_VM_RATIO ((75000.0f + 6040.0f) / 6040.0f)
#define BENCH_CSA_V_PER_A 0.6f

typedef enum {
  BENCH_DRV_STAGE_NONE = 0,
  BENCH_DRV_STAGE_UNLOCK,
  BENCH_DRV_STAGE_CONFIG_WRITE,
  BENCH_DRV_STAGE_CONFIG_READ,
  BENCH_DRV_STAGE_STATUS_PRE,
  BENCH_DRV_STAGE_CLEAR_WRITE,
  BENCH_DRV_STAGE_CLEAR_READ,
  BENCH_DRV_STAGE_LOCK_WRITE,
  BENCH_DRV_STAGE_LOCK_READ,
  BENCH_DRV_STAGE_STATUS_FINAL,
  BENCH_DRV_STAGE_FINAL_GAIN,
  BENCH_DRV_STAGE_FINAL_BUCK,
  BENCH_DRV_STAGE_READY,
} bench_drv_stage_t;

typedef enum {
  BENCH_DRV_REASON_NONE = 0,
  BENCH_DRV_REASON_TRANSPORT,
  BENCH_DRV_REASON_READBACK,
  BENCH_DRV_REASON_STATUS_FLAGS,
  BENCH_DRV_REASON_NFAULT,
  BENCH_DRV_REASON_GUARD,
} bench_drv_reason_t;

typedef struct bench_adc_diag {
  uint32_t sample;
  uint32_t time_us;
  uint32_t bad;
  uint16_t raw[4];
  uint8_t synchronized;
} bench_adc_diag_t;

typedef struct {
  uint32_t time_us;
  uint32_t gpioa_idr, gpioa_odr;
  uint32_t gpiob_idr, gpiob_odr;
  uint32_t gpioc_idr, gpioc_odr;
  uint32_t pb12_pupdr;
  uint32_t tim1_ccer;
  bench_adc_diag_t adc;
  uint8_t valid;
  uint8_t nfault, nsleep, drvoff, moe;
} bench_drv_state_t;

typedef struct {
  uint16_t tx;
  uint16_t rx;
  uint32_t raw_status;
  uint8_t status_read_mask;
  bool transfer_ok;
  /* exact-readback-pass mask for 6 config registers */
  uint8_t config_read_mask;
  uint16_t status_rx[3];
  uint32_t status_us[3];
  uint32_t normalized_status;
  bench_drv_stage_t stage;
  bench_drv_reason_t reason;
  uint8_t expected;
  uint8_t relock_attempted;
  uint8_t relock_ok;
  uint16_t relock_rx;
  bench_drv_state_t pre_cleanup;
  bench_drv_state_t post_cleanup;
} bench_drv_diag_t;

typedef struct {
  bench_drv_diag_t diag;
  bench_drv_state_t entry;
  bench_drv_state_t result;
  bool valid;
} bench_drv_poll_diag_t;

#define BENCH_DRV_TRACE_POINTS 9u
typedef enum {
  BENCH_DRV_TRACE_WAKE = 0,
  BENCH_DRV_TRACE_UNLOCK,
  BENCH_DRV_TRACE_CONFIG,
  BENCH_DRV_TRACE_CLEAR,
} bench_drv_trace_phase_t;

typedef struct {
  bench_drv_trace_phase_t phase;
  uint8_t reg, mask;
  uint16_t rx[3];
  uint32_t us[3], normalized_status;
  bench_drv_state_t state;
} bench_drv_trace_point_t;

typedef struct {
  bench_drv_trace_point_t points[BENCH_DRV_TRACE_POINTS];
  bench_drv_diag_t last;
  uint8_t count, first_issue; /* first_issue is one-based; zero means none. */
  uint8_t clear_attempted;
} bench_drv_trace_t;

typedef struct {
  uint32_t stage; /* 0=complete, 1=TX wait, 2=RX timeout, 3=OVR, 4=BSY timeout, 5=DRV quiet timeout */
  uint32_t elapsed_us;
  uint32_t sr;
} bench_spi_error_t;

typedef struct {
  uint32_t sample;
  uint32_t time_us;
  uint16_t raw[3]; /* Previous reply, angle reply, diagnostic reply. */
  uint8_t transfer_mask;
  uint8_t reason; /* 0=valid; 1=transport; 2=angle frame; 3=diag frame; 4=magnetic */
  bench_spi_error_t spi;
} bench_enc_diag_t;

typedef struct {
  uint32_t count;
  uint16_t previous;
  uint16_t error;
  uint8_t transfer_mask;
  bench_spi_error_t spi;
} bench_enc_clear_diag_t;

typedef struct {
  uint32_t time_us;
  uint16_t raw[4], angle, diagnostics, magnitude;
  uint8_t transfer_mask, valid_mask;
  bench_spi_error_t spi;
} bench_enc_field_diag_t;

typedef enum {
  BENCH_ARM_STAGE_NONE = 0,
  BENCH_ARM_STAGE_GUARD,
  BENCH_ARM_STAGE_ENABLE,
  BENCH_ARM_STAGE_READY,
} bench_arm_stage_t;

enum {
  BENCH_ARM_GUARD_NFAULT = 1u << 0,
  BENCH_ARM_GUARD_DRVOFF = 1u << 2,
  BENCH_ARM_GUARD_PINS = 1u << 3,
  BENCH_ARM_GUARD_CHANNELS = 1u << 4,
  BENCH_ARM_GUARD_MOE = 1u << 5,
};

typedef struct {
  uint32_t attempt, time_us;
  bench_arm_stage_t stage;
  uint32_t guard_flags;
  /* Captured before failure cleanup, not reconstructed from the OFF state. */
  uint32_t tim1_sr, tim1_bdtr, tim1_ccer;
  uint32_t gpiob_idr, gpioc_idr, gpioc_odr;
} bench_arm_diag_t;

uint32_t bench_now_us(void);
void bench_delay_us(uint32_t duration);
bool bench_hw_init(void);
void bench_hw_off(void);
void bench_hw_idle(void);
bool bench_hw_arm(void);
/* Cached copy only; caller masks IRQs. STOP/CLEAR do not erase this evidence. */
void bench_hw_arm_diagnostics(bench_arm_diag_t *out);
void bench_hw_duty(float a, float b, float c);
bool bench_hw_button(void);
bool bench_hw_nfault(void);
bool bench_hw_driver_configure(void);
bool bench_hw_driver_status(uint32_t *status);
void bench_hw_driver_diagnostics(bench_drv_diag_t *out);
/* First failed periodic poll; cached only, cleared by a new configure attempt. */
void bench_hw_driver_poll_diagnostics(bench_drv_poll_diag_t *out);
/* Read-only, outputs disabled and already awake; true means all reads completed. */
bool bench_hw_driver_snapshot(bench_drv_diag_t *out);
/* Explicit, motor-disconnected diagnostic only. Never sets driver readiness.
 * True means the bounded sequence completed, NOT that the driver is healthy. */
bool bench_hw_driver_trace(bench_drv_trace_t *out);
bool bench_hw_encoder_read(uint16_t *angle, uint16_t *diagnostics);
void bench_hw_encoder_clear(void);
/* Read-only angle/AGC/magnitude snapshot. Caller parks outputs and masks TIM6. */
bool bench_hw_encoder_field(bench_enc_field_diag_t *out);
/* Caller masks only TIM6 while copying; never format/report in an ISR. */
void bench_hw_encoder_diagnostics(bench_enc_diag_t *first, bench_enc_diag_t *latest,
                                  bench_enc_clear_diag_t *clear);
bool bench_hw_break_test(void);
void bench_hw_watchdog_start(void);
void bench_hw_watchdog_feed(void);

#endif
