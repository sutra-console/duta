// Duta on Zephyr: nRF52840 (DK + dongle), and any Zephyr board.
// ============================================================================
// Built on the shared core (../../common/skrit_device.h). The skrit-mux byte
// stream (DATA + CMD over one channel) rides one of two transports, chosen at
// build time:
//
//   * USB CDC ACM (default): a CDC ACM port; build with the board's normal
//     prj.conf / board overlay.
//   * BLE (Nordic UART Service): enable with overlay-ble.conf
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
#if DT_NODE_EXISTS(DT_PATH(zephyr_user)) && DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#include <zephyr/drivers/adc.h>
#endif
#if defined(CONFIG_REBOOT)
#include <zephyr/sys/reboot.h>
#endif

// The 802.15.4 sniffer streams whole MAC frames (up to 127 B) as one DATA record
// (~137 B); raise the device send-buffer cap so each rides a single mux frame.
#if defined(CONFIG_DUTA_SNIFF_154)
#define SKRIT_SEND_CAP 200
#endif
#include "skrit_device.h"
// Sniffer variants drive a radio directly and replace the UART DATA bridge with
// captured frames. The portable contract + backend selection lives in
// duta_sniffer.h: it binds the active backend to duta_sniffer_*() and defines
// DUTA_SNIFFER / DUTA_SNIFF_KIND / DUTA_SNIFF_HAS_TX|CHANNEL, so the loop below is
// fully backend-agnostic (no nRF/154 names here).
#include "duta_sniffer.h"

#define DUTA_N_GPIO_OUT 1 // onboard status LED (led0) — the onboard-only default
#ifdef DUTA_SNIFFER
#define SNIFF_OUT_IDX DUTA_N_GPIO_OUT // a virtual "Sniffing" output past the gpios
#define DUTA_N_OUTPUTS (DUTA_N_GPIO_OUT + 1)
#else
#define DUTA_N_OUTPUTS DUTA_N_GPIO_OUT
#endif

#define FW_LO 0x04
#define FW_HI 0x00

// ---- DATA console UART (the `duta-data` alias; both transports use it) -------
#if DT_NODE_EXISTS(DT_ALIAS(duta_data))
#define DATA_DEV DEVICE_DT_GET(DT_ALIAS(duta_data))
#else
#define DATA_DEV NULL
#endif
static const struct device *data_dev = DATA_DEV;

// ---- outputs: the board's onboard status LED only (led0). External fixtures
// (relays, etc.) are NOT a compiled default — add them at runtime via
// provisioning. GPIO_DT_SPEC_GET_OR yields {0} for a missing alias; guarded by
// device_is_ready. ----------------------------------------------------------
static const struct gpio_dt_spec out_gpio[DUTA_N_GPIO_OUT] = {
    GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0}), // onboard status LED
};
static const char *const OUT_NAME[DUTA_N_GPIO_OUT] = {"Status LED"};
static uint8_t g_out[DUTA_N_GPIO_OUT];

// optional target reset/boot lines (drive a target's DTR/RTS-equivalent pins)
static const struct gpio_dt_spec dtr_gpio = GPIO_DT_SPEC_GET_OR(DT_ALIAS(duta_dtr), gpios, {0});
static const struct gpio_dt_spec rts_gpio = GPIO_DT_SPEC_GET_OR(DT_ALIAS(duta_rts), gpios, {0});

// ---- optional ADC input (skrit input 0) — present when a board overlay gives a
// `zephyr,user` node with `io-channels` (see the promicro overlay). Reports the
// channel in millivolts. -----------------------------------------------------
#if DT_NODE_EXISTS(DT_PATH(zephyr_user)) && DT_NODE_HAS_PROP(DT_PATH(zephyr_user), io_channels)
#define HAVE_ADC 1
#define DUTA_N_INPUTS 1
static const struct adc_dt_spec adc_ch = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
#else
#define HAVE_ADC 0
#define DUTA_N_INPUTS 0
#endif

static uint32_t g_baud = 115200;
static uint8_t g_bits = 8, g_parity = SKRIT_PAR_NONE, g_stop = 1;

static skrit_dev dev;

