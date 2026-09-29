#include "library.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <string.h>

#include "books.h"
#include "cover.h"
#include "epd.h"
#include "reading.h"
#include "screens.h"

static const uint16_t INK = 0x0000;
static const uint16_t PAPER = 0xFFFF;

static const int16_t SCREEN_W = 480;
static const int16_t SCREEN_H = 800;
static const int16_t MARGIN_X = 24;
static const int16_t HEADER_H = 48;
static const int16_t BODY_BOTTOM = 760;
static const int16_t TOGGLE_X = 384;  // header, x >= this toggles the view

// Grid: two columns of COVER_W with a 24px gutter, flush to the text margins.
static const int16_t GRID_PER_PAGE = 4;
static const int16_t GRID_COL_X[2] = { 24, 252 };
static const int16_t GRID_ROW_Y[2] = { 54, 408 };
static const int16_t GRID_CELL_H = 350;  // cover 306 + gap 6 + title 20 + author 18

static uint8_t view = LIB_VIEW_GRID;
static uint8_t page = 0;

uint8_t libraryPageCount(uint8_t v) {
  int16_t perPage = v == LIB_VIEW_GRID ? GRID_PER_PAGE : 6;
  return (uint8_t)((BOOK_COUNT + perPage - 1) / perPage);
}

static LibraryHit gridHitTest(int16_t x, int16_t y, uint8_t p) {
  for (uint8_t row = 0; row < 2; row++) {
    if (y < GRID_ROW_Y[row] || y >= GRID_ROW_Y[row] + GRID_CELL_H) continue;
    for (uint8_t col = 0; col < 2; col++) {
      if (x < GRID_COL_X[col] || x >= GRID_COL_X[col] + COVER_W) continue;
      uint8_t index = (uint8_t)(p * GRID_PER_PAGE + row * 2 + col);
      if (index >= BOOK_COUNT) return { LIB_NONE, 0 };
      return { LIB_OPEN_BOOK, index };
    }
  }
  return { LIB_NONE, 0 };
}

LibraryHit libraryHitTest(int16_t x, int16_t y, uint8_t p, uint8_t v) {
  if (y < HEADER_H) {
    return x >= TOGGLE_X ? LibraryHit{ LIB_TOGGLE_VIEW, 0 } : LibraryHit{ LIB_NONE, 0 };
  }
  if (y >= BODY_BOTTOM) {
    if (x < SCREEN_W / 3) return { LIB_PREV_PAGE, 0 };
    if (x >= 2 * SCREEN_W / 3) return { LIB_NEXT_PAGE, 0 };
    return { LIB_NONE, 0 };
  }
  if (v == LIB_VIEW_GRID) return gridHitTest(x, y, p);
  return { LIB_NONE, 0 };  // list view arrives in Task 6
}

static void drawHeader(Adafruit_GFX &gfx) {
  gfx.setTextColor(INK);
  gfx.setFont(&FreeSerifBold9pt7b);
  gfx.setCursor(MARGIN_X, 30);
  gfx.print("My Library");
  // The view toggle, drawn as the name of the view you would switch to.
  gfx.setFont(&FreeSerif9pt7b);
  gfx.setCursor(TOGGLE_X + 8, 30);
  gfx.print(view == LIB_VIEW_GRID ? "List" : "Grid");
  gfx.drawFastHLine(0, HEADER_H - 1, SCREEN_W, INK);
}

static void drawFooter(Adafruit_GFX &gfx) {
  uint8_t pages = libraryPageCount(view);
  if (pages <= 1) return;
  char label[16];
  snprintf(label, sizeof(label), "%u / %u", (unsigned)page + 1, (unsigned)pages);
  gfx.setTextColor(INK);
  drawCentredText(gfx, label, &FreeSerif9pt7b, 0, SCREEN_W, SCREEN_H - 14);
  if (page > 0) {
    gfx.setCursor(MARGIN_X, SCREEN_H - 14);
    gfx.print("<");
  }
  if (page + 1 < pages) {
    gfx.setCursor(SCREEN_W - MARGIN_X - 10, SCREEN_H - 14);
    gfx.print(">");
  }
}

static void drawGrid(Adafruit_GFX &gfx) {
  for (uint8_t cell = 0; cell < GRID_PER_PAGE; cell++) {
    uint8_t index = (uint8_t)(page * GRID_PER_PAGE + cell);
    if (index >= BOOK_COUNT) break;
    int16_t x = GRID_COL_X[cell % 2];
    int16_t y = GRID_ROW_Y[cell / 2];
    drawCover(gfx, BOOKS[index], x, y, COVER_W, COVER_H);
    gfx.setTextColor(INK);
    drawCentredText(gfx, BOOKS[index].title, &FreeSerifBold9pt7b, x, COVER_W,
                    y + COVER_H + 20);
    drawCentredText(gfx, BOOKS[index].author, &FreeSerif9pt7b, x, COVER_W,
                    y + COVER_H + 38);
  }
}

void libraryShow(bool fullRefresh) {
  GFXcanvas1 &gfx = appCanvas();
  gfx.fillScreen(PAPER);
  gfx.setTextWrap(false);
  drawHeader(gfx);
  if (view == LIB_VIEW_GRID) drawGrid(gfx);
  drawFooter(gfx);
  if (fullRefresh) epdShowFull(gfx.getBuffer());
  else epdShowPartial(gfx.getBuffer());
}

void libraryTurnPage(int delta) {
  uint8_t pages = libraryPageCount(view);
  if (delta > 0 && page + 1 >= pages) return;
  if (delta < 0 && page == 0) return;
  page = (uint8_t)(page + delta);
  libraryShow(true);
}

void libraryTap(int16_t x, int16_t y) {
  LibraryHit hit = libraryHitTest(x, y, page, view);
  switch (hit.action) {
    case LIB_OPEN_BOOK:
      readingOpenBook(hit.book);
      appGoTo(SCREEN_READING);
      break;
    case LIB_PREV_PAGE: libraryTurnPage(-1); break;
    case LIB_NEXT_PAGE: libraryTurnPage(1); break;
    case LIB_TOGGLE_VIEW:
      view = view == LIB_VIEW_GRID ? LIB_VIEW_LIST : LIB_VIEW_GRID;
      page = 0;
      libraryShow(true);
      break;
    default: break;
  }
}
