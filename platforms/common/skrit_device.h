// skrit_device.h — portable Duta protocol core (header-only, no deps).
// ============================================================================
// One implementation of the skrit CMD dispatch + skrit-mc macro VM + the
// dual-CDC / skrit-mux transport framing, shared by every C/C++ platform
// (espressif, pico, zephyr, host). A platform supplies a `skrit_hal` vtable of
// thin callbacks (drive a pin, read a UART byte, get a timestamp) and a small
// loop; this header is the brain. Add a board, inherit the protocol.
//
// Usage sketch (see platforms/*/ for real ones):
//
//   static skrit_dev dev;
//   skrit_dev_init(&dev, &my_hal, /*ctx*/NULL, /*muxed*/true);
//   ... in the main loop, forever ...
//   skrit_dev_poll(&dev);                 // tee target console -> host
//   while (cmd_endpoint_has_byte())       // bytes from USB/WS/BLE CMD stream
//       skrit_dev_rx(&dev, next_byte());
//
// The core never allocates and keeps a fixed, small RAM footprint. Multi-byte
// integers on the wire are little-endian. Helpers (CRC-8, COBS) come from
// protocol.h. See ../../protocol/PROTOCOL.md.
// ============================================================================
#ifndef DUTA_SKRIT_DEVICE_H
#define DUTA_SKRIT_DEVICE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- tunables (override with -D before including) -------------------------
#ifndef SKRIT_MAX_BODY
#define SKRIT_MAX_BODY 64 // CMD body cap, matches the wire spec (LEN 0..64)
#endif
#define SKRIT_MAX_FRAME (3 + SKRIT_MAX_BODY + 1) // TYPE SEQ LEN BODY CRC
// RX accumulator must hold a COBS-encoded frame (+ channel tag on a mux link).
#ifndef SKRIT_RX_CAP
#define SKRIT_RX_CAP (SKRIT_MAX_FRAME + 8)
#endif
// Volatile scratch program (macro id 0xFF) for push-and-run with no storage.
#ifndef SKRIT_SCRATCH_CAP
#define SKRIT_SCRATCH_CAP 512
#endif
// Largest console burst wrapped into one mux DATA frame (keep COBS overhead 1B).
#ifndef SKRIT_MUX_CHUNK
#define SKRIT_MUX_CHUNK 240
#endif
#ifndef SKRIT_ENABLE_ASCII
#define SKRIT_ENABLE_ASCII 1 // hand-terminal line mode (PING/ID/STATUS/HELP)
#endif

// ---- HAL: the platform fills these in. NULL = "feature absent" -------------
// Every callback takes the user `ctx` first so a platform can avoid globals.
typedef struct skrit_hal {
  const char *name;    // device name (DEVICE_NAME / ASCII ID)
  uint16_t fw_ver;     // (hi<<8) | lo
  uint8_t caps;        // SKRIT_CAP_* — MUST set CAP_MUX iff constructed muxed
  uint8_t macro_tier;  // 0..2: highest skrit-mc tier this build runs
  uint8_t store_kb;    // persistent macro store (KB); 0 = scratch-only
  uint8_t n_outputs;   // controllable outputs (digital on/off, PWM, or RGB)
  uint8_t n_inputs;    // readable inputs (digital/analog)

  // Transport out. The core has already framed these bytes (COBS + delimiters,
  // and the channel tag on a mux link); just push them to the CMD endpoint.
  void (*link_write)(void *ctx, const uint8_t *p, uint16_t n);
  // DATA console out, host -> target UART (macro EMIT; and host DATA on a mux).
  void (*data_write)(void *ctx, const uint8_t *p, uint16_t n);
  // DATA console out, target -> host, DUAL transports only (the raw DATA port).
  // Leave NULL on a muxed device — the core wraps console bytes onto link_write.
  void (*host_write)(void *ctx, const uint8_t *p, uint16_t n);
  // DATA console in: drain up to `cap` bytes from the target UART into `out`,
  // return the count (0 if idle). The core tees these to the host and feeds the
  // macro VM's EXPECT matcher. NULL = no console bridge.
  uint16_t (*data_read)(void *ctx, uint8_t *out, uint16_t cap);

  // Outputs. out_get returns 0/1. out_desc fills *type (SKRIT_CTRL_*) and *name.
  void (*out_set)(void *ctx, uint8_t idx, uint8_t on);
  uint8_t (*out_get)(void *ctx, uint8_t idx);
  void (*out_desc)(void *ctx, uint8_t idx, uint8_t *type, const char **name);

  // PWM (NULL unless a pwm-type output exists). duty 0..1023. pwm_set returns 0
  // if the output can't PWM (the core answers BADARGS); pwm_get reads it back.
  uint8_t (*pwm_set)(void *ctx, uint8_t idx, uint16_t duty);
  uint16_t (*pwm_get)(void *ctx, uint8_t idx);

  // RGB / addressable LED (NULL unless an rgb-type output exists).
  //   rgb_count: pixels in output `idx` (0 = not an RGB output).
  //   rgb_set:   set pixel `px` to r,g,b; px == SKRIT_RGB_ALL fills the strip.
  //              returns 0 if `idx` isn't RGB or `px` is out of range.
  //   rgb_get:   read pixel `px`'s color (clamped to the strip).
  uint8_t (*rgb_count)(void *ctx, uint8_t idx);
  uint8_t (*rgb_set)(void *ctx, uint8_t idx, uint8_t px, uint8_t r, uint8_t g, uint8_t b);
  void (*rgb_get)(void *ctx, uint8_t idx, uint8_t px, uint8_t *r, uint8_t *g, uint8_t *b);

  // Inputs (NULL if n_inputs == 0). in_get: digital 0/1, analog 0..1023.
  void (*in_desc)(void *ctx, uint8_t idx, uint8_t *type, const char **name);
  uint16_t (*in_get)(void *ctx, uint8_t idx);

  // DATA-UART control (NULL unless CAP_SERIAL). parity = SKRIT_PAR_*.
  void (*serial_get)(void *ctx, uint32_t *baud, uint8_t *bits, uint8_t *par, uint8_t *stop);
  void (*serial_set)(void *ctx, uint32_t baud, uint8_t bits, uint8_t par, uint8_t stop);
  void (*serial_signal)(void *ctx, uint8_t mask, uint8_t value);

  // System (NULL unless CAP_REBOOT). mode = SKRIT_REBOOT_*.
  void (*reboot)(void *ctx, uint8_t mode);

  // Time + cooperative service, for the macro VM's waits. millis: ms tick.
  // pump: keep USB/transport + watchdog alive during a blocking wait (may be NULL).
  uint32_t (*millis)(void *ctx);
  void (*pump)(void *ctx);

  // ---- network auth (NULL/0 on USB/BLE — those aren't gated). These trail the
  // struct so existing positional HAL initializers zero-fill them. ----
  uint8_t auth_required; // 1 = require AUTH before other CMDs / DATA bridging
  uint8_t (*auth_check)(void *ctx, const char *pw, uint8_t n);    // 1 if pw matches
  uint8_t (*auth_set)(void *ctx, const char *pw, uint8_t n);      // store new pw; 1 = ok
  uint8_t (*auth_is_default)(void *ctx);                          // 1 if still factory default

  // ---- DATA medium (what the bridged channel carries). 0 = UART console (the
  // default; trails the struct so positional initializers zero-fill it). ----
  uint8_t data_kind;     // SKRIT_DATA_*
  const char *data_name; // optional label; NULL -> a built-in name per kind

  // ---- PWM frequency/resolution (NULL = fixed; OUTPUT_PWM duty still works) ----
  //   pwm_config_get: report this output's current freq (Hz) + resolution (bits).
  //   pwm_config_set: apply freq/res where supported (0 = leave); return 1 if the
  //   output is PWM (even if a value couldn't change — the GET reports the truth).
  void (*pwm_config_get)(void *ctx, uint8_t idx, uint32_t *freq, uint8_t *res);
  uint8_t (*pwm_config_set)(void *ctx, uint8_t idx, uint32_t freq, uint8_t res);

  // ---- runtime IO provisioning (all NULL = fixed board; pin_caps != NULL
  // advertises SKRIT_FLAG_PROVISION and enables PIN_CAPS/CONFIG_GET/CONFIG_SET) ----
  //   pin_caps:   the provisioning menu. Returns `total` offerable pins; when
  //               index < total, fills the pin's resolved info (mcu ∩ board).
  //   config_get: the current IO table. Returns row count `n`; when index < n,
  //               fills that row (the read counterpart of config_set).
  //   config_set: replace the IO table from a raw CONFIG_SET body (n + rows;
  //               n=SKRIT_CONFIG_RESET reverts to default). Validates + persists.
  //               Returns a SKRIT_ST_* status; on BADARGS, *bad_index = bad row.
  uint8_t (*pin_caps)(void *ctx, uint8_t index, int16_t *pin, uint8_t *caps,
                      uint8_t *warn, uint8_t *bus, const char **name);
  uint8_t (*config_get)(void *ctx, uint8_t index, uint8_t *type, int16_t *pin,
                        uint8_t *flags, uint16_t *arg, const char **name);
  uint8_t (*config_set)(void *ctx, const uint8_t *body, uint8_t len, uint8_t *bad_index);
} skrit_hal;

