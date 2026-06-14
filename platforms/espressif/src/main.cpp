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
//
// The pure-IDF build (-DDUTA_PURE_IDF) uses main_idf.c instead; compile this
// Arduino entry out entirely there.
#ifndef DUTA_PURE_IDF
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
// UART bridge.
#include "duta_sniffer.h"
// BLE GATT transport variant (-DDUTA_BLE_TRANSPORT): the host link is BLE instead
// of USB/WiFi (the target UART console is still bridged, over the DATA service).
#ifdef DUTA_BLE_TRANSPORT
#include "duta_ble.h"
#endif

// Radio-only links (the BLE sniffer or the BLE transport) compile out WiFi/I2C to
// leave room for the BLE stack and keep the build single-purpose.
#if defined(DUTA_SNIFFER) || defined(DUTA_BLE_TRANSPORT)
#define DUTA_NO_NET 1
#endif

#include "driver/gpio.h" // gpio_pullup_en — pull the DATA RX line when unwired
#ifndef DUTA_NO_NET
#include "duta_i2c.h"    // I2C master DATA backend (Wire) — the first non-UART medium
#include "duta_vl53l0x.h" // VL53L0X ToF sensor exposed as INVOKE commands (over the I2C bus)
#include "duta_wifi.h"   // WiFi + WebSocket bridge + captive portal (second skrit_dev)
#endif

#ifndef DUTA_NO_NET // UART/I2C DATA bridge + WiFi config — not in a radio-only build
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

#ifndef DUTA_NO_NET
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

// ---- INVOKE: device-owned high-level commands -------------------------------
// The host (or a macro) forwards an intent and the device runs the multi-step
// sequence itself. First set: the VL53L0X ToF sensor over the I2C master bus.
// Vendor ids 0x8000+; the addr arg (u8) defaults to 0x29 when omitted.
static uint8_t hal_cmd_desc(void *, uint8_t index, uint16_t *id, uint8_t *nargs,
                            uint8_t *argtype, uint8_t *flags, const char **name) {
  switch (index) {
    case 0: *id = 0x8010; *nargs = 1; argtype[0] = SKRIT_ARG_U8; *flags = SKRIT_INVOKE_REPLY; *name = "vl53l0x_id"; break;
    case 1: *id = 0x8011; *nargs = 1; argtype[0] = SKRIT_ARG_U8; *flags = 0; *name = "vl53l0x_init"; break;
    case 2: *id = 0x8012; *nargs = 1; argtype[0] = SKRIT_ARG_U8; *flags = SKRIT_INVOKE_REPLY; *name = "vl53l0x_read_mm"; break;
    case 3: *id = 0x8013; *nargs = 2; argtype[0] = SKRIT_ARG_U8; argtype[1] = SKRIT_ARG_U16; *flags = 0; *name = "vl53l0x_start"; break;
    case 4: *id = 0x8014; *nargs = 0; *flags = 0; *name = "vl53l0x_stop"; break;
    default: break;
  }
  return 5;
}
// vl53l0x_start streams continuous readings on the DATA console; this is its state.
static bool vl_stream_on = false;
static uint8_t vl_stream_addr = VL53L0X_DEFAULT_ADDR;
static uint16_t vl_stream_period = 100; // ms between emitted readings
static uint32_t vl_stream_last = 0;

static uint8_t hal_cmd_invoke(void *, uint16_t id, const uint8_t *p, uint8_t plen,
                              uint8_t *reply, uint8_t cap, uint8_t *rlen) {
  *rlen = 0;
  uint8_t addr = plen >= 1 ? p[0] : VL53L0X_DEFAULT_ADDR;
  switch (id) {
    case 0x8010: // vl53l0x_id -> model-id byte (0xEE on a healthy part)
      if (cap < 1) return SKRIT_ST_BADARGS;
      reply[0] = vl53l0x_model_id(addr);
      *rlen = 1;
      return SKRIT_ST_OK;
    case 0x8011: // vl53l0x_init -> status only
      return vl53l0x_init(addr) ? SKRIT_ST_OK : SKRIT_ST_NOTFOUND;
    case 0x8012: { // vl53l0x_read_mm -> u16 millimetres (LE)
      bool ok = false;
      uint16_t mm = vl53l0x_read_mm(addr, &ok);
      if (!ok) return SKRIT_ST_NOTFOUND;
      if (cap < 2) return SKRIT_ST_BADARGS;
      reply[0] = (uint8_t)mm;
      reply[1] = (uint8_t)(mm >> 8);
      *rlen = 2;
      return SKRIT_ST_OK;
    }
    case 0x8013: { // vl53l0x_start(addr u8, period_ms u16) -> stream on the console
      if (!vl53l0x_init(addr)) return SKRIT_ST_NOTFOUND;
      if (!vl53l0x_start_continuous(addr)) return SKRIT_ST_NOTFOUND;
      uint16_t period = plen >= 3 ? (uint16_t)(p[1] | (p[2] << 8)) : 100;
      vl_stream_addr = addr;
      vl_stream_period = period < 20 ? 20 : period; // floor below the sensor's sample rate is pointless
      vl_stream_last = 0;
      vl_stream_on = true;
      return SKRIT_ST_OK;
    }
    case 0x8014: // vl53l0x_stop -> end the stream
      if (vl_stream_on) vl53l0x_stop_continuous(vl_stream_addr);
      vl_stream_on = false;
      return SKRIT_ST_OK;
    default: break;
  }
  return SKRIT_ST_NOTFOUND;
}

