#include "app.h"
#include <Adafruit_GFX.h>

#include "board_config.h"
#include "epd.h"
#include "library.h"
#include "reading.h"
#include "screens.h"

// Full-screen buffer (48 KB): each screen is drawn in RAM, then sent in one
// go. Its memory layout matches the panel's, so it is sent unchanged.
static GFXcanvas1 canvas(EPD_NATIVE_WIDTH, EPD_NATIVE_HEIGHT);
static Screen current = SCREEN_LIBRARY;

GFXcanvas1 &appCanvas() {
  return canvas;
}

static void showCurrent(bool fullRefresh) {
  switch (current) {
    case SCREEN_LIBRARY: libraryShow(fullRefresh); break;
    case SCREEN_READING: readingShow(fullRefresh); break;
  }
}

void appGoTo(Screen s) {
  current = s;
  showCurrent(true);
}

void appBegin() {
  canvas.setRotation(SCREEN_ROTATION);
  appGoTo(SCREEN_LIBRARY);
}

void appTurnPage(int delta) {
  switch (current) {
    case SCREEN_LIBRARY: libraryTurnPage(delta); break;
    case SCREEN_READING: readingTurnPage(delta); break;
  }
}

// Taps arrive in raw touch coordinates. Normalise once here, so every screen
// hit-tests in plain 480 x 800 screen space.
void appTap(uint16_t rawX, uint16_t rawY) {
  int16_t x = TOUCH_FLIP_X ? (int16_t)(TOUCH_WIDTH - 1 - rawX) : (int16_t)rawX;
  int16_t y = (int16_t)rawY;
  Serial.printf("tap at %d,%d\n", x, y);
  switch (current) {
    case SCREEN_LIBRARY: libraryTap(x, y); break;
    case SCREEN_READING: readingTap(x, y); break;
  }
}
