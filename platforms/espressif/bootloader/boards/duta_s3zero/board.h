/*
 * Duta S3-Zero — TinyUF2 board variant (Waveshare ESP32-S3-Zero, 4MB, no PSRAM).
 *
 * The ESP-IDF analog of the nRF Duta UF2 bootloader: a UF2 mass-storage drive
 * that IDs as a Duta and is entered/exited by command. Overlaid onto a stock
 * TinyUF2 board (feather_esp32s3_nopsram) by build.sh — only the identity + the
 * S3-Zero's pins change; the sdkconfig/partition map come from the stock board.
 *
 * MIT — based on adafruit_feather_esp32s3/board.h (c) 2020 tinyusb.org.
 */
#ifndef DUTA_S3ZERO_H_
#define DUTA_S3ZERO_H_

//--------------------------------------------------------------------+
// Button — the BOOT button (GPIO0). Hold during the indicator to force UF2.
// (No RC double-reset pin on the S3-Zero, so PIN_DOUBLE_RESET_RC is omitted;
// entry is by command — esp_reset_reason hint 0x11F2 — or holding BOOT.)
//--------------------------------------------------------------------+
#define PIN_BUTTON_UF2        0

//--------------------------------------------------------------------+
// LED — the S3-Zero has only an onboard WS2812 on GPIO21 (no plain LED), so
// TinyUF2 uses the neopixel as the flash-activity indicator.
//--------------------------------------------------------------------+
#define NEOPIXEL_PIN          21
#define NEOPIXEL_NUMBER       1
#define NEOPIXEL_BRIGHTNESS   0x10
// (S3-Zero wires the WS2812 in RGB order; the indicator color may look swapped
//  vs GRB boards — cosmetic only.)

//--------------------------------------------------------------------+
// USB UF2 — Duta identity. VID 0x1209 (Duta family, recognized by Sutra) + a
// distinct bootloader PID. Note: this customizes the *TinyUF2* USB identity;
// the ESP32-S3 ROM serial/JTAG downloader underneath stays Espressif-branded
// (it lives in silicon and can't be changed — but that's also why it's an
// unbrickable recovery path).
//--------------------------------------------------------------------+
#define USB_VID                  0x1209
#define USB_PID                  0x5DE0
#define USB_MANUFACTURER         "sutra-console"
#define USB_PRODUCT              "Duta S3-Zero"

#define UF2_PRODUCT_NAME         USB_MANUFACTURER " " USB_PRODUCT
#define UF2_BOARD_ID             "ESP32-S3-duta-s3zero"
#define UF2_VOLUME_LABEL         "DUTA"
#define UF2_INDEX_URL            "https://github.com/sutra-console/duta"

#endif
