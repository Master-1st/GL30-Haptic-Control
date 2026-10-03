/* Exercise the real product DRV8316 transport/status/configuration code. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../config/board_config.h"
#include "../drivers/drv8316.h"

#define GL30_BUILD_ONLY 0
#define __MAIN_H
#define SPI3 3
#define TIM1 1
#define GPIOA 1
#define GPIOB 2
#define LL_GPIO_PIN_3 (1u << 3)
#define LL_GPIO_PIN_8 (1u << 8)
#define LL_GPIO_PIN_9 (1u << 9)
#define LL_GPIO_PIN_10 (1u << 10)
#define LL_GPIO_PIN_12 (1u << 12)
#define LL_GPIO_PIN_13 (1u << 13)
#define LL_GPIO_PIN_14 (1u << 14)
#define LL_GPIO_PIN_15 (1u << 15)
#define DRV8316_DRVOFF_GPIO_Port GPIOA
#define DRV8316_DRVOFF_Pin LL_GPIO_PIN_3
#define DRV8316_NSS_GPIO_Port 2
#define DRV8316_NSS_Pin 64

enum { TRACE_CAPACITY = 64 };

static uint16_t active_reply;
static uint8_t device_regs[64];
static uint8_t status_summary[64];
static uint8_t gate_sense_high_mask;
static int transient_summary_address;
static uint8_t transient_summary_mask;
static unsigned transient_summary_reads;
static uint32_t primask;
static uint64_t fake_us;
static uint64_t transfer_deadline_start_us;
static bool rx_ready;
static bool n_fault_high = true;
static bool moe;
static bool nss_high = true;
static bool drvoff_high = true;
static bool transfer_deadline_active;
static bool spi_enabled = true;
static bool stale_fifo_stuck;
static bool late_reply;
static bool txe_stuck;
static bool bsy_stuck_before_tx;
static bool rxne_stuck;
static bool bsy_stuck_after_rx;
static bool ovr_active;
static bool modf_active;
static bool fre_active;
static bool ovr_on_tx;
static bool modf_on_tx;
static bool fre_on_tx;
static bool spi_disable_on_tx;
static bool safe_off_on_1ms_delay;
static bool invalidate_on_1ms_delay;
static bool clear_faults_take_effect;
static bool mismatch_enabled;
static bool mismatch_after_clear;
static uint8_t mismatch_address;
static uint8_t mismatch_mask;
static unsigned async_safe_off_on_tx;
static bool safe_off_on_receive;
static bool safe_off_inside_receive;
static bool safe_off_injected;
static bool invalidate_on_tx;
static bool invalidate_on_receive;
static bool cancel_irq_at_drvoff;
static bool cancel_irq_at_moe;
static bool safe_off_on_verify_publish;
static bool invalidate_on_verify_publish;
static bool break_before_moe;
static bool break_after_moe;
static bool fake_brk_active;
static bool pending_safe_off_irq;
static unsigned safe_off_cs_toggles;
static unsigned stale_reads;
static unsigned tx_count;
static unsigned rx_count;
static unsigned delay_calls;
static unsigned timebase_reads;
static unsigned irq_io_violations;
static unsigned irq_restore_count;
static unsigned final_config_read_restore_first;
static unsigned final_config_read_restore_last;
static unsigned inject_safe_off_at_restore_count;
static bool injected_safe_off_at_restore;
static unsigned rx_count_at_moe;
static bool n_fault_high_at_moe;
static unsigned ctrl1_unlock_writes;
static unsigned ctrl1_lock_writes;
static unsigned clear_fault_commands;
static uint64_t nss_fall_times[TRACE_CAPACITY];
static uint64_t nss_rise_times[TRACE_CAPACITY];
static uint64_t tx_times[TRACE_CAPACITY];
static uint64_t rx_times[TRACE_CAPACITY];
static unsigned nss_fall_count;
static unsigned nss_rise_count;

static void dispatch_pending_irq(void) {
  if (primask == 0u && pending_safe_off_irq) {
    pending_safe_off_irq = false;
    gl30_drv8316_safe_off();
  }
}

static void restore_primask(uint32_t value) {
  const bool restoring = primask != 0u && value == 0u;
  primask = value;
  if (restoring) {
    irq_restore_count++;
    if (tx_count >= 15u && rx_count >= 7u) {
      if (final_config_read_restore_first == 0u) {
        final_config_read_restore_first = irq_restore_count;
      }
      final_config_read_restore_last = irq_restore_count;
    }
    if (inject_safe_off_at_restore_count == irq_restore_count &&
        tx_count >= 15u && rx_count >= 7u) {
      inject_safe_off_at_restore_count = 0u;
      injected_safe_off_at_restore = true;
      pending_safe_off_irq = true;
    }
    if (safe_off_on_verify_publish && gl30_drv8316_startup_verified()) {
      safe_off_on_verify_publish = false;
      pending_safe_off_irq = true;
    }
    if (invalidate_on_verify_publish &&
        gl30_drv8316_startup_verified()) {
      invalidate_on_verify_publish = false;
      gl30_drv8316_invalidate_configuration();
    }
  }
  dispatch_pending_irq();
}

static void request_safe_off_irq(void) {
  pending_safe_off_irq = true;
  dispatch_pending_irq();
}

static uint32_t __get_PRIMASK(void) { return primask; }
static void __disable_irq(void) { primask = 1u; }
static void __enable_irq(void) { restore_primask(0u); }

uint64_t gl30_timebase_now_us(void) {
  timebase_reads++;
  return fake_us;
}

void gl30_timebase_delay_us(uint32_t us) {
  if (primask != 0u) {
    irq_io_violations++;
  }
  delay_calls++;
  fake_us += us;
  if (us == 1000u && safe_off_on_1ms_delay) {
    safe_off_on_1ms_delay = false;
    gl30_drv8316_safe_off();
  }
  if (us == 1000u && invalidate_on_1ms_delay) {
    invalidate_on_1ms_delay = false;
    gl30_drv8316_invalidate_configuration();
  }
}

static bool fake_spi_is_enabled(int spi) {
  (void)spi;
  if (primask != 0u) {
    irq_io_violations++;
  }
  if (!transfer_deadline_active) {
    transfer_deadline_start_us = fake_us;
    transfer_deadline_active = true;
  }
  return spi_enabled;
}

static bool fake_spi_txe(int spi) {
  (void)spi;
  if (primask != 0u) {
    irq_io_violations++;
  }
  if (txe_stuck) {
    fake_us++;
    return false;
  }
  return true;
}

static bool fake_spi_bsy(int spi) {
  (void)spi;
  if (primask != 0u) {
    irq_io_violations++;
  }
  if (bsy_stuck_before_tx) {
    fake_us++;
    return true;
  }
  if (bsy_stuck_after_rx && rx_count != 0u) {
    fake_us++;
    return true;
  }
  return false;
}

static bool fake_spi_rxne(int spi) {
  (void)spi;
  if (primask != 0u) {
    irq_io_violations++;
  }
  if (stale_fifo_stuck && tx_count == 0u) {
    return rx_ready;
  }
  if (rxne_stuck && tx_count != 0u) {
    fake_us++;
    return false;
  }
  if (late_reply && tx_count != 0u) {
    /* Model RXNE asserting exactly at the shared transaction deadline. */
    fake_us = transfer_deadline_start_us + GL30_DRV8316_SPI_TIMEOUT_US;
    rx_ready = true;
    late_reply = false;
  }
  return rx_ready;
}

