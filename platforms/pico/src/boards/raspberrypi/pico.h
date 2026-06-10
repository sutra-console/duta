// boards/raspberrypi/pico.h — Raspberry Pi Pico (RP2040; vendored board).
// FACTS ONLY: which mcu, what's broken out, what's wired onboard. No Duta roles —
// a target (targets/*.h) includes this and assigns those.
#ifndef DUTA_VBOARD_RASPBERRYPI_PICO_H
#define DUTA_VBOARD_RASPBERRYPI_PICO_H

#include "../../mcu/rp2040.h"

#define BOARD_VENDOR "Raspberry Pi"
#define BOARD_MODEL "Pico"

// The Pico breaks out GP0-22 and GP26-28; GP23/24/25 are board-internal.
static const int16_t duta_board_broken_out[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 26, 27, 28,
};
#define DUTA_BROKEN_OUT_N ((uint8_t)(sizeof duta_board_broken_out / sizeof duta_board_broken_out[0]))

// Onboard LED on GP25 — NOT on a header, so it's fixed (never provisionable).
#define ONBOARD_LED_PIN 25
static const duta_pin_use duta_board_uses[] = {
    {ONBOARD_LED_PIN, DUTA_USE_FIXED, "onboard LED"},
};
#define DUTA_USES_N ((uint8_t)(sizeof duta_board_uses / sizeof duta_board_uses[0]))

#endif // DUTA_VBOARD_RASPBERRYPI_PICO_H
