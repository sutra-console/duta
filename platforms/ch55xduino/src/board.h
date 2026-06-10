#ifndef __DUTA_BOARD_H__
#define __DUTA_BOARD_H__

// ---------------------------------------------------------------------------
// Board abstraction.
//
// Exactly one BOARD_* macro selects the pin map below. Define it in config.h
// (or pass -DBOARD_xxx as a build flag). Each board header must define:
//
//   BOARD_NAME        string
//   RELAY1_PIN        ch55xduino pin (Px.y -> x*10+y)
//   RELAY2_PIN
//   RELAY_ACTIVE_LOW  1 if the relay turns ON when the pin is driven LOW
//   LED_PIN           commandable "aux" LED
//   LED_ACTIVE_LOW    1 if that LED is active-low
//   OLED_SCL_PIN      bit-bang I2C clock  (only needed when ENABLE_OLED)
//   OLED_SDA_PIN      bit-bang I2C data
//   OLED_I2C_ADDR     usually 0x3C
//
// UART0 (the DATA bridge) is fixed by the CH552 silicon: P3.1 = TXD, P3.0 = RXD.
// ---------------------------------------------------------------------------

#if defined(BOARD_WEACT_CH552)
#include "boards/weact_ch552.h"
#elif defined(BOARD_GENERIC_CH552)
#include "boards/generic_ch552.h"
#elif defined(BOARD_CUSTOM)
#include "boards/custom.h"
#else
#error "No board selected. Define one of BOARD_WEACT_CH552 / BOARD_GENERIC_CH552 / BOARD_CUSTOM (see src/config.h)."
#endif

// ---- sanity-check the board provided everything ----
#if !defined(BOARD_NAME) || !defined(RELAY1_PIN) || !defined(RELAY2_PIN) ||     \
    !defined(RELAY_ACTIVE_LOW) || !defined(LED_PIN) || !defined(LED_ACTIVE_LOW)
#error "Selected board header is missing required pin definitions (see src/board.h)."
#endif

// OLED pins are only required when the OLED is enabled; give harmless defaults
// otherwise so the gated driver still compiles.
#ifndef OLED_SCL_PIN
#define OLED_SCL_PIN 17
#endif
#ifndef OLED_SDA_PIN
#define OLED_SDA_PIN 16
#endif
#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR 0x3C
#endif

// Inputs are optional. A board declares them by defining INPUT_COUNT (>0) plus
// INPUT_PINS / INPUT_TYPES (0=digital, 1=analog) / INPUT_NAMES arrays.
#ifndef INPUT_COUNT
#define INPUT_COUNT 0
#endif

// Vendor is self-described (grouping in the app); harmless default if a board omits it.
#ifndef BOARD_VENDOR
#define BOARD_VENDOR "Generic"
#endif

#endif
