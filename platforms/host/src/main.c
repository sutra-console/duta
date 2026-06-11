// Duta host/native reference: carries the skrit-mux stream over a WebSocket.
// ============================================================================
// The hardware-free CI build target and the seed of the network transport (an
// ESP32 "WiFi bridge" answers the same way). Built on the shared core
// (../../common/skrit_device.h): muxed, auth-gated (it's a network transport),
// no real DATA console. The WS plumbing (handshake + framing) is in ws.h.
//
//   cmake -B build && cmake --build build
//   ./duta-host [port]            (default 9555)
//   connect:  ws://127.0.0.1:9555/   then AUTH "duta"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "skrit_device.h"
#include "ws_codec.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <unistd.h>
#endif

// ---- in-memory device state ------------------------------------------------
static uint8_t g_out[3];
static const char *const OUT_NAME[3] = {"Relay 1", "Relay 2", "Aux LED"};
static char g_pw[SKRIT_PASSWORD_MAX + 1] = SKRIT_DEFAULT_PASSWORD;
static uint8_t g_pwlen = (uint8_t)(sizeof(SKRIT_DEFAULT_PASSWORD) - 1);
static uint8_t g_default = 1;
static int g_client = -1; // the connected WS socket (for link_write)

// ---- HAL -------------------------------------------------------------------
// link_write: wrap the (already mux-framed) bytes in a WebSocket binary frame.
static void hal_link_write(void *c, const uint8_t *p, uint16_t n) {
  (void)c;
  if (g_client < 0) return;
  uint8_t frame[64 + 16];
  uint8_t *f = ((size_t)n + 10 <= sizeof frame) ? frame : malloc((size_t)n + 10);
  if (!f) return;
  size_t fl = ws_frame_encode(WS_OP_BIN, p, n, f);
  send(g_client, (const char *)f, (int)fl, 0);
  if (f != frame) free(f);
}

static void hal_out_set(void *c, uint8_t idx, uint8_t on) {
  (void)c;
  if (idx < 3) g_out[idx] = on ? 1 : 0;
}
static uint8_t hal_out_get(void *c, uint8_t idx) {
  (void)c;
  return idx < 3 ? g_out[idx] : 0;
}
static void hal_out_desc(void *c, uint8_t idx, uint8_t *type, const char **name) {
  (void)c;
  if (idx >= 3) return;
  *type = SKRIT_CTRL_IO;
  *name = OUT_NAME[idx];
}

static uint8_t hal_auth_check(void *c, const char *pw, uint8_t n) {
  (void)c;
  return n == g_pwlen && memcmp(pw, g_pw, n) == 0;
}
static uint8_t hal_auth_set(void *c, const char *pw, uint8_t n) {
  (void)c;
  if (n > SKRIT_PASSWORD_MAX) return 0;
  memcpy(g_pw, pw, n);
  g_pwlen = n;
  g_default = 0;
  return 1;
}
static uint8_t hal_auth_is_default(void *c) {
  (void)c;
  return g_default;
}

static uint32_t hal_millis(void *c) {
  (void)c;
  return (uint32_t)(clock() * 1000 / CLOCKS_PER_SEC);
}

static skrit_hal HAL = {
    .name = "Duta host",
    .fw_ver = 0x0004,
    .caps = SKRIT_CAP_MUX,
    .macro_tier = SKRIT_TIER_INTERACTIVE,
    .n_outputs = 3,
    .link_write = hal_link_write,
    .out_set = hal_out_set,
    .out_get = hal_out_get,
    .out_desc = hal_out_desc,
    .millis = hal_millis,
    .auth_required = 1, // network transport: gate behind AUTH
    .auth_check = hal_auth_check,
    .auth_set = hal_auth_set,
    .auth_is_default = hal_auth_is_default,
};

static skrit_dev dev;

// case-insensitive substring search (HTTP header names aren't case-fixed).
static const char *ci_find(const char *hay, const char *needle) {
  size_t nl = strlen(needle);
  for (const char *p = hay; *p; p++) {
    size_t i = 0;
    while (i < nl && p[i] &&
           (p[i] == needle[i] ||
            (p[i] | 0x20) == (needle[i] | 0x20))) // fold ASCII case
      i++;
    if (i == nl) return p;
  }
  return NULL;
}

