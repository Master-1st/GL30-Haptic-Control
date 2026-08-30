#ifndef GL30_SAFETY_SUPERVISOR_H_
#define GL30_SAFETY_SUPERVISOR_H_

#include <stdbool.h>
#include <stdint.h>

typedef enum {
  GL30_STARTUP_POWER_CHECK = 0,
  GL30_STARTUP_SELF_TEST,
  GL30_STARTUP_ENCODER_CHECK,
  GL30_STARTUP_DRIVER_CHECK,
  GL30_STARTUP_ADC_ZERO_CHECK,
  GL30_STARTUP_FOC_ALIGN,
  GL30_STARTUP_READY,
  GL30_STARTUP_ACTIVE,
  GL30_STARTUP_FAULT_LATCHED
} gl30_startup_state_t;

typedef enum {
  GL30_COMM_WAITING = 0,
  GL30_COMM_NORMAL,
  GL30_COMM_WARN,
  GL30_COMM_SAFE_ZERO,
  GL30_COMM_LOST
} gl30_comm_state_t;

enum {
  GL30_FAULT_HARDWARE_BKIN  = 1u << 0,
  GL30_FAULT_ADC_SYNC       = 1u << 1,
  GL30_FAULT_OVERCURRENT    = 1u << 2,
  GL30_FAULT_ENCODER        = 1u << 3,
  GL30_FAULT_COMM_LOST      = 1u << 4,
  GL30_FAULT_FOC_DEADLINE   = 1u << 5,
  GL30_FAULT_DRIVER_SPI     = 1u << 6,
  GL30_FAULT_VBUS           = 1u << 7,
  GL30_FAULT_TEMPERATURE    = 1u << 8,
  GL30_FAULT_STARTUP        = 1u << 9
};

enum {
  GL30_WARNING_COMM_10MS    = 1u << 0,
  GL30_WARNING_VBUS         = 1u << 1,
  GL30_WARNING_TEMPERATURE  = 1u << 2,
  GL30_WARNING_ENCODER      = 1u << 3
};

typedef struct {
  gl30_startup_state_t startup_state;
  gl30_comm_state_t comm_state;
  uint64_t boot_us;
  uint64_t last_valid_command_us;
  uint64_t last_tick_us;
  uint32_t fault_bits;
  uint32_t warning_bits;
  uint32_t unknown_version_count;
  uint32_t unknown_type_count;
  uint32_t bad_crc_count;
  uint32_t bad_length_count;
  uint32_t dropped_frame_count;
  uint32_t applied_command_count;
  uint32_t adc_sync_error_count;
  uint32_t encoder_error_count;
  uint32_t foc_deadline_count;
  bool power_ok;
  bool self_test_ok;
  bool encoder_ok;
  bool driver_ok;
  bool adc_zero_ok;
  bool alignment_ok;
  bool arm_requested;
} gl30_safety_t;

void gl30_safety_init(gl30_safety_t *ctx, uint64_t now_us);
void gl30_safety_set_startup_checks(
    gl30_safety_t *ctx,
    bool power_ok,
    bool self_test_ok,
    bool encoder_ok,
    bool driver_ok,
    bool adc_zero_ok,
    bool alignment_ok);
void gl30_safety_on_valid_command(gl30_safety_t *ctx, uint64_t received_at_us);
void gl30_safety_on_bad_crc(gl30_safety_t *ctx);
void gl30_safety_on_bad_length(gl30_safety_t *ctx);
void gl30_safety_on_unknown_version(gl30_safety_t *ctx);
void gl30_safety_on_unknown_type(gl30_safety_t *ctx);
void gl30_safety_request_arm(gl30_safety_t *ctx);
void gl30_safety_disarm(gl30_safety_t *ctx);
void gl30_safety_latch_fault(gl30_safety_t *ctx, uint32_t fault_bit);
void gl30_safety_set_warning(gl30_safety_t *ctx, uint32_t warning_bit, bool active);
void gl30_safety_tick(gl30_safety_t *ctx, uint64_t now_us);
bool gl30_safety_torque_allowed(const gl30_safety_t *ctx);
bool gl30_safety_fault_latched(const gl30_safety_t *ctx);

#endif
