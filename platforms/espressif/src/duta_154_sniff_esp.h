// duta_154_sniff_esp.h — IEEE 802.15.4 sniffer+injector on the ESP32-C6/H2 radio.
#ifndef DUTA_154_SNIFF_ESP_H
#define DUTA_154_SNIFF_ESP_H

#include <stdbool.h>
#include <stdint.h>

void duta_154_sniff_esp_init(void);
void duta_154_sniff_esp_start(void);
void duta_154_sniff_esp_stop(void);
bool duta_154_sniff_esp_is_on(void);
uint16_t duta_154_sniff_esp_take(uint8_t *out, uint16_t cap);
void duta_154_sniff_esp_set_channel(uint8_t ch); // 11..26; 0/out-of-range = auto-hop
uint8_t duta_154_sniff_esp_get_channel(void);
void duta_154_sniff_esp_tx(const uint8_t *mac, uint16_t maclen); // inject; radio adds FCS

#endif // DUTA_154_SNIFF_ESP_H
