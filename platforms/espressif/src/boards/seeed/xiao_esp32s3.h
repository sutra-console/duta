// boards/seeed/xiao_esp32s3.h: Seeed Studio XIAO ESP32-S3 (vendored board).
// FACTS ONLY: ESP32-S3R8 (8MB flash, 8MB PSRAM), 21x17.5mm XIAO module, native
// USB-C. 11 side pads (the XIAO D0-D10 footprint); the Sense variant adds bottom
// pads for the camera (not modeled here). Onboard yellow user LED on GPIO21
// (active low), not on a header. A target assigns the Duta roles.
#ifndef DUTA_VBOARD_SEEED_XIAO_ESP32S3_H
#define DUTA_VBOARD_SEEED_XIAO_ESP32S3_H

#include "../../mcu/esp32s3.h"

#define BOARD_VENDOR "Seeed Studio"
#define BOARD_MODEL "XIAO ESP32-S3"

// XIAO D0-D10 -> GPIO {1,2,3,4,5,6,7,8,9,43,44}. D6/D7 are silkscreened TX/RX
// (U0TXD GPIO43 / U0RXD GPIO44).
static const int16_t duta_board_broken_out[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 43, 44,
};
#define DUTA_BROKEN_OUT_N ((uint8_t)(sizeof duta_board_broken_out / sizeof duta_board_broken_out[0]))

// Onboard yellow user LED on GPIO21 (active low), NOT on a header -> fixed.
// GPIO0 is the BOOT button.
#define ONBOARD_LED_PIN 21
static const duta_pin_use duta_board_uses[] = {
    {ONBOARD_LED_PIN, DUTA_USE_FIXED, "onboard LED"},
    {0, DUTA_USE_FIXED, "BOOT button"},
};
#define DUTA_USES_N ((uint8_t)(sizeof duta_board_uses / sizeof duta_board_uses[0]))

#endif // DUTA_VBOARD_SEEED_XIAO_ESP32S3_H
