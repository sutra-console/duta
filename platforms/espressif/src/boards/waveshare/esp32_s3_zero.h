// boards/waveshare/esp32_s3_zero.h: Waveshare ESP32-S3-Zero (vendored board).
// FACTS ONLY: ESP32-S3FH4R2 (4MB flash, 2MB quad PSRAM), 23.5×18mm castellated
// module, native USB-C (no USB-UART bridge). No Duta roles here; a target
// (targets/*.h) includes this and assigns those.
#ifndef DUTA_VBOARD_WAVESHARE_ESP32_S3_ZERO_H
#define DUTA_VBOARD_WAVESHARE_ESP32_S3_ZERO_H

#include "../../mcu/esp32s3.h"

#define BOARD_VENDOR "Waveshare"
#define BOARD_MODEL "ESP32-S3-Zero"

// Breakout: GP1-GP13 + TX/RX (GP43/44) on the side headers; GP14-GP18 and
// GP38-GP42 + GP45 on the bottom castellated pads. GP33-37 are not exposed.
static const int16_t duta_board_broken_out[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
    38, 39, 40, 41, 42, 43, 44, 45,
};
#define DUTA_BROKEN_OUT_N ((uint8_t)(sizeof duta_board_broken_out / sizeof duta_board_broken_out[0]))

// Onboard hardware. The WS2812 data pin (GP21) is NOT on a header -> fixed:
// it's always the RGB LED, never provisionable. The BOOT button owns GP0
// (not exposed either; listed for documentation).
#define ONBOARD_WS2812_PIN 21
static const duta_pin_use duta_board_uses[] = {
    {ONBOARD_WS2812_PIN, DUTA_USE_FIXED, "onboard WS2812"},
    {0, DUTA_USE_FIXED, "BOOT button"},
};
#define DUTA_USES_N ((uint8_t)(sizeof duta_board_uses / sizeof duta_board_uses[0]))

#endif // DUTA_VBOARD_WAVESHARE_ESP32_S3_ZERO_H
