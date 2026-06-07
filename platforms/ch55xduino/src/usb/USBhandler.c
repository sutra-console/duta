/*
  USB control + endpoint dispatch for the Duta dual-CDC composite (CH552).
  Adapted from the ch55xduino single-CDC user example, extended to 4 interfaces
  / endpoints EP1..EP4.
*/

#include "USBhandler.h"
#include "USBconstant.h"

void setLineCodingHandler(void);
uint16_t getLineCodingHandler(void);
void setControlLineStateHandler(void);
void resetCDCParameters(void);
extern __data uint8_t cdcSetupIface;

// clang-format off
__xdata __at (EP0_ADDR) uint8_t Ep0Buffer[8];            // EP0 ctrl; EP1 notify DMA aliases here; EP4 notify is fixed at +0x40
__xdata __at (EP2_ADDR) uint8_t Ep2Buffer[CDC_EP_BUFSZ]; // EP2 data A: OUT 0..31, IN 64..95
__xdata __at (EP3_ADDR) uint8_t Ep3Buffer[CDC_EP_BUFSZ]; // EP3 data B: OUT 0..31, IN 64..95
// clang-format on

#if (EP3_ADDR + CDC_EP_BUFSZ) > USER_USB_RAM
#error "Dual-CDC endpoint buffers exceed USER_USB_RAM (use the user266 board option)"
#endif

__data uint16_t SetupLen;
__data uint8_t SetupReq;
volatile __xdata uint8_t UsbConfig;

const __code uint8_t *__data pDescr;

inline void NOP_Process(void) {}

