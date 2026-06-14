// duta_vl53l0x.h — ST VL53L0X Time-of-Flight ranging sensor, exposed as INVOKE
// commands. The first concrete "device function" on top of the I2C master bus:
// the host (or a macro) calls a high-level intent — init / read distance — and
// the device runs the multi-step I2C sequence itself (Duta-as-framework).
// ============================================================================
// The init/tuning sequence is the documented ST flow (as popularized by the
// Pololu VL53L0X library, MIT): 2.8V mode, the SPAD reference-management dance,
// the default tuning blob, interrupt config, and reference (VHV+phase) calibration.
// We keep the chip's factory measurement-timing budget (no re-encode) — readings
// are valid, just the default ~33ms budget. Single sensor at a time (one stop
// variable). Talks the bus through duta_i2c.h's duta_i2c_xfer(); the bus is
// brought up on demand (duta_i2c_begin is idempotent), so INVOKE works whether or
// not the DATA medium is currently I2C.
#ifndef DUTA_VL53L0X_H
#define DUTA_VL53L0X_H

#include <stdbool.h>
#include <stdint.h>

#include "duta_i2c.h" // duta_i2c_xfer / duta_i2c_begin + SKRIT_ST_*

#define VL53L0X_DEFAULT_ADDR 0x29
#define VL53L0X_MODEL_ID 0xEE
#define VL53L0X_TIMEOUT_MS 200

static uint8_t vl53l0x_stop_variable = 0; // captured during init, replayed per read

// ---- register helpers (the result/range registers are big-endian) ----------
static inline bool vl53__w8(uint8_t a, uint8_t reg, uint8_t v) {
  uint8_t b[2] = {reg, v};
  return duta_i2c_xfer(a, b, 2, 0, 0) == SKRIT_ST_OK;
}
static inline bool vl53__w16(uint8_t a, uint8_t reg, uint16_t v) {
  uint8_t b[3] = {reg, (uint8_t)(v >> 8), (uint8_t)v};
  return duta_i2c_xfer(a, b, 3, 0, 0) == SKRIT_ST_OK;
}
static inline uint8_t vl53__r8(uint8_t a, uint8_t reg) {
  uint8_t v = 0;
  duta_i2c_xfer(a, &reg, 1, &v, 1);
  return v;
}
static inline uint16_t vl53__r16(uint8_t a, uint8_t reg) {
  uint8_t b[2] = {0, 0};
  duta_i2c_xfer(a, &reg, 1, b, 2);
  return (uint16_t)((b[0] << 8) | b[1]);
}
static inline bool vl53__wmulti(uint8_t a, uint8_t reg, const uint8_t *src, uint8_t n) {
  uint8_t b[8];
  if (n > 7) return false;
  b[0] = reg;
  for (uint8_t i = 0; i < n; i++) b[1 + i] = src[i];
  return duta_i2c_xfer(a, b, (uint8_t)(n + 1), 0, 0) == SKRIT_ST_OK;
}
static inline bool vl53__rmulti(uint8_t a, uint8_t reg, uint8_t *dst, uint8_t n) {
  return duta_i2c_xfer(a, &reg, 1, dst, n) == SKRIT_ST_OK;
}

// ST default tuning settings (reg,val pairs) — loaded verbatim after SPAD setup.
static const uint8_t vl53__tuning[] = {
    0xFF, 0x01, 0x00, 0x00, 0xFF, 0x00, 0x09, 0x00, 0x10, 0x00, 0x11, 0x00,
    0x24, 0x01, 0x25, 0xFF, 0x75, 0x00, 0xFF, 0x01, 0x4E, 0x2C, 0x48, 0x00,
    0x30, 0x20, 0xFF, 0x00, 0x30, 0x09, 0x54, 0x00, 0x31, 0x04, 0x32, 0x03,
    0x40, 0x83, 0x46, 0x25, 0x60, 0x00, 0x27, 0x00, 0x50, 0x06, 0x51, 0x00,
    0x52, 0x96, 0x56, 0x08, 0x57, 0x30, 0x61, 0x00, 0x62, 0x00, 0x64, 0x00,
    0x65, 0x00, 0x66, 0xA0, 0xFF, 0x01, 0x22, 0x32, 0x47, 0x14, 0x49, 0xFF,
    0x4A, 0x00, 0xFF, 0x00, 0x7A, 0x0A, 0x7B, 0x00, 0x78, 0x21, 0xFF, 0x01,
    0x23, 0x34, 0x42, 0x00, 0x44, 0xFF, 0x45, 0x26, 0x46, 0x05, 0x40, 0x40,
    0x0E, 0x06, 0x20, 0x1A, 0x43, 0x40, 0xFF, 0x00, 0x34, 0x03, 0x35, 0x44,
    0xFF, 0x01, 0x31, 0x04, 0x4B, 0x09, 0x4C, 0x05, 0x4D, 0x04, 0xFF, 0x00,
    0x44, 0x00, 0x45, 0x20, 0x47, 0x08, 0x48, 0x28, 0x67, 0x00, 0x70, 0x04,
    0x71, 0x01, 0x72, 0xFE, 0x76, 0x00, 0x77, 0x00, 0xFF, 0x01, 0x0D, 0x01,
    0xFF, 0x00, 0x80, 0x01, 0x01, 0xF8, 0xFF, 0x01, 0x8E, 0x01, 0x00, 0x01,
    0xFF, 0x00, 0x80, 0x00,
};

