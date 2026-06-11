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

#include "driver/gpio.h" // gpio_pullup_en — pull the DATA RX line when unwired
#include "duta_wifi.h"   // WiFi + WebSocket bridge + captive portal (second skrit_dev)

// CFG_GET/CFG_SET route to the WiFi module (the only key owner so far).
static int16_t hal_cfg_get(void *, uint8_t key, uint8_t *out, uint8_t cap) {
  return duta_wifi_cfg_get(key, out, cap);
}
static uint8_t hal_cfg_set(void *, uint8_t key, const uint8_t *val, uint8_t len) {
  return duta_wifi_cfg_set(key, val, len);
}

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
static void hal_data_write(void *, const uint8_t *p, uint16_t n) { TARGET.write(p, n); }

static uint16_t hal_data_read(void *, uint8_t *out, uint16_t cap) {
  uint16_t k = 0;
  while (TARGET.available() && k < cap) out[k++] = (uint8_t)TARGET.read();
  return k;
}

static void hal_serial_get(void *, uint32_t *baud, uint8_t *bits, uint8_t *par, uint8_t *stop) {
  *baud = g_baud;
  *bits = g_bits;
  *par = g_parity;
  *stop = g_stop;
}
static void hal_serial_set(void *, uint32_t baud, uint8_t bits, uint8_t par, uint8_t stop) {
  if (baud) g_baud = baud;
  if (bits) g_bits = bits;
  g_parity = par;
  if (stop) g_stop = stop;
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
    hal_serial_get, hal_serial_set, hal_serial_signal,
    hal_reboot,
    hal_millis, hal_pump,
};

// ---- Arduino entry points --------------------------------------------------
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
  HAL.cfg_get = hal_cfg_get; // key-value config (WiFi credentials/status)
  HAL.cfg_set = hal_cfg_set;
  skrit_dev_init(&dev, &HAL, nullptr, /*muxed*/ 1);
  duta_wifi_init(&HAL); // WS bridge: copies the HAL, joins / raises the portal
}

void loop() {
  // Read the target console ONCE and tee it to every link (USB + WebSocket),
  // each with its own session/auth state.
  uint8_t buf[64];
  uint16_t got = hal_data_read(nullptr, buf, sizeof buf);
  if (got) {
    skrit_dev_feed_data(&dev, buf, got);
    skrit_dev_feed_data(&ws_dev, buf, got);
  }
  while (Serial.available()) skrit_dev_rx(&dev, (uint8_t)Serial.read());
  duta_wifi_loop(); // WiFi state machine + WS server pump
}
