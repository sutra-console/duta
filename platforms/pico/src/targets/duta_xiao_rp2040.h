// targets/duta_xiao_rp2040.h: OUR Duta build on the Seeed XIAO RP2040.
// The mux link rides the native USB-C; the silkscreened TX/RX (UART0 GP0/GP1)
// are the DATA console bridge. Relays on free pads; the aux LED uses the onboard
// red user LED (active low). The onboard WS2812 is left unwired by default: it
// needs GP11 driven HIGH to power it, which the generic driver doesn't do (set
// RGB_PIN to 12 and drive GP11 yourself to enable it).
#ifndef DUTA_TARGET_XIAO_RP2040_H
#define DUTA_TARGET_XIAO_RP2040_H

#include "../boards/seeed/xiao_rp2040.h"

#define BOARD_NAME "Duta XIAO RP2040"

// ---- Duta role pins (our wiring) -------------------------------------------
#define DATA_TX_PIN 0 // D6 / UART0 TX to the target console
#define DATA_RX_PIN 1 // D7 / UART0 RX
#define RELAY1_PIN 2  // D8, free pad
#define RELAY2_PIN 3  // D10, free pad
#define LED_PIN 17    // onboard red user LED
#define LED_ACTIVE_LOW 1
#define DTR_PIN (-1)
#define RTS_PIN (-1)

#include "duta_board_io.h"

#endif // DUTA_TARGET_XIAO_RP2040_H
