// boards/seeed/xiao_rp2040.h: Seeed Studio XIAO RP2040 (vendored board).
// FACTS ONLY: RP2040 + 2MB flash, 21x17.8mm XIAO module, native USB-C. 11 side
// pads (the XIAO D0-D10 footprint). Onboard, none on a header: a WS2812 NeoPixel
// on GP12 gated by a power-enable on GP11 (drive HIGH to light it), and a
// 3-channel user RGB LED (R=GP17, G=GP16, B=GP25; all active low). A target
// assigns the Duta roles.
#ifndef DUTA_VBOARD_SEEED_XIAO_RP2040_H
#define DUTA_VBOARD_SEEED_XIAO_RP2040_H

#include "../../mcu/rp2040.h"

#define BOARD_VENDOR "Seeed Studio"
#define BOARD_MODEL "XIAO RP2040"

// XIAO D0-D10 -> GP {26,27,28,29,6,7,0,1,2,4,3}. D6/D7 are silkscreened TX/RX
// (UART0 GP0/GP1). GP29 (D3/A3) is exposed on this module but modeled internal
// in the shared Pico-centric rp2_pins table, so it stays out of provisioning
// until added there.
static const int16_t duta_board_broken_out[] = {
    0, 1, 2, 3, 4, 6, 7, 26, 27, 28, 29,
};
#define DUTA_BROKEN_OUT_N ((uint8_t)(sizeof duta_board_broken_out / sizeof duta_board_broken_out[0]))

// Onboard hardware, none on a header -> all fixed (never provisionable).
#define ONBOARD_WS2812_PIN 12
static const duta_pin_use duta_board_uses[] = {
    {12, DUTA_USE_FIXED, "onboard WS2812"},
    {11, DUTA_USE_FIXED, "WS2812 power enable"},
    {17, DUTA_USE_FIXED, "user LED (red)"},
    {16, DUTA_USE_FIXED, "user LED (green)"},
    {25, DUTA_USE_FIXED, "user LED (blue)"},
};
#define DUTA_USES_N ((uint8_t)(sizeof duta_board_uses / sizeof duta_board_uses[0]))

#endif // DUTA_VBOARD_SEEED_XIAO_RP2040_H