// ---- device state ----------------------------------------------------------
typedef struct skrit_dev {
  const skrit_hal *hal;
  void *ctx;
  uint8_t muxed; // 1 = single endpoint carries both channels (skrit-mux)
  uint8_t authed; // session authenticated (only matters when hal->auth_required)

  // CMD/mux receive state machine
  uint8_t rx[SKRIT_RX_CAP];
  uint16_t rx_len;
  uint8_t mode; // 0 idle, 1 ascii, 2 binary/mux-frame
#if SKRIT_ENABLE_ASCII
  char line[40];
  uint8_t line_len;
#endif

  // scratch macro program (id 0xFF) + write cursor
  uint8_t scratch[SKRIT_SCRATCH_CAP];
  uint16_t scratch_len;   // committed length
  uint16_t scratch_total; // expected (from WRITE_BEGIN)

  // macro VM: EXPECT matcher fed by skrit_dev_poll()
  const uint8_t *exp_pat;
  uint8_t exp_n, exp_pos, exp_hit;
} skrit_dev;

// ---- forward decls ---------------------------------------------------------
static void skrit_dev_init(skrit_dev *d, const skrit_hal *hal, void *ctx, uint8_t muxed);
static void skrit_dev_rx(skrit_dev *d, uint8_t b);     // feed CMD-endpoint bytes
static void skrit_dev_poll(skrit_dev *d);              // tee target console -> host
// Optional: platforms call this to push an async event (NUS button, input edge).
// `static inline` so it never warns when a platform doesn't use it.
static inline void skrit_dev_emit_event(skrit_dev *d, uint8_t type, const uint8_t *body, uint8_t n);
// Network transports call this on each new connection to drop back to unauthed.
static inline void skrit_dev_reset_auth(skrit_dev *d) { d->authed = 0; }
// True while a network device is waiting for AUTH (gate other CMDs + DATA).
static inline uint8_t skrit__gated(skrit_dev *d) { return d->hal->auth_required && !d->authed; }

// ===========================================================================
// framing
// ===========================================================================

// Frame `raw` (TYPE SEQ LEN BODY CRC, or a DATA blob) onto the CMD endpoint.
// `channel` is ignored on a dual link; on a mux link it is prepended pre-COBS.
static void skrit__send(skrit_dev *d, uint8_t channel, const uint8_t *raw, uint16_t n) {
  uint8_t buf[1 + SKRIT_RX_CAP];
  uint8_t cobs[SKRIT_RX_CAP + 8];
  uint16_t plen = n;
  const uint8_t *payload = raw;
  if (d->muxed) {
    if ((uint16_t)(n + 1) > sizeof buf) return;
    buf[0] = channel;
    memcpy(buf + 1, raw, n);
    payload = buf;
    plen = (uint16_t)(n + 1);
  }
  size_t en = skrit_cobs_encode(payload, plen, cobs);
  uint8_t z = 0x00;
  if (d->hal->link_write) {
    d->hal->link_write(d->ctx, &z, 1);
    d->hal->link_write(d->ctx, cobs, (uint16_t)en);
    d->hal->link_write(d->ctx, &z, 1);
  }
}

