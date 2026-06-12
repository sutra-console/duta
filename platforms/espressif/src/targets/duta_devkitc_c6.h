// targets/duta_devkitc_c6.h: OUR Duta build on the Espressif ESP32-C6-DevKitC-1.
#ifndef DUTA_TARGET_DEVKITC_C6_H
#define DUTA_TARGET_DEVKITC_C6_H

#include "../boards/espressif/devkitc_c6.h"

#define BOARD_NAME "Duta ESP32-C6"

// ---- Duta role pins (our wiring) -------------------------------------------
// The skrit mux (CMD + DATA) rides the C6's native USB-Serial/JTAG. We put the
// DATA console bridge on U0 (GPIO16/17), which the DevKitC-1 wires to its CH343
// USB-UART — so the board's SECOND USB port is a plain DATA console while Sutra
// drives the mux on the native port. Re-provision DATA to header pins to bridge
// an external target instead.
#define DATA_TX_PIN 16
#define DATA_RX_PIN 17
#define RGB_PIN ONBOARD_WS2812_PIN // onboard addressable LED (the default output)
#define RGB_COUNT 1
#define DTR_PIN (-1)
#define RTS_PIN (-1)

#include "duta_board_io.h"

#endif // DUTA_TARGET_DEVKITC_C6_H
