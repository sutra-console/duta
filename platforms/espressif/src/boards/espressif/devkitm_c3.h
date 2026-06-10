// boards/espressif/devkitm_c3.h — Espressif ESP32-C3-DevKitM-1 (vendored board).
// Facts only; a target assigns the Duta roles.
#ifndef DUTA_VBOARD_ESPRESSIF_DEVKITM_C3_H
#define DUTA_VBOARD_ESPRESSIF_DEVKITM_C3_H

#include "../../mcu/esp32c3.h"

#define BOARD_VENDOR "Espressif"
#define BOARD_MODEL "ESP32-C3-DevKitM-1"

#define DUTA_BROKEN_OUT_ALL 1

// Onboard WS2812 on GPIO8 (also a strapping pin — the mcu map flags that) and
// on the header -> dual-use.
#define ONBOARD_WS2812_PIN 8
static const duta_pin_use duta_board_uses[] = {
    {ONBOARD_WS2812_PIN, DUTA_USE_DUAL, "onboard WS2812"},
};
#define DUTA_USES_N ((uint8_t)(sizeof duta_board_uses / sizeof duta_board_uses[0]))

#endif // DUTA_VBOARD_ESPRESSIF_DEVKITM_C3_H