void USB_EP0_SETUP(void) {
  __data uint8_t len = USB_RX_LEN;
  if (len == (sizeof(USB_SETUP_REQ))) {
    SetupLen = ((uint16_t)UsbSetupBuf->wLengthH << 8) | (UsbSetupBuf->wLengthL);
    len = 0;
    SetupReq = UsbSetupBuf->bRequest;
    if ((UsbSetupBuf->bRequestType & USB_REQ_TYP_MASK) != USB_REQ_TYP_STANDARD) {
      switch ((UsbSetupBuf->bRequestType & USB_REQ_TYP_MASK)) {
      case USB_REQ_TYP_CLASS: {
        switch (SetupReq) {
        case GET_LINE_CODING:
          cdcSetupIface = UsbSetupBuf->wIndexL;
          len = getLineCodingHandler();
          break;
        case SET_CONTROL_LINE_STATE:
          cdcSetupIface = UsbSetupBuf->wIndexL;
          setControlLineStateHandler();
          break;
        case SET_LINE_CODING:
          cdcSetupIface = UsbSetupBuf->wIndexL; // data arrives in EP0_OUT
          break;
        default:
          len = 0xFF;
          break;
        }
        break;
      }
      default:
        len = 0xFF;
        break;
      }
    } else { // Standard request
      switch (SetupReq) {
      case USB_GET_DESCRIPTOR:
        switch (UsbSetupBuf->wValueH) {
        case 1: // Device
          pDescr = DeviceDescriptor;
          len = DeviceDescriptorLen;
          break;
        case 2: // Configuration
          pDescr = ConfigDescriptor;
          len = ConfigDescriptorLen;
          break;
        case 3: // String
          switch (UsbSetupBuf->wValueL) {
          case 0:
            pDescr = LanguageDescriptor;
            break;
          case STR_IDX_MANUFACTURER:
            pDescr = (__code uint8_t *)ManufacturerDescriptor;
            break;
          case STR_IDX_PRODUCT:
            pDescr = (__code uint8_t *)ProductDescriptor;
            break;
          case STR_IDX_SERIAL:
            pDescr = (__code uint8_t *)SerialDescriptor;
            break;
          case STR_IDX_CDCA_NAME:
            pDescr = (__code uint8_t *)CDCADescriptor;
            break;
          case STR_IDX_CDCB_NAME:
            pDescr = (__code uint8_t *)CDCBDescriptor;
            break;
          default:
            len = 0xFF;
            break;
          }
          if (len != 0xFF)
            len = pDescr[0];
          break;
        default:
          len = 0xFF;
          break;
        }
        if (len != 0xFF) {
          if (SetupLen > len)
            SetupLen = len;
          len = SetupLen >= DEFAULT_ENDP0_SIZE ? DEFAULT_ENDP0_SIZE : SetupLen;
          for (__data uint8_t i = 0; i < len; i++)
            Ep0Buffer[i] = pDescr[i];
          SetupLen -= len;
          pDescr += len;
        }
        break;
      case USB_SET_ADDRESS:
        SetupLen = UsbSetupBuf->wValueL;
        break;
      case USB_GET_CONFIGURATION:
        Ep0Buffer[0] = UsbConfig;
        if (SetupLen >= 1)
          len = 1;
        break;
      case USB_SET_CONFIGURATION:
        UsbConfig = UsbSetupBuf->wValueL;
        break;
      case USB_GET_INTERFACE:
        break;
      case USB_SET_INTERFACE:
        break;
      case USB_CLEAR_FEATURE:
        if ((UsbSetupBuf->bRequestType & 0x1F) == USB_REQ_RECIP_DEVICE) {
          if ((((uint16_t)UsbSetupBuf->wValueH << 8) | UsbSetupBuf->wValueL) == 0x01) {
            // remote wakeup not supported
            len = 0xFF;
          } else {
            len = 0xFF;
          }
        } else if ((UsbSetupBuf->bRequestType & USB_REQ_RECIP_MASK) == USB_REQ_RECIP_ENDP) {
          switch (UsbSetupBuf->wIndexL) {
          case 0x84:
            UEP4_CTRL = UEP4_CTRL & ~(bUEP_T_TOG | MASK_UEP_T_RES) | UEP_T_RES_NAK;
            break;
          case 0x83:
            UEP3_CTRL = UEP3_CTRL & ~(bUEP_T_TOG | MASK_UEP_T_RES) | UEP_T_RES_NAK;
            break;
          case 0x03:
            UEP3_CTRL = UEP3_CTRL & ~(bUEP_R_TOG | MASK_UEP_R_RES) | UEP_R_RES_ACK;
            break;
          case 0x82:
            UEP2_CTRL = UEP2_CTRL & ~(bUEP_T_TOG | MASK_UEP_T_RES) | UEP_T_RES_NAK;
            break;
          case 0x02:
            UEP2_CTRL = UEP2_CTRL & ~(bUEP_R_TOG | MASK_UEP_R_RES) | UEP_R_RES_ACK;
            break;
          case 0x81:
            UEP1_CTRL = UEP1_CTRL & ~(bUEP_T_TOG | MASK_UEP_T_RES) | UEP_T_RES_NAK;
            break;
          default:
            len = 0xFF;
            break;
          }
        } else {
          len = 0xFF;
        }
        break;
      case USB_SET_FEATURE:
        len = 0xFF; // endpoint halt not needed
        break;
      case USB_GET_STATUS:
        Ep0Buffer[0] = 0x00;
        Ep0Buffer[1] = 0x00;
        len = (SetupLen >= 2) ? 2 : SetupLen;
        break;
      default:
        len = 0xFF;
        break;
      }
    }
  } else {
    len = 0xFF;
  }
  if (len == 0xFF) {
    SetupReq = 0xFF;
    UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
  } else if (len <= DEFAULT_ENDP0_SIZE) {
    UEP0_T_LEN = len;
    UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
  } else {
    UEP0_T_LEN = 0;
    UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;
  }
}

void USB_EP0_IN(void) {
  switch (SetupReq) {
  case USB_GET_DESCRIPTOR: {
    __data uint8_t len = SetupLen >= DEFAULT_ENDP0_SIZE ? DEFAULT_ENDP0_SIZE : SetupLen;
    for (__data uint8_t i = 0; i < len; i++)
      Ep0Buffer[i] = pDescr[i];
    SetupLen -= len;
    pDescr += len;
    UEP0_T_LEN = len;
    UEP0_CTRL ^= bUEP_T_TOG;
  } break;
  case USB_SET_ADDRESS:
    USB_DEV_AD = USB_DEV_AD & bUDA_GP_BIT | SetupLen;
    UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    break;
  default:
    UEP0_T_LEN = 0;
    UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    break;
  }
}

