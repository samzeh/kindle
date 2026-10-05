// Host tests for the reader: hit-testing, pagination, covers, the library
// and the reading screen, using the small EPUBs in fixtures/library. Run
// with `make test` (from simulator/). EPUB parsing itself is tested in
// test_epub.cpp.
#include <cstdio>
#include "Arduino.h"
#include "check.h"
#include "catalog.h"
#include "cover.h"
#include "host_platform.h"
#include "layout.h"
#include "epd.h"
#include "library.h"
#include "reading.h"
#include "screens.h"
#include "storage.h"
#include "store.h"
#include <Fonts/FreeSerifBold9pt7b.h>

SerialPort Serial;

void runEpubTests();  // test_epub.cpp

// ---- Stand-ins for the screen driver (reader/epd.h) ----
void epdBegin() {}
void epdShowFull(const uint8_t *) {}
void epdShowPartial(const uint8_t *) {}
void epdSleep() {}

// Saved positions (reader/store.h) come from host_platform.cpp, in memory
// only. Tests that need a book nobody has read call hostStoreReset() first.

// ---- Tiny test framework: see check.h ----
int failures = 0;
static void testReadingHitTest() {
  // Controls hidden: left third back, centre third opens the bar, right third
  // forward.
  CHECK_EQ(readingHitTest(0, 400, false), READ_PREV);
  CHECK_EQ(readingHitTest(159, 400, false), READ_PREV);
  CHECK_EQ(readingHitTest(160, 400, false), READ_SHOW_CONTROLS);
  CHECK_EQ(readingHitTest(319, 400, false), READ_SHOW_CONTROLS);
  CHECK_EQ(readingHitTest(320, 400, false), READ_NEXT);
  CHECK_EQ(readingHitTest(479, 400, false), READ_NEXT);

  // Controls visible: the chevron (top left) returns to the library, the
  // gear (top right) opens settings, and anything else dismisses the bar.
  // No page turns while it is up.
  CHECK_EQ(readingHitTest(10, 20, true), READ_BACK_TO_LIBRARY);
  CHECK_EQ(readingHitTest(63, 20, true), READ_BACK_TO_LIBRARY);
  CHECK_EQ(readingHitTest(64, 20, true), READ_HIDE_CONTROLS);
  CHECK_EQ(readingHitTest(240, 20, true), READ_HIDE_CONTROLS);  // the chapter title
  CHECK_EQ(readingHitTest(415, 20, true), READ_HIDE_CONTROLS);
  CHECK_EQ(readingHitTest(416, 20, true), READ_OPEN_SETTINGS);
  CHECK_EQ(readingHitTest(479, 20, true), READ_OPEN_SETTINGS);
  CHECK_EQ(readingHitTest(240, 400, true), READ_HIDE_CONTROLS);
  CHECK_EQ(readingHitTest(10, 400, true), READ_HIDE_CONTROLS);

  // The bar's bottom edge (BAR_H = 44): last row inside vs. first row below,
  // for both icons, so only the y boundary is exercised.
  CHECK_EQ(readingHitTest(10, 43, true), READ_BACK_TO_LIBRARY);
  CHECK_EQ(readingHitTest(10, 44, true), READ_HIDE_CONTROLS);
  CHECK_EQ(readingHitTest(470, 43, true), READ_OPEN_SETTINGS);
  CHECK_EQ(readingHitTest(470, 44, true), READ_HIDE_CONTROLS);
}

// The fixture library holds four books plus two files that are not books
// (a macOS "._" file and notes.txt), which must be skipped. Books come out
// sorted by title, with metadata and covers from their EPUBs; a second scan
// finds them all in the cache and imports nothing.
static void testCatalogScan() {
  CHECK_EQ(catalogCount(), 4);
  const char *titles[] = { "Aardvark Stories", "Bramble Tales", "Coverless Notes", "Deep Folder" };
  const bool covers[] = { true, true, false, false };
  for (uint16_t i = 0; i < 4 && i < catalogCount(); i++) {
    CHECK_STR(catalogBook(i).title, titles[i]);
    CHECK(catalogBook(i).author[0] != '\0');
    CHECK(catalogBook(i).hasCover == covers[i]);
    CHECK_EQ(catalogBook(i).textLength, 0);  // not opened yet
  }
  CHECK_STR(catalogBook(0).author, "Ann Author");
  // Ids differ, so each book gets its own cache folder and saved position.
  for (uint16_t i = 0; i < catalogCount(); i++)
    for (uint16_t j = i + 1; j < catalogCount(); j++) CHECK(catalogBook(i).id != catalogBook(j).id);
  CHECK_EQ(catalogScan(nullptr, nullptr), 0);  // everything already cached
  CHECK_EQ(catalogCount(), 4);
}

