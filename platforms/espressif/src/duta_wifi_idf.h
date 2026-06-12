// duta_wifi_idf.h — WiFi + WebSocket bridge + captive portal, pure ESP-IDF.
// ============================================================================
// The IDF-native twin of duta_wifi.h: same network transport (a WebSocket server
// on ws://<ip>:9555/ carrying the auth-gated skrit-mux stream as a SECOND
// skrit_dev) and the same two provisioning paths (CFG_SET WIFI_* over a CMD link,
// or a "Duta-XXXX" captive-portal AP). Built on esp_wifi + raw lwip sockets, and
// it reuses the shared ws_codec.h framing — single-threaded, pumped from the main
// loop exactly like the Arduino version. mDNS (_skrit._tcp) is the one piece
// deferred (managed-component dep); connect by the IP that WIFI_STATUS reports.
// ============================================================================
#ifndef DUTA_WIFI_IDF_H
#define DUTA_WIFI_IDF_H
#ifdef DUTA_PURE_IDF

#include <fcntl.h>
#include <string.h>

#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "nvs.h"

#include "skrit_device.h"
#include "ws_codec.h"

#ifndef DUTA_WS_RX_CAP
#define DUTA_WS_RX_CAP 768
#endif

static uint8_t wifi_state = SKRIT_WIFI_OFF;
static char wifi_ssid[33], wifi_pass[65];
static char wifi_ap_name[32];
static char wifi_ip[20];
static uint32_t wifi_join_t0;
static volatile bool wifi_got_ip;

// ---- the WebSocket-session skrit_dev ----------------------------------------
static skrit_dev ws_dev;
static skrit_hal WS_HAL;          // copy of the board HAL with link/auth overrides
static int ws_listen = -1;        // listening socket (port 9555)
static int ws_fd = -1;            // active WS client
static int ws_pending_fd = -1;    // accepted, not yet handshaken
static uint32_t ws_pending_t0;
static bool ws_handshaken;
static uint8_t ws_rx[DUTA_WS_RX_CAP];
static size_t ws_rx_n;
// captive portal sockets
static int portal_http = -1, portal_dns = -1;

static inline uint32_t wms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static int sock_nonblock(int fd) { return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK); }

