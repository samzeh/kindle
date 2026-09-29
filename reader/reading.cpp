#include "reading.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerifItalic12pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifBoldItalic12pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <string.h>
#include <vector>

#include "board_config.h"
#include "books.h"
#include "epd.h"
#include "layout.h"
#include "screens.h"

// Page turns use the no-flash refresh, which leaves faint ghosting over time.
// Set this to N to do a full (flashing) refresh every N turns; 0 = never.
static const uint8_t FULL_REFRESH_EVERY = 0;

static const int16_t SCREEN_W = 480;

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

ReadingAction readingHitTest(int16_t x) {
  return x < SCREEN_W / 3 ? READ_PREV : READ_NEXT;
}

uint32_t readingProgressPercent(uint8_t book) {
  if (book >= BOOK_COUNT) return 0;
  if (book != currentBook || pageStarts.empty()) return 0;
  uint32_t len = (uint32_t)strlen(BOOKS[book].text);
  if (len == 0) return 0;
  return (uint32_t)((uint64_t)pageStarts[currentPage].offset * 100 / len);
}

void readingShow(bool fullRefresh) {
  if (!layout) readingOpenBook(currentBook);
  unsigned long t0 = millis();
  PagePos next = layout->layoutPage(pageStarts[currentPage], true);
  if (currentPage + 1 == pageStarts.size() && !layout->isEnd(next)) {
    pageStarts.push_back(next);
  }
  if (fullRefresh) epdShowFull(appCanvas().getBuffer());
  else epdShowPartial(appCanvas().getBuffer());
  Serial.printf("page %u shown in %lu ms (%s)\n", (unsigned)currentPage + 1,
                millis() - t0, fullRefresh ? "full" : "partial");
}

void readingTurnPage(int delta) {
  if (delta > 0 && currentPage + 1 >= pageStarts.size()) return;  // last page
  if (delta < 0 && currentPage == 0) return;
  currentPage += delta;
  bool full = FULL_REFRESH_EVERY > 0 && ++turnsSinceFullRefresh >= FULL_REFRESH_EVERY;
  if (full) turnsSinceFullRefresh = 0;
  readingShow(full);
}

void readingTap(int16_t x, int16_t y) {
  (void)y;
  switch (readingHitTest(x)) {
    case READ_PREV: readingTurnPage(-1); break;
    case READ_NEXT: readingTurnPage(1); break;
    default: break;
  }
}
