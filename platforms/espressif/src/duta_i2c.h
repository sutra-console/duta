// duta_i2c.h — I2C master DATA backend (espressif platform, Wire/TwoWire).
// ============================================================================
// The first non-UART bridge medium. While the DATA kind is i2c, the host
// drives the bus over CMD (I2C_SCAN / I2C_XFER) and every transfer is emitted
// on the DATA channel as one record per mux frame (see PROTOCOL.md "I²C"):
//   ts_ms(4 LE) · addr(1) · flags(1) · wlen(1) · w · rlen(1) · r
// main.cpp owns the kind switch (CFG_DATA_KIND, NVS-persisted) and the record
// fan-out to the links; this module is just the bus + record builder.
#ifndef DUTA_I2C_H
#define DUTA_I2C_H

#include <Wire.h>

#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 8 // override per target; defaults fit the S3 side headers
#endif
#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 9
#endif
#ifndef I2C_FREQ_HZ
#define I2C_FREQ_HZ 100000
#endif

static bool duta_i2c_active = false;

static void duta_i2c_begin(void) {
  if (duta_i2c_active) return;
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, (uint32_t)I2C_FREQ_HZ);
  duta_i2c_active = true;
}

static void duta_i2c_end(void) {
  if (!duta_i2c_active) return;
  Wire.end();
  duta_i2c_active = false;
}

// Probe every assignable 7-bit address (0x08..0x77) with a zero-length write.
static uint8_t duta_i2c_scan(uint8_t bitmap[16]) {
  if (!duta_i2c_active) return SKRIT_ST_UNSUPPORTED;
  memset(bitmap, 0, 16);
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) bitmap[a >> 3] |= (uint8_t)(1 << (a & 7));
  }
  return SKRIT_ST_OK;
}

// Master transfer: write `wlen` bytes (repeated-start when a read follows),
// then read `rlen`. Returns SKRIT_ST_*; NAK/short read -> NOTFOUND.
static uint8_t duta_i2c_xfer(uint8_t addr, const uint8_t *w, uint8_t wlen, uint8_t *r,
                             uint8_t rlen) {
  if (!duta_i2c_active) return SKRIT_ST_UNSUPPORTED;
  if (wlen) {
    Wire.beginTransmission(addr);
    Wire.write(w, wlen);
    if (Wire.endTransmission(rlen == 0) != 0) return SKRIT_ST_NOTFOUND; // NAK
  }
  if (rlen) {
    if (Wire.requestFrom((int)addr, (int)rlen) != (int)rlen) return SKRIT_ST_NOTFOUND;
    for (uint8_t i = 0; i < rlen; i++) r[i] = (uint8_t)Wire.read();
  }
  return SKRIT_ST_OK;
}

// Build one DATA record for a transfer (caller fans it out to the links).
// `cap` must be >= 8 + wlen + rlen. Returns the record length.
static uint16_t duta_i2c_record(uint8_t *out, uint16_t cap, uint8_t addr, uint8_t flags,
                                const uint8_t *w, uint8_t wlen, const uint8_t *r,
                                uint8_t rlen) {
  if ((uint16_t)8 + wlen + rlen > cap) return 0;
  uint32_t ts = millis();
  uint16_t n = 0;
  out[n++] = (uint8_t)ts;
  out[n++] = (uint8_t)(ts >> 8);
  out[n++] = (uint8_t)(ts >> 16);
  out[n++] = (uint8_t)(ts >> 24);
  out[n++] = addr;
  out[n++] = flags; // bit0 = had read phase, bit1 = NAK/failed
  out[n++] = wlen;
  if (wlen) { memcpy(out + n, w, wlen); n = (uint16_t)(n + wlen); }
  out[n++] = rlen;
  if (rlen && r) { memcpy(out + n, r, rlen); n = (uint16_t)(n + rlen); }
  return n;
}

#endif // DUTA_I2C_H
