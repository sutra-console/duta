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

// Wire protocol version, reported as INFO body[6]. Additive changes (new TYPEs,
// new caps bits, the skrit-mux channel tag) keep this; breaking changes bump it.
#define SKRIT_PROTO_VER 1

// ---- message types (response = request | SKRIT_RESP) ----
enum {
  SKRIT_PING = 0x01,
  SKRIT_INFO = 0x02,
  SKRIT_DEVICE_NAME = 0x03, // self-describe: device name string
  SKRIT_REBOOT = 0x04,      // mode(1): 0=app reset, 1=bootloader/DFU. OK then reboots.
  SKRIT_AUTH = 0x05,        // password(...): authenticate the session (network transports)
  SKRIT_AUTH_SET = 0x06,    // new_password(...): change the password (must be authed)
  SKRIT_DATA_DESC = 0x07,   // -> kind(1), name string: what the DATA channel carries
  SKRIT_OUT_SET = 0x10,
  SKRIT_OUT_GET = 0x11,
  SKRIT_OUT_TOGGLE = 0x12,
  SKRIT_OUT_DESC = 0x13,  // self-describe output: {index, type, name}
  SKRIT_INPUT_DESC = 0x14, // self-describe input: {index, type, name}
  SKRIT_INPUT_GET = 0x15,  // read input value (digital 0/1, analog 0-1023)
  SKRIT_OUT_PULSE = 0x16,  // index(1), ms(2): drive output on, restore after ms (momentary)
  SKRIT_SERIAL_GET = 0x17, // -> baud(4), data_bits(1), parity(1), stop_bits(1)  (DATA UART)
  SKRIT_SERIAL_SET = 0x18, // baud(4), data_bits(1), parity(1), stop_bits(1)
  SKRIT_SERIAL_SIGNAL = 0x19, // mask(1), value(1): drive DATA modem/break lines (see SKRIT_SIG_*)
  SKRIT_OUT_PWM = 0x1A, // index(1)[, duty(2)]: with duty = set PWM 0..1023; without = read back duty(2)
  SKRIT_OUT_RGB = 0x1B, // index(1)[, [pixel(1),] r(1), g(1), b(1)] -> index, count(1), r, g, b
                        //   len 1 = read; len 4 = set all pixels; len 5 = set one pixel
  SKRIT_PWM_CONFIG = 0x1C, // index(1)[, freq(4), res(1)] -> index, freq(4), res(1)
                           //   len 1 = read config; len 6 = set freq/resolution (0 = leave)
  // ---- runtime provisioning (see "Provisioning" in PROTOCOL.md) ----
  SKRIT_PIN_CAPS = 0x1D,   // index(1) -> index, total(1)[, pin(2), caps(1), warn(1), bus(1), name]
                           //   the provisioning MENU: the offerable pins (mcu ∩ board)
  SKRIT_CONFIG_GET = 0x1E, // index(1) -> index, n(1)[, type(1), pin(2), flags(1), arg(2), name]
                           //   the CURRENT IO table, one row per index (0..n-1)
  SKRIT_CONFIG_SET = 0x1F, // n(1), rows[n]{type(1), pin(2), flags(1), arg(2), namelen(1), name}
                           //   replaces the IO table; n=SKRIT_CONFIG_RESET reverts to the compiled
                           //   default. -> status[, bad_index(1)]. Takes effect after REBOOT.
  SKRIT_MACRO_LIST = 0x20,
  SKRIT_MACRO_META = 0x21,
  SKRIT_MACRO_READ = 0x22,
  SKRIT_MACRO_WRITE_BEGIN = 0x23,
  SKRIT_MACRO_WRITE_DATA = 0x24,
  SKRIT_MACRO_WRITE_END = 0x25,
  SKRIT_MACRO_DELETE = 0x26,
  SKRIT_MACRO_RUN = 0x27,
  SKRIT_EE_READ = 0x30,
  SKRIT_EE_WRITE = 0x31,
  SKRIT_CFG_GET = 0x40,
  SKRIT_CFG_SET = 0x41,
  // Async device->host events (0x50..0x5F): RESP bit clear, SEQ=0, NOT request/reply.
  // A host routes TYPE in this range to an event sink, never the seq-matcher.
  SKRIT_EVENT_LOG = 0x50,   // text(...): device log line (e.g. macro progress)
  SKRIT_EVENT_INPUT = 0x51, // index(1), value(2): an input changed (edge / threshold)
};
#define SKRIT_RESP 0x80
#define SKRIT_EVENT_LO 0x50 // inclusive range of async event TYPEs
#define SKRIT_EVENT_HI 0x5F

