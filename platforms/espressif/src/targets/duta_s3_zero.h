// targets/duta_s3_zero.h: OUR Duta build on the Waveshare ESP32-S3-Zero.
// The mux link rides the native USB-C, which leaves the board's silkscreened
// TX/RX (UART0, GP43/44) free, so they ARE the DATA console bridge: wire the
// target straight to the labeled pins. Default output is the onboard RGB only;
// relays / external fixtures are added at runtime (Configure Device).
#ifndef DUTA_TARGET_S3_ZERO_H
#define DUTA_TARGET_S3_ZERO_H

#include "../boards/waveshare/esp32_s3_zero.h"

#define BOARD_NAME "Duta S3-Zero"

// ---- Duta role pins (our wiring) -------------------------------------------
#define DATA_TX_PIN 43 // the side-header pin silkscreened "TX" (GP43)
#define DATA_RX_PIN 44 // the side-header pin silkscreened "RX" (GP44)
#define RGB_PIN ONBOARD_WS2812_PIN // fixed GP21: always the RGB (onboard)
#define RGB_ORDER ONBOARD_WS2812_ORDER
#define RGB_COUNT 1
#define DTR_PIN (-1)
#define RTS_PIN (-1)
// I²C master DATA bridge: our wiring puts SCL on GP1 and SDA on GP2 (both
// broken out on the side header, not fixed). Overrides duta_i2c.h's GP8/9 default.
#define I2C_SCL_PIN 1
#define I2C_SDA_PIN 2
// VL53L0X XSHUT (active-low shutdown/reset) on GP4 — driven by the ToF driver to
// power-cycle the sensor on init.
#define VL53L0X_XSHUT_PIN 4

#include "duta_board_io.h"

#endif // DUTA_TARGET_S3_ZERO_H
