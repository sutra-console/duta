// duta_154_sniff.h — raw-radio IEEE 802.15.4 sniffer (nRF; CONFIG_DUTA_SNIFF_154).
// Same shape as duta_ble_sniff.h, different radio mode — the nRF52840's radio
// also does 802.15.4 (the PHY/MAC under Zigbee AND Thread).
#ifndef DUTA_154_SNIFF_H
#define DUTA_154_SNIFF_H

#include <stdbool.h>
#include <stdint.h>

// Configure the radio for 802.15.4 sniffing (call once).
void duta_154_sniff_init(void);

// Start / stop capturing — backs the "Sniffing" virtual output. Idempotent.
void duta_154_sniff_start(void);
void duta_154_sniff_stop(void);
bool duta_154_sniff_is_on(void);

// If a frame was captured since the last call (and sniffing is on), write one
// DATA record into `out` (cap bytes) and return its length; else return 0. The
// sniffer dwells on a channel and hops (11..26) when it goes quiet, sticking
// wherever traffic appears. Record layout: see duta_154_sniff.c / PROTOCOL.md.
uint16_t duta_154_sniff_take(uint8_t *out, uint16_t cap);

// Pin the sniffer to one channel (11..26), or 0 to resume auto-hopping. Surfaced
// over SERIAL_SET (the baud field carries the channel). Takes effect immediately.
void duta_154_sniff_set_channel(uint8_t ch);
// The channel currently being listened on (the pinned one, or the live hop).
uint8_t duta_154_sniff_get_channel(void);

#endif // DUTA_154_SNIFF_H
