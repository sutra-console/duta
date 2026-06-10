// mcu/esp32s3.h — ESP32-S3 silicon truth (pin inventory + caps + hazards).
// Reused by every ESP32-S3 board; boards/<vendor>_<board>.h includes this and
// adds only the physical overlay (broken-out + committed pins). See duta_pincap.h.
//
// Hazard notes (the documented set): GPIO0/45/46 are strapping/boot pins
// (caution); GPIO19/20 are the native-USB D-/D+ lines (forbidden when USB-CDC
// is the link, which it is here); GPIO26-32 are the SPI flash bus and GPIO33-37
// the octal-PSRAM bus on -R8 modules (forbidden/caution). On the ESP32 the I²C
// controller routes through the GPIO matrix, so any free pin can be SDA/SCL
// (bus = DUTA_NO_BUS, flexible).
#ifndef DUTA_MCU_ESP32S3_H
#define DUTA_MCU_ESP32S3_H

#include "duta_pincap.h"

#define DUTA_MCU_NAME "ESP32-S3"
#define BOARD_HAS_NATIVE_USB 1 // native USB-CDC carries the mux link

// free digital pin: also ADC/PWM-capable and matrix-routable to I²C.
#define S3_FREE (DUTA_CAP_DIGITAL | DUTA_CAP_ADC | DUTA_CAP_PWM | DUTA_CAP_I2C)
// free digital pin with no ADC (GPIO38+ have no ADC channel).
#define S3_DIG (DUTA_CAP_DIGITAL | DUTA_CAP_PWM | DUTA_CAP_I2C)

static const duta_pin duta_mcu_pins[] = {
    {0, S3_FREE, DUTA_PIN_CAUTION, DUTA_NO_BUS}, // strapping (boot)
    {1, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {2, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {3, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {4, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {5, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {6, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {7, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {8, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {9, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {10, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {11, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {12, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {13, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {14, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {15, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {16, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {17, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {18, S3_FREE, DUTA_PIN_FREE, DUTA_NO_BUS},
    {19, S3_FREE, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS}, // USB D-
    {20, S3_FREE, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS}, // USB D+
    {21, S3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    // 26-32: SPI flash bus — never repurpose.
    {26, S3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {27, S3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {28, S3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {29, S3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {30, S3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {31, S3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {32, S3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    // 33-37: octal PSRAM on -R8 modules; risky in general.
    {33, S3_DIG, DUTA_PIN_CAUTION, DUTA_NO_BUS},
    {34, S3_DIG, DUTA_PIN_CAUTION, DUTA_NO_BUS},
    {35, S3_DIG, DUTA_PIN_CAUTION, DUTA_NO_BUS},
    {36, S3_DIG, DUTA_PIN_CAUTION, DUTA_NO_BUS},
    {37, S3_DIG, DUTA_PIN_CAUTION, DUTA_NO_BUS},
    {38, S3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {39, S3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {40, S3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {41, S3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {42, S3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {43, S3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS}, // U0TXD (console)
    {44, S3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS}, // U0RXD (console)
    {45, S3_DIG, DUTA_PIN_CAUTION, DUTA_NO_BUS}, // strapping
    {46, S3_DIG, DUTA_PIN_CAUTION, DUTA_NO_BUS}, // strapping
    {47, S3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {48, S3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS}, // common onboard-WS2812 pin
};
#define DUTA_MCU_NPINS ((uint8_t)(sizeof duta_mcu_pins / sizeof duta_mcu_pins[0]))

#endif // DUTA_MCU_ESP32S3_H
