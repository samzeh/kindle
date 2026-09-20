#include "hal/Display.h"

#include <SPI.h>

Display gDisplay;

bool Display::begin() {
  // SPI is initialised here rather than letting GxEPD2 do it, because the SD
  // card shares the bus and needs MISO wired up.
  SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);

  panel_.epd2.selectSPI(SPI, SPISettings(20000000, MSBFIRST, SPI_MODE0));
  panel_.init(115200, true, 2, false);
  panel_.setRotation(DISPLAY_ROTATION);
  panel_.setFullWindow();
  panel_.setTextWrap(false);

  ready_ = true;
  return true;
}

void Display::clearBuffer() {
  panel_.fillScreen(GxEPD_WHITE);
}

void Display::present(Refresh mode) {
  if (!ready_) return;

  bool partial;
  switch (mode) {
    case Refresh::Full:
      partial = false;
      break;
    case Refresh::Partial:
      partial = true;
      break;
    case Refresh::Auto:
    default:
      partial = sincefull_ < FULL_REFRESH_INTERVAL;
      break;
  }

  panel_.setFullWindow();
  panel_.display(partial);

  sincefull_ = partial ? sincefull_ + 1 : 0;
}

void Display::presentRegion(int16_t x, int16_t y, int16_t w, int16_t h) {
  if (!ready_) return;

  panel_.setPartialWindow(x, y, w, h);
  panel_.display(true);
  panel_.setFullWindow();
  sincefull_++;
}

void Display::clearScreen() {
  if (!ready_) return;

  panel_.setFullWindow();
  panel_.fillScreen(GxEPD_WHITE);
  panel_.display(false);
  sincefull_ = 0;
}

void Display::hibernate() {
  if (!ready_) return;
  panel_.hibernate();
}
