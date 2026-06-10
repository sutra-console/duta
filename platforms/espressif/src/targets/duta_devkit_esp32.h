// targets/duta_devkit_esp32.h: OUR Duta build on a classic ESP32 DevKit.
#ifndef DUTA_TARGET_DEVKIT_ESP32_H
#define DUTA_TARGET_DEVKIT_ESP32_H

#include "../boards/espressif/devkit_esp32.h"

#define BOARD_NAME "Duta ESP32"

// ---- Duta role pins (our wiring) -------------------------------------------
#define DATA_TX_PIN 17
#define DATA_RX_PIN 16
#define RELAY1_PIN 25
#define RELAY2_PIN 26
#define LED_PIN ONBOARD_LED_PIN // reuse the onboard blue LED (GPIO2, dual-use)
#define RGB_PIN (-1)            // no addressable LED on this board
#define DTR_PIN (-1)
#define RTS_PIN (-1)

#include "duta_board_io.h"

#endif // DUTA_TARGET_DEVKIT_ESP32_H
