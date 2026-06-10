// targets/duta_devkitc_s3.h: OUR Duta build on the Espressif ESP32-S3-DevKitC-1.
// Based on the vendored board (facts) + the Duta role assignments (choices).
// These pins are what WE wire, not board givens. Change them to match your build,
// or provision at runtime (the compiled table is just the default).
#ifndef DUTA_TARGET_DEVKITC_S3_H
#define DUTA_TARGET_DEVKITC_S3_H

#include "../boards/espressif/devkitc_s3.h"

#define BOARD_NAME "Duta ESP32-S3"

// ---- Duta role pins (our wiring) -------------------------------------------
#define DATA_TX_PIN 17 // target-console UART
#define DATA_RX_PIN 18
#define RELAY1_PIN 4 // external relay modules on free GPIOs
#define RELAY2_PIN 5
#define LED_PIN 2 // external aux LED (the DevKitC has no plain onboard LED)
#define RGB_PIN ONBOARD_WS2812_PIN
#define RGB_COUNT 1
#define DTR_PIN (-1)
#define RTS_PIN (-1)

#include "duta_board_io.h" // builds the default duta_outputs[] from the roles

#endif // DUTA_TARGET_DEVKITC_S3_H
