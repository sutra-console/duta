// targets/duta_pico.h: OUR Duta build on the Raspberry Pi Pico.
// Based on the vendored board (facts) + the Duta role assignments (choices).
#ifndef DUTA_TARGET_PICO_H
#define DUTA_TARGET_PICO_H

#include "../boards/raspberrypi/pico.h"

#define BOARD_NAME "Duta Pico"

// ---- Duta role pins (our wiring) -------------------------------------------
#define DATA_TX_PIN 0 // GP0, UART0 TX to the target console
#define DATA_RX_PIN 1 // GP1, UART0 RX
#define RELAY1_PIN 2  // external relay modules on free GPIOs
#define RELAY2_PIN 3
#define LED_PIN ONBOARD_LED_PIN // GP25: fixed onboard LED (not provisionable)
#define DTR_PIN (-1)
#define RTS_PIN (-1)

// No onboard addressable LED; wire a WS2812 and set RGB_PIN/RGB_COUNT to add one.
#include "duta_board_io.h"

#endif // DUTA_TARGET_PICO_H
