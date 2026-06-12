// main_idf.c — Duta on pure ESP-IDF (no Arduino). Built for -DDUTA_PURE_IDF.
// ============================================================================
// The IDF-native counterpart to main.cpp: the same skrit core (../../common +
// the board's duta_outputs[] table), driven entirely on IDF APIs —
//   * host link : the native USB-Serial/JTAG, carrying the muxed CMD+DATA stream
//   * DATA bridge: UART1 on DATA_TX/RX (the target console)
//   * outputs   : GPIO (on/off), LEDC (PWM), RMT WS2812 (addressable RGB)
//   * reboot    : esp_restart, or download-boot on S3/C3
// First cut: transport + IO + reboot + serial params. Runtime provisioning, I2C,
// and WiFi/WS are still the Arduino build's job (ported later).
// ============================================================================
#ifdef DUTA_PURE_IDF

#include <stdbool.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
// RTC_CNTL register block (download-boot poke below) only exists on the S3/C3
// here; the C6 replaced it with the LP_AON/PMU domain, so guard the include.
#if defined(BOARD_ESP32S3) || defined(BOARD_S3_ZERO) || defined(BOARD_ESP32C3)
#include "soc/rtc_cntl_reg.h"
#endif

#include "board.h" // duta_outputs[] + role pins + BOARD_NAME + DUTA_RGB_* + mcu pins
// Optional radio-sniffer DATA backend (e.g. -DDUTA_SNIFF_ESP154 for the C6/H2
// 802.15.4 radio): binds duta_sniffer_*() + DUTA_SNIFFER / DUTA_SNIFF_KIND /
// DUTA_SNIFF_HAS_TX|CHANNEL. When set, DATA is captured radio frames, not a UART.
#include "duta_sniffer.h"

