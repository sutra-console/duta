// duta_board_io.h: builds the default duta_outputs[] table from a board's
// ONBOARD signal hardware, so each leaf board header only declares pins.
// ============================================================================
// The compiled default is the board's own onboard outputs and nothing else: a
// status LED (LED_PIN) and/or an addressable RGB (RGB_PIN). A target designates
// those by defining the pin; each row is emitted only if its pin is defined, so
// a board with neither (e.g. the bare XIAO ESP32-C3) gets an empty table.
//
// Relays and other EXTERNAL fixtures are deliberately NOT a compiled default —
// they're a runtime choice: the duta_io[] table is mutable, and the host adds
// them with CONFIG_WRITE (Configure Device). A target wanting a different
// built-in set declares duta_outputs[] itself instead of including this.
// ============================================================================
#ifndef DUTA_BOARD_IO_H
#define DUTA_BOARD_IO_H

#include "duta_io.h"  // duta_io descriptor + DUTA_ACTIVE_LOW
#include "protocol.h" // SKRIT_CTRL_*

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
#if defined(LED_PIN) && (LED_PIN) >= 0
    {SKRIT_CTRL_PWM, LED_PIN, "Status LED", LED_ACTIVE_LOW ? DUTA_ACTIVE_LOW : 0, 0},
#endif
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
// I²C master bus pins, when a target wires them (the DATA medium uses them only
// in i2c mode). DUAL, not FIXED: still offerable for provisioning, but flagged so
// the host warns. Only present when the target defines them (duta_i2c.h's GP8/9
// fallback is included after this header, so generic boards don't reserve).
#if defined(I2C_SCL_PIN) && (I2C_SCL_PIN) >= 0
    {I2C_SCL_PIN, DUTA_USE_DUAL, "I2C SCL"},
#endif
#if defined(I2C_SDA_PIN) && (I2C_SDA_PIN) >= 0
    {I2C_SDA_PIN, DUTA_USE_DUAL, "I2C SDA"},
#endif
};
#define DUTA_ROLE_USES_N ((uint8_t)(sizeof duta_role_uses / sizeof duta_role_uses[0]))
#endif

#endif // DUTA_BOARD_IO_H
