// mcu/rp2_pins.h — the GP0..29 pin table shared by RP2040 and RP2350. Included by
// mcu/rp2040.h and mcu/rp2350.h after they set DUTA_MCU_NAME / native-USB. The
// RP2 boot ROM uses dedicated QSPI pins (not in GP0..29), so there are no
// strapping/flash hazards in the GPIO range. Every GPIO is PWM-capable; GP26-29
// reach the ADC; I²C routes via the two controllers (bus = DUTA_NO_BUS, flexible).
#ifndef DUTA_MCU_RP2_PINS_H
#define DUTA_MCU_RP2_PINS_H

#include "duta_pincap.h"

#define BOARD_HAS_NATIVE_USB 1 // native USB-CDC carries the mux link

#define RP2_DIG (DUTA_CAP_DIGITAL | DUTA_CAP_PWM | DUTA_CAP_I2C | DUTA_CAP_SPI)
#define RP2_ADC (RP2_DIG | DUTA_CAP_ADC)

static const duta_pin duta_mcu_pins[] = {
    {0, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {1, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {2, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {3, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {4, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {5, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {6, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {7, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {8, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {9, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {10, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {11, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {12, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {13, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {14, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {15, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {16, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {17, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {18, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {19, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {20, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {21, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {22, RP2_DIG, DUTA_PIN_FREE, DUTA_NO_BUS},
    {26, RP2_ADC, DUTA_PIN_FREE, DUTA_NO_BUS},
    {27, RP2_ADC, DUTA_PIN_FREE, DUTA_NO_BUS},
    {28, RP2_ADC, DUTA_PIN_FREE, DUTA_NO_BUS},
};
#define DUTA_MCU_NPINS ((uint8_t)(sizeof duta_mcu_pins / sizeof duta_mcu_pins[0]))

#endif // DUTA_MCU_RP2_PINS_H
