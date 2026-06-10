// duta_io_arduino.h — generic, table-driven Arduino IO driver (header-only).
// ============================================================================
// IS the skrit_hal IO callbacks (outputs, PWM, RGB, inputs) for the board's
// `duta_outputs[]` / `duta_inputs[]` tables (see duta_io.h). A platform's
// main.cpp includes board.h then this, and points the HAL's IO fields at the
// duta_io_* functions — no hand-wired per-pin logic. Adding a relay is one row.
//
// Shared by the Arduino platforms (espressif, pico). Zephyr drives IO from its
// devicetree instead. Requires (from board.h, included first):
//   static const duta_io duta_outputs[] = { ... };          // required
//   static const duta_io duta_inputs[]  = { ... }; + #define DUTA_HAVE_INPUTS
//   #define DUTA_RGB_PIN <pin> / #define DUTA_RGB_COUNT <n>  // if an RGB row
// ============================================================================
#ifndef DUTA_IO_ARDUINO_H
#define DUTA_IO_ARDUINO_H

#include <Arduino.h>

#include "duta_io.h"
#include "protocol.h"

#ifndef DUTA_MAX_OUTPUTS
#define DUTA_MAX_OUTPUTS 12
#endif
#ifndef DUTA_PWM_MAX
#define DUTA_PWM_MAX 1023 // skrit PWM duty range; we set 10-bit resolution
#endif

#define DUTA_N_OUTPUTS ((uint8_t)(sizeof duta_outputs / sizeof duta_outputs[0]))
#ifdef DUTA_HAVE_INPUTS
#define DUTA_N_INPUTS ((uint8_t)(sizeof duta_inputs / sizeof duta_inputs[0]))
#else
#define DUTA_N_INPUTS 0
#endif

#if defined(DUTA_RGB_PIN) && DUTA_RGB_PIN >= 0
#define FASTLED_INTERNAL // silence the version banner
#include <FastLED.h>
#ifndef DUTA_RGB_COUNT
#define DUTA_RGB_COUNT 1
#endif
static CRGB duta_leds[DUTA_RGB_COUNT];
#define DUTA_HAS_RGB 1
#else
#define DUTA_HAS_RGB 0
#endif

// per-output state owned by the driver
static uint8_t duta_on[DUTA_MAX_OUTPUTS];    // digital on/off latch
static uint16_t duta_duty[DUTA_MAX_OUTPUTS]; // pwm duty 0..DUTA_PWM_MAX

// ---- helpers ---------------------------------------------------------------
static inline void duta__write_digital(const duta_io *io, uint8_t on) {
  uint8_t lvl = (io->flags & DUTA_ACTIVE_LOW) ? !on : on;
  digitalWrite(io->pin, lvl ? HIGH : LOW);
}

static inline void duta__write_pwm(const duta_io *io, uint16_t duty) {
  uint16_t v = (io->flags & DUTA_ACTIVE_LOW) ? (DUTA_PWM_MAX - duty) : duty;
  analogWrite(io->pin, v);
}

#if DUTA_HAS_RGB
static inline uint8_t duta__rgb_lit(void) {
  uint8_t lit = 0;
  for (int i = 0; i < DUTA_RGB_COUNT; i++) lit |= duta_leds[i].r | duta_leds[i].g | duta_leds[i].b;
  return lit ? 1 : 0;
}
#endif

// ---- skrit_hal IO callbacks (signatures match skrit_hal exactly) -----------
static inline void duta_io_out_set(void *ctx, uint8_t idx, uint8_t on) {
  (void)ctx;
  if (idx >= DUTA_N_OUTPUTS) return;
  const duta_io *io = &duta_outputs[idx];
  switch (io->type) {
  case SKRIT_CTRL_PWM:
    duta_duty[idx] = on ? DUTA_PWM_MAX : 0; // a plain set snaps to the rails
    duta__write_pwm(io, duta_duty[idx]);
    duta_on[idx] = on ? 1 : 0;
    break;
  case SKRIT_CTRL_RGB:
#if DUTA_HAS_RGB
    for (int i = 0; i < DUTA_RGB_COUNT; i++) duta_leds[i] = on ? CRGB(64, 64, 64) : CRGB::Black;
    FastLED.show();
#endif
    break;
  default: // plain digital on/off (relay, LED, reset line — see the name)
    duta_on[idx] = on ? 1 : 0;
    duta__write_digital(io, on);
    break;
  }
}

static inline uint8_t duta_io_out_get(void *ctx, uint8_t idx) {
  (void)ctx;
  if (idx >= DUTA_N_OUTPUTS) return 0;
  const duta_io *io = &duta_outputs[idx];
  if (io->type == SKRIT_CTRL_PWM) return duta_duty[idx] > 0;
#if DUTA_HAS_RGB
  if (io->type == SKRIT_CTRL_RGB) return duta__rgb_lit();
#endif
  return duta_on[idx];
}