// ===========================================================================
// Transport: BLE (GATT) when CONFIG_BT, else USB CDC ACM
// ===========================================================================
#if defined(CONFIG_BT)
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/ring_buffer.h> // queued TX so a full controller never drops bytes
#if defined(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h> // persist BLE bonds across reboots
#endif

// BLE is dual-channel (like dual-CDC): two skrit GATT services. DATA carries
// the raw console (its UUID is NUS-compatible so plain BLE-UART terminals read
// it); the sibling CMD service carries the framed CMD protocol. Not muxed.
//   DATA svc: 6E40 0001 / 0002 (RX) / 0003 (TX)   (NUS-compatible UUID)
//   CMD  svc: 6E41 0001 / 0002 (RX) / 0003 (TX)   (same vendor base, 6E41)
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
// The RX (write) and CCC (subscribe) attributes require an ENCRYPTED link, so a
// central must complete LESC pairing before it can drive the device or receive
// notifications — the air link is never plaintext. (The TX value attr stays
// PERM_NONE: it's only ever pushed via bt_gatt_notify, which checks the CCC.)
BT_GATT_SERVICE_DEFINE(data_svc, BT_GATT_PRIMARY_SERVICE(&data_svc_uuid),
                       BT_GATT_CHARACTERISTIC(&data_tx_uuid.uuid, BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_NONE, NULL, NULL, NULL),
                       BT_GATT_CCC(data_ccc_changed,
                                   BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
                       BT_GATT_CHARACTERISTIC(&data_rx_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE_ENCRYPT, NULL, data_rx, NULL));
BT_GATT_SERVICE_DEFINE(cmd_svc, BT_GATT_PRIMARY_SERVICE(&cmd_svc_uuid),
                       BT_GATT_CHARACTERISTIC(&cmd_tx_uuid.uuid, BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_NONE, NULL, NULL, NULL),
                       BT_GATT_CCC(cmd_ccc_changed,
                                   BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
                       BT_GATT_CHARACTERISTIC(&cmd_rx_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE_ENCRYPT, NULL, cmd_rx, NULL));

// Per-unit advertised name "Duta-XXXX", filled from the BLE address at runtime.
static char duta_name[16] = "Duta";
static const struct bt_data ble_sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, CMD_SVC), // scan rsp: the skrit CMD UUID identifies us
};

// Zephyr 4.3 removed the deprecated BT_LE_ADV_CONN; FAST_2 (100-150ms interval)
// is the recommended replacement and exists on 4.2 too.
#ifndef BT_LE_ADV_CONN
#define BT_LE_ADV_CONN BT_LE_ADV_CONN_FAST_2
#endif

static void ble_advertise(void) {
  // Build the adv data each time so it carries the current dynamic name.
  struct bt_data ad[] = {
      BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
      BT_DATA(BT_DATA_NAME_COMPLETE, duta_name, strlen(duta_name)),
  };
  int err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), ble_sd, ARRAY_SIZE(ble_sd));
  if (err) printk("Duta: adv start failed (%d)\n", err);
}

