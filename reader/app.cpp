#include "app.h"
#include <Adafruit_GFX.h>

#include "board_config.h"
#include "busy.h"
#include "catalog.h"
#include "epd.h"
#include "library.h"
#include "reading.h"
#include "screens.h"
#include "storage.h"
#include "store.h"

// Full-screen buffer (48 KB): each screen is drawn in RAM, then sent in one
// go. Its memory layout matches the panel's, so it is sent unchanged.
static GFXcanvas1 canvas(EPD_NATIVE_WIDTH, EPD_NATIVE_HEIGHT);
static Screen current = SCREEN_LIBRARY;

GFXcanvas1 &appCanvas() {
  return canvas;
}

void appRefresh(bool full) {
  static bool panelReady = false;
  if (full || !panelReady) epdShowFull(canvas.getBuffer());
  else epdShowPartial(canvas.getBuffer());
  panelReady = true;
}

Screen appCurrentScreen() {
  return current;
}

static void showCurrent(bool fullRefresh) {
  switch (current) {
    case SCREEN_LIBRARY: libraryShow(fullRefresh); break;
    case SCREEN_READING: readingShow(fullRefresh); break;
  }
}

void appGoTo(Screen s) {
  current = s;
  showCurrent(false);
}

// Shown while new books' titles and covers are read at startup.
static void onImportProgress(uint16_t done, uint16_t total, const char *name, void *) {
  char line[48];
  snprintf(line, sizeof(line), "Adding book %u of %u", (unsigned)done + 1, (unsigned)total);
  busyShow(line, name, total ? done * 100 / total : -1);
}

void appBegin() {
  storeBegin();
  canvas.setRotation(SCREEN_ROTATION);
  if (storageBegin()) catalogScan(onImportProgress, nullptr);
  else Serial.println("app: no book storage; the library will be empty");
  // appRefresh makes this the power-up full refresh, unless a progress
  // screen for new books already did it.
  current = SCREEN_LIBRARY;
  showCurrent(false);
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