// Send a CMD response: TYPE SEQ LEN BODY[blen] CRC8, framed for the link.
static void skrit__respond(skrit_dev *d, uint8_t type, uint8_t seq, const uint8_t *body, uint8_t blen) {
  uint8_t raw[SKRIT_MAX_FRAME];
  if (blen > SKRIT_MAX_BODY) blen = SKRIT_MAX_BODY;
  raw[0] = type;
  raw[1] = seq;
  raw[2] = blen;
  if (blen) memcpy(raw + 3, body, blen);
  raw[3 + blen] = skrit_crc8(raw, (size_t)(3 + blen));
  skrit__send(d, SKRIT_MUX_CMD, raw, (uint16_t)(4 + blen));
}

static void skrit__status(skrit_dev *d, uint8_t type, uint8_t seq, uint8_t status) {
  skrit__respond(d, (uint8_t)(type | SKRIT_RESP), seq, &status, 1);
}

// Push DATA console bytes to the host (target -> host), chunked + framed.
static void skrit__console_out(skrit_dev *d, const uint8_t *p, uint16_t n) {
  if (!d->muxed) {
    if (d->hal->host_write && n) d->hal->host_write(d->ctx, p, n);
    return;
  }
  while (n) {
    uint16_t c = n > SKRIT_MUX_CHUNK ? SKRIT_MUX_CHUNK : n;
    skrit__send(d, SKRIT_MUX_DATA, p, c);
    p += c;
    n -= c;
  }
}

// Emit an unsolicited device->host event (TYPE in 0x50..0x5F, SEQ=0).
static inline void skrit_dev_emit_event(skrit_dev *d, uint8_t type, const uint8_t *body, uint8_t n) {
  if (type < SKRIT_EVENT_LO || type > SKRIT_EVENT_HI) return;
  skrit__respond(d, type, 0, body, n); // RESP bit clear: it's an event, not a reply
}

// ===========================================================================
// skrit-mc macro VM (tiers 1-2). Runs `prog` of `len` bytes; returns a STATUS.
// ===========================================================================
static int skrit__cmp(uint16_t a, uint8_t op, uint16_t b) {
  switch (op) {
  case SKRIT_MC_GT: return a > b;
  case SKRIT_MC_LT: return a < b;
  case SKRIT_MC_GE: return a >= b;
  case SKRIT_MC_LE: return a <= b;
  case SKRIT_MC_EQ: return a == b;
  case SKRIT_MC_NE: return a != b;
  }
  return 0;
}

// Pump console + (if armed) feed the EXPECT matcher. Tees bytes to the host so
// the human terminal stays live during a macro wait.
static void skrit_dev_poll(skrit_dev *d) {
  uint8_t buf[64];
  if (!d->hal->data_read) return;
  uint16_t got = d->hal->data_read(d->ctx, buf, sizeof buf);
  if (!got) return;
  if (skrit__gated(d)) return; // drained, but don't leak the console while unauthed
  skrit__console_out(d, buf, got);
  if (d->exp_pat && !d->exp_hit) {
    for (uint16_t i = 0; i < got; i++) {
      uint8_t c = buf[i];
      if (c == d->exp_pat[d->exp_pos]) {
        if (++d->exp_pos == d->exp_n) { d->exp_hit = 1; break; }
      } else {
        d->exp_pos = (c == d->exp_pat[0]) ? 1 : 0;
      }
    }
  }
}

static void skrit__wait(skrit_dev *d, uint32_t until) {
  while (d->hal->millis && d->hal->millis(d->ctx) < until) {
    skrit_dev_poll(d);
    if (d->hal->pump) d->hal->pump(d->ctx);
    if (d->exp_hit) return; // EXPECT short-circuits the wait
  }
}

