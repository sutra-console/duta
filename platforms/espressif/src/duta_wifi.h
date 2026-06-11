// duta_wifi.h — WiFi + WebSocket bridge + captive portal (espressif platform).
// ============================================================================
// Gives an ESP32 Duta the network transport: a WebSocket server on
// ws://<ip>:9555/ carrying the skrit-mux stream (RFC6455 binary frames, via the
// shared ws_codec.h), auth-gated like every network transport. The WS session
// is a SECOND skrit_dev sharing the board HAL — USB stays fully usable, and
// main.cpp tees the target console to both links via skrit_dev_feed_data().
//
// Two provisioning paths (see PROTOCOL.md "WiFi provisioning"):
//   * CFG_SET WIFI_SSID/WIFI_PASS over any CMD link (Sutra over USB) — saved to
//     NVS, the device joins, CFG_GET WIFI_STATUS reports the IP.
//   * Captive portal — with no stored credentials (or a failed join) the device
//     raises a "Duta-XXXX" AP with a DNS catch-all + config page.
//
// Include from main.cpp after skrit_device.h; call duta_wifi_init(&HAL) in
// setup() (after the HAL is fully populated) and duta_wifi_loop() from loop().
// ============================================================================
#ifndef DUTA_WIFI_H
#define DUTA_WIFI_H

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

#include "ws_codec.h"

#ifndef DUTA_WS_RX_CAP
#define DUTA_WS_RX_CAP 768 // one handshake / a few mux frames
#endif

static Preferences duta_wifi_prefs;
static uint8_t wifi_state = SKRIT_WIFI_OFF; // SKRIT_WIFI_*
static char wifi_ssid[33], wifi_pass[65];
static uint32_t wifi_join_t0;
static char wifi_ap_name[32];

// ---- the WebSocket-session skrit_dev ----------------------------------------
static skrit_dev ws_dev;
static skrit_hal WS_HAL; // copy of the board HAL with link/auth overrides
static WiFiServer ws_server(SKRIT_WS_PORT);
static WiFiClient ws_client;
static bool ws_handshaken = false;
static uint8_t ws_rx[DUTA_WS_RX_CAP];
static size_t ws_rx_n = 0;

// ---- NVS helpers (namespace "duta": wssid / wpass / pw) ---------------------
static void wifi_load_creds(void) {
  wifi_ssid[0] = wifi_pass[0] = 0;
  if (!duta_wifi_prefs.begin("duta", true)) return;
  duta_wifi_prefs.getString("wssid", wifi_ssid, sizeof wifi_ssid);
  duta_wifi_prefs.getString("wpass", wifi_pass, sizeof wifi_pass);
  duta_wifi_prefs.end();
}
static void wifi_save_creds(void) {
  if (!duta_wifi_prefs.begin("duta", false)) return;
  duta_wifi_prefs.putString("wssid", wifi_ssid);
  duta_wifi_prefs.putString("wpass", wifi_pass);
  duta_wifi_prefs.end();
}

// Session password for the network transport (default "duta"; AUTH_SET changes it).
static uint8_t ws_auth_check(void *, const char *pw, uint8_t n) {
  char stored[SKRIT_PASSWORD_MAX + 1] = SKRIT_DEFAULT_PASSWORD;
  if (duta_wifi_prefs.begin("duta", true)) {
    duta_wifi_prefs.getString("pw", stored, sizeof stored);
    duta_wifi_prefs.end();
  }
  return strlen(stored) == n && memcmp(stored, pw, n) == 0;
}
static uint8_t ws_auth_set(void *, const char *pw, uint8_t n) {
  if (n > SKRIT_PASSWORD_MAX || !duta_wifi_prefs.begin("duta", false)) return 0;
  char buf[SKRIT_PASSWORD_MAX + 1];
  memcpy(buf, pw, n);
  buf[n] = 0;
  duta_wifi_prefs.putString("pw", buf);
  duta_wifi_prefs.end();
  return 1;
}
static uint8_t ws_auth_is_default(void *) {
  return ws_auth_check(NULL, SKRIT_DEFAULT_PASSWORD, (uint8_t)strlen(SKRIT_DEFAULT_PASSWORD));
}

// ---- WS link: each link_write chunk goes out as one binary frame ------------
static void ws_link_write(void *, const uint8_t *p, uint16_t n) {
  if (!ws_client || !ws_handshaken) return;
  uint8_t frame[DUTA_WS_RX_CAP + 10];
  if ((size_t)n + 10 > sizeof frame) return;
  size_t en = ws_frame_encode(WS_OP_BIN, p, n, frame);
  ws_client.write(frame, en);
}

static void ws_drop_client(void) {
  if (ws_client) ws_client.stop();
  ws_handshaken = false;
  ws_rx_n = 0;
  skrit_dev_reset_auth(&ws_dev); // next session must AUTH again
}

