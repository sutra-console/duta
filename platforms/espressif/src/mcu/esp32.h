// mcu/esp32.h: classic ESP32 silicon truth. See duta_pincap.h / mcu/esp32s3.h.
// No native USB (the mux link rides UART0). Hazards: GPIO0/2/5/12/15 strapping
// (caution); GPIO6-11 SPI flash (forbidden); GPIO34-39 are INPUT-ONLY (no output,
// no PWM, encoded as ADC-only so they're never offered as outputs). DAC on
// GPIO25/26. I²C is matrix-routable (bus = DUTA_NO_BUS).
#ifndef DUTA_MCU_ESP32_H
#define DUTA_MCU_ESP32_H

#include "duta_pincap.h"

#define DUTA_MCU_NAME "ESP32"
#define BOARD_HAS_NATIVE_USB 0

#define E_OUT (DUTA_CAP_DIGITAL | DUTA_CAP_ADC | DUTA_CAP_PWM | DUTA_CAP_I2C)
#define E_DIG (DUTA_CAP_DIGITAL | DUTA_CAP_PWM | DUTA_CAP_I2C) // no ADC (32+ have, but keep simple)
#define E_IN (DUTA_CAP_ADC)                                    // input-only pins

static const duta_pin duta_mcu_pins[] = {
    {0, E_OUT, DUTA_PIN_CAUTION, DUTA_NO_BUS}, // strapping (boot)
    {1, E_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},    // U0TXD
    {2, E_OUT, DUTA_PIN_CAUTION, DUTA_NO_BUS}, // strapping
    {3, E_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},    // U0RXD
    {4, E_OUT, DUTA_PIN_FREE, DUTA_NO_BUS},
    {5, E_DIG, DUTA_PIN_CAUTION, DUTA_NO_BUS}, // strapping
    {6, E_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {7, E_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {8, E_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {9, E_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {10, E_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {11, E_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS}, // 6-11 SPI flash
    {12, E_OUT, DUTA_PIN_CAUTION, DUTA_NO_BUS},   // strapping
    {13, E_OUT, DUTA_PIN_FREE, DUTA_NO_BUS},
    {14, E_OUT, DUTA_PIN_FREE, DUTA_NO_BUS},
    {15, E_OUT, DUTA_PIN_CAUTION, DUTA_NO_BUS}, // strapping
    {16, E_OUT, DUTA_PIN_FREE, DUTA_NO_BUS},
    {17, E_OUT, DUTA_PIN_FREE, DUTA_NO_BUS},
    {18, E_OUT, DUTA_PIN_FREE, DUTA_NO_BUS},
    {19, E_OUT, DUTA_PIN_FREE, DUTA_NO_BUS},
    {21, E_OUT, DUTA_PIN_FREE, DUTA_NO_BUS},
    {22, E_OUT, DUTA_PIN_FREE, DUTA_NO_BUS},
    {23, E_OUT, DUTA_PIN_FREE, DUTA_NO_BUS},
    {25, E_OUT | DUTA_CAP_DAC, DUTA_PIN_FREE, DUTA_NO_BUS},
    {26, E_OUT | DUTA_CAP_DAC, DUTA_PIN_FREE, DUTA_NO_BUS},
    {27, E_OUT, DUTA_PIN_FREE, DUTA_NO_BUS},
    {32, E_OUT, DUTA_PIN_FREE, DUTA_NO_BUS},
    {33, E_OUT, DUTA_PIN_FREE, DUTA_NO_BUS},
    {34, E_IN, DUTA_PIN_FREE, DUTA_NO_BUS}, // 34-39 input-only
    {35, E_IN, DUTA_PIN_FREE, DUTA_NO_BUS},
    {36, E_IN, DUTA_PIN_FREE, DUTA_NO_BUS},
    {39, E_IN, DUTA_PIN_FREE, DUTA_NO_BUS},
};
#define DUTA_MCU_NPINS ((uint8_t)(sizeof duta_mcu_pins / sizeof duta_mcu_pins[0]))

#endif // DUTA_MCU_ESP32_H
