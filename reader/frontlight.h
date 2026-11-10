// The front light: LEDs on PIN_FRONTLIGHT, dimmed with one of the ESP32's
// LEDC PWM channels.
//
// Hardware-only, like epd.cpp and touch.cpp: the simulator stands in for it
// (simulator/host_platform.cpp), where setting a brightness only prints it.
// A Mac has no light to turn on, so this is the one setting that can be
// checked on the device alone.
#pragma once
#include <stdint.h>

void frontlightBegin();

// 0 (off) to 100. The percentage is perceptual, not a raw duty cycle: see
// frontlight.cpp.
void frontlightSet(uint8_t percent);
