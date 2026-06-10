/*
  Dual CDC-ACM composite descriptors for Duta (CH552).
  Two virtual COM ports:
    CDC A = "DATA" (interfaces 0/1, EP1 notify, EP2 data) -> transparent UART bridge
    CDC B = "CMD"  (interfaces 2/3, EP4 notify, EP3 data) -> command/control port
  Raw byte arrays are used for the descriptors so every field is explicit.
*/

#include "USBconstant.h"

// ---- Device descriptor (18 bytes) -----------------------------------------
__code uint8_t DeviceDescriptor[] = {
    0x12,       // bLength
    0x01,       // bDescriptorType = DEVICE
    0x10, 0x01, // bcdUSB 1.10
    0xEF,       // bDeviceClass = Miscellaneous
    0x02,       // bDeviceSubClass = Common Class
    0x01,       // bDeviceProtocol = IAD
    0x08,       // bMaxPacketSize0 = 8
    0x09, 0x12, // idVendor  = 0x1209 (pid.codes)
    0x50, 0xC5, // idProduct = 0xC550
    0x01, 0x01, // bcdDevice = 1.01
    STR_IDX_MANUFACTURER,
    STR_IDX_PRODUCT,
    STR_IDX_SERIAL,
    0x01 // bNumConfigurations
};
const uint8_t DeviceDescriptorLen = sizeof(DeviceDescriptor);

// ---- Configuration descriptor (131 bytes, 4 interfaces) -------------------
__code uint8_t ConfigDescriptor[] = {
    // Configuration header
    0x09, 0x02,
    0x83, 0x00,             // wTotalLength = 131
    0x04,                   // bNumInterfaces = 4
    0x01,                   // bConfigurationValue
    0x00,                   // iConfiguration
    0x80,                   // bmAttributes = bus powered
    0x32,                   // bMaxPower = 100 mA

    // ============ CDC A : DATA port ============
    // IAD: interfaces 0..1
    0x08, 0x0B, INTERFACE_ID_CDCA_CCI, 0x02, 0x02, 0x02, 0x01, STR_IDX_CDCA_NAME,
    // Interface 0: CDC Communications (CCI)
    0x09, 0x04, INTERFACE_ID_CDCA_CCI, 0x00, 0x01, 0x02, 0x02, 0x01, STR_IDX_CDCA_NAME,
    // CDC Header functional
    0x05, 0x24, 0x00, 0x10, 0x01,
    // CDC ACM functional (supports Set/Get Line Coding + Control Line State)
    0x04, 0x24, 0x02, 0x02,
    // CDC Union functional: master 0, slave 1
    0x05, 0x24, 0x06, INTERFACE_ID_CDCA_CCI, INTERFACE_ID_CDCA_DCI,
    // Notification endpoint EP1 IN (interrupt, 8B)
    0x07, 0x05, CDCA_NOTIFY_EPADDR, 0x03, CDC_NOTIFY_EPSIZE, 0x00, 0x40,
    // Interface 1: CDC Data (DCI)
    0x09, 0x04, INTERFACE_ID_CDCA_DCI, 0x00, 0x02, 0x0A, 0x00, 0x00, STR_IDX_CDCA_NAME,
    // Data OUT EP2 (bulk, 32B)
    0x07, 0x05, CDCA_RX_EPADDR, 0x02, CDC_DATA_PKT, 0x00, 0x00,
    // Data IN EP2 (bulk, 32B)
    0x07, 0x05, CDCA_TX_EPADDR, 0x02, CDC_DATA_PKT, 0x00, 0x00,

    // ============ CDC B : CMD port ============
    // IAD: interfaces 2..3
    0x08, 0x0B, INTERFACE_ID_CDCB_CCI, 0x02, 0x02, 0x02, 0x01, STR_IDX_CDCB_NAME,
    // Interface 2: CDC Communications (CCI)
    0x09, 0x04, INTERFACE_ID_CDCB_CCI, 0x00, 0x01, 0x02, 0x02, 0x01, STR_IDX_CDCB_NAME,
    // CDC Header functional
    0x05, 0x24, 0x00, 0x10, 0x01,
    // CDC ACM functional
    0x04, 0x24, 0x02, 0x02,
    // CDC Union functional: master 2, slave 3
    0x05, 0x24, 0x06, INTERFACE_ID_CDCB_CCI, INTERFACE_ID_CDCB_DCI,
    // Notification endpoint EP4 IN (interrupt, 8B)
    0x07, 0x05, CDCB_NOTIFY_EPADDR, 0x03, CDC_NOTIFY_EPSIZE, 0x00, 0x40,
    // Interface 3: CDC Data (DCI)
    0x09, 0x04, INTERFACE_ID_CDCB_DCI, 0x00, 0x02, 0x0A, 0x00, 0x00, STR_IDX_CDCB_NAME,
    // Data OUT EP3 (bulk, 32B)
    0x07, 0x05, CDCB_RX_EPADDR, 0x02, CDC_DATA_PKT, 0x00, 0x00,
    // Data IN EP3 (bulk, 32B)
    0x07, 0x05, CDCB_TX_EPADDR, 0x02, CDC_DATA_PKT, 0x00, 0x00};
const uint8_t ConfigDescriptorLen = sizeof(ConfigDescriptor);

// ---- String descriptors ---------------------------------------------------
// First uint16 packs: bLength = (nChars+1)*2 in low byte, 0x03 (STRING) in high.
__code uint8_t LanguageDescriptor[] = {0x04, 0x03, 0x09, 0x04};

__code uint16_t SerialDescriptor[] = {
    (((5 + 1) * 2) | (0x03 << 8)), 'C', 'H', '5', '5', 'X'};

__code uint16_t ManufacturerDescriptor[] = {
    (((11 + 1) * 2) | (0x03 << 8)),
    'T', 'h', 'e', 'B', 'e', 's', 't', 'J', 'o', 'h', 'n'};

__code uint16_t ProductDescriptor[] = {
    (((11 + 1) * 2) | (0x03 << 8)),
    'D', 'u', 't', 'a', ' ', 'B', 'r', 'i', 'd', 'g', 'e'};

__code uint16_t CDCADescriptor[] = {
    (((9 + 1) * 2) | (0x03 << 8)),
    'D', 'u', 't', 'a', ' ', 'D', 'A', 'T', 'A'};

__code uint16_t CDCBDescriptor[] = {
    (((8 + 1) * 2) | (0x03 << 8)),
    'D', 'u', 't', 'a', ' ', 'C', 'M', 'D'};
