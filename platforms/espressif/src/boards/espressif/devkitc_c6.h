// boards/espressif/devkitc_c6.h: Espressif ESP32-C6-DevKitC-1 (vendored board).
// Facts only; a target assigns the Duta roles. The DevKitC-1 has TWO USB ports:
// the C6's native USB-Serial/JTAG (GPIO12/13) and a CH343 USB-UART on U0
// (GPIO16/17). Onboard addressable RGB on GPIO8.
#ifndef DUTA_VBOARD_ESPRESSIF_DEVKITC_C6_H
#define DUTA_VBOARD_ESPRESSIF_DEVKITC_C6_H

#include "../../mcu/esp32c6.h"

#define BOARD_VENDOR "Espressif"
#define BOARD_MODEL "ESP32-C6-DevKitC-1"

#define DUTA_BROKEN_OUT_ALL 1

// Onboard WS2812 on GPIO8 (also a strapping pin; the mcu map flags that) and
// on the header -> dual-use.
#define ONBOARD_WS2812_PIN 8
static const duta_pin_use duta_board_uses[] = {
    {ONBOARD_WS2812_PIN, DUTA_USE_DUAL, "onboard WS2812"},
};
#define DUTA_USES_N ((uint8_t)(sizeof duta_board_uses / sizeof duta_board_uses[0]))

#endif // DUTA_VBOARD_ESPRESSIF_DEVKITC_C6_H
