// targets/duta_xiao_esp32s3.h: OUR Duta build on the Seeed XIAO ESP32-S3.
// The mux link rides the native USB-C, leaving the silkscreened TX/RX (U0,
// GPIO43/44) free as the DATA console bridge. The only onboard output is the
// yellow user LED (GPIO21, active low) -> the aux LED.
#ifndef DUTA_TARGET_XIAO_ESP32S3_H
#define DUTA_TARGET_XIAO_ESP32S3_H

#include "../boards/seeed/xiao_esp32s3.h"

#define BOARD_NAME "Duta XIAO ESP32-S3"

// ---- Duta role pins (our wiring) -------------------------------------------
#define DATA_TX_PIN 43 // D6, silkscreened "TX" (U0TXD)
#define DATA_RX_PIN 44 // D7, silkscreened "RX" (U0RXD)
#define LED_PIN ONBOARD_LED_PIN // GPIO21 onboard user LED
#define LED_ACTIVE_LOW 1        // it sinks to light
#define DTR_PIN (-1)
#define RTS_PIN (-1)

#include "duta_board_io.h"

#endif // DUTA_TARGET_XIAO_ESP32S3_H
