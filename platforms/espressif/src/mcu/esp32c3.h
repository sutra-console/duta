// mcu/esp32c3.h: ESP32-C3 silicon truth. See duta_pincap.h / mcu/esp32s3.h.
// Hazards: GPIO2/8/9 strapping (caution; 9 = boot); GPIO12-17 SPI flash
// (forbidden); GPIO18/19 native-USB D-/D+ (forbidden). ADC on GPIO0-5. I²C is
// matrix-routable (bus = DUTA_NO_BUS).
#ifndef DUTA_MCU_ESP32C3_H
#define DUTA_MCU_ESP32C3_H

#include "duta_pincap.h"

#define DUTA_MCU_NAME "ESP32-C3"
#define BOARD_HAS_NATIVE_USB 1

#define C3_ADC (DUTA_CAP_DIGITAL | DUTA_CAP_ADC | DUTA_CAP_PWM | DUTA_CAP_I2C)
#define C3_DIG (DUTA_CAP_DIGITAL | DUTA_CAP_PWM | DUTA_CAP_I2C)

static const duta_pin duta_mcu_pins[] = {
    {0, C3_ADC, DUTA_PIN_FREE, DUTA_NO_BUS},
    {1, C3_ADC, DUTA_PIN_FREE, DUTA_NO_BUS},
    {2, C3_ADC, DUTA_PIN_CAUTION, DUTA_NO_BUS}, // strapping
    {3, C3_ADC, DUTA_PIN_FREE, DUTA_NO_BUS},
    {4, C3_ADC, DUTA_PIN_FREE, DUTA_NO_BUS},
    {5, C3_ADC, DUTA_PIN_FREE, DUTA_NO_BUS},
    {6, C3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {7, C3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {8, C3_DIG, DUTA_PIN_CAUTION, DUTA_NO_BUS}, // strapping; common onboard WS2812
    {9, C3_DIG, DUTA_PIN_CAUTION, DUTA_NO_BUS}, // strapping (boot)
    {10, C3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {11, C3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS}, // VDD_SPI
    {12, C3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS}, // SPI flash
    {13, C3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {14, C3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {15, C3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {16, C3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {17, C3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {18, C3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS}, // USB D-
    {19, C3_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS}, // USB D+
    {20, C3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS}, // U0RXD
    {21, C3_DIG, DUTA_PIN_FREE, DUTA_NO_BUS}, // U0TXD
};
#define DUTA_MCU_NPINS ((uint8_t)(sizeof duta_mcu_pins / sizeof duta_mcu_pins[0]))

#endif // DUTA_MCU_ESP32C3_H