// Called every loop: when a stream is active, emit the latest reading on the DATA
// console as "vl53l0x=<mm>\r\n" (parseable by a yantra "uart" readout/table). Only
// in UART/console kind — in I2C-record kind the DATA channel carries framed records.
static void vl53_stream_pump() {
  if (!vl_stream_on || g_data_kind != SKRIT_DATA_UART) return;
  uint32_t now = millis();
  if (now - vl_stream_last < vl_stream_period) return;
  vl_stream_last = now;
  bool ok = false, valid = false;
  uint16_t mm = vl53l0x_read_continuous_mm(vl_stream_addr, &ok, &valid);
  if (!ok) return; // no fresh sample yet
  char line[24];
  // valid → the distance; non-detection (status invalid / out of range) → a clear
  // "none" marker so the host can hold its last value instead of acting on garbage.
  int n = valid ? snprintf(line, sizeof line, "vl53l0x=%u\r\n", (unsigned)mm)
                : snprintf(line, sizeof line, "vl53l0x=none\r\n");
  if (n <= 0) return;
  skrit_dev_feed_data(&dev, (const uint8_t *)line, (uint16_t)n);
  skrit_dev_feed_data(&ws_dev, (const uint8_t *)line, (uint16_t)n);
}

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

#ifndef DUTA_NO_NET
// Switch the bridged medium live: both devs read their HALs by pointer.
static void data_kind_apply(uint8_t kind) {
  g_data_kind = kind;
  HAL.data_kind = kind;
  WS_HAL.data_kind = kind;
  if (kind == SKRIT_DATA_I2C) duta_i2c_begin();
  else duta_i2c_end();
}
#endif

#ifdef DUTA_BLE_TRANSPORT
// Dual-channel BLE transport HAL: CMD responses + DATA console each notify their
// own GATT service; host->target console writes arrive via the DATA-RX callback.
static void ble_cmd_link_write(void *, const uint8_t *p, uint16_t n) { duta_ble::cmd_notify(p, n); }
static void ble_data_host_write(void *, const uint8_t *p, uint16_t n) { duta_ble::data_notify(p, n); }
static void ble_data_sink(const uint8_t *p, uint16_t n) { TARGET.write(p, n); }
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
#elif defined(DUTA_BLE_TRANSPORT)
// BLE transport build: the host link is the two skrit GATT services (paired +
// encrypted); the target UART console is bridged out over the DATA service. No
// USB-mux, no WiFi. Mirrors the nRF BLE transport.
void setup() {
  duta_io_begin();
  if (DTR_PIN >= 0) pinMode(DTR_PIN, OUTPUT);
  if (RTS_PIN >= 0) pinMode(RTS_PIN, OUTPUT);
  Serial.begin(115200); // keep USB-CDC enumerated for flashing; not the link
  beginTarget();        // the DATA console is still the target UART
  HAL.n_outputs = duta_tbl_n;
  HAL.caps = (uint8_t)(board_caps() & ~(uint8_t)SKRIT_CAP_MUX); // dual-channel, not muxed
  HAL.pwm_config_get = duta_io_pwm_config_get;
  HAL.pwm_config_set = duta_io_pwm_config_set;
  HAL.pin_caps = duta_io_pin_caps;
  HAL.config_get = duta_io_config_get;
  HAL.config_set = duta_io_config_set;
  HAL.link_write = ble_cmd_link_write;  // device->host CMD responses/events
  HAL.host_write = ble_data_host_write; // device->host DATA console (its own pipe)
  skrit_dev_init(&dev, &HAL, nullptr, /*muxed*/ 0);
  duta_ble::init(&dev, ble_data_sink);
}

void loop() {
  skrit_dev_poll(&dev); // read the target console -> DATA notify (host_write)
  duta_ble::loop();     // drain the TX rings to the radio
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
  HAL.cmd_desc = hal_cmd_desc; // INVOKE: device-owned commands (VL53L0X); set
  HAL.cmd_invoke = hal_cmd_invoke; // before duta_wifi_init so the WS HAL copy gets them
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
  vl53_stream_pump();  // INVOKE-started sensor stream -> DATA console (no-op unless started)
  duta_wifi_loop(); // WiFi state machine + WS server pump
}
#endif // DUTA_SNIFFER

#endif // !DUTA_PURE_IDF
