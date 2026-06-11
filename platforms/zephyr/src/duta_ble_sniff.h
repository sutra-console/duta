// duta_ble_sniff.h — raw-radio BLE advertising sniffer (nRF; CONFIG_DUTA_SNIFF).
#ifndef DUTA_BLE_SNIFF_H
#define DUTA_BLE_SNIFF_H

#include <stdint.h>

// Set up the radio and start listening on BLE advertising channel 37.
void duta_sniff_init(void);

// If a packet was captured since the last call, write one DATA record into
// `out` (cap bytes) and return its length; else return 0. Re-arms RX (hopping
// 37→38→39) each time. Record layout: see duta_ble_sniff.c / PROTOCOL.md.
uint16_t duta_sniff_take(uint8_t *out, uint16_t cap);

#endif // DUTA_BLE_SNIFF_H