static bool fake_spi_ovr(int spi) {
  (void)spi;
  if (primask != 0u) {
    irq_io_violations++;
  }
  return ovr_active;
}

static bool fake_spi_modf(int spi) {
  (void)spi;
  if (primask != 0u) {
    irq_io_violations++;
  }
  return modf_active;
}

static bool fake_spi_fre(int spi) {
  (void)spi;
  if (primask != 0u) {
    irq_io_violations++;
  }
  return fre_active;
}

#define LL_SPI_IsEnabled(x) fake_spi_is_enabled((x))
#define LL_SPI_Enable(x) ((void)(x), spi_enabled = true)
#define LL_SPI_IsActiveFlag_OVR(x) fake_spi_ovr((x))
#define LL_SPI_IsActiveFlag_MODF(x) fake_spi_modf((x))
#define LL_SPI_IsActiveFlag_FRE(x) fake_spi_fre((x))
#define LL_SPI_IsActiveFlag_TXE(x) fake_spi_txe((x))
#define LL_SPI_IsActiveFlag_BSY(x) fake_spi_bsy((x))
#define LL_SPI_IsActiveFlag_RXNE(x) fake_spi_rxne((x))

static void LL_SPI_TransmitData16(int spi, uint16_t word) {
  (void)spi;
  if (primask != 0u) {
    irq_io_violations++;
  }
  if (tx_count < TRACE_CAPACITY) {
    tx_times[tx_count] = fake_us;
  }
  tx_count++;

  const unsigned address = (word >> 9u) & 63u;
  const bool is_read = (word & 0x8000u) != 0u;
  uint8_t data = device_regs[address];
  if (!is_read) {
    if (address == GL30_DRV8316_REG_CTRL1) {
      const uint8_t next = (uint8_t)word & 0x07u;
      if (next == 0x03u) {
        ctrl1_unlock_writes++;
      } else if (next == 0x06u) {
        ctrl1_lock_writes++;
      }
    }
    const bool ctrl1 = address == GL30_DRV8316_REG_CTRL1;
    const bool unlocked = (device_regs[GL30_DRV8316_REG_CTRL1] & 0x07u) == 0x03u;
    if (ctrl1 || unlocked) {
      device_regs[address] = (uint8_t)word;
    }
  } else if (mismatch_enabled && address == mismatch_address) {
    data ^= mismatch_mask;
  }

  if (!is_read && address == GL30_DRV8316_REG_CTRL2 &&
      (uint8_t)word == 0x69u) {
    clear_fault_commands++;
    /* CLR_FLT is self-clearing even when the modeled fault remains active. */
    device_regs[GL30_DRV8316_REG_CTRL2] = 0x68u;
    if (clear_faults_take_effect) {
      memset(status_summary, 0x08, sizeof(status_summary));
      device_regs[GL30_DRV8316_REG_STAT0] = 0x08u;
      device_regs[GL30_DRV8316_REG_STAT1] = 0u;
      device_regs[GL30_DRV8316_REG_STAT2] = 0u;
      clear_faults_take_effect = false;
    }
    if (mismatch_after_clear) {
      mismatch_enabled = true;
      mismatch_address = GL30_DRV8316_REG_CTRL2;
      mismatch_mask = 0x01u;
      mismatch_after_clear = false;
    }
  }
  uint8_t reply_summary = status_summary[address];
  if (is_read && (int)address == transient_summary_address &&
      transient_summary_reads != 0u) {
    reply_summary |= transient_summary_mask;
    transient_summary_reads--;
  }
  active_reply = (uint16_t)(((uint16_t)reply_summary << 8u) | data);
  if (async_safe_off_on_tx == tx_count) {
    async_safe_off_on_tx = 0u;
    gl30_drv8316_safe_off();
  }
  if (ovr_on_tx) {
    ovr_active = true;
    ovr_on_tx = false;
  }
  if (modf_on_tx) {
    modf_active = true;
    modf_on_tx = false;
  }
  if (fre_on_tx) {
    fre_active = true;
    fre_on_tx = false;
  }
  if (spi_disable_on_tx) {
    spi_disable_on_tx = false;
    spi_enabled = false;
  }
  if (invalidate_on_tx) {
    invalidate_on_tx = false;
    gl30_drv8316_invalidate_configuration();
  }
  rx_ready = !rxne_stuck && !late_reply;
}

static uint16_t LL_SPI_ReceiveData16(int spi) {
  (void)spi;
  if (primask != 0u) {
    irq_io_violations++;
  }
  if (rx_count < TRACE_CAPACITY) {
    rx_times[rx_count] = fake_us;
  }
  rx_count++;
  if (stale_fifo_stuck && tx_count == 0u) {
    stale_reads++;
    if (stale_reads >= 10u) {
      stale_fifo_stuck = false;
      rx_ready = false;
    }
  } else {
    rx_ready = false;
    if (safe_off_on_receive && !safe_off_injected) {
      safe_off_injected = true;
      safe_off_inside_receive = true;
      gl30_drv8316_safe_off();
      safe_off_inside_receive = false;
    }
    if (invalidate_on_receive) {
      invalidate_on_receive = false;
      gl30_drv8316_invalidate_configuration();
    }
  }
  return active_reply;
}

