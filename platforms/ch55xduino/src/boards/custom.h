#ifndef __BOARD_CUSTOM_H__
#define __BOARD_CUSTOM_H__

// Template for your own board. Copy this, fill in the pins, and select it with
// BOARD_CUSTOM in src/config.h. Pin numbers are ch55xduino style: Px.y -> x*10+y
// (e.g. P3.4 -> 34, P1.6 -> 16). Avoid P3.6/P3.7 (USB) and P3.0/P3.1 (UART0).
#define BOARD_NAME "Custom"
#define BOARD_VENDOR "Custom"

#define RELAY1_PIN       34
#define RELAY2_PIN       33
#define RELAY_ACTIVE_LOW 1   // 1 = relay ON when pin LOW (typical modules)

#define LED_PIN        14
#define LED_ACTIVE_LOW 0     // 1 = LED ON when pin LOW

#define OLED_SCL_PIN   17
#define OLED_SDA_PIN   16
#define OLED_I2C_ADDR  0x3C

// Opt in to DATA-UART parity (none/odd/even) for legacy gear. Off by default.
//#define PARITY_SUPPORT 1

// Optional inputs for WAITIO (digital/analog). Example: an LDR on an ADC pin.
//#define INPUT_COUNT 1
//#define INPUT_PINS  { 11 }      // pin per input (e.g. P1.1)
//#define INPUT_TYPES { 1 }       // 0 = digital, 1 = analog
//#define INPUT_NAMES { "LDR" }

#endif
