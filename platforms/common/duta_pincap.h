// duta_pincap.h — the pin-capability vocabulary (the "menu" for provisioning).
// ============================================================================
// Runtime provisioning is "pick a role for a pin from what that pin can be."
// The menu is LAYERED, and each layer only ever *narrows* freedom:
//
//   mcu layer  (mcu/<chip>.h)  — silicon truth: the full pin inventory, each
//                pin's intrinsic capabilities, and an immutable hazard status.
//                Written once per chip, reused by every board on it.
//   board layer (boards/<v>_<b>.h) — the small overlay of physical reality: which
//                mcu pins are broken out to a header, and which are committed to
//                something onboard (fixed = hidden, dual = offered-with-warning).
//
// A pin is provisionable iff  broken_out && committed != FIXED.  The device
// resolves mcu ∩ board into a per-pin status and reports it over PIN_CAPS, so the
// app's picker renders the menu with ZERO hardcoded chip knowledge.
//
// This header is the shared type/vocabulary only — the mcu tables that USE it
// live in each platform's mcu/<chip>.h. Header-only, no deps.
// ============================================================================
#ifndef DUTA_PINCAP_H
#define DUTA_PINCAP_H

#include <stdint.h>

// ---- intrinsic capability bits (what the silicon can drive on this pin) -----
// These ride the wire in PIN_CAPS, so the values are part of the protocol.
enum {
  DUTA_CAP_DIGITAL = 0x01, // digital in/out
  DUTA_CAP_ADC = 0x02,     // analog input (ADC)
  DUTA_CAP_PWM = 0x04,     // PWM / LEDC-capable
  DUTA_CAP_DAC = 0x08,     // true analog out (DAC)
  DUTA_CAP_I2C = 0x10,     // usable as I²C SDA/SCL (see .bus for which bus)
  DUTA_CAP_SPI = 0x20,     // usable as a SPI signal
  DUTA_CAP_TOUCH = 0x40,   // capacitive-touch input
};

// ---- mcu-layer hazard status (silicon truth, immutable) ---------------------
enum {
  DUTA_PIN_FREE = 0,      // safe to provision to anything in .caps
  DUTA_PIN_CAUTION = 1,   // works, but risky — strapping/boot pin; offer + warn
  DUTA_PIN_FORBIDDEN = 2, // never offer — flash/PSRAM/USB/internal
};

// ---- board-layer commitment (overlay; resolved against broken-out) ----------
enum {
  DUTA_USE_NONE = 0,  // uncommitted
  DUTA_USE_FIXED = 1, // wired to onboard hw, not reclaimable -> hide
  DUTA_USE_DUAL = 2,  // wired onboard AND broken out -> offer, but warn
};

#define DUTA_NO_BUS 0xFF // .bus sentinel: not a bus pin (or n/a)

// One MCU pin's silicon truth. A board's mcu/<chip>.h declares an array of these.
typedef struct duta_pin {
  int16_t pin;     // GPIO number
  uint8_t caps;    // DUTA_CAP_* bitmap
  uint8_t status;  // DUTA_PIN_* hazard
  uint8_t bus;     // I²C/SPI bus index for DUTA_CAP_I2C/SPI pins, else DUTA_NO_BUS
} duta_pin;

// One board-layer commitment: pin P is used by onboard hardware `what`.
typedef struct duta_pin_use {
  int16_t pin;       // GPIO number
  uint8_t use;       // DUTA_USE_FIXED / DUTA_USE_DUAL
  const char *what;  // human label, e.g. "onboard LED", "QSPI flash"
} duta_pin_use;

// ---- resolved per-pin status (what PIN_CAPS emits, mcu ∩ board) -------------
// Resolution rule, applied per mcu pin:
//   not broken out OR FORBIDDEN OR USE_FIXED  -> omitted (the app never sees it)
//   USE_DUAL                                  -> emitted with DUTA_RES_WARN + label
//   CAUTION                                   -> emitted with DUTA_RES_WARN
//   else                                      -> emitted clean
enum {
  DUTA_RES_FREE = 0, // offer clean
  DUTA_RES_WARN = 1, // offer, but show the warning string
};

#endif // DUTA_PINCAP_H