static void testCoverScaling() {
  // At native size, destination and source indices agree.
  CHECK_EQ(coverSrcIndex(0, 204, 204), 0);
  CHECK_EQ(coverSrcIndex(203, 204, 204), 203);

  // Downscaled, indices stay inside the source.
  CHECK_EQ(coverSrcIndex(0, 72, 204), 0);
  CHECK_EQ(coverSrcIndex(71, 72, 204), 201);
  CHECK_EQ(coverSrcIndex(0, 108, 306), 0);
  CHECK_EQ(coverSrcIndex(107, 108, 306), 303);

  // Never reads past the end, at any destination size.
  for (int16_t size = 1; size <= COVER_W; size++) {
    CHECK(coverSrcIndex(size - 1, size, COVER_W) < COVER_W);
    CHECK(coverSrcIndex(0, size, COVER_W) >= 0);
  }
}

// truncateToWidth backs both drawCentredText (grid captions, footer labels)
// and the list view's title/author -- this is the one place its shortening
// behaviour is under direct test.
static void testTruncateToWidth() {
  GFXcanvas1 canvas(400, 100);  // throwaway: only used to measure text
  char buf[48];

  // Fits already: comes back unchanged.
  truncateToWidth(canvas, "Short", &FreeSerifBold9pt7b, 200, buf, sizeof(buf));
  CHECK(strcmp(buf, "Short") == 0);

  // Too long for the box: shortened, and ends with the ellipsis rather than
  // overflowing its column.
  const char *longTitle = "A Remarkably Long and Overwrought Title That Will Not Fit";
  truncateToWidth(canvas, longTitle, &FreeSerifBold9pt7b, 200, buf, sizeof(buf));
  CHECK(strlen(buf) < strlen(longTitle));
  CHECK(strlen(buf) >= 3);
  CHECK(strcmp(buf + strlen(buf) - 3, "...") == 0);

  // The truncated result actually fits the box it was truncated to.
  int16_t x1, y1;
  uint16_t w, h;
  canvas.setFont(&FreeSerifBold9pt7b);
  canvas.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  CHECK(w <= 200);
}

