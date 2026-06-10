// duta_io_arduino.h — generic, table-driven Arduino IO driver (header-only).
// ============================================================================
// IS the skrit_hal IO callbacks (outputs, PWM, RGB, inputs) for the board's
// `duta_outputs[]` / `duta_inputs[]` tables (see duta_io.h). A platform's
// main.cpp includes board.h then this, and points the HAL's IO fields at the
// duta_io_* functions — no hand-wired per-pin logic. Adding a relay is one row.
//
// The driver operates on an *active table* pointer (`duta_tbl`/`duta_tbl_n`) that
// defaults to the board's compiled `duta_outputs[]`. Defining DUTA_PROVISION
// repoints it at a RAM table loaded from persistence (runtime provisioning) and
// supplies the pin_caps/config_get/config_set HAL callbacks. Without it, the
// driver is byte-for-byte the compiled-table behavior.
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
#ifdef DUTA_PROVISION
#include <string.h>      // memcpy for the table loader / persistence
#include "duta_pincap.h" // the mcu pin table + board overlay live here / in board.h
#endif

#ifndef DUTA_MAX_OUTPUTS
#define DUTA_MAX_OUTPUTS 12
#endif
#ifndef DUTA_PWM_MAX
#define DUTA_PWM_MAX 1023 // wire duty range (normalized; independent of hw resolution)
#endif
#ifndef DUTA_PWM_FREQ
#define DUTA_PWM_FREQ 1000 // default PWM frequency (Hz)
#endif
#ifndef DUTA_PWM_RES
#define DUTA_PWM_RES 10 // default hardware resolution (bits)
#endif
// Current (settable) PWM frequency + resolution. Wire duty stays 0..DUTA_PWM_MAX;
// duta__write_pwm rescales it to the hardware resolution.
static uint32_t duta_pwm_freq = DUTA_PWM_FREQ;
static uint8_t duta_pwm_res = DUTA_PWM_RES;

#define DUTA_N_OUTPUTS ((uint8_t)(sizeof duta_outputs / sizeof duta_outputs[0]))
#ifdef DUTA_HAVE_INPUTS
#define DUTA_N_INPUTS ((uint8_t)(sizeof duta_inputs / sizeof duta_inputs[0]))
#else
#define DUTA_N_INPUTS 0
#endif

// Active output table. Defaults to the compiled board table; DUTA_PROVISION
// repoints it at the RAM table in duta_io_load(). The const-view pointer accepts
// both the const default and the RAM array.
static const duta_io *duta_tbl = duta_outputs;
static uint8_t duta_tbl_n = DUTA_N_OUTPUTS;

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
  uint32_t hwmax = (1u << duta_pwm_res) - 1; // rescale wire duty -> hardware range
  uint32_t v = (uint32_t)duty * hwmax / DUTA_PWM_MAX;
  if (io->flags & DUTA_ACTIVE_LOW) v = hwmax - v;
  analogWrite(io->pin, (int)v);
}

// Apply the current PWM frequency + resolution to the analog peripheral.
// arduino-esp32 3.x made these PER-PIN (and they need the LEDC channel already
// attached, i.e. call after the first analogWrite); 2.x and RP2040 are global.
static inline void duta__pwm_apply(void) {
#if defined(ARDUINO_ARCH_RP2040)
  analogWriteFreq(duta_pwm_freq);
  analogWriteResolution(duta_pwm_res);
#elif defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  for (uint8_t i = 0; i < duta_tbl_n; i++)
    if (duta_tbl[i].type == SKRIT_CTRL_PWM) {
      analogWriteFrequency((uint8_t)duta_tbl[i].pin, duta_pwm_freq);
      analogWriteResolution((uint8_t)duta_tbl[i].pin, duta_pwm_res);
    }
#else
  analogWriteFrequency(duta_pwm_freq); // ESP32 core 2.x global
  analogWriteResolution(duta_pwm_res);
#endif
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
  if (idx >= duta_tbl_n) return;
  const duta_io *io = &duta_tbl[idx];
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
  if (idx >= duta_tbl_n) return 0;
  const duta_io *io = &duta_tbl[idx];
  if (io->type == SKRIT_CTRL_PWM) return duta_duty[idx] > 0;
#if DUTA_HAS_RGB
  if (io->type == SKRIT_CTRL_RGB) return duta__rgb_lit();
#endif
  return duta_on[idx];
}

