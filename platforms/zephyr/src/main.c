// Duta on Zephyr — nRF52840 (DK + dongle), and any Zephyr board.
// ============================================================================
// Built on the shared core (../../common/skrit_device.h). The skrit-mux byte
// stream (DATA + CMD over one channel) rides one of two transports, chosen at
// build time:
//
//   * USB CDC ACM (default) — a CDC ACM port; build with the board's normal
//     prj.conf / board overlay.
//   * BLE (Nordic UART Service) — enable with overlay-ble.conf
//     (`west build -- -DEXTRA_CONF_FILE=overlay-ble.conf`). CONFIG_BT selects it.
//
// A hardware UART (the `duta-data` alias) bridges the target console in both.
// On `native_sim` (the CI build-check) there is no transport, so main() runs a
// compile/link smoke check.
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

// ---- DATA console UART (the `duta-data` alias; both transports use it) -------
#if DT_NODE_EXISTS(DT_ALIAS(duta_data))
#define DATA_DEV DEVICE_DT_GET(DT_ALIAS(duta_data))
#else
#define DATA_DEV NULL
#endif
static const struct device *data_dev = DATA_DEV;

// ---- outputs: Relay 1, Relay 2, Aux LED (map to whatever LEDs/gpios exist) ---
// GPIO_DT_SPEC_GET_OR yields {0} for a missing alias; guarded by device_is_ready.
static const struct gpio_dt_spec out_gpio[3] = {
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(led1), gpios, {0}), // Relay 1
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(led2), gpios, {0}), // Relay 2
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0}), // Aux LED
};
static const char *const OUT_NAME[3] = {"Relay 1", "Relay 2", "Aux LED"};
static uint8_t g_out[3];

// optional target reset/boot lines (drive a target's DTR/RTS-equivalent pins)
static const struct gpio_dt_spec dtr_gpio = GPIO_DT_SPEC_GET_OR(DT_ALIAS(duta_dtr), gpios, {0});
static const struct gpio_dt_spec rts_gpio = GPIO_DT_SPEC_GET_OR(DT_ALIAS(duta_rts), gpios, {0});

static uint32_t g_baud = 115200;
static uint8_t g_bits = 8, g_parity = SKRIT_PAR_NONE, g_stop = 1;

static skrit_dev dev;

// ===========================================================================
// Transport: BLE (NUS) when CONFIG_BT, else USB CDC ACM
// ===========================================================================
#if defined(CONFIG_BT)
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

// BLE is dual-channel (like dual-CDC): a Nordic UART Service carries the raw
// DATA console (so plain BLE-UART terminals read it), and a sibling skrit CMD
// service carries the framed CMD protocol. Not muxed.
//   DATA = NUS:        6E40 0001 / 0002 (RX) / 0003 (TX)
//   CMD  = skrit svc:  6E41 0001 / 0002 (RX) / 0003 (TX)  (Nordic base, 6E41)
#define DATA_SVC BT_UUID_128_ENCODE(0x6e400001, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
#define CMD_SVC BT_UUID_128_ENCODE(0x6e410001, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e)
static struct bt_uuid_128 data_svc_uuid = BT_UUID_INIT_128(DATA_SVC);
static struct bt_uuid_128 data_rx_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x6e400002, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e));
static struct bt_uuid_128 data_tx_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x6e400003, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e));
static struct bt_uuid_128 cmd_svc_uuid = BT_UUID_INIT_128(CMD_SVC);
static struct bt_uuid_128 cmd_rx_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x6e410002, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e));
static struct bt_uuid_128 cmd_tx_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x6e410003, 0xb5a3, 0xf393, 0xe0a9, 0xe50e24dcca9e));

static struct bt_conn *ble_conn;
static bool data_subscribed, cmd_subscribed;

// host -> target console: DATA-RX writes go straight to the target UART.
static ssize_t data_rx(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
                       uint16_t len, uint16_t offset, uint8_t flags) {
  ARG_UNUSED(conn);
  ARG_UNUSED(attr);
  ARG_UNUSED(offset);
  ARG_UNUSED(flags);
  if (data_dev) {
    const uint8_t *p = buf;
    for (uint16_t i = 0; i < len; i++) uart_poll_out(data_dev, p[i]);
  }
  return len;
}
// host -> device CMD: CMD-RX writes feed the protocol dispatcher.
static ssize_t cmd_rx(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
                      uint16_t len, uint16_t offset, uint8_t flags) {
  ARG_UNUSED(conn);
  ARG_UNUSED(attr);
  ARG_UNUSED(offset);
  ARG_UNUSED(flags);
  const uint8_t *p = buf;
  for (uint16_t i = 0; i < len; i++) skrit_dev_rx(&dev, p[i]);
  return len;
}

