// Duta on Zephyr — nRF52840 (DK + dongle), and any Zephyr board.
// ============================================================================
// Built on the shared core (../../common/skrit_device.h). One CDC ACM endpoint
// carries CMD + DATA multiplexed (skrit-mux); a hardware UART (the `duta-data`
// alias) bridges the target console. Outputs are board LEDs/relays via GPIO.
//
// Transport bindings are devicetree-guarded: on `native_sim` (the hardware-free
// CI build-check) there is no CDC/UART, so the link/data devices resolve to NULL
// and main() runs a compile/link smoke check. On a real nRF52840 the same code
// brings up USB, the console UART, and the full protocol + macro VM.
// ============================================================================
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#if defined(CONFIG_REBOOT)
#include <zephyr/sys/reboot.h>
#endif

#include "skrit_device.h"

#define FW_LO 0x04
#define FW_HI 0x00

// ---- transport devices (compile-time guarded so native_sim still builds) ----
#if DT_NODE_HAS_STATUS(DT_NODELABEL(cdc_acm_uart0), okay)
#include <zephyr/usb/usb_device.h>
#define CMD_DEV DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0))
#define HAVE_USB 1
#else
#define CMD_DEV NULL
#define HAVE_USB 0
#endif

#if DT_NODE_EXISTS(DT_ALIAS(duta_data))
#define DATA_DEV DEVICE_DT_GET(DT_ALIAS(duta_data))
#else
#define DATA_DEV NULL
#endif

static const struct device *cmd_dev = CMD_DEV;
static const struct device *data_dev = DATA_DEV;

// ---- outputs: Relay 1, Relay 2, Aux LED (map to whatever LEDs/gpios exist) ---
// GPIO_DT_SPEC_GET_OR yields {0} for a missing alias; guarded by device_is_ready.
static const struct gpio_dt_spec out_gpio[3] = {
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios, {0}), // Relay 1
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(led2), gpios, {0}), // Relay 2
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0}), // Aux LED
};
static const char *const OUT_NAME[3] = {"Relay 1", "Relay 2", "Aux LED"};
static const uint8_t OUT_TYPE[3] = {SKRIT_CTRL_IO, SKRIT_CTRL_IO, SKRIT_CTRL_IO};
static uint8_t g_out[3];

// optional target reset/boot lines (drive a target's DTR/RTS-equivalent pins)
static const struct gpio_dt_spec dtr_gpio = GPIO_DT_SPEC_GET_OR(DT_ALIAS(duta_dtr), gpios, {0});
static const struct gpio_dt_spec rts_gpio = GPIO_DT_SPEC_GET_OR(DT_ALIAS(duta_rts), gpios, {0});

static uint32_t g_baud = 115200;
static uint8_t g_bits = 8, g_parity = SKRIT_PAR_NONE, g_stop = 1;

static skrit_dev dev;

// ---- HAL ------------------------------------------------------------------
static void hal_link_write(void *c, const uint8_t *p, uint16_t n) {
  (void)c;
  if (!cmd_dev) return;
  for (uint16_t i = 0; i < n; i++) uart_poll_out(cmd_dev, p[i]);
}
static void hal_data_write(void *c, const uint8_t *p, uint16_t n) {
  (void)c;
  if (!data_dev) return;
  for (uint16_t i = 0; i < n; i++) uart_poll_out(data_dev, p[i]);
}
static uint16_t hal_data_read(void *c, uint8_t *out, uint16_t cap) {
  (void)c;
  if (!data_dev) return 0;
  uint16_t k = 0;
  unsigned char ch;
  while (k < cap && uart_poll_in(data_dev, &ch) == 0) out[k++] = ch;
  return k;
}

static void hal_out_set(void *c, uint8_t idx, uint8_t on) {
  (void)c;
  if (idx > 2) return;
  g_out[idx] = on ? 1 : 0;
  if (device_is_ready(out_gpio[idx].port)) gpio_pin_set_dt(&out_gpio[idx], on ? 1 : 0);
}
static uint8_t hal_out_get(void *c, uint8_t idx) {
  (void)c;
  return idx < 3 ? g_out[idx] : 0;
}
static void hal_out_desc(void *c, uint8_t idx, uint8_t *type, const char **name) {
  (void)c;
  if (idx > 2) return;
  *type = OUT_TYPE[idx];
  *name = OUT_NAME[idx];
}

