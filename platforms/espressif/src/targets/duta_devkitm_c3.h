// targets/duta_devkitm_c3.h — OUR Duta build on the Espressif ESP32-C3-DevKitM-1.
#ifndef DUTA_TARGET_DEVKITM_C3_H
#define DUTA_TARGET_DEVKITM_C3_H

#include "../boards/espressif/devkitm_c3.h"

#define BOARD_NAME "Duta ESP32-C3"

// ---- Duta role pins (our wiring) -------------------------------------------
#define DATA_TX_PIN 21
#define DATA_RX_PIN 20
#define RELAY1_PIN 4
#define RELAY2_PIN 5
#define LED_PIN 6 // external aux LED
#define RGB_PIN ONBOARD_WS2812_PIN
#define RGB_COUNT 1
#define DTR_PIN (-1)
#define RTS_PIN (-1)

#include "duta_board_io.h"

#endif // DUTA_TARGET_DEVKITM_C3_H
