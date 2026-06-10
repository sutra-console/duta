// boards/espressif_devkitm_c3.h — Espressif ESP32-C3-DevKitM-1.
#ifndef DUTA_BOARD_ESPRESSIF_DEVKITM_C3_H
#define DUTA_BOARD_ESPRESSIF_DEVKITM_C3_H

#include "../mcu/esp32c3.h"

#define BOARD_NAME "Duta ESP32-C3"
#define BOARD_VENDOR "Espressif"

#define DATA_TX_PIN 21
#define DATA_RX_PIN 20
#define RELAY1_PIN 4
#define RELAY2_PIN 5
#define LED_PIN 6
#define RGB_PIN 8 // DevKitM-1 onboard WS2812
#define RGB_COUNT 1
#define DTR_PIN (-1)
#define RTS_PIN (-1)

#define DUTA_BROKEN_OUT_ALL 1
static const duta_pin_use duta_board_uses[] = {
    {8, DUTA_USE_DUAL, "onboard WS2812"},
};
#define DUTA_USES_N ((uint8_t)(sizeof duta_board_uses / sizeof duta_board_uses[0]))

#include "duta_board_io.h"

#endif // DUTA_BOARD_ESPRESSIF_DEVKITM_C3_H
