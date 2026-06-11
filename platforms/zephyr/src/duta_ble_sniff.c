// duta_ble_sniff.c — raw-radio BLE advertising sniffer (nRF52840).
// ============================================================================
// The ble-sniff DATA backend. With CONFIG_DUTA_SNIFF, the nRF radio is driven
// directly (NOT the BLE controller — CONFIG_BT must be off) to listen
// promiscuously on the three BLE advertising channels and capture PDUs. Each
// captured packet is emitted on the DATA channel as one record (PROTOCOL.md
// "BLE sniffer"):
//   ts_ms(4 LE) · channel(1) · rssi(1, -dBm) · access-address(4 LE) · pdu_len(1) · pdu…
// The pdu is the on-air advertising PDU (2-byte header + payload), de-whitened
// and CRC-checked by the radio. Connection-following / data channels are out of
// scope for this MVP (advertising only).
// ============================================================================
#include <nrfx.h> // brings in the MDK: NRF_RADIO + the RADIO_* register bitfields
#include <string.h>
#include <zephyr/kernel.h>

#include "duta_ble_sniff.h"

#define ADV_ACCESS_ADDR 0x8E89BED6u // the fixed BLE advertising access address

static uint8_t rx_buf[2 + 257];                  // header(2) + max payload
static const uint8_t adv_chan[3] = {37, 38, 39}; // BLE advertising channel indices
static const uint8_t adv_freq[3] = {2, 26, 80};  // 2402 / 2426 / 2480 MHz (FREQUENCY reg)
static uint8_t chan_i;
static bool sniff_active; // start/stop gate (the "Sniffing" virtual output)

static void radio_arm(uint8_t i) {
  NRF_RADIO->FREQUENCY = adv_freq[i];
  NRF_RADIO->DATAWHITEIV = adv_chan[i]; // whitening IV = channel index (bit6 forced by HW)
  NRF_RADIO->EVENTS_END = 0;
  NRF_RADIO->EVENTS_DISABLED = 0;
  NRF_RADIO->TASKS_RXEN = 1; // READY_START short kicks RX automatically
}

void duta_sniff_init(void) {
  NRF_RADIO->POWER = 0;
  NRF_RADIO->POWER = 1;
  NRF_RADIO->MODE = (RADIO_MODE_MODE_Ble_1Mbit << RADIO_MODE_MODE_Pos);
  // BLE adv on-air packet: 1-byte S0 (PDU header byte 0), 8-bit LENGTH, no S1.
  NRF_RADIO->PCNF0 = (1 << RADIO_PCNF0_S0LEN_Pos) | (8 << RADIO_PCNF0_LFLEN_Pos);
  NRF_RADIO->PCNF1 = (RADIO_PCNF1_WHITEEN_Enabled << RADIO_PCNF1_WHITEEN_Pos) |
                     (RADIO_PCNF1_ENDIAN_Little << RADIO_PCNF1_ENDIAN_Pos) |
                     (3 << RADIO_PCNF1_BALEN_Pos) | // 3 base bytes + 1 prefix = 4-byte AA
                     (255 << RADIO_PCNF1_MAXLEN_Pos);
  NRF_RADIO->BASE0 = (ADV_ACCESS_ADDR << 8);        // 0x89BED600
  NRF_RADIO->PREFIX0 = (ADV_ACCESS_ADDR >> 24) & 0xFF; // 0x8E
  NRF_RADIO->RXADDRESSES = 1;                       // listen on logical address 0
  // BLE CRC: 24-bit, computed over the PDU (skip the access address), init 0x555555.
  NRF_RADIO->CRCCNF = (RADIO_CRCCNF_LEN_Three << RADIO_CRCCNF_LEN_Pos) |
                      (RADIO_CRCCNF_SKIPADDR_Skip << RADIO_CRCCNF_SKIPADDR_Pos);
  NRF_RADIO->CRCPOLY = 0x100065Bu;
  NRF_RADIO->CRCINIT = 0x555555u;
  NRF_RADIO->PACKETPTR = (uint32_t)rx_buf;
  NRF_RADIO->SHORTS = RADIO_SHORTS_READY_START_Msk | RADIO_SHORTS_END_DISABLE_Msk |
                      RADIO_SHORTS_ADDRESS_RSSISTART_Msk;
}

void duta_sniff_start(void) {
  if (sniff_active) return;
  sniff_active = true;
  chan_i = 0;
  radio_arm(chan_i);
}

void duta_sniff_stop(void) {
  if (!sniff_active) return;
  sniff_active = false;
  NRF_RADIO->TASKS_DISABLE = 1;
  uint32_t guard = 100000;
  while (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled && guard--) { /* settle */ }
  NRF_RADIO->EVENTS_END = 0;
}

bool duta_sniff_is_on(void) { return sniff_active; }

uint16_t duta_sniff_take(uint8_t *out, uint16_t cap) {
  if (!sniff_active || !NRF_RADIO->EVENTS_END) return 0; // stopped, or nothing captured
  NRF_RADIO->EVENTS_END = 0;

  uint8_t crc_ok = (uint8_t)(NRF_RADIO->CRCSTATUS & 1u);
  uint8_t rssi = (uint8_t)NRF_RADIO->RSSISAMPLE; // magnitude in -dBm
  uint8_t ch = adv_chan[chan_i];
  uint8_t pdu_len = (uint8_t)(2 + rx_buf[1]); // header(2) + payload length field

  // hop to the next advertising channel and re-arm (END_DISABLE already fired)
  chan_i = (uint8_t)((chan_i + 1) % 3);
  uint32_t guard = 100000;
  while (NRF_RADIO->STATE != RADIO_STATE_STATE_Disabled && guard--) { /* spin briefly */ }
  radio_arm(chan_i);

  if (!crc_ok || pdu_len > 39) return 0; // drop corrupt / over-long captures
  uint16_t need = 4 + 1 + 1 + 4 + 1 + pdu_len;
  if (need > cap) return 0;

  uint32_t ts = k_uptime_get_32();
  uint32_t aa = ADV_ACCESS_ADDR;
  uint16_t n = 0;
  out[n++] = (uint8_t)ts;
  out[n++] = (uint8_t)(ts >> 8);
  out[n++] = (uint8_t)(ts >> 16);
  out[n++] = (uint8_t)(ts >> 24);
  out[n++] = ch;
  out[n++] = rssi;
  out[n++] = (uint8_t)aa;
  out[n++] = (uint8_t)(aa >> 8);
  out[n++] = (uint8_t)(aa >> 16);
  out[n++] = (uint8_t)(aa >> 24);
  out[n++] = pdu_len;
  memcpy(out + n, rx_buf, pdu_len);
  return (uint16_t)(n + pdu_len);
}
