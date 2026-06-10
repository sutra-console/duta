// duta_io.h — the board IO descriptor type (small; board.h includes this).
// ============================================================================
// A board declares WHAT it has as tables of these descriptors, e.g.:
//
//   static const duta_io duta_outputs[] = {
//     { SKRIT_CTRL_IO,  RELAY1_PIN, "Relay 1", DUTA_ACTIVE_LOW },
//     { SKRIT_CTRL_PWM, LED_PIN,    "Aux LED" },
//     { SKRIT_CTRL_RGB, RGB_PIN,    "RGB LED", 0, RGB_COUNT },
//   };
// (type is the *behavior* — IO/PWM/RGB; the fixture goes in the name.)
//
// The generic Arduino driver that turns these into skrit_hal IO callbacks lives
// in duta_io_arduino.h (included by main.cpp after board.h). Splitting the type
// out lets board.h declare the tables before the driver is pulled in.
// ============================================================================
#ifndef DUTA_IO_H
#define DUTA_IO_H

#include <stdint.h>

// duta_io flags
#define DUTA_ACTIVE_LOW 0x01

// One IO line (output or input). `arg` is type-specific: RGB pixel count.
typedef struct duta_io {
  uint8_t type;     // SKRIT_CTRL_* (outputs) or SKRIT_IN_* (inputs)
  int16_t pin;      // GPIO; for RGB the data pin (also fixed via DUTA_RGB_PIN)
  const char *name; // self-describe label
  uint8_t flags;    // DUTA_ACTIVE_LOW
  uint16_t arg;     // RGB: pixel count
} duta_io;

#endif // DUTA_IO_H
