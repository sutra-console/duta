#ifndef DUTA_ESP_BOARD_H
#define DUTA_ESP_BOARD_H
// Per-board pin map for the espressif platform. The PlatformIO env passes a
// -DBOARD_* flag (see platformio.ini); default is the ESP32-S3 DevKitC.
//
// Roles:
//   DATA_TX / DATA_RX   hardware UART bridged to the target console (Serial1)
//   RELAY1 / RELAY2     two commandable outputs (active-HIGH by default)
//   LED                 a commandable aux LED
//   DTR / RTS           optional auto-reset lines to the target (-1 = unused);
//                       driven by SERIAL_SIGNAL so a host can enter ESP/AVR
//                       bootloaders. BREAK always works via the UART itself.

#if !defined(BOARD_ESP32S3) && !defined(BOARD_ESP32C3) && !defined(BOARD_ESP32)
#define BOARD_ESP32S3
#endif

// RGB_PIN: an onboard WS2812/NeoPixel data pin (-1 = none). When set, the board
// exposes a 4th output (index 3) of type rgb, driven via Arduino neopixelWrite().

#if defined(BOARD_ESP32S3)
#define BOARD_NAME "Duta ESP32-S3"
#define DATA_TX_PIN 17
#define DATA_RX_PIN 18
#define RELAY1_PIN 4
#define RELAY2_PIN 5
#define LED_PIN 2
#define RGB_PIN 48 // DevKitC-1 onboard WS2812
#define DTR_PIN (-1)
#define RTS_PIN (-1)
#define BOARD_HAS_NATIVE_USB 1 // CMD/DATA muxed over the native USB-CDC

#elif defined(BOARD_ESP32C3)
#define BOARD_NAME "Duta ESP32-C3"
#define DATA_TX_PIN 21
#define DATA_RX_PIN 20
#define RELAY1_PIN 4
#define RELAY2_PIN 5
#define LED_PIN 6
#define RGB_PIN 8 // DevKitM-1 onboard WS2812
#define DTR_PIN (-1)
#define RTS_PIN (-1)
#define BOARD_HAS_NATIVE_USB 1

#elif defined(BOARD_ESP32)
#define BOARD_NAME "Duta ESP32"
#define DATA_TX_PIN 17
#define DATA_RX_PIN 16
#define RELAY1_PIN 25
#define RELAY2_PIN 26
#define LED_PIN 2
#define RGB_PIN (-1) // classic DevKit has no onboard addressable LED
#define DTR_PIN (-1)
#define RTS_PIN (-1)
#define BOARD_HAS_NATIVE_USB 0 // classic ESP32: CMD/DATA muxed over UART0-USB
#endif

#ifndef RGB_PIN
#define RGB_PIN (-1)
#endif

#ifndef RELAY_ACTIVE_LOW
#define RELAY_ACTIVE_LOW 0
#endif
#ifndef LED_ACTIVE_LOW
#define LED_ACTIVE_LOW 0
#endif

#endif // DUTA_ESP_BOARD_H