// ---- DATA-UART parity (SERIAL_GET/SET parity byte) ----
enum { SKRIT_PAR_NONE = 0, SKRIT_PAR_ODD = 1, SKRIT_PAR_EVEN = 2 };

// ---- DATA modem/break line bits (SERIAL_SIGNAL mask/value) ----
// Mask selects which lines to act on; value gives the level (1=asserted). The
// BREAK bit in `value` asserts a line break for the device's default break
// window. Driving DTR/RTS lets a host enter ESP32 / AVR bootloaders by hand.
enum { SKRIT_SIG_DTR = 0x01, SKRIT_SIG_RTS = 0x02, SKRIT_SIG_BREAK = 0x04 };

// ---- REBOOT modes ----
enum { SKRIT_REBOOT_APP = 0, SKRIT_REBOOT_BOOTLOADER = 1 };

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
  SKRIT_ST_UNAUTH = 0x08,      // session not authenticated (send AUTH first)
};

// ---- INFO flags byte (trailing, after macro_tier) ----
// Additive: an older host that stops at macro_tier reads 0 (no auth). Network
// transports (WebSocket) set AUTH_REQUIRED; until AUTH succeeds the device
// answers only PING/INFO/AUTH and does not bridge the DATA console.
enum {
  SKRIT_FLAG_AUTH_REQUIRED = 0x01, // AUTH needed before other commands / DATA
  SKRIT_FLAG_DEFAULT_CRED = 0x02,  // still the factory password — prompt a change
  SKRIT_FLAG_PROVISION = 0x04,     // accepts runtime IO provisioning (PIN_CAPS/CONFIG_*)
};
// Factory default password (network devices ship with this; change on first use).
#define SKRIT_DEFAULT_PASSWORD "duta"
#define SKRIT_PASSWORD_MAX 32

// ---- capability bits (INFO body[3]) ----
enum {
  SKRIT_CAP_STORE = 0x01,  // persists macros (MACRO_WRITE_* survive reboot)
  SKRIT_CAP_OLED = 0x02,   // has an OLED mirror
  SKRIT_CAP_SPI = 0x04,    // has SPI flash
  SKRIT_CAP_PARITY = 0x08, // DATA UART can do parity
  SKRIT_CAP_MUX = 0x10,    // a single endpoint carries BOTH channels (see skrit-mux)
  SKRIT_CAP_SERIAL = 0x20, // honors SERIAL_GET/SET/SIGNAL on the DATA UART
  SKRIT_CAP_REBOOT = 0x40, // honors REBOOT (incl. reboot-to-bootloader)
  SKRIT_CAP_PWM = 0x80,    // honors OUT_PWM on at least one output
};

// ===========================================================================
// skrit-mux — one byte stream carrying BOTH the DATA console and the CMD
// protocol, for transports with a single channel (single USB-CDC, TCP, BLE).
// Dual-CDC devices (e.g. CH552) do NOT mux: DATA is its own raw port. A muxed
// device sets SKRIT_CAP_MUX in INFO.
//
//   wire:  0x00 , COBS( CHANNEL , payload... ) , 0x00
//
//   CHANNEL = SKRIT_MUX_DATA -> payload is raw target-console bytes
//   CHANNEL = SKRIT_MUX_CMD  -> payload is a CMD frame: TYPE SEQ LEN BODY CRC8
//
// COBS already removes 0x00 from the body, so the delimiters stay unambiguous
// and the link resyncs after a glitch. The CMD payload is byte-identical to the
// dual-CDC CMD frame — only a 1-byte channel tag is prepended before COBS.
// ===========================================================================
enum { SKRIT_MUX_DATA = 0x00, SKRIT_MUX_CMD = 0x01 };

