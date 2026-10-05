// The reading screen: which page of which book is showing, and what a tap
// does. No hardware code, so the simulator runs it too.
#pragma once
#include <stdint.h>

enum ReadingAction : uint8_t {
  READ_NONE,
  READ_PREV,
  READ_NEXT,
  READ_SHOW_CONTROLS,
  READ_HIDE_CONTROLS,
  READ_BACK_TO_LIBRARY,
  READ_OPEN_SETTINGS,
};

// What a tap means, given whether the control bar is up. Pure, so it is
// tested on the host.
ReadingAction readingHitTest(int16_t x, int16_t y, bool controlsVisible);

// Opens book `index` (catalog order) at its saved position, or at its first
// page: the cover, if it has one.
// The first time a book is opened its text is converted and its pages are
// counted (a few seconds on the device, behind a progress screen); after
// that it opens straight away. False if the book cannot be read.
bool readingOpenBook(uint16_t index);

// How far through a book the reader has got, 0-100. Books never opened read 0.
uint32_t readingProgressPercent(uint16_t index);

// Draws the current page.
void readingShow(bool fullRefresh);

// A tap in screen coordinates.
void readingTap(int16_t x, int16_t y);

// +1 = next page, -1 = previous page.
void readingTurnPage(int delta);

// The open book's current page (0-based), its page count, and the title of
// the chapter the current page is in ("" if the book has no chapters).
// Page 0 is the book's cover, if it has one.
uint32_t readingCurrentPage();
uint32_t readingPageCount();
const char *readingChapterTitle();
// Where a page starts in the text (the cover counts as 0).
uint32_t readingPageOffset(uint32_t page);

// Bump when anything changes where pages break (fonts, margins, layout
// rules), so every book's page count is redone.
static const uint16_t LAYOUT_VERSION = 9;