static void fake_gpio_set(int port, int pin) {
  if (port == DRV8316_DRVOFF_GPIO_Port && pin == DRV8316_DRVOFF_Pin) {
    drvoff_high = true;
  }
  if (port == DRV8316_NSS_GPIO_Port && pin == DRV8316_NSS_Pin) {
    if (safe_off_inside_receive) {
      safe_off_cs_toggles++;
    }
    nss_high = true;
    transfer_deadline_active = false;
    if (nss_rise_count < TRACE_CAPACITY) {
      nss_rise_times[nss_rise_count] = fake_us;
    }
    nss_rise_count++;
  }
}

static void fake_gpio_reset(int port, int pin) {
  if (port == DRV8316_DRVOFF_GPIO_Port && pin == DRV8316_DRVOFF_Pin) {
    if (cancel_irq_at_drvoff) {
      cancel_irq_at_drvoff = false;
      request_safe_off_irq();
    }
    drvoff_high = false;
  }
  if (port == DRV8316_NSS_GPIO_Port && pin == DRV8316_NSS_Pin) {
    nss_high = false;
    if (nss_fall_count < TRACE_CAPACITY) {
      nss_fall_times[nss_fall_count] = fake_us;
    }
    nss_fall_count++;
  }
}

static bool fake_gpio_input(int port, int pin) {
  if (port == DRV8316_DRVOFF_GPIO_Port && pin == DRV8316_DRVOFF_Pin) {
    return drvoff_high;
  }
  if (port == GPIOA && pin == LL_GPIO_PIN_8) {
    return (gate_sense_high_mask & (1u << 0u)) != 0u;
  }
  if (port == GPIOA && pin == LL_GPIO_PIN_9) {
    return (gate_sense_high_mask & (1u << 1u)) != 0u;
  }
  if (port == GPIOA && pin == LL_GPIO_PIN_10) {
    return (gate_sense_high_mask & (1u << 2u)) != 0u;
  }
  if (port == GPIOB && pin == LL_GPIO_PIN_13) {
    return (gate_sense_high_mask & (1u << 3u)) != 0u;
  }
  if (port == GPIOB && pin == LL_GPIO_PIN_14) {
    return (gate_sense_high_mask & (1u << 4u)) != 0u;
  }
  if (port == GPIOB && pin == LL_GPIO_PIN_15) {
    return (gate_sense_high_mask & (1u << 5u)) != 0u;
  }
  if (port == GPIOB && pin == LL_GPIO_PIN_12) {
    return n_fault_high;
  }
  return false;
}

#define LL_GPIO_SetOutputPin(x, y) fake_gpio_set((x), (y))
#define LL_GPIO_ResetOutputPin(x, y) fake_gpio_reset((x), (y))
#define LL_GPIO_IsInputPinSet(x, y) fake_gpio_input((x), (y))
#define LL_TIM_DisableIT_BRK(x) ((void)(x))

static void fake_tim_enable_brk(void) {
  if (break_before_moe) {
    break_before_moe = false;
    fake_brk_active = true;
  }
}

static void fake_tim_enable_moe(void) {
  rx_count_at_moe = rx_count;
  n_fault_high_at_moe = n_fault_high;
  moe = true;
  if (break_after_moe) {
    break_after_moe = false;
    fake_brk_active = true;
    moe = false;
  }
  if (cancel_irq_at_moe) {
    cancel_irq_at_moe = false;
    request_safe_off_irq();
  }
}

static void fake_tim_clear_brk(int timer) {
  (void)timer;
  fake_brk_active = false;
}

static bool fake_tim_is_brk_active(int timer) {
  (void)timer;
  return fake_brk_active;
}

#define LL_TIM_EnableIT_BRK(x) ((void)(x), fake_tim_enable_brk())
#define LL_TIM_DisableAllOutputs(x) ((void)(x), moe = false)
#define LL_TIM_EnableAllOutputs(x) ((void)(x), fake_tim_enable_moe())
#define LL_TIM_IsEnabledAllOutputs(x) ((void)(x), moe)
#define LL_TIM_ClearFlag_BRK(x) fake_tim_clear_brk((x))
#define LL_TIM_IsActiveFlag_BRK(x) fake_tim_is_brk_active((x))
#define LL_TIM_OC_SetCompareCH1(x, y) ((void)(x), (void)(y))
#define LL_TIM_OC_SetCompareCH2(x, y) ((void)(x), (void)(y))
#define LL_TIM_OC_SetCompareCH3(x, y) ((void)(x), (void)(y))

#include "../drivers/drv8316_spi.c"
#include "../drivers/drv8316.c"

static unsigned failed;
static unsigned checks;

#define CHECK(condition, message)                                               \
  do {                                                                          \
    checks++;                                                                   \
    if (!(condition)) {                                                         \
      failed++;                                                                 \
      fprintf(stderr, "[FAIL] %s\n", (message));                               \
    }                                                                           \
  } while (0)

static void reset_trace(void) {
  memset(nss_fall_times, 0, sizeof(nss_fall_times));
  memset(nss_rise_times, 0, sizeof(nss_rise_times));
  memset(tx_times, 0, sizeof(tx_times));
  memset(rx_times, 0, sizeof(rx_times));
  nss_fall_count = 0u;
  nss_rise_count = 0u;
}

static void normal(void) {
  memset(device_regs, 0, sizeof(device_regs));
  memset(status_summary, 0x08, sizeof(status_summary));
  gate_sense_high_mask = 0u;
  transient_summary_address = -1;
  transient_summary_mask = 0u;
  transient_summary_reads = 0u;
  device_regs[GL30_DRV8316_REG_STAT0] = 0x08u;
  device_regs[GL30_DRV8316_REG_CTRL1] = 0x06u;
  primask = 0u;
  fake_us = 0u;
  transfer_deadline_start_us = 0u;
  rx_ready = false;
  n_fault_high = true;
  moe = false;
  nss_high = true;
  drvoff_high = true;
  spi_enabled = true;
  stale_fifo_stuck = false;
  late_reply = false;
  txe_stuck = false;
  bsy_stuck_before_tx = false;
  rxne_stuck = false;
  bsy_stuck_after_rx = false;
  ovr_active = false;
  modf_active = false;
  fre_active = false;
  ovr_on_tx = false;
  modf_on_tx = false;
  fre_on_tx = false;
  mismatch_enabled = false;
  mismatch_after_clear = false;
  safe_off_on_1ms_delay = false;
  invalidate_on_1ms_delay = false;
  clear_faults_take_effect = false;
  mismatch_address = 0u;
  mismatch_mask = 0x80u;
  async_safe_off_on_tx = 0u;
  safe_off_on_receive = false;
  safe_off_inside_receive = false;
  safe_off_injected = false;
  invalidate_on_tx = false;
  invalidate_on_receive = false;
  transfer_deadline_active = false;
  cancel_irq_at_drvoff = false;
  cancel_irq_at_moe = false;
  safe_off_on_verify_publish = false;
  invalidate_on_verify_publish = false;
  break_before_moe = false;
  break_after_moe = false;
  fake_brk_active = false;
  pending_safe_off_irq = false;
  safe_off_cs_toggles = 0u;
  stale_reads = 0u;
  tx_count = 0u;
  rx_count = 0u;
  delay_calls = 0u;
  timebase_reads = 0u;
  irq_io_violations = 0u;
  irq_restore_count = 0u;
  final_config_read_restore_first = 0u;
  final_config_read_restore_last = 0u;
  inject_safe_off_at_restore_count = 0u;
  injected_safe_off_at_restore = false;
  rx_count_at_moe = 0u;
  n_fault_high_at_moe = true;
  ctrl1_unlock_writes = 0u;
  ctrl1_lock_writes = 0u;
  clear_fault_commands = 0u;
  reset_trace();
}