static void data_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
  ARG_UNUSED(attr);
  data_subscribed = (value == BT_GATT_CCC_NOTIFY);
}
static void cmd_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
  ARG_UNUSED(attr);
  cmd_subscribed = (value == BT_GATT_CCC_NOTIFY);
}

// attrs per service: [0]=service [1]=TX decl [2]=TX value [3]=CCC [4]=RX decl
// [5]=RX value. We notify on the TX value attribute (attrs[2]).
BT_GATT_SERVICE_DEFINE(data_svc, BT_GATT_PRIMARY_SERVICE(&data_svc_uuid),
                       BT_GATT_CHARACTERISTIC(&data_tx_uuid.uuid, BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_NONE, NULL, NULL, NULL),
                       BT_GATT_CCC(data_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CHARACTERISTIC(&data_rx_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE, NULL, data_rx, NULL));
BT_GATT_SERVICE_DEFINE(cmd_svc, BT_GATT_PRIMARY_SERVICE(&cmd_svc_uuid),
                       BT_GATT_CHARACTERISTIC(&cmd_tx_uuid.uuid, BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_NONE, NULL, NULL, NULL),
                       BT_GATT_CCC(cmd_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       BT_GATT_CHARACTERISTIC(&cmd_rx_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE, NULL, cmd_rx, NULL));

static const struct bt_data ble_ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};
static const struct bt_data ble_sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, CMD_SVC), // scan rsp: the skrit CMD UUID identifies us
};

static void ble_advertise(void) {
  int err = bt_le_adv_start(BT_LE_ADV_CONN, ble_ad, ARRAY_SIZE(ble_ad), ble_sd, ARRAY_SIZE(ble_sd));
  if (err) printk("Duta: adv start failed (%d)\n", err);
}

static void ble_connected(struct bt_conn *conn, uint8_t err) {
  if (err) return;
  ble_conn = bt_conn_ref(conn);
}
static void ble_disconnected(struct bt_conn *conn, uint8_t reason) {
  ARG_UNUSED(conn);
  ARG_UNUSED(reason);
  if (ble_conn) {
    bt_conn_unref(ble_conn);
    ble_conn = NULL;
  }
  data_subscribed = cmd_subscribed = false;
  ble_advertise(); // become connectable again
}
BT_CONN_CB_DEFINE(ble_conn_cb) = {.connected = ble_connected, .disconnected = ble_disconnected};

// notify helper, chunked to the negotiated ATT MTU.
static void ble_notify(const struct bt_gatt_attr *tx_attr, bool subscribed, const uint8_t *p,
                       uint16_t n) {
  if (!ble_conn || !subscribed) return;
  uint16_t mtu = bt_gatt_get_mtu(ble_conn);
  uint16_t chunk = (mtu > 3) ? (uint16_t)(mtu - 3) : 20;
  while (n) {
    uint16_t c2 = n < chunk ? n : chunk;
    if (bt_gatt_notify(ble_conn, tx_attr, p, c2)) break; // buffers full -> drop (scaffold)
    p += c2;
    n -= c2;
  }
}
// device -> host CMD responses (link_write) and DATA console out (host_write).
static void hal_link_write(void *c, const uint8_t *p, uint16_t n) {
  ARG_UNUSED(c);
  ble_notify(&cmd_svc.attrs[2], cmd_subscribed, p, n);
}
static void ble_data_notify(void *c, const uint8_t *p, uint16_t n) {
  ARG_UNUSED(c);
  ble_notify(&data_svc.attrs[2], data_subscribed, p, n);
}

static void transport_start(void) {
  int err = bt_enable(NULL);
  if (err) {
    printk("Duta: bt_enable failed (%d)\n", err);
    return;
  }
  ble_advertise();
}

#define TRANSPORT_MUXED 0           // BLE is dual-channel (NUS DATA + CMD service)
#define TRANSPORT_CAP_MUX 0         // not muxed
#define TRANSPORT_HOST_WRITE ble_data_notify // dual: console out is its own pipe

#else // ---- USB CDC ACM transport (default) ----

#if DT_NODE_HAS_STATUS(DT_NODELABEL(cdc_acm_uart0), okay)
#include <zephyr/usb/usb_device.h>
#define CMD_DEV DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0))
#define HAVE_USB 1
#else
#define CMD_DEV NULL
#define HAVE_USB 0
#endif
static const struct device *cmd_dev = CMD_DEV;

static void hal_link_write(void *c, const uint8_t *p, uint16_t n) {
  ARG_UNUSED(c);
  if (!cmd_dev) return;
  for (uint16_t i = 0; i < n; i++) uart_poll_out(cmd_dev, p[i]);
}

