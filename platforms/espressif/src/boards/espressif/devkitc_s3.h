// boards/espressif/devkitc_s3.h — Espressif ESP32-S3-DevKitC-1 (vendored board).
// FACTS ONLY: which mcu it carries, what's broken out, what's wired onboard.
// No Duta roles here — a target (targets/*.h) includes this and assigns those.
#ifndef DUTA_VBOARD_ESPRESSIF_DEVKITC_S3_H
#define DUTA_VBOARD_ESPRESSIF_DEVKITC_S3_H

#include "../../mcu/esp32s3.h"

#define BOARD_VENDOR "Espressif"
#define BOARD_MODEL "ESP32-S3-DevKitC-1"

// Every usable GPIO reaches the headers (the mcu map still hides flash/USB).
#define DUTA_BROKEN_OUT_ALL 1

// Onboard hardware. The WS2812 data pin is also on the header -> dual-use.
#define ONBOARD_WS2812_PIN 48
static const duta_pin_use duta_board_uses[] = {
    {ONBOARD_WS2812_PIN, DUTA_USE_DUAL, "onboard WS2812"},
};
#define DUTA_USES_N ((uint8_t)(sizeof duta_board_uses / sizeof duta_board_uses[0]))

#endif // DUTA_VBOARD_ESPRESSIF_DEVKITC_S3_H
