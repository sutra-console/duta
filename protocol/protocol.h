// Duta wire protocol — portable C reference (no deps). See PROTOCOL.md.
//
// On the wire each frame is:   0x00  COBS( TYPE SEQ LEN BODY...  CRC8 )  0x00
//   TYPE  request id; the response echoes TYPE | TTLB_RESP
//   SEQ   request sequence, echoed in the response
//   LEN   length of BODY
//   CRC8  CRC-8/ATM (poly 0x07, init 0x00) over TYPE SEQ LEN BODY
//
// This header is shared by the C/C++ firmware platforms (espressif, zephyr,
// host, and — eventually — ch55xduino). MicroPython mirrors it in protocol.py.
#ifndef DUTA_PROTOCOL_H
#define DUTA_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

// ---- message types (response = request | TTLB_RESP) ----
enum {
  TTLB_PING = 0x01,
  TTLB_INFO = 0x02,
  TTLB_DEVICE_NAME = 0x03, // self-describe: device name string
  TTLB_OUT_SET = 0x10,
  TTLB_OUT_GET = 0x11,
  TTLB_OUT_TOGGLE = 0x12,
  TTLB_OUT_DESC = 0x13, // self-describe: {index, type, name}
  TTLB_SNIP_LIST = 0x20,
  TTLB_SNIP_META = 0x21,
  TTLB_SNIP_READ = 0x22,
  TTLB_SNIP_WRITE_BEGIN = 0x23,
  TTLB_SNIP_WRITE_DATA = 0x24,
  TTLB_SNIP_WRITE_END = 0x25,
  TTLB_SNIP_DELETE = 0x26,
  TTLB_SNIP_RUN = 0x27,
};
#define TTLB_RESP 0x80

// ---- status codes (response body[0]) ----
enum {
  TTLB_ST_OK = 0x00,
  TTLB_ST_BADCRC = 0x01,
  TTLB_ST_BADMSG = 0x02,
  TTLB_ST_BADARGS = 0x03,
  TTLB_ST_STORAGE = 0x04,
  TTLB_ST_NOTFOUND = 0x05,
};

// ---- capability bits (INFO body[3]) ----
enum {
  TTLB_CAP_EEPROM = 0x01,
  TTLB_CAP_OLED = 0x02,
  TTLB_CAP_SPI = 0x04,
  TTLB_CAP_PARITY = 0x08,
};

// ---- control types (OUT_DESC body[2]) ----
enum { TTLB_CTRL_RELAY = 0, TTLB_CTRL_LED = 1, TTLB_CTRL_BUTTON = 2 };

// ---- CRC-8/ATM (poly 0x07, init 0x00) ----
static inline uint8_t ttlb_crc8(const uint8_t *p, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++) {
    c ^= p[i];
    for (uint8_t b = 0; b < 8; b++)
      c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x07) : (uint8_t)(c << 1);
  }
  return c;
}

// ---- COBS (canonical) ----
// Encode `len` bytes from `in` into `out` (no delimiters). `out` must hold at
// least len + len/254 + 2 bytes. Returns the encoded length.
static inline size_t ttlb_cobs_encode(const uint8_t *in, size_t len, uint8_t *out) {
  uint8_t *enc = out;
  uint8_t *code_ptr = enc++;
  uint8_t code = 1;
  for (size_t i = 0; i < len; i++) {
    if (in[i]) {
      *enc++ = in[i];
      if (++code == 0xFF) {
        *code_ptr = code;
        code_ptr = enc++;
        code = 1;
      }
    } else {
      *code_ptr = code;
      code_ptr = enc++;
      code = 1;
    }
  }
  *code_ptr = code;
  return (size_t)(enc - out);
}

// Decode `len` COBS bytes from `in` (delimiters already stripped) into `out`.
// Returns the decoded length.
static inline size_t ttlb_cobs_decode(const uint8_t *in, size_t len, uint8_t *out) {
  const uint8_t *byte = in;
  uint8_t *dec = out;
  for (uint8_t code = 0xFF, block = 0; byte < in + len; --block) {
    if (block) {
      *dec++ = *byte++;
    } else {
      block = *byte++;
      if (block && (code != 0xFF))
        *dec++ = 0;
      code = block;
      if (!code)
        break;
    }
  }
  return (size_t)(dec - out);
}

#endif // DUTA_PROTOCOL_H
