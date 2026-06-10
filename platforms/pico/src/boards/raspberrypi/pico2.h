// boards/raspberrypi/pico2.h — Raspberry Pi Pico 2 (RP2350; vendored board).
// Same breakout + onboard LED as the Pico (non-W variant).
#ifndef DUTA_VBOARD_RASPBERRYPI_PICO2_H
#define DUTA_VBOARD_RASPBERRYPI_PICO2_H

#include "../../mcu/rp2350.h"

#define BOARD_VENDOR "Raspberry Pi"
#define BOARD_MODEL "Pico 2"

static const int16_t duta_board_broken_out[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 26, 27, 28,
};
#define DUTA_BROKEN_OUT_N ((uint8_t)(sizeof duta_board_broken_out / sizeof duta_board_broken_out[0]))

#define ONBOARD_LED_PIN 25
static const duta_pin_use duta_board_uses[] = {
    {ONBOARD_LED_PIN, DUTA_USE_FIXED, "onboard LED"},
};
#define DUTA_USES_N ((uint8_t)(sizeof duta_board_uses / sizeof duta_board_uses[0]))

#endif // DUTA_VBOARD_RASPBERRYPI_PICO2_H