static void hal_serial_get(void *c, uint32_t *baud, uint8_t *bits, uint8_t *par, uint8_t *stop) {
  (void)c;
  *baud = g_baud;
  *bits = g_bits;
  *par = g_parity;
  *stop = g_stop;
}
static void hal_serial_set(void *c, uint32_t baud, uint8_t bits, uint8_t par, uint8_t stop) {
  (void)c;
  if (baud) g_baud = baud;
  if (bits) g_bits = bits;
  g_parity = par;
  if (stop) g_stop = stop;
  if (!data_dev || !device_is_ready(data_dev)) return;
  struct uart_config cfg = {
      .baudrate = g_baud,
      .data_bits = (g_bits == 7) ? UART_CFG_DATA_BITS_7 : UART_CFG_DATA_BITS_8,
      .stop_bits = (g_stop == 2) ? UART_CFG_STOP_BITS_2 : UART_CFG_STOP_BITS_1,
      .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
      .parity = (par == SKRIT_PAR_EVEN)  ? UART_CFG_PARITY_EVEN
                : (par == SKRIT_PAR_ODD) ? UART_CFG_PARITY_ODD
                                         : UART_CFG_PARITY_NONE,
  };
  uart_configure(data_dev, &cfg);
}
static void hal_serial_signal(void *c, uint8_t mask, uint8_t value) {
  (void)c;
  if ((mask & SKRIT_SIG_DTR) && device_is_ready(dtr_gpio.port))
    gpio_pin_set_dt(&dtr_gpio, (value & SKRIT_SIG_DTR) ? 1 : 0);
  if ((mask & SKRIT_SIG_RTS) && device_is_ready(rts_gpio.port))
    gpio_pin_set_dt(&rts_gpio, (value & SKRIT_SIG_RTS) ? 1 : 0);
  // BREAK on a hardware UART has no portable Zephyr API; left to a board hook.
}

#if defined(CONFIG_SOC_SERIES_NRF52X)
#include <hal/nrf_power.h>
#endif
static void hal_reboot(void *c, uint8_t mode) {
  (void)c;
#if defined(CONFIG_SOC_SERIES_NRF52X)
  if (mode == SKRIT_REBOOT_BOOTLOADER) {
    // 0x57 = the Adafruit/UF2 nRF52 bootloader's "enter DFU" magic in GPREGRET.
    nrf_power_gpregret_set(NRF_POWER, 0x57);
  }
#else
  (void)mode;
#endif
#if defined(CONFIG_REBOOT)
  sys_reboot(SYS_REBOOT_WARM);
#endif
}

static uint32_t hal_millis(void *c) {
  (void)c;
  return k_uptime_get_32();
}
static void hal_pump(void *c) {
  (void)c;
  k_yield();
}

static const skrit_hal HAL = {
    .name = "Duta nRF52840",
    .fw_ver = (FW_HI << 8) | FW_LO,
    .caps = SKRIT_CAP_MUX | SKRIT_CAP_SERIAL | SKRIT_CAP_REBOOT,
    .macro_tier = SKRIT_TIER_INTERACTIVE,
    .store_kb = 0,
    .n_outputs = 3,
    .n_inputs = 0,
    .link_write = hal_link_write,
    .data_write = hal_data_write,
    .host_write = NULL,
    .data_read = hal_data_read,
    .out_set = hal_out_set,
    .out_get = hal_out_get,
    .out_desc = hal_out_desc,
    .in_desc = NULL,
    .in_get = NULL,
    .serial_get = hal_serial_get,
    .serial_set = hal_serial_set,
    .serial_signal = hal_serial_signal,
    .reboot = hal_reboot,
    .millis = hal_millis,
    .pump = hal_pump,
};

int main(void) {
  for (int i = 0; i < 3; i++)
    if (device_is_ready(out_gpio[i].port)) gpio_pin_configure_dt(&out_gpio[i], GPIO_OUTPUT_INACTIVE);
  if (device_is_ready(dtr_gpio.port)) gpio_pin_configure_dt(&dtr_gpio, GPIO_OUTPUT_INACTIVE);
  if (device_is_ready(rts_gpio.port)) gpio_pin_configure_dt(&rts_gpio, GPIO_OUTPUT_INACTIVE);

#if HAVE_USB
  if (usb_enable(NULL)) printk("Duta: usb_enable failed\n");
#endif

  skrit_dev_init(&dev, &HAL, NULL, /*muxed*/ 1);

  if (!cmd_dev) {
    // native_sim / no transport: this is the CI build-link check.
    printk("Duta zephyr build-check (no CDC). PING=0x%02x tier=%d\n", SKRIT_PING, HAL.macro_tier);
    return 0;
  }

  for (;;) {
    skrit_dev_poll(&dev); // tee target console -> host
    unsigned char b;
    while (uart_poll_in(cmd_dev, &b) == 0) skrit_dev_rx(&dev, (uint8_t)b);
    k_msleep(1);
  }
  return 0;
}