static void testLibraryGridHitTest() {
  // Nine books per page in grid view: three across, three down.
  CHECK_EQ(libraryPageCount(LIB_VIEW_GRID), 1);  // catalogCount() == 4

  // Header, right side: toggles the view.
  CHECK_EQ(libraryHitTest(400, 20, 0, LIB_VIEW_GRID).action, LIB_TOGGLE_VIEW);
  // Header, left side: nothing.
  CHECK_EQ(libraryHitTest(100, 20, 0, LIB_VIEW_GRID).action, LIB_NONE);

  // The cells, sampled at their covers' centres. Columns start at x = 24,
  // 180 and 336 (120 wide, 36 apart); rows at y = 72 and 300 (220 tall).
  const int16_t colMid[3] = { 84, 240, 396 };
  for (int16_t col = 0; col < 3; col++) {
    LibraryHit hit = libraryHitTest(colMid[col], 160, 0, LIB_VIEW_GRID);
    CHECK_EQ(hit.action, LIB_OPEN_BOOK);
    CHECK_EQ(hit.book, col);
  }
  LibraryHit secondRow = libraryHitTest(84, 390, 0, LIB_VIEW_GRID);
  CHECK_EQ(secondRow.action, LIB_OPEN_BOOK);
  CHECK_EQ(secondRow.book, 3);
  // The next cell would be book 4, past the shelf.
  CHECK_EQ(libraryHitTest(240, 390, 0, LIB_VIEW_GRID).action, LIB_NONE);

  // The gaps between columns open nothing; nor does the left margin.
  CHECK_EQ(libraryHitTest(160, 150, 0, LIB_VIEW_GRID).action, LIB_NONE);
  CHECK_EQ(libraryHitTest(10, 150, 0, LIB_VIEW_GRID).action, LIB_NONE);

  // Cell and header boundaries, each pair "last pixel inside" then "first
  // pixel outside". A cell is its cover plus the title below it.
  CHECK_EQ(libraryHitTest(143, 150, 0, LIB_VIEW_GRID).book, 0);           // col 0 right edge
  CHECK_EQ(libraryHitTest(144, 150, 0, LIB_VIEW_GRID).action, LIB_NONE);
  CHECK_EQ(libraryHitTest(179, 150, 0, LIB_VIEW_GRID).action, LIB_NONE);  // col 1 left edge
  CHECK_EQ(libraryHitTest(180, 150, 0, LIB_VIEW_GRID).book, 1);
  CHECK_EQ(libraryHitTest(455, 150, 0, LIB_VIEW_GRID).book, 2);           // col 2 right edge
  CHECK_EQ(libraryHitTest(456, 150, 0, LIB_VIEW_GRID).action, LIB_NONE);
  CHECK_EQ(libraryHitTest(84, 71, 0, LIB_VIEW_GRID).action, LIB_NONE);    // row 0 top edge
  CHECK_EQ(libraryHitTest(84, 72, 0, LIB_VIEW_GRID).book, 0);
  CHECK_EQ(libraryHitTest(84, 291, 0, LIB_VIEW_GRID).book, 0);            // row 0 bottom edge
  CHECK_EQ(libraryHitTest(84, 292, 0, LIB_VIEW_GRID).action, LIB_NONE);
  CHECK_EQ(libraryHitTest(84, 299, 0, LIB_VIEW_GRID).action, LIB_NONE);   // row 1 top edge
  CHECK_EQ(libraryHitTest(84, 300, 0, LIB_VIEW_GRID).book, 3);
  CHECK_EQ(libraryHitTest(100, 59, 0, LIB_VIEW_GRID).action, LIB_NONE);   // header bottom edge
  CHECK_EQ(libraryHitTest(400, 59, 0, LIB_VIEW_GRID).action, LIB_TOGGLE_VIEW);
  CHECK_EQ(libraryHitTest(383, 20, 0, LIB_VIEW_GRID).action, LIB_NONE);   // toggle edge
  CHECK_EQ(libraryHitTest(384, 20, 0, LIB_VIEW_GRID).action, LIB_TOGGLE_VIEW);

  // (126, 80) is the point the simulator's 'o' key taps. It has to open book 0
  // in *both* views, since the key has no way to know which one is up; the
  // list-view half of this pair is asserted in testLibraryListHitTest.
  LibraryHit simO = libraryHitTest(126, 80, 0, LIB_VIEW_GRID);
  CHECK_EQ(simO.action, LIB_OPEN_BOOK);
  CHECK_EQ(simO.book, 0);

  // Footer paging.
  CHECK_EQ(libraryHitTest(50, 780, 0, LIB_VIEW_GRID).action, LIB_PREV_PAGE);
  CHECK_EQ(libraryHitTest(400, 780, 0, LIB_VIEW_GRID).action, LIB_NEXT_PAGE);
  CHECK_EQ(libraryHitTest(240, 780, 0, LIB_VIEW_GRID).action, LIB_NONE);
}

static void testLibraryListHitTest() {
  // Six rows of 114px starting at y = 66.
  LibraryHit first = libraryHitTest(240, 80, 0, LIB_VIEW_LIST);
  CHECK_EQ(first.action, LIB_OPEN_BOOK);
  CHECK_EQ(first.book, 0);

  LibraryHit second = libraryHitTest(240, 220, 0, LIB_VIEW_LIST);
  CHECK_EQ(second.action, LIB_OPEN_BOOK);
  CHECK_EQ(second.book, 1);

  LibraryHit fourth = libraryHitTest(240, 460, 0, LIB_VIEW_LIST);
  CHECK_EQ(fourth.action, LIB_OPEN_BOOK);
  CHECK_EQ(fourth.book, 3);

  // Rows past the end of the shelf open nothing.
  CHECK_EQ(libraryHitTest(240, 580, 0, LIB_VIEW_LIST).action, LIB_NONE);

  // The other half of the simulator 'o' key's shared point: the same (126, 80)
  // that hits the grid's top-left cover also hits list row 0.
  LibraryHit simO = libraryHitTest(126, 80, 0, LIB_VIEW_LIST);
  CHECK_EQ(simO.action, LIB_OPEN_BOOK);
  CHECK_EQ(simO.book, 0);

  // The header and footer behave the same in both views.
  CHECK_EQ(libraryHitTest(400, 20, 0, LIB_VIEW_LIST).action, LIB_TOGGLE_VIEW);
  CHECK_EQ(libraryHitTest(400, 780, 0, LIB_VIEW_LIST).action, LIB_NEXT_PAGE);

  // Four books still fit on one page in list view.
  CHECK_EQ(libraryPageCount(LIB_VIEW_LIST), 1);

  // Row boundaries (rows are 114px starting at y = 66): row 0's last pixel
  // (179) vs row 1's first pixel (180).
  CHECK_EQ(libraryHitTest(240, 179, 0, LIB_VIEW_LIST).book, 0);
  CHECK_EQ(libraryHitTest(240, 180, 0, LIB_VIEW_LIST).book, 1);

  // The last valid row (row 3, book index 3, y 408-521) vs the first row
  // past the shelf (row 4 would be index 4, but catalogCount() == 4).
  CHECK_EQ(libraryHitTest(240, 521, 0, LIB_VIEW_LIST).book, 3);
  CHECK_EQ(libraryHitTest(240, 522, 0, LIB_VIEW_LIST).action, LIB_NONE);
}

