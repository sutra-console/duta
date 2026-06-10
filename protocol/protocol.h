// skrit wire protocol — portable C reference (no deps). See PROTOCOL.md.
//
// On the wire each frame is:   0x00  COBS( TYPE SEQ LEN BODY...  CRC8 )  0x00
//   TYPE  request id; the response echoes TYPE | SKRIT_RESP
//   SEQ   request sequence, echoed in the response
//   LEN   length of BODY
//   CRC8  CRC-8/ATM (poly 0x07, init 0x00) over TYPE SEQ LEN BODY
//
// This header is shared by the C/C++ firmware platforms (espressif, zephyr,
// host, and — eventually — ch55xduino). MicroPython mirrors it in protocol.py.
#ifndef SKRIT_PROTOCOL_H
#define SKRIT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

// ---- message types (response = request | SKRIT_RESP) ----
enum {
  SKRIT_PING = 0x01,
  SKRIT_INFO = 0x02,
  SKRIT_DEVICE_NAME = 0x03, // self-describe: device name string
  SKRIT_OUT_SET = 0x10,
  SKRIT_OUT_GET = 0x11,
  SKRIT_OUT_TOGGLE = 0x12,
  SKRIT_OUT_DESC = 0x13,  // self-describe output: {index, type, name}
  SKRIT_INPUT_DESC = 0x14, // self-describe input: {index, type, name}
  SKRIT_INPUT_GET = 0x15,  // read input value (digital 0/1, analog 0-1023)
  SKRIT_MACRO_LIST = 0x20,
  SKRIT_MACRO_META = 0x21,
  SKRIT_MACRO_READ = 0x22,
  SKRIT_MACRO_WRITE_BEGIN = 0x23,
  SKRIT_MACRO_WRITE_DATA = 0x24,
  SKRIT_MACRO_WRITE_END = 0x25,
  SKRIT_MACRO_DELETE = 0x26,
  SKRIT_MACRO_RUN = 0x27,
};
#define SKRIT_RESP 0x80

// ---- status codes (response body[0]) ----
enum {
  SKRIT_ST_OK = 0x00,
  SKRIT_ST_BADCRC = 0x01,
  SKRIT_ST_BADMSG = 0x02,
  SKRIT_ST_BADARGS = 0x03,
  SKRIT_ST_STORAGE = 0x04,
  SKRIT_ST_NOTFOUND = 0x05,
  SKRIT_ST_BUSY = 0x06,
  SKRIT_ST_UNSUPPORTED = 0x07, // skrit-mc tier/opcode above macro_tier
};

// ---- capability bits (INFO body[3]) ----
enum {
  SKRIT_CAP_STORE = 0x01,
  SKRIT_CAP_OLED = 0x02,
  SKRIT_CAP_SPI = 0x04,
  SKRIT_CAP_PARITY = 0x08,
};

// ---- skrit-mc macro bytecode (see PROTOCOL.md "Macro bytecode") ----
// A program is: SKRIT_MC_VER(1), op..., SKRIT_MC_END. Multi-byte operands are LE.
#define SKRIT_MC_VER 0x01
#define SKRIT_MC_SCRATCH 0xFF // reserved volatile macro id for push-and-run
enum {                        // opcodes (low nibble groups by tier)
  SKRIT_MC_END = 0x00,        // halt (success)
  SKRIT_MC_EMIT = 0x01,       // n(1), bytes[n]            -> DATA/UART   [tier 1]
  SKRIT_MC_DELAY = 0x02,      // ms(2)                                    [tier 1]
  SKRIT_MC_SETOUT = 0x03,     // index(1), val(1)                         [tier 1]
  SKRIT_MC_EXPECT = 0x10,     // timeout(2), n(1), bytes[n] -> outcome    [tier 2]
  SKRIT_MC_WAITIO = 0x11,     // index(1), cmp(1), val(2), timeout(2)     [tier 2]
  SKRIT_MC_WAITOK = 0x12,     // halt FAIL if last outcome is FAIL        [tier 2]
  SKRIT_MC_IF = 0x20,         // cond(1), skip(2)   reserved v2
  SKRIT_MC_ELSE = 0x21,       // skip(2)            reserved v2
  SKRIT_MC_ENDIF = 0x22,      // reserved v2
};
// WAITIO comparison ops (cmp byte) — matches the host Cmp order.
enum {
  SKRIT_MC_GT = 0, SKRIT_MC_LT = 1, SKRIT_MC_GE = 2,
  SKRIT_MC_LE = 3, SKRIT_MC_EQ = 4, SKRIT_MC_NE = 5,
};
// highest tier a device VM runs (INFO macro_tier byte); 0 = no VM
enum { SKRIT_TIER_NONE = 0, SKRIT_TIER_REPLAY = 1, SKRIT_TIER_INTERACTIVE = 2, SKRIT_TIER_APP = 3 };

// ---- output control types (OUT_DESC body[2]) ----
enum { SKRIT_CTRL_RELAY = 0, SKRIT_CTRL_LED = 1, SKRIT_CTRL_BUTTON = 2 };

// ---- input types (INPUT_DESC body[2]) ----
enum { SKRIT_IN_DIGITAL = 0, SKRIT_IN_ANALOG = 1 };

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

#endif // SKRIT_PROTOCOL_H
