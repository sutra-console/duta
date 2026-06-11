// boards/seeed/xiao_esp32c3.h: Seeed Studio XIAO ESP32-C3 (vendored board).
// FACTS ONLY: ESP32-C3 (4MB flash), 21x17.8mm XIAO castellated module, native
// USB-C (no USB-UART bridge), external u.FL antenna. 11 broken-out pads (the XIAO
// D0-D10 footprint). No onboard user LED (only a charge indicator). A target
// (targets/*.h) includes this and assigns the Duta roles.
#ifndef DUTA_VBOARD_SEEED_XIAO_ESP32C3_H
#define DUTA_VBOARD_SEEED_XIAO_ESP32C3_H

#include "../../mcu/esp32c3.h"

#define BOARD_VENDOR "Seeed Studio"
#define BOARD_MODEL "XIAO ESP32-C3"

// XIAO D0-D10 -> GPIO {2,3,4,5,6,7,21,20,8,9,10}. D6/D7 are silkscreened TX/RX
// (U0TXD GPIO21 / U0RXD GPIO20). No bottom pads on this model.
static const int16_t duta_board_broken_out[] = {
    2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 21,
};
#define DUTA_BROKEN_OUT_N ((uint8_t)(sizeof duta_board_broken_out / sizeof duta_board_broken_out[0]))

// GPIO9 is the BOOT button and is also exposed as D9 -> dual-use (the mcu map
// already flags it as a strapping pin).
static const duta_pin_use duta_board_uses[] = {
    {9, DUTA_USE_DUAL, "BOOT button"},
};
#define DUTA_USES_N ((uint8_t)(sizeof duta_board_uses / sizeof duta_board_uses[0]))

#endif // DUTA_VBOARD_SEEED_XIAO_ESP32C3_H
