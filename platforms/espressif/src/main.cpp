// Duta on ESP32 / ESP32-S3 / ESP32-C3 (Arduino + PlatformIO).
// ============================================================================
// A real Duta adapter built on the shared core (../../common/skrit_device.h):
//
//   * DATA console  — a hardware UART (Serial1 on DATA_TX/DATA_RX) bridged to
//                     the target, full duplex.
//   * CMD + DATA    — multiplexed over the single native USB-CDC via skrit-mux,
//                     so the Sutra app opens ONE port and gets both.
//   * Controls      — 2 relays + an aux LED, self-describing.
//   * Macro VM      — skrit-mc tiers 1-2 (EMIT/DELAY/SETOUT/EXPECT/WAITIO),
//                     scratch push-and-run (no flash wear).
//   * Serial control— SERIAL_GET/SET reconfigures the DATA UART; SERIAL_SIGNAL
//                     drives DTR/RTS (auto-reset into ESP/AVR bootloaders) + BREAK.
//   * REBOOT        — app reset, or reboot-to-download on S3/C3.
//
// The protocol/CRC/COBS/VM all live in the shared core; this file is just the
// ESP HAL + the two-line main loop.
#include <Arduino.h>

#include "board.h"
#if RGB_PIN >= 0
#define FASTLED_INTERNAL // silence the FastLED version banner
#include <FastLED.h>
#endif
extern "C" {
#include "skrit_device.h"
}

#if defined(BOARD_ESP32S3) || defined(BOARD_ESP32C3)
#include "soc/rtc_cntl_reg.h" // RTC_CNTL_OPTION1_REG — force download (DFU) boot
#endif

#define FW_LO 0x04
#define FW_HI 0x00

// Serial1 is the bridged target UART. On the classic ESP32 it also exists; on
// S3/C3 the USB-CDC is `Serial`, leaving Serial1 free for the console.
static HardwareSerial &TARGET = Serial1;

// Index 3 (RGB LED) exists only on boards with an onboard NeoPixel (RGB_PIN >= 0).
#define RGB_IDX 3
#define N_OUTPUTS (RGB_PIN >= 0 ? 4 : 3)

static uint32_t g_baud = 115200;
static uint8_t g_bits = 8, g_parity = SKRIT_PAR_NONE, g_stop = 1;
static uint8_t g_out[4]; // relay1, relay2, led, [rgb]

static const char *const OUT_NAME[4] = {"Relay 1", "Relay 2", "Aux LED", "RGB LED"};
static const uint8_t OUT_TYPE[4] = {SKRIT_CTRL_RELAY, SKRIT_CTRL_RELAY, SKRIT_CTRL_PWM, SKRIT_CTRL_RGB};
static const int8_t OUT_PIN[4] = {RELAY1_PIN, RELAY2_PIN, LED_PIN, RGB_PIN};
static uint16_t g_pwm[4]; // duty 0..1023 (the Aux LED channel)

#if RGB_PIN >= 0
static CRGB g_leds[RGB_COUNT]; // the addressable strip, driven by FastLED
#endif

static skrit_dev dev;

// Set pixel `px` (or SKRIT_RGB_ALL to fill) and push to the strip. Tracks the
// output's on/off state for the bitmap (lit = any pixel non-black).
static void applyRgb(uint8_t px, uint8_t r, uint8_t g, uint8_t b) {
#if RGB_PIN >= 0
  if (px == SKRIT_RGB_ALL) {
    for (int i = 0; i < RGB_COUNT; i++) g_leds[i] = CRGB(r, g, b);
  } else if (px < RGB_COUNT) {
    g_leds[px] = CRGB(r, g, b);
  }
  FastLED.show();
  uint8_t lit = 0;
  for (int i = 0; i < RGB_COUNT; i++) lit |= g_leds[i].r | g_leds[i].g | g_leds[i].b;
  g_out[RGB_IDX] = lit ? 1 : 0;
#else
  (void)px; (void)r; (void)g; (void)b;
#endif
}

// ---- helpers ---------------------------------------------------------------
static void applyOut(uint8_t idx, uint8_t on) {
  if (idx == RGB_IDX) { applyRgb(SKRIT_RGB_ALL, on ? 64 : 0, on ? 64 : 0, on ? 64 : 0); return; }
  if (idx > 2) return;
  g_out[idx] = on ? 1 : 0;
  g_pwm[idx] = on ? 1023 : 0; // a plain set snaps the duty to the rails
  bool activeLow = (idx == 2) ? LED_ACTIVE_LOW : RELAY_ACTIVE_LOW;
  digitalWrite(OUT_PIN[idx], (on ^ activeLow) ? HIGH : LOW);
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
}

// ---- HAL callbacks ---------------------------------------------------------
static void hal_link_write(void *, const uint8_t *p, uint16_t n) { Serial.write(p, n); }
static void hal_data_write(void *, const uint8_t *p, uint16_t n) { TARGET.write(p, n); }

