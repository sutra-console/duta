// Duta on ESP32 / ESP32-S3 / ESP32-C3 (Arduino + PlatformIO).
// ============================================================================
// A real Duta adapter built on the shared core (../../common/skrit_device.h):
//
//   * DATA console  : a hardware UART (Serial1 on DATA_TX/DATA_RX) bridged to
//                     the target, full duplex.
//   * CMD + DATA    : multiplexed over the single native USB-CDC via skrit-mux.
//   * Controls      : declared as a table in board.h, driven by the shared
//                     duta_io driver (relays, PWM LED, addressable RGB).
//   * Macro VM      : skrit-mc tiers 1-2; scratch push-and-run.
//   * Serial control: SERIAL_GET/SET reconfigures the DATA UART; SERIAL_SIGNAL
//                     drives DTR/RTS (auto-reset into ESP/AVR bootloaders) + BREAK.
//   * REBOOT        : app reset, or reboot-to-download on S3/C3.
//
// IO is table-driven (board.h `duta_outputs[]` + duta_io_arduino.h); this file
// is just the transport, serial, and reboot HAL plus the two-line main loop.
#include <Arduino.h>

#include "board.h"            // declares the duta_outputs[] table + mcu/board maps

// Runtime provisioning: the IO table can be re-provisioned from the app and
// persisted in NVS (Preferences). These store hooks must be defined before
// duta_io_arduino.h (its loader calls them). See PROTOCOL.md "Provisioning".
#define DUTA_PROVISION
#define DUTA_HAVE_STORE
#include <Preferences.h>
static Preferences duta_prefs;
static uint16_t duta_io_store_load(uint8_t *buf, uint16_t cap) {
  if (!duta_prefs.begin("duta", true)) return 0;
  size_t n = duta_prefs.getBytesLength("io");
  uint16_t got = (n && n <= cap) ? (uint16_t)duta_prefs.getBytes("io", buf, n) : 0;
  duta_prefs.end();
  return got;
}
static uint8_t duta_io_store_save(const uint8_t *buf, uint16_t n) {
  if (!duta_prefs.begin("duta", false)) return 0;
  size_t w = duta_prefs.putBytes("io", buf, n);
  duta_prefs.end();
  return w == n;
}
static void duta_io_store_clear(void) {
  if (!duta_prefs.begin("duta", false)) return;
  duta_prefs.remove("io");
  duta_prefs.end();
}

#include "duta_io_arduino.h"  // generic IO driver -> skrit_hal IO callbacks
extern "C" {
#include "skrit_device.h"
}

// Sniffer variants (e.g. -DDUTA_SNIFF_BLE_ESP): the portable contract + backend
// selection. When a backend is selected this defines DUTA_SNIFFER / DUTA_SNIFF_KIND
// and binds duta_sniffer_*(); DATA becomes captured radio frames instead of the
// UART bridge, and WiFi/I2C are compiled out to leave room for the radio stack.
#include "duta_sniffer.h"

#include "driver/gpio.h" // gpio_pullup_en — pull the DATA RX line when unwired
#ifndef DUTA_SNIFFER
#include "duta_i2c.h"    // I2C master DATA backend (Wire) — the first non-UART medium
#include "duta_wifi.h"   // WiFi + WebSocket bridge + captive portal (second skrit_dev)
#endif

#ifndef DUTA_SNIFFER // UART/I2C DATA bridge + WiFi config — replaced by the radio sniffer
// The bridged medium: SKRIT_DATA_UART (console tee) or SKRIT_DATA_I2C (master +
// transaction records). Switched via CFG_DATA_KIND, persisted in NVS ("dkind").
static uint8_t g_data_kind = SKRIT_DATA_UART;
static Preferences kind_prefs;

static void data_kind_apply(uint8_t kind); // defined after the HAL (it updates it)
static void data_kind_load(void) {
  uint8_t kind = SKRIT_DATA_UART;
  if (kind_prefs.begin("duta", true)) {
    kind = (uint8_t)kind_prefs.getUChar("dkind", SKRIT_DATA_UART);
    kind_prefs.end();
  }
  data_kind_apply(kind == SKRIT_DATA_I2C ? SKRIT_DATA_I2C : SKRIT_DATA_UART);
}
static void data_kind_save(uint8_t kind) {
  if (!kind_prefs.begin("duta", false)) return;
  kind_prefs.putUChar("dkind", kind);
  kind_prefs.end();
}

