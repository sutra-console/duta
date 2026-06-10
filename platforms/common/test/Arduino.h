#pragma once
#include <stdint.h>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
extern int pin_level[64], pin_pwm[64], pin_mode_[64], awres;
static inline void pinMode(int p,int m){pin_mode_[p]=m;}
static inline void digitalWrite(int p,int v){pin_level[p]=v;}
static inline int digitalRead(int p){return pin_level[p];}
static inline void analogWrite(int p,int v){pin_pwm[p]=v;}
static inline int analogRead(int p){return p==30?512:0;}
static inline void analogWriteResolution(int b){awres=b;}