// A 48 x 72 grayscale JPEG, black top half and white bottom half, made with
// Pillow (quality 95). Same 2:3 shape as the cover box, so nothing is cropped.
static const uint8_t HALF_BLACK_JPEG[] = {
  0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x01, 0x00, 0x00, 0x01,
  0x00, 0x01, 0x00, 0x00, 0xFF, 0xDB, 0x00, 0x43, 0x00, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02,
  0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x04, 0x03, 0x02, 0x02, 0x02, 0x02, 0x05, 0x04,
  0x04, 0x03, 0x04, 0x06, 0x05, 0x06, 0x06, 0x06, 0x05, 0x06, 0x06, 0x06, 0x07, 0x09, 0x08, 0x06,
  0x07, 0x09, 0x07, 0x06, 0x06, 0x08, 0x0B, 0x08, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x06, 0x08,
  0x0B, 0x0C, 0x0B, 0x0A, 0x0C, 0x09, 0x0A, 0x0A, 0x0A, 0xFF, 0xC0, 0x00, 0x0B, 0x08, 0x00, 0x48,
  0x00, 0x30, 0x01, 0x01, 0x11, 0x00, 0xFF, 0xC4, 0x00, 0x1F, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01,
  0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04,
  0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0xFF, 0xC4, 0x00, 0xB5, 0x10, 0x00, 0x02, 0x01, 0x03,
  0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00, 0x01, 0x7D, 0x01, 0x02, 0x03, 0x00,
  0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32,
  0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72,
  0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35,
  0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55,
  0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75,
  0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94,
  0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2,
  0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9,
  0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6,
  0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA, 0xFF, 0xDA,
  0x00, 0x08, 0x01, 0x01, 0x00, 0x00, 0x3F, 0x00, 0xFE, 0x7F, 0xE8, 0xA2, 0x8A, 0x28, 0xA2, 0x8A,
  0x28, 0xA2, 0x8A, 0x28, 0xA2, 0x8A, 0x28, 0xA2, 0x8A, 0x28, 0xA2, 0x8A, 0xFD, 0xFE, 0xFF, 0x00,
  0x88, 0x18, 0xFF, 0x00, 0xEB, 0x28, 0xBF, 0xF9, 0x84, 0xFF, 0x00, 0xFB, 0xF5, 0x47, 0xFC, 0x40,
  0xC7, 0xFF, 0x00, 0x59, 0x45, 0xFF, 0x00, 0xCC, 0x27, 0xFF, 0x00, 0xDF, 0xAA, 0x3F, 0xE2, 0x06,
  0x3F, 0xFA, 0xCA, 0x2F, 0xFE, 0x61, 0x3F, 0xFE, 0xFD, 0x51, 0xFF, 0x00, 0x10, 0x31, 0xFF, 0x00,
  0xD6, 0x51, 0x7F, 0xF3, 0x09, 0xFF, 0x00, 0xF7, 0xEA, 0x8F, 0xF8, 0x81, 0x8F, 0xFE, 0xB2, 0x8B,
  0xFF, 0x00, 0x98, 0x4F, 0xFF, 0x00, 0xBF, 0x54, 0x7F, 0xC4, 0x0C, 0x7F, 0xF5, 0x94, 0x5F, 0xFC,
  0xC2, 0x7F, 0xFD, 0xFA, 0xAF, 0xDF, 0xEA, 0x28, 0xA2, 0x8A, 0x28, 0xA2, 0x8A, 0x28, 0xA2, 0x8A,
  0x28, 0xA2, 0x8A, 0x28, 0xA2, 0x8A, 0x28, 0xA2, 0xBF, 0xFF, 0xD9,
};

static bool bitSet(const uint8_t *bits, int16_t w, int16_t x, int16_t y) {
  return bits[y * ((w + 7) / 8) + x / 8] & (0x80 >> (x % 8));
}

