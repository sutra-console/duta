// duta_ble_sniff_esp.cpp — ESP32 BLE advertising sniffer (controller scan).
// ============================================================================
// The ESP32's BLE radio is a closed controller (no raw-PHY access like the nRF),
// so this captures advertising via a PASSIVE SCAN: the controller reports every
// nearby advertising packet and we reframe each into the standard ble-sniff DATA
// record (PROTOCOL.md "BLE sniffer"):
//   ts_ms(4 LE) · channel(1) · rssi(1, -dBm) · access-address(4 LE) · pdu_len(1) · pdu…
// The pdu is a reconstructed ADV_IND PDU: header(2) + AdvA(6, on-air LE) + adv
// data. Caveats vs the raw nRF sniffer: the controller hops 37/38/39 internally
// (we can't know which — we report 37), only well-formed advertising the
// controller surfaces is seen, and there's no TX. Advertising only.
//
// Built straight on the IDF Bluedroid GAP API (esp_gap_ble_*) rather than the
// Arduino BLEScan C++ wrapper, whose signatures churn across arduino-esp32 majors.
// Compiled in only for the BLE-sniffer build (-DDUTA_SNIFF_BLE_ESP); the body is
// empty otherwise so normal builds don't pull in the BLE stack.
// ============================================================================
#ifdef DUTA_SNIFF_BLE_ESP

#include "duta_ble_sniff_esp.h"

#include <string.h>

#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

#define ADV_ACCESS_ADDR 0x8E89BED6u // the fixed BLE advertising access address
#define ADV_DATA_MAX 31             // legacy advertising payload cap
#define RING_N 32

// One captured advertising report, queued from the GAP callback for take().
struct AdvEntry {
  uint32_t ts;            // ms
  int8_t rssi;            // signed dBm as reported
  uint8_t addr[6];        // AdvA in on-air (little-endian) order
  uint8_t txadd;          // 0 = public, 1 = random (PDU header TxAdd bit)
  uint8_t advlen;         // adv data length (<= ADV_DATA_MAX)
  uint8_t adv[ADV_DATA_MAX];
};

static AdvEntry s_ring[RING_N];
static volatile uint16_t s_head, s_tail; // single-producer (BT task) / single-consumer (loop)
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_on;
static esp_ble_scan_params_t s_scan_params;

static void gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param) {
  switch (event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
      esp_ble_gap_start_scanning(0); // 0 = scan continuously until stopped
      break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT: {
      esp_ble_gap_cb_param_t::ble_scan_result_evt_param *r = &param->scan_rst;
      if (r->search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break; // only adv reports
      AdvEntry e;
      e.ts = (uint32_t)(esp_timer_get_time() / 1000);
      e.rssi = (int8_t)r->rssi;
      for (int i = 0; i < 6; i++) e.addr[i] = r->bda[5 - i]; // reverse → on-air LE
      e.txadd = (r->ble_addr_type == BLE_ADDR_TYPE_PUBLIC) ? 0 : 1;
      uint8_t total = (uint8_t)(r->adv_data_len + r->scan_rsp_len);
      if (total > ADV_DATA_MAX) total = ADV_DATA_MAX;
      e.advlen = total;
      memcpy(e.adv, r->ble_adv, total);
      portENTER_CRITICAL(&s_mux);
      uint16_t nh = (uint16_t)((s_head + 1) % RING_N);
      if (nh != s_tail) { // drop on overflow rather than block the BT task
        s_ring[s_head] = e;
        s_head = nh;
      }
      portEXIT_CRITICAL(&s_mux);
      break;
    }
    default:
      break;
  }
}

void duta_ble_sniff_esp_init(void) {
  esp_bt_controller_config_t cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  esp_bt_controller_init(&cfg);
  esp_bt_controller_enable(ESP_BT_MODE_BLE); // BLE-only — no classic BT memory
  esp_bluedroid_init();
  esp_bluedroid_enable();
  esp_ble_gap_register_callback(gap_cb);

  s_scan_params.scan_type = BLE_SCAN_TYPE_PASSIVE;       // don't transmit SCAN_REQ
  s_scan_params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
  s_scan_params.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
  s_scan_params.scan_interval = 0x60;                    // 60ms (units of 0.625ms)
  s_scan_params.scan_window = 0x60;                      // = interval → listen continuously
  s_scan_params.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE; // report every packet
}

void duta_ble_sniff_esp_start(void) {
  if (s_on) return;
  s_on = true;
  esp_ble_gap_set_scan_params(&s_scan_params); // SCAN_PARAM_SET_COMPLETE_EVT starts the scan
}

void duta_ble_sniff_esp_stop(void) {
  if (!s_on) return;
  s_on = false;
  esp_ble_gap_stop_scanning();
}

bool duta_ble_sniff_esp_is_on(void) { return s_on; }

uint16_t duta_ble_sniff_esp_take(uint8_t *out, uint16_t cap) {
  AdvEntry e;
  bool have = false;
  portENTER_CRITICAL(&s_mux);
  if (s_tail != s_head) {
    e = s_ring[s_tail];
    s_tail = (uint16_t)((s_tail + 1) % RING_N);
    have = true;
  }
  portEXIT_CRITICAL(&s_mux);
  if (!have) return 0;

  uint8_t pdu_len = (uint8_t)(2 + 6 + e.advlen); // header(2) + AdvA(6) + adv data
  uint16_t need = 4 + 1 + 1 + 4 + 1 + pdu_len;
  if (need > cap) return 0;

  uint32_t aa = ADV_ACCESS_ADDR;
  uint16_t n = 0;
  out[n++] = (uint8_t)e.ts;
  out[n++] = (uint8_t)(e.ts >> 8);
  out[n++] = (uint8_t)(e.ts >> 16);
  out[n++] = (uint8_t)(e.ts >> 24);
  out[n++] = 37; // physical channel unknown on the controller — report a valid adv channel
  out[n++] = (uint8_t)(e.rssi < 0 ? -e.rssi : e.rssi); // magnitude, -dBm
  out[n++] = (uint8_t)aa;
  out[n++] = (uint8_t)(aa >> 8);
  out[n++] = (uint8_t)(aa >> 16);
  out[n++] = (uint8_t)(aa >> 24);
  out[n++] = pdu_len;
  out[n++] = (uint8_t)(0x00 | (e.txadd << 6)); // PDU header byte 0: ADV_IND (type 0) + TxAdd
  out[n++] = (uint8_t)(6 + e.advlen);          // PDU header byte 1: payload length
  for (int i = 0; i < 6; i++) out[n++] = e.addr[i];
  memcpy(out + n, e.adv, e.advlen);
  return (uint16_t)(n + e.advlen);
}

#endif // DUTA_SNIFF_BLE_ESP
