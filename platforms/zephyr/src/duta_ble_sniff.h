// duta_ble_sniff.h — raw-radio BLE advertising sniffer (nRF; CONFIG_DUTA_SNIFF).
#ifndef DUTA_BLE_SNIFF_H
#define DUTA_BLE_SNIFF_H

#include <stdbool.h>
#include <stdint.h>

// Configure the radio for BLE advertising sniffing (call once).
void duta_sniff_init(void);

// Start / stop capturing — backs the "Sniffing" virtual output. Idempotent.
void duta_sniff_start(void);
void duta_sniff_stop(void);
bool duta_sniff_is_on(void);

// If a packet was captured since the last call (and sniffing is on), write one
// DATA record into `out` (cap bytes) and return its length; else return 0.
// Re-arms RX (hopping 37→38→39). Record layout: see duta_ble_sniff.c / PROTOCOL.md.
uint16_t duta_sniff_take(uint8_t *out, uint16_t cap);

#endif // DUTA_BLE_SNIFF_H