static uint16_t hal_data_read(void *, uint8_t *out, uint16_t cap) {
  uint16_t k = 0;
  while (TARGET.available() && k < cap) out[k++] = (uint8_t)TARGET.read();
  return k;
}

static void hal_out_set(void *, uint8_t idx, uint8_t on) { applyOut(idx, on); }
static uint8_t hal_out_get(void *, uint8_t idx) { return idx < 4 ? g_out[idx] : 0; }
static void hal_out_desc(void *, uint8_t idx, uint8_t *type, const char **name) {
  if (idx >= 4) return;
  *type = OUT_TYPE[idx];
  *name = OUT_NAME[idx];
}

// PWM rides the Arduino LEDC-backed analogWrite (8-bit) on the LED channel.
// Relays are mechanical — they stay strictly on/off.
static uint8_t hal_pwm_set(void *, uint8_t idx, uint16_t duty) {
  if (idx > 2 || OUT_TYPE[idx] != SKRIT_CTRL_PWM) return 0;
  g_pwm[idx] = duty;
  g_out[idx] = duty > 0;
  uint16_t v = duty >> 2; // 0..1023 -> 0..255
  analogWrite(OUT_PIN[idx], LED_ACTIVE_LOW ? (255 - v) : v);
  return 1;
}
static uint16_t hal_pwm_get(void *, uint8_t idx) { return idx < 4 ? g_pwm[idx] : 0; }

// RGB via FastLED on the WS2812 strip (onboard pixel by default).
static uint8_t hal_rgb_count(void *, uint8_t idx) {
  return (idx == RGB_IDX && RGB_PIN >= 0) ? RGB_COUNT : 0;
}
static uint8_t hal_rgb_set(void *, uint8_t idx, uint8_t px, uint8_t r, uint8_t g, uint8_t b) {
  if (idx != RGB_IDX || RGB_PIN < 0) return 0;
  if (px != SKRIT_RGB_ALL && px >= RGB_COUNT) return 0;
  applyRgb(px, r, g, b);
  return 1;
}
static void hal_rgb_get(void *, uint8_t idx, uint8_t px, uint8_t *r, uint8_t *g, uint8_t *b) {
  (void)idx;
#if RGB_PIN >= 0
  if (px >= RGB_COUNT) px = 0;
  *r = g_leds[px].r; *g = g_leds[px].g; *b = g_leds[px].b;
#else
  (void)px; *r = *g = *b = 0;
#endif
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
#if defined(BOARD_ESP32S3) || defined(BOARD_ESP32C3)
  if (mode == SKRIT_REBOOT_BOOTLOADER) {
    // Latch the ROM download/DFU path, then reset — the native USB re-enumerates
    // as the serial/JTAG downloader (no BOOT-button dance needed).
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
  }
#endif
  esp_restart();
}

static uint32_t hal_millis(void *) { return millis(); }
static void hal_pump(void *) { yield(); }

static const skrit_hal HAL = {
    /*name*/ BOARD_NAME,
    /*fw_ver*/ (FW_HI << 8) | FW_LO,
    /*caps*/ SKRIT_CAP_MUX | SKRIT_CAP_SERIAL | SKRIT_CAP_REBOOT | SKRIT_CAP_PWM,
    /*macro_tier*/ SKRIT_TIER_INTERACTIVE,
    /*store_kb*/ 0,
    /*n_outputs*/ (uint8_t)N_OUTPUTS,
    /*n_inputs*/ 0,
    hal_link_write, hal_data_write, /*host_write*/ nullptr, hal_data_read,
    hal_out_set, hal_out_get, hal_out_desc,
    hal_pwm_set, hal_pwm_get,
    hal_rgb_count, hal_rgb_set, hal_rgb_get,
    /*in_desc*/ nullptr, /*in_get*/ nullptr,
    hal_serial_get, hal_serial_set, hal_serial_signal,
    hal_reboot,
    hal_millis, hal_pump,
};

// ---- Arduino entry points --------------------------------------------------
void setup() {
  for (int i = 0; i < 3; i++) { // relays + Aux LED are plain GPIO
    pinMode(OUT_PIN[i], OUTPUT);
    applyOut(i, 0);
  }
#if RGB_PIN >= 0
  FastLED.addLeds<WS2812, RGB_PIN, GRB>(g_leds, RGB_COUNT);
  FastLED.setBrightness(255);
  applyRgb(SKRIT_RGB_ALL, 0, 0, 0); // start dark
#endif
  if (DTR_PIN >= 0) pinMode(DTR_PIN, OUTPUT);
  if (RTS_PIN >= 0) pinMode(RTS_PIN, OUTPUT);

  Serial.begin(115200); // USB-CDC (S3/C3) or UART0-USB (classic) — the mux link
  beginTarget();

  skrit_dev_init(&dev, &HAL, nullptr, /*muxed*/ 1);
}

void loop() {
  skrit_dev_poll(&dev); // drain target console -> host (wrapped on the mux link)
  while (Serial.available()) skrit_dev_rx(&dev, (uint8_t)Serial.read());
}
