// Host test for the Arduino-side runtime provisioning layer (DUTA_PROVISION):
// the mcu ∩ board resolver, the current-table reader, validated writes, and the
// persistence load round-trip. Uses the mock Arduino.h + a mock board.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "Arduino.h"
#include "protocol.h"
#include "duta_io.h"
#include "duta_pincap.h"
int pin_level[64], pin_pwm[64], pin_mode_[64], awres, awfreq;

// ---- mock board: compiled default (3 outputs) + mcu map + board overlay -----
// Pin 9 is a Pico-style FIXED onboard LED: in the compiled default but never
// offerable; a provisioned table may KEEP it as PWM, never repurpose it.
static const duta_io duta_outputs[] = {
    {SKRIT_CTRL_IO, 4, "Relay 1", 0, 0},
    {SKRIT_CTRL_PWM, 5, "Aux LED", 0, 0},
    {SKRIT_CTRL_PWM, 9, "Onboard LED", 0, 0},
};
static const duta_pin duta_mcu_pins[] = {
    {4, DUTA_CAP_DIGITAL | DUTA_CAP_PWM, DUTA_PIN_FREE, DUTA_NO_BUS},
    {5, DUTA_CAP_DIGITAL | DUTA_CAP_PWM, DUTA_PIN_FREE, DUTA_NO_BUS},
    {6, DUTA_CAP_DIGITAL | DUTA_CAP_PWM, DUTA_PIN_CAUTION, DUTA_NO_BUS}, // strapping
    {7, DUTA_CAP_DIGITAL, DUTA_PIN_FORBIDDEN, DUTA_NO_BUS},             // flash -> hidden
    {8, DUTA_CAP_DIGITAL | DUTA_CAP_PWM, DUTA_PIN_FREE, DUTA_NO_BUS},   // dual-use LED
    {9, DUTA_CAP_DIGITAL | DUTA_CAP_PWM, DUTA_PIN_FREE, DUTA_NO_BUS},   // fixed LED
};
#define DUTA_MCU_NPINS ((uint8_t)(sizeof duta_mcu_pins / sizeof duta_mcu_pins[0]))
#define DUTA_BROKEN_OUT_ALL 1
static const duta_pin_use duta_board_uses[] = {
    {8, DUTA_USE_DUAL, "onboard LED"},
    {9, DUTA_USE_FIXED, "onboard LED"},
};
#define DUTA_USES_N 2
#define DUTA_PROVISION
#include "duta_io_arduino.h"

int main(void) {
  int16_t pin; uint8_t caps, warn, bus; const char *nm;

  // resolver: offerable pins are 4,5,6,8 (7 forbidden, 9 FIXED -> hidden). total = 4.
  uint8_t total = duta_io_pin_caps(0, 0, &pin, &caps, &warn, &bus, &nm);
  assert(total == 4 && pin == 4 && warn == SKRIT_PIN_CLEAN && (caps & DUTA_CAP_PWM));
  duta_io_pin_caps(0, 2, &pin, &caps, &warn, &bus, &nm); // 3rd offerable = pin 6 (caution)
  assert(pin == 6 && warn == SKRIT_PIN_WARN);
  duta_io_pin_caps(0, 3, &pin, &caps, &warn, &bus, &nm); // 4th = pin 8 (dual-use LED)
  assert(pin == 8 && warn == SKRIT_PIN_WARN && nm[0] == 'o');

  // boot with nothing stored -> the compiled default stays active
  duta_io_begin();
  assert(duta_tbl_n == 3 && duta_tbl[0].pin == 4);
  uint8_t t, fl; uint16_t arg;
  assert(duta_io_config_get(0, 0, &t, &pin, &fl, &arg, &nm) == 3 && t == SKRIT_CTRL_IO && pin == 4);

  // the FIXED pin may be KEPT in its compiled role, never repurposed
  {
    uint8_t bad9 = 0xFF;
    uint8_t keep[] = {1, SKRIT_CTRL_PWM, 9, 0, 0, 0, 0, 3, 'L', 'E', 'D'}; // keep as PWM -> OK
    assert(duta_io_config_set(0, keep, (uint8_t)sizeof keep, &bad9) == SKRIT_ST_OK);
    uint8_t flip[] = {1, SKRIT_CTRL_IO, 9, 0, 0, 0, 0, 0}; // repurpose as IO -> reject
    assert(duta_io_config_set(0, flip, (uint8_t)sizeof flip, &bad9) == SKRIT_ST_BADARGS && bad9 == 0);
  }

  // write a new 1-row table {PWM, pin 6, "Fan"} -> OK, persisted
  uint8_t bad = 0xFF;
  uint8_t row[] = {1, SKRIT_CTRL_PWM, 6, 0, 0, 0, 0, 3, 'F', 'a', 'n'};
  assert(duta_io_config_set(0, row, (uint8_t)sizeof row, &bad) == SKRIT_ST_OK);
  // an off-menu pin (forbidden 7) is rejected with its row index
  uint8_t row2[] = {1, SKRIT_CTRL_IO, 7, 0, 0, 0, 0, 0};
  assert(duta_io_config_set(0, row2, (uint8_t)sizeof row2, &bad) == SKRIT_ST_BADARGS && bad == 0);

  // a fresh boot reloads the persisted table
  duta_io_load();
  assert(duta_tbl_n == 1 && duta_tbl[0].type == SKRIT_CTRL_PWM && duta_tbl[0].pin == 6);
  assert(strcmp(duta_tbl[0].name, "Fan") == 0);

  // reset reverts to the compiled default on the next boot
  uint8_t rst[1] = {SKRIT_CONFIG_RESET};
  assert(duta_io_config_set(0, rst, 1, &bad) == SKRIT_ST_OK);
  duta_tbl = duta_outputs; duta_tbl_n = 3; // simulate a power-cycle (static init state)
  duta_io_load();
  assert(duta_tbl_n == 3 && duta_tbl[0].pin == 4);

  printf("provisioning resolver/config/load OK (menu=%u pins)\n", total);
  return 0;
}