// CFG_GET/CFG_SET: DATA pins + kind answered here; WiFi keys route to the module.
static int16_t hal_cfg_get(void *, uint8_t key, uint8_t *out, uint8_t cap) {
  if (key == SKRIT_CFG_DATA_PINS) {
    if (cap < 4) return -1;
    int16_t tx = DATA_TX_PIN, rx = DATA_RX_PIN;
    out[0] = (uint8_t)tx;
    out[1] = (uint8_t)(tx >> 8);
    out[2] = (uint8_t)rx;
    out[3] = (uint8_t)(rx >> 8);
    return 4;
  }
  if (key == SKRIT_CFG_DATA_KIND) {
    if (cap < 1) return -1;
    out[0] = g_data_kind;
    return 1;
  }
  return duta_wifi_cfg_get(key, out, cap);
}
static uint8_t hal_cfg_set(void *, uint8_t key, const uint8_t *val, uint8_t len) {
  if (key == SKRIT_CFG_DATA_KIND) {
    if (len != 1) return SKRIT_ST_BADARGS;
    if (val[0] != SKRIT_DATA_UART && val[0] != SKRIT_DATA_I2C) return SKRIT_ST_BADARGS;
    data_kind_apply(val[0]);
    data_kind_save(val[0]);
    return SKRIT_ST_OK;
  }
  return duta_wifi_cfg_set(key, val, len);
}
#endif // !DUTA_SNIFFER

// (I2C HAL shims live below the dev declarations — they fan records to the links.)

#if defined(BOARD_ESP32S3) || defined(BOARD_S3_ZERO) || defined(BOARD_ESP32C3)
#include "soc/rtc_cntl_reg.h" // RTC_CNTL_OPTION1_REG: force download (DFU) boot
#endif

#define FW_LO 0x04
#define FW_HI 0x00

// Serial1 is the bridged target UART. On the classic ESP32 it also exists; on
// S3/C3 the USB-CDC is `Serial`, leaving Serial1 free for the console.
static HardwareSerial &TARGET = Serial1;

static uint32_t g_baud = 115200;
static uint8_t g_bits = 8, g_parity = SKRIT_PAR_NONE, g_stop = 1;

static skrit_dev dev;

#ifndef DUTA_SNIFFER
// ---- I2C HAL shims: do the transfer, then fan the record out to every link --
static void i2c_emit_record(uint8_t addr, uint8_t flags, const uint8_t *w, uint8_t wlen,
                            const uint8_t *r, uint8_t rlen) {
  uint8_t rec[8 + 255];
  uint16_t n = duta_i2c_record(rec, sizeof rec, addr, flags, w, wlen, r, rlen);
  if (!n) return;
  skrit_dev_feed_data(&dev, rec, n); // auth/gating is per-link inside feed
  skrit_dev_feed_data(&ws_dev, rec, n);
}
static uint8_t hal_i2c_scan(void *, uint8_t bitmap[16]) { return duta_i2c_scan(bitmap); }
static uint8_t hal_i2c_xfer(void *, uint8_t addr, const uint8_t *w, uint8_t wlen, uint8_t *r,
                            uint8_t rlen) {
  uint8_t st = duta_i2c_xfer(addr, w, wlen, r, rlen);
  uint8_t flags = (uint8_t)((rlen ? 1 : 0) | (st != SKRIT_ST_OK ? 2 : 0));
  if (g_data_kind == SKRIT_DATA_I2C && st != SKRIT_ST_UNSUPPORTED)
    i2c_emit_record(addr, flags, w, wlen, st == SKRIT_ST_OK ? r : NULL,
                    st == SKRIT_ST_OK ? rlen : 0);
  return st;
}
#endif // !DUTA_SNIFFER

// Translate (bits,parity,stop) into the Arduino SerialConfig word, covering the
// configs people actually use; falls back to 8N1.
static uint32_t serialConfig(uint8_t bits, uint8_t par, uint8_t stop) {
  if (bits == 7 && par == SKRIT_PAR_EVEN && stop == 1) return SERIAL_7E1;
  if (bits == 7 && par == SKRIT_PAR_ODD && stop == 1) return SERIAL_7O1;
  if (bits == 8 && par == SKRIT_PAR_EVEN && stop == 1) return SERIAL_8E1;
  if (bits == 8 && par == SKRIT_PAR_ODD && stop == 1) return SERIAL_8O1;
  if (bits == 8 && par == SKRIT_PAR_NONE && stop == 2) return SERIAL_8N2;
  return SERIAL_8N1;
}

static void beginTarget() {
  TARGET.begin(g_baud, serialConfig(g_bits, g_parity, g_stop), DATA_RX_PIN, DATA_TX_PIN);
  // Pull the RX line up (without disturbing the UART matrix routing): a
  // floating RX with no target wired streams garbage into the bridge.
  gpio_pullup_en((gpio_num_t)DATA_RX_PIN);
}