// ---- captive portal ---------------------------------------------------------
static WebServer *portal_http = nullptr;
static DNSServer *portal_dns = nullptr;

static void portal_page(void) {
  String page =
      F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Duta WiFi setup</title><style>body{font-family:sans-serif;max-width:22rem;margin:2rem auto;"
        "padding:0 1rem}input,select,button{width:100%;padding:.5rem;margin:.25rem 0 .75rem;box-sizing:border-box}"
        "button{background:#222;color:#fff;border:0;border-radius:4px;padding:.6rem}</style></head><body>"
        "<h2>Duta WiFi setup</h2><form method=POST action=/save><label>Network</label><select name=s "
        "onchange=\"document.getElementsByName('ssid')[0].value=this.value\"><option value=''>scan…</option>");
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) WiFi.scanNetworks(true); // kick a fresh async scan
  for (int i = 0; i < n; i++) {
    page += "<option>" + WiFi.SSID(i) + "</option>";
  }
  page += F("</select><label>SSID</label><input name=ssid required><label>Password</label>"
            "<input name=pass type=password><button>Save &amp; connect</button></form>"
            "<p style='color:#666;font-size:.85rem'>The Duta reboots and joins your network; find its IP in "
            "Sutra (Network status) and connect via ws://&lt;ip&gt;:9555/.</p></body></html>");
  portal_http->send(200, "text/html", page);
}

static void portal_save(void) {
  String s = portal_http->arg("ssid"), p = portal_http->arg("pass");
  s.toCharArray(wifi_ssid, sizeof wifi_ssid);
  p.toCharArray(wifi_pass, sizeof wifi_pass);
  wifi_save_creds();
  portal_http->send(200, "text/html",
                    F("<!doctype html><body style='font-family:sans-serif'><h2>Saved.</h2>"
                      "<p>The Duta is rebooting and joining your network.</p></body>"));
  delay(500);
  ESP.restart();
}

static void portal_start(void) {
  WiFi.mode(WIFI_AP_STA); // AP for the portal, STA so scanning works
  uint64_t mac = ESP.getEfuseMac(); // efuse MAC — valid before the radio is up
  snprintf(wifi_ap_name, sizeof wifi_ap_name, "Duta-%02X%02X", (uint8_t)(mac >> 32),
           (uint8_t)(mac >> 40));
  WiFi.softAP(wifi_ap_name); // open AP: it only serves the setup page
  WiFi.scanNetworks(true);
  portal_dns = new DNSServer();
  portal_dns->start(53, "*", WiFi.softAPIP()); // captive: every name -> us
  portal_http = new WebServer(80);
  portal_http->on("/", portal_page);
  portal_http->on("/save", HTTP_POST, portal_save);
  portal_http->onNotFound([]() { // captive-detection endpoints -> the page
    portal_http->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
    portal_http->send(302, "text/plain", "");
  });
  portal_http->begin();
  wifi_state = SKRIT_WIFI_PORTAL;
}

// ---- join / state machine ----------------------------------------------------
static void wifi_join(void) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_pass);
  wifi_join_t0 = millis();
  wifi_state = SKRIT_WIFI_CONNECTING;
}

