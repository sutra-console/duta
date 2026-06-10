// targets/duta_s3_zero.h — OUR Duta build on the Waveshare ESP32-S3-Zero.
// The mux link rides the native USB-C, which leaves the board's silkscreened
// TX/RX (UART0, GP43/44) free — so they ARE the DATA console bridge: wire the
// target straight to the labeled pins. Relays/LED go on free side-header GPIOs.
#ifndef DUTA_TARGET_S3_ZERO_H
#define DUTA_TARGET_S3_ZERO_H

#include "../boards/waveshare/esp32_s3_zero.h"

#define BOARD_NAME "Duta S3-Zero"

// ---- Duta role pins (our wiring) -------------------------------------------
#define DATA_TX_PIN 43 // the board's "TX" pin
#define DATA_RX_PIN 44 // the board's "RX" pin
#define RELAY1_PIN 4   // external relay modules on free GPIOs
#define RELAY2_PIN 5
#define LED_PIN 6 // external aux LED (no plain onboard LED)
#define RGB_PIN ONBOARD_WS2812_PIN // fixed GP21 — always the RGB
#define RGB_COUNT 1
#define DTR_PIN (-1)
#define RTS_PIN (-1)

#include "duta_board_io.h"

#endif // DUTA_TARGET_S3_ZERO_H
