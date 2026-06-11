// targets/duta_xiao_esp32c3.h: OUR Duta build on the Seeed XIAO ESP32-C3.
// The mux link rides the native USB-C, leaving the silkscreened TX/RX (U0,
// GPIO21/20) free as the DATA console bridge. The XIAO ESP32-C3 has no onboard
// user-controllable output, so there are no compiled-default outputs: wire what
// you need to the free pads and add it at runtime via Configure Device.
#ifndef DUTA_TARGET_XIAO_ESP32C3_H
#define DUTA_TARGET_XIAO_ESP32C3_H

#include "../boards/seeed/xiao_esp32c3.h"

#define BOARD_NAME "Duta XIAO ESP32-C3"

// ---- Duta role pins (our wiring) -------------------------------------------
#define DATA_TX_PIN 21 // D6, silkscreened "TX" (U0TXD)
#define DATA_RX_PIN 20 // D7, silkscreened "RX" (U0RXD)
#define DTR_PIN (-1)
#define RTS_PIN (-1)

#include "duta_board_io.h"

#endif // DUTA_TARGET_XIAO_ESP32C3_H
