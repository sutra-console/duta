// duta_154_sniff_esp.c — IEEE 802.15.4 sniffer+injector on the ESP32-C6/H2 radio.
// ============================================================================
// The espressif counterpart of the nRF backend (zephyr/duta_154_sniff.c): same
// DATA-record + injection contract (duta_sniffer.h), different silicon. Here the
// ESP-IDF `esp_ieee802154` driver does the O-QPSK PHY + MAC + 16-bit FCS, so this
// is just promiscuous RX into a ring + the record framing, raw TX for injection,
// and the same dwell-and-hop channel search.
//
//   record: ts_ms(4 LE) · channel(1, 11..26) · rssi(1, signed dBm) · lqi(1) ·
//           flags(1, bit0 = FCS ok) · psdu_len(1) · psdu…   (psdu includes the FCS)
//
// "Dumb radio": the device knows nothing about Zigbee — it transmits/receives raw
// 802.15.4 MAC frames; all NWK/APS crypto + modelling is host-side (Sutra).
// ============================================================================
#include "duta_154_sniff_esp.h"

// Only compiled into a sniffer build (pio globs src/*.c). Keeps the esp_ieee802154
// dependency + the radio code out of the normal/UART builds.
#if defined(DUTA_SNIFF_ESP154)

#include <string.h>

#include "esp_ieee802154.h"
#include "esp_timer.h"

#define CH_MIN 11
#define CH_MAX 26
#define DWELL_MS 400 // hop away from a quiet channel after this long
#define RING 8       // captured-frame ring (SPSC: RX callback -> take())

typedef struct {
  uint8_t len;       // PSDU length incl. the 2-byte FCS
  uint8_t channel;   // 11..26
  int8_t rssi;       // signed dBm
  uint8_t lqi;
  uint8_t fcs_ok;    // bit0 of the record flags
  uint8_t psdu[127];
} sniff_rec;

static volatile sniff_rec ring[RING];
static volatile uint8_t r_head, r_tail; // single producer (cb) / single consumer (take)
static uint8_t chan = CH_MIN;
static uint8_t fixed_chan; // 0 = auto-hop; 11..26 = pinned
static uint32_t last_ms;
static bool active;
static uint8_t tx_buf[1 + 127]; // PHR(1) + MAC frame (radio appends the FCS)

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// RX-complete callback — overrides the driver's weak symbol. Runs in the
// 802.15.4 driver task; copy the frame into the ring and return promptly.
void esp_ieee802154_receive_done(uint8_t *frame, esp_ieee802154_frame_info_t *info) {
  uint8_t len = frame[0]; // PSDU length incl. FCS
  if (len >= 2 && len <= 127) {
    uint8_t nh = (uint8_t)((r_head + 1u) % RING);
    if (nh != r_tail) { // ring has room (else drop — host isn't draining)
      volatile sniff_rec *s = &ring[r_head];
      s->len = len;
      s->channel = info ? info->channel : chan;
      s->rssi = info ? info->rssi : 0;
      s->lqi = info ? info->lqi : 0xFF;
      s->fcs_ok = 1; // the driver delivers only FCS-valid frames here
      memcpy((void *)s->psdu, frame + 1, len);
      r_head = nh;
    }
  }
  // CRITICAL: hand the RX buffer back to the driver so it keeps receiving. Skip
  // this and the pool exhausts after a few frames and RX silently stalls.
  esp_ieee802154_receive_handle_done(frame);
}

void duta_154_sniff_esp_init(void) {
  esp_ieee802154_enable();
  esp_ieee802154_set_promiscuous(true); // capture all frames (no address filter)
  esp_ieee802154_set_channel(chan);
}

// Enter RX on `chan`. Order matters: receive() first, then rx_when_idle(true) so
// the radio re-arms after each frame (matches the IDF ieee802154 CLI example).
static void rx_arm(void) {
  esp_ieee802154_set_channel(chan);
  esp_ieee802154_receive();
  esp_ieee802154_set_rx_when_idle(true);
}

