// Duta on ESP32 — skeleton. Speaks the CMD protocol over Serial
// (USB-CDC on S3/C3, UART0 otherwise). 3 demo controls.
//
// TODO: DATA-console passthrough, and the TCP (WiFi bridge) + BLE (NUS)
// transports — both multiplex DATA + CMD over one channel (see protocol/).
#include <Arduino.h>
extern "C" {
#include "protocol.h"
}

static const char *DEV_NAME = "Duta esp32";
static const uint8_t N_OUT = 3;
static const char *OUT_NAME[N_OUT] = {"Relay 1", "Relay 2", "Aux LED"};
static const uint8_t OUT_TYPE[N_OUT] = {TTLB_CTRL_RELAY, TTLB_CTRL_RELAY, TTLB_CTRL_LED};
static uint8_t outState = 0;

static void sendFrame(uint8_t typ, uint8_t seq, const uint8_t *body, uint8_t len) {
  uint8_t raw[3 + 255 + 1];
  raw[0] = typ;
  raw[1] = seq;
  raw[2] = len;
  memcpy(raw + 3, body, len);
  raw[3 + len] = ttlb_crc8(raw, 3 + len);
  uint8_t cobs[sizeof(raw) + 8];
  size_t n = ttlb_cobs_encode(raw, 3 + len + 1, cobs);
  Serial.write((uint8_t)0x00);
  Serial.write(cobs, n);
  Serial.write((uint8_t)0x00);
}

static void handle(const uint8_t *p, size_t n) {
  if (n < 4) return;
  uint8_t typ = p[0], seq = p[1], len = p[2];
  if ((size_t)(3 + len + 1) > n || ttlb_crc8(p, 3 + len) != p[3 + len]) return;
  const uint8_t *b = p + 3;
  uint8_t r[64], rl = 0;
  switch (typ) {
  case TTLB_PING:
    r[rl++] = TTLB_ST_OK;
    break;
  case TTLB_INFO:
    r[rl++] = TTLB_ST_OK;
    r[rl++] = 1; r[rl++] = 0; r[rl++] = 0; // fw 0.1, caps 0
    r[rl++] = N_OUT; r[rl++] = 0; r[rl++] = 1; // nout, eekb, proto
    break;
  case TTLB_DEVICE_NAME:
    r[rl++] = TTLB_ST_OK;
    for (const char *s = DEV_NAME; *s; s++) r[rl++] = (uint8_t)*s;
    break;
  case TTLB_OUT_GET:
    r[rl++] = TTLB_ST_OK; r[rl++] = outState;
    break;
  case TTLB_OUT_SET:
    if (len >= 2 && b[0] < N_OUT) {
      if (b[1]) outState |= (1u << b[0]); else outState &= ~(1u << b[0]);
      r[rl++] = TTLB_ST_OK; r[rl++] = outState;
    } else r[rl++] = TTLB_ST_BADARGS;
    break;
  case TTLB_OUT_TOGGLE:
    if (len >= 1 && b[0] < N_OUT) {
      outState ^= (1u << b[0]);
      r[rl++] = TTLB_ST_OK; r[rl++] = outState;
    } else r[rl++] = TTLB_ST_BADARGS;
    break;
  case TTLB_OUT_DESC:
    if (len >= 1 && b[0] < N_OUT) {
      r[rl++] = TTLB_ST_OK; r[rl++] = b[0]; r[rl++] = OUT_TYPE[b[0]];
      for (const char *s = OUT_NAME[b[0]]; *s; s++) r[rl++] = (uint8_t)*s;
    } else r[rl++] = TTLB_ST_BADARGS;
    break;
  default:
    r[rl++] = TTLB_ST_BADMSG;
    break;
  }
  sendFrame(typ | TTLB_RESP, seq, r, rl);
}

static uint8_t acc[600];
static size_t accLen = 0;

void setup() { Serial.begin(115200); }

void loop() {
  while (Serial.available()) {
    uint8_t b = (uint8_t)Serial.read();
    if (b == 0x00) {
      if (accLen) {
        uint8_t dec[600];
        size_t dl = ttlb_cobs_decode(acc, accLen, dec);
        handle(dec, dl);
        accLen = 0;
      }
    } else if (accLen < sizeof acc) {
      acc[accLen++] = b;
    }
  }
}
