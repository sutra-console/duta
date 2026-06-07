#ifndef __USBCDC_H__
#define __USBCDC_H__

#include <stdbool.h>
#include <stdint.h>

void USBInit(void);

// CDC A = DATA port (transparent UART bridge)
bool    CdcA_connected(void);     // host has the DATA port open (DTR)
uint8_t CdcA_available(void);
char    CdcA_read(void);
uint8_t CdcA_write(__data char c);
void    CdcA_flush(void);

// CDC B = CMD port (command/control)
bool    CdcB_connected(void);     // host has the CMD port open (DTR)
uint8_t CdcB_available(void);
char    CdcB_read(void);
uint8_t CdcB_write(__data char c);
void    CdcB_flush(void);
void    CdcB_print(const char *__xdata s); // convenience for the command port

// DATA-port line coding (first 4 bytes = host-requested baud, little-endian)
extern __xdata uint8_t LineCodingA[];

#endif
