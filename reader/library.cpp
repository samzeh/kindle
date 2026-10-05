#include "library.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <stdio.h>  // snprintf, for the footer and list progress labels
#include <string.h>

#include "catalog.h"
#include "cover.h"
#include "epd.h"
#include "reading.h"
#include "screens.h"
#include "store.h"

static const uint16_t INK = 0x0000;
static const uint16_t PAPER = 0xFFFF;

static const int16_t SCREEN_W = 480;
static const int16_t SCREEN_H = 800;
static const int16_t MARGIN_X = 24;
static const int16_t HEADER_H = 48;
static const int16_t BODY_BOTTOM = 760;
static const int16_t TOGGLE_X = 384;  // header, x >= this toggles the view

// Grid: three columns of COVER_W covers, spread across the text margins,
// three rows deep. Each cell is the cover and up to two lines of title,
// kept within the cover's width.
static const int16_t GRID_COLS = 3, GRID_ROWS = 3;
static const int16_t GRID_PER_PAGE = GRID_COLS * GRID_ROWS;
static const int16_t GRID_GAP_X = (SCREEN_W - 2 * MARGIN_X - GRID_COLS * COVER_W) / (GRID_COLS - 1);  // 36
static const int16_t GRID_TOP = 56;
static const int16_t GRID_CAPTION_H = 40;  // two lines of 9pt below the cover
static const int16_t GRID_CELL_H = COVER_H + GRID_CAPTION_H;  // 220
static const int16_t GRID_ROW_PITCH = GRID_CELL_H + 8;

static int16_t gridColX(int16_t col) {
  return MARGIN_X + col * (COVER_W + GRID_GAP_X);
}

static int16_t gridRowY(int16_t row) {
  return GRID_TOP + row * GRID_ROW_PITCH;
}

// List: six rows, each a thumbnail, then the title (up to two lines) and a
// progress line like the one at the bottom of a book's pages.
static const int16_t LIST_PER_PAGE = 6;
static const int16_t LIST_TOP = 50;
static const int16_t LIST_ROW_H = 118;
static const int16_t LIST_TEXT_X = 112;   // MARGIN_X + THUMB_W + 16
static const int16_t LIST_BAR_W = 280;
static const int16_t LIST_BAR_DY = 90;    // the progress line, from the row's top
// Text column right edge is the page's right margin, matching MARGIN_X.
static const int16_t LIST_TEXT_MAX_W = SCREEN_W - MARGIN_X - LIST_TEXT_X;  // 344

static uint8_t view = LIB_VIEW_GRID;
static uint8_t page = 0;
static bool viewLoaded = false;

uint8_t libraryPageCount(uint8_t v) {
  int16_t perPage = v == LIB_VIEW_GRID ? GRID_PER_PAGE : LIST_PER_PAGE;
  int16_t pages = (int16_t)((catalogCount() + perPage - 1) / perPage);
  return (uint8_t)(pages < 1 ? 1 : pages > 255 ? 255 : pages);
}

static LibraryHit gridHitTest(int16_t x, int16_t y, uint8_t p) {
  for (int16_t row = 0; row < GRID_ROWS; row++) {
    if (y < gridRowY(row) || y >= gridRowY(row) + GRID_CELL_H) continue;
    for (int16_t col = 0; col < GRID_COLS; col++) {
      if (x < gridColX(col) || x >= gridColX(col) + COVER_W) continue;
      uint16_t index = (uint16_t)(p * GRID_PER_PAGE + row * GRID_COLS + col);
      if (index >= catalogCount()) return { LIB_NONE, 0 };
      return { LIB_OPEN_BOOK, index };
    }
  }
  return { LIB_NONE, 0 };
}

