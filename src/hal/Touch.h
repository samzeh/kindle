#pragma once
#include <Arduino.h>

#include "config.h"

enum class Gesture : uint8_t {
  None,
  Tap,
  LongPress,
  SwipeLeft,
  SwipeRight,
  SwipeUp,
  SwipeDown,
};

struct TouchEvent {
  Gesture gesture = Gesture::None;
  int16_t x = 0;  // screen coordinates, already rotated
  int16_t y = 0;
};

// FT6336U capacitive controller on hardware I2C.
//
// The vendor demo bit-banged I2C on these pins; using the Wire peripheral at
// 400kHz is both faster and frees the CPU during transfers.
class Touch {
 public:
  bool begin();
  bool present() const { return present_; }

  // True while a finger is on the panel. The controller holds INT low for the
  // duration of a contact, so this costs nothing but a GPIO read.
  bool contact() const { return digitalRead(PIN_TOUCH_INT) == LOW; }

  // Sample the panel and classify gestures. Call from the main loop; returns
  // Gesture::None until a gesture completes on finger lift.
  TouchEvent poll();

  uint32_t lastActivityMs() const { return lastActivity_; }

  // Put the controller in monitor mode so it still pulls INT low on touch
  // while drawing far less current. Call before deep sleep.
  void sleep();

 private:
  bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len);
  bool writeReg(uint8_t reg, uint8_t value);
  bool readPoint(int16_t& x, int16_t& y);
  void mapToScreen(int16_t rawX, int16_t rawY, int16_t& outX, int16_t& outY);

  bool present_ = false;
  bool down_ = false;
  int16_t downX_ = 0, downY_ = 0;
  int16_t lastX_ = 0, lastY_ = 0;
  uint32_t downMs_ = 0;
  uint32_t lastActivity_ = 0;
};

extern Touch gTouch;
