#ifndef __DUTA_CONFIG_H__
#define __DUTA_CONFIG_H__

// Shared build-time configuration for both the sketch and the src/ modules.

// ===== Board selection =====
// Pick exactly one (or pass -DBOARD_xxx as a build flag). Default: WeAct CH552.
// To add a board, copy src/boards/custom.h and select BOARD_CUSTOM.
//#define BOARD_GENERIC_CH552
//#define BOARD_CUSTOM
#if !defined(BOARD_WEACT_CH552) && !defined(BOARD_GENERIC_CH552) && !defined(BOARD_CUSTOM)
#define BOARD_WEACT_CH552
#endif
#include "board.h" // provides RELAY*/LED*/OLED* pins for the selected board

// ===== Features =====
// SSD1306 OLED mirroring the DATA-port RX (pins come from the board). Set to 0
// if no panel is wired.
#define ENABLE_OLED 1

// Parity on the DATA UART (CH552 9-bit mode). Rarely needed; modern serial is
// 8N1. A board may opt in by `#define PARITY_SUPPORT 1` in its header; default
// is lean 8N1. Advertised to the app via the INFO 'parity' capability bit.
#ifndef PARITY_SUPPORT
#define PARITY_SUPPORT 0
#endif

#endif
