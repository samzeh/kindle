// The reading screen: which page of which book is showing, and what a tap
// does. No hardware code, so the simulator runs it too.
#pragma once
#include <stdint.h>

enum ReadingAction : uint8_t {
  READ_NONE,
  READ_PREV,
  READ_NEXT,
};

// Which action a tap at screen x maps to. Pure, so it is tested on the host.
ReadingAction readingHitTest(int16_t x);

// Switches to book `index`, at the start (or its saved position once
// persistence exists).
void readingOpenBook(uint8_t index);

// How far through `book` the reader has got, 0-100. Books never opened read 0.
uint32_t readingProgressPercent(uint8_t book);

// Draws the current page.
void readingShow(bool fullRefresh);

// A tap in screen coordinates.
void readingTap(int16_t x, int16_t y);

// +1 = next page, -1 = previous page.
void readingTurnPage(int delta);
