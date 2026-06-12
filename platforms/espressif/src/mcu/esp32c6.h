// mcu/esp32c6.h: ESP32-C6 silicon truth (RISC-V; WiFi 6 + BLE 5 + 802.15.4).
// Hazards: GPIO4/5/8/9/15 strapping (4/5 = MTMS/MTDI JTAG, 9 = boot button);
// GPIO12/13 native-USB D-/D+
// (forbidden); GPIO24-30 SPI flash on the WROOM-1's in-package flash (forbidden).
// ADC1 on GPIO0-6. I²C is matrix-routable (bus = DUTA_NO_BUS). U0 = GPIO16/17
// (wired to the DevKitC-1's CH343 bridge). See mcu/esp32c3.h for the pattern.
// NOTE: verify edge pins against the C6 datasheet before trusting provisioning.
#ifndef DUTA_MCU_ESP32C6_H
#define DUTA_MCU_ESP32C6_H

#include "duta_pincap.h"

#define DUTA_MCU_NAME "ESP32-C6"
#define BOARD_HAS_NATIVE_USB 1

#define C6_ADC (DUTA_CAP_DIGITAL | DUTA_CAP_ADC | DUTA_CAP_PWM | DUTA_CAP_I2C)
#define C6_DIG (DUTA_CAP_DIGITAL | DUTA_CAP_PWM | DUTA_CAP_I2C)

static const duta_pin duta_mcu_pins[] = {
    {0, C6_ADC, DUTA_PIN_FREE, DUTA_NO_BUS},
    {1, C6_ADC, DUTA_PIN_FREE, DUTA_NO_BUS},
    {2, C6_ADC, DUTA_PIN_FREE, DUTA_NO_BUS},
    {3, C6_ADC, DUTA_PIN_FREE, DUTA_NO_BUS},
    {4, C6_ADC, DUTA_PIN_CAUTION, DUTA_NO_BUS},    // strapping (MTMS / JTAG)
    {5, C6_ADC, DUTA_PIN_CAUTION, DUTA_NO_BUS},    // strapping (MTDI / JTAG)
    {6, C6_ADC, DUTA_PIN_FREE, DUTA_NO_BUS},
    {7, C6_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {8, C6_DIG, DUTA_PIN_CAUTION, DUTA_NO_BUS},    // strapping; onboard WS2812
    {9, C6_DIG, DUTA_PIN_CAUTION, DUTA_NO_BUS},    // strapping (boot button)
    {10, C6_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {11, C6_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {12, C6_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS}, // USB D-
    {13, C6_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS}, // USB D+
    {14, C6_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {15, C6_DIG, DUTA_PIN_CAUTION, DUTA_NO_BUS},   // strapping
    {16, C6_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},      // U0TXD -> CH343 bridge
    {17, C6_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},      // U0RXD -> CH343 bridge
    {18, C6_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {19, C6_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {20, C6_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {21, C6_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {22, C6_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {23, C6_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {24, C6_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS}, // SPI flash (WROOM-1 in-package)
    {25, C6_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {26, C6_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {27, C6_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {28, C6_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {29, C6_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
    {30, C6_DIG, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},
};
#define DUTA_MCU_NPINS ((uint8_t)(sizeof duta_mcu_pins / sizeof duta_mcu_pins[0]))

#endif // DUTA_MCU_ESP32C6_H