// Give each unit a distinct name from its BLE address: "Duta-XXXX". Sets both the
// advertised name and the GAP Device Name characteristic.
static void set_device_name(void) {
  bt_addr_le_t addr;
  size_t count = 1;
  bt_id_get(&addr, &count);
  snprintk(duta_name, sizeof duta_name, "Duta-%02X%02X", addr.a.val[1], addr.a.val[0]);
  bt_set_name(duta_name);
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

// Device -> host TX is queued, not pushed straight to the radio: bt_gatt_notify
// fails when the controller's buffers are full, and the old path dropped those
// bytes (corrupting a COBS frame). Instead each pipe has a ring buffer that the
// BLE loop drains, retrying when buffers free up — no lost console/CMD bytes
// under burst. Pushes can come from the BT RX thread (CMD responses) and the
// main loop (events / DATA), so the put side is spinlock-guarded; the single
// consumer is the loop's txq_drain.
RING_BUF_DECLARE(cmd_tx_rb, 768);
RING_BUF_DECLARE(data_tx_rb, 1024);
static struct k_spinlock tx_lock;

static void txq_push(struct ring_buf *rb, const uint8_t *p, uint16_t n) {
  k_spinlock_key_t k = k_spin_lock(&tx_lock);
  ring_buf_put(rb, p, n); // bounded: extreme overrun drops the tail, never corrupts the head
  k_spin_unlock(&tx_lock, k);
}

// Drain one pipe to its TX characteristic, chunked to the negotiated ATT MTU.
// Peek -> notify -> consume-on-success, so a full controller just retries later.
static void txq_drain(struct ring_buf *rb, const struct bt_gatt_attr *tx_attr, bool subscribed) {
  if (!ble_conn) return;
  if (!subscribed) { // nobody listening: discard so the queue can't wedge
    k_spinlock_key_t k = k_spin_lock(&tx_lock);
    ring_buf_reset(rb);
    k_spin_unlock(&tx_lock, k);
    return;
  }
  uint16_t mtu = bt_gatt_get_mtu(ble_conn);
  uint16_t chunk = (mtu > 3) ? (uint16_t)(mtu - 3) : 20;
  uint8_t tmp[247];
  if (chunk > sizeof tmp) chunk = sizeof tmp;
  for (;;) {
    k_spinlock_key_t k = k_spin_lock(&tx_lock);
    uint32_t got = ring_buf_peek(rb, tmp, chunk);
    k_spin_unlock(&tx_lock, k);
    if (got == 0) break;
    if (bt_gatt_notify(ble_conn, tx_attr, tmp, (uint16_t)got)) break; // full: leave queued, retry
    k = k_spin_lock(&tx_lock);
    ring_buf_get(rb, NULL, got); // consume the bytes we just sent
    k_spin_unlock(&tx_lock, k);
  }
}
static void ble_tx_drain(void) {
  txq_drain(&cmd_tx_rb, &cmd_svc.attrs[2], cmd_subscribed);
  txq_drain(&data_tx_rb, &data_svc.attrs[2], data_subscribed);
}

// device -> host CMD responses (link_write) and DATA console out (host_write).
static void hal_link_write(void *c, const uint8_t *p, uint16_t n) {
  ARG_UNUSED(c);
  txq_push(&cmd_tx_rb, p, n);
}
static void ble_data_notify(void *c, const uint8_t *p, uint16_t n) {
  ARG_UNUSED(c);
  txq_push(&data_tx_rb, p, n);
}

static void transport_start(void) {
  int err = bt_enable(NULL);
  if (err) {
    printk("Duta: bt_enable failed (%d)\n", err);
    return;
  }
#if defined(CONFIG_SETTINGS)
  settings_load(); // restore persisted bonds so paired hosts reconnect without re-pairing
#endif
  set_device_name();
  ble_advertise();
}

#define TRANSPORT_MUXED 0           // BLE is dual-channel (DATA + CMD GATT services)
#define TRANSPORT_CAP_MUX 0         // not muxed
#define TRANSPORT_HOST_WRITE ble_data_notify // dual: console out is its own pipe

#else // ---- USB CDC ACM transport (default) ----

// The CDC ACM that carries the mux link. Our overlays add `cdc_acm_uart0`; boards
// that include Zephyr's common cdc_acm_serial.dtsi (e.g. promicro_nrf52840) name
// it `board_cdc_acm_uart` instead — accept either.
#if DT_NODE_HAS_STATUS(DT_NODELABEL(cdc_acm_uart0), okay)
#include <zephyr/usb/usb_device.h>
#define CMD_DEV DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0))
#define HAVE_USB 1
#elif DT_NODE_HAS_STATUS(DT_NODELABEL(board_cdc_acm_uart), okay)
#include <zephyr/usb/usb_device.h>
#define CMD_DEV DEVICE_DT_GET(DT_NODELABEL(board_cdc_acm_uart))
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
#ifdef DUTA_SNIFF_HAS_TX
  // The "wire" is the radio: a host DATA write is a frame to inject over the air.
  duta_sniffer_tx(p, n);
  return;
#endif
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
#ifdef DUTA_SNIFFER
  if (idx == SNIFF_OUT_IDX) { // virtual "Sniffing" toggle drives the radio
    if (on) duta_sniffer_start();
    else duta_sniffer_stop();
    return;
  }
#endif
  if (idx >= DUTA_N_GPIO_OUT) return;
  g_out[idx] = on ? 1 : 0;
  if (device_is_ready(out_gpio[idx].port)) gpio_pin_set_dt(&out_gpio[idx], on ? 1 : 0);
}
static uint8_t hal_out_get(void *c, uint8_t idx) {
  ARG_UNUSED(c);
#ifdef DUTA_SNIFFER
  if (idx == SNIFF_OUT_IDX) return duta_sniffer_is_on() ? 1 : 0;
#endif
  return idx < DUTA_N_GPIO_OUT ? g_out[idx] : 0;
}
static void hal_out_desc(void *c, uint8_t idx, uint8_t *type, const char **name) {
  ARG_UNUSED(c);
#ifdef DUTA_SNIFFER
  if (idx == SNIFF_OUT_IDX) {
    *type = SKRIT_CTRL_IO;
    *name = "Sniffing";
    return;
  }
#endif
  if (idx >= DUTA_N_GPIO_OUT) return;
  *type = SKRIT_CTRL_IO;
  *name = OUT_NAME[idx];
}