// Returns a SKRIT_ST_* code. `tier` gates ops above the device's VM tier.
static uint8_t skrit__run_program(skrit_dev *d, const uint8_t *prog, uint16_t len) {
  uint8_t tier = d->hal->macro_tier;
  if (tier == 0) return SKRIT_ST_UNSUPPORTED;
  if (len < 2 || prog[0] != SKRIT_MC_VER) return SKRIT_ST_BADARGS;
  uint16_t pc = 1;
  uint8_t outcome_ok = 1; // the single boolean outcome flag (init OK)
  uint32_t (*ms)(void *) = d->hal->millis;
  while (pc < len) {
    uint8_t op = prog[pc++];
    switch (op) {
    case SKRIT_MC_END:
      return SKRIT_ST_OK;
    case SKRIT_MC_EMIT: {
      if (pc >= len) return SKRIT_ST_BADARGS;
      uint8_t n = prog[pc++];
      if ((uint16_t)(pc + n) > len) return SKRIT_ST_BADARGS;
      if (d->hal->data_write && n) d->hal->data_write(d->ctx, prog + pc, n);
      pc += n;
      break;
    }
    case SKRIT_MC_DELAY: {
      if ((uint16_t)(pc + 2) > len) return SKRIT_ST_BADARGS;
      uint16_t m = (uint16_t)(prog[pc] | (prog[pc + 1] << 8));
      pc += 2;
      if (ms) skrit__wait(d, ms(d->ctx) + m);
      break;
    }
    case SKRIT_MC_SETOUT: {
      if ((uint16_t)(pc + 2) > len) return SKRIT_ST_BADARGS;
      uint8_t idx = prog[pc], val = prog[pc + 1];
      pc += 2;
      if (d->hal->out_set && idx < d->hal->n_outputs) d->hal->out_set(d->ctx, idx, val ? 1 : 0);
      break;
    }
    case SKRIT_MC_SETPWM: {
      if ((uint16_t)(pc + 3) > len) return SKRIT_ST_BADARGS;
      uint8_t idx = prog[pc];
      uint16_t duty = (uint16_t)(prog[pc + 1] | (prog[pc + 2] << 8));
      pc += 3;
      if (d->hal->pwm_set && idx < d->hal->n_outputs) d->hal->pwm_set(d->ctx, idx, duty);
      break; // no PWM output -> silently no-op, like an unwired SETOUT
    }
    case SKRIT_MC_SETRGB: {
      if ((uint16_t)(pc + 4) > len) return SKRIT_ST_BADARGS;
      uint8_t idx = prog[pc];
      uint8_t rr = prog[pc + 1], gg = prog[pc + 2], bb = prog[pc + 3];
      pc += 4;
      if (d->hal->rgb_set && idx < d->hal->n_outputs)
        d->hal->rgb_set(d->ctx, idx, SKRIT_RGB_ALL, rr, gg, bb); // fill the strip
      break; // no RGB output -> silently no-op
    }
    case SKRIT_MC_EXPECT: {
      if (tier < SKRIT_TIER_INTERACTIVE) return SKRIT_ST_UNSUPPORTED;
      if ((uint16_t)(pc + 3) > len) return SKRIT_ST_BADARGS;
      uint16_t to = (uint16_t)(prog[pc] | (prog[pc + 1] << 8));
      uint8_t n = prog[pc + 2];
      pc += 3;
      if ((uint16_t)(pc + n) > len || n == 0) return SKRIT_ST_BADARGS;
      d->exp_pat = prog + pc;
      d->exp_n = n;
      d->exp_pos = 0;
      d->exp_hit = 0;
      if (ms) skrit__wait(d, ms(d->ctx) + (to ? to : 1));
      outcome_ok = d->exp_hit ? 1 : 0;
      d->exp_pat = NULL;
      pc += n;
      break;
    }
    case SKRIT_MC_WAITIO: {
      if (tier < SKRIT_TIER_INTERACTIVE) return SKRIT_ST_UNSUPPORTED;
      if ((uint16_t)(pc + 6) > len) return SKRIT_ST_BADARGS;
      uint8_t idx = prog[pc], cmp = prog[pc + 1];
      uint16_t val = (uint16_t)(prog[pc + 2] | (prog[pc + 3] << 8));
      uint16_t to = (uint16_t)(prog[pc + 4] | (prog[pc + 5] << 8));
      pc += 6;
      uint8_t met = 0;
      uint32_t until = ms ? ms(d->ctx) + (to ? to : 1) : 0;
      while (ms && ms(d->ctx) < until) {
        skrit_dev_poll(d);
        if (d->hal->pump) d->hal->pump(d->ctx);
        if (d->hal->in_get && idx < d->hal->n_inputs &&
            skrit__cmp(d->hal->in_get(d->ctx, idx), cmp, val)) { met = 1; break; }
      }
      outcome_ok = met;
      break;
    }
    case SKRIT_MC_WAITOK:
      if (tier < SKRIT_TIER_INTERACTIVE) return SKRIT_ST_UNSUPPORTED;
      if (!outcome_ok) return SKRIT_ST_BUSY; // run aborted: last read failed
      break;
    default:
      return SKRIT_ST_UNSUPPORTED; // reserved / over-tier opcode
    }
  }
  return SKRIT_ST_OK;
}

// Built-in label for a DATA medium kind (used when the HAL gives no data_name).
static const char *skrit__data_kind_name(uint8_t kind) {
  switch (kind) {
  case SKRIT_DATA_CAN: return "CAN";
  case SKRIT_DATA_RS485: return "RS-485";
  case SKRIT_DATA_SPI: return "SPI";
  case SKRIT_DATA_BLE_SNIFF: return "BLE sniffer";
  case SKRIT_DATA_LOGIC: return "Logic";
  case SKRIT_DATA_I2C: return "I2C";
  default: return "UART";
  }
}

