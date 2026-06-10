#ifndef DUTA_PICO_BOARD_H
#define DUTA_PICO_BOARD_H
// Board dispatcher for the pico platform. The PlatformIO env passes a -DBOARD_*
// flag (see platformio.ini); each board lives in its own leaf header under
// boards/, pulling its silicon truth from mcu/<chip>.h. Default is the Raspberry
// Pi Pico (RP2040). See BOARDS.md for the platform → mcu → board layout.

#if !defined(BOARD_PICO) && !defined(BOARD_PICO2)
#define BOARD_PICO
#endif

#if defined(BOARD_PICO)
#include "boards/raspberrypi_pico.h"
#elif defined(BOARD_PICO2)
#include "boards/raspberrypi_pico2.h"
#endif

#ifndef BOARD_NAME
#error "No board selected — define BOARD_PICO / BOARD_PICO2 (see platformio.ini)."
#endif

#endif // DUTA_PICO_BOARD_H
