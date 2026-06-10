// boards/raspberrypi_pico2.h — Raspberry Pi Pico 2 (RP2350).
#ifndef DUTA_BOARD_RASPBERRYPI_PICO2_H
#define DUTA_BOARD_RASPBERRYPI_PICO2_H

#include "../mcu/rp2350.h"

#define BOARD_NAME "Duta Pico 2"
#define BOARD_VENDOR "Raspberry Pi"

#define DATA_TX_PIN 0 // GP0 — UART0 TX
#define DATA_RX_PIN 1 // GP1 — UART0 RX
#define RELAY1_PIN 2  // GP2
#define RELAY2_PIN 3  // GP3
#define LED_PIN 25    // GP25 — onboard LED (non-W variant)
#define DTR_PIN (-1)
#define RTS_PIN (-1)

static const int16_t duta_board_broken_out[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 26, 27, 28,
};
#define DUTA_BROKEN_OUT_N ((uint8_t)(sizeof duta_board_broken_out / sizeof duta_board_broken_out[0]))
static const duta_pin_use duta_board_uses[] = {
    {25, DUTA_USE_FIXED, "onboard LED"},
};
#define DUTA_USES_N ((uint8_t)(sizeof duta_board_uses / sizeof duta_board_uses[0]))

#include "duta_board_io.h"

#endif // DUTA_BOARD_RASPBERRYPI_PICO2_H