// ===========================================================================
// CMD dispatch — one decoded frame: TYPE SEQ LEN BODY CRC (already CRC-checked)
// ===========================================================================
static void skrit__dispatch(skrit_dev *d, const uint8_t *raw, uint16_t n) {
  const skrit_hal *h = d->hal;
  (void)n; // length already validated against LEN in skrit__on_frame
  uint8_t type = raw[0], seq = raw[1], len = raw[2];
  const uint8_t *b = raw + 3;
  uint8_t body[SKRIT_MAX_BODY];
  uint8_t bl = 0;

  // Auth gate: an unauthenticated network session may only PING/INFO/AUTH.
  if (skrit__gated(d) && type != SKRIT_PING && type != SKRIT_INFO && type != SKRIT_AUTH) {
    skrit__status(d, type, seq, SKRIT_ST_UNAUTH);
    return;
  }

  switch (type) {
  case SKRIT_PING:
    body[bl++] = SKRIT_ST_OK;
    body[bl++] = 'P'; body[bl++] = 'O'; body[bl++] = 'N'; body[bl++] = 'G';
    skrit__respond(d, SKRIT_PING | SKRIT_RESP, seq, body, bl);
    break;
  case SKRIT_INFO:
    body[bl++] = SKRIT_ST_OK;
    body[bl++] = (uint8_t)(h->fw_ver & 0xFF);
    body[bl++] = (uint8_t)(h->fw_ver >> 8);
    body[bl++] = h->caps;
    body[bl++] = h->n_outputs;
    body[bl++] = h->store_kb;
    body[bl++] = (uint8_t)SKRIT_PROTO_VER;
    body[bl++] = h->n_inputs;
    body[bl++] = h->macro_tier;
    body[bl++] = (h->auth_required ? SKRIT_FLAG_AUTH_REQUIRED : 0) |
                 ((h->auth_is_default && h->auth_is_default(d->ctx)) ? SKRIT_FLAG_DEFAULT_CRED : 0) |
                 (h->pin_caps ? SKRIT_FLAG_PROVISION : 0);
    skrit__respond(d, SKRIT_INFO | SKRIT_RESP, seq, body, bl);
    break;
  case SKRIT_AUTH:
    if (h->auth_check && h->auth_check(d->ctx, (const char *)b, len)) {
      d->authed = 1;
      skrit__status(d, type, seq, SKRIT_ST_OK);
    } else {
      skrit__status(d, type, seq, SKRIT_ST_UNAUTH);
    }
    break;
  case SKRIT_AUTH_SET: // only reached when authed (or on a non-gated device)
    if (!h->auth_set) skrit__status(d, type, seq, SKRIT_ST_UNSUPPORTED);
    else if (len == 0 || len > SKRIT_PASSWORD_MAX) skrit__status(d, type, seq, SKRIT_ST_BADARGS);
    else if (h->auth_set(d->ctx, (const char *)b, len)) skrit__status(d, type, seq, SKRIT_ST_OK);
    else skrit__status(d, type, seq, SKRIT_ST_STORAGE);
    break;
  case SKRIT_DATA_DESC: {
    body[bl++] = SKRIT_ST_OK;
    body[bl++] = h->data_kind;
    const char *nm = h->data_name ? h->data_name : skrit__data_kind_name(h->data_kind);
    while (*nm && bl < SKRIT_MAX_BODY) body[bl++] = (uint8_t)*nm++;
    skrit__respond(d, type | SKRIT_RESP, seq, body, bl);
    break;
  }
  case SKRIT_DEVICE_NAME: {
    body[bl++] = SKRIT_ST_OK;
    const char *s = h->name ? h->name : "Duta";
    while (*s && bl < SKRIT_MAX_BODY) body[bl++] = (uint8_t)*s++;
    skrit__respond(d, SKRIT_DEVICE_NAME | SKRIT_RESP, seq, body, bl);
    break;
  }
  case SKRIT_REBOOT:
    if (!h->reboot) { skrit__status(d, type, seq, SKRIT_ST_UNSUPPORTED); break; }
    skrit__status(d, type, seq, SKRIT_ST_OK); // ack before we go down
    h->reboot(d->ctx, len >= 1 ? b[0] : (uint8_t)SKRIT_REBOOT_APP);
    break;

  case SKRIT_OUT_SET:
    if (len >= 2 && b[0] < h->n_outputs && h->out_set) {
      h->out_set(d->ctx, b[0], b[1] ? 1 : 0);
      skrit__status(d, type, seq, SKRIT_ST_OK);
    } else skrit__status(d, type, seq, SKRIT_ST_BADARGS);
    break;
  case SKRIT_OUT_GET: {
    body[bl++] = SKRIT_ST_OK;
    uint8_t bm = 0;
    for (uint8_t i = 0; i < h->n_outputs && i < 8; i++)
      if (h->out_get && h->out_get(d->ctx, i)) bm |= (uint8_t)(1u << i);
    body[bl++] = bm;
    skrit__respond(d, type | SKRIT_RESP, seq, body, bl);
    break;
  }
  case SKRIT_OUT_TOGGLE:
    if (len >= 1 && b[0] < h->n_outputs && h->out_set && h->out_get) {
      h->out_set(d->ctx, b[0], h->out_get(d->ctx, b[0]) ? 0 : 1);
      body[bl++] = SKRIT_ST_OK;
      uint8_t bm = 0;
      for (uint8_t i = 0; i < h->n_outputs && i < 8; i++)
        if (h->out_get(d->ctx, i)) bm |= (uint8_t)(1u << i);
      body[bl++] = bm;
      skrit__respond(d, type | SKRIT_RESP, seq, body, bl);
    } else skrit__status(d, type, seq, SKRIT_ST_BADARGS);
    break;
  case SKRIT_OUT_PULSE: {
    if (len >= 3 && b[0] < h->n_outputs && h->out_set) {
      uint8_t idx = b[0];
      uint16_t pulse = (uint16_t)(b[1] | (b[2] << 8));
      uint8_t prev = h->out_get ? h->out_get(d->ctx, idx) : 0;
      h->out_set(d->ctx, idx, prev ? 0 : 1); // pulse to the opposite level
      skrit__status(d, type, seq, SKRIT_ST_OK);
      if (h->millis) skrit__wait(d, h->millis(d->ctx) + (pulse ? pulse : 1));
      h->out_set(d->ctx, idx, prev); // restore
    } else skrit__status(d, type, seq, SKRIT_ST_BADARGS);
    break;
  }
  case SKRIT_OUT_DESC: {
    if (len >= 1 && b[0] < h->n_outputs && h->out_desc) {
      uint8_t ot = SKRIT_CTRL_IO;
      const char *nm = "";
      h->out_desc(d->ctx, b[0], &ot, &nm);
      body[bl++] = SKRIT_ST_OK;
      body[bl++] = b[0];
      body[bl++] = ot;
      while (*nm && bl < SKRIT_MAX_BODY) body[bl++] = (uint8_t)*nm++;
      skrit__respond(d, type | SKRIT_RESP, seq, body, bl);
    } else skrit__status(d, type, seq, SKRIT_ST_BADARGS);
    break;
  }
  case SKRIT_INPUT_DESC: {
    if (len >= 1 && b[0] < h->n_inputs && h->in_desc) {
      uint8_t it = SKRIT_IN_DIGITAL;
      const char *nm = "";
      h->in_desc(d->ctx, b[0], &it, &nm);
      body[bl++] = SKRIT_ST_OK;
      body[bl++] = b[0];
      body[bl++] = it;
      while (*nm && bl < SKRIT_MAX_BODY) body[bl++] = (uint8_t)*nm++;
      skrit__respond(d, type | SKRIT_RESP, seq, body, bl);
    } else skrit__status(d, type, seq, SKRIT_ST_BADARGS);
    break;
  }
  case SKRIT_INPUT_GET: {
    if (len >= 1 && b[0] < h->n_inputs && h->in_get) {
      uint16_t v = h->in_get(d->ctx, b[0]);
      body[bl++] = SKRIT_ST_OK;
      body[bl++] = b[0];
      body[bl++] = (uint8_t)(v & 0xFF);
      body[bl++] = (uint8_t)(v >> 8);
      skrit__respond(d, type | SKRIT_RESP, seq, body, bl);
    } else skrit__status(d, type, seq, SKRIT_ST_BADARGS);
    break;
  }

  case SKRIT_SERIAL_GET:
    if (h->serial_get) {
      uint32_t baud = 0; uint8_t bits = 8, par = 0, stop = 1;
      h->serial_get(d->ctx, &baud, &bits, &par, &stop);
      body[bl++] = SKRIT_ST_OK;
      body[bl++] = (uint8_t)(baud & 0xFF);
      body[bl++] = (uint8_t)((baud >> 8) & 0xFF);
      body[bl++] = (uint8_t)((baud >> 16) & 0xFF);
      body[bl++] = (uint8_t)((baud >> 24) & 0xFF);
      body[bl++] = bits; body[bl++] = par; body[bl++] = stop;
      skrit__respond(d, type | SKRIT_RESP, seq, body, bl);
    } else skrit__status(d, type, seq, SKRIT_ST_UNSUPPORTED);
    break;
  case SKRIT_SERIAL_SET:
    if (h->serial_set && len >= 7) {
      uint32_t baud = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                      ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
      h->serial_set(d->ctx, baud, b[4], b[5], b[6]);
      skrit__status(d, type, seq, SKRIT_ST_OK);
    } else skrit__status(d, type, seq, h->serial_set ? SKRIT_ST_BADARGS : SKRIT_ST_UNSUPPORTED);
    break;
  case SKRIT_SERIAL_SIGNAL:
    if (h->serial_signal && len >= 2) {
      h->serial_signal(d->ctx, b[0], b[1]);
      skrit__status(d, type, seq, SKRIT_ST_OK);
    } else skrit__status(d, type, seq, h->serial_signal ? SKRIT_ST_BADARGS : SKRIT_ST_UNSUPPORTED);
    break;
  case SKRIT_OUT_PWM: {
    // len 1 = read back duty; len >= 3 = set duty (LE, 0..1023)
    if (!h->pwm_set || !h->pwm_get) { skrit__status(d, type, seq, SKRIT_ST_UNSUPPORTED); break; }
    if (len < 1 || b[0] >= h->n_outputs) { skrit__status(d, type, seq, SKRIT_ST_BADARGS); break; }
    if (len >= 3) {
      uint16_t duty = (uint16_t)(b[1] | (b[2] << 8));
      if (duty > 1023 || !h->pwm_set(d->ctx, b[0], duty)) {
        skrit__status(d, type, seq, SKRIT_ST_BADARGS);
        break;
      }
    }
    uint16_t cur = h->pwm_get(d->ctx, b[0]);
    body[bl++] = SKRIT_ST_OK;
    body[bl++] = b[0];
    body[bl++] = (uint8_t)(cur & 0xFF);
    body[bl++] = (uint8_t)(cur >> 8);
    skrit__respond(d, type | SKRIT_RESP, seq, body, bl);
    break;
  }
  case SKRIT_PWM_CONFIG: {
    // len 1 = read freq/res; len 6 = set (index, freq(4), res(1); 0 = leave)
    if (!h->pwm_config_get) { skrit__status(d, type, seq, SKRIT_ST_UNSUPPORTED); break; }
    if (len < 1 || b[0] >= h->n_outputs) { skrit__status(d, type, seq, SKRIT_ST_BADARGS); break; }
    if (len >= 6) {
      uint32_t freq = (uint32_t)b[1] | ((uint32_t)b[2] << 8) | ((uint32_t)b[3] << 16) |
                      ((uint32_t)b[4] << 24);
      if (!h->pwm_config_set || !h->pwm_config_set(d->ctx, b[0], freq, b[5])) {
        skrit__status(d, type, seq, SKRIT_ST_BADARGS); // not a PWM output
        break;
      }
    } else if (len != 1) {
      skrit__status(d, type, seq, SKRIT_ST_BADARGS);
      break;
    }
    uint32_t freq = 0;
    uint8_t res = 0;
    h->pwm_config_get(d->ctx, b[0], &freq, &res);
    body[bl++] = SKRIT_ST_OK;
    body[bl++] = b[0];
    body[bl++] = (uint8_t)(freq & 0xFF);
    body[bl++] = (uint8_t)((freq >> 8) & 0xFF);
    body[bl++] = (uint8_t)((freq >> 16) & 0xFF);
    body[bl++] = (uint8_t)((freq >> 24) & 0xFF);
    body[bl++] = res;
    skrit__respond(d, type | SKRIT_RESP, seq, body, bl);
    break;
  }
  case SKRIT_PIN_CAPS: {
    // index(1) -> status, index, total[, pin(2), caps, warn, bus, name] (the
    // provisioning menu — offerable pins resolved from mcu ∩ board).
    if (!h->pin_caps) { skrit__status(d, type, seq, SKRIT_ST_UNSUPPORTED); break; }
    if (len < 1) { skrit__status(d, type, seq, SKRIT_ST_BADARGS); break; }
    int16_t pin = 0; uint8_t caps = 0, warn = 0, bus = SKRIT_NO_BUS; const char *nm = 0;
    uint8_t total = h->pin_caps(d->ctx, b[0], &pin, &caps, &warn, &bus, &nm);
    body[bl++] = SKRIT_ST_OK;
    body[bl++] = b[0];
    body[bl++] = total;
    if (b[0] < total) {
      body[bl++] = (uint8_t)(pin & 0xFF);
      body[bl++] = (uint8_t)((pin >> 8) & 0xFF);
      body[bl++] = caps;
      body[bl++] = warn;
      body[bl++] = bus;
      if (nm) while (*nm && bl < SKRIT_MAX_BODY) body[bl++] = (uint8_t)*nm++;
    }
    skrit__respond(d, type | SKRIT_RESP, seq, body, bl);
    break;
  }
  case SKRIT_CONFIG_GET: {
    // index(1) -> status, index, n[, type, pin(2), flags, arg(2), name] (current table row).
    if (!h->config_get) { skrit__status(d, type, seq, SKRIT_ST_UNSUPPORTED); break; }
    if (len < 1) { skrit__status(d, type, seq, SKRIT_ST_BADARGS); break; }
    uint8_t rtype = 0, flags = 0; int16_t pin = 0; uint16_t arg = 0; const char *nm = 0;
    uint8_t nrows = h->config_get(d->ctx, b[0], &rtype, &pin, &flags, &arg, &nm);
    body[bl++] = SKRIT_ST_OK;
    body[bl++] = b[0];
    body[bl++] = nrows;
    if (b[0] < nrows) {
      body[bl++] = rtype;
      body[bl++] = (uint8_t)(pin & 0xFF);
      body[bl++] = (uint8_t)((pin >> 8) & 0xFF);
      body[bl++] = flags;
      body[bl++] = (uint8_t)(arg & 0xFF);
      body[bl++] = (uint8_t)((arg >> 8) & 0xFF);
      if (nm) while (*nm && bl < SKRIT_MAX_BODY) body[bl++] = (uint8_t)*nm++;
    }
    skrit__respond(d, type | SKRIT_RESP, seq, body, bl);
    break;
  }
  case SKRIT_CONFIG_SET: {
    // n(1), rows -> status[, bad_index]. The HAL validates against PIN_CAPS,
    // persists, and applies on the next reboot. n=SKRIT_CONFIG_RESET = default.
    if (!h->config_set) { skrit__status(d, type, seq, SKRIT_ST_UNSUPPORTED); break; }
    if (len < 1) { skrit__status(d, type, seq, SKRIT_ST_BADARGS); break; }
    uint8_t bad = 0;
    uint8_t st = h->config_set(d->ctx, b, len, &bad);
    body[bl++] = st;
    if (st == SKRIT_ST_BADARGS) body[bl++] = bad;
    skrit__respond(d, type | SKRIT_RESP, seq, body, bl);
    break;
  }
  case SKRIT_OUT_RGB: {
    // len 1 = read; len 4 = set all pixels (r,g,b); len 5 = set one (pixel,r,g,b)
    if (!h->rgb_set || !h->rgb_get || !h->rgb_count) {
      skrit__status(d, type, seq, SKRIT_ST_UNSUPPORTED);
      break;
    }
    if (len < 1 || b[0] >= h->n_outputs) { skrit__status(d, type, seq, SKRIT_ST_BADARGS); break; }
    uint8_t count = h->rgb_count(d->ctx, b[0]);
    if (count == 0) { skrit__status(d, type, seq, SKRIT_ST_BADARGS); break; } // not an RGB output
    uint8_t ok = 1;
    if (len >= 5) ok = h->rgb_set(d->ctx, b[0], b[1], b[2], b[3], b[4]); // one pixel
    else if (len >= 4) ok = h->rgb_set(d->ctx, b[0], SKRIT_RGB_ALL, b[1], b[2], b[3]); // fill
    else if (len != 1) ok = 0; // 2 or 3 bytes is malformed
    if (!ok) { skrit__status(d, type, seq, SKRIT_ST_BADARGS); break; }
    uint8_t rr = 0, gg = 0, bb = 0;
    h->rgb_get(d->ctx, b[0], 0, &rr, &gg, &bb); // pixel 0's color
    body[bl++] = SKRIT_ST_OK;
    body[bl++] = b[0];
    body[bl++] = count;
    body[bl++] = rr;
    body[bl++] = gg;
    body[bl++] = bb;
    skrit__respond(d, type | SKRIT_RESP, seq, body, bl);
    break;
  }

  // ---- macros: scratch (0xFF) push-and-run is always available; persistent
  //      ids need a storage HAL (not yet wired here -> STORAGE error) ----
  case SKRIT_MACRO_WRITE_BEGIN:
    if (len >= 3 && b[0] == SKRIT_MC_SCRATCH) {
      uint16_t total = (uint16_t)(b[1] | (b[2] << 8));
      if (total > SKRIT_SCRATCH_CAP) { skrit__status(d, type, seq, SKRIT_ST_STORAGE); break; }
      d->scratch_total = total;
      d->scratch_len = 0;
      skrit__status(d, type, seq, SKRIT_ST_OK);
    } else skrit__status(d, type, seq, len >= 1 ? SKRIT_ST_STORAGE : SKRIT_ST_BADARGS);
    break;
  case SKRIT_MACRO_WRITE_DATA:
    if (len >= 3 && b[0] == SKRIT_MC_SCRATCH) {
      uint16_t off = (uint16_t)(b[1] | (b[2] << 8));
      uint8_t n2 = (uint8_t)(len - 3);
      if ((uint16_t)(off + n2) > SKRIT_SCRATCH_CAP) { skrit__status(d, type, seq, SKRIT_ST_STORAGE); break; }
      memcpy(d->scratch + off, b + 3, n2);
      if ((uint16_t)(off + n2) > d->scratch_len) d->scratch_len = (uint16_t)(off + n2);
      skrit__status(d, type, seq, SKRIT_ST_OK);
    } else skrit__status(d, type, seq, len >= 1 ? SKRIT_ST_STORAGE : SKRIT_ST_BADARGS);
    break;
  case SKRIT_MACRO_WRITE_END:
    if (len >= 1 && b[0] == SKRIT_MC_SCRATCH) skrit__status(d, type, seq, SKRIT_ST_OK);
    else skrit__status(d, type, seq, len >= 1 ? SKRIT_ST_STORAGE : SKRIT_ST_BADARGS);
    break;
  case SKRIT_MACRO_RUN:
    if (len >= 1 && b[0] == SKRIT_MC_SCRATCH) {
      uint8_t st = skrit__run_program(d, d->scratch, d->scratch_len);
      skrit__status(d, type, seq, st);
    } else skrit__status(d, type, seq, len >= 1 ? SKRIT_ST_NOTFOUND : SKRIT_ST_BADARGS);
    break;
  case SKRIT_MACRO_LIST: {
    // No persistent store here: report an empty list (count 0) so the app's
    // enumeration terminates cleanly rather than erroring.
    body[bl++] = SKRIT_ST_OK;
    body[bl++] = 0;
    skrit__respond(d, type | SKRIT_RESP, seq, body, bl);
    break;
  }
  case SKRIT_MACRO_META:
  case SKRIT_MACRO_READ:
  case SKRIT_MACRO_DELETE:
  case SKRIT_EE_READ:
  case SKRIT_EE_WRITE:
    skrit__status(d, type, seq, SKRIT_ST_STORAGE);
    break;
  case SKRIT_CFG_GET:
  case SKRIT_CFG_SET:
    skrit__status(d, type, seq, SKRIT_ST_OK); // no keys defined yet; accept no-op
    break;

  default:
    skrit__status(d, type, seq, SKRIT_ST_BADMSG);
    break;
  }
}