static inline void duta_io_out_desc(void *ctx, uint8_t idx, uint8_t *type, const char **name) {
  (void)ctx;
  if (idx >= duta_tbl_n) return;
  *type = duta_tbl[idx].type;
  *name = duta_tbl[idx].name;
}

static inline uint8_t duta_io_pwm_set(void *ctx, uint8_t idx, uint16_t duty) {
  (void)ctx;
  if (idx >= duta_tbl_n || duta_tbl[idx].type != SKRIT_CTRL_PWM) return 0;
  if (duty > DUTA_PWM_MAX) duty = DUTA_PWM_MAX;
  duta_duty[idx] = duty;
  duta__write_pwm(&duta_tbl[idx], duty);
  return 1;
}

static inline uint16_t duta_io_pwm_get(void *ctx, uint8_t idx) {
  (void)ctx;
  return idx < duta_tbl_n ? duta_duty[idx] : 0;
}

// PWM frequency + resolution (shared across the board's PWM outputs).
static inline void duta_io_pwm_config_get(void *ctx, uint8_t idx, uint32_t *freq, uint8_t *res) {
  (void)ctx;
  if (idx < duta_tbl_n && duta_tbl[idx].type == SKRIT_CTRL_PWM) {
    *freq = duta_pwm_freq;
    *res = duta_pwm_res;
  } else {
    *freq = 0;
    *res = 0;
  }
}
static inline uint8_t duta_io_pwm_config_set(void *ctx, uint8_t idx, uint32_t freq, uint8_t res) {
  (void)ctx;
  if (idx >= duta_tbl_n || duta_tbl[idx].type != SKRIT_CTRL_PWM) return 0;
  if (freq) duta_pwm_freq = freq;
  if (res >= 1 && res <= 16) duta_pwm_res = res;
  duta__pwm_apply();
  // re-assert current duties at the new scale
  for (uint8_t i = 0; i < duta_tbl_n; i++)
    if (duta_tbl[i].type == SKRIT_CTRL_PWM) duta__write_pwm(&duta_tbl[i], duta_duty[i]);
  return 1;
}

static inline uint8_t duta_io_rgb_count(void *ctx, uint8_t idx) {
  (void)ctx;
#if DUTA_HAS_RGB
  if (idx < duta_tbl_n && duta_tbl[idx].type == SKRIT_CTRL_RGB) return DUTA_RGB_COUNT;
#else
  (void)idx;
#endif
  return 0;
}