static void transport_start(void) {
#if HAVE_USB
  if (usb_enable(NULL)) printk("Duta: usb_enable failed\n");
#endif
}

#define TRANSPORT_MUXED 1                // single CDC ACM carries both channels
#define TRANSPORT_CAP_MUX SKRIT_CAP_MUX
#define TRANSPORT_HOST_WRITE NULL        // muxed: core wraps console onto link_write
#endif // transport

// ---- DATA console + control HAL (shared by both transports) ----------------
static void hal_data_write(void *c, const uint8_t *p, uint16_t n) {
  ARG_UNUSED(c);
  if (!data_dev) return;
  for (uint16_t i = 0; i < n; i++) uart_poll_out(data_dev, p[i]);
}
static uint16_t hal_data_read(void *c, uint8_t *out, uint16_t cap) {
  ARG_UNUSED(c);
  if (!data_dev) return 0;
  uint16_t k = 0;
  unsigned char ch;
  while (k < cap && uart_poll_in(data_dev, &ch) == 0) out[k++] = ch;
  return k;
}

static void hal_out_set(void *c, uint8_t idx, uint8_t on) {
  ARG_UNUSED(c);
  if (idx > 2) return;
  g_out[idx] = on ? 1 : 0;
  if (device_is_ready(out_gpio[idx].port)) gpio_pin_set_dt(&out_gpio[idx], on ? 1 : 0);
}
static uint8_t hal_out_get(void *c, uint8_t idx) {
  ARG_UNUSED(c);
  return idx < 3 ? g_out[idx] : 0;
}
static void hal_out_desc(void *c, uint8_t idx, uint8_t *type, const char **name) {
  ARG_UNUSED(c);
  if (idx > 2) return;
  *type = SKRIT_CTRL_IO;
  *name = OUT_NAME[idx];
}

static void hal_serial_get(void *c, uint32_t *baud, uint8_t *bits, uint8_t *par, uint8_t *stop) {
  ARG_UNUSED(c);
  *baud = g_baud;
  *bits = g_bits;
  *par = g_parity;
  *stop = g_stop;
}
static void hal_serial_set(void *c, uint32_t baud, uint8_t bits, uint8_t par, uint8_t stop) {
  ARG_UNUSED(c);
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
  ARG_UNUSED(c);
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
  ARG_UNUSED(c);
#if defined(CONFIG_SOC_SERIES_NRF52X)
  if (mode == SKRIT_REBOOT_BOOTLOADER) {
    // 0x57 = the Adafruit/UF2 nRF52 bootloader's "enter DFU" magic in GPREGRET.
    nrf_power_gpregret_set(NRF_POWER, 0x57);
  }
#else
  ARG_UNUSED(mode);
#endif
#if defined(CONFIG_REBOOT)
  sys_reboot(SYS_REBOOT_WARM);
#endif
}

static uint32_t hal_millis(void *c) {
  ARG_UNUSED(c);
  return k_uptime_get_32();
}
static void hal_pump(void *c) {
  ARG_UNUSED(c);
  k_yield();
}

static const skrit_hal HAL = {
    .name = "Duta nRF52840",
    .fw_ver = (FW_HI << 8) | FW_LO,
    .caps = TRANSPORT_CAP_MUX | SKRIT_CAP_SERIAL | SKRIT_CAP_REBOOT,
    .macro_tier = SKRIT_TIER_INTERACTIVE,
    .store_kb = 0,
    .n_outputs = 3,
    .n_inputs = 0,
    .link_write = hal_link_write,
    .data_write = hal_data_write,
    .host_write = TRANSPORT_HOST_WRITE,
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

  skrit_dev_init(&dev, &HAL, NULL, /*muxed*/ TRANSPORT_MUXED);
  transport_start();

#if defined(CONFIG_BT)
  // BLE: received bytes arrive via the NUS write callback (nus_rx); the loop
  // only tees the target console out and lets the BT stack run.
  for (;;) {
    skrit_dev_poll(&dev);
    k_msleep(1);
  }
#else
  if (!cmd_dev) {
    // native_sim / no transport: this is the CI build-link check.
    printk("Duta zephyr build-check (no transport). PING=0x%02x\n", SKRIT_PING);
    return 0;
  }
  for (;;) {
    skrit_dev_poll(&dev); // tee target console -> host
    unsigned char b;
    while (uart_poll_in(cmd_dev, &b) == 0) skrit_dev_rx(&dev, (uint8_t)b);
    k_msleep(1);
  }
#endif
  return 0;
}
