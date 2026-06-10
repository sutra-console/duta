// Duta on Raspberry Pi Pico / Pico 2 (RP2040 / RP2350, Arduino + PlatformIO).
// ============================================================================
// A real Duta adapter built on the shared core (../../common/skrit_device.h):
//
//   * DATA console  : a hardware UART (Serial1 = UART0 on DATA_TX/DATA_RX)
//                     bridged to the target, full duplex.
//   * CMD + DATA    : multiplexed over the single native USB-CDC via skrit-mux.
//   * Controls      : declared as a table in board.h, driven by the shared
//                     duta_io driver (2 digital outputs + a PWM LED).
//   * Macro VM      : skrit-mc tiers 1-2; scratch push-and-run.
//   * Serial control: SERIAL_GET/SET reconfigures the DATA UART; SERIAL_SIGNAL
//                     drives DTR/RTS (auto-reset into ESP/AVR bootloaders) + BREAK.
//   * REBOOT        : app reset, or reboot-to-UF2-bootloader (BOOTSEL/DFU).
//
// IO is table-driven (board.h `duta_outputs[]` + duta_io_arduino.h); this file
// is just the transport, serial, and reboot HAL plus the two-line main loop.
#include <Arduino.h>

#include "board.h"            // declares the duta_outputs[] table
#include "duta_io_arduino.h"  // generic IO driver -> skrit_hal IO callbacks
extern "C" {
#include "skrit_device.h"
}

#define FW_LO 0x04
#define FW_HI 0x00

// Serial1 is UART0, the bridged target console. `Serial` is the native USB-CDC,
// which carries the mux link (CMD + DATA) to the host.
static SerialUART &TARGET = Serial1;

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
  TARGET.setTX(DATA_TX_PIN);
  TARGET.setRX(DATA_RX_PIN);
  TARGET.begin(g_baud, serialConfig(g_bits, g_parity, g_stop));
}

// ---- transport + serial HAL (IO callbacks come from duta_io_arduino.h) ------
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
  if (mode == SKRIT_REBOOT_BOOTLOADER) {
    // Drop into the RP2040/RP2350 ROM UF2 bootloader; the native USB
    // re-enumerates as the RPI-RP2 mass-storage device (no BOOTSEL dance).
    rp2040.rebootToBootloader();
  } else {
    rp2040.reboot();
  }
}

static uint32_t hal_millis(void *) { return millis(); }
static void hal_pump(void *) {} // earlephilhower core services USB in the background

// caps: advertise PWM only if the board table actually has a PWM output.
static uint8_t board_caps() {
  uint8_t caps = SKRIT_CAP_MUX | SKRIT_CAP_SERIAL | SKRIT_CAP_REBOOT;
  for (uint8_t i = 0; i < DUTA_N_OUTPUTS; i++)
    if (duta_outputs[i].type == SKRIT_CTRL_PWM) caps |= SKRIT_CAP_PWM;
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

  Serial.begin(115200); // native USB-CDC: the mux link
  beginTarget();

  HAL.caps = board_caps();
  HAL.pwm_config_get = duta_io_pwm_config_get; // freq + resolution (trailing HAL fields)
  HAL.pwm_config_set = duta_io_pwm_config_set;
  skrit_dev_init(&dev, &HAL, nullptr, /*muxed*/ 1);
}

void loop() {
  skrit_dev_poll(&dev); // drain target console -> host (wrapped on the mux link)
  while (Serial.available()) skrit_dev_rx(&dev, (uint8_t)Serial.read());
}
