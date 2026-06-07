#ifndef __USB_CONSTANT_H__
#define __USB_CONSTANT_H__

// clang-format off
#include <stdint.h>
#include "include/ch5xx.h"
#include "include/ch5xx_usb.h"
// clang-format on

// ---------------------------------------------------------------------------
// Endpoint buffer layout in USB RAM (must stay < USER_USB_RAM = 266).
//
// The CH552 transmits an IN packet from a FIXED +64 offset within the
// endpoint's DMA buffer, regardless of wMaxPacketSize. So a bidirectional
// data endpoint needs a 96-byte buffer: OUT slot 0..63 (only 0..31 used at
// 32-byte packets) + IN slot 64..95.
//
//   0x00..0x07  EP0  control (8B); EP1 notify-A DMA is aliased here (never TX)
//   0x40..0x47  EP4  IN notify B  (HARDWARE-FIXED at Ep0+0x40)
//   0x48..0xA7  EP2  data A (DATA port): OUT 0x48.., IN 0x88..0xA7  (96B)
//   0xA8..0x107 EP3  data B (CMD  port): OUT 0xA8.., IN 0xE8..0x107 (96B)
//
// top = 0x107 = 263 < 266.
// ---------------------------------------------------------------------------
#define EP0_ADDR 0x00
#define EP2_ADDR 0x48
#define EP3_ADDR 0xA8

#define CDC_DATA_PKT  32 // wMaxPacketSize for the bulk data endpoints
#define CDC_IN_OFFSET 64 // hardware-fixed IN buffer offset within the EP buffer
#define CDC_EP_BUFSZ  96 // 64 (OUT slot) + 32 (IN slot)

// CDC class-specific requests
#define SET_LINE_CODING        0x20
#define GET_LINE_CODING        0x21
#define SET_CONTROL_LINE_STATE 0x22

// Endpoint addresses
#define CDCA_NOTIFY_EPADDR 0x81 // EP1 IN  (DATA port notify)
#define CDCA_TX_EPADDR     0x82 // EP2 IN  (DATA port -> host)
#define CDCA_RX_EPADDR     0x02 // EP2 OUT (host -> DATA port)
#define CDCB_NOTIFY_EPADDR 0x84 // EP4 IN  (CMD port notify)
#define CDCB_TX_EPADDR     0x83 // EP3 IN  (CMD port -> host)
#define CDCB_RX_EPADDR     0x03 // EP3 OUT (host -> CMD port)
#define CDC_NOTIFY_EPSIZE  0x08

// Interface numbers
#define INTERFACE_ID_CDCA_CCI 0
#define INTERFACE_ID_CDCA_DCI 1
#define INTERFACE_ID_CDCB_CCI 2
#define INTERFACE_ID_CDCB_DCI 3

// String indices
#define STR_IDX_MANUFACTURER 1
#define STR_IDX_PRODUCT      2
#define STR_IDX_SERIAL       3
#define STR_IDX_CDCA_NAME    4 // "Duta DATA"
#define STR_IDX_CDCB_NAME    5 // "Duta CMD"

extern __code uint8_t DeviceDescriptor[];
extern __code uint8_t ConfigDescriptor[];
extern __code uint8_t LanguageDescriptor[];
extern __code uint16_t SerialDescriptor[];
extern __code uint16_t ProductDescriptor[];
extern __code uint16_t ManufacturerDescriptor[];
extern __code uint16_t CDCADescriptor[];
extern __code uint16_t CDCBDescriptor[];

extern const uint8_t DeviceDescriptorLen;
extern const uint8_t ConfigDescriptorLen;

#endif
