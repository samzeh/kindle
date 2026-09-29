// Minimal FT6336 touch driver using the hardware I2C peripheral (Wire).
#pragma once
#include <Arduino.h>

void touchBegin();

// Returns true once per new press (not repeatedly while the finger stays
// down), with the first touch point's raw panel coordinates.
bool touchGetTap(uint16_t &x, uint16_t &y);
