#ifndef DUTA_PICO_BOARD_H
#define DUTA_PICO_BOARD_H
// Target dispatcher for the pico platform. The PlatformIO env passes a -DBOARD_*
// flag (see platformio.ini); each flag selects a TARGET under targets/, our Duta
// build on a specific vendored board. Default is the Raspberry Pi Pico (RP2040).
// See BOARDS.md for the platform → mcu → board → target layering.

#if !defined(BOARD_PICO) && !defined(BOARD_PICO2)
#define BOARD_PICO
#endif

#if defined(BOARD_PICO)
#include "targets/duta_pico.h"
#elif defined(BOARD_PICO2)
#include "targets/duta_pico2.h"
#endif

#ifndef BOARD_NAME
#error "No target selected: define BOARD_PICO / BOARD_PICO2 (see platformio.ini)."
#endif

#endif // DUTA_PICO_BOARD_H