// Validate a decoded raw frame, then dispatch (or reply BADCRC/BADARGS).
static void skrit__on_frame(skrit_dev *d, const uint8_t *raw, uint16_t n) {
  if (n < 4) return;
  uint8_t len = raw[2];
  if ((uint16_t)(len + 4) != n) { skrit__status(d, raw[0], raw[1], SKRIT_ST_BADARGS); return; }
  if (skrit_crc8(raw, (size_t)(3 + len)) != raw[3 + len]) {
    skrit__status(d, raw[0], raw[1], SKRIT_ST_BADCRC);
    return;
  }
  skrit__dispatch(d, raw, n);
}

#if SKRIT_ENABLE_ASCII
static int skrit__streq(const char *s, const char *t) {
  while (*t) { if (*s++ != *t++) return 0; }
  return *s == '\0';
}
static void skrit__ascii_write(skrit_dev *d, const char *s) {
  // ASCII replies go on the CMD endpoint verbatim (no framing) — on a mux link
  // they would need wrapping, so ASCII mode is a dual-link convenience only.
  if (d->muxed || !d->hal->link_write) return;
  d->hal->link_write(d->ctx, (const uint8_t *)s, (uint16_t)strlen(s));
}
static void skrit__ascii_line(skrit_dev *d) {
  const skrit_hal *h = d->hal;
  if (skrit__streq(d->line, "PING")) {
    skrit__ascii_write(d, "PONG\r\n");
  } else if (skrit__streq(d->line, "ID")) {
    skrit__ascii_write(d, h->name ? h->name : "Duta");
    skrit__ascii_write(d, "\r\n");
  } else if (skrit__streq(d->line, "STATUS")) {
    for (uint8_t i = 0; i < h->n_outputs && i < 8; i++) {
      uint8_t t = SKRIT_CTRL_IO; const char *nm = "";
      if (h->out_desc) h->out_desc(d->ctx, i, &t, &nm);
      skrit__ascii_write(d, nm);
      skrit__ascii_write(d, (h->out_get && h->out_get(d->ctx, i)) ? "=1 " : "=0 ");
    }
    skrit__ascii_write(d, "\r\n");
  } else if (skrit__streq(d->line, "HELP")) {
    skrit__ascii_write(d, "CMDS: PING, ID, STATUS, HELP (binary CMD port for control)\r\n");
  } else {
    skrit__ascii_write(d, "ERR\r\n");
  }
}
#endif