static inline void duta_io_out_desc(void *ctx, uint8_t idx, uint8_t *type, const char **name) {
  (void)ctx;
  if (idx >= DUTA_N_OUTPUTS) return;
  *type = duta_outputs[idx].type;
  *name = duta_outputs[idx].name;
}

static inline uint8_t duta_io_pwm_set(void *ctx, uint8_t idx, uint16_t duty) {
  (void)ctx;
  if (idx >= DUTA_N_OUTPUTS || duta_outputs[idx].type != SKRIT_CTRL_PWM) return 0;
  if (duty > DUTA_PWM_MAX) duty = DUTA_PWM_MAX;
  duta_duty[idx] = duty;
  duta__write_pwm(&duta_outputs[idx], duty);
  return 1;
}

static inline uint16_t duta_io_pwm_get(void *ctx, uint8_t idx) {
  (void)ctx;
  return idx < DUTA_N_OUTPUTS ? duta_duty[idx] : 0;
}

static inline uint8_t duta_io_rgb_count(void *ctx, uint8_t idx) {
  (void)ctx;
#if DUTA_HAS_RGB
  if (idx < DUTA_N_OUTPUTS && duta_outputs[idx].type == SKRIT_CTRL_RGB) return DUTA_RGB_COUNT;
#else
  (void)idx;
#endif
  return 0;
}

static inline uint8_t duta_io_rgb_set(void *ctx, uint8_t idx, uint8_t px, uint8_t r, uint8_t g, uint8_t b) {
  (void)ctx;
#if DUTA_HAS_RGB
  if (idx >= DUTA_N_OUTPUTS || duta_outputs[idx].type != SKRIT_CTRL_RGB) return 0;
  if (px == SKRIT_RGB_ALL) {
    for (int i = 0; i < DUTA_RGB_COUNT; i++) duta_leds[i] = CRGB(r, g, b);
  } else if (px < DUTA_RGB_COUNT) {
    duta_leds[px] = CRGB(r, g, b);
  } else {
    return 0;
  }
  FastLED.show();
  return 1;
#else
  (void)idx; (void)px; (void)r; (void)g; (void)b;
  return 0;
#endif
}

static inline void duta_io_rgb_get(void *ctx, uint8_t idx, uint8_t px, uint8_t *r, uint8_t *g, uint8_t *b) {
  (void)ctx; (void)idx;
#if DUTA_HAS_RGB
  if (px >= DUTA_RGB_COUNT) px = 0;
  *r = duta_leds[px].r; *g = duta_leds[px].g; *b = duta_leds[px].b;
#else
  (void)px; *r = *g = *b = 0;
#endif
}

#ifdef DUTA_HAVE_INPUTS
static inline void duta_io_in_desc(void *ctx, uint8_t idx, uint8_t *type, const char **name) {
  (void)ctx;
  if (idx >= DUTA_N_INPUTS) return;
  *type = duta_inputs[idx].type;
  *name = duta_inputs[idx].name;
}
static inline uint16_t duta_io_in_get(void *ctx, uint8_t idx) {
  (void)ctx;
  if (idx >= DUTA_N_INPUTS) return 0;
  const duta_io *io = &duta_inputs[idx];
  return io->type == SKRIT_IN_ANALOG ? (uint16_t)analogRead(io->pin) : (digitalRead(io->pin) ? 1 : 0);
}
#endif

// Configure pins from the table. Call from setup() before skrit_dev_init.
static inline void duta_io_begin(void) {
#if defined(ARDUINO_ARCH_RP2040)
  analogWriteRange(DUTA_PWM_MAX);
#else
  analogWriteResolution(10); // 0..1023, matches DUTA_PWM_MAX
#endif
  for (uint8_t i = 0; i < DUTA_N_OUTPUTS; i++) {
    const duta_io *io = &duta_outputs[i];
    if (io->type == SKRIT_CTRL_RGB) continue; // driven by FastLED, not a GPIO
    pinMode(io->pin, OUTPUT);
    duta_io_out_set(NULL, i, 0);
  }
#if DUTA_HAS_RGB
  FastLED.addLeds<WS2812, DUTA_RGB_PIN, GRB>(duta_leds, DUTA_RGB_COUNT);
  FastLED.setBrightness(255);
  FastLED.clear(true);
#endif
#ifdef DUTA_HAVE_INPUTS
  for (uint8_t i = 0; i < DUTA_N_INPUTS; i++)
    if (duta_inputs[i].type == SKRIT_IN_DIGITAL) pinMode(duta_inputs[i].pin, INPUT);
#endif
}

#endif // DUTA_IO_ARDUINO_H
