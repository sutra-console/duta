// targets/duta_xiao_esp32c3.h: OUR Duta build on the Seeed XIAO ESP32-C3.
// The mux link rides the native USB-C, leaving the silkscreened TX/RX (U0,
// GPIO21/20) free as the DATA console bridge. Relays + aux LED on free XIAO pads
// (no onboard user LED, so the aux LED is an external one you wire to D4).
#ifndef DUTA_TARGET_XIAO_ESP32C3_H
#define DUTA_TARGET_XIAO_ESP32C3_H

#include "../boards/seeed/xiao_esp32c3.h"

#define BOARD_NAME "Duta XIAO ESP32-C3"

// ---- Duta role pins (our wiring) -------------------------------------------
#define DATA_TX_PIN 21 // D6, silkscreened "TX" (U0TXD)
#define DATA_RX_PIN 20 // D7, silkscreened "RX" (U0RXD)
#define RELAY1_PIN 4   // D2, free pad
#define RELAY2_PIN 5   // D3, free pad
#define LED_PIN 6      // D4, external aux LED
#define DTR_PIN (-1)
#define RTS_PIN (-1)

#include "duta_board_io.h"

#endif // DUTA_TARGET_XIAO_ESP32C3_H