static void init_driver(void) {
  normal();
  gl30_drv8316_init();
  reset_trace();
}

static bool read_stat0(uint8_t *out) {
  return gl30_drv8316_read_register(GL30_DRV8316_REG_STAT0, out);
}

static bool configure_and_verify(void) {
  return gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()) &&
         gl30_drv8316_verify_startup(
             gl30_drv8316_off_generation_snapshot());
}

static void test_status_normalization(void) {
  gl30_drv8316_status_t status;
  init_driver();
  CHECK(gl30_drv8316_read_faults(&status), "product reads normal status triplet");
  CHECK(status.stat0 == 0x08u && status.stat1 == 0u && status.stat2 == 0u,
        "raw status remains diagnostic data");
  CHECK(gl30_drv8316_clear_faults(),
        "normal NPOR=1 must pass product fault-clear validation");
  for (unsigned address = 0u; address < 3u; ++address) {
    normal();
    status_summary[address] |= 0x01u;
    CHECK(!gl30_drv8316_clear_faults(),
          "fault in any reply summary must survive later healthy reads");
    normal();
    status_summary[address] &= (uint8_t)~0x08u;
    CHECK(!gl30_drv8316_clear_faults(), "NPOR low in any summary is a fault");
    normal();
    device_regs[address] |= 0x01u;
    CHECK(!gl30_drv8316_clear_faults(), "detail fault in any register is rejected");
  }
  normal();
  device_regs[GL30_DRV8316_REG_STAT0] = 0u;
  CHECK(!gl30_drv8316_clear_faults(),
        "detail NPOR=0 cannot be accepted as all-zero healthy status");
  normal();
  device_regs[GL30_DRV8316_REG_STAT2] |= 0x80u;
  CHECK(gl30_drv8316_clear_faults(), "reserved STAT2 bit7 alone is not a fault");
  normal();
  n_fault_high = false;
  CHECK(!gl30_drv8316_clear_faults(),
        "physical nFAULT still gates a healthy SPI triplet");
}

static void test_stale_fifo_and_late_ready(void) {
  uint8_t data;
  init_driver();
  data = 0xA5u;
  rx_ready = true;
  stale_fifo_stuck = true;
  CHECK(!read_stat0(&data), "stuck stale RXNE FIFO must fail before starting a transfer");
  CHECK(stale_reads <= 4u && tx_count == 0u && nss_high,
        "stale FIFO failure must be bounded, avoid TX, and leave CS high");
  CHECK(data == 0xA5u, "stale FIFO failure must not publish a register value");

  init_driver();
  data = 0xA5u;
  late_reply = true;
  CHECK(!read_stat0(&data), "RXNE at the transaction deadline must fail");
  CHECK(nss_high && data == 0xA5u,
        "late-ready reply must leave CS high and preserve caller output");
}

static void test_wait_timeouts_and_spi_errors(void) {
  uint8_t data;

  init_driver();
  data = 0u;
  CHECK(read_stat0(&data), "successful prior frame establishes an idle SPI boundary");
  reset_trace();
  const unsigned tx_before_txe_timeout = tx_count;
  txe_stuck = true;
  data = 0xA5u;
  CHECK(!read_stat0(&data) && tx_count == tx_before_txe_timeout &&
            nss_high && nss_fall_count == 0u,
        "idle TXE timeout must fail before CS falls or another word is sent");

  init_driver();
  data = 0u;
  CHECK(read_stat0(&data), "successful prior frame precedes BSY boundary test");
  reset_trace();
  const unsigned tx_before_bsy_timeout = tx_count;
  bsy_stuck_before_tx = true;
  data = 0xA5u;
  CHECK(!read_stat0(&data) && tx_count == tx_before_bsy_timeout &&
            nss_high && nss_fall_count == 0u,
        "idle BSY timeout must fail before CS falls or another word is sent");

  init_driver();
  rxne_stuck = true;
  data = 0xA5u;
  CHECK(!read_stat0(&data) && tx_count == 1u && nss_high,
        "RXNE timeout must stop the active frame and release CS");

  init_driver();
  bsy_stuck_after_rx = true;
  data = 0xA5u;
  CHECK(!read_stat0(&data) && tx_count == 1u && rx_count == 1u && nss_high,
        "post-receive BSY timeout must release CS");

  init_driver();
  spi_enabled = false;
  data = 0xA5u;
  CHECK(!read_stat0(&data) && tx_count == 0u && nss_high && data == 0xA5u,
        "disabled SPI must fail without sending or changing caller output");

  init_driver();
  ovr_on_tx = true;
  data = 0xA5u;
  CHECK(!read_stat0(&data) && nss_high && data == 0xA5u,
        "OVR raised during a frame must fail and release CS");

  init_driver();
  modf_on_tx = true;
  data = 0xA5u;
  CHECK(!read_stat0(&data) && nss_high && data == 0xA5u,
        "MODF raised during a frame must fail and release CS");

  init_driver();
  fre_on_tx = true;
  data = 0xA5u;
  CHECK(!read_stat0(&data) && nss_high && data == 0xA5u,
        "FRE raised during a frame must fail and release CS");

  init_driver();
  spi_disable_on_tx = true;
  data = 0xA5u;
  CHECK(!read_stat0(&data) && nss_high && data == 0xA5u,
        "SPI disabled after TX must fail and release CS");
}

