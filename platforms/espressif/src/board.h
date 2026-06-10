#ifndef DUTA_ESP_BOARD_H
#define DUTA_ESP_BOARD_H
// Board dispatcher for the espressif platform. The PlatformIO env passes a
// -DBOARD_* flag (see platformio.ini); each board lives in its own leaf header
// under boards/, which pulls its silicon truth from mcu/<chip>.h. Default is the
// ESP32-S3 DevKitC. See BOARDS.md for the platform → mcu → board layout.
//
// A leaf board header provides: BOARD_NAME, BOARD_VENDOR, the role pins
// (DATA_TX/RX, RELAY1/2, LED, RGB, DTR/RTS), the broken-out/committed overlay,
// and the compiled-default duta_outputs[] table.

#if !defined(BOARD_ESP32S3) && !defined(BOARD_ESP32C3) && !defined(BOARD_ESP32)
#define BOARD_ESP32S3
#endif

#if defined(BOARD_ESP32S3)
#include "boards/espressif_devkitc_s3.h"
#elif defined(BOARD_ESP32C3)
#include "boards/espressif_devkitm_c3.h"
#elif defined(BOARD_ESP32)
#include "boards/espressif_devkit_esp32.h"
#endif

#ifndef BOARD_NAME
#error "No board selected — define BOARD_ESP32S3 / BOARD_ESP32C3 / BOARD_ESP32 (see platformio.ini)."
#endif

#endif // DUTA_ESP_BOARD_H
