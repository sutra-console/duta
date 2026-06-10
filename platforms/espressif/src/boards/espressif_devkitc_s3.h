// boards/espressif_devkitc_s3.h — Espressif ESP32-S3-DevKitC-1.
// Leaf board header: silicon truth from mcu/esp32s3.h, plus the physical overlay
// (what's broken out / committed) and the compiled-default IO table. See
// duta_io.h (table) and duta_pincap.h (overlay).
#ifndef DUTA_BOARD_ESPRESSIF_DEVKITC_S3_H
#define DUTA_BOARD_ESPRESSIF_DEVKITC_S3_H

#include "../mcu/esp32s3.h" // pin inventory + caps + hazards (+ BOARD_HAS_NATIVE_USB)

#define BOARD_NAME "Duta ESP32-S3"
#define BOARD_VENDOR "Espressif"

// ---- role pins (the compiled default; provisioning may reassign) -----------
#define DATA_TX_PIN 17
#define DATA_RX_PIN 18
#define RELAY1_PIN 4
#define RELAY2_PIN 5
#define LED_PIN 2
#define RGB_PIN 48 // DevKitC-1 onboard WS2812
#define RGB_COUNT 1
#define DTR_PIN (-1)
#define RTS_PIN (-1)

// ---- board-layer overlay: what's physically exposed / committed ------------
// A DevKitC breaks out every usable header pin; the resolver still filters out
// FORBIDDEN (flash/USB) and FIXED uses.
#define DUTA_BROKEN_OUT_ALL 1
static const duta_pin_use duta_board_uses[] = {
    {48, DUTA_USE_DUAL, "onboard WS2812"}, // broken out AND drives the onboard RGB
};
#define DUTA_USES_N ((uint8_t)(sizeof duta_board_uses / sizeof duta_board_uses[0]))

#include "duta_board_io.h" // builds duta_outputs[] from RELAY1/2 + LED + RGB_PIN

#endif // DUTA_BOARD_ESPRESSIF_DEVKITC_S3_H