static void test_cs_timing_and_irq_scope(void) {
  uint8_t data = 0u;
  init_driver();
  CHECK(read_stat0(&data), "first timed read succeeds");
  CHECK(read_stat0(&data), "second timed read succeeds");
  CHECK(nss_fall_count == 2u && nss_rise_count == 2u,
        "two reads must produce two complete CS frames");
  if (nss_fall_count >= 2u && nss_rise_count >= 2u && rx_count >= 2u) {
    CHECK(tx_times[0] >= nss_fall_times[0] + 1u,
          "CS setup interval must be at least one microsecond");
    CHECK(nss_rise_times[0] >= rx_times[0] + 1u,
          "CS hold interval must be at least one microsecond");
    CHECK(nss_fall_times[1] >= nss_rise_times[0] + 1u,
          "CS high interval between frames must be at least one microsecond");
  }
  CHECK(irq_io_violations == 0u,
        "SPI waits, transfers, and delays must run with interrupts enabled");
}

static void test_configuration_image_and_failures(void) {
  static const struct {
    uint8_t address;
    uint8_t value;
  } expected[] = {
      {GL30_DRV8316_REG_CTRL2, 0x68u},
      {GL30_DRV8316_REG_CTRL3, 0x5Fu},
      {GL30_DRV8316_REG_CTRL4, 0x10u},
      {GL30_DRV8316_REG_CTRL5, 0x02u},
      {GL30_DRV8316_REG_CTRL6, 0x01u},
      {GL30_DRV8316_REG_CTRL10, 0x00u},
  };

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "full device register model accepts configuration");
  CHECK(gl30_drv8316_is_configured() && device_regs[GL30_DRV8316_REG_CTRL1] == 0x06u,
        "configuration succeeds only after locking CTRL1");
  for (unsigned i = 0u; i < sizeof(expected) / sizeof(expected[0]); ++i) {
    CHECK(device_regs[expected[i].address] == expected[i].value,
          "configured register contains the requested image");
  }
  CHECK(ctrl1_unlock_writes == 1u && ctrl1_lock_writes == 1u,
        "successful configuration unlocks then locks once");
  gl30_drv8316_safe_off();
  CHECK(gl30_drv8316_is_configured(),
        "ordinary safe-off leaves completed configuration reusable");

  const uint8_t readback_addresses[] = {
      GL30_DRV8316_REG_CTRL2, GL30_DRV8316_REG_CTRL3,
      GL30_DRV8316_REG_CTRL4, GL30_DRV8316_REG_CTRL5,
      GL30_DRV8316_REG_CTRL6, GL30_DRV8316_REG_CTRL10,
      GL30_DRV8316_REG_CTRL1,
  };
  for (unsigned i = 0u;
       i < sizeof(readback_addresses) / sizeof(readback_addresses[0]); ++i) {
    init_driver();
    mismatch_enabled = true;
    mismatch_address = readback_addresses[i];
    mismatch_mask = mismatch_address == GL30_DRV8316_REG_CTRL1 ? 0x01u : 0x80u;
    CHECK(!gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "mismatched configuration readback fails closed");
    CHECK(!gl30_drv8316_is_configured() && drvoff_high && !moe,
          "readback failure never marks configured and keeps power off");
  }

  init_driver();
  async_safe_off_on_tx = 2u;
  CHECK(!gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "ordinary safe-off cancels in-flight configure");
  CHECK(!gl30_drv8316_is_configured() && drvoff_high && !moe,
        "cancelled configuration remains unpublished and outputs stay off");
  CHECK(device_regs[GL30_DRV8316_REG_CTRL1] == 0x06u &&
            ctrl1_lock_writes == 1u,
        "ordinary cancellation best-effort relocks CTRL1 once");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "baseline configure locates final read restore points");
  const unsigned restore_first = final_config_read_restore_first;
  const unsigned restore_last = final_config_read_restore_last;
  CHECK(restore_first != 0u && restore_last >= restore_first,
        "configuration restores IRQ after the final locked readback");
  for (unsigned restore_index = restore_first;
       restore_index <= restore_last && restore_index != 0u; ++restore_index) {
    init_driver();
    inject_safe_off_at_restore_count = restore_index;
    CHECK(!gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()),
          "pending safe-off at final-read IRQ restore cancels configuration");
    CHECK(injected_safe_off_at_restore && !gl30_drv8316_is_configured() &&
              drvoff_high && !moe,
          "IRQ-restore cancellation clears configuration and leaves output off");
  }
  CHECK(irq_io_violations == 0u,
        "configuration SPI and timing work never holds IRQs disabled");
}

static void test_configuration_generation_gate(void) {
  init_driver();
  const uint32_t stale_after_safe_off =
      gl30_drv8316_off_generation_snapshot();
  gl30_drv8316_safe_off();
  const unsigned tx_before_safe_off_reject = tx_count;
  CHECK(!gl30_drv8316_configure(stale_after_safe_off),
        "configuration rejects a generation invalidated by ordinary safe-off");
  CHECK(tx_count == tx_before_safe_off_reject &&
            !gl30_drv8316_is_configured() && drvoff_high,
        "stale safe-off generation is rejected before any SPI transfer");

  init_driver();
  const uint32_t stale_after_invalidate =
      gl30_drv8316_off_generation_snapshot();
  gl30_drv8316_invalidate_configuration();
  const unsigned tx_before_invalidate_reject = tx_count;
  CHECK(!gl30_drv8316_configure(stale_after_invalidate),
        "configuration rejects a generation invalidated by power-off");
  CHECK(tx_count == tx_before_invalidate_reject &&
            !gl30_drv8316_is_configured() && drvoff_high && nss_high,
        "stale power generation cannot configure or start SPI");

  init_driver();
  const unsigned tx_before_masked_configure = tx_count;
  primask = 1u;
  CHECK(!gl30_drv8316_configure(
            gl30_drv8316_off_generation_snapshot()),
        "configuration rejects a caller that entered with PRIMASK set");
  CHECK(primask == 1u && tx_count == tx_before_masked_configure &&
            !gl30_drv8316_is_configured() && drvoff_high,
        "masked configuration preserves PRIMASK and does not issue SPI");
  primask = 0u;
}

static void test_safe_off_does_not_cancel_status_read(void) {
  uint8_t data = 0xA5u;
  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "configuration fixture succeeds before status read");
  const unsigned tx_before = tx_count;
  const unsigned rx_before = rx_count;
  safe_off_on_receive = true;
  CHECK(read_stat0(&data), "ordinary safe-off during a status read does not cancel it");
  CHECK(data == 0x08u && tx_count == tx_before + 1u && rx_count == rx_before + 1u,
        "status read returns its current reply despite ordinary safe-off");
  CHECK(gl30_drv8316_is_configured() && drvoff_high && nss_high,
        "ordinary safe-off preserves configured state and releases power/CS");
  CHECK(safe_off_cs_toggles == 0u,
        "ordinary safe-off does not change CS during an unrelated read");
}

