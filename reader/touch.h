// Minimal FT6336 touch driver using the hardware I2C peripheral (Wire).
#pragma once
#include <Arduino.h>

void touchBegin();

// What the panel did since the last poll.
enum TouchPhase : uint8_t {
  TOUCH_NONE,  // nothing is happening
  TOUCH_DOWN,  // a finger has just landed; x, y say where
  TOUCH_MOVE,  // a finger is still down; x, y are where it is now
  TOUCH_UP,    // the finger has just lifted; x, y are not set
};

// Polls the panel once and says what changed. One I2C read per call, so call
// it once per loop and act on what comes back.
//
// TOUCH_MOVE repeats for as long as a finger stays down, whether or not it
// has actually moved: the chip reports a position, not a delta.
TouchPhase touchPoll(uint16_t &x, uint16_t &y);