// IDENTIFICATION_MODEL_ID (0xC0) — 0xEE on a healthy part. Cheap presence probe.
static uint8_t vl53l0x_model_id(uint8_t addr) {
  duta_i2c_begin();
  return vl53__r8(addr, 0xC0);
}

// Read the SPAD count + aperture flag from NVM via the documented magic sequence.
static bool vl53__spad_info(uint8_t a, uint8_t *count, bool *is_aperture) {
  vl53__w8(a, 0x80, 0x01);
  vl53__w8(a, 0xFF, 0x01);
  vl53__w8(a, 0x00, 0x00);
  vl53__w8(a, 0xFF, 0x06);
  vl53__w8(a, 0x83, (uint8_t)(vl53__r8(a, 0x83) | 0x04));
  vl53__w8(a, 0xFF, 0x07);
  vl53__w8(a, 0x81, 0x01);
  vl53__w8(a, 0x80, 0x01);
  vl53__w8(a, 0x94, 0x6B);
  vl53__w8(a, 0x83, 0x00);
  uint32_t t = millis();
  while (vl53__r8(a, 0x83) == 0x00)
    if (millis() - t > VL53L0X_TIMEOUT_MS) return false;
  vl53__w8(a, 0x83, 0x01);
  uint8_t tmp = vl53__r8(a, 0x92);
  *count = (uint8_t)(tmp & 0x7F);
  *is_aperture = (tmp >> 7) & 1;
  vl53__w8(a, 0x81, 0x00);
  vl53__w8(a, 0xFF, 0x06);
  vl53__w8(a, 0x83, (uint8_t)(vl53__r8(a, 0x83) & ~0x04));
  vl53__w8(a, 0xFF, 0x01);
  vl53__w8(a, 0x00, 0x01);
  vl53__w8(a, 0xFF, 0x00);
  vl53__w8(a, 0x80, 0x00);
  return true;
}

// One reference-calibration pass (vhv = 0x40 for VHV, 0x00 for phase).
static bool vl53__ref_cal(uint8_t a, uint8_t vhv) {
  vl53__w8(a, 0x00, (uint8_t)(0x01 | vhv)); // SYSRANGE_START | vhv_init
  uint32_t t = millis();
  while ((vl53__r8(a, 0x13) & 0x07) == 0) // RESULT_INTERRUPT_STATUS
    if (millis() - t > VL53L0X_TIMEOUT_MS) return false;
  vl53__w8(a, 0x0B, 0x01); // SYSTEM_INTERRUPT_CLEAR
  vl53__w8(a, 0x00, 0x00); // SYSRANGE_START stop
  return true;
}