static void test_primask_preservation(void) {
  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "PRIMASK preservation fixture configures");
  const unsigned tx_before = tx_count;
  const unsigned rx_before = rx_count;
  primask = 1u;
  gl30_drv8316_safe_off();
  CHECK(primask == 1u && gl30_drv8316_is_configured() && drvoff_high && !moe,
        "ordinary safe-off preserves caller PRIMASK and configured state");
  gl30_drv8316_invalidate_configuration();
  CHECK(primask == 1u && !gl30_drv8316_is_configured() && drvoff_high &&
            !moe && nss_high,
        "invalidation preserves caller PRIMASK and forces safe outputs/CS");
  CHECK(tx_count == tx_before && rx_count == rx_before,
        "safe-off and invalidation perform no SPI traffic");
  primask = 0u;
}

static void test_invalidate_configuration(void) {
  init_driver();
  CHECK(configure_and_verify(), "invalidation fixture configures and verifies");
  const unsigned tx_before = tx_count;
  const unsigned rx_before = rx_count;
  const unsigned delay_before = delay_calls;
  const unsigned time_before = timebase_reads;
  gl30_drv8316_invalidate_configuration();
  CHECK(!gl30_drv8316_is_configured() && drvoff_high && !moe && nss_high,
        "invalidation clears configuration and forces the hardware off");
  CHECK(!gl30_drv8316_startup_verified(),
        "power invalidation also clears startup verification");
  CHECK(tx_count == tx_before && rx_count == rx_before &&
            delay_calls == delay_before && timebase_reads == time_before,
        "invalidation performs no SPI transaction, wait, or delay");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "read invalidation fixture configures");
  uint8_t data = 0xA5u;
  const unsigned tx_before_read_invalidate = tx_count;
  invalidate_on_receive = true;
  CHECK(!read_stat0(&data), "invalidation during a reply aborts the read");
  CHECK(data == 0xA5u && tx_count == tx_before_read_invalidate + 1u &&
            !gl30_drv8316_is_configured() && drvoff_high && nss_high,
        "aborted read does not publish data and leaves driver off with CS high");

  init_driver();
  invalidate_on_tx = true;
  CHECK(!gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()),
        "invalidation during configuration prevents configured publication");
  CHECK(tx_count == 1u && !gl30_drv8316_is_configured() && drvoff_high &&
            nss_high,
        "invalidation stops later configuration transfers and releases CS");
}

static void test_startup_verify_preconditions_and_healthy_path(void) {
  init_driver();
  const unsigned tx_before_unconfigured = tx_count;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "startup verification rejects an unconfigured driver");
  CHECK(tx_count == tx_before_unconfigured && !moe && drvoff_high &&
            !gl30_drv8316_startup_verified(),
        "unconfigured verification fails closed without SPI or output enable");

  for (unsigned input = 0u; input < 6u; ++input) {
    init_driver();
    CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "feedback-high fixture configures");
    gate_sense_high_mask = (uint8_t)(1u << input);
    CHECK(!gl30_drv8316_verify_startup(
              gl30_drv8316_off_generation_snapshot()),
          "each non-low PWM feedback input independently blocks verification");
    CHECK(!gl30_drv8316_startup_verified() && !moe && drvoff_high,
          "feedback precondition failure leaves bridge disabled and DRVOFF high");
  }

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "healthy verification fixture configures");
  CHECK(gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "healthy cold status verifies startup");
  CHECK(gl30_drv8316_startup_verified() && !moe && !drvoff_high &&
            gl30_drv8316_is_configured(),
        "successful verification leaves DRVOFF asserted low with MOE off");
  CHECK(clear_fault_commands == 0u &&
            device_regs[GL30_DRV8316_REG_CTRL2] == 0x68u &&
            (device_regs[GL30_DRV8316_REG_CTRL1] & 0x07u) == 0x06u,
        "healthy status needs no clear and preserves the locked config image");
  CHECK(irq_io_violations == 0u,
        "verification does not mask IRQs across SPI or delays");

  init_driver();
  CHECK(configure_and_verify(), "active-output fixture verifies before arm");
  const uint32_t generation = gl30_drv8316_off_generation_snapshot();
  CHECK(gl30_drv8316_arm(generation), "verified fixture arms before gate check");
  CHECK(!gl30_drv8316_verify_startup(generation),
        "startup verification rejects an already enabled bridge");
  CHECK(!moe && !gl30_drv8316_outputs_enabled() && drvoff_high &&
            !gl30_drv8316_startup_verified(),
        "active-output verification failure turns outputs off");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "stale-generation fixture configures");
  const uint32_t stale_generation = gl30_drv8316_off_generation_snapshot();
  gl30_drv8316_safe_off();
  CHECK(!gl30_drv8316_verify_startup(stale_generation),
        "startup verification rejects an obsolete off generation");
  CHECK(!gl30_drv8316_startup_verified() && drvoff_high && !moe,
        "obsolete-generation rejection leaves the driver safely off");
}

