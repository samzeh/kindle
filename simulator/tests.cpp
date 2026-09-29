// Host tests for the reader's pure arithmetic: hit-testing, pagination and
// cover scaling. Run with `make test`.
#include <cstdio>
#include "Arduino.h"
#include "books.h"
#include "cover.h"
#include "epd.h"
#include "library.h"
#include "reading.h"
#include "screens.h"

SerialPort Serial;

// ---- Stand-ins for the screen driver (reader/epd.h) ----
void epdBegin() {}
void epdShowFull(const uint8_t *) {}
void epdShowPartial(const uint8_t *) {}
void epdSleep() {}

// ---- Tiny test framework ----
static int failures = 0;

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);              \
      failures++;                                                         \
    }                                                                     \
  } while (0)

#define CHECK_EQ(actual, expected)                                        \
  do {                                                                    \
    long a_ = (long)(actual), e_ = (long)(expected);                      \
    if (a_ != e_) {                                                       \
      printf("FAIL %s:%d  %s: got %ld, want %ld\n", __FILE__, __LINE__,   \
             #actual, a_, e_);                                            \
      failures++;                                                         \
    }                                                                     \
  } while (0)

static void testReadingHitTest() {
  CHECK_EQ(readingHitTest(0), READ_PREV);
  CHECK_EQ(readingHitTest(159), READ_PREV);
  CHECK_EQ(readingHitTest(160), READ_NEXT);
  CHECK_EQ(readingHitTest(479), READ_NEXT);
}

static void testBookTable() {
  CHECK_EQ(BOOK_COUNT, 4);
  for (uint8_t i = 0; i < BOOK_COUNT; i++) {
    CHECK(BOOKS[i].title != nullptr && BOOKS[i].title[0] != '\0');
    CHECK(BOOKS[i].author != nullptr && BOOKS[i].author[0] != '\0');
    CHECK(BOOKS[i].text != nullptr);
    // Long enough to paginate to more than one page.
    CHECK(strlen(BOOKS[i].text) > 1200);
  }
  // Titles are distinct, so the library never shows two identical cells.
  for (uint8_t i = 0; i < BOOK_COUNT; i++)
    for (uint8_t j = i + 1; j < BOOK_COUNT; j++)
      CHECK(strcmp(BOOKS[i].title, BOOKS[j].title) != 0);
}

static void testCoverScaling() {
  // At native size, destination and source indices agree.
  CHECK_EQ(coverSrcIndex(0, COVER_W, COVER_W), 0);
  CHECK_EQ(coverSrcIndex(203, COVER_W, COVER_W), 203);

  // Downscaled to a thumbnail, indices stay inside the source.
  CHECK_EQ(coverSrcIndex(0, 72, COVER_W), 0);
  CHECK_EQ(coverSrcIndex(71, 72, COVER_W), 201);
  CHECK_EQ(coverSrcIndex(0, 108, COVER_H), 0);
  CHECK_EQ(coverSrcIndex(107, 108, COVER_H), 303);

  // Never reads past the end, at any destination size.
  for (int16_t size = 1; size <= COVER_W; size++) {
    CHECK(coverSrcIndex(size - 1, size, COVER_W) < COVER_W);
    CHECK(coverSrcIndex(0, size, COVER_W) >= 0);
  }

  // Bit order is MSB first: bit 0 of the first byte is the leftmost pixel.
  static uint8_t row[COVER_ROW_BYTES * 2] = { 0 };
  row[0] = 0x80;                     // (0,0) is ink
  row[COVER_ROW_BYTES] = 0x01;       // (7,1) is ink
  CHECK(coverBit(row, 0, 0));
  CHECK(!coverBit(row, 1, 0));
  CHECK(coverBit(row, 7, 1));
  CHECK(!coverBit(row, 6, 1));
}

static void testLibraryGridHitTest() {
  // Four books per page in grid view.
  CHECK_EQ(libraryPageCount(LIB_VIEW_GRID), 1);  // BOOK_COUNT == 4

  // Header, right side: toggles the view.
  CHECK_EQ(libraryHitTest(400, 20, 0, LIB_VIEW_GRID).action, LIB_TOGGLE_VIEW);
  // Header, left side: nothing.
  CHECK_EQ(libraryHitTest(100, 20, 0, LIB_VIEW_GRID).action, LIB_NONE);

  // The four cover cells, sampled at their centres.
  LibraryHit topLeft = libraryHitTest(126, 200, 0, LIB_VIEW_GRID);
  CHECK_EQ(topLeft.action, LIB_OPEN_BOOK);
  CHECK_EQ(topLeft.book, 0);

  LibraryHit topRight = libraryHitTest(354, 200, 0, LIB_VIEW_GRID);
  CHECK_EQ(topRight.action, LIB_OPEN_BOOK);
  CHECK_EQ(topRight.book, 1);

  LibraryHit bottomLeft = libraryHitTest(126, 550, 0, LIB_VIEW_GRID);
  CHECK_EQ(bottomLeft.action, LIB_OPEN_BOOK);
  CHECK_EQ(bottomLeft.book, 2);

  LibraryHit bottomRight = libraryHitTest(354, 550, 0, LIB_VIEW_GRID);
  CHECK_EQ(bottomRight.action, LIB_OPEN_BOOK);
  CHECK_EQ(bottomRight.book, 3);

  // The gutter between the columns opens nothing.
  CHECK_EQ(libraryHitTest(240, 200, 0, LIB_VIEW_GRID).action, LIB_NONE);
  // Left of the first column, inside the margin, opens nothing.
  CHECK_EQ(libraryHitTest(10, 200, 0, LIB_VIEW_GRID).action, LIB_NONE);

  // Cell and header boundaries, each pair "last pixel inside" then "first
  // pixel outside".
  CHECK_EQ(libraryHitTest(227, 200, 0, LIB_VIEW_GRID).book, 0);           // col 0 right edge
  CHECK_EQ(libraryHitTest(228, 200, 0, LIB_VIEW_GRID).action, LIB_NONE);
  CHECK_EQ(libraryHitTest(251, 200, 0, LIB_VIEW_GRID).action, LIB_NONE);  // col 1 left edge
  CHECK_EQ(libraryHitTest(252, 200, 0, LIB_VIEW_GRID).book, 1);
  CHECK_EQ(libraryHitTest(455, 200, 0, LIB_VIEW_GRID).book, 1);           // col 1 right edge
  CHECK_EQ(libraryHitTest(456, 200, 0, LIB_VIEW_GRID).action, LIB_NONE);
  CHECK_EQ(libraryHitTest(126, 403, 0, LIB_VIEW_GRID).book, 0);           // row 0 bottom edge
  CHECK_EQ(libraryHitTest(126, 404, 0, LIB_VIEW_GRID).action, LIB_NONE);
  CHECK_EQ(libraryHitTest(126, 407, 0, LIB_VIEW_GRID).action, LIB_NONE);  // row 1 top edge
  CHECK_EQ(libraryHitTest(126, 408, 0, LIB_VIEW_GRID).book, 2);
  CHECK_EQ(libraryHitTest(383, 20, 0, LIB_VIEW_GRID).action, LIB_NONE);   // toggle edge
  CHECK_EQ(libraryHitTest(384, 20, 0, LIB_VIEW_GRID).action, LIB_TOGGLE_VIEW);

  // Footer paging.
  CHECK_EQ(libraryHitTest(50, 780, 0, LIB_VIEW_GRID).action, LIB_PREV_PAGE);
  CHECK_EQ(libraryHitTest(400, 780, 0, LIB_VIEW_GRID).action, LIB_NEXT_PAGE);
  CHECK_EQ(libraryHitTest(240, 780, 0, LIB_VIEW_GRID).action, LIB_NONE);
}

static void testLibraryListHitTest() {
  // Six rows of 118px starting at y = 50.
  LibraryHit first = libraryHitTest(240, 80, 0, LIB_VIEW_LIST);
  CHECK_EQ(first.action, LIB_OPEN_BOOK);
  CHECK_EQ(first.book, 0);

  LibraryHit second = libraryHitTest(240, 200, 0, LIB_VIEW_LIST);
  CHECK_EQ(second.action, LIB_OPEN_BOOK);
  CHECK_EQ(second.book, 1);

  LibraryHit fourth = libraryHitTest(240, 440, 0, LIB_VIEW_LIST);
  CHECK_EQ(fourth.action, LIB_OPEN_BOOK);
  CHECK_EQ(fourth.book, 3);

  // Rows past the end of the shelf open nothing.
  CHECK_EQ(libraryHitTest(240, 560, 0, LIB_VIEW_LIST).action, LIB_NONE);

  // The header and footer behave the same in both views.
  CHECK_EQ(libraryHitTest(400, 20, 0, LIB_VIEW_LIST).action, LIB_TOGGLE_VIEW);
  CHECK_EQ(libraryHitTest(400, 780, 0, LIB_VIEW_LIST).action, LIB_NEXT_PAGE);

  // Four books still fit on one page in list view.
  CHECK_EQ(libraryPageCount(LIB_VIEW_LIST), 1);

  // Row boundaries (rows are 118px starting at y = 50): row 0's last pixel
  // (167) vs row 1's first pixel (168).
  CHECK_EQ(libraryHitTest(240, 167, 0, LIB_VIEW_LIST).book, 0);
  CHECK_EQ(libraryHitTest(240, 168, 0, LIB_VIEW_LIST).book, 1);

  // The last valid row (row 3, book index 3, y 404-521) vs the first row
  // past the shelf (row 4 would be index 4, but BOOK_COUNT == 4).
  CHECK_EQ(libraryHitTest(240, 521, 0, LIB_VIEW_LIST).book, 3);
  CHECK_EQ(libraryHitTest(240, 522, 0, LIB_VIEW_LIST).action, LIB_NONE);
}

static void testDrawCoverPixels() {
  // A synthetic cover: ink at (10,10) and across a band of rows around 200,
  // paper elsewhere. Points are chosen well inside the 1px frame drawCover
  // draws. The band (not a single row) matters for the thumbnail check below:
  // nearest-neighbour downsampling from 306 to 108 rows only samples every
  // ~2.8th source row, so a single inked row can fall in a gap between
  // sampled rows and never appear in the thumbnail even though the bit
  // convention is correct.
  static uint8_t cover[COVER_ROW_BYTES * COVER_H] = { 0 };
  cover[10 * COVER_ROW_BYTES + 10 / 8] |= 0x80 >> (10 % 8);
  for (int16_t y = 195; y <= 205; y++) {
    for (int16_t x = 0; x < COVER_W; x++) {
      cover[y * COVER_ROW_BYTES + x / 8] |= 0x80 >> (x % 8);
    }
  }

  const Book withArt = { "T", "A", "text", cover };
  GFXcanvas1 full(COVER_W, COVER_H);
  full.fillScreen(0xFFFF);
  drawCover(full, withArt, 0, 0, COVER_W, COVER_H);

  // A 1 bit in the source is INK, i.e. a BLACK pixel. If this fails with the
  // paper check below passing, the bit convention is inverted and every cover
  // renders as a photographic negative.
  CHECK(!full.getPixel(10, 10));       // false == black in GFXcanvas1
  CHECK(!full.getPixel(100, 200));     // on the ink row
  CHECK(full.getPixel(100, 150));      // a 0 bit stays white

  // Downscaled to a thumbnail, the ink band still lands as ink.
  GFXcanvas1 thumb(THUMB_W, THUMB_H);
  thumb.fillScreen(0xFFFF);
  drawCover(thumb, withArt, 0, 0, THUMB_W, THUMB_H);
  int16_t inkRow = 200 * THUMB_H / COVER_H;   // nearest-neighbour inverse
  CHECK(!thumb.getPixel(THUMB_W / 2, inkRow));
  CHECK(thumb.getPixel(THUMB_W / 2, 30));     // well outside the band: white

  // The nullptr fallback draws a frame rather than nothing — this is the only
  // path that renders today, since every book has cover == nullptr.
  const Book noArt = { "Title", "Author", "text", nullptr };
  GFXcanvas1 fb(COVER_W, COVER_H);
  fb.fillScreen(0xFFFF);
  drawCover(fb, noArt, 0, 0, COVER_W, COVER_H);
  CHECK(!fb.getPixel(0, 0));                       // frame corner is ink
  CHECK(!fb.getPixel(COVER_W - 1, COVER_H - 1));   // opposite corner too
}

// drawCover's typographic placeholder writes title/author text inside the
// frame only when the box clears the legibility floor. Below it (e.g. the
// list view's 72px thumbnail), the caller draws the caption elsewhere, so
// drawCover must draw the frame alone or the text is stated twice.
static void testDrawCoverPlaceholderLegibility() {
  const Book noArt = { "Title", "Author", "text", nullptr };

  GFXcanvas1 full(COVER_W, COVER_H);
  full.fillScreen(0xFFFF);
  drawCover(full, noArt, 0, 0, COVER_W, COVER_H);
  // At full width (well above the floor), some interior pixel is ink: the
  // placeholder text is drawn.
  bool fullHasInteriorInk = false;
  for (int16_t y = 4; y < COVER_H - 4 && !fullHasInteriorInk; y++)
    for (int16_t x = 4; x < COVER_W - 4; x++)
      if (!full.getPixel(x, y)) { fullHasInteriorInk = true; break; }
  CHECK(fullHasInteriorInk);

  GFXcanvas1 thumb(THUMB_W, THUMB_H);
  thumb.fillScreen(0xFFFF);
  drawCover(thumb, noArt, 0, 0, THUMB_W, THUMB_H);
  CHECK(THUMB_W < COVER_TEXT_MIN_W);  // this test only means something if so
  // Below the floor: frame only, no interior ink.
  bool thumbHasInteriorInk = false;
  for (int16_t y = 1; y < THUMB_H - 1 && !thumbHasInteriorInk; y++)
    for (int16_t x = 1; x < THUMB_W - 1; x++)
      if (!thumb.getPixel(x, y)) { thumbHasInteriorInk = true; break; }
  CHECK(!thumbHasInteriorInk);
  // The frame itself is still drawn.
  CHECK(!thumb.getPixel(0, 0));
  CHECK(!thumb.getPixel(THUMB_W - 1, THUMB_H - 1));
}

// Task 8 rewrites readingProgressPercent to read saved positions for every
// book from NVS, instead of only the currently-showing book having one. This
// test asserts the behaviour that must survive that rewrite -- ranges and
// ordering, never an exact percentage, since the exact value is a function
// of font metrics and page layout, not a contract worth pinning. It sits
// here, after the pure arithmetic tests, because readingOpenBook and
// readingTurnPage draw through the shared appCanvas() singleton and mutate
// reading.cpp's statics.
static void testReadingProgressPercent() {
  // A book that has never been read reports 0.
  CHECK_EQ(readingProgressPercent(2), 0);

  // An out-of-range index reports 0 rather than reading past the table.
  CHECK_EQ(readingProgressPercent(BOOK_COUNT), 0);
  CHECK_EQ(readingProgressPercent(255), 0);

  readingOpenBook(0);
  readingShow(true);
  CHECK_EQ(readingProgressPercent(0), 0);  // start of the book

  readingTurnPage(1);
  readingTurnPage(1);
  uint32_t pct = readingProgressPercent(0);
  CHECK(pct > 0);
  CHECK(pct <= 100);

  // A different book still reports 0 at this moment.
  CHECK_EQ(readingProgressPercent(1), 0);
}

// Every current book has cover == nullptr, so drawCover's placeholder
// already states title and author once inside the frame. The grid's own
// caption band below the cover must stay blank in that case, or the text is
// stated twice. (Coordinates match library.cpp's private grid geometry: the
// top-left cell starts at (24, 54); it is the one cell that stays inside the
// unrotated test canvas without calling appBegin().)
static void testLibraryGridNoDuplicateCaption() {
  libraryShow(false);
  GFXcanvas1 &gfx = appCanvas();
  const int16_t cellX = 24, cellY = 54, cellH = 350;
  bool inkBelowCover = false;
  for (int16_t y = cellY + COVER_H + 1; y < cellY + cellH && !inkBelowCover; y++)
    for (int16_t x = cellX; x < cellX + COVER_W; x++)
      if (!gfx.getPixel(x, y)) { inkBelowCover = true; break; }
  CHECK(!inkBelowCover);
}

int main() {
  testReadingHitTest();
  testBookTable();
  testCoverScaling();
  testLibraryGridHitTest();
  testLibraryListHitTest();
  testDrawCoverPixels();
  testDrawCoverPlaceholderLegibility();
  testReadingProgressPercent();
  testLibraryGridNoDuplicateCaption();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