static inline uint8_t duta_io_rgb_set(void *ctx, uint8_t idx, uint8_t px, uint8_t r, uint8_t g, uint8_t b) {
  (void)ctx;
#if DUTA_HAS_RGB
  if (idx >= duta_tbl_n || duta_tbl[idx].type != SKRIT_CTRL_RGB) return 0;
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

// ===========================================================================
// Runtime provisioning (DUTA_PROVISION) — the mcu ∩ board resolver, the current
// table reader, and the validated table writer + persistence. The board header
// supplies the mcu pin table (duta_mcu_pins / DUTA_MCU_NPINS) and the overlay
// (DUTA_BROKEN_OUT_ALL or duta_board_broken_out[]; duta_board_uses[]/DUTA_USES_N).
// ===========================================================================
#ifdef DUTA_PROVISION
#ifndef DUTA_NAME_POOL
#define DUTA_NAME_POOL 192
#endif
// A board with no committed pins just doesn't declare duta_board_uses[]; give a
// harmless placeholder so the resolver loop always compiles. (DUTA_USES_N is a
// sizeof expression on real boards — runtime-checked, not #if-able.)
#ifndef DUTA_USES_N
static const duta_pin_use duta_board_uses[] = {{-1, DUTA_USE_NONE, ""}};
#define DUTA_USES_N 0
#endif

static duta_io duta_io_ram[DUTA_MAX_OUTPUTS]; // the provisioned table (loaded at boot)
static char duta_io_namepool[DUTA_NAME_POOL]; // RAM backing for provisioned names

// Persistence hooks. A platform defines DUTA_HAVE_STORE + these three to persist
// across reboots (ESP32 NVS, RP2040 LittleFS). The default is a RAM buffer —
// provisioning works for the session but reverts on power-cycle.
#ifndef DUTA_HAVE_STORE
static uint8_t duta__store[DUTA_MAX_OUTPUTS * 40 + 8];
static uint16_t duta__store_n = 0;
static uint16_t duta_io_store_load(uint8_t *buf, uint16_t cap) {
  if (duta__store_n == 0 || duta__store_n > cap) return 0;
  memcpy(buf, duta__store, duta__store_n);
  return duta__store_n;
}
static uint8_t duta_io_store_save(const uint8_t *buf, uint16_t n) {
  if (n > sizeof duta__store) return 0;
  memcpy(duta__store, buf, n);
  duta__store_n = n;
  return 1;
}
static void duta_io_store_clear(void) { duta__store_n = 0; }
#endif

// Is `pin` broken out on this board?
static uint8_t duta__pin_broken_out(int16_t pin) {
#ifdef DUTA_BROKEN_OUT_ALL
  (void)pin;
  return 1;
#else
  for (uint8_t i = 0; i < DUTA_BROKEN_OUT_N; i++)
    if (duta_board_broken_out[i] == pin) return 1;
  return 0;
#endif
}

// The board-layer commitment for `pin`: DUTA_USE_NONE / FIXED / DUAL (+ label).
static uint8_t duta__pin_use(int16_t pin, const char **what) {
  for (uint8_t i = 0; i < (uint8_t)(DUTA_USES_N); i++)
    if (duta_board_uses[i].pin == pin) {
      if (what) *what = duta_board_uses[i].what;
      return duta_board_uses[i].use;
    }
  if (what) *what = "";
  return DUTA_USE_NONE;
}

// pin_caps HAL: enumerate the offerable pins (mcu ∩ board). Returns total; when
// index < total, fills the index-th offerable pin's resolved info.
static uint8_t duta_io_pin_caps(void *ctx, uint8_t index, int16_t *pin, uint8_t *caps,
                                uint8_t *warn, uint8_t *bus, const char **name) {
  (void)ctx;
  uint8_t total = 0;
  for (uint8_t i = 0; i < DUTA_MCU_NPINS; i++) {
    const duta_pin *mp = &duta_mcu_pins[i];
    if (mp->status == DUTA_PIN_FORBIDDEN) continue;
    if (!duta__pin_broken_out(mp->pin)) continue;
    const char *what = "";
    uint8_t use = duta__pin_use(mp->pin, &what);
    if (use == DUTA_USE_FIXED) continue; // committed + not reclaimable -> hidden
    if (total == index) {               // this is the one the caller asked for
      *pin = mp->pin;
      *caps = mp->caps;
      *bus = mp->bus;
      if (use == DUTA_USE_DUAL) { *warn = SKRIT_PIN_WARN; *name = what; }
      else if (mp->status == DUTA_PIN_CAUTION) { *warn = SKRIT_PIN_WARN; *name = "strapping/boot pin"; }
      else { *warn = SKRIT_PIN_CLEAN; *name = ""; }
    }
    total++;
  }
  return total;
}

// config_get HAL: the current IO table, one row per index.
static uint8_t duta_io_config_get(void *ctx, uint8_t index, uint8_t *type, int16_t *pin,
                                  uint8_t *flags, uint16_t *arg, const char **name) {
  (void)ctx;
  if (index < duta_tbl_n) {
    *type = duta_tbl[index].type;
    *pin = duta_tbl[index].pin;
    *flags = duta_tbl[index].flags;
    *arg = duta_tbl[index].arg;
    *name = duta_tbl[index].name;
  }
  return duta_tbl_n;
}

// Validate one provisioning row: the pin must be offerable, and the role must fit
// the pin's caps. RGB is only allowed on the compiled RGB pin (FastLED is fixed).
// A FIXED pin (e.g. the Pico's onboard GP25 LED) may be KEPT in its compiled-
// default role — "if the LED can't move, it's always the LED" — just never
// repurposed; pins absent from the mcu map are likewise compiled-default-only.
static uint8_t duta__keeps_default_role(uint8_t type, int16_t pin) {
  for (uint8_t i = 0; i < DUTA_N_OUTPUTS; i++)
    if (duta_outputs[i].pin == pin) return duta_outputs[i].type == type;
  return 0;
}
static uint8_t duta__row_ok(uint8_t type, int16_t pin) {
  // find the pin in the mcu table
  const duta_pin *mp = 0;
  for (uint8_t i = 0; i < DUTA_MCU_NPINS; i++)
    if (duta_mcu_pins[i].pin == pin) { mp = &duta_mcu_pins[i]; break; }
  if (!mp || mp->status == DUTA_PIN_FORBIDDEN || !duta__pin_broken_out(pin) ||
      duta__pin_use(pin, 0) == DUTA_USE_FIXED)
    return duta__keeps_default_role(type, pin);
  if (type == SKRIT_CTRL_IO) return (mp->caps & DUTA_CAP_DIGITAL) ? 1 : 0;
  if (type == SKRIT_CTRL_PWM) return (mp->caps & DUTA_CAP_PWM) ? 1 : 0;
#if DUTA_HAS_RGB
  if (type == SKRIT_CTRL_RGB) return pin == DUTA_RGB_PIN; // fixed FastLED pin only
#endif
  return 0;
}

// config_set HAL: validate + persist a new table (body = n + rows). n=RESET wipes.
static uint8_t duta_io_config_set(void *ctx, const uint8_t *body, uint8_t len, uint8_t *bad_index) {
  (void)ctx;
  if (len < 1) return SKRIT_ST_BADARGS;
  uint8_t n = body[0];
  if (n == SKRIT_CONFIG_RESET) { duta_io_store_clear(); return SKRIT_ST_OK; }
  if (n > DUTA_MAX_OUTPUTS) return SKRIT_ST_BADARGS;
  // walk + validate each row: {type(1), pin(2), flags(1), arg(2), namelen(1), name}
  const uint8_t *p = body + 1;
  const uint8_t *end = body + len;
  for (uint8_t k = 0; k < n; k++) {
    if (p + 7 > end) { *bad_index = k; return SKRIT_ST_BADARGS; }
    uint8_t type = p[0];
    int16_t pin = (int16_t)(p[1] | (p[2] << 8));
    uint8_t namelen = p[6];
    if (p + 7 + namelen > end) { *bad_index = k; return SKRIT_ST_BADARGS; }
    if (!duta__row_ok(type, pin)) { *bad_index = k; return SKRIT_ST_BADARGS; }
    p += 7 + namelen;
  }
  return duta_io_store_save(body, len) ? SKRIT_ST_OK : SKRIT_ST_STORAGE;
}

// Load the persisted table into duta_io_ram and repoint the active table at it.
// No persisted config (or a malformed one) -> the compiled default stays active.
static void duta_io_load(void) {
  uint8_t buf[DUTA_MAX_OUTPUTS * 40 + 8];
  uint16_t k = duta_io_store_load(buf, sizeof buf);
  if (k < 1) return; // nothing stored -> keep the compiled default
  uint8_t n = buf[0];
  if (n == SKRIT_CONFIG_RESET || n > DUTA_MAX_OUTPUTS) return;
  const uint8_t *p = buf + 1;
  const uint8_t *end = buf + k;
  uint16_t pool = 0;
  uint8_t count = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (p + 7 > end) return; // malformed -> fall back to default
    uint8_t namelen = p[6];
    if (p + 7 + namelen > end) return;
    duta_io *row = &duta_io_ram[count];
    row->type = p[0];
    row->pin = (int16_t)(p[1] | (p[2] << 8));
    row->flags = p[3];
    row->arg = (uint16_t)(p[4] | (p[5] << 8));
    // copy the name into the RAM pool (NUL-terminated)
    if (pool + namelen + 1 > DUTA_NAME_POOL) return;
    char *nm = &duta_io_namepool[pool];
    memcpy(nm, p + 7, namelen);
    nm[namelen] = 0;
    row->name = nm;
    pool += namelen + 1;
    p += 7 + namelen;
    count++;
  }
  duta_tbl = duta_io_ram;
  duta_tbl_n = count;
}
#endif // DUTA_PROVISION

// Configure pins from the active table. Call from setup() before skrit_dev_init.
static inline void duta_io_begin(void) {
#ifdef DUTA_PROVISION
  duta_io_load(); // swap in a provisioned table if one is persisted
#endif
  for (uint8_t i = 0; i < duta_tbl_n; i++) {
    const duta_io *io = &duta_tbl[i];
    if (io->type == SKRIT_CTRL_RGB) continue; // driven by FastLED, not a GPIO
    pinMode(io->pin, OUTPUT);
    duta_io_out_set(NULL, i, 0); // PWM rows: first analogWrite attaches the channel
  }
  duta__pwm_apply(); // default PWM frequency + resolution (per-pin on esp32 core 3.x)
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