static void test_startup_verify_status_and_npor_recovery(void) {
  for (unsigned address = GL30_DRV8316_REG_STAT0;
       address <= GL30_DRV8316_REG_STAT2; ++address) {
    init_driver();
    CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "summary-fault fixture configures");
    status_summary[address] |= 0x01u;
    CHECK(!gl30_drv8316_verify_startup(
              gl30_drv8316_off_generation_snapshot()),
          "a fault in any STAT reply summary blocks startup verification");
    CHECK(clear_fault_commands == 0u && !gl30_drv8316_startup_verified() &&
              drvoff_high && !moe,
          "summary fault is never cleared by startup verification");

    init_driver();
    CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "detail-fault fixture configures");
    device_regs[address] |= 0x01u;
    CHECK(!gl30_drv8316_verify_startup(
              gl30_drv8316_off_generation_snapshot()),
          "a detail fault in any STAT register blocks startup verification");
    CHECK(clear_fault_commands == 0u && !gl30_drv8316_startup_verified() &&
              drvoff_high && !moe,
          "detail fault is never cleared by startup verification");
  }

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "transient-summary fixture configures");
  transient_summary_address = GL30_DRV8316_REG_STAT1;
  transient_summary_mask = 0x01u;
  transient_summary_reads = 1u;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "a transient fault summary during the STAT triplet blocks verification");
  CHECK(clear_fault_commands == 0u && !gl30_drv8316_startup_verified(),
        "transient summary fault is preserved across the status read");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "fault-plus-NPOR fixture configures");
  status_summary[GL30_DRV8316_REG_STAT0] = 0u;
  device_regs[GL30_DRV8316_REG_STAT0] = 0u;
  device_regs[GL30_DRV8316_REG_STAT1] = 0x01u;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "NPOR combined with a real status fault is not clearable");
  CHECK(clear_fault_commands == 0u && !gl30_drv8316_startup_verified() &&
            drvoff_high && !moe,
        "mixed NPOR and fault exits safely without CLR_FLT");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "NPOR-only fixture configures");
  status_summary[GL30_DRV8316_REG_STAT0] = 0u;
  device_regs[GL30_DRV8316_REG_STAT0] = 0u;
  clear_faults_take_effect = true;
  CHECK(gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "NPOR-only status permits one explicit startup clear and recheck");
  CHECK(clear_fault_commands == 1u &&
            device_regs[GL30_DRV8316_REG_CTRL2] == 0x68u &&
            (device_regs[GL30_DRV8316_REG_CTRL1] & 0x07u) == 0x06u &&
            gl30_drv8316_startup_verified() && !moe,
        "NPOR recovery relocks exact configuration and never enables MOE");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "ineffective-clear fixture configures");
  status_summary[GL30_DRV8316_REG_STAT0] = 0u;
  device_regs[GL30_DRV8316_REG_STAT0] = 0u;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "startup verification rejects an ineffective NPOR clear");
  CHECK(clear_fault_commands == 1u && !gl30_drv8316_startup_verified() &&
            drvoff_high && !moe,
        "ineffective clear is attempted once and leaves the bridge off");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "register-mismatch fixture configures");
  mismatch_enabled = true;
  mismatch_address = GL30_DRV8316_REG_CTRL2;
  mismatch_mask = 0x01u;
  status_summary[GL30_DRV8316_REG_STAT0] = 0u;
  device_regs[GL30_DRV8316_REG_STAT0] = 0u;
  clear_faults_take_effect = true;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "NPOR recovery rejects a mismatched exact CTRL2 readback");
  CHECK(clear_fault_commands == 0u && !gl30_drv8316_startup_verified() &&
            drvoff_high && !moe,
        "initial config readback mismatch cannot publish startup verified");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "post-clear-readback fixture configures");
  status_summary[GL30_DRV8316_REG_STAT0] = 0u;
  device_regs[GL30_DRV8316_REG_STAT0] = 0u;
  clear_faults_take_effect = true;
  mismatch_after_clear = true;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "NPOR recovery rejects a mismatched post-clear CTRL2 readback");
  CHECK(clear_fault_commands == 1u && !gl30_drv8316_startup_verified() &&
            drvoff_high && !moe,
        "post-clear configuration mismatch cannot publish startup verified");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "PB12-low fixture configures");
  n_fault_high = false;
  const unsigned clears_before_low_pb12 = clear_fault_commands;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "low PB12 blocks startup verification");
  CHECK(clear_fault_commands == clears_before_low_pb12 &&
            !gl30_drv8316_startup_verified() && drvoff_high && !moe,
        "low PB12 is never cleared and final state remains safely off");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "SPI-timeout verify fixture configures");
  rxne_stuck = true;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "bounded SPI timeout blocks startup verification");
  CHECK(!gl30_drv8316_startup_verified() && drvoff_high && !moe && nss_high,
        "SPI timeout leaves CS high and the driver output disabled");
}

static void test_startup_verify_cancellation_and_invalidation(void) {
  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "DRVOFF-race fixture configures");
  cancel_irq_at_drvoff = true;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "pending ordinary safe-off at DRVOFF assertion cancels verification");
  CHECK(gl30_drv8316_is_configured() &&
            !gl30_drv8316_startup_verified() && drvoff_high && !moe,
        "ordinary cancellation retains config while clearing verification");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "delay-race fixture configures");
  safe_off_on_1ms_delay = true;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "ordinary safe-off during the startup delay cancels verification");
  CHECK(gl30_drv8316_is_configured() && !gl30_drv8316_startup_verified() &&
            drvoff_high && !moe,
        "delay cancellation keeps configuration and leaves outputs off");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "transfer-race fixture configures");
  async_safe_off_on_tx = tx_count + 1u;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "ordinary safe-off during SPI status transfer cancels verification");
  CHECK(gl30_drv8316_is_configured() && !gl30_drv8316_startup_verified() &&
            drvoff_high && !moe && nss_high,
        "transfer cancellation retains config and releases CS");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "power-delay fixture configures");
  invalidate_on_1ms_delay = true;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "power invalidation during startup delay cancels verification");
  CHECK(!gl30_drv8316_is_configured() &&
            !gl30_drv8316_startup_verified() && drvoff_high && !moe,
        "power cancellation clears config and startup verification");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "power-transfer fixture configures");
  invalidate_on_tx = true;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "power invalidation during SPI status transfer cancels verification");
  CHECK(!gl30_drv8316_is_configured() && !gl30_drv8316_startup_verified() &&
            drvoff_high && !moe && nss_high,
        "transfer invalidation clears config and releases CS");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "publish-race fixture configures");
  safe_off_on_verify_publish = true;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "ordinary safe-off after final verify publication cancels success");
  CHECK(gl30_drv8316_is_configured() &&
            !gl30_drv8316_startup_verified() && drvoff_high && !moe,
        "final ordinary cancellation cannot leave verified state published");

  init_driver();
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "invalidate-publish fixture configures");
  invalidate_on_verify_publish = true;
  CHECK(!gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "power invalidation after final verify publication cancels success");
  CHECK(!gl30_drv8316_is_configured() &&
            !gl30_drv8316_startup_verified() && drvoff_high && !moe && nss_high,
        "final invalidation cannot leave configured or verified state published");
  CHECK(irq_io_violations == 0u,
        "all startup cancellation paths keep SPI and delays interruptible");
}

