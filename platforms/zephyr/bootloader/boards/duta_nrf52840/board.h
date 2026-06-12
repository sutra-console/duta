/*
 * Duta nRF52840 — UF2 bootloader board variant.
 *
 * Binary-layout-identical to Adafruit's `nice_nano` (same SoftDevice, MBR,
 * bootloader region, 3V3 REGOUT0, blue LED on P0.15) so it can replace the
 * stock nice!nano bootloader via UF2 self-update. The ONLY changes are Duta
 * board identity (USB VID/PID + product/volume strings + Board-ID) and the
 * exit-to-app hook compiled in via board.mk (see duta_exit.c).
 *
 * MIT — based on nice_nano/board.h (c) 2020 Nick Winans.
 */
#ifndef _DUTA_NRF52840_H
#define _DUTA_NRF52840_H

#define UICR_REGOUT0_VALUE UICR_REGOUT0_VOUT_3V3 // run the chip at 3.3V (DO NOT change)

/*------------------------------------------------------------------*/
/* LED — onboard blue LED, same pin as nice!nano / Pro Micro nRF52840
 *------------------------------------------------------------------*/
#define LEDS_NUMBER     1
#define LED_PRIMARY_PIN PINNUM(0, 15)
#define LED_STATE_ON    1

/*------------------------------------------------------------------*/
/* BUTTON — this board has none (the whole point of command-driven DFU)
 *------------------------------------------------------------------*/

//--------------------------------------------------------------------+
// BLE OTA
//--------------------------------------------------------------------+
#define BLEDIS_MANUFACTURER "sutra-console"
#define BLEDIS_MODEL        "Duta nRF52840"

//--------------------------------------------------------------------+
// USB — Duta identity so the board IDs as a Duta even in DFU. VID 0x1209
// (pid.codes, the same family the Duta app uses) so Sutra recognizes it; a
// distinct PID marks "this is the bootloader, not the running app".
//--------------------------------------------------------------------+
#define USB_DESC_VID          0x1209
#define USB_DESC_UF2_PID      0x5DF0
#define USB_DESC_CDC_ONLY_PID 0x5DF0

#define UF2_PRODUCT_NAME      "Duta nRF52840"
#define UF2_VOLUME_LABEL      "DUTA"
#define UF2_BOARD_ID          "nRF52840-duta-v1"
#define UF2_INDEX_URL         "https://github.com/sutra-console/duta"

#endif // _DUTA_NRF52840_H
