// targets/duta_pico2.h: OUR Duta build on the Raspberry Pi Pico 2.
#ifndef DUTA_TARGET_PICO2_H
#define DUTA_TARGET_PICO2_H

#include "../boards/raspberrypi/pico2.h"

#define BOARD_NAME "Duta Pico 2"

// ---- Duta role pins (our wiring) -------------------------------------------
#define DATA_TX_PIN 0
#define DATA_RX_PIN 1
#define RELAY1_PIN 2
#define RELAY2_PIN 3
#define LED_PIN ONBOARD_LED_PIN
#define DTR_PIN (-1)
#define RTS_PIN (-1)

#include "duta_board_io.h"

#endif // DUTA_TARGET_PICO2_H