// ---- skrit-mc macro bytecode (see PROTOCOL.md "Macro bytecode") ----
// A program is: SKRIT_MC_VER(1), op..., SKRIT_MC_END. Multi-byte operands are LE.
#define SKRIT_MC_VER 0x01
#define SKRIT_MC_SCRATCH 0xFF // reserved volatile macro id for push-and-run
enum {                        // opcodes (low nibble groups by tier)
  SKRIT_MC_END = 0x00,        // halt (success)
  SKRIT_MC_EMIT = 0x01,       // n(1), bytes[n]            -> DATA/UART   [tier 1]
  SKRIT_MC_DELAY = 0x02,      // ms(2)                                    [tier 1]
  SKRIT_MC_SETOUT = 0x03,     // index(1), val(1)                         [tier 1]
  SKRIT_MC_SETPWM = 0x04,     // index(1), duty(2) 0..1023                [tier 1]
  SKRIT_MC_SETRGB = 0x05,     // index(1), r(1), g(1), b(1) -> fill all   [tier 1]
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
// Typed by BEHAVIOR, not fixture: what a pin IS (relay, LED, fan, reset button)
// goes in the descriptive name; the type says how it's driven. PWM and RGB
// outputs also answer OUT_SET/TOGGLE (0 = off, 1 = full/white). RGB is an
// addressable-LED color via OUT_RGB; no caps bit — the type advertises it.
enum {
  SKRIT_CTRL_IO = 0,  // digital on/off line
  SKRIT_CTRL_PWM = 1, // duty-cycle output (OUT_PWM, 0..1023)
  SKRIT_CTRL_RGB = 2, // addressable-LED strip (OUT_RGB, per-pixel)
};
// OUT_RGB pixel sentinel: address the whole strip (fill all pixels).
#define SKRIT_RGB_ALL 0xFF

// ---- input types (INPUT_DESC body[2]) ----
enum { SKRIT_IN_DIGITAL = 0, SKRIT_IN_ANALOG = 1 };

// ---- DATA medium (DATA_DESC kind) — what the bridged channel carries. UART is
// the default (a raw byte console); the rest are the roadmap. A device that
// doesn't answer DATA_DESC is treated as UART, so nothing regresses.
enum {
  SKRIT_DATA_UART = 0,      // raw byte console (the Serial Console)
  SKRIT_DATA_CAN = 1,       // CAN frames
  SKRIT_DATA_RS485 = 2,     // RS-485
  SKRIT_DATA_SPI = 3,       // SPI
  SKRIT_DATA_BLE_SNIFF = 4, // sniffed BLE packets
  SKRIT_DATA_LOGIC = 5,     // logic-analyzer samples
  SKRIT_DATA_I2C = 6,       // I2C transactions (first non-UART backend target)
};

// ---- pin capability bits (PIN_CAPS `caps` byte) — what a pin's silicon can do.
// The firmware mirrors these in duta_pincap.h (the per-mcu tables); the app reads
// them to constrain the provisioning picker to valid roles for each pin.
enum {
  SKRIT_PINCAP_DIGITAL = 0x01, // digital in/out
  SKRIT_PINCAP_ADC = 0x02,     // analog input
  SKRIT_PINCAP_PWM = 0x04,     // PWM / LEDC
  SKRIT_PINCAP_DAC = 0x08,     // true analog out
  SKRIT_PINCAP_I2C = 0x10,     // I²C SDA/SCL (see `bus`)
  SKRIT_PINCAP_SPI = 0x20,     // SPI signal
  SKRIT_PINCAP_TOUCH = 0x40,   // capacitive touch
};
// PIN_CAPS `warn` byte: 0 = offer clean, 1 = offer but show the reason (the row's
// name string carries the reason — a strapping/boot caution or a dual-use label).
enum { SKRIT_PIN_CLEAN = 0, SKRIT_PIN_WARN = 1 };
#define SKRIT_NO_BUS 0xFF       // PIN_CAPS `bus`: not a bus pin / matrix-routable
#define SKRIT_CONFIG_RESET 0xFF // CONFIG_SET `n`: revert to the compiled-default table

// ---- CRC-8/ATM (poly 0x07, init 0x00) ----
static inline uint8_t skrit_crc8(const uint8_t *p, size_t n) {
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
static inline size_t skrit_cobs_encode(const uint8_t *in, size_t len, uint8_t *out) {
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
static inline size_t skrit_cobs_decode(const uint8_t *in, size_t len, uint8_t *out) {
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