static LibraryHit listHitTest(int16_t x, int16_t y, uint8_t p) {
  (void)x;  // a row is hit anywhere across its width
  if (y < LIST_TOP) return { LIB_NONE, 0 };
  int16_t row = (y - LIST_TOP) / LIST_ROW_H;
  if (row < 0 || row >= LIST_PER_PAGE) return { LIB_NONE, 0 };
  uint16_t index = (uint16_t)(p * LIST_PER_PAGE + row);
  if (index >= catalogCount()) return { LIB_NONE, 0 };
  return { LIB_OPEN_BOOK, index };
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
  return listHitTest(x, y, p);
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

// Draws text on at most two lines within a box `w` wide starting at x:
// as many whole words as fit on the first, the rest on the second,
// shortened with "..." if it is still too long. Nothing is drawn outside
// the box. Centred in the box, or left-aligned at x.
static void drawTwoLines(Adafruit_GFX &gfx, const char *text, const GFXfont *font, int16_t x,
                         int16_t w, int16_t baseline, int16_t lineGap, bool centred) {
  char line[64];
  size_t fit = 0, end = 0;
  gfx.setFont(font);
  while (text[end]) {
    size_t next = end;
    while (text[next] == ' ') next++;
    while (text[next] && text[next] != ' ') next++;
    if (next >= sizeof(line)) break;
    memcpy(line, text, next);
    line[next] = '\0';
    int16_t x1, y1;
    uint16_t lw, lh;
    gfx.getTextBounds(line, 0, 0, &x1, &y1, &lw, &lh);
    if (lw > w) break;
    fit = end = next;
  }
  const char *rest = text;
  if (fit > 0) {  // else not even one word fits: shorten it on the first line
    memcpy(line, text, fit);
    line[fit] = '\0';
    rest = text + fit;
    while (*rest == ' ') rest++;
  }
  char buf[64];
  const char *lines[2] = { fit > 0 ? line : rest, fit > 0 ? rest : "" };
  for (int16_t i = 0; i < 2 && *lines[i]; i++) {
    int16_t y = baseline + i * lineGap;
    if (centred) {
      drawCentredText(gfx, lines[i], font, x, w, y);
    } else {
      truncateToWidth(gfx, lines[i], font, w, buf, sizeof(buf));
      gfx.setCursor(x, y);
      gfx.print(buf);
    }
  }
}

// Where a line of text in `font` goes so its capitals are centred on y.
static int16_t baselineCentredOn(const GFXfont &font, int16_t y) {
  const GFXglyph &cap = font.glyph['H' - font.first];
  return y - cap.yOffset - (cap.height - 1) / 2;
}

static void drawGrid(Adafruit_GFX &gfx) {
  for (int16_t cell = 0; cell < GRID_PER_PAGE; cell++) {
    uint16_t index = (uint16_t)(page * GRID_PER_PAGE + cell);
    if (index >= catalogCount()) break;
    const BookInfo &book = catalogBook(index);
    int16_t x = gridColX(cell % GRID_COLS);
    int16_t y = gridRowY(cell / GRID_COLS);
    drawCover(gfx, book, x, y, COVER_W, COVER_H);
    gfx.setTextColor(INK);
    drawTwoLines(gfx, book.title, &FreeSerif9pt7b, x, COVER_W, y + COVER_H + 16, 18, true);
  }
}

// The list thumbnail is THUMB_W (72px) wide, below COVER_TEXT_MIN_W, so for
// a book without cover art drawCover renders it as a grey block only (no
// placeholder title). The row's title, drawn here for every book, is the
// only place it appears.
static void drawList(Adafruit_GFX &gfx) {
  for (uint8_t row = 0; row < LIST_PER_PAGE; row++) {
    uint16_t index = (uint16_t)(page * LIST_PER_PAGE + row);
    if (index >= catalogCount()) break;
    const BookInfo &book = catalogBook(index);
    int16_t top = LIST_TOP + row * LIST_ROW_H;

    drawCover(gfx, book, MARGIN_X, top + 5, THUMB_W, THUMB_H);

    gfx.setTextColor(INK);
    drawTwoLines(gfx, book.title, &FreeSerifBold12pt7b, LIST_TEXT_X, LIST_TEXT_MAX_W, top + 30, 26,
                 false);

    // The progress line, as at the bottom of a book's pages: a thin track,
    // and a thicker part for how far through.
    uint32_t pct = readingProgressPercent(index);
    int16_t barY = top + LIST_BAR_DY;
    gfx.drawFastHLine(LIST_TEXT_X, barY, LIST_BAR_W, INK);
    int16_t done = (int16_t)(LIST_BAR_W * pct / 100);
    if (done > 0) gfx.fillRect(LIST_TEXT_X, barY - 1, done, 3, INK);

    // The percentage, right-aligned at x = SCREEN_W - MARGIN_X and centred
    // on the line: measured, since "0%" and "100%" are different widths.
    char label[8];
    snprintf(label, sizeof(label), "%u%%", (unsigned)pct);
    gfx.setFont(&FreeSerif12pt7b);
    int16_t x1, y1;
    uint16_t w, h;
    gfx.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
    gfx.setCursor(SCREEN_W - MARGIN_X - (int16_t)w - x1, baselineCentredOn(FreeSerif12pt7b, barY));
    gfx.print(label);
  }
}

// Loads the persisted view on first use. Called from both libraryShow and
// libraryTap: on the device, appBegin's initial libraryShow always runs
// before any tap can arrive, but a caller that taps first (as the host tests
// do, driving libraryTap directly without ever calling appBegin) must not
// have that first tap's LIB_TOGGLE_VIEW clobbered by a load that runs after
// the toggle already flipped `view`.
static void ensureViewLoaded() {
  if (!viewLoaded) {
    view = storeLoadView();
    viewLoaded = true;
  }
}

void libraryShow(bool fullRefresh) {
  ensureViewLoaded();
  GFXcanvas1 &gfx = appCanvas();
  gfx.fillScreen(PAPER);
  gfx.setTextWrap(false);
  drawHeader(gfx);
  if (catalogCount() == 0) {
    gfx.setTextColor(INK);
    drawCentredText(gfx, "No books yet", &FreeSerifBold12pt7b, 0, SCREEN_W, SCREEN_H / 2 - 14);
    drawCentredText(gfx, "Add .epub files to the /books folder", &FreeSerif12pt7b, 0, SCREEN_W,
                    SCREEN_H / 2 + 18);
  } else if (view == LIB_VIEW_GRID) {
    drawGrid(gfx);
  } else {
    drawList(gfx);
  }
  drawFooter(gfx);
  appRefresh(fullRefresh);
}

void libraryTurnPage(int delta) {
  uint8_t pages = libraryPageCount(view);
  if (delta > 0 && page + 1 >= pages) return;
  if (delta < 0 && page == 0) return;
  page = (uint8_t)(page + delta);
  libraryShow(false);
}

void libraryTap(int16_t x, int16_t y) {
  ensureViewLoaded();
  LibraryHit hit = libraryHitTest(x, y, page, view);
  switch (hit.action) {
    case LIB_OPEN_BOOK:
      if (readingOpenBook(hit.book)) appGoTo(SCREEN_READING);
      else libraryShow(false);  // could not be read: back to the shelf
      break;
    case LIB_PREV_PAGE: libraryTurnPage(-1); break;
    case LIB_NEXT_PAGE: libraryTurnPage(1); break;
    case LIB_TOGGLE_VIEW:
      view = view == LIB_VIEW_GRID ? LIB_VIEW_LIST : LIB_VIEW_GRID;
      storeSaveView(view);
      page = 0;
      libraryShow(false);
      break;
    default: break;
  }
}