static void testDrawCoverPixels() {
  // Black in the JPEG is ink (a 1 bit) at both sizes. If the black check
  // fails with the white one passing, covers render as photographic
  // negatives. Points avoid the black/white boundary, where dithering mixes
  // the two.
  static uint8_t grid[COVER_GRID_BYTES], thumb[COVER_THUMB_BYTES];
  MemReader jpeg(HALF_BLACK_JPEG, sizeof(HALF_BLACK_JPEG));
  CHECK(coverRender(jpeg, grid, thumb));
  CHECK(bitSet(grid, COVER_W, COVER_W / 2, COVER_H / 5));
  CHECK(!bitSet(grid, COVER_W, COVER_W / 2, COVER_H * 4 / 5));
  CHECK(bitSet(thumb, THUMB_W, THUMB_W / 2, 20));
  CHECK(!bitSet(thumb, THUMB_W, THUMB_W / 2, 90));

  // Bytes that are not a JPEG are refused rather than drawn as garbage.
  static const uint8_t notJpeg[] = { 0x00, 0x01, 0x02, 0x03 };
  MemReader bad(notJpeg, sizeof(notJpeg));
  CHECK(!coverRender(bad, grid, thumb));

  // A book without a cover draws a framed light grey block (one pixel in
  // four ink inside the frame) rather than nothing.
  BookInfo noArt = {};
  strcpy(noArt.title, "Title");
  strcpy(noArt.author, "Author");
  GFXcanvas1 fb(COVER_W, COVER_H);
  fb.fillScreen(0xFFFF);
  drawCover(fb, noArt, 0, 0, COVER_W, COVER_H);
  CHECK(!fb.getPixel(0, 0));                       // frame corner is ink
  CHECK(!fb.getPixel(COVER_W - 1, COVER_H - 1));   // opposite corner too
  int ink = 0, inside = 0;
  for (int16_t y = 2; y < COVER_H - 2; y++)
    for (int16_t x = 2; x < COVER_W - 2; x++, inside++) ink += !fb.getPixel(x, y);
  CHECK(ink * 4 > inside * 9 / 10 && ink * 4 < inside * 11 / 10);  // about a quarter
}

// Whether the box has a white band: several entirely white rows in a row
// inside the frame (the grey pattern alone only ever has one white row at a
// time). That band is what the placeholder's text sits on.
static bool hasWhiteBand(GFXcanvas1 &c, int16_t w, int16_t h) {
  int16_t run = 0;
  for (int16_t y = 1; y < h - 1; y++) {
    bool white = true;
    for (int16_t x = 1; x < w - 1 && white; x++) white = c.getPixel(x, y);
    run = white ? run + 1 : 0;
    if (run >= 3) return true;
  }
  return false;
}

// drawCover's typographic placeholder writes title/author text (on a white
// band) only when the box clears the legibility floor. Below it, as for all
// of the library's covers, the caller draws the caption elsewhere, so
// drawCover must draw the grey block alone or the text is stated twice.
static void testDrawCoverPlaceholderLegibility() {
  BookInfo noArt = {};
  strcpy(noArt.title, "Title");
  strcpy(noArt.author, "Author");

  const int16_t bigW = 204, bigH = 306;  // a box above the legibility floor
  CHECK(bigW >= COVER_TEXT_MIN_W);
  GFXcanvas1 full(bigW, bigH);
  full.fillScreen(0xFFFF);
  drawCover(full, noArt, 0, 0, bigW, bigH);
  CHECK(hasWhiteBand(full, bigW, bigH));  // the band behind the text
  bool textInk = false;                   // ...and the text on it
  for (int16_t y = bigH / 2 - 20; y < bigH / 2 + 20 && !textInk; y++)
    for (int16_t x = 10; x < bigW - 10; x++)
      if (!full.getPixel(x, y)) { textInk = true; break; }
  CHECK(textInk);
  CHECK(COVER_W < COVER_TEXT_MIN_W);  // the library's covers are all captioned

  GFXcanvas1 thumb(THUMB_W, THUMB_H);
  thumb.fillScreen(0xFFFF);
  drawCover(thumb, noArt, 0, 0, THUMB_W, THUMB_H);
  CHECK(THUMB_W < COVER_TEXT_MIN_W);  // this test only means something if so
  // Below the floor: the grey block alone, with no band and no text.
  CHECK(!hasWhiteBand(thumb, THUMB_W, THUMB_H));
  // The frame itself is still drawn.
  CHECK(!thumb.getPixel(0, 0));
  CHECK(!thumb.getPixel(THUMB_W - 1, THUMB_H - 1));
}