static void test_startup_verification_lifecycle(void) {
  init_driver();
  CHECK(configure_and_verify(), "lifecycle fixture starts configured and verified");
  gl30_drv8316_safe_off();
  CHECK(gl30_drv8316_startup_verified(),
        "ordinary safe-off preserves the successful startup check");
  CHECK(gl30_drv8316_configure(gl30_drv8316_off_generation_snapshot()), "a later configuration operation succeeds");
  CHECK(!gl30_drv8316_startup_verified() && gl30_drv8316_is_configured(),
        "reconfiguration clears old startup verification");

  CHECK(gl30_drv8316_verify_startup(
            gl30_drv8316_off_generation_snapshot()),
        "reconfigured driver can pass startup verification again");
  gl30_drv8316_safe_off();
  uint32_t generation = gl30_drv8316_off_generation_snapshot();
  CHECK(gl30_drv8316_arm(generation),
        "ordinary safe-off permits re-arm with the current generation");
  gl30_drv8316_safe_off();
  CHECK(gl30_drv8316_startup_verified(),
        "second ordinary safe-off still retains verification");

  gl30_drv8316_invalidate_configuration();
  generation = gl30_drv8316_off_generation_snapshot();
  const unsigned tx_before_invalid_arm = tx_count;
  CHECK(!gl30_drv8316_startup_verified() &&
            !gl30_drv8316_is_configured(),
        "power invalidation removes both config and verification");
  CHECK(!gl30_drv8316_arm(generation) && tx_count == tx_before_invalid_arm &&
            !moe && drvoff_high,
        "invalidated driver cannot arm or send SPI without reconfiguration");
}

static void test_arm_cancellation_and_break_races(void) {
  uint32_t generation;

  init_driver();
  CHECK(configure_and_verify(), "positive arm fixture configures and verifies");
  const uint32_t first_generation = gl30_drv8316_off_generation_snapshot();
  const unsigned rx_before_first_arm = rx_count;
  CHECK(gl30_drv8316_arm(first_generation), "current generation allows a healthy arm");
  CHECK(gl30_drv8316_outputs_enabled() && moe && !drvoff_high,
        "successful arm releases DRVOFF and enables outputs");
  CHECK(rx_count_at_moe >= rx_before_first_arm + 3u && n_fault_high_at_moe,
        "arm reads all status registers and checks PB12 before enabling MOE");
  gl30_drv8316_safe_off();
  CHECK(gl30_drv8316_is_configured(), "ordinary off after arm retains configuration");
  CHECK(gl30_drv8316_startup_verified(),
        "ordinary safe-off retains successful startup verification");
  CHECK(!gl30_drv8316_arm(first_generation), "an older off generation cannot re-arm");
  CHECK(drvoff_high && !moe && !gl30_drv8316_outputs_enabled(),
        "stale-generation arm leaves outputs safely off");
  generation = gl30_drv8316_off_generation_snapshot();
  CHECK(gl30_drv8316_arm(generation), "fresh generation re-arms retained configuration");
  gl30_drv8316_safe_off();
  const unsigned tx_before_low_fault = tx_count;
  const unsigned clears_before_low_fault = clear_fault_commands;
  n_fault_high = false;
  generation = gl30_drv8316_off_generation_snapshot();
  CHECK(!gl30_drv8316_arm(generation), "asserted PB12 fault prevents arm");
  CHECK(tx_count >= tx_before_low_fault &&
            clear_fault_commands == clears_before_low_fault &&
            drvoff_high && !moe,
        "arm does not clear faults while PB12 remains low");

  init_driver();
  CHECK(configure_and_verify(), "NPOR-arm fixture starts verified");
  status_summary[GL30_DRV8316_REG_STAT0] = 0u;
  device_regs[GL30_DRV8316_REG_STAT0] = 0u;
  clear_faults_take_effect = true;
  generation = gl30_drv8316_off_generation_snapshot();
  CHECK(!gl30_drv8316_arm(generation), "arm rejects a new NPOR status");
  CHECK(clear_fault_commands == 0u && !moe && drvoff_high,
        "arm never sends CLR_FLT to recover a later NPOR fault");

  init_driver();
  CHECK(configure_and_verify(), "arm race fixture configures and verifies");
  generation = gl30_drv8316_off_generation_snapshot();
  cancel_irq_at_drvoff = true;
  CHECK(!gl30_drv8316_arm(generation), "pending safe-off at DRVOFF release cancels arm");
  CHECK(drvoff_high && !moe && !gl30_drv8316_outputs_enabled(),
        "cancelled arm leaves DRVOFF asserted and MOE disabled");

  init_driver();
  CHECK(configure_and_verify(), "MOE race fixture configures and verifies");
  generation = gl30_drv8316_off_generation_snapshot();
  cancel_irq_at_moe = true;
  CHECK(!gl30_drv8316_arm(generation),
        "pending safe-off while enabling MOE must prevent successful arm");
  CHECK(drvoff_high && !moe && !gl30_drv8316_outputs_enabled(),
        "pending off after IRQ restore leaves hardware and published state off");

  init_driver();
  CHECK(configure_and_verify(), "pre-MOE BRK fixture configures and verifies");
  generation = gl30_drv8316_off_generation_snapshot();
  break_before_moe = true;
  CHECK(!gl30_drv8316_arm(generation), "BRK before MOE enable prevents arm");
  CHECK(drvoff_high && !moe && !gl30_drv8316_outputs_enabled(),
        "pre-MOE BRK leaves output disabled");

  init_driver();
  CHECK(configure_and_verify(), "post-MOE BRK fixture configures and verifies");
  generation = gl30_drv8316_off_generation_snapshot();
  break_after_moe = true;
  CHECK(!gl30_drv8316_arm(generation), "BRK immediately after MOE enable cancels arm");
  CHECK(drvoff_high && !moe && !gl30_drv8316_outputs_enabled(),
        "post-MOE BRK leaves output disabled");
  CHECK(irq_io_violations == 0u,
        "arming does not mask IRQs across SPI or the millisecond delay");
}

int main(void) {
  test_status_normalization();
  test_stale_fifo_and_late_ready();
  test_wait_timeouts_and_spi_errors();
  test_cs_timing_and_irq_scope();
  test_configuration_image_and_failures();
  test_configuration_generation_gate();
  test_safe_off_does_not_cancel_status_read();
  test_primask_preservation();
  test_invalidate_configuration();
  test_startup_verify_preconditions_and_healthy_path();
  test_startup_verify_status_and_npor_recovery();
  test_startup_verify_cancellation_and_invalidation();
  test_startup_verification_lifecycle();
  test_arm_cancellation_and_break_races();
  printf("product driver: %u checks, %u failed\n", checks, failed);
  return failed == 0u ? 0 : 1;
}
