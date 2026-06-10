// ws.h: minimal RFC6455 WebSocket server codec (no deps).
// ============================================================================
// Just enough to carry the skrit-mux byte stream over WebSocket binary frames:
// the opening-handshake accept key (SHA-1 + base64) and frame decode/encode
// (server reads masked client frames, writes unmasked frames). Used by the host
// reference; the same shape ports to ESP32 (esp_http_server) or Zephyr.
// ============================================================================
#ifndef DUTA_WS_H
#define DUTA_WS_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// ---- SHA-1 (RFC 3174) ------------------------------------------------------
typedef struct {
  uint32_t h[5];
  uint64_t bits;
  uint8_t buf[64];
  size_t n;
} ws_sha1;

static void ws_sha1_block(ws_sha1 *c, const uint8_t *p) {
  uint32_t w[80];
  for (int i = 0; i < 16; i++)
    w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 |
           (uint32_t)p[i * 4 + 2] << 8 | (uint32_t)p[i * 4 + 3];
  for (int i = 16; i < 80; i++) {
    uint32_t v = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
    w[i] = (v << 1) | (v >> 31);
  }
  uint32_t a = c->h[0], b = c->h[1], d = c->h[2], e = c->h[3], f = c->h[4];
  for (int i = 0; i < 80; i++) {
    uint32_t k, t;
    if (i < 20) { t = (b & d) | (~b & e); k = 0x5A827999; }
    else if (i < 40) { t = b ^ d ^ e; k = 0x6ED9EBA1; }
    else if (i < 60) { t = (b & d) | (b & e) | (d & e); k = 0x8F1BBCDC; }
    else { t = b ^ d ^ e; k = 0xCA62C1D6; }
    uint32_t tmp = ((a << 5) | (a >> 27)) + t + f + k + w[i];
    f = e; e = d; d = (b << 30) | (b >> 2); b = a; a = tmp;
  }
  c->h[0] += a; c->h[1] += b; c->h[2] += d; c->h[3] += e; c->h[4] += f;
}

static void ws_sha1_init(ws_sha1 *c) {
  c->h[0] = 0x67452301; c->h[1] = 0xEFCDAB89; c->h[2] = 0x98BADCFE;
  c->h[3] = 0x10325476; c->h[4] = 0xC3D2E1F0;
  c->bits = 0; c->n = 0;
}
static void ws_sha1_update(ws_sha1 *c, const uint8_t *p, size_t len) {
  c->bits += (uint64_t)len * 8;
  while (len--) {
    c->buf[c->n++] = *p++;
    if (c->n == 64) { ws_sha1_block(c, c->buf); c->n = 0; }
  }
}
static void ws_sha1_final(ws_sha1 *c, uint8_t out[20]) {
  uint8_t pad = 0x80;
  uint64_t bits = c->bits;
  ws_sha1_update(c, &pad, 1);
  uint8_t z = 0;
  while (c->n != 56) ws_sha1_update(c, &z, 1);
  uint8_t lb[8];
  for (int i = 0; i < 8; i++) lb[i] = (uint8_t)(bits >> (56 - i * 8));
  ws_sha1_update(c, lb, 8);
  for (int i = 0; i < 5; i++) {
    out[i * 4] = (uint8_t)(c->h[i] >> 24);
    out[i * 4 + 1] = (uint8_t)(c->h[i] >> 16);
    out[i * 4 + 2] = (uint8_t)(c->h[i] >> 8);
    out[i * 4 + 3] = (uint8_t)(c->h[i]);
  }
}

// ---- base64 encode ---------------------------------------------------------
static size_t ws_b64(const uint8_t *in, size_t n, char *out) {
  static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t)in[i] << 16;
    if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
    if (i + 2 < n) v |= in[i + 2];
    out[o++] = T[(v >> 18) & 63];
    out[o++] = T[(v >> 12) & 63];
    out[o++] = (i + 1 < n) ? T[(v >> 6) & 63] : '=';
    out[o++] = (i + 2 < n) ? T[v & 63] : '=';
  }
  out[o] = '\0';
  return o;
}

// Compute the Sec-WebSocket-Accept value for a client key. `out` >= 32 bytes.
static void ws_accept_key(const char *client_key, char *out) {
  static const char *MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  ws_sha1 c;
  ws_sha1_init(&c);
  ws_sha1_update(&c, (const uint8_t *)client_key, strlen(client_key));
  ws_sha1_update(&c, (const uint8_t *)MAGIC, strlen(MAGIC));
  uint8_t digest[20];
  ws_sha1_final(&c, digest);
  ws_b64(digest, 20, out);
}

// ---- frame codec -----------------------------------------------------------
enum { WS_OP_CONT = 0x0, WS_OP_TEXT = 0x1, WS_OP_BIN = 0x2, WS_OP_CLOSE = 0x8, WS_OP_PING = 0x9, WS_OP_PONG = 0xA };

// Decode one frame from `in` (len bytes). On success returns the total bytes
// consumed, writes the unmasked payload to `out` (cap `out_cap`), sets *out_len
// and *opcode. Returns 0 if the buffer doesn't yet hold a full frame, -1 on error.
static long ws_frame_decode(const uint8_t *in, size_t len, uint8_t *out, size_t out_cap,
                            size_t *out_len, uint8_t *opcode) {
  if (len < 2) return 0;
  *opcode = in[0] & 0x0F;
  uint8_t masked = in[1] & 0x80;
  uint64_t plen = in[1] & 0x7F;
  size_t hdr = 2;
  if (plen == 126) {
    if (len < 4) return 0;
    plen = (uint64_t)in[2] << 8 | in[3];
    hdr = 4;
  } else if (plen == 127) {
    if (len < 10) return 0;
    plen = 0;
    for (int i = 0; i < 8; i++) plen = plen << 8 | in[2 + i];
    hdr = 10;
  }
  uint8_t mask[4] = {0, 0, 0, 0};
  if (masked) {
    if (len < hdr + 4) return 0;
    memcpy(mask, in + hdr, 4);
    hdr += 4;
  }
  if (len < hdr + plen) return 0; // wait for the rest
  if (plen > out_cap) return -1;
  for (uint64_t i = 0; i < plen; i++) out[i] = in[hdr + i] ^ mask[i & 3];
  *out_len = (size_t)plen;
  return (long)(hdr + plen);
}

// Encode an (unmasked, server->client) frame with `opcode` into `out`
// (cap must hold len + 10). Returns the encoded length.
static size_t ws_frame_encode(uint8_t opcode, const uint8_t *payload, size_t len, uint8_t *out) {
  size_t o = 0;
  out[o++] = (uint8_t)(0x80 | (opcode & 0x0F)); // FIN + opcode
  if (len < 126) {
    out[o++] = (uint8_t)len;
  } else if (len < 0x10000) {
    out[o++] = 126;
    out[o++] = (uint8_t)(len >> 8);
    out[o++] = (uint8_t)len;
  } else {
    out[o++] = 127;
    for (int i = 0; i < 8; i++) out[o++] = (uint8_t)(len >> (56 - i * 8));
  }
  if (len) memcpy(out + o, payload, len);
  return o + len;
}

#endif // DUTA_WS_H
