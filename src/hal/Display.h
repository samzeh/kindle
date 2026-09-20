#pragma once
#include <Arduino.h>
#include <GxEPD2_BW.h>

#include "config.h"

using EpdDriver = GxEPD2_426_GDEQ0426T82;
using EpdPanel = GxEPD2_BW<EpdDriver, EpdDriver::HEIGHT>;

// Owns the 48KB framebuffer and the partial/full refresh policy.
//
// The whole screen fits in one buffer, so drawing is direct rather than
// GxEPD2's paged picture loop: draw into the buffer, then call present().
class Display {
 public:
  enum class Refresh {
    Auto,     // partial, but promote to full every FULL_REFRESH_INTERVAL
    Partial,  // ~0.42s, no flashing
    Full,     // ~3.5s, flashes, clears ghosting
  };

  bool begin();

  EpdPanel& gfx() { return panel_; }
  int16_t width() const { return SCREEN_W; }
  int16_t height() const { return SCREEN_H; }

  // Fill the buffer white. Does not touch the panel.
  void clearBuffer();

  // Push the framebuffer to the panel.
  void present(Refresh mode = Refresh::Auto);

  // Push only a sub-rectangle. Always a partial refresh; used for status bar
  // and button feedback so a page turn is not needed.
  void presentRegion(int16_t x, int16_t y, int16_t w, int16_t h);

  // Blank the panel and reset the ghosting counter.
  void clearScreen();

  // Power the panel down. The image persists. Call before deep sleep.
  void hibernate();

  void forceFullNext() { sincefull_ = FULL_REFRESH_INTERVAL; }

 private:
  EpdPanel panel_{EpdDriver(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY)};
  uint16_t sincefull_ = 0;
  bool ready_ = false;
};

extern Display gDisplay;