// ---- NVS creds (namespace "duta": wssid / wpass / pw) -----------------------
static void wifi_load_creds(void) {
  wifi_ssid[0] = wifi_pass[0] = 0;
  nvs_handle_t h;
  if (nvs_open("duta", NVS_READONLY, &h) != ESP_OK) return;
  size_t n = sizeof wifi_ssid;
  nvs_get_str(h, "wssid", wifi_ssid, &n);
  n = sizeof wifi_pass;
  nvs_get_str(h, "wpass", wifi_pass, &n);
  nvs_close(h);
}
static void wifi_save_creds(void) {
  nvs_handle_t h;
  if (nvs_open("duta", NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_str(h, "wssid", wifi_ssid);
  nvs_set_str(h, "wpass", wifi_pass);
  nvs_commit(h);
  nvs_close(h);
}

// ---- session password (default "duta"; AUTH_SET changes it) -----------------
static uint8_t ws_auth_check(void *c, const char *pw, uint8_t n) {
  (void)c;
  char stored[SKRIT_PASSWORD_MAX + 1] = SKRIT_DEFAULT_PASSWORD;
  nvs_handle_t h;
  if (nvs_open("duta", NVS_READONLY, &h) == ESP_OK) {
    size_t sn = sizeof stored;
    nvs_get_str(h, "pw", stored, &sn);
    nvs_close(h);
  }
  return strlen(stored) == n && memcmp(stored, pw, n) == 0;
}
static uint8_t ws_auth_set(void *c, const char *pw, uint8_t n) {
  (void)c;
  if (n > SKRIT_PASSWORD_MAX) return 0;
  char buf[SKRIT_PASSWORD_MAX + 1];
  memcpy(buf, pw, n);
  buf[n] = 0;
  nvs_handle_t h;
  if (nvs_open("duta", NVS_READWRITE, &h) != ESP_OK) return 0;
  nvs_set_str(h, "pw", buf);
  nvs_commit(h);
  nvs_close(h);
  return 1;
}
static uint8_t ws_auth_is_default(void *c) {
  return ws_auth_check(c, SKRIT_DEFAULT_PASSWORD, (uint8_t)strlen(SKRIT_DEFAULT_PASSWORD));
}

// ---- WS link: each link_write chunk goes out as one binary frame ------------
static void ws_link_write(void *c, const uint8_t *p, uint16_t n) {
  (void)c;
  if (ws_fd < 0 || !ws_handshaken) return;
  uint8_t frame[DUTA_WS_RX_CAP + 10];
  if ((size_t)n + 10 > sizeof frame) return;
  size_t en = ws_frame_encode(WS_OP_BIN, p, n, frame);
  send(ws_fd, frame, en, MSG_DONTWAIT);
}
static void ws_drop_client(void) {
  if (ws_fd >= 0) close(ws_fd);
  ws_fd = -1;
  ws_handshaken = false;
  ws_rx_n = 0;
  skrit_dev_reset_auth(&ws_dev); // next session must AUTH again
}

// ---- esp_wifi events --------------------------------------------------------
static void wifi_evt(void *arg, esp_event_base_t base, int32_t id, void *data) {
  (void)arg;
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    wifi_got_ip = false;
    if (wifi_state == SKRIT_WIFI_CONNECTED) esp_wifi_connect(); // transient drop: rejoin
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
    esp_ip4addr_ntoa(&e->ip_info.ip, wifi_ip, sizeof wifi_ip);
    wifi_got_ip = true;
  }
}

static void wifi_join(void) {
  esp_wifi_stop();
  esp_wifi_set_mode(WIFI_MODE_STA);
  wifi_config_t wc = {0};
  strncpy((char *)wc.sta.ssid, wifi_ssid, sizeof wc.sta.ssid);
  strncpy((char *)wc.sta.password, wifi_pass, sizeof wc.sta.password);
  esp_wifi_set_config(WIFI_IF_STA, &wc);
  esp_wifi_start(); // STA_START event triggers esp_wifi_connect()
  wifi_join_t0 = wms();
  wifi_got_ip = false;
  wifi_state = SKRIT_WIFI_CONNECTING;
}

// ---- captive portal (AP + HTTP page + DNS catch-all) ------------------------
static esp_ip4_addr_t portal_ip;
static void portal_start(void) {
  esp_wifi_stop();
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  snprintf(wifi_ap_name, sizeof wifi_ap_name, "Duta-%02X%02X", mac[4], mac[5]);
  esp_wifi_set_mode(WIFI_MODE_AP);
  wifi_config_t ap = {0};
  strncpy((char *)ap.ap.ssid, wifi_ap_name, sizeof ap.ap.ssid);
  ap.ap.ssid_len = strlen(wifi_ap_name);
  ap.ap.max_connection = 4;
  ap.ap.authmode = WIFI_AUTH_OPEN;
  esp_wifi_set_config(WIFI_IF_AP, &ap);
  esp_wifi_start();
  IP4_ADDR(&portal_ip, 192, 168, 4, 1);

  // HTTP server on :80
  portal_http = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(portal_http, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_port = htons(80);
  a.sin_addr.s_addr = INADDR_ANY;
  bind(portal_http, (struct sockaddr *)&a, sizeof a);
  listen(portal_http, 2);
  sock_nonblock(portal_http);
  // DNS catch-all on :53
  portal_dns = socket(AF_INET, SOCK_DGRAM, 0);
  a.sin_port = htons(53);
  bind(portal_dns, (struct sockaddr *)&a, sizeof a);
  sock_nonblock(portal_dns);
  wifi_state = SKRIT_WIFI_PORTAL;
}

static const char PORTAL_HTML[] =
    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Duta WiFi setup</title><body style='font-family:sans-serif;max-width:22rem;margin:2rem auto'>"
    "<h2>Duta WiFi setup</h2><form method=POST action=/save>"
    "<p>SSID<br><input name=ssid required style=width:100%></p>"
    "<p>Password<br><input name=pass type=password style=width:100%></p>"
    "<button style='width:100%;padding:.6rem'>Save &amp; connect</button></form>"
    "<p style=color:#666>The Duta reboots and joins your network; find its IP in Sutra "
    "(Network status) and connect via ws://&lt;ip&gt;:9555/.</p></body>";

static int url_field(const char *body, const char *key, char *out, int cap) {
  char pat[24];
  int pn = snprintf(pat, sizeof pat, "%s=", key);
  const char *p = strstr(body, pat);
  if (!p) { out[0] = 0; return 0; }
  p += pn;
  int n = 0;
  while (*p && *p != '&' && *p != ' ' && *p != '\r' && n < cap - 1) {
    char c = *p++;
    if (c == '+') c = ' ';
    if (c == '%' && p[0] && p[1]) { // %XX
      int hi = p[0] <= '9' ? p[0] - '0' : (p[0] | 32) - 'a' + 10;
      int lo = p[1] <= '9' ? p[1] - '0' : (p[1] | 32) - 'a' + 10;
      c = (char)((hi << 4) | lo);
      p += 2;
    }
    out[n++] = c;
  }
  out[n] = 0;
  return n;
}

static void portal_pump(void) {
  // DNS: answer every query with our AP IP (captive redirect)
  if (portal_dns >= 0) {
    uint8_t q[256];
    struct sockaddr_in from;
    socklen_t fl = sizeof from;
    int n = recvfrom(portal_dns, q, sizeof q, MSG_DONTWAIT, (struct sockaddr *)&from, &fl);
    if (n >= 12) {
      uint8_t r[512];
      int rn = n > 480 ? 480 : n;
      memcpy(r, q, rn);
      r[2] = 0x81; r[3] = 0x80;             // response, recursion available
      r[6] = 0; r[7] = 1;                    // 1 answer
      // answer: name pointer 0xC00C, type A, class IN, ttl 60, len 4, the IP
      uint8_t ans[] = {0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4,
                       (uint8_t)(portal_ip.addr), (uint8_t)(portal_ip.addr >> 8),
                       (uint8_t)(portal_ip.addr >> 16), (uint8_t)(portal_ip.addr >> 24)};
      memcpy(r + rn, ans, sizeof ans);
      sendto(portal_dns, r, rn + sizeof ans, 0, (struct sockaddr *)&from, fl);
    }
  }
  // HTTP: serve the page / handle the POST
  if (portal_http < 0) return;
  int c = accept(portal_http, NULL, NULL);
  if (c < 0) return;
  sock_nonblock(c);
  char req[1024];
  int total = 0, r;
  uint32_t t0 = wms();
  while (total < (int)sizeof req - 1 && wms() - t0 < 800) {
    r = recv(c, req + total, sizeof req - 1 - total, MSG_DONTWAIT);
    if (r > 0) { total += r; if (strstr(req, "\r\n\r\n") && (req[0] != 'P' || strchr(req, '='))) break; }
    else vTaskDelay(1);
  }
  req[total > 0 ? total : 0] = 0;
  if (strncmp(req, "POST /save", 10) == 0) {
    const char *body = strstr(req, "\r\n\r\n");
    body = body ? body + 4 : req;
    url_field(body, "ssid", wifi_ssid, sizeof wifi_ssid);
    url_field(body, "pass", wifi_pass, sizeof wifi_pass);
    wifi_save_creds();
    const char *ok = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n"
                     "<body style='font-family:sans-serif'><h2>Saved.</h2><p>Rebooting and joining.</p>";
    send(c, ok, strlen(ok), 0);
    close(c);
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_restart();
  } else {
    send(c, PORTAL_HTML, sizeof PORTAL_HTML - 1, 0);
  }
  close(c);
}

// ---- CFG_GET/SET (wire from the board cfg callbacks) ------------------------
static int16_t duta_wifi_cfg_get(uint8_t key, uint8_t *out, uint8_t cap) {
  switch (key) {
  case SKRIT_CFG_WIFI_SSID: {
    uint8_t n = (uint8_t)strlen(wifi_ssid);
    if (n > cap) return -1;
    memcpy(out, wifi_ssid, n);
    return n;
  }
  case SKRIT_CFG_WIFI_PASS:
    if (!wifi_pass[0]) return 0;
    if (cap < 1) return -1;
    out[0] = '*';
    return 1;
  case SKRIT_CFG_WIFI_STATUS: {
    if (cap < 1) return -1;
    out[0] = wifi_state;
    const char *detail = "";
    if (wifi_state == SKRIT_WIFI_CONNECTED) detail = wifi_ip;
    else if (wifi_state == SKRIT_WIFI_PORTAL) detail = wifi_ap_name;
    else if (wifi_state != SKRIT_WIFI_OFF) detail = wifi_ssid;
    uint8_t n = (uint8_t)strlen(detail);
    if (1 + n > cap) n = cap - 1;
    memcpy(out + 1, detail, n);
    return (int16_t)(1 + n);
  }
  default:
    return -1;
  }
}
static uint8_t duta_wifi_cfg_set(uint8_t key, const uint8_t *val, uint8_t len) {
  switch (key) {
  case SKRIT_CFG_WIFI_SSID:
    if (len >= sizeof wifi_ssid) return SKRIT_ST_BADARGS;
    memcpy(wifi_ssid, val, len);
    wifi_ssid[len] = 0;
    wifi_save_creds();
    if (len) wifi_join();
    else { esp_wifi_disconnect(); wifi_state = SKRIT_WIFI_OFF; }
    return SKRIT_ST_OK;
  case SKRIT_CFG_WIFI_PASS:
    if (len >= sizeof wifi_pass) return SKRIT_ST_BADARGS;
    memcpy(wifi_pass, val, len);
    wifi_pass[len] = 0;
    wifi_save_creds();
    if (wifi_ssid[0]) wifi_join();
    return SKRIT_ST_OK;
  default:
    return SKRIT_ST_NOTFOUND;
  }
}

// ---- WS server pump (mirrors duta_wifi.h ws_pump, on lwip sockets) ----------
static void ws_pump(void) {
  if (ws_listen < 0) {
    ws_listen = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(ws_listen, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(SKRIT_WS_PORT);
    a.sin_addr.s_addr = INADDR_ANY;
    bind(ws_listen, (struct sockaddr *)&a, sizeof a);
    listen(ws_listen, 2);
    sock_nonblock(ws_listen);
  }
  int inc = accept(ws_listen, NULL, NULL);
  if (inc >= 0) { // a newcomer waits in PENDING until it proves itself with bytes
    if (ws_pending_fd >= 0) close(ws_pending_fd);
    sock_nonblock(inc);
    ws_pending_fd = inc;
    ws_pending_t0 = wms();
  }
  if (ws_pending_fd >= 0) {
    uint8_t peek;
    int r = recv(ws_pending_fd, &peek, 1, MSG_PEEK | MSG_DONTWAIT);
    if (r > 0) { // real client: take over the session (always resets auth + buffers)
      ws_drop_client();
      ws_fd = ws_pending_fd;
      ws_pending_fd = -1;
    } else if (r == 0 || wms() - ws_pending_t0 > 3000) {
      close(ws_pending_fd);
      ws_pending_fd = -1;
    }
  }
  if (ws_fd < 0) return;

  uint8_t b;
  while (ws_rx_n < sizeof ws_rx) {
    int r = recv(ws_fd, &b, 1, MSG_DONTWAIT);
    if (r <= 0) { if (r == 0) ws_drop_client(); break; }
    ws_rx[ws_rx_n++] = b;
    if (!ws_handshaken) {
      if (ws_rx_n >= 4 && memcmp(ws_rx + ws_rx_n - 4, "\r\n\r\n", 4) == 0) {
        ws_rx[ws_rx_n < sizeof ws_rx ? ws_rx_n : sizeof ws_rx - 1] = 0;
        const char *KEY = "sec-websocket-key:";
        char *k = NULL;
        for (size_t i = 0; i + 18 < ws_rx_n && !k; i++) {
          size_t j = 0;
          while (j < 18 && (int)(ws_rx[i + j] | 0x20) == KEY[j]) j++;
          if (j == 18) k = (char *)ws_rx + i + 18;
        }
        if (!k) { ws_drop_client(); return; }
        while (*k == ' ') k++;
        char ckey[64] = {0};
        size_t kn = 0;
        while (k[kn] && k[kn] != '\r' && kn < sizeof ckey - 1) { ckey[kn] = k[kn]; kn++; }
        char accept_key[40];
        ws_accept_key(ckey, accept_key);
        char resp[160];
        int rn = snprintf(resp, sizeof resp,
                          "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                          "Connection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept_key);
        send(ws_fd, resp, rn, 0);
        ws_handshaken = true;
        ws_rx_n = 0;
      }
      continue;
    }
    for (;;) { // decode complete frames
      uint8_t payload[DUTA_WS_RX_CAP];
      size_t plen = 0;
      uint8_t op = 0;
      long used = ws_frame_decode(ws_rx, ws_rx_n, payload, sizeof payload, &plen, &op);
      if (used == 0) break;
      if (used < 0) { ws_drop_client(); return; }
      memmove(ws_rx, ws_rx + used, ws_rx_n - used);
      ws_rx_n -= (size_t)used;
      if (op == WS_OP_CLOSE) { ws_drop_client(); return; }
      if (op == WS_OP_PING) {
        uint8_t pong[DUTA_WS_RX_CAP + 10];
        size_t en = ws_frame_encode(WS_OP_PONG, payload, plen, pong);
        send(ws_fd, pong, en, MSG_DONTWAIT);
        continue;
      }
      for (size_t i = 0; i < plen; i++) skrit_dev_rx(&ws_dev, payload[i]);
    }
  }
  if (ws_rx_n >= sizeof ws_rx) ws_drop_client(); // oversized — resync
}

// ---- public: init + loop ----------------------------------------------------
static void duta_wifi_init(const skrit_hal *base) {
  WS_HAL = *base;
  WS_HAL.link_write = ws_link_write;
  WS_HAL.auth_required = 1; // network transport — always gated
  WS_HAL.auth_check = ws_auth_check;
  WS_HAL.auth_set = ws_auth_set;
  WS_HAL.auth_is_default = ws_auth_is_default;
  skrit_dev_init(&ws_dev, &WS_HAL, NULL, /*muxed*/ 1);

  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();
  esp_netif_create_default_wifi_ap();
  wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&ic);
  esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_evt, NULL, NULL);
  esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_evt, NULL, NULL);
  esp_wifi_set_storage(WIFI_STORAGE_RAM);

  wifi_load_creds();
  if (wifi_ssid[0]) wifi_join();
  else portal_start();
}

// Feed the WS session a DATA record / console bytes (called by the board's tee).
static inline void duta_wifi_feed(const uint8_t *p, uint16_t n) { skrit_dev_feed_data(&ws_dev, p, n); }

static void duta_wifi_loop(void) {
  switch (wifi_state) {
  case SKRIT_WIFI_PORTAL:
    portal_pump();
    break;
  case SKRIT_WIFI_CONNECTING:
    if (wifi_got_ip) wifi_state = SKRIT_WIFI_CONNECTED;
    else if (wms() - wifi_join_t0 > 20000) { wifi_state = SKRIT_WIFI_FAILED; portal_start(); }
    break;
  case SKRIT_WIFI_CONNECTED:
    if (!wifi_got_ip) { ws_drop_client(); if (ws_listen >= 0) { close(ws_listen); ws_listen = -1; } wifi_state = SKRIT_WIFI_CONNECTING; wifi_join_t0 = wms(); break; }
    ws_pump();
    break;
  default:
    break;
  }
}

#endif // DUTA_PURE_IDF
#endif // DUTA_WIFI_IDF_H
