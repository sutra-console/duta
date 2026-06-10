// Duta on Raspberry Pi Pico / Pico 2 (RP2040 / RP2350, Arduino + PlatformIO).
// ============================================================================
// A real Duta adapter built on the shared core (../../common/skrit_device.h):
//
//   * DATA console  — a hardware UART (Serial1 = UART0 on DATA_TX/DATA_RX)
//                     bridged to the target, full duplex.
//   * CMD + DATA    — multiplexed over the single native USB-CDC via skrit-mux,
//                     so the Sutra app opens ONE port and gets both.
//   * Controls      — 2 relays + an aux LED, self-describing.
//   * Macro VM      — skrit-mc tiers 1-2 (EMIT/DELAY/SETOUT/EXPECT/WAITIO),
//                     scratch push-and-run (no flash wear).
//   * Serial control— SERIAL_GET/SET reconfigures the DATA UART; SERIAL_SIGNAL
//                     drives DTR/RTS (auto-reset into ESP/AVR bootloaders) + BREAK.
//   * REBOOT        — app reset, or reboot-to-UF2-bootloader (BOOTSEL/DFU).
//
// The protocol/CRC/COBS/VM all live in the shared core; this file is just the
// RP2040/RP2350 HAL + the two-line main loop.
#include <Arduino.h>

#include "board.h"
extern "C" {
#include "skrit_device.h"
}

#define FW_LO 0x04
#define FW_HI 0x00

// Serial1 is UART0 — the bridged target console. `Serial` is the native USB-CDC,
// which carries the mux link (CMD + DATA) to the host.
static SerialUART &TARGET = Serial1;

static uint32_t g_baud = 115200;
static uint8_t g_bits = 8, g_parity = SKRIT_PAR_NONE, g_stop = 1;
static uint8_t g_out[3]; // relay1, relay2, led

static const char *const OUT_NAME[3] = {"Relay 1", "Relay 2", "Aux LED"};
static const uint8_t OUT_TYPE[3] = {SKRIT_CTRL_RELAY, SKRIT_CTRL_RELAY, SKRIT_CTRL_PWM};
static uint16_t g_pwm[3]; // duty 0..1023 (only the LED channel is PWM-capable)
static const int8_t OUT_PIN[3] = {RELAY1_PIN, RELAY2_PIN, LED_PIN};

static skrit_dev dev;

// ---- helpers ---------------------------------------------------------------
static void applyOut(uint8_t idx, uint8_t on) {
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
  TARGET.setTX(DATA_TX_PIN);
  TARGET.setRX(DATA_RX_PIN);
  TARGET.begin(g_baud, serialConfig(g_bits, g_parity, g_stop));
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
static uint8_t hal_out_get(void *, uint8_t idx) { return idx < 3 ? g_out[idx] : 0; }
static void hal_out_desc(void *, uint8_t idx, uint8_t *type, const char **name) {
  if (idx > 2) return;
  *type = OUT_TYPE[idx];
  *name = OUT_NAME[idx];
}

// PWM rides the hardware-PWM-backed analogWrite on the LED channel (the range
// is set to 0..1023 in setup). Relays are mechanical — strictly on/off.
static uint8_t hal_pwm_set(void *, uint8_t idx, uint16_t duty) {
  if (idx > 2 || OUT_TYPE[idx] != SKRIT_CTRL_PWM) return 0;
  g_pwm[idx] = duty;
  g_out[idx] = duty > 0;
  analogWrite(OUT_PIN[idx], LED_ACTIVE_LOW ? (1023 - duty) : duty);
  return 1;
}
static uint16_t hal_pwm_get(void *, uint8_t idx) { return idx < 3 ? g_pwm[idx] : 0; }

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
  if (mode == SKRIT_REBOOT_BOOTLOADER) {
    // Drop into the RP2040/RP2350 ROM UF2 bootloader — the native USB
    // re-enumerates as the RPI-RP2 mass-storage device (no BOOTSEL dance).
    rp2040.rebootToBootloader();
  } else {
    rp2040.reboot();
  }
}

static uint32_t hal_millis(void *) { return millis(); }
static void hal_pump(void *) {} // earlephilhower core services USB in the background

static const skrit_hal HAL = {
    /*name*/ BOARD_NAME,
    /*fw_ver*/ (FW_HI << 8) | FW_LO,
    /*caps*/ SKRIT_CAP_MUX | SKRIT_CAP_SERIAL | SKRIT_CAP_REBOOT | SKRIT_CAP_PWM,
    /*macro_tier*/ SKRIT_TIER_INTERACTIVE,
    /*store_kb*/ 0,
    /*n_outputs*/ 3,
    /*n_inputs*/ 0,
    hal_link_write, hal_data_write, /*host_write*/ nullptr, hal_data_read,
    hal_out_set, hal_out_get, hal_out_desc,
    hal_pwm_set, hal_pwm_get,
    /*rgb_count*/ nullptr, /*rgb_set*/ nullptr, /*rgb_get*/ nullptr, // no addressable LED
    /*in_desc*/ nullptr, /*in_get*/ nullptr,
    hal_serial_get, hal_serial_set, hal_serial_signal,
    hal_reboot,
    hal_millis, hal_pump,
};

// ---- Arduino entry points --------------------------------------------------
void setup() {
  for (int i = 0; i < 3; i++) {
    pinMode(OUT_PIN[i], OUTPUT);
    applyOut(i, 0);
  }
  if (DTR_PIN >= 0) pinMode(DTR_PIN, OUTPUT);
  if (RTS_PIN >= 0) pinMode(RTS_PIN, OUTPUT);
  analogWriteRange(1023); // PWM duty in skrit units (OUT_PWM is 0..1023)

  Serial.begin(115200); // native USB-CDC — the mux link
  beginTarget();

  skrit_dev_init(&dev, &HAL, nullptr, /*muxed*/ 1);
}

void loop() {
  skrit_dev_poll(&dev); // drain target console -> host (wrapped on the mux link)
  while (Serial.available()) skrit_dev_rx(&dev, (uint8_t)Serial.read());
}
