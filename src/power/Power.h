#pragma once
#include <Arduino.h>

#include "config.h"

// Deep sleep and wake.
//
// E-paper holds its image with no power, so sleeping is invisible to the
// reader: the page stays on screen and a touch brings the device back.
class Power {
 public:
  void begin();

  // True when this boot came from deep sleep rather than a cold start, which
  // lets the UI skip the splash and restore straight to the page.
  bool wokeFromTouch() const { return wokeFromTouch_; }

  void noteActivity() { lastActivity_ = millis(); }
  uint32_t idleMs() const { return millis() - lastActivity_; }
  bool shouldSleep() const { return idleMs() >= IDLE_SLEEP_MS; }

  // Does not return. The caller must persist state first.
  [[noreturn]] void deepSleep();

 private:
  bool wokeFromTouch_ = false;
  uint32_t lastActivity_ = 0;
};

extern Power gPower;
