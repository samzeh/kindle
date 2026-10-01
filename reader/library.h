// The library: the home screen, showing every book with its cover.
//
// Two views of the same shelf, toggled from the header: a cover grid and a
// list with reading progress.
#pragma once
#include <stdint.h>

enum LibraryView : uint8_t {
  LIB_VIEW_GRID = 0,
  LIB_VIEW_LIST = 1,
};

enum LibraryAction : uint8_t {
  LIB_NONE,
  LIB_OPEN_BOOK,
  LIB_PREV_PAGE,
  LIB_NEXT_PAGE,
  LIB_TOGGLE_VIEW,
};

struct LibraryHit {
  LibraryAction action;
  uint16_t book;  // catalog index; meaningful only when action is LIB_OPEN_BOOK
};

// What a tap at (x, y) means on the given page of the given view. Pure, so it
// is tested on the host.
LibraryHit libraryHitTest(int16_t x, int16_t y, uint8_t page, uint8_t view);

// How many pages the shelf needs in this view.
uint8_t libraryPageCount(uint8_t view);

void libraryShow(bool fullRefresh);
void libraryTap(int16_t x, int16_t y);
void libraryTurnPage(int delta);
