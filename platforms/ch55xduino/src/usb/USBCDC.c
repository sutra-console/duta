/*
  Dual CDC-ACM data path for Duta (CH552).
  Port A -> EP2 (DATA / UART bridge), Port B -> EP3 (CMD / control).
  Adapted from the ch55xduino single-CDC user example.
*/

// clang-format off
#include <stdint.h>
#include <stdbool.h>
#include "include/ch5xx.h"
#include "include/ch5xx_usb.h"
#include "USBconstant.h"
#include "USBhandler.h"
// clang-format on

// clang-format off
extern __xdata __at (EP0_ADDR) uint8_t Ep0Buffer[];
extern __xdata __at (EP2_ADDR) uint8_t Ep2Buffer[];
extern __xdata __at (EP3_ADDR) uint8_t Ep3Buffer[];
// clang-format on

#define LINE_CODING_SIZE 7

// Default line coding: 57600 8N1
__xdata uint8_t LineCodingA[LINE_CODING_SIZE] = {0x00, 0xe1, 0x00, 0x00, 0x00, 0x00, 0x08};
__xdata uint8_t LineCodingB[LINE_CODING_SIZE] = {0x00, 0xe1, 0x00, 0x00, 0x00, 0x00, 0x08};

// Which CDC interface a pending SET/GET_LINE_CODING targets (set in EP0 setup)
__data uint8_t cdcSetupIface = 0;

// ---- Port A (DATA / EP2) state ----
volatile __xdata uint8_t USBByteCountA = 0;
volatile __xdata uint8_t USBBufOutPointA = 0;
volatile __bit UpPointABusy = 0;
volatile __xdata uint8_t controlLineStateA = 0;
__xdata uint8_t usbWritePointerA = 0;

// ---- Port B (CMD / EP3) state ----
volatile __xdata uint8_t USBByteCountB = 0;
volatile __xdata uint8_t USBBufOutPointB = 0;
volatile __bit UpPointBBusy = 0;
volatile __xdata uint8_t controlLineStateB = 0;
__xdata uint8_t usbWritePointerB = 0;

void delayMicroseconds(__data uint16_t us);

void USBInit(void) {
  USBDeviceCfg();
  USBDeviceEndPointCfg();
  USBDeviceIntCfg();
  UEP0_T_LEN = 0;
  UEP1_T_LEN = 0;
  UEP2_T_LEN = 0;
  UEP3_T_LEN = 0;
}

void resetCDCParameters(void) {
  USBByteCountA = 0;
  UpPointABusy = 0;
  USBByteCountB = 0;
  UpPointBBusy = 0;
}

// ---- CDC class request handlers (routed by cdcSetupIface) ------------------
void setLineCodingHandler(void) {
  __xdata uint8_t *lc = (cdcSetupIface == INTERFACE_ID_CDCB_CCI) ? LineCodingB : LineCodingA;
  __data uint8_t n = (LINE_CODING_SIZE <= USB_RX_LEN) ? LINE_CODING_SIZE : USB_RX_LEN;
  for (__data uint8_t i = 0; i < n; i++)
    lc[i] = Ep0Buffer[i];
}

uint16_t getLineCodingHandler(void) {
  __xdata uint8_t *lc = (cdcSetupIface == INTERFACE_ID_CDCB_CCI) ? LineCodingB : LineCodingA;
  for (__data uint8_t i = 0; i < LINE_CODING_SIZE; i++)
    Ep0Buffer[i] = lc[i];
  return LINE_CODING_SIZE;
}

void setControlLineStateHandler(void) {
  if (cdcSetupIface == INTERFACE_ID_CDCB_CCI) {
    controlLineStateB = Ep0Buffer[2];
    return;
  }
  controlLineStateA = Ep0Buffer[2];

  // DATA port: 1200bps + DTR deasserted -> jump to ROM bootloader (auto-flash).
#if defined(CH552)
  if (((controlLineStateA & 0x01) == 0) &&
      (*((__xdata uint32_t *)LineCodingA) == 1200)) {
    USB_CTRL = 0;
    EA = 0;
    TMOD = 0;
    delayMicroseconds(50000);
    delayMicroseconds(50000);
    __asm__("lcall #0x3800");
    while (1)
      ;
  }
#endif
}

