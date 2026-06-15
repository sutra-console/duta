// duta_sniffer.h — the portable Duta radio-sniffer contract + backend selection.
// ============================================================================
// A "sniffer" is a DATA backend that, instead of bridging a wired bus, listens
// promiscuously on a radio and emits each captured frame as one DATA record (the
// per-kind record layout is in PROTOCOL.md). The DATA channel is bidirectional:
// a host write is a frame to TRANSMIT (injection), for backends that support it.
//
// This header is the CONTRACT every sniffer backend implements, plus the
// compile-time selection that binds the active one. The application main loop
// talks only to the `duta_sniffer_*` names below and the capability macros — it
// never references a concrete backend, so the same loop drives any chip's radio.
//
// THE CONTRACT — a backend provides these (portable spelling on the left):
//   void     duta_sniffer_init(void)                  configure the radio (once)
//   void     duta_sniffer_start(void) / _stop(void)   begin/end capture (idempotent)
//   bool     duta_sniffer_is_on(void)                 capturing?
//   uint16_t duta_sniffer_take(uint8_t*, uint16_t)    pop one DATA record if ready, else 0
// and  #define DUTA_SNIFF_KIND <SKRIT_DATA_*>         the record kind it produces.
// Optional capabilities (declare the macro when present):
//   DUTA_SNIFF_HAS_CHANNEL → void duta_sniffer_set_channel(uint8_t) (0 = auto/hop)
//                            uint8_t duta_sniffer_get_channel(void)
//   DUTA_SNIFF_HAS_TX      → void duta_sniffer_tx(const uint8_t*, uint16_t) (inject; no FCS)
// When a backend is selected, DUTA_SNIFFER is defined (a sniffer build).
//
// TO ADD A BACKEND ON A NEW CHIP/CAPABILITY (e.g. an ESP32 802.15.4 radio, a
// CC2531, an nRF24): implement the functions above for that radio (duta_154_sniff.c
// is the reference nRF backend), then add a selection arm below that includes your
// header, maps `duta_sniffer_*` to it, and sets DUTA_SNIFF_KIND (+ capability
// macros). Backends are mutually exclusive — one radio, exactly one selected.
// ============================================================================
#ifndef DUTA_SNIFFER_H
#define DUTA_SNIFFER_H

#include "protocol.h" // SKRIT_DATA_*

// ---- nRF BLE advertising sniffer (Zephyr/nRF; raw radio, channels 37/38/39) --
#if defined(CONFIG_DUTA_SNIFF)
#include "duta_ble_sniff.h"
#define DUTA_SNIFFER 1
#define DUTA_SNIFF_KIND SKRIT_DATA_BLE_SNIFF
#define duta_sniffer_init duta_sniff_init
#define duta_sniffer_start duta_sniff_start
#define duta_sniffer_stop duta_sniff_stop
#define duta_sniffer_is_on duta_sniff_is_on
#define duta_sniffer_take duta_sniff_take
// BLE advertising sniffer hops 37/38/39 internally; RX-only FOR NOW (no
// DUTA_SNIFF_HAS_CHANNEL/HAS_TX yet). TX (raw adv-PDU injection) is intended —
// the nRF raw radio can transmit a crafted BLE PDU the same way the 802.15.4
// backend injects; not wired yet. The end state is TX+RX like ieee802154.

// ---- nRF IEEE 802.15.4 sniffer (Zephyr/nRF; Zigbee/Thread PHY, channels 11-26) -
#elif defined(CONFIG_DUTA_SNIFF_154)
#include "duta_154_sniff.h"
#define DUTA_SNIFFER 1
#define DUTA_SNIFF_KIND SKRIT_DATA_IEEE802154
#define duta_sniffer_init duta_154_sniff_init
#define duta_sniffer_start duta_154_sniff_start
#define duta_sniffer_stop duta_154_sniff_stop
#define duta_sniffer_is_on duta_154_sniff_is_on
#define duta_sniffer_take duta_154_sniff_take
#define duta_sniffer_set_channel duta_154_sniff_set_channel
#define duta_sniffer_get_channel duta_154_sniff_get_channel
#define duta_sniffer_tx duta_154_sniff_tx
#define DUTA_SNIFF_HAS_CHANNEL 1
#define DUTA_SNIFF_HAS_TX 1

