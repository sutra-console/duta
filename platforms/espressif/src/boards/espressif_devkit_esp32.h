// boards/espressif_devkit_esp32.h — classic ESP32 DevKit (no native USB; the mux
// link rides UART0 via the onboard CP2102). No onboard addressable LED.
#ifndef DUTA_BOARD_ESPRESSIF_DEVKIT_ESP32_H
#define DUTA_BOARD_ESPRESSIF_DEVKIT_ESP32_H

#include "../mcu/esp32.h"

#define BOARD_NAME "Duta ESP32"
#define BOARD_VENDOR "Espressif"

#define DATA_TX_PIN 17
#define DATA_RX_PIN 16
#define RELAY1_PIN 25
#define RELAY2_PIN 26
#define LED_PIN 2
#define RGB_PIN (-1) // classic DevKit has no onboard addressable LED
#define DTR_PIN (-1)
#define RTS_PIN (-1)

#define DUTA_BROKEN_OUT_ALL 1
// No onboard committed peripherals to flag (flash/strapping handled by the mcu map).

#include "duta_board_io.h"

#endif // DUTA_BOARD_ESPRESSIF_DEVKIT_ESP32_H