// ---- transport + serial HAL (IO callbacks come from duta_io_arduino.h) ------
// HWCDC already FIFO-drops when no host is draining; the TX timeout set in
// setup() caps any residual stall, so this never blocks the loop.
static void hal_link_write(void *, const uint8_t *p, uint16_t n) { Serial.write(p, n); }
#ifdef DUTA_SNIFFER
static void hal_data_write(void *, const uint8_t *, uint16_t) {} // sniffer DATA is RX-only
#else
static void hal_data_write(void *, const uint8_t *p, uint16_t n) { TARGET.write(p, n); }
#endif

static uint16_t hal_data_read(void *, uint8_t *out, uint16_t cap) {
  uint16_t k = 0;
  while (TARGET.available() && k < cap) out[k++] = (uint8_t)TARGET.read();
  return k;
}

static void hal_proto_get(void *, uint8_t idx, uint32_t *value, uint8_t *o0, uint8_t *o1, uint8_t *o2) {
  (void)idx; // single interface; uart: value=baud, opt0=data_bits, opt1=parity, opt2=stop
  *value = g_baud;
  *o0 = g_bits;
  *o1 = g_parity;
  *o2 = g_stop;
}
static void hal_proto_set(void *, uint8_t idx, uint32_t value, uint8_t o0, uint8_t o1, uint8_t o2) {
  (void)idx;
  if (value) g_baud = value;
  if (o0) g_bits = o0;
  g_parity = o1;
  if (o2) g_stop = o2;
  TARGET.flush();
  TARGET.end();
  beginTarget();
}

static void hal_serial_signal(void *, uint8_t mask, uint8_t value) {
  if ((mask & SKRIT_SIG_DTR) && DTR_PIN >= 0)
    digitalWrite(DTR_PIN, (value & SKRIT_SIG_DTR) ? HIGH : LOW);
  if ((mask & SKRIT_SIG_RTS) && RTS_PIN >= 0)
    digitalWrite(RTS_PIN, (value & SKRIT_SIG_RTS) ? HIGH : LOW);
  if ((mask & SKRIT_SIG_BREAK) && (value & SKRIT_SIG_BREAK)) {
    // Best-effort line break: hold TX low ~1 bit-frame, then restore the UART.
    TARGET.flush();
    TARGET.end();
    pinMode(DATA_TX_PIN, OUTPUT);
    digitalWrite(DATA_TX_PIN, LOW);
    delay(2);
    beginTarget();
  }
}

static void hal_reboot(void *, uint8_t mode) {
  Serial.flush();
#if defined(BOARD_ESP32S3) || defined(BOARD_S3_ZERO) || defined(BOARD_ESP32C3)
  if (mode == SKRIT_REBOOT_BOOTLOADER) {
    // Latch the ROM download/DFU path, then reset; the native USB re-enumerates
    // as the serial/JTAG downloader (no BOOT-button dance needed).
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  }
#endif
  esp_restart();
}

static uint32_t hal_millis(void *) { return millis(); }
static void hal_pump(void *) { yield(); }

// caps: advertise PWM only if the active table actually has a PWM output. Runs
// after duta_io_begin(), so duta_tbl reflects any provisioned table.
static uint8_t board_caps() {
  uint8_t caps = SKRIT_CAP_MUX | SKRIT_CAP_SERIAL | SKRIT_CAP_REBOOT;
  for (uint8_t i = 0; i < duta_tbl_n; i++)
    if (duta_tbl[i].type == SKRIT_CTRL_PWM) caps |= SKRIT_CAP_PWM;
  return caps;
}

static skrit_hal HAL = {
    /*name*/ BOARD_NAME,
    /*fw_ver*/ (FW_HI << 8) | FW_LO,
    /*caps*/ 0, // filled in setup() from the board table
    /*macro_tier*/ SKRIT_TIER_INTERACTIVE,
    /*store_kb*/ 0,
    /*n_outputs*/ DUTA_N_OUTPUTS,
    /*n_inputs*/ DUTA_N_INPUTS,
    hal_link_write, hal_data_write, /*host_write*/ nullptr, hal_data_read,
    duta_io_out_set, duta_io_out_get, duta_io_out_desc,
    duta_io_pwm_set, duta_io_pwm_get,
    duta_io_rgb_count, duta_io_rgb_set, duta_io_rgb_get,
#ifdef DUTA_HAVE_INPUTS
    duta_io_in_desc, duta_io_in_get,
#else
    /*in_desc*/ nullptr, /*in_get*/ nullptr,
#endif
    hal_proto_get, hal_proto_set, hal_serial_signal,
    hal_reboot,
    hal_millis, hal_pump,
};

