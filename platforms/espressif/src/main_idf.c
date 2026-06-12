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
#include "soc/rtc_cntl_reg.h"

#include "board.h"            // duta_outputs[] + role pins + BOARD_NAME + DUTA_RGB_*
#include "duta_ws2812_rmt.h"  // onboard addressable LED
#include "skrit_device.h"     // the portable core
#include "duta_i2c.h"         // I2C master DATA backend (new i2c_master driver)
#include "duta_wifi_idf.h"    // WiFi + WebSocket bridge + captive portal (lwip)

#define FW_LO 0x04
#define FW_HI 0x00
#define DUTA_N_OUT ((uint8_t)(sizeof duta_outputs / sizeof duta_outputs[0]))
#define DATA_UART UART_NUM_1

// map a bare RGB-order token (RGB / GRB) to a flag for the WS2812 driver
#define DUTA_ORDER_RGB 1
#define DUTA_ORDER_GRB 0
#define DUTA_CAT2(a, b) a##b
#define DUTA_CAT(a, b) DUTA_CAT2(a, b)
#ifdef DUTA_RGB_ORDER
#define DUTA_RGB_IS_RGB DUTA_CAT(DUTA_ORDER_, DUTA_RGB_ORDER)
#else
#define DUTA_RGB_IS_RGB 0
#endif

static skrit_dev g_dev;
static uint8_t g_out[DUTA_N_OUT > 0 ? DUTA_N_OUT : 1];
static uint16_t g_duty[DUTA_N_OUT > 0 ? DUTA_N_OUT : 1];
static int8_t g_pwm_ch[DUTA_N_OUT > 0 ? DUTA_N_OUT : 1]; // LEDC channel per PWM output, else -1
static uint32_t g_baud = 115200;
static uint8_t g_data_kind = SKRIT_DATA_UART; // UART console (default) or I2C master
static skrit_hal HAL;                         // filled in app_main; referenced by data_kind_apply

static inline bool is_io(uint8_t i) { return duta_outputs[i].type == SKRIT_CTRL_IO; }
static inline bool is_pwm(uint8_t i) { return duta_outputs[i].type == SKRIT_CTRL_PWM; }
static inline bool is_rgb(uint8_t i) { return duta_outputs[i].type == SKRIT_CTRL_RGB; }
static inline bool active_low(uint8_t i) { return duta_outputs[i].flags & DUTA_ACTIVE_LOW; }

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

// ---- output setup -----------------------------------------------------------
static void outputs_init(void) {
  uint8_t ledc_ch = 0;
  bool ledc_timer_done = false;
  for (uint8_t i = 0; i < DUTA_N_OUT; i++) {
    g_pwm_ch[i] = -1;
    if (is_io(i)) {
      gpio_reset_pin((gpio_num_t)duta_outputs[i].pin);
      gpio_set_direction((gpio_num_t)duta_outputs[i].pin, GPIO_MODE_OUTPUT);
      gpio_set_level((gpio_num_t)duta_outputs[i].pin, active_low(i) ? 1 : 0);
    } else if (is_pwm(i)) {
      if (!ledc_timer_done) {
        ledc_timer_config_t t = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_10_BIT, // 0..1023, matches the skrit duty range
            .timer_num = LEDC_TIMER_0,
            .freq_hz = 5000,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ledc_timer_config(&t);
        ledc_timer_done = true;
      }
      ledc_channel_config_t ch = {
          .gpio_num = duta_outputs[i].pin,
          .speed_mode = LEDC_LOW_SPEED_MODE,
          .channel = (ledc_channel_t)ledc_ch,
          .timer_sel = LEDC_TIMER_0,
          .duty = 0,
          .hpoint = 0,
          .flags.output_invert = active_low(i) ? 1 : 0,
      };
      ledc_channel_config(&ch);
      g_pwm_ch[i] = (int8_t)ledc_ch++;
    }
  }
#ifdef DUTA_RGB_PIN
  ws2812_init(DUTA_RGB_PIN, DUTA_RGB_COUNT, DUTA_RGB_IS_RGB);
#endif
}

// ---- HAL: transport ---------------------------------------------------------
static void hal_link_write(void *c, const uint8_t *p, uint16_t n) {
  (void)c;
  usb_serial_jtag_write_bytes(p, n, pdMS_TO_TICKS(20));
}
static void hal_data_write(void *c, const uint8_t *p, uint16_t n) {
  (void)c;
  uart_write_bytes(DATA_UART, p, n);
}
static uint16_t hal_data_read(void *c, uint8_t *out, uint16_t cap) {
  (void)c;
  int n = uart_read_bytes(DATA_UART, out, cap, 0);
  return n > 0 ? (uint16_t)n : 0;
}

