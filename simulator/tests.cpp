// Host tests for the reader's pure arithmetic: hit-testing, pagination and
// cover scaling. Run with `make test`.
#include <cstdio>
#include "Arduino.h"
#include "books.h"
#include "cover.h"
#include "epd.h"
#include "library.h"
#include "reading.h"

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

  // Footer paging.
  CHECK_EQ(libraryHitTest(50, 780, 0, LIB_VIEW_GRID).action, LIB_PREV_PAGE);
  CHECK_EQ(libraryHitTest(400, 780, 0, LIB_VIEW_GRID).action, LIB_NEXT_PAGE);
  CHECK_EQ(libraryHitTest(240, 780, 0, LIB_VIEW_GRID).action, LIB_NONE);
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

int main() {
  testReadingHitTest();
  testBookTable();
  testCoverScaling();
  testLibraryGridHitTest();
  testDrawCoverPixels();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
