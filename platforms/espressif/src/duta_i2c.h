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

#include <string.h>

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

#ifdef DUTA_PURE_IDF
// ---- pure-IDF backend: the new i2c_master driver --------------------------
#include "driver/i2c_master.h"
#include "esp_timer.h"
static i2c_master_bus_handle_t duta_i2c_bus;

static void duta_i2c_begin(void) {
  if (duta_i2c_active) return;
  i2c_master_bus_config_t bc = {
      .i2c_port = I2C_NUM_0,
      .sda_io_num = I2C_SDA_PIN,
      .scl_io_num = I2C_SCL_PIN,
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .glitch_ignore_cnt = 7,
      .flags = {.enable_internal_pullup = true},
  };
  if (i2c_new_master_bus(&bc, &duta_i2c_bus) == ESP_OK) duta_i2c_active = true;
}
static void duta_i2c_end(void) {
  if (!duta_i2c_active) return;
  i2c_del_master_bus(duta_i2c_bus);
  duta_i2c_active = false;
}
static uint8_t duta_i2c_scan(uint8_t bitmap[16]) {
  if (!duta_i2c_active) return SKRIT_ST_UNSUPPORTED;
  memset(bitmap, 0, 16);
  for (uint8_t a = 0x08; a <= 0x77; a++)
    if (i2c_master_probe(duta_i2c_bus, a, 50) == ESP_OK) bitmap[a >> 3] |= (uint8_t)(1 << (a & 7));
  return SKRIT_ST_OK;
}
static uint8_t duta_i2c_xfer(uint8_t addr, const uint8_t *w, uint8_t wlen, uint8_t *r,
                             uint8_t rlen) {
  if (!duta_i2c_active) return SKRIT_ST_UNSUPPORTED;
  i2c_device_config_t dc = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = addr,
      .scl_speed_hz = I2C_FREQ_HZ,
  };
  i2c_master_dev_handle_t dev;
  if (i2c_master_bus_add_device(duta_i2c_bus, &dc, &dev) != ESP_OK) return SKRIT_ST_NOTFOUND;
  esp_err_t e;
  if (wlen && rlen) e = i2c_master_transmit_receive(dev, w, wlen, r, rlen, 100);
  else if (wlen) e = i2c_master_transmit(dev, w, wlen, 100);
  else if (rlen) e = i2c_master_receive(dev, r, rlen, 100);
  else e = ESP_OK;
  i2c_master_bus_rm_device(dev);
  return e == ESP_OK ? SKRIT_ST_OK : SKRIT_ST_NOTFOUND;
}
#define DUTA__NOW_MS() ((uint32_t)(esp_timer_get_time() / 1000))

#else
// ---- Arduino backend: Wire/TwoWire ----------------------------------------
#include <Wire.h>

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
#define DUTA__NOW_MS() ((uint32_t)millis())
#endif

// Build one DATA record for a transfer (caller fans it out to the links).
// `cap` must be >= 8 + wlen + rlen. Returns the record length.
static uint16_t duta_i2c_record(uint8_t *out, uint16_t cap, uint8_t addr, uint8_t flags,
                                const uint8_t *w, uint8_t wlen, const uint8_t *r,
                                uint8_t rlen) {
  if ((uint16_t)8 + wlen + rlen > cap) return 0;
  uint32_t ts = DUTA__NOW_MS();
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
