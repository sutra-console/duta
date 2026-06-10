#ifndef __BOARD_GENERIC_CH552_H__
#define __BOARD_GENERIC_CH552_H__

// Generic CH552 (CH552G/T) breakout. Pins chosen to exist on most boards and
// avoid USB (P3.6/P3.7) and the UART0 bridge (P3.0/P3.1). Adjust for your wiring.
#define BOARD_NAME "Generic CH552"
#define BOARD_VENDOR "Generic"

#define RELAY1_PIN       34 // P3.4
#define RELAY2_PIN       33 // P3.3
#define RELAY_ACTIVE_LOW 1

#define LED_PIN        15   // P1.5  (no assumption about an on-board LED here)
#define LED_ACTIVE_LOW 0

#define OLED_SCL_PIN   17   // P1.7
#define OLED_SDA_PIN   16   // P1.6
#define OLED_I2C_ADDR  0x3C

#endif
