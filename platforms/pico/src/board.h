#ifndef DUTA_PICO_BOARD_H
#define DUTA_PICO_BOARD_H
// Per-board pin map for the pico platform. The PlatformIO env passes a
// -DBOARD_* flag (see platformio.ini); default is the Raspberry Pi Pico (RP2040).
//
// Roles:
//   DATA_TX / DATA_RX   hardware UART bridged to the target console (Serial1/UART0)
//   RELAY1 / RELAY2     two commandable outputs (active-HIGH by default)
//   LED                 a commandable aux LED (onboard GP25 on both Pico boards)
//   DTR / RTS           optional auto-reset lines to the target (-1 = unused);
//                       driven by SERIAL_SIGNAL so a host can enter ESP/AVR
//                       bootloaders. BREAK always works via the UART itself.

#if !defined(BOARD_PICO) && !defined(BOARD_PICO2)
#define BOARD_PICO
#endif

#if defined(BOARD_PICO)
#define BOARD_NAME "Duta Pico"
#define DATA_TX_PIN 0  // GP0 — UART0 TX
#define DATA_RX_PIN 1  // GP1 — UART0 RX
#define RELAY1_PIN 2   // GP2
#define RELAY2_PIN 3   // GP3
#define LED_PIN 25     // GP25 — onboard LED
#define DTR_PIN (-1)
#define RTS_PIN (-1)
#define BOARD_HAS_NATIVE_USB 1 // CMD/DATA muxed over the native USB-CDC

#elif defined(BOARD_PICO2)
#define BOARD_NAME "Duta Pico 2"
#define DATA_TX_PIN 0  // GP0 — UART0 TX
#define DATA_RX_PIN 1  // GP1 — UART0 RX
#define RELAY1_PIN 2   // GP2
#define RELAY2_PIN 3   // GP3
#define LED_PIN 25     // GP25 — onboard LED (non-W variant)
#define DTR_PIN (-1)
#define RTS_PIN (-1)
#define BOARD_HAS_NATIVE_USB 1
#endif

#ifndef RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LOW 0
#endif
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0
#endif

// ---- IO table: WHAT this board exposes (driven by duta_io_arduino.h) -------
// The Pico has no onboard addressable LED, so no RGB row. Wire a WS2812 strip
// and add { SKRIT_CTRL_RGB, <pin>, "Strip", 0, <count> } + DUTA_RGB_PIN/COUNT.
#include "protocol.h" // SKRIT_CTRL_*
#include "duta_io.h"  // duta_io descriptor + DUTA_ACTIVE_LOW

static const duta_io duta_outputs[] = {
    {SKRIT_CTRL_IO, RELAY1_PIN, "Relay 1", RELAY_ACTIVE_LOW ? DUTA_ACTIVE_LOW : 0, 0},
    {SKRIT_CTRL_IO, RELAY2_PIN, "Relay 2", RELAY_ACTIVE_LOW ? DUTA_ACTIVE_LOW : 0, 0},
    {SKRIT_CTRL_PWM, LED_PIN, "Aux LED", LED_ACTIVE_LOW ? DUTA_ACTIVE_LOW : 0, 0},
};

#endif // DUTA_PICO_BOARD_H
