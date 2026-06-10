#include <assert.h>
#include <stdio.h>
#include "Arduino.h"
#include "protocol.h"
#include "duta_io.h"   // descriptor type (board.h includes this first)
int pin_level[64], pin_pwm[64], pin_mode_[64], awres;

// board declares the tables...
#define DUTA_HAVE_INPUTS
static const duta_io duta_outputs[] = {
  { SKRIT_CTRL_IO, 4, "Relay 1", DUTA_ACTIVE_LOW, 0 },
  { SKRIT_CTRL_PWM,   5, "Aux LED", 0, 0 },
};
static const duta_io duta_inputs[] = {
  { SKRIT_IN_ANALOG, 30, "LDR", 0, 0 },
};
#include "duta_io_arduino.h"  // ...then main.cpp pulls in the driver

int main(void){
  duta_io_begin();
  assert(awres==10);
  assert(pin_level[4]==HIGH);                 // relay active-low: off => HIGH
  duta_io_out_set(0,0,1); assert(pin_level[4]==LOW && duta_io_out_get(0,0)==1);
  uint8_t t; const char*n; duta_io_out_desc(0,0,&t,&n);
  assert(t==SKRIT_CTRL_IO && n[0]=='R');
  assert(duta_io_pwm_set(0,1,700)==1 && pin_pwm[5]==700 && duta_io_pwm_get(0,1)==700);
  assert(duta_io_out_get(0,1)==1);            // duty>0 => lit
  assert(duta_io_pwm_set(0,0,500)==0);        // reject pwm on a relay
  assert(duta_io_rgb_count(0,1)==0);          // no rgb on this board
  assert(duta_io_rgb_set(0,1,SKRIT_RGB_ALL,1,2,3)==0);
  assert(DUTA_N_INPUTS==1 && duta_io_in_get(0,0)==512);
  uint8_t it; const char*in; duta_io_in_desc(0,0,&it,&in);
  assert(it==SKRIT_IN_ANALOG && in[0]=='L');
  assert(DUTA_N_OUTPUTS==2);
  printf("duta_io table driver OK (nout=%d nin=%d)\n", DUTA_N_OUTPUTS, DUTA_N_INPUTS);
  return 0;
}
