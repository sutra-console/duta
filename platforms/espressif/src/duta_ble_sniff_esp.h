// duta_ble_sniff_esp.h — ESP32 BLE advertising sniffer backend (controller scan).
// Implements the portable duta_sniffer.h contract on the ESP32's BLE controller.
// Unlike the nRF raw-PHY sniffer, this is SCAN-BASED: the controller hands us
// every advertising report (passive scan), which we reframe into the standard
// ble-sniff DATA record. Advertising only; no channel (the controller hops
// internally) and no TX/injection — so the facade sets neither HAS_* capability.
#ifndef DUTA_BLE_SNIFF_ESP_H
#define DUTA_BLE_SNIFF_ESP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the BLE controller + Bluedroid GAP and register the scan callback.
void duta_ble_sniff_esp_init(void);
// Start / stop the passive scan (idempotent). Backs the sniffer DATA backend.
void duta_ble_sniff_esp_start(void);
void duta_ble_sniff_esp_stop(void);
bool duta_ble_sniff_esp_is_on(void);
// Pop one captured advertising report as a ble-sniff DATA record (PROTOCOL.md:
// ts·channel·rssi·access-address·pdu_len·pdu) if one is queued, else 0.
uint16_t duta_ble_sniff_esp_take(uint8_t *out, uint16_t cap);

#ifdef __cplusplus
}
#endif

#endif // DUTA_BLE_SNIFF_ESP_H
