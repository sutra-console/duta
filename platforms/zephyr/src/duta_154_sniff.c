// duta_154_sniff.c — raw-radio IEEE 802.15.4 sniffer (nRF52840).
// ============================================================================
// The ieee802154 DATA backend. With CONFIG_DUTA_SNIFF_154, the nRF radio is
// driven directly in its 802.15.4 mode (O-QPSK, 250 kbps) — the same silicon
// the BLE sniffer uses, different MODE. It listens promiscuously and captures
// MAC frames; each is emitted on the DATA channel as one record (PROTOCOL.md
// "IEEE 802.15.4 sniffer"):
//   ts_ms(4 LE) · channel(1, 11..26) · rssi(1, signed dBm) · lqi(1) ·
//     flags(1, bit0=FCS ok) · psdu_len(1) · psdu…
// The radio handles the O-QPSK PHY, the SFD, and the 16-bit FCS in hardware, so
// (unlike the BLE path) there's no software de-whitening; bad-FCS frames drop.
//
// Channel strategy: 802.15.4 networks live on ONE channel (not repeated across
// channels like BLE advertising), and we don't know which. So we dwell on a
// channel and hop to the next (11→26) only after it stays quiet for DWELL_MS,
// resetting the timer whenever a frame lands — i.e. we drift until we find the
// active channel, then stick there. (A fixed channel via CFG is a future knob.)
//
// NOTE: the radio register setup below follows the nRF52840 PS 802.15.4 mode;
// the values marked CONFIRM should be validated against live capture.
// ============================================================================
#include <nrfx.h> // MDK: NRF_RADIO + the RADIO_* register bitfields
#include <string.h>
#include <zephyr/kernel.h>

#include "duta_154_sniff.h"

#define CH_MIN 11
#define CH_MAX 26
#define N_CHAN (CH_MAX - CH_MIN + 1)
#define DWELL_MS 400 // hop away from a quiet channel after this long

static uint8_t rx_buf[1 + 127 + 4]; // PHR(1) + max PSDU(127) + slack
static uint8_t chan;                // current channel (11..26)
static uint32_t last_hop_ms;
static bool sniff_active;

// 802.15.4 channel k (11..26) is 2405 + 5*(k-11) MHz; FREQUENCY = MHz - 2400.
static uint8_t chan_freq(uint8_t ch) { return (uint8_t)(5 * (ch - 10)); }

static void radio_arm(uint8_t ch) {
  NRF_RADIO->FREQUENCY = chan_freq(ch);
  NRF_RADIO->EVENTS_END = 0;
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_RXEN = 1; // READY_START short kicks RX automatically
}

static void radio_rearm(uint8_t ch) {
  uint32_t guard = 100000;
  while (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled && guard--) { /* settle */ }
  radio_arm(ch);
}

void duta_154_sniff_init(void) {
  NRF_RADIO->POWER = 0;
  NRF_RADIO->POWER = 1;
  NRF_RADIO->MODE = (RADIO_MODE_MODE_Ieee802154_250Kbit << RADIO_MODE_MODE_Pos);
  // 802.15.4 on-air frame: PHR is one byte (7-bit length), no S0/S1. The length
  // field counts the PSDU *including* the 2-byte FCS (CRCINC = Include). The
  // radio frames on the SFD (0xA7) with a 32-bit zero preamble — no access addr.
  NRF_RADIO->PCNF0 = (8 << RADIO_PCNF0_LFLEN_Pos) |
                     (RADIO_PCNF0_PLEN_32bitZero << RADIO_PCNF0_PLEN_Pos) |
                     (RADIO_PCNF0_CRCINC_Include << RADIO_PCNF0_CRCINC_Pos);
  NRF_RADIO->PCNF1 = (127 << RADIO_PCNF1_MAXLEN_Pos); // no whitening, no base addr
  NRF_RADIO->SFD = 0xA7; // the 802.15.4 start-of-frame delimiter
  // 802.15.4 FCS: 16-bit, poly x^16+x^12+x^5+1 = 0x011021, init 0. SKIPADDR set
  // to the dedicated 802.15.4 value so the radio computes the FCS over the PSDU.
  NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Two << RADIO_CRCCNF_LEN_Pos) |
                      (RADIO_CRCCNF_SKIPADDR_Ieee802154 << RADIO_CRCCNF_SKIPADDR_Pos);
  NRF_RADIO->CRCPOLY = 0x011021u;
  NRF_RADIO->CRCINIT = 0u;
  NRF_RADIO->PACKETPTR = (uint32_t)rx_buf;
  NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk |
                      RADIO_SHORTS_ADDRESS_RSSISTART_Msk;
}

void duta_154_sniff_start(void) {
  if (sniff_active) return;
  sniff_active = true;
  chan = CH_MIN;
  last_hop_ms = k_uptime_get_32();
  radio_arm(chan);
}

void duta_154_sniff_stop(void) {
  if (!sniff_active) return;
  sniff_active = false;
  NRF_RADIO->TASKS_DISABLE = 1;
  uint32_t guard = 100000;
  while (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled && guard--) { /* settle */ }
  NRF_RADIO->EVENTS_END = 0;
}

bool duta_154_sniff_is_on(void) { return sniff_active; }

uint16_t duta_154_sniff_take(uint8_t *out, uint16_t cap) {
  if (!sniff_active) return 0;

  if (!NRF_RADIO->EVENTS_END) {
    // No frame yet — hop away from a channel that has stayed quiet too long.
    if ((k_uptime_get_32() - last_hop_ms) >= DWELL_MS) {
      NRF_RADIO->TASKS_DISABLE = 1;
      chan = (uint8_t)(chan + 1);
      if (chan > CH_MAX) chan = CH_MIN;
      last_hop_ms = k_uptime_get_32();
      radio_rearm(chan);
    }
    return 0;
  }
  NRF_RADIO->EVENTS_END = 0;

  uint8_t crc_ok = (uint8_t)(NRF_RADIO->CRCSTATUS & 1u);
  int8_t rssi = (int8_t)(-(int32_t)(NRF_RADIO->RSSISAMPLE)); // magnitude -> signed dBm
  uint8_t cur = chan;
  uint8_t len = (uint8_t)(rx_buf[0] & 0x7Fu); // PHR length (PSDU incl. 2-byte FCS)

  // Traffic here — stay on this channel and re-arm. END_DISABLE already fired.
  last_hop_ms = k_uptime_get_32();
  radio_rearm(cur);

  if (!crc_ok || len < 2 || len > 127) return 0; // drop bad-FCS / impossible
  uint16_t need = 4 + 1 + 1 + 1 + 1 + 1 + len;
  if (need > cap) return 0;

  uint32_t ts = k_uptime_get_32();
  uint16_t n = 0;
  out[n++] = (uint8_t)ts;
  out[n++] = (uint8_t)(ts >> 8);
  out[n++] = (uint8_t)(ts >> 16);
  out[n++] = (uint8_t)(ts >> 24);
  out[n++] = cur;            // channel 11..26
  out[n++] = (uint8_t)rssi;  // signed dBm
  out[n++] = 0xFF;           // LQI not measured yet — placeholder (CONFIRM)
  out[n++] = 0x01;           // flags: bit0 = FCS ok (we dropped bad-FCS above)
  out[n++] = len;            // PSDU length (includes FCS)
  memcpy(out + n, rx_buf + 1, len);
  return (uint16_t)(n + len);
}
