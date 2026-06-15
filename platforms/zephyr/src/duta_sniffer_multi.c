// duta_sniffer_multi.c — unified runtime-switchable radio sniffer (nRF52840).
// ============================================================================
// Compiles BOTH raw-radio backends (BLE advertising + IEEE 802.15.4) into one
// image and dispatches the portable duta_sniffer_* contract through a vtable to
// whichever is active. One radio, so exactly one PHY is configured at a time;
// switching fully re-inits the radio for the target backend (each *_init writes
// all the MODE/PCNF/CRC/SHORTS registers, so no residue carries over).
//
// The host switches at runtime with CFG_SET key 0x14 (SKRIT_CFG_DATA_KIND):
//   kind 4 = ble-sniff   → BLE advertising (channels 37/38/39, RX only for now)
//   kind 7 = ieee802154  → Zigbee / Thread / Matter-over-Thread (channels 11-26,
//                          TX + channel select). The three share the 802.15.4
//                          PHY — only the Wireshark dissector differs.
// main.c's cfg_set calls duta_sniffer_select() and updates the HAL data_kind so
// DATA_DESC reports the active link type (the extcap probes it to pick the DLT).
// ============================================================================
#include "duta_sniffer.h"

#include "duta_154_sniff.h"
#include "duta_ble_sniff.h"
#include "protocol.h" // SKRIT_DATA_*

typedef struct {
  uint8_t kind;
  bool has_tx;
  void (*init)(void);
  void (*start)(void);
  void (*stop)(void);
  bool (*is_on)(void);
  uint16_t (*take)(uint8_t *, uint16_t);
  void (*set_channel)(uint8_t);
  uint8_t (*get_channel)(void);
  void (*tx)(const uint8_t *, uint16_t);
} sniff_vt;

// The BLE backend is RX-only and hops adv channels internally — no channel/TX.
static void ble_set_channel(uint8_t ch) { (void)ch; }
static uint8_t ble_get_channel(void) { return 0; } // 0 = auto/hop
static void ble_no_tx(const uint8_t *p, uint16_t n) {
  (void)p;
  (void)n;
}

static const sniff_vt VT_BLE = {
    .kind = SKRIT_DATA_BLE_SNIFF,
    .has_tx = false,
    .init = duta_sniff_init,
    .start = duta_sniff_start,
    .stop = duta_sniff_stop,
    .is_on = duta_sniff_is_on,
    .take = duta_sniff_take,
    .set_channel = ble_set_channel,
    .get_channel = ble_get_channel,
    .tx = ble_no_tx,
};
static const sniff_vt VT_154 = {
    .kind = SKRIT_DATA_IEEE802154,
    .has_tx = true,
    .init = duta_154_sniff_init,
    .start = duta_154_sniff_start,
    .stop = duta_154_sniff_stop,
    .is_on = duta_154_sniff_is_on,
    .take = duta_154_sniff_take,
    .set_channel = duta_154_sniff_set_channel,
    .get_channel = duta_154_sniff_get_channel,
    .tx = duta_154_sniff_tx,
};

// Boots in BLE-sniff mode (matches DUTA_SNIFF_KIND in duta_sniffer.h).
static const sniff_vt *active = &VT_BLE;

void duta_sniffer_init(void) { active->init(); }
void duta_sniffer_start(void) { active->start(); }
void duta_sniffer_stop(void) { active->stop(); }
bool duta_sniffer_is_on(void) { return active->is_on(); }
uint16_t duta_sniffer_take(uint8_t *out, uint16_t cap) { return active->take(out, cap); }
void duta_sniffer_set_channel(uint8_t ch) { active->set_channel(ch); }
uint8_t duta_sniffer_get_channel(void) { return active->get_channel(); }
void duta_sniffer_tx(const uint8_t *frame, uint16_t len) {
  if (active->has_tx) active->tx(frame, len);
}
uint8_t duta_sniffer_kind(void) { return active->kind; }

bool duta_sniffer_select(uint8_t kind) {
  const sniff_vt *target = (kind == SKRIT_DATA_BLE_SNIFF)    ? &VT_BLE
                           : (kind == SKRIT_DATA_IEEE802154) ? &VT_154
                                                             : NULL;
  if (target == NULL || target == active) return false;
  bool was_on = active->is_on();
  active->stop();      // disable the radio in the old PHY
  active = target;
  active->init();      // reconfigure all RADIO registers for the new PHY
  if (was_on) active->start();
  return true;
}
