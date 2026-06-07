#ifndef __DUTA_SSD1306_H__
#define __DUTA_SSD1306_H__

// Minimal SSD1306 text API used by Duta to mirror the DATA-port RX.
// The implementation is gated by ENABLE_OLED (src/config.h). When disabled the
// .c body is empty so it costs no flash. Real bit-bang I2C + font driver is
// wired in once the dual-CDC core is verified on hardware.

void oledInit(void);
void oledClear(void);
void oledPutc(char c);
void oledPrint(const char *s); // generic pointer: accepts __code and __xdata

#endif
