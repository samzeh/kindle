#include "reading.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerifItalic12pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifBoldItalic12pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <string.h>
#include <vector>

#include "board_config.h"
#include "books.h"
#include "cover.h"
#include "epd.h"
#include "layout.h"
#include "screens.h"

// Page turns use the no-flash refresh, which leaves faint ghosting over time.
// Set this to N to do a full (flashing) refresh every N turns; 0 = never.
static const uint8_t FULL_REFRESH_EVERY = 0;

static const int16_t SCREEN_W = 480;
static const int16_t BAR_H = 64;    // control bar height, drawn over the page top
static const int16_t BACK_W = 80;   // width of the back arrow's tap target

static const PageFonts fonts = {
  &FreeSerif12pt7b,
  &FreeSerifItalic12pt7b,
  &FreeSerifBold12pt7b,
  &FreeSerifBoldItalic12pt7b,
  &FreeSerif9pt7b,
};

static PageLayout *layout = nullptr;
static uint8_t currentBook = 0;
static std::vector<PagePos> pageStarts;  // grows as pages are visited
static size_t currentPage = 0;
static uint8_t turnsSinceFullRefresh = 0;
static bool controlsVisible = false;

void readingOpenBook(uint8_t index) {
  if (index >= BOOK_COUNT) return;
  currentBook = index;
  delete layout;
  layout = new PageLayout(appCanvas(), BOOKS[index].text, fonts);
  pageStarts.clear();
  pageStarts.push_back({ 0, false });
  currentPage = 0;
  turnsSinceFullRefresh = 0;
}

ReadingAction readingHitTest(int16_t x, int16_t y, bool barUp) {
  if (barUp) {
    if (y < BAR_H && x < BACK_W) return READ_BACK_TO_LIBRARY;
    return READ_HIDE_CONTROLS;
  }
  if (x < SCREEN_W / 3) return READ_PREV;
  if (x < 2 * SCREEN_W / 3) return READ_SHOW_CONTROLS;
  return READ_NEXT;
}

uint32_t readingProgressPercent(uint8_t book) {
  if (book >= BOOK_COUNT) return 0;
  if (book != currentBook || pageStarts.empty()) return 0;
  uint32_t len = (uint32_t)strlen(BOOKS[book].text);
  if (len == 0) return 0;
  return (uint32_t)((uint64_t)pageStarts[currentPage].offset * 100 / len);
}

// Draws the control bar over the top BAR_H pixels of the current page: a
// back arrow to the library, and the book's title centred in the rest of the
// bar (drawCentredText also truncates it, so a long title cannot run under
// the arrow or off the edge).
static void drawControlBar(Adafruit_GFX &gfx) {
  const uint16_t INK = 0x0000, PAPER = 0xFFFF;
  gfx.fillRect(0, 0, SCREEN_W, BAR_H, PAPER);
  gfx.drawFastHLine(0, BAR_H - 1, SCREEN_W, INK);

  // Back arrow: a triangle with a shaft, pointing left.
  gfx.fillTriangle(24, 32, 38, 22, 38, 42, INK);
  gfx.drawFastHLine(38, 32, 22, INK);

  gfx.setTextColor(INK);
  drawCentredText(gfx, BOOKS[currentBook].title, &FreeSerifBold9pt7b, BACK_W,
                  SCREEN_W - BACK_W, 38);
}

void readingShow(bool fullRefresh) {
  if (!layout) readingOpenBook(currentBook);
  unsigned long t0 = millis();
  PagePos next = layout->layoutPage(pageStarts[currentPage], true);
  if (currentPage + 1 == pageStarts.size() && !layout->isEnd(next)) {
    pageStarts.push_back(next);
  }
  if (controlsVisible) drawControlBar(appCanvas());
  if (fullRefresh) epdShowFull(appCanvas().getBuffer());
  else epdShowPartial(appCanvas().getBuffer());
  Serial.printf("page %u shown in %lu ms (%s)\n", (unsigned)currentPage + 1,
                millis() - t0, fullRefresh ? "full" : "partial");
}

void readingTurnPage(int delta) {
  if (delta > 0 && currentPage + 1 >= pageStarts.size()) return;  // last page
  if (delta < 0 && currentPage == 0) return;
  controlsVisible = false;  // after the guards: a rejected turn changes nothing
  currentPage += delta;
  bool full = FULL_REFRESH_EVERY > 0 && ++turnsSinceFullRefresh >= FULL_REFRESH_EVERY;
  if (full) turnsSinceFullRefresh = 0;
  readingShow(full);
}

void readingTap(int16_t x, int16_t y) {
  switch (readingHitTest(x, y, controlsVisible)) {
    case READ_PREV: readingTurnPage(-1); break;
    case READ_NEXT: readingTurnPage(1); break;
    case READ_SHOW_CONTROLS:
      controlsVisible = true;
      readingShow(false);  // a small delta: the fast refresh
      break;
    case READ_HIDE_CONTROLS:
      controlsVisible = false;
      readingShow(false);
      break;
    case READ_BACK_TO_LIBRARY:
      controlsVisible = false;
      appGoTo(SCREEN_LIBRARY);
      break;
    default: break;
  }
}