// CFG_GET/CFG_SET handlers (wire these from the HAL cfg callbacks).
static int16_t duta_wifi_cfg_get(uint8_t key, uint8_t *out, uint8_t cap) {
  switch (key) {
  case SKRIT_CFG_WIFI_SSID: {
    uint8_t n = (uint8_t)strlen(wifi_ssid);
    if (n > cap) return -1;
    memcpy(out, wifi_ssid, n);
    return n;
  }
  case SKRIT_CFG_WIFI_PASS: // never the secret — presence only
    if (!wifi_pass[0]) return 0;
    if (cap < 1) return -1;
    out[0] = '*';
    return 1;
  case SKRIT_CFG_WIFI_STATUS: {
    if (cap < 1) return -1;
    out[0] = wifi_state;
    const char *detail = "";
    char ip[20] = {0};
    if (wifi_state == SKRIT_WIFI_CONNECTED) {
      WiFi.localIP().toString().toCharArray(ip, sizeof ip);
      detail = ip;
    } else if (wifi_state == SKRIT_WIFI_PORTAL) {
      detail = wifi_ap_name;
    } else if (wifi_state != SKRIT_WIFI_OFF) {
      detail = wifi_ssid;
    }
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
    if (!len) { // forget the network
      WiFi.disconnect(true);
      wifi_state = SKRIT_WIFI_OFF;
    } else {
      wifi_join();
    }
    return SKRIT_ST_OK;
  case SKRIT_CFG_WIFI_PASS:
    if (len >= sizeof wifi_pass) return SKRIT_ST_BADARGS;
    memcpy(wifi_pass, val, len);
    wifi_pass[len] = 0;
    wifi_save_creds();
    if (wifi_ssid[0]) wifi_join(); // re-join with the new password
    return SKRIT_ST_OK;
  default:
    return SKRIT_ST_NOTFOUND;
  }
}

// ---- WebSocket server pump ----------------------------------------------------
static void ws_pump(void) {
  // accept one client at a time
  if (!ws_client || !ws_client.connected()) {
    if (ws_client) ws_drop_client();
    WiFiClient incoming = ws_server.accept();
    if (incoming) ws_client = incoming;
  }
  if (!ws_client || !ws_client.connected()) return;

  while (ws_client.available() && ws_rx_n < sizeof ws_rx) {
    ws_rx[ws_rx_n++] = (uint8_t)ws_client.read();

    if (!ws_handshaken) {
      // HTTP upgrade: wait for the end of the request headers
      if (ws_rx_n >= 4 && memcmp(ws_rx + ws_rx_n - 4, "\r\n\r\n", 4) == 0) {
        ws_rx[ws_rx_n < sizeof ws_rx ? ws_rx_n : sizeof ws_rx - 1] = 0;
        // find the key, case-insensitively
        const char *KEY = "sec-websocket-key:";
        char *k = NULL;
        for (size_t i = 0; i + 18 < ws_rx_n && !k; i++) {
          size_t j = 0;
          while (j < 18 && tolower((int)ws_rx[i + j]) == KEY[j]) j++;
          if (j == 18) k = (char *)ws_rx + i + 18;
        }
        if (!k) { ws_drop_client(); return; }
        while (*k == ' ') k++;
        char client_key[64] = {0};
        size_t kn = 0;
        while (k[kn] && k[kn] != '\r' && kn < sizeof client_key - 1) { client_key[kn] = k[kn]; kn++; }
        client_key[kn] = 0;
        char accept[40];
        ws_accept_key(client_key, accept);
        ws_client.print("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
                        "Connection: Upgrade\r\nSec-WebSocket-Accept: ");
        ws_client.print(accept);
        ws_client.print("\r\n\r\n");
        ws_handshaken = true;
        ws_rx_n = 0;
      }
      continue;
    }

    // frame phase: decode as many complete frames as the buffer holds
    for (;;) {
      uint8_t payload[DUTA_WS_RX_CAP];
      size_t plen = 0;
      uint8_t op = 0;
      long used = ws_frame_decode(ws_rx, ws_rx_n, payload, sizeof payload, &plen, &op);
      if (used == 0) break; // incomplete
      if (used < 0) { ws_drop_client(); return; }
      memmove(ws_rx, ws_rx + used, ws_rx_n - used);
      ws_rx_n -= (size_t)used;
      if (op == WS_OP_CLOSE) { ws_drop_client(); return; }
      if (op == WS_OP_PING) {
        uint8_t pong[DUTA_WS_RX_CAP + 10];
        size_t en = ws_frame_encode(WS_OP_PONG, payload, plen, pong);
        ws_client.write(pong, en);
        continue;
      }
      for (size_t i = 0; i < plen; i++) skrit_dev_rx(&ws_dev, payload[i]); // the mux stream
    }
  }
  if (ws_rx_n >= sizeof ws_rx) ws_drop_client(); // oversized frame/header — resync
}

// ---- public: init + loop -----------------------------------------------------
// Call after the board HAL is fully populated (incl. trailing fields).
static void duta_wifi_init(const skrit_hal *base) {
  WS_HAL = *base;
  WS_HAL.link_write = ws_link_write;
  WS_HAL.auth_required = 1; // network transport — always gated
  WS_HAL.auth_check = ws_auth_check;
  WS_HAL.auth_set = ws_auth_set;
  WS_HAL.auth_is_default = ws_auth_is_default;
  skrit_dev_init(&ws_dev, &WS_HAL, NULL, /*muxed*/ 1);

  wifi_load_creds();
  if (wifi_ssid[0]) wifi_join();
  else portal_start();
}

static void duta_wifi_loop(void) {
  switch (wifi_state) {
  case SKRIT_WIFI_PORTAL:
    if (portal_dns) portal_dns->processNextRequest();
    if (portal_http) portal_http->handleClient();
    break;
  case SKRIT_WIFI_CONNECTING:
    if (WiFi.status() == WL_CONNECTED) {
      wifi_state = SKRIT_WIFI_CONNECTED;
      ws_server.begin();
    } else if (millis() - wifi_join_t0 > 20000) {
      wifi_state = SKRIT_WIFI_FAILED;
      portal_start(); // fall back to the portal so the device is reachable
    }
    break;
  case SKRIT_WIFI_CONNECTED:
    if (WiFi.status() != WL_CONNECTED) { // dropped — rejoin
      ws_drop_client();
      wifi_join();
      break;
    }
    ws_pump();
    break;
  default:
    break;
  }
}

#endif // DUTA_WIFI_H
