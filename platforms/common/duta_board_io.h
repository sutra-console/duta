// duta_board_io.h: builds the standard duta_outputs[] table from a board's role
// macros, so each leaf board header only declares pins, not the table boilerplate.
// ============================================================================
// A board header defines RELAY1_PIN / RELAY2_PIN / LED_PIN (and optionally
// RGB_PIN / RGB_COUNT, RELAY_ACTIVE_LOW, LED_ACTIVE_LOW), then includes this.
// The table is the *compiled default*; runtime provisioning may replace it.
// A board with a non-standard IO set can skip this and declare duta_outputs[]
// itself (the shared Arduino driver only requires the array to exist).
// ============================================================================
#ifndef DUTA_BOARD_IO_H
#define DUTA_BOARD_IO_H

#include "duta_io.h"  // duta_io descriptor + DUTA_ACTIVE_LOW
#include "protocol.h" // SKRIT_CTRL_*

#ifndef RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LOW 0
#endif
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0
#endif
#ifndef RGB_PIN
#define RGB_PIN (-1)
#endif
#ifndef RGB_COUNT
#define RGB_COUNT 1
#endif

#if RGB_PIN >= 0
#define DUTA_RGB_PIN RGB_PIN // FastLED needs the pin as a compile-time arg
#define DUTA_RGB_COUNT RGB_COUNT
#ifdef RGB_ORDER
#define DUTA_RGB_ORDER RGB_ORDER // board-specific color order (default GRB)
#endif
#endif

static const duta_io duta_outputs[] = {
    {SKRIT_CTRL_IO, RELAY1_PIN, "Relay 1", RELAY_ACTIVE_LOW ? DUTA_ACTIVE_LOW : 0, 0},
    {SKRIT_CTRL_IO, RELAY2_PIN, "Relay 2", RELAY_ACTIVE_LOW ? DUTA_ACTIVE_LOW : 0, 0},
    {SKRIT_CTRL_PWM, LED_PIN, "Aux LED", LED_ACTIVE_LOW ? DUTA_ACTIVE_LOW : 0, 0},
#if RGB_PIN >= 0
    {SKRIT_CTRL_RGB, RGB_PIN, "RGB LED", 0, RGB_COUNT},
#endif
};

// Role pins (the DATA bridge UART + optional DTR/RTS) are committed hardware:
// reserve them from the provisioning menu so a relay can't land on the console.
// Hosts list them via CFG_GET DATA_PINS.
#ifdef DUTA_PINCAP_H
static const duta_pin_use duta_role_uses[] = {
    {DATA_TX_PIN, DUTA_USE_FIXED, "DATA UART TX"},
    {DATA_RX_PIN, DUTA_USE_FIXED, "DATA UART RX"},
#if defined(DTR_PIN) && (DTR_PIN) >= 0
    {DTR_PIN, DUTA_USE_FIXED, "target DTR"},
#endif
#if defined(RTS_PIN) && (RTS_PIN) >= 0
    {RTS_PIN, DUTA_USE_FIXED, "target RTS"},
#endif
};
#define DUTA_ROLE_USES_N ((uint8_t)(sizeof duta_role_uses / sizeof duta_role_uses[0]))
#endif

#endif // DUTA_BOARD_IO_H