// ===========================================================================
// receive state machine — feed every byte that arrives on the CMD/mux endpoint
// ===========================================================================
static void skrit__frame_complete(skrit_dev *d) {
  if (d->rx_len == 0) return;
  uint8_t dec[SKRIT_RX_CAP];
  size_t dn = skrit_cobs_decode(d->rx, d->rx_len, dec);
  d->rx_len = 0;
  if (dn < 1) return;
  if (d->muxed) {
    uint8_t ch = dec[0];
    if (ch == SKRIT_MUX_DATA) {
      // Drop host->target console writes while a network session is unauthed.
      if (!skrit__gated(d) && d->hal->data_write && dn > 1)
        d->hal->data_write(d->ctx, dec + 1, (uint16_t)(dn - 1));
    } else if (ch == SKRIT_MUX_CMD) {
      skrit__on_frame(d, dec + 1, (uint16_t)(dn - 1));
    }
  } else {
    skrit__on_frame(d, dec, (uint16_t)dn);
  }
}

static void skrit_dev_rx(skrit_dev *d, uint8_t b) {
  switch (d->mode) {
  case 0: // idle
    if (b == 0x00) { d->mode = 2; d->rx_len = 0; }
#if SKRIT_ENABLE_ASCII
    else if (!d->muxed && b >= 0x20) { d->mode = 1; d->line_len = 0; if (d->line_len < sizeof d->line - 1) {
        char c = (char)b; if (c >= 'a' && c <= 'z') c -= 32; d->line[d->line_len++] = c; } }
#endif
    break;
#if SKRIT_ENABLE_ASCII
  case 1: // ascii line (dual link only)
    if (b == '\n' || b == '\r') { d->line[d->line_len] = '\0'; skrit__ascii_line(d); d->mode = 0; }
    else if (b == 0x00) { d->mode = 2; d->rx_len = 0; }
    else if (d->line_len < sizeof d->line - 1) {
      char c = (char)b; if (c >= 'a' && c <= 'z') c -= 32; d->line[d->line_len++] = c;
    }
    break;
#endif
  default: // 2: binary/mux frame, terminated by 0x00
    if (b == 0x00) { skrit__frame_complete(d); d->mode = 0; }
    else if (d->rx_len < SKRIT_RX_CAP) d->rx[d->rx_len++] = b;
    else { d->mode = 0; d->rx_len = 0; } // overflow -> drop, resync on next 0x00
    break;
  }
}

static void skrit_dev_init(skrit_dev *d, const skrit_hal *hal, void *ctx, uint8_t muxed) {
  memset(d, 0, sizeof *d);
  d->hal = hal;
  d->ctx = ctx;
  d->muxed = muxed ? 1 : 0;
}

#ifdef __cplusplus
}
#endif

#endif // DUTA_SKRIT_DEVICE_H
