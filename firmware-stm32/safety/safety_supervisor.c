#include "safety_supervisor.h"

#include <stddef.h>
#include <string.h>

#include "board_config.h"

void gl30_safety_init(gl30_safety_t *ctx, uint64_t now_us) {
  if (ctx == NULL) {
    return;
  }
  memset(ctx, 0, sizeof(*ctx));
  ctx->boot_us = now_us;
  ctx->last_tick_us = now_us;
  ctx->startup_state = GL30_STARTUP_POWER_CHECK;
  ctx->comm_state = GL30_COMM_WAITING;
}

void gl30_safety_set_startup_checks(
    gl30_safety_t *ctx,
    bool power_ok,
    bool self_test_ok,
    bool encoder_ok,
    bool driver_ok,
    bool adc_zero_ok,
    bool alignment_ok) {
  if (ctx == NULL || gl30_safety_fault_latched(ctx)) {
    return;
  }
  ctx->power_ok = power_ok;
  ctx->self_test_ok = self_test_ok;
  ctx->encoder_ok = encoder_ok;
  ctx->driver_ok = driver_ok;
  ctx->adc_zero_ok = adc_zero_ok;
  ctx->alignment_ok = alignment_ok;

  if (!power_ok) {
    ctx->startup_state = GL30_STARTUP_POWER_CHECK;
  } else if (!self_test_ok) {
    ctx->startup_state = GL30_STARTUP_SELF_TEST;
  } else if (!encoder_ok) {
    ctx->startup_state = GL30_STARTUP_ENCODER_CHECK;
  } else if (!driver_ok) {
    ctx->startup_state = GL30_STARTUP_DRIVER_CHECK;
  } else if (!adc_zero_ok) {
    ctx->startup_state = GL30_STARTUP_ADC_ZERO_CHECK;
  } else if (!alignment_ok) {
    ctx->startup_state = GL30_STARTUP_FOC_ALIGN;
  } else if (ctx->startup_state != GL30_STARTUP_ACTIVE) {
    ctx->startup_state = GL30_STARTUP_READY;
  }
}

void gl30_safety_on_valid_command(gl30_safety_t *ctx, uint64_t received_at_us) {
  if (ctx == NULL || gl30_safety_fault_latched(ctx)) {
    return;
  }
  ctx->last_valid_command_us = received_at_us;
  ctx->comm_state = GL30_COMM_NORMAL;
  ctx->warning_bits &= (uint32_t)~GL30_WARNING_COMM_10MS;
  ctx->applied_command_count += 1u;
}

void gl30_safety_on_bad_crc(gl30_safety_t *ctx) {
  if (ctx != NULL) {
    ctx->bad_crc_count++;
    ctx->dropped_frame_count++;
  }
}

void gl30_safety_on_bad_length(gl30_safety_t *ctx) {
  if (ctx != NULL) {
    ctx->bad_length_count++;
    ctx->dropped_frame_count++;
  }
}

void gl30_safety_on_unknown_version(gl30_safety_t *ctx) {
  if (ctx != NULL) {
    ctx->unknown_version_count++;
    ctx->dropped_frame_count++;
  }
}

void gl30_safety_on_unknown_type(gl30_safety_t *ctx) {
  if (ctx != NULL) {
    ctx->unknown_type_count++;
    ctx->dropped_frame_count++;
  }
}

void gl30_safety_request_arm(gl30_safety_t *ctx) {
  if (ctx == NULL || gl30_safety_fault_latched(ctx)) {
    return;
  }
  ctx->arm_requested = true;
  if (ctx->startup_state == GL30_STARTUP_READY &&
      ctx->last_valid_command_us != 0u &&
      ctx->comm_state == GL30_COMM_NORMAL) {
    ctx->startup_state = GL30_STARTUP_ACTIVE;
  }
}

void gl30_safety_disarm(gl30_safety_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  ctx->arm_requested = false;
  if (!gl30_safety_fault_latched(ctx) &&
      ctx->startup_state == GL30_STARTUP_ACTIVE) {
    ctx->startup_state = GL30_STARTUP_READY;
  }
}

void gl30_safety_release_control(gl30_safety_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  gl30_safety_disarm(ctx);
  ctx->last_valid_command_us = 0u;
  if (!gl30_safety_fault_latched(ctx)) {
    ctx->comm_state = GL30_COMM_WAITING;
    ctx->warning_bits &= (uint32_t)~GL30_WARNING_COMM_10MS;
  }
}

void gl30_safety_latch_fault(gl30_safety_t *ctx, uint32_t fault_bit) {
  if (ctx == NULL) {
    return;
  }
  ctx->fault_bits |= fault_bit;
  ctx->arm_requested = false;
  ctx->startup_state = GL30_STARTUP_FAULT_LATCHED;
}

void gl30_safety_set_warning(gl30_safety_t *ctx, uint32_t warning_bit, bool active) {
  if (ctx == NULL) {
    return;
  }
  if (active) {
    ctx->warning_bits |= warning_bit;
  } else {
    ctx->warning_bits &= ~warning_bit;
  }
}

void gl30_safety_tick(gl30_safety_t *ctx, uint64_t now_us) {
  if (ctx == NULL || gl30_safety_fault_latched(ctx)) {
    return;
  }
  ctx->last_tick_us = now_us;

  if (ctx->last_valid_command_us == 0u || now_us < ctx->last_valid_command_us) {
    ctx->comm_state = GL30_COMM_WAITING;
    gl30_safety_disarm(ctx);
    return;
  }

  const uint64_t age_us = now_us - ctx->last_valid_command_us;
  if (age_us >= GL30_COMM_LOST_US) {
    ctx->comm_state = GL30_COMM_LOST;
    gl30_safety_latch_fault(ctx, GL30_FAULT_COMM_LOST);
  } else if (age_us >= GL30_COMM_SAFE_ZERO_US) {
    ctx->comm_state = GL30_COMM_SAFE_ZERO;
    gl30_safety_disarm(ctx);
  } else if (age_us >= GL30_COMM_WARN_US) {
    ctx->comm_state = GL30_COMM_WARN;
    ctx->warning_bits |= GL30_WARNING_COMM_10MS;
  } else {
    ctx->comm_state = GL30_COMM_NORMAL;
    ctx->warning_bits &= (uint32_t)~GL30_WARNING_COMM_10MS;
  }
}

bool gl30_safety_torque_allowed(const gl30_safety_t *ctx) {
  return ctx != NULL &&
         ctx->fault_bits == 0u &&
         ctx->startup_state == GL30_STARTUP_ACTIVE &&
         (ctx->comm_state == GL30_COMM_NORMAL ||
          ctx->comm_state == GL30_COMM_WARN);
}

bool gl30_safety_fault_latched(const gl30_safety_t *ctx) {
  return ctx != NULL && ctx->fault_bits != 0u;
}