#ifndef DUTA_SNIFFER
// Switch the bridged medium live: both devs read their HALs by pointer.
static void data_kind_apply(uint8_t kind) {
  g_data_kind = kind;
  HAL.data_kind = kind;
  WS_HAL.data_kind = kind;
  if (kind == SKRIT_DATA_I2C) duta_i2c_begin();
  else duta_i2c_end();
}
#endif

// ---- Arduino entry points --------------------------------------------------
#ifdef DUTA_SNIFFER
// Sniffer build (e.g. -DDUTA_SNIFF_BLE_ESP): DATA is captured radio frames, not a
// UART/I2C bridge, and there's no WiFi — just the mux + CMD over USB-CDC and the
// radio backend behind duta_sniffer.h. Mirrors the Zephyr sniffer main loop.
void setup() {
  duta_io_begin(); // onboard RGB/LED sign-of-life from the board table
  Serial.begin(115200);
#if BOARD_HAS_NATIVE_USB
  Serial.setTxTimeoutMs(50); // HWCDC: cap stalls so a quiet host can't wedge the loop
#endif
  HAL.n_outputs = duta_tbl_n;
  HAL.caps = board_caps();
  HAL.data_kind = DUTA_SNIFF_KIND; // DATA is captured radio frames
  HAL.pwm_config_get = duta_io_pwm_config_get;
  HAL.pwm_config_set = duta_io_pwm_config_set;
  HAL.pin_caps = duta_io_pin_caps;
  HAL.config_get = duta_io_config_get;
  HAL.config_set = duta_io_config_set;
  skrit_dev_init(&dev, &HAL, nullptr, /*muxed*/ 1);
  duta_sniffer_init();
  duta_sniffer_start();
}

void loop() {
  uint8_t rec[2 + 64]; // one ble-sniff record: header(2)+AdvA(6)+adv(<=31) fits easily
  uint16_t n;
  while ((n = duta_sniffer_take(rec, sizeof rec)) != 0) skrit_dev_feed_data(&dev, rec, n);
  while (Serial.available()) skrit_dev_rx(&dev, (uint8_t)Serial.read());
}
#else
void setup() {
  duta_io_begin(); // configure every output/input from the board table
  if (DTR_PIN >= 0) pinMode(DTR_PIN, OUTPUT);
  if (RTS_PIN >= 0) pinMode(RTS_PIN, OUTPUT);

  Serial.begin(115200); // USB-CDC (S3/C3) or UART0-USB (classic): the mux link
#if BOARD_HAS_NATIVE_USB
  // HWCDC: cap write stalls so a host that stops draining can't wedge the loop.
  // NOTE for hosts: the USB-Serial/JTAG converts DTR/RTS edge patterns into
  // chip reset / download-mode entry (rst:0x15, boot:0x0) and firmware cannot
  // disable it — assert DTR only, never toggle RTS on a running Duta, and drop
  // both lines before closing the port (Sutra does). esptool still works.
  Serial.setTxTimeoutMs(50);
#endif
  beginTarget();

  HAL.n_outputs = duta_tbl_n; // a provisioned table may differ from the compiled default
  HAL.caps = board_caps();
  HAL.pwm_config_get = duta_io_pwm_config_get; // freq + resolution (trailing HAL fields)
  HAL.pwm_config_set = duta_io_pwm_config_set;
  HAL.pin_caps = duta_io_pin_caps; // runtime provisioning (advertises FLAG_PROVISION)
  HAL.config_get = duta_io_config_get;
  HAL.config_set = duta_io_config_set;
  HAL.cfg_get = hal_cfg_get; // key-value config (WiFi credentials/status/kind)
  HAL.cfg_set = hal_cfg_set;
  HAL.i2c_scan = hal_i2c_scan; // I2C master (active when DATA kind = i2c)
  HAL.i2c_xfer = hal_i2c_xfer;
  skrit_dev_init(&dev, &HAL, nullptr, /*muxed*/ 1);
  duta_wifi_init(&HAL); // WS bridge: copies the HAL, joins / raises the portal
  data_kind_load();     // after wifi_init so the WS_HAL copy gets the kind too
}

void loop() {
  // UART kind: read the target console ONCE and tee it to every link (USB +
  // WebSocket), each with its own session/auth state. I2C kind: the records
  // are emitted from the I2C_XFER path instead; the console is paused.
  if (g_data_kind == SKRIT_DATA_UART) {
    uint8_t buf[64];
    uint16_t got = hal_data_read(nullptr, buf, sizeof buf);
    if (got) {
      skrit_dev_feed_data(&dev, buf, got);
      skrit_dev_feed_data(&ws_dev, buf, got);
    }
  }
  while (Serial.available()) skrit_dev_rx(&dev, (uint8_t)Serial.read());
  duta_wifi_loop(); // WiFi state machine + WS server pump
}
#endif // DUTA_SNIFFER