// ---- WebSocket opening handshake -------------------------------------------
// Read the HTTP upgrade request, reply 101 with the computed accept key.
static int ws_handshake(int fd) {
  char req[2048];
  int total = 0;
  // Read until the end of the HTTP headers (\r\n\r\n) or the buffer fills.
  while (total < (int)sizeof req - 1) {
    int n = recv(fd, req + total, (int)sizeof req - 1 - total, 0);
    if (n <= 0) return -1;
    total += n;
    req[total] = '\0';
    if (strstr(req, "\r\n\r\n")) break;
  }
  const char *k = ci_find(req, "Sec-WebSocket-Key:");
  if (!k) return -1;
  k += strlen("Sec-WebSocket-Key:");
  while (*k == ' ') k++;
  char key[128];
  size_t i = 0;
  while (*k && *k != '\r' && *k != '\n' && i < sizeof key - 1) key[i++] = *k++;
  key[i] = '\0';
  char accept[40];
  ws_accept_key(key, accept);
  char resp[256];
  int rl = snprintf(resp, sizeof resp,
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: %s\r\n\r\n",
                    accept);
  return send(fd, resp, rl, 0) == rl ? 0 : -1;
}

static void serve(int fd) {
  g_client = fd;
  skrit_dev_reset_auth(&dev); // each connection starts unauthenticated

  uint8_t acc[8192];
  size_t acc_len = 0;
  uint8_t buf[2048];
  int n;
  while ((n = recv(fd, (char *)buf, sizeof buf, 0)) > 0) {
    if (acc_len + (size_t)n > sizeof acc) acc_len = 0; // overflow guard
    memcpy(acc + acc_len, buf, (size_t)n);
    acc_len += (size_t)n;

    // decode as many complete frames as we have
    size_t off = 0;
    for (;;) {
      uint8_t payload[4096];
      size_t plen;
      uint8_t op;
      long used = ws_frame_decode(acc + off, acc_len - off, payload, sizeof payload, &plen, &op);
      if (used == 0) break;                          // need more bytes
      if (used < 0) { acc_len = 0; off = 0; break; } // bad frame -> resync
      off += (size_t)used;
      if (op == WS_OP_BIN || op == WS_OP_CONT) {
        for (size_t i = 0; i < plen; i++) skrit_dev_rx(&dev, payload[i]);
      } else if (op == WS_OP_PING) {
        uint8_t pong[16];
        size_t pl = ws_frame_encode(WS_OP_PONG, payload, plen < 8 ? plen : 8, pong);
        send(fd, (const char *)pong, (int)pl, 0);
      } else if (op == WS_OP_CLOSE) {
        n = 0;
        break;
      }
    }
    memmove(acc, acc + off, acc_len - off); // keep the partial remainder
    acc_len -= off;
    if (n == 0) break;
  }
  g_client = -1;
  close(fd);
}

int main(int argc, char **argv) {
  int port = argc > 1 ? atoi(argv[1]) : 9555;
#ifdef _WIN32
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
  skrit_dev_init(&dev, &HAL, NULL, /*muxed*/ 1);

  int s = (int)socket(AF_INET, SOCK_STREAM, 0);
  int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof yes);
  struct sockaddr_in a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = INADDR_ANY;
  a.sin_port = htons((unsigned short)port);
  if (bind(s, (struct sockaddr *)&a, sizeof a) < 0) {
    perror("bind");
    return 1;
  }
  listen(s, 1);
  printf("Duta host (WebSocket) on ws://127.0.0.1:%d/  (AUTH \"%s\")\n", port, SKRIT_DEFAULT_PASSWORD);
  fflush(stdout);

  for (;;) {
    int c = (int)accept(s, NULL, NULL);
    if (c < 0) continue;
    if (ws_handshake(c) == 0) serve(c);
    else close(c);
  }
}
