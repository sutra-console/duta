// boards/espressif/devkit_esp32.h — classic ESP32 DevKit (DOIT-style; vendored
// board). Facts only; a target assigns the Duta roles. No native USB — UART0
// rides the onboard CP2102.
#ifndef DUTA_VBOARD_ESPRESSIF_DEVKIT_ESP32_H
#define DUTA_VBOARD_ESPRESSIF_DEVKIT_ESP32_H

#include "../../mcu/esp32.h"

#define BOARD_VENDOR "Espressif"
#define BOARD_MODEL "ESP32 DevKit"

#define DUTA_BROKEN_OUT_ALL 1

// Onboard blue LED on GPIO2 (also a strapping pin); on the header -> dual-use.
#define ONBOARD_LED_PIN 2
static const duta_pin_use duta_board_uses[] = {
    {ONBOARD_LED_PIN, DUTA_USE_DUAL, "onboard LED"},
};
#define DUTA_USES_N ((uint8_t)(sizeof duta_board_uses / sizeof duta_board_uses[0]))

#endif // DUTA_VBOARD_ESPRESSIF_DEVKIT_ESP32_H