// ---- ESP32 BLE advertising sniffer (espressif/Arduino; controller passive scan) -
#elif defined(DUTA_SNIFF_BLE_ESP)
#include "duta_ble_sniff_esp.h"
#define DUTA_SNIFFER 1
#define DUTA_SNIFF_KIND SKRIT_DATA_BLE_SNIFF
#define duta_sniffer_init duta_ble_sniff_esp_init
#define duta_sniffer_start duta_ble_sniff_esp_start
#define duta_sniffer_stop duta_ble_sniff_esp_stop
#define duta_sniffer_is_on duta_ble_sniff_esp_is_on
#define duta_sniffer_take duta_ble_sniff_esp_take
// Scan-based: advertising-RX only FOR NOW (no DUTA_SNIFF_HAS_TX/CHANNEL yet),
// the controller hops internally. Same DATA records, different radio — proof the
// contract spans chips. TX is intended but constrained here: the ESP BLE is
// CONTROLLER-based, so injection means "advertise custom adv data" (esp_ble_gap),
// not the arbitrary raw-PDU injection the nRF raw radio allows. Not wired yet.

// ---- ESP32-C6/H2 IEEE 802.15.4 sniffer+injector (espressif/pure-IDF; native
//      802.15.4 radio via esp_ieee802154, channels 11-26). Same record + TX
//      contract as the nRF backend, different silicon. ------------------------
#elif defined(DUTA_SNIFF_ESP154)
#include "duta_154_sniff_esp.h"
#define DUTA_SNIFFER 1
#define DUTA_SNIFF_KIND SKRIT_DATA_IEEE802154
#define duta_sniffer_init duta_154_sniff_esp_init
#define duta_sniffer_start duta_154_sniff_esp_start
#define duta_sniffer_stop duta_154_sniff_esp_stop
#define duta_sniffer_is_on duta_154_sniff_esp_is_on
#define duta_sniffer_take duta_154_sniff_esp_take
#define duta_sniffer_set_channel duta_154_sniff_esp_set_channel
#define duta_sniffer_get_channel duta_154_sniff_esp_get_channel
#define duta_sniffer_tx duta_154_sniff_esp_tx
#define DUTA_SNIFF_HAS_CHANNEL 1
#define DUTA_SNIFF_HAS_TX 1

// ---- Unified runtime-switchable sniffer (Zephyr/nRF): BOTH the BLE and the
//      IEEE 802.15.4 backends compiled in and dispatched through a vtable. One
//      nRF radio, so exactly one PHY is live at a time — the host switches it at
//      RUNTIME with CFG_SET key 0x14 (SKRIT_CFG_DATA_KIND): 4 = BLE advertising,
//      7 = IEEE 802.15.4 (Zigbee / Thread / Matter-over-Thread, same capture —
//      the difference is only the Wireshark dissector). A switch fully re-inits
//      the radio for the target PHY. DATA_DESC reports the active kind so the
//      extcap picks the matching DLT. Unlike the arms above, duta_sniffer_* are
//      real functions here (duta_sniffer_multi.c), not a macro alias. ---------
#elif defined(CONFIG_DUTA_SNIFF_MULTI)
#include <stdbool.h>
#include <stdint.h>
#define DUTA_SNIFFER 1
#define DUTA_SNIFF_MULTI 1
#define DUTA_SNIFF_HAS_CHANNEL 1
#define DUTA_SNIFF_HAS_TX 1
#define DUTA_SNIFF_KIND SKRIT_DATA_BLE_SNIFF // INITIAL kind; runtime via duta_sniffer_select()
void duta_sniffer_init(void);
void duta_sniffer_start(void);
void duta_sniffer_stop(void);
bool duta_sniffer_is_on(void);
uint16_t duta_sniffer_take(uint8_t *out, uint16_t cap);
void duta_sniffer_set_channel(uint8_t ch);
uint8_t duta_sniffer_get_channel(void);
void duta_sniffer_tx(const uint8_t *frame, uint16_t len);
uint8_t duta_sniffer_kind(void);        // the active SKRIT_DATA_* kind (4 or 7)
bool duta_sniffer_select(uint8_t kind);  // switch by DATA kind; true if it changed

#endif

#endif // DUTA_SNIFFER_H
