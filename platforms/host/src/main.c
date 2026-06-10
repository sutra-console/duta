// Duta host/native reference — speaks the CMD protocol over a TCP socket.
//
// It is both the hardware-free CI build target and the seed of the future TCP
// transport (an ESP32 "bridge mode" answers the same way). POSIX sockets;
// Winsock shim for Windows.
//
//   cmake -B build && cmake --build build
//   ./duta-host [port]     (default 9555)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#define close closesocket
#else
#include <arpa/inet.h>
#include <unistd.h>
#endif

#define DEV_NAME "Duta host"
#define N_OUT 3
static const char *const OUT_NAME[N_OUT] = {"Relay 1", "Relay 2", "Aux LED"};
static const uint8_t OUT_TYPE[N_OUT] = {SKRIT_CTRL_RELAY, SKRIT_CTRL_RELAY, SKRIT_CTRL_LED};
static uint8_t out_state = 0;

// frame + send a response: 0x00 COBS(TYPE SEQ LEN BODY CRC8) 0x00
static void send_frame(int fd, uint8_t typ, uint8_t seq, const uint8_t *body, uint8_t len) {
  uint8_t raw[3 + 255 + 1];
  raw[0] = typ;
  raw[1] = seq;
  raw[2] = len;
  memcpy(raw + 3, body, len);
  raw[3 + len] = skrit_crc8(raw, (size_t)(3 + len));

  uint8_t cobs[sizeof(raw) + 8];
  size_t n = skrit_cobs_encode(raw, (size_t)(3 + len + 1), cobs);

  uint8_t out[sizeof(cobs) + 2];
  out[0] = 0x00;
  memcpy(out + 1, cobs, n);
  out[1 + n] = 0x00;
  send(fd, (const char *)out, (int)(n + 2), 0);
}

static void handle(int fd, const uint8_t *p, size_t n) {
  if (n < 4)
    return;
  uint8_t typ = p[0], seq = p[1], len = p[2];
  if ((size_t)(3 + len + 1) > n)
    return;
  if (skrit_crc8(p, (size_t)(3 + len)) != p[3 + len])
    return;
  const uint8_t *b = p + 3;

  uint8_t r[64];
  uint8_t rl = 0;
  switch (typ) {
  case SKRIT_PING:
    r[rl++] = SKRIT_ST_OK;
    break;
  case SKRIT_INFO:
    r[rl++] = SKRIT_ST_OK;
    r[rl++] = 1; // fw lo
    r[rl++] = 0; // fw hi
    r[rl++] = 0; // caps
    r[rl++] = N_OUT;
    r[rl++] = 0; // eeprom KB
    r[rl++] = 1; // proto
    break;
  case SKRIT_DEVICE_NAME:
    r[rl++] = SKRIT_ST_OK;
    for (const char *s = DEV_NAME; *s; s++)
      r[rl++] = (uint8_t)*s;
    break;
  case SKRIT_OUT_GET:
    r[rl++] = SKRIT_ST_OK;
    r[rl++] = out_state;
    break;
  case SKRIT_OUT_SET:
    if (len >= 2 && b[0] < N_OUT) {
      if (b[1])
        out_state |= (uint8_t)(1u << b[0]);
      else
        out_state &= (uint8_t)~(1u << b[0]);
      r[rl++] = SKRIT_ST_OK;
      r[rl++] = out_state;
    } else
      r[rl++] = SKRIT_ST_BADARGS;
    break;
  case SKRIT_OUT_TOGGLE:
    if (len >= 1 && b[0] < N_OUT) {
      out_state ^= (uint8_t)(1u << b[0]);
      r[rl++] = SKRIT_ST_OK;
      r[rl++] = out_state;
    } else
      r[rl++] = SKRIT_ST_BADARGS;
    break;
  case SKRIT_OUT_DESC:
    if (len >= 1 && b[0] < N_OUT) {
      r[rl++] = SKRIT_ST_OK;
      r[rl++] = b[0];
      r[rl++] = OUT_TYPE[b[0]];
      for (const char *s = OUT_NAME[b[0]]; *s; s++)
        r[rl++] = (uint8_t)*s;
    } else
      r[rl++] = SKRIT_ST_BADARGS;
    break;
  default:
    r[rl++] = SKRIT_ST_BADMSG;
    break;
  }
  send_frame(fd, (uint8_t)(typ | SKRIT_RESP), seq, r, rl);
}

int main(int argc, char **argv) {
  int port = argc > 1 ? atoi(argv[1]) : 9555;
#ifdef _WIN32
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
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
  printf("Duta host reference listening on tcp/%d\n", port);
  fflush(stdout);

  for (;;) {
    int c = (int)accept(s, NULL, NULL);
    if (c < 0)
      continue;
    uint8_t buf[512], acc[600];
    size_t acc_len = 0;
    int n;
    while ((n = recv(c, (char *)buf, sizeof buf, 0)) > 0) {
      for (int i = 0; i < n; i++) {
        uint8_t byte = buf[i];
        if (byte == 0x00) {
          if (acc_len) {
            uint8_t dec[600];
            size_t dl = skrit_cobs_decode(acc, acc_len, dec);
            handle(c, dec, dl);
            acc_len = 0;
          }
        } else if (acc_len < sizeof acc) {
          acc[acc_len++] = byte;
        }
      }
    }
    close(c);
  }
}