void duta_154_sniff_esp_start(void) {
  if (active) return;
  active = true;
  chan = fixed_chan ? fixed_chan : CH_MIN;
  last_ms = now_ms();
  rx_arm();
}

void duta_154_sniff_esp_stop(void) {
  if (!active) return;
  active = false;
  esp_ieee802154_disable();
}

bool duta_154_sniff_esp_is_on(void) { return active; }

void duta_154_sniff_esp_set_channel(uint8_t ch) {
  fixed_chan = (ch >= CH_MIN && ch <= CH_MAX) ? ch : 0; // out of range => auto-hop
  chan = fixed_chan ? fixed_chan : CH_MIN;
  last_ms = now_ms();
  if (active) rx_arm();
}

uint8_t duta_154_sniff_esp_get_channel(void) { return chan; }

void duta_154_sniff_esp_tx(const uint8_t *mac, uint16_t maclen) {
  if (maclen == 0 || maclen > 125) return; // +2 FCS must fit a 127-byte PSDU
  tx_buf[0] = (uint8_t)(maclen + 2);       // PHR counts the FCS; the radio appends it
  memcpy(tx_buf + 1, mac, maclen);
  // transmit() is ASYNC — it queues the frame and returns immediately; the PHY
  // sends it later and fires esp_ieee802154_transmit_done(). Forcing RX here
  // (the old bug) flips the radio back before the frame leaves the antenna, so
  // nothing is transmitted. RX re-arms in the tx-done callback / via rx_when_idle.
  esp_ieee802154_transmit(tx_buf, false); // no CCA — raw injection
}

// TX-complete callback — overrides the driver's weak symbol. Re-arm RX so a
// host inject doesn't leave the sniffer deaf (belt-and-suspenders with the
// rx_when_idle the driver already honors). Runs in the 802.15.4 driver task.
void esp_ieee802154_transmit_done(const uint8_t *frame, const uint8_t *ack,
                                  esp_ieee802154_frame_info_t *ack_frame_info) {
  (void)frame;
  (void)ack;
  (void)ack_frame_info;
  if (active) esp_ieee802154_receive();
}

uint16_t duta_154_sniff_esp_take(uint8_t *out, uint16_t cap) {
  if (!active) return 0;

  if (r_tail == r_head) {
    // No frame ready — hop away from a channel that has stayed quiet too long
    // (unless pinned). 802.15.4 nets live on ONE channel; drift until we find it.
    if (!fixed_chan && (now_ms() - last_ms) >= DWELL_MS) {
      chan = (chan >= CH_MAX) ? CH_MIN : (uint8_t)(chan + 1);
      rx_arm();
      last_ms = now_ms();
    }
    return 0;
  }

  volatile sniff_rec *s = &ring[r_tail];
  uint8_t len = s->len;
  uint16_t need = 4 + 1 + 1 + 1 + 1 + 1 + len;
  if (need > cap) return 0; // caller's buffer too small; leave it queued

  last_ms = now_ms(); // traffic on this channel — stick here
  uint32_t ts = now_ms();
  uint16_t n = 0;
  out[n++] = (uint8_t)ts;
  out[n++] = (uint8_t)(ts >> 8);
  out[n++] = (uint8_t)(ts >> 16);
  out[n++] = (uint8_t)(ts >> 24);
  out[n++] = s->channel;
  out[n++] = (uint8_t)s->rssi;
  out[n++] = s->lqi;
  out[n++] = (uint8_t)(s->fcs_ok ? 0x01 : 0x00);
  out[n++] = len;
  memcpy(out + n, (const void *)s->psdu, len);
  r_tail = (uint8_t)((r_tail + 1u) % RING);
  return (uint16_t)(n + len);
}

#endif // DUTA_SNIFF_ESP154