#if HAVE_ADC
static void hal_in_desc(void *c, uint8_t idx, uint8_t *type, const char **name) {
  ARG_UNUSED(c);
  if (idx >= DUTA_N_INPUTS) return;
  *type = SKRIT_IN_ANALOG;
  *name = "ADC (mV)";
}
static uint16_t hal_in_get(void *c, uint8_t idx) {
  ARG_UNUSED(c);
  if (idx >= DUTA_N_INPUTS) return 0;
  int16_t raw = 0;
  struct adc_sequence seq = {.buffer = &raw, .buffer_size = sizeof raw};
  if (adc_sequence_init_dt(&adc_ch, &seq) != 0 || adc_read_dt(&adc_ch, &seq) != 0) return 0;
  int32_t mv = raw;
  if (adc_raw_to_millivolts_dt(&adc_ch, &mv) != 0) mv = raw; // unscaled fallback
  return (uint16_t)(mv < 0 ? 0 : (mv > 0xFFFF ? 0xFFFF : mv));
}
#endif

static void hal_proto_get(void *c, uint8_t idx, uint32_t *value, uint8_t *o0, uint8_t *o1, uint8_t *o2) {
  ARG_UNUSED(c);
  ARG_UNUSED(idx); // single interface
#ifdef DUTA_SNIFF_HAS_CHANNEL
  // No UART here — a radio sniffer's link param is its channel (11..26; 0 = hop).
  *value = duta_sniffer_get_channel();
  *o0 = 0;
  *o1 = 0;
  *o2 = 0;
  return;
#endif
  *value = g_baud; // uart: value=baud, opt0=data_bits, opt1=parity, opt2=stop_bits
  *o0 = g_bits;
  *o1 = g_parity;
  *o2 = g_stop;
}
static void hal_proto_set(void *c, uint8_t idx, uint32_t value, uint8_t o0, uint8_t o1, uint8_t o2) {
  ARG_UNUSED(c);
  ARG_UNUSED(idx);
#ifdef DUTA_SNIFF_HAS_CHANNEL
  ARG_UNUSED(o0);
  ARG_UNUSED(o1);
  ARG_UNUSED(o2);
  duta_sniffer_set_channel((uint8_t)value); // value carries the 802.15.4 channel
  return;
#endif
  if (value) g_baud = value;
  if (o0) g_bits = o0;
  g_parity = o1;
  if (o2) g_stop = o2;
  if (!data_dev || !device_is_ready(data_dev)) return;
  struct uart_config cfg = {
      .baudrate = g_baud,
      .data_bits = (g_bits == 7) ? UART_CFG_DATA_BITS_7 : UART_CFG_DATA_BITS_8,
      .stop_bits = (g_stop == 2) ? UART_CFG_STOP_BITS_2 : UART_CFG_STOP_BITS_1,
      .flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
      .parity = (o1 == SKRIT_PAR_EVEN)  ? UART_CFG_PARITY_EVEN
                : (o1 == SKRIT_PAR_ODD) ? UART_CFG_PARITY_ODD
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

// The Zephyr SoC-series symbol for the nRF52 family is CONFIG_SOC_SERIES_NRF52
// (NOT ..._NRF52X — that name does not exist here). Guarding on the wrong symbol
// silently compiled this block out, so REBOOT mode=1 only did an app reset.
#if defined(CONFIG_SOC_SERIES_NRF52) || defined(CONFIG_SOC_SERIES_NRF52X)
#include <hal/nrf_power.h> // brings in the MDK: NRF_POWER + GPREGRET
#define DUTA_HAVE_NRF_DFU_REBOOT 1
#endif
static void hal_reboot(void *c, uint8_t mode) {
  ARG_UNUSED(c);
#if defined(DUTA_HAVE_NRF_DFU_REBOOT)
  if (mode == SKRIT_REBOOT_BOOTLOADER) {
    // 0x57 = the Adafruit/UF2 nRF52 bootloader's "enter DFU" magic in GPREGRET.
    // Write the register directly — the nrfx hal's gpregret setter signature has
    // churned (2-arg vs 3-arg w/ reg_num); the register write is version-proof.
    NRF_POWER->GPREGRET = 0x57;
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

// ---- demo INVOKE commands (user-defined; proves the framework extension point
// on real hardware). The device owns these; Sutra forwards intents blind.
//   0x0001 set_position(x:u16,y:u16)  well-known — blips the status LED as an ack
//   0x8001 blink(times:u8)          vendor — blink the status LED `times` (visible)
//   0x8002 echo(bytes)->reply       vendor — echo the payload back (reply path)
static void led_pulse(uint16_t on_ms, uint16_t off_ms) {
  if (!device_is_ready(out_gpio[0].port)) return;
  gpio_pin_set_dt(&out_gpio[0], 1);
  k_msleep(on_ms);
  gpio_pin_set_dt(&out_gpio[0], 0);
  if (off_ms) k_msleep(off_ms);
}
static uint8_t hal_cmd_desc(void *c, uint8_t index, uint16_t *id, uint8_t *nargs,
                            uint8_t *argtype, uint8_t *flags, const char **name) {
  ARG_UNUSED(c);
  switch (index) {
  case 0:
    *id = SKRIT_INVOKE_SET_POSITION; *nargs = 2;
    argtype[0] = SKRIT_ARG_U16; argtype[1] = SKRIT_ARG_U16;
    *flags = 0; *name = "set_position";
    break;
  case 1:
    *id = SKRIT_INVOKE_VENDOR_BASE + 1; *nargs = 1;
    argtype[0] = SKRIT_ARG_U8; *flags = 0; *name = "blink";
    break;
  case 2:
    *id = SKRIT_INVOKE_VENDOR_BASE + 2; *nargs = 1;
    argtype[0] = SKRIT_ARG_BYTES; *flags = SKRIT_INVOKE_REPLY; *name = "echo";
    break;
  default:
    break;
  }
  return 3;
}
static uint8_t hal_cmd_invoke(void *c, uint16_t id, const uint8_t *p, uint8_t plen,
                              uint8_t *reply, uint8_t cap, uint8_t *rlen) {
  ARG_UNUSED(c);
  *rlen = 0;
  if (id == SKRIT_INVOKE_SET_POSITION) {
    if (plen < 4) return SKRIT_ST_BADARGS; // x(u16), y(u16)
    led_pulse(60, 0);
    return SKRIT_ST_OK;
  }
  if (id == SKRIT_INVOKE_VENDOR_BASE + 1) { // blink(times)
    uint8_t times = plen ? p[0] : 1;
    if (times > 10) times = 10;
    for (uint8_t i = 0; i < times; i++) led_pulse(120, 120);
    return SKRIT_ST_OK;
  }
  if (id == SKRIT_INVOKE_VENDOR_BASE + 2) { // echo(len(1), bytes) -> reply bytes
    if (plen < 1) return SKRIT_ST_BADARGS;
    uint8_t n = p[0];
    if ((uint16_t)n + 1 > plen) return SKRIT_ST_BADARGS;
    if (n > cap) n = cap;
    for (uint8_t i = 0; i < n; i++) reply[i] = p[1 + i];
    *rlen = n;
    return SKRIT_ST_OK;
  }
  return SKRIT_ST_NOTFOUND;
}

static const skrit_hal HAL = {
    .name = "Duta nRF52840",
    .fw_ver = (FW_HI << 8) | FW_LO,
#ifdef DUTA_SNIFFER
    .data_kind = DUTA_SNIFF_KIND, // DATA is captured radio frames, not a UART console
#endif
    .caps = TRANSPORT_CAP_MUX | SKRIT_CAP_SERIAL | SKRIT_CAP_REBOOT,
    .macro_tier = SKRIT_TIER_INTERACTIVE,
    .store_kb = 0,
    .n_outputs = DUTA_N_OUTPUTS,
    .n_inputs = DUTA_N_INPUTS,
    .link_write = hal_link_write,
    .data_write = hal_data_write,
    .host_write = TRANSPORT_HOST_WRITE,
    .data_read = hal_data_read,
    .out_set = hal_out_set,
    .out_get = hal_out_get,
    .out_desc = hal_out_desc,
#if HAVE_ADC
    .in_desc = hal_in_desc,
    .in_get = hal_in_get,
#else
    .in_desc = NULL,
    .in_get = NULL,
#endif
    .proto_get = hal_proto_get,
    .proto_set = hal_proto_set,
    .serial_signal = hal_serial_signal,
    .reboot = hal_reboot,
    .millis = hal_millis,
    .pump = hal_pump,
    .cmd_desc = hal_cmd_desc,   // advertises SKRIT_FLAG_INVOKE + the demo commands
    .cmd_invoke = hal_cmd_invoke,
};

int main(void) {
  for (int i = 0; i < DUTA_N_GPIO_OUT; i++)
    if (device_is_ready(out_gpio[i].port)) gpio_pin_configure_dt(&out_gpio[i], GPIO_OUTPUT_INACTIVE);
  if (device_is_ready(dtr_gpio.port)) gpio_pin_configure_dt(&dtr_gpio, GPIO_OUTPUT_INACTIVE);
  if (device_is_ready(rts_gpio.port)) gpio_pin_configure_dt(&rts_gpio, GPIO_OUTPUT_INACTIVE);
#if HAVE_ADC
  if (adc_is_ready_dt(&adc_ch)) adc_channel_setup_dt(&adc_ch);
#endif

  skrit_dev_init(&dev, &HAL, NULL, /*muxed*/ TRANSPORT_MUXED);
  transport_start();

#if defined(CONFIG_BT)
  // BLE: received bytes arrive via the GATT write callbacks (data_rx/cmd_rx); the
  // loop tees the target console out (queued into the TX rings) and drains those
  // rings to the radio, retrying when the controller's buffers free up.
  for (;;) {
    skrit_dev_poll(&dev);
    ble_tx_drain();
    k_msleep(1);
  }
#else
  if (!cmd_dev) {
    // native_sim / no transport: this is the CI build-link check.
    printk("Duta zephyr build-check (no transport). PING=0x%02x\n", SKRIT_PING);
    return 0;
  }
#ifdef DUTA_SNIFFER
  // Sniffer build: DATA is captured radio frames (no UART). Poll the radio and
  // emit each frame as a DATA record; CMD still works over the same mux link.
  // The "Sniffing" virtual output (SNIFF_OUT_IDX) starts/stops capture; default on.
  // rec must hold the largest record: 802.15.4 is 9 + up-to-127-byte PSDU.
  duta_sniffer_init();
  duta_sniffer_start();
  static uint8_t rec[160]; // static: keep the 802.15.4-sized record off the stack
  for (;;) {
    uint16_t n;
    while ((n = duta_sniffer_take(rec, sizeof rec)) != 0) skrit_dev_feed_data(&dev, rec, n);
    unsigned char b;
    while (uart_poll_in(cmd_dev, &b) == 0) skrit_dev_rx(&dev, (uint8_t)b);
    k_yield();
  }
#else
  for (;;) {
    skrit_dev_poll(&dev); // tee target console -> host
    unsigned char b;
    while (uart_poll_in(cmd_dev, &b) == 0) skrit_dev_rx(&dev, (uint8_t)b);
    k_msleep(1);
  }
#endif
#endif
  return 0;
}