// Runtime IO provisioning: the duta_io[] table can be re-provisioned from the app
// and persisted in NVS. These store hooks must be defined before duta_io_arduino.h
// (the engine's loader calls them under DUTA_HAVE_STORE).
#define DUTA_PROVISION
#define DUTA_HAVE_STORE
#include "nvs.h"
static uint16_t duta_io_store_load(uint8_t *buf, uint16_t cap) {
  nvs_handle_t h;
  if (nvs_open("duta", NVS_READONLY, &h) != ESP_OK) return 0;
  size_t n = cap;
  esp_err_t e = nvs_get_blob(h, "io", buf, &n);
  nvs_close(h);
  return e == ESP_OK ? (uint16_t)n : 0;
}
static uint8_t duta_io_store_save(const uint8_t *buf, uint16_t n) {
  nvs_handle_t h;
  if (nvs_open("duta", NVS_READWRITE, &h) != ESP_OK) return 0;
  esp_err_t e = nvs_set_blob(h, "io", buf, n);
  nvs_commit(h);
  nvs_close(h);
  return e == ESP_OK;
}
static void duta_io_store_clear(void) {
  nvs_handle_t h;
  if (nvs_open("duta", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_erase_key(h, "io");
  nvs_commit(h);
  nvs_close(h);
}

#include "duta_io_arduino.h" // table-driven IO HAL (GPIO/LEDC/RMT) + provisioning resolver
#include "skrit_device.h"    // the portable core
#include "duta_i2c.h"        // I2C master DATA backend (new i2c_master driver)
#include "duta_wifi_idf.h"   // WiFi + WebSocket bridge + captive portal (lwip)

#define FW_LO 0x04
#define FW_HI 0x00
#define DATA_UART UART_NUM_1

static skrit_dev g_dev;
static uint32_t g_baud = 115200;
static uint8_t g_data_kind = SKRIT_DATA_UART; // UART console (default) or I2C master
static skrit_hal HAL;                         // filled in app_main; referenced by data_kind_apply

// ---- DATA target UART -------------------------------------------------------
static void data_uart_begin(void) {
  uart_config_t c = {
      .baud_rate = (int)g_baud,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  if (!uart_is_driver_installed(DATA_UART)) uart_driver_install(DATA_UART, 2048, 0, 0, NULL, 0);
  uart_param_config(DATA_UART, &c);
  uart_set_pin(DATA_UART, DATA_TX_PIN, DATA_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// ---- HAL: transport ---------------------------------------------------------
static void hal_link_write(void *c, const uint8_t *p, uint16_t n) {
  (void)c;
  usb_serial_jtag_write_bytes(p, n, pdMS_TO_TICKS(20));
}
static void hal_data_write(void *c, const uint8_t *p, uint16_t n) {
  (void)c;
#ifdef DUTA_SNIFF_HAS_TX
  duta_sniffer_tx(p, n); // sniffer build: a host DATA write is a frame to inject
#else
  uart_write_bytes(DATA_UART, p, n);
#endif
}
static uint16_t hal_data_read(void *c, uint8_t *out, uint16_t cap) {
  (void)c;
  int n = uart_read_bytes(DATA_UART, out, cap, 0);
  return n > 0 ? (uint16_t)n : 0;
}

// ---- HAL: serial params + signals ------------------------------------------
static void hal_proto_get(void *c, uint8_t idx, uint32_t *value, uint8_t *o0, uint8_t *o1, uint8_t *o2) {
  (void)c;
  (void)idx;
#ifdef DUTA_SNIFF_HAS_CHANNEL
  *value = duta_sniffer_get_channel(); // ieee802154: value = channel (11..26)
  *o0 = *o1 = *o2 = 0;
#else
  *value = g_baud;
  *o0 = 8;
  *o1 = SKRIT_PAR_NONE;
  *o2 = 1;
#endif
}
static void hal_proto_set(void *c, uint8_t idx, uint32_t value, uint8_t o0, uint8_t o1, uint8_t o2) {
  (void)c;
  (void)idx;
  (void)o0;
  (void)o1;
  (void)o2;
#ifdef DUTA_SNIFF_HAS_CHANNEL
  duta_sniffer_set_channel((uint8_t)value); // value carries the channel (0/out-of-range = hop)
#else
  if (value) g_baud = value;
  data_uart_begin(); // re-apply (baud only for now; bits/parity/stop = 8N1)
#endif
}
static void hal_serial_signal(void *c, uint8_t mask, uint8_t value) {
  (void)c;
#if DTR_PIN >= 0
  if (mask & SKRIT_SIG_DTR) gpio_set_level((gpio_num_t)DTR_PIN, (value & SKRIT_SIG_DTR) ? 1 : 0);
#endif
#if RTS_PIN >= 0
  if (mask & SKRIT_SIG_RTS) gpio_set_level((gpio_num_t)RTS_PIN, (value & SKRIT_SIG_RTS) ? 1 : 0);
#endif
}

// ---- HAL: system ------------------------------------------------------------
static void hal_reboot(void *c, uint8_t mode) {
  (void)c;
#if defined(BOARD_ESP32S3) || defined(BOARD_S3_ZERO) || defined(BOARD_ESP32C3)
  if (mode == SKRIT_REBOOT_BOOTLOADER) REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
#endif
  esp_restart();
}
static uint32_t hal_millis(void *c) {
  (void)c;
  return (uint32_t)(esp_timer_get_time() / 1000);
}
static void hal_pump(void *c) { (void)c; }

static uint8_t board_caps(void) {
  uint8_t caps = SKRIT_CAP_MUX | SKRIT_CAP_SERIAL | SKRIT_CAP_REBOOT;
  for (uint8_t i = 0; i < duta_tbl_n; i++) // the active (maybe provisioned) table
    if (duta_tbl[i].type == SKRIT_CTRL_PWM) caps |= SKRIT_CAP_PWM;
  return caps;
}

// ---- DATA medium kind (UART console default / I2C master), persisted in NVS --
static void data_kind_save(uint8_t k) {
  nvs_handle_t h;
  if (nvs_open("duta", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u8(h, "dkind", k);
  nvs_commit(h);
  nvs_close(h);
}
static uint8_t data_kind_load(void) {
  nvs_handle_t h;
  uint8_t k = SKRIT_DATA_UART;
  if (nvs_open("duta", NVS_READONLY, &h) == ESP_OK) {
    nvs_get_u8(h, "dkind", &k);
    nvs_close(h);
  }
  return (k == SKRIT_DATA_I2C) ? SKRIT_DATA_I2C : SKRIT_DATA_UART;
}
static void data_kind_apply(uint8_t kind) {
  g_data_kind = kind;
  HAL.data_kind = kind;
  if (kind == SKRIT_DATA_I2C) duta_i2c_begin();
  else duta_i2c_end();
}

// ---- CFG_GET/SET: DATA pins + kind (WiFi keys arrive with the WiFi port) -----
static int16_t hal_cfg_get(void *c, uint8_t key, uint8_t *out, uint8_t cap) {
  (void)c;
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
  return duta_wifi_cfg_get(key, out, cap); // WIFI_SSID/PASS/STATUS
}
static uint8_t hal_cfg_set(void *c, uint8_t key, const uint8_t *val, uint8_t len) {
  (void)c;
  if (key == SKRIT_CFG_DATA_KIND) {
    if (len != 1) return SKRIT_ST_BADARGS;
    if (val[0] != SKRIT_DATA_UART && val[0] != SKRIT_DATA_I2C) return SKRIT_ST_BADARGS;
    data_kind_apply(val[0]);
    data_kind_save(val[0]);
    return SKRIT_ST_OK;
  }
  return duta_wifi_cfg_set(key, val, len); // WIFI_SSID/PASS
}

// ---- I2C HAL: do the transfer, then emit a DATA record when DATA kind = i2c --
static void i2c_emit_record(uint8_t addr, uint8_t flags, const uint8_t *w, uint8_t wlen,
                            const uint8_t *r, uint8_t rlen) {
  uint8_t rec[8 + 255];
  uint16_t n = duta_i2c_record(rec, sizeof rec, addr, flags, w, wlen, r, rlen);
  if (n) { skrit_dev_feed_data(&g_dev, rec, n); duta_wifi_feed(rec, n); } // tee to USB + WS
}
static uint8_t hal_i2c_scan(void *c, uint8_t bitmap[16]) {
  (void)c;
  return duta_i2c_scan(bitmap);
}
static uint8_t hal_i2c_xfer(void *c, uint8_t addr, const uint8_t *w, uint8_t wlen, uint8_t *r, uint8_t rlen) {
  (void)c;
  uint8_t st = duta_i2c_xfer(addr, w, wlen, r, rlen);
  uint8_t flags = (uint8_t)((rlen ? 1 : 0) | (st != SKRIT_ST_OK ? 2 : 0));
  if (g_data_kind == SKRIT_DATA_I2C && st != SKRIT_ST_UNSUPPORTED)
    i2c_emit_record(addr, flags, w, wlen, st == SKRIT_ST_OK ? r : NULL, st == SKRIT_ST_OK ? rlen : 0);
  return st;
}

void app_main(void) {
  nvs_flash_init();
  duta_io_begin(); // load any provisioned table + configure GPIO/LEDC/RGB from it
#ifndef DUTA_SNIFFER
  data_uart_begin(); // a sniffer build has no DATA UART — DATA is captured frames
#endif
#if DTR_PIN >= 0
  gpio_set_direction((gpio_num_t)DTR_PIN, GPIO_MODE_OUTPUT);
#endif
#if RTS_PIN >= 0
  gpio_set_direction((gpio_num_t)RTS_PIN, GPIO_MODE_OUTPUT);
#endif
  usb_serial_jtag_driver_config_t uc = {.tx_buffer_size = 1024, .rx_buffer_size = 1024};
  usb_serial_jtag_driver_install(&uc);

  memset(&HAL, 0, sizeof HAL);
  HAL.name = BOARD_NAME;
  HAL.fw_ver = (FW_HI << 8) | FW_LO;
  HAL.caps = board_caps();
  HAL.macro_tier = SKRIT_TIER_INTERACTIVE;
  HAL.n_outputs = duta_tbl_n; // the active (maybe provisioned) table
  HAL.link_write = hal_link_write;
  HAL.data_write = hal_data_write;
  HAL.data_read = hal_data_read;
  HAL.out_set = duta_io_out_set; // the table-driven engine (GPIO/LEDC/RMT)
  HAL.out_get = duta_io_out_get;
  HAL.out_desc = duta_io_out_desc;
  HAL.pwm_set = duta_io_pwm_set;
  HAL.pwm_get = duta_io_pwm_get;
  HAL.pwm_config_get = duta_io_pwm_config_get;
  HAL.pwm_config_set = duta_io_pwm_config_set;
  HAL.rgb_count = duta_io_rgb_count;
  HAL.rgb_set = duta_io_rgb_set;
  HAL.rgb_get = duta_io_rgb_get;
  HAL.pin_caps = duta_io_pin_caps; // runtime provisioning (advertises FLAG_PROVISION)
  HAL.config_get = duta_io_config_get;
  HAL.config_set = duta_io_config_set;
  HAL.proto_get = hal_proto_get;
  HAL.proto_set = hal_proto_set;
  HAL.serial_signal = hal_serial_signal;
  HAL.reboot = hal_reboot;
  HAL.millis = hal_millis;
  HAL.pump = hal_pump;
  HAL.cfg_get = hal_cfg_get; // DATA pins + kind
  HAL.cfg_set = hal_cfg_set;
  HAL.i2c_scan = hal_i2c_scan; // I2C master (active when DATA kind = i2c)
  HAL.i2c_xfer = hal_i2c_xfer;
  skrit_dev_init(&g_dev, &HAL, NULL, /*muxed*/ 1);
#ifdef DUTA_SNIFFER
  HAL.data_kind = DUTA_SNIFF_KIND; // DATA = captured radio frames (DATA_DESC reports it)
  g_data_kind = DUTA_SNIFF_KIND;
  duta_sniffer_init();
  duta_sniffer_start(); // always-on; the host gets records + can inject + set channel
#else
  data_kind_apply(data_kind_load()); // restore the persisted bridged medium
  duta_wifi_init(&HAL);              // WS bridge: copies the HAL, joins / raises the portal
#endif

  uint8_t buf[64], db[140]; // db also holds one ieee802154 record (up to ~136 bytes)
  for (;;) {
#ifdef DUTA_SNIFFER
    // Sniffer kind: pop captured radio frames; each is one DATA record on the mux.
    uint16_t got;
    while ((got = duta_sniffer_take(db, sizeof db)) != 0) skrit_dev_feed_data(&g_dev, db, got);
#else
    // UART kind: read the target console ONCE and tee it to both links (USB + WS),
    // each with its own session/auth state. I2C kind: records come from I2C_XFER.
    if (g_data_kind == SKRIT_DATA_UART) {
      uint16_t got = hal_data_read(NULL, db, sizeof db);
      if (got) { skrit_dev_feed_data(&g_dev, db, got); duta_wifi_feed(db, got); }
    }
#endif
    int n = usb_serial_jtag_read_bytes(buf, sizeof buf, 0);
    for (int i = 0; i < n; i++) skrit_dev_rx(&g_dev, buf[i]); // CMD + host DATA in over USB
#ifndef DUTA_SNIFFER
    duta_wifi_loop();                                         // WiFi state machine + WS pump
#endif
    vTaskDelay(1);
  }
}

#endif // DUTA_PURE_IDF
