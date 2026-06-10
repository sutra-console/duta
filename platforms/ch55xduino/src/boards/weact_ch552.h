#ifndef __BOARD_WEACT_CH552_H__
#define __BOARD_WEACT_CH552_H__

// WeAct Studio CH552 core board (V1.0). Schematic-verified.
#define BOARD_NAME "WeAct CH552"
#define BOARD_VENDOR "WeAct Studio"

// Relays — typical active-LOW relay modules (drive LOW = ON).
#define RELAY1_PIN       34 // P3.4
#define RELAY2_PIN       33 // P3.3
#define RELAY_ACTIVE_LOW 1

// Commandable aux LED (external). NOTE: the on-board blue D1 is wired to P3.0,
// which is the UART RXD — so D1 is a free RX-activity light, not controllable
// while the DATA bridge is running. P1.4 is a free pin for an external LED.
#define LED_PIN        14   // P1.4
#define LED_ACTIVE_LOW 0    // active-HIGH

// OLED (bit-bang I2C)
#define OLED_SCL_PIN   17   // P1.7
#define OLED_SDA_PIN   16   // P1.6
#define OLED_I2C_ADDR  0x3C

// DATA bridge UART0 (fixed by silicon): P3.1 = TXD, P3.0 = RXD.

#endif