// =================== Port A (DATA / EP2) ===================
static uint8_t A_wait_busy_clear(void) {
  __data uint16_t w = 0;
  while (UpPointABusy) {
    w++;
    delayMicroseconds(5);
    if (w >= 50000)
      return 0;
  }
  return 1;
}

bool CdcA_connected(void) { return controlLineStateA > 0; }

void CdcA_flush(void) {
  if (!UpPointABusy && usbWritePointerA > 0) {
    __data uint8_t ic = USB_INT_EN;
    USB_INT_EN &= ~bUIE_TRANSFER;
    UEP2_T_LEN = usbWritePointerA;
    UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
    UpPointABusy = 1;
    USB_INT_EN = ic;
    usbWritePointerA = 0;
  }
}

uint8_t CdcA_write(__data char c) {
  if (controlLineStateA > 0) {
    while (true) {
      if (A_wait_busy_clear() == 0)
        return 0;
      if (usbWritePointerA < CDC_DATA_PKT) {
        Ep2Buffer[CDC_IN_OFFSET + usbWritePointerA] = c;
        usbWritePointerA++;
        return 1;
      } else {
        CdcA_flush();
      }
    }
  }
  return 0;
}

uint8_t CdcA_available(void) { return USBByteCountA; }

char CdcA_read(void) {
  if (USBByteCountA == 0)
    return 0;
  __data char d = Ep2Buffer[USBBufOutPointA];
  USBBufOutPointA++;
  USBByteCountA--;
  if (USBByteCountA == 0)
    UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_R_RES | UEP_R_RES_ACK;
  return d;
}

void USB_EP2_IN(void) {
  UEP2_T_LEN = 0;
  UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_NAK;
  UpPointABusy = 0;
}

void USB_EP2_OUT(void) {
  if (U_TOG_OK) {
    USBByteCountA = USB_RX_LEN;
    USBBufOutPointA = 0;
    if (USBByteCountA)
      UEP2_CTRL = UEP2_CTRL & ~MASK_UEP_R_RES | UEP_R_RES_NAK;
  }
}

// =================== Port B (CMD / EP3) ===================
static uint8_t B_wait_busy_clear(void) {
  __data uint16_t w = 0;
  while (UpPointBBusy) {
    w++;
    delayMicroseconds(5);
    if (w >= 50000)
      return 0;
  }
  return 1;
}

bool CdcB_connected(void) { return controlLineStateB > 0; }

void CdcB_flush(void) {
  if (!UpPointBBusy && usbWritePointerB > 0) {
    __data uint8_t ic = USB_INT_EN;
    USB_INT_EN &= ~bUIE_TRANSFER;
    UEP3_T_LEN = usbWritePointerB;
    UEP3_CTRL = UEP3_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_ACK;
    UpPointBBusy = 1;
    USB_INT_EN = ic;
    usbWritePointerB = 0;
  }
}

uint8_t CdcB_write(__data char c) {
  if (controlLineStateB > 0) {
    while (true) {
      if (B_wait_busy_clear() == 0)
        return 0;
      if (usbWritePointerB < CDC_DATA_PKT) {
        Ep3Buffer[CDC_IN_OFFSET + usbWritePointerB] = c;
        usbWritePointerB++;
        return 1;
      } else {
        CdcB_flush();
      }
    }
  }
  return 0;
}

void CdcB_print(const char *__xdata s) {
  while (*s)
    CdcB_write(*s++);
}

uint8_t CdcB_available(void) { return USBByteCountB; }

char CdcB_read(void) {
  if (USBByteCountB == 0)
    return 0;
  __data char d = Ep3Buffer[USBBufOutPointB];
  USBBufOutPointB++;
  USBByteCountB--;
  if (USBByteCountB == 0)
    UEP3_CTRL = UEP3_CTRL & ~MASK_UEP_R_RES | UEP_R_RES_ACK;
  return d;
}

void USB_EP3_IN(void) {
  UEP3_T_LEN = 0;
  UEP3_CTRL = UEP3_CTRL & ~MASK_UEP_T_RES | UEP_T_RES_NAK;
  UpPointBBusy = 0;
}

void USB_EP3_OUT(void) {
  if (U_TOG_OK) {
    USBByteCountB = USB_RX_LEN;
    USBBufOutPointB = 0;
    if (USBByteCountB)
      UEP3_CTRL = UEP3_CTRL & ~MASK_UEP_R_RES | UEP_R_RES_NAK;
  }
}
