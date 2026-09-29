// E-reader, step 1: render a book-like page of text and turn pages by touch.
// Tap the left third of the screen to go back, anywhere else to go forward.
//
// Requires the Adafruit GFX library.

#include <Adafruit_GFX.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerifItalic12pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifBoldItalic12pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <vector>

#include "board_config.h"
#include "epd.h"
#include "layout.h"
#include "touch.h"
#include "sample_text.h"

// Page turns use the no-flash refresh, which leaves faint ghosting over time.
// Set this to N to do a full (flashing) refresh every N turns; 0 = never.
static const uint8_t FULL_REFRESH_EVERY = 0;

// Full-screen buffer (48 KB): each page is drawn in RAM, then sent in one go.
// Its memory layout matches the panel's, so it is sent unchanged.
GFXcanvas1 canvas(EPD_NATIVE_WIDTH, EPD_NATIVE_HEIGHT);

static const PageFonts fonts = {
  &FreeSerif12pt7b,
  &FreeSerifItalic12pt7b,
  &FreeSerifBold12pt7b,
  &FreeSerifBoldItalic12pt7b,
  &FreeSerif9pt7b,
};
PageLayout layout(canvas, SAMPLE_TEXT, fonts);

std::vector<PagePos> pageStarts;  // grows as pages are visited
size_t currentPage = 0;
uint8_t turnsSinceFullRefresh = 0;

void showPage(bool fullRefresh) {
  unsigned long t0 = millis();
  PagePos next = layout.layoutPage(pageStarts[currentPage], true);
  if (currentPage + 1 == pageStarts.size() && !layout.isEnd(next)) {
    pageStarts.push_back(next);
  }
  if (fullRefresh) epdShowFull(canvas.getBuffer());
  else epdShowPartial(canvas.getBuffer());
  Serial.printf("page %u shown in %lu ms (%s)\n", (unsigned)currentPage + 1,
                millis() - t0, fullRefresh ? "full" : "partial");
}

void turnPage(int delta) {
  if (delta > 0 && currentPage + 1 >= pageStarts.size()) return;  // last page
  if (delta < 0 && currentPage == 0) return;
  currentPage += delta;
  bool full = FULL_REFRESH_EVERY > 0 && ++turnsSinceFullRefresh >= FULL_REFRESH_EVERY;
  if (full) turnsSinceFullRefresh = 0;
  showPage(full);
}

void setup() {
  Serial.begin(115200);

  pinMode(PIN_FRONTLIGHT, OUTPUT);
  digitalWrite(PIN_FRONTLIGHT, HIGH);

  epdBegin();
  canvas.setRotation(SCREEN_ROTATION);

  touchBegin();

  pageStarts.push_back({ 0, false });
  showPage(true);
}

void loop() {
  // Page turns can also be sent over USB serial: 'n' = next, 'p' = previous.
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'n') turnPage(1);
    if (c == 'p') turnPage(-1);
  }

  uint16_t x, y;
  if (touchGetTap(x, y)) {
    Serial.printf("tap at %u,%u\n", x, y);
    if (TOUCH_FLIP_X) x = TOUCH_WIDTH - 1 - x;
    turnPage(x < TOUCH_WIDTH / 3 ? -1 : 1);
  }
  delay(10);
}
