#ifndef DUTA_ESP_BOARD_H
#define DUTA_ESP_BOARD_H
// Target dispatcher for the espressif platform. The PlatformIO env passes a
// -DBOARD_* flag (see platformio.ini); each flag selects a TARGET under
// targets/ — our Duta build on a specific vendored board. Default is the
// ESP32-S3 DevKitC. See BOARDS.md for the platform → mcu → board → target
// layering (vendored boards are facts; targets assign the Duta roles).

#if !defined(BOARD_ESP32S3) && !defined(BOARD_ESP32C3) && !defined(BOARD_ESP32)
#define BOARD_ESP32S3
#endif

#if defined(BOARD_ESP32S3)
#include "targets/duta_devkitc_s3.h"
#elif defined(BOARD_ESP32C3)
#include "targets/duta_devkitm_c3.h"
#elif defined(BOARD_ESP32)
#include "targets/duta_devkit_esp32.h"
#endif

#ifndef BOARD_NAME
#error "No target selected — define BOARD_ESP32S3 / BOARD_ESP32C3 / BOARD_ESP32 (see platformio.ini)."
#endif

#endif // DUTA_ESP_BOARD_H