// Full init: returns true once the sensor is ready for single-shot ranging.
static bool vl53l0x_init(uint8_t addr) {
  duta_i2c_begin();
  if (vl53__r8(addr, 0xC0) != VL53L0X_MODEL_ID) return false; // not a VL53L0X

  vl53__w8(addr, 0x89, (uint8_t)(vl53__r8(addr, 0x89) | 0x01)); // 2.8V I/O
  vl53__w8(addr, 0x88, 0x00);                                   // standard I2C

  vl53__w8(addr, 0x80, 0x01);
  vl53__w8(addr, 0xFF, 0x01);
  vl53__w8(addr, 0x00, 0x00);
  vl53l0x_stop_variable = vl53__r8(addr, 0x91);
  vl53__w8(addr, 0x00, 0x01);
  vl53__w8(addr, 0xFF, 0x00);
  vl53__w8(addr, 0x80, 0x00);

  // disable SIGNAL_RATE_MSRC + SIGNAL_RATE_PRE_RANGE limit checks
  vl53__w8(addr, 0x60, (uint8_t)(vl53__r8(addr, 0x60) | 0x12));
  vl53__w16(addr, 0x44, 32); // signal-rate limit 0.25 MCPS (0.25 * (1<<7))
  vl53__w8(addr, 0x01, 0xFF); // SYSTEM_SEQUENCE_CONFIG

  uint8_t spad_count = 0;
  bool aperture = false;
  if (!vl53__spad_info(addr, &spad_count, &aperture)) return false;

  uint8_t spad_map[6];
  if (!vl53__rmulti(addr, 0xB0, spad_map, 6)) return false; // GLOBAL_CONFIG_SPAD_ENABLES_REF_0
  vl53__w8(addr, 0xFF, 0x01);
  vl53__w8(addr, 0x4F, 0x00); // DYNAMIC_SPAD_REF_EN_START_OFFSET
  vl53__w8(addr, 0x4E, 0x2C); // DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD
  vl53__w8(addr, 0xFF, 0x00);
  vl53__w8(addr, 0xB6, 0xB4); // GLOBAL_CONFIG_REF_EN_START_SELECT
  uint8_t first = aperture ? 12 : 0;
  uint8_t enabled = 0;
  for (uint8_t i = 0; i < 48; i++) {
    if (i < first || enabled == spad_count) spad_map[i / 8] &= (uint8_t)~(1 << (i % 8));
    else if ((spad_map[i / 8] >> (i % 8)) & 1) enabled++;
  }
  vl53__wmulti(addr, 0xB0, spad_map, 6);

  for (size_t i = 0; i < sizeof vl53__tuning / 2; i++)
    vl53__w8(addr, vl53__tuning[2 * i], vl53__tuning[2 * i + 1]);

  vl53__w8(addr, 0x0A, 0x04);                                  // SYSTEM_INTERRUPT_CONFIG_GPIO: new sample ready
  vl53__w8(addr, 0x84, (uint8_t)(vl53__r8(addr, 0x84) & ~0x10)); // GPIO active low
  vl53__w8(addr, 0x0B, 0x01);                                  // clear interrupt
  vl53__w8(addr, 0x01, 0xE8);                                  // restore SYSTEM_SEQUENCE_CONFIG

  vl53__w8(addr, 0x01, 0x01);
  if (!vl53__ref_cal(addr, 0x40)) return false; // VHV calibration
  vl53__w8(addr, 0x01, 0x02);
  if (!vl53__ref_cal(addr, 0x00)) return false; // phase calibration
  vl53__w8(addr, 0x01, 0xE8);                   // restore full sequence
  return true;
}

// Begin back-to-back continuous ranging (call after vl53l0x_init). Each new
// sample raises the data-ready interrupt; poll with vl53l0x_read_continuous_mm.
static bool vl53l0x_start_continuous(uint8_t a) {
  vl53__w8(a, 0x80, 0x01);
  vl53__w8(a, 0xFF, 0x01);
  vl53__w8(a, 0x00, 0x00);
  vl53__w8(a, 0x91, vl53l0x_stop_variable);
  vl53__w8(a, 0x00, 0x01);
  vl53__w8(a, 0xFF, 0x00);
  vl53__w8(a, 0x80, 0x00);
  return vl53__w8(a, 0x00, 0x02); // SYSRANGE_START = continuous back-to-back
}

// Non-blocking: returns the latest sample with *ok=true if one is ready, else
// *ok=false (no new measurement yet — caller should just try again later).
static uint16_t vl53l0x_read_continuous_mm(uint8_t a, bool *ok) {
  *ok = false;
  if ((vl53__r8(a, 0x13) & 0x07) == 0) return 0; // RESULT_INTERRUPT_STATUS: nothing new
  uint16_t range = vl53__r16(a, 0x1E);
  vl53__w8(a, 0x0B, 0x01); // clear interrupt
  *ok = true;
  return range;
}

// Stop continuous ranging.
static void vl53l0x_stop_continuous(uint8_t a) {
  vl53__w8(a, 0x00, 0x01); // SYSRANGE_START stop
  vl53__w8(a, 0xFF, 0x01);
  vl53__w8(a, 0x00, 0x00);
  vl53__w8(a, 0x91, 0x00);
  vl53__w8(a, 0x00, 0x01);
  vl53__w8(a, 0xFF, 0x00);
}

// Single-shot range in millimetres. *ok = false on timeout/bus error.
static uint16_t vl53l0x_read_mm(uint8_t addr, bool *ok) {
  *ok = false;
  duta_i2c_begin();

  vl53__w8(addr, 0x80, 0x01);
  vl53__w8(addr, 0xFF, 0x01);
  vl53__w8(addr, 0x00, 0x00);
  vl53__w8(addr, 0x91, vl53l0x_stop_variable);
  vl53__w8(addr, 0x00, 0x01);
  vl53__w8(addr, 0xFF, 0x00);
  vl53__w8(addr, 0x80, 0x00);

  vl53__w8(addr, 0x00, 0x01); // SYSRANGE_START
  uint32_t t = millis();
  while (vl53__r8(addr, 0x00) & 0x01)
    if (millis() - t > VL53L0X_TIMEOUT_MS) return 0;
  t = millis();
  while ((vl53__r8(addr, 0x13) & 0x07) == 0)
    if (millis() - t > VL53L0X_TIMEOUT_MS) return 0;

  uint16_t range = vl53__r16(addr, 0x1E); // RESULT_RANGE_STATUS + 10 (big-endian mm)
  vl53__w8(addr, 0x0B, 0x01);             // clear interrupt
  *ok = true;
  return range;
}

#endif // DUTA_VL53L0X_H