// readingProgressPercent reads saved positions for every book, not only the
// open one. This asserts ranges and ordering, never an exact percentage,
// since the exact value is a function of font metrics and page layout, not a
// contract worth pinning. It sits here, after the pure arithmetic tests,
// because readingOpenBook and readingTurnPage draw through the shared
// appCanvas() singleton and mutate reading.cpp's statics.
static void testReadingProgressPercent() {
  hostStoreReset();
  // A book that has never been read reports 0.
  CHECK_EQ(readingProgressPercent(2), 0);

  // An out-of-range index reports 0 rather than reading past the table.
  CHECK_EQ(readingProgressPercent(catalogCount()), 0);
  CHECK_EQ(readingProgressPercent(255), 0);

  CHECK(readingOpenBook(0));
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

// A page turn that arrives while the control bar is up must leave the bar's
// visibility untouched if the turn is rejected at a boundary (last/first
// page) -- otherwise the screen would keep showing the bar while the model
// says it is hidden, until something else forces a redraw. An accepted turn,
// by contrast, must still clear it, since the page underneath really did
// change. `controlsVisible` is a file-static in reading.cpp with no getter,
// so this drives it through readingTap's behaviour, which reads it
// internally: a tap in the left third moves a page only while the bar is
// down, and only hides the bar while it is up. The two cases are told apart
// by watching readingProgressPercent move (or not) after such a tap. Placed
// with the other state-mutating reading tests, before the list view test
// takes over the shared canvas.
static void testControlsVisibleSurvivesRejectedTurn() {
  hostStoreReset();
  CHECK(readingOpenBook(0));
  readingShow(true);

  // Run to the end of the book, so the next forward turn is rejected.
  const uint32_t last = readingPageCount() - 1;
  int guard = 0;
  while (readingCurrentPage() < last && guard < 500) {
    readingTurnPage(1);
    guard++;
  }
  CHECK_EQ(readingCurrentPage(), last);

  readingTap(240, 400);            // show the bar
  readingTurnPage(1);              // rejected: already at the last page
  CHECK_EQ(readingCurrentPage(), last);  // confirms the rejection

  // If the rejected turn had wrongly cleared controlsVisible, this tap would
  // be read as READ_PREV and the page would move backward.
  readingTap(10, 400);
  CHECK_EQ(readingCurrentPage(), last);  // unchanged: the tap only hid the bar

  // Accepted case: bring the bar back up, then turn backward -- accepted,
  // since we are not at the first page.
  readingTap(240, 400);
  readingTurnPage(-1);
  CHECK_EQ(readingCurrentPage(), last - 1);  // the turn actually moved

  // The bar must already be down now, so the same tap moves a page instead
  // of merely hiding it.
  readingTap(10, 400);
  CHECK_EQ(readingCurrentPage(), last - 2);
}

// Opening a book resumes on the page containing its saved position, with
// the pages before it intact: page numbers stay real and backward turns
// work. Placed with the other state-mutating reading tests; it clears the
// saved positions again so later tests start books at page 1.
static void testResumeLandsOnSavedPage() {
  hostStoreReset();
  CHECK(readingOpenBook(0));
  const BookInfo &book = catalogBook(0);
  const uint32_t len = book.textLength;
  const uint32_t pageCount = readingPageCount();
  CHECK(len > 0);
  CHECK(pageCount > 3);

  // Resume from the middle of the book.
  storeSaveProgress(book.id, len / 2);
  CHECK(readingOpenBook(0));
  readingShow(true);
  const uint32_t page = readingCurrentPage();
  CHECK(page > 0);
  CHECK(page < pageCount - 1);
  const uint32_t restored = readingProgressPercent(0);
  CHECK(restored > 0);
  CHECK(restored <= 50);  // the page starts at or before the saved offset

  // The next page starts past it, so the saved offset is on this page.
  readingTurnPage(1);
  CHECK(readingProgressPercent(0) >= 50);
  readingTurnPage(-1);
  CHECK_EQ(readingCurrentPage(), page);

  // Backward turns reach page 1.
  int guard = 0;
  while (readingCurrentPage() > 0 && guard < 500) {
    readingTurnPage(-1);
    guard++;
  }
  CHECK_EQ(guard, (int)page);
  CHECK_EQ(readingProgressPercent(0), 0);

  // A saved offset past the end lands on the last page.
  storeSaveProgress(book.id, len + 1000);
  CHECK(readingOpenBook(0));
  CHECK_EQ(readingCurrentPage(), pageCount - 1);
  readingTurnPage(1);
  CHECK_EQ(readingCurrentPage(), pageCount - 1);  // already the last page

  // Turning pages saves the position; reopening comes back to it.
  readingTurnPage(-1);
  CHECK(readingOpenBook(0));
  CHECK_EQ(readingCurrentPage(), pageCount - 2);

  hostStoreReset();
}

// A book's first page is its cover, and every top-level chapter starts a new
// page, like a Kindle. A book without a cover starts straight with its text.
static void testCoverAndChapterPages() {
  hostStoreReset();
  CHECK(readingOpenBook(0));  // Aardvark Stories: has a cover
  CHECK_EQ(readingCurrentPage(), 0);
  CHECK_EQ(readingPageOffset(0), 0);  // the cover...
  CHECK_EQ(readingPageOffset(1), 0);  // ...then the text from its start
  readingShow(false);
  GFXcanvas1 &gfx = appCanvas();
  int ink = 0;
  for (int16_t y = 0; y < gfx.height(); y += 4)
    for (int16_t x = 0; x < gfx.width(); x += 4) ink += !gfx.getPixel(x, y);
  CHECK(ink > 100);  // the cover was drawn, not a blank page

  std::vector<Chapter> chapters;
  CHECK(catalogLoadChapters(catalogBook(0), chapters));
  for (const Chapter &c : chapters) {
    if (c.depth != 0) continue;  // sub-sections run on
    bool startsPage = false;
    for (uint32_t p = 1; p < readingPageCount(); p++) startsPage |= readingPageOffset(p) == c.offset;
    if (!startsPage) {
      printf("FAIL chapter \"%s\" does not start a page\n", c.title);
      failures++;
    }
  }

  CHECK(readingOpenBook(2));  // Coverless Notes
  CHECK_EQ(readingCurrentPage(), 0);
  CHECK_EQ(readingPageCount(), 1);  // just its text: no cover page added
  CHECK(readingChapterTitle()[0] != '\0');  // page 1 is text, so in a chapter
  hostStoreReset();
}

// The control bar shows the chapter the current page is in: the fixture's
// first chapter, its sub-section halfway through, then chapter 2.
static void testChapterTitles() {
  hostStoreReset();
  CHECK(readingOpenBook(0));
  CHECK_EQ(readingCurrentPage(), 0);  // a new book opens on its cover...
  CHECK_STR(readingChapterTitle(), "");  // ...which is in no chapter
  readingTurnPage(1);
  CHECK_STR(readingChapterTitle(), "Chapter 1: The Burrow");
  bool sawScene = false, sawChapter2 = false;
  for (int i = 0; i < 100 && readingCurrentPage() + 1 < readingPageCount(); i++) {
    readingTurnPage(1);
    if (strcmp(readingChapterTitle(), "A New Scene") == 0) sawScene = true;
    if (strcmp(readingChapterTitle(), "Chapter 2: The Ants") == 0) sawChapter2 = sawScene;
  }
  CHECK(sawScene);
  CHECK(sawChapter2);  // and in that order
  CHECK_STR(readingChapterTitle(), "Chapter 3: Home");
  hostStoreReset();
}

// The list row's percentage must be right-aligned by measuring its actual
// width, not a fixed cursor offset -- a fixed offset is only ever correct
// for one string length, and "0%" (2 chars) and a two-digit percentage
// (4 chars) are different widths. This drives book 0's progress into double
// digits (turning pages on the real, long sample text) and checks the
// rendered label's rightmost ink pixel, so it actually exercises a
// multi-character label rather than only ever testing "0%", which would
// pass even under the fixed-offset bug. Placed with the other
// state-mutating tests (drives readingOpenBook/readingTurnPage, which
// draw through the shared appCanvas() singleton and mutate reading.cpp's
// statics), and restores `view` to grid afterward so it does not affect
// testLibraryGridCaptionsEveryBook, which must stay last.
static void testListRowPercentRightAligned() {
  hostStoreReset();
  libraryTap(400, 20);  // header toggle: grid -> list

  CHECK(readingOpenBook(0));
  readingShow(true);
  uint32_t pct = readingProgressPercent(0);
  int guard = 0;
  while (pct < 10 && guard < 50) {
    readingTurnPage(1);
    pct = readingProgressPercent(0);
    guard++;
  }
  CHECK(pct >= 10);  // needed a 2+ digit label; the fixture book is long enough
  CHECK(pct <= 99);

  libraryShow(false);  // redraw the list view with book 0's updated progress

  GFXcanvas1 &gfx = appCanvas();
  // Book 0 is list row 0: y 66-179 (LIST_TOP=66, LIST_ROW_H=114). Scanning
  // x >= 400 stays clear of the progress line (which ends at x = 392) and
  // of the fixture book 0's short title, so only the percentage label's own
  // pixels can be found here.
  int16_t maxInkX = -1;
  for (int16_t y = 66; y <= 179; y++) {
    for (int16_t x = 400; x < 480; x++) {
      // false == black in GFXcanvas1. Track a true running max, not the last
      // ink pixel visited -- a later row with ink further left would
      // otherwise overwrite a correct answer from an earlier row.
      if (!gfx.getPixel(x, y) && x > maxInkX) maxInkX = x;
    }
  }
  CHECK(maxInkX >= 0);            // the label did render something
  CHECK_EQ(maxInkX, 455);         // SCREEN_W(480) - MARGIN_X(24) - 1

  libraryTap(400, 20);  // header toggle: list -> grid, restore state
}

// Every book in the grid is captioned with its title below its cover,
// whether or not it has cover art (the grey placeholder at this size has no
// text of its own), and the caption stays within the cover's width.
// (Coordinates match library.cpp's grid geometry: the top-left cell starts
// at (24, 72); it is inside the unrotated test canvas without calling
// appBegin().)
static void testLibraryGridCaptionsEveryBook() {
  libraryShow(false);
  GFXcanvas1 &gfx = appCanvas();
  const int16_t cellX = 24, cellY = 72, captionH = 40;
  bool inkBelowCover = false, inkOutside = false;
  for (int16_t y = cellY + COVER_H + 1; y < cellY + COVER_H + captionH; y++) {
    for (int16_t x = cellX; x < cellX + COVER_W; x++) inkBelowCover |= !gfx.getPixel(x, y);
    for (int16_t x = 0; x < cellX; x++) inkOutside |= !gfx.getPixel(x, y);
    for (int16_t x = cellX + COVER_W; x < cellX + COVER_W + 36; x++) inkOutside |= !gfx.getPixel(x, y);
  }
  CHECK(inkBelowCover);
  CHECK(!inkOutside);
}

// THE ORDER OF THE SECOND GROUP IS A CONSTRAINT, NOT A STYLE CHOICE.
//
// The first group is order-independent: those tests are pure functions, or they
// render into their own throwaway canvases, and the library hit tests take the
// view and page as arguments rather than reading the current one.
//
// The second group is not. Every test in it draws into the one shared
// appCanvas() and mutates reading.cpp's and library.cpp's file statics --
// current book, page history, controls-visible, grid-vs-list view -- so each
// one inherits whatever the last one left behind. The dependencies that exist
// today:
//
//   - testLibraryGridCaptionsEveryBook reads the shared canvas directly and
//     needs the view to be grid, which is also the initial value. It must not
//     run after anything that leaves the view on list.
//   - testListRowPercentRightAligned toggles the view to list and back, so it
//     is what makes the above hold -- and it is why it must restore grid on the
//     way out, not merely why it must run before.
//   - Opening a book resumes at its saved position, and turning pages saves
//     one. Tests that need book 0 at page 1 call hostStoreReset() first, and
//     tests that save positions on purpose clear them again on the way out.
//
// Reorder these and the failure will look like a rendering bug and will not be
// one. A new test that touches the canvas belongs at the end of this group.
int main() {
  // Every test reads the fixture library, with a fresh cache each run.
  if (system("rm -rf /tmp/reader-tests-cache") != 0) return 1;
  hostStorageSetRoots("fixtures/library", "/tmp/reader-tests-cache");
  if (!storageBegin()) {
    printf("FAIL fixtures/library not found: run the tests from simulator/\n");
    return 1;
  }
  catalogScan(nullptr, nullptr);

  // Order-independent: pure arithmetic, or rendering into their own canvases.
  testReadingHitTest();
  testCatalogScan();
  testCoverScaling();
  testTruncateToWidth();
  testLibraryGridHitTest();
  testLibraryListHitTest();
  testDrawCoverPixels();
  testDrawCoverPlaceholderLegibility();

  // ---- Below here: shared canvas and screen statics. Order matters. ----
  testReadingProgressPercent();
  testControlsVisibleSurvivesRejectedTurn();
  testResumeLandsOnSavedPage();
  testChapterTitles();
  testCoverAndChapterPages();
  testListRowPercentRightAligned();
  testLibraryGridCaptionsEveryBook();

  runEpubTests();  // EPUB reading (test_epub.cpp): independent of the above
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