void USB_EP0_OUT(void) {
  if (SetupReq == SET_LINE_CODING) {
    if (U_TOG_OK) {
      setLineCodingHandler();
      UEP0_T_LEN = 0;
      UEP0_CTRL |= UEP_R_RES_ACK | UEP_T_RES_ACK;
    }
  } else {
    UEP0_T_LEN = 0;
    UEP0_CTRL |= UEP_R_RES_ACK | UEP_T_RES_NAK;
  }
}

#pragma save
#pragma nooverlay
void USBInterrupt(void) {
  if (UIF_TRANSFER) {
    __data uint8_t callIndex = USB_INT_ST & MASK_UIS_ENDP;
    switch (USB_INT_ST & MASK_UIS_TOKEN) {
    case UIS_TOKEN_OUT:
      switch (callIndex) {
      case 0: EP0_OUT_Callback(); break;
      case 2: EP2_OUT_Callback(); break;
      case 3: EP3_OUT_Callback(); break;
      default: break;
      }
      break;
    case UIS_TOKEN_SOF:
      break;
    case UIS_TOKEN_IN:
      switch (callIndex) {
      case 0: EP0_IN_Callback(); break;
      case 2: EP2_IN_Callback(); break;
      case 3: EP3_IN_Callback(); break;
      default: break;
      }
      break;
    case UIS_TOKEN_SETUP:
      switch (callIndex) {
      case 0: EP0_SETUP_Callback(); break;
      default: break;
      }
      break;
    }
    UIF_TRANSFER = 0;
  }

  if (UIF_BUS_RST) {
    UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    UEP1_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK;
    UEP2_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK | UEP_R_RES_ACK;
    UEP3_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK | UEP_R_RES_ACK;
    UEP4_CTRL = UEP_T_RES_NAK; // EP4 cannot auto-toggle

    USB_DEV_AD = 0x00;
    UIF_SUSPEND = 0;
    UIF_TRANSFER = 0;
    UIF_BUS_RST = 0;
    UsbConfig = 0;
    resetCDCParameters();
  }

  if (UIF_SUSPEND) {
    UIF_SUSPEND = 0;
    if (!(USB_MIS_ST & bUMS_SUSPEND)) {
      USB_INT_FG = 0xFF;
    }
  }
}
#pragma restore

void USBDeviceCfg(void) {
  USB_CTRL = 0x00;
  USB_CTRL &= ~bUC_HOST_MODE;
  USB_CTRL |= bUC_DEV_PU_EN | bUC_INT_BUSY | bUC_DMA_EN;
  USB_DEV_AD = 0x00;
  USB_CTRL &= ~bUC_LOW_SPEED;
  UDEV_CTRL &= ~bUD_LOW_SPEED;
  UDEV_CTRL = bUD_PD_DIS;
  UDEV_CTRL |= bUD_PORT_EN;
}

void USBDeviceIntCfg(void) {
  USB_INT_EN |= bUIE_SUSPEND;
  USB_INT_EN |= bUIE_TRANSFER;
  USB_INT_EN |= bUIE_BUS_RST;
  USB_INT_FG |= 0x1F;
  IE_USB = 1;
  EA = 1;
}

void USBDeviceEndPointCfg(void) {
  UEP0_DMA = (uint16_t)Ep0Buffer;
  UEP1_DMA = (uint16_t)Ep0Buffer; // EP1 notify never transmits; alias onto EP0
  UEP2_DMA = (uint16_t)Ep2Buffer;
  UEP3_DMA = (uint16_t)Ep3Buffer;
  // EP4 buffer is hardware-fixed at Ep0Buffer + 0x40 (no UEP4_DMA register).

  // EP2 + EP3: single-buffer, IN+OUT enabled.
  UEP2_3_MOD = 0xCC; // bUEP2_TX_EN|bUEP2_RX_EN|bUEP3_TX_EN|bUEP3_RX_EN
  // EP1 TX + EP4 TX enabled (both are interrupt-IN notify endpoints).
  UEP4_1_MOD = 0x44; // bUEP1_TX_EN|bUEP4_TX_EN

  UEP1_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK;
  UEP2_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK | UEP_R_RES_ACK;
  UEP3_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK | UEP_R_RES_ACK;
  UEP4_CTRL = UEP_T_RES_NAK;

  UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
}
