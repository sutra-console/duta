/*
 * Duta bootloader extension: exit DFU back to the application on a host command.
 *
 * The host kicks the bootloader out of DFU by opening the UF2/serial CDC at
 * 1200 baud — the mirror of the app's "open at 1200 / GPREGRET = 0x57" enter-DFU
 * touch. TinyUSB calls tud_cdc_line_coding_cb (a weak hook) on every
 * SET_LINE_CODING; this strong definition overrides it, no main.c patch needed.
 *
 * Reset path mirrors src/main.c: stash the "skip DFU, start the app" marker in
 * the retained double-reset RAM word, clear any DFU magic in GPREGRET, reset.
 * On reboot the bootloader sees a valid app + the marker and jumps straight in.
 */
#include "tusb.h"
#include "nrf.h"

// Must match the values in src/main.c (retained across a soft reset).
#define DUTA_DFU_DBL_RESET_MEM 0x20007F7C // DFU_DBL_RESET_MEM
#define DUTA_DFU_DBL_RESET_APP 0x4ee5677e // DFU_DBL_RESET_APP ("start the app")

#define DUTA_EXIT_BAUD 1200

void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const* coding)
{
  (void) itf;
  if (coding->bit_rate == DUTA_EXIT_BAUD)
  {
    *((uint32_t*) DUTA_DFU_DBL_RESET_MEM) = DUTA_DFU_DBL_RESET_APP; // next boot -> app
    NRF_POWER->GPREGRET = 0;                                        // clear DFU magic
    NVIC_SystemReset();
  }
}