// ---- HAL: outputs -----------------------------------------------------------
static void hal_out_set(void *c, uint8_t idx, uint8_t on) {
  (void)c;
  if (idx >= DUTA_N_OUT) return;
  g_out[idx] = on ? 1 : 0;
  if (is_io(idx)) {
    gpio_set_level((gpio_num_t)duta_outputs[idx].pin, (on ? 1 : 0) ^ (active_low(idx) ? 1 : 0));
  } else if (is_pwm(idx)) {
    g_duty[idx] = on ? 1023 : 0;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)g_pwm_ch[idx], g_duty[idx]);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)g_pwm_ch[idx]);
  } else if (is_rgb(idx)) {
    uint8_t v = on ? 64 : 0;
    for (uint16_t px = 0; px < duta_outputs[idx].arg; px++) ws2812_set(px, v, v, v);
    ws2812_show();
  }
}
static uint8_t hal_out_get(void *c, uint8_t idx) {
  (void)c;
  return idx < DUTA_N_OUT ? g_out[idx] : 0;
}
static void hal_out_desc(void *c, uint8_t idx, uint8_t *type, const char **name) {
  (void)c;
  if (idx >= DUTA_N_OUT) return;
  *type = duta_outputs[idx].type;
  *name = duta_outputs[idx].name;
}

// ---- HAL: PWM ---------------------------------------------------------------
static uint8_t hal_pwm_set(void *c, uint8_t idx, uint16_t duty) {
  (void)c;
  if (idx >= DUTA_N_OUT || !is_pwm(idx)) return 0;
  if (duty > 1023) duty = 1023;
  g_duty[idx] = duty;
  g_out[idx] = duty ? 1 : 0;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)g_pwm_ch[idx], duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)g_pwm_ch[idx]);
  return 1;
}
static uint16_t hal_pwm_get(void *c, uint8_t idx) {
  (void)c;
  return idx < DUTA_N_OUT ? g_duty[idx] : 0;
}

// ---- HAL: RGB ---------------------------------------------------------------
static uint8_t hal_rgb_count(void *c, uint8_t idx) {
  (void)c;
  return (idx < DUTA_N_OUT && is_rgb(idx)) ? (uint8_t)duta_outputs[idx].arg : 0;
}
static uint8_t hal_rgb_set(void *c, uint8_t idx, uint8_t px, uint8_t r, uint8_t g, uint8_t b) {
  (void)c;
  if (idx >= DUTA_N_OUT || !is_rgb(idx)) return 0;
  uint16_t n = duta_outputs[idx].arg;
  if (px == SKRIT_RGB_ALL) {
    for (uint16_t i = 0; i < n; i++) ws2812_set(i, r, g, b);
  } else if (px < n) {
    ws2812_set(px, r, g, b);
  } else {
    return 0;
  }
  ws2812_show();
  g_out[idx] = (r | g | b) ? 1 : 0;
  return 1;
}
static void hal_rgb_get(void *c, uint8_t idx, uint8_t px, uint8_t *r, uint8_t *g, uint8_t *b) {
  (void)c;
  if (idx >= DUTA_N_OUT || !is_rgb(idx) || px >= duta_outputs[idx].arg) { *r = *g = *b = 0; return; }
  ws2812_get(px, r, g, b);
}

// ---- HAL: serial params + signals ------------------------------------------
static void hal_proto_get(void *c, uint8_t idx, uint32_t *value, uint8_t *o0, uint8_t *o1, uint8_t *o2) {
  (void)c;
  (void)idx;
  *value = g_baud;
  *o0 = 8;
  *o1 = SKRIT_PAR_NONE;
  *o2 = 1;
}
static void hal_proto_set(void *c, uint8_t idx, uint32_t value, uint8_t o0, uint8_t o1, uint8_t o2) {
  (void)c;
  (void)idx;
  (void)o0;
  (void)o1;
  (void)o2;
  if (value) g_baud = value;
  data_uart_begin(); // re-apply (baud only for now; bits/parity/stop = 8N1)
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
  for (uint8_t i = 0; i < DUTA_N_OUT; i++)
    if (is_pwm(i)) caps |= SKRIT_CAP_PWM;
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
  outputs_init();
  data_uart_begin();
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
  HAL.n_outputs = DUTA_N_OUT;
  HAL.link_write = hal_link_write;
  HAL.data_write = hal_data_write;
  HAL.data_read = hal_data_read;
  HAL.out_set = hal_out_set;
  HAL.out_get = hal_out_get;
  HAL.out_desc = hal_out_desc;
  HAL.pwm_set = hal_pwm_set;
  HAL.pwm_get = hal_pwm_get;
  HAL.rgb_count = hal_rgb_count;
  HAL.rgb_set = hal_rgb_set;
  HAL.rgb_get = hal_rgb_get;
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
  data_kind_apply(data_kind_load()); // restore the persisted bridged medium
  duta_wifi_init(&HAL);              // WS bridge: copies the HAL, joins / raises the portal

  uint8_t buf[64], db[64];
  for (;;) {
    // UART kind: read the target console ONCE and tee it to both links (USB + WS),
    // each with its own session/auth state. I2C kind: records come from I2C_XFER.
    if (g_data_kind == SKRIT_DATA_UART) {
      uint16_t got = hal_data_read(NULL, db, sizeof db);
      if (got) { skrit_dev_feed_data(&g_dev, db, got); duta_wifi_feed(db, got); }
    }
    int n = usb_serial_jtag_read_bytes(buf, sizeof buf, 0);
    for (int i = 0; i < n; i++) skrit_dev_rx(&g_dev, buf[i]); // CMD + host DATA in over USB
    duta_wifi_loop();                                         // WiFi state machine + WS pump
    vTaskDelay(1);
  }
}

#endif // DUTA_PURE_IDF
