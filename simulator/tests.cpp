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
#include "store.h"
#include <Fonts/FreeSerifBold9pt7b.h>

SerialPort Serial;

// ---- Stand-ins for the screen driver (reader/epd.h) ----
void epdBegin() {}
void epdShowFull(const uint8_t *) {}
void epdShowPartial(const uint8_t *) {}
void epdSleep() {}

// ---- Stand-ins for persistence (reader/store.h) ----
// store.cpp is hardware-only (real NVS via Preferences), so it is not linked
// into this target, but reading.cpp and library.cpp call into it regardless.
// Nothing touches a state file on disk -- that would make these tests order-
// and history-dependent. Most tests want "nothing has ever been saved", which
// is the default; testResumeRebuildsPageHistory sets a saved offset through
// `fakeSaved*` and clears it again afterwards.
static bool fakeSavedValid = false;
static uint32_t fakeSavedOffset = 0;
static bool fakeSavedItalic = false;

void storeBegin() {}
void storeSaveProgress(uint8_t, uint32_t, bool) {}
bool storeLoadProgress(uint8_t, uint32_t &offset, bool &italic) {
  if (!fakeSavedValid) return false;
  offset = fakeSavedOffset;
  italic = fakeSavedItalic;
  return true;
}
void storeSaveView(uint8_t) {}
uint8_t storeLoadView() { return 0; }

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
  // Controls hidden: left third back, centre third opens the bar, right third
  // forward.
  CHECK_EQ(readingHitTest(0, 400, false), READ_PREV);
  CHECK_EQ(readingHitTest(159, 400, false), READ_PREV);
  CHECK_EQ(readingHitTest(160, 400, false), READ_SHOW_CONTROLS);
  CHECK_EQ(readingHitTest(319, 400, false), READ_SHOW_CONTROLS);
  CHECK_EQ(readingHitTest(320, 400, false), READ_NEXT);
  CHECK_EQ(readingHitTest(479, 400, false), READ_NEXT);

  // Controls visible: the back arrow returns to the library, the rest of the
  // bar and the page below it dismiss the bar. No page turns while it is up.
  CHECK_EQ(readingHitTest(10, 30, true), READ_BACK_TO_LIBRARY);
  CHECK_EQ(readingHitTest(79, 30, true), READ_BACK_TO_LIBRARY);
  CHECK_EQ(readingHitTest(80, 30, true), READ_HIDE_CONTROLS);
  CHECK_EQ(readingHitTest(240, 400, true), READ_HIDE_CONTROLS);
  CHECK_EQ(readingHitTest(10, 400, true), READ_HIDE_CONTROLS);

  // The bar's bottom edge (BAR_H = 64): last row inside vs. first row below,
  // both well within the back arrow's x range so only the y boundary is
  // exercised.
  CHECK_EQ(readingHitTest(10, 63, true), READ_BACK_TO_LIBRARY);
  CHECK_EQ(readingHitTest(10, 64, true), READ_HIDE_CONTROLS);

  // The back arrow only counts inside the bar, not down the left edge.
  CHECK_EQ(readingHitTest(10, 65, true), READ_HIDE_CONTROLS);
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
  readingOpenBook(0);
  readingShow(true);

  // Run to the end of the book, so the next forward turn is rejected.
  uint32_t pct = readingProgressPercent(0);
  int guard = 0;
  while (guard < 200) {
    readingTurnPage(1);
    uint32_t next = readingProgressPercent(0);
    if (next == pct) break;  // stopped moving: this is the last page
    pct = next;
    guard++;
  }
  CHECK(guard < 200);  // sanity: the book paginated and we found the end

  readingTap(240, 400);            // show the bar
  uint32_t lastPct = readingProgressPercent(0);
  readingTurnPage(1);              // rejected: already at the last page
  CHECK_EQ(readingProgressPercent(0), lastPct);  // confirms the rejection

  // If the rejected turn had wrongly cleared controlsVisible, this tap would
  // be read as READ_PREV and the page would move backward.
  readingTap(10, 400);
  CHECK_EQ(readingProgressPercent(0), lastPct);  // unchanged: the tap only hid the bar

  // Accepted case: bring the bar back up, then turn backward -- accepted,
  // since we are not at the first page.
  readingTap(240, 400);
  readingTurnPage(-1);
  uint32_t afterTurn = readingProgressPercent(0);
  CHECK(afterTurn < lastPct);       // confirms the turn actually moved

  // The bar must already be down now, so the same tap moves a page instead
  // of merely hiding it.
  readingTap(10, 400);
  CHECK(readingProgressPercent(0) < afterTurn);
}

// Resuming a book must restore the page the reader was on together with the
// history in front of it, not fabricate a one-entry history: currentPage == 0
// is pagination's meaning of "the first page of this book", so a restored page
// numbered 0 can never be turned back and is labelled page 1 in the footer.
// There is no getter for currentPage, so this drives the observable
// consequences instead -- readingProgressPercent, and whether a backward turn
// actually moves the page. Placed with the other state-mutating reading tests,
// and it clears the fake saved position again so later tests still see a shelf
// nothing has ever been read from.
static void testResumeRebuildsPageHistory() {
  const uint32_t len = (uint32_t)strlen(BOOKS[0].text);

  // Resume from the middle of the book.
  fakeSavedValid = true;
  fakeSavedItalic = false;
  fakeSavedOffset = len / 2;
  readingOpenBook(0);
  readingShow(true);

  // The restored page is the one containing the saved offset, so it begins at
  // or before the halfway mark (percent is floor(offset * 100 / len)).
  const uint32_t restored = readingProgressPercent(0);
  CHECK(restored > 0);
  CHECK(restored <= 50);

  // The backward turn is accepted and moves the page: the whole point of
  // rebuilding the history. Against a one-entry history this turn is refused
  // and the percentage never budges.
  readingTurnPage(-1);
  const uint32_t previous = readingProgressPercent(0);
  CHECK(previous < restored);

  // Forward again returns to exactly the restored page, and the page after it
  // begins past the saved offset -- together with `restored <= 50` that pins
  // the saved offset inside the restored page rather than merely near it.
  readingTurnPage(1);
  CHECK_EQ(readingProgressPercent(0), restored);
  readingTurnPage(1);
  CHECK(readingProgressPercent(0) >= 50);

  // Turning back from there reaches the first page rather than stalling
  // partway, and it takes several turns -- so this really was a page deep in
  // the book, not page 1 wearing a high percentage.
  int guard = 0;
  while (readingProgressPercent(0) > 0 && guard < 500) {
    readingTurnPage(-1);
    guard++;
  }
  CHECK(guard > 1);
  CHECK(guard < 500);
  CHECK_EQ(readingProgressPercent(0), 0);

  // A saved offset at or past the end of the text must terminate the walk and
  // land on a real last page: forward refused, backward still accepted.
  fakeSavedOffset = len + 1000;
  readingOpenBook(0);
  readingShow(true);
  const uint32_t last = readingProgressPercent(0);
  CHECK(last > 0);
  CHECK(last <= 100);
  readingTurnPage(1);
  CHECK_EQ(readingProgressPercent(0), last);  // already the last page
  readingTurnPage(-1);
  CHECK(readingProgressPercent(0) < last);

  // A saved offset of 0 behaves exactly as a book that was never opened: the
  // first page, with the backward turn refused and nothing left corrupted.
  fakeSavedOffset = 0;
  readingOpenBook(0);
  readingShow(true);
  CHECK_EQ(readingProgressPercent(0), 0);
  readingTurnPage(-1);
  CHECK_EQ(readingProgressPercent(0), 0);
  readingTurnPage(1);
  CHECK(readingProgressPercent(0) > 0);

  fakeSavedValid = false;
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
// testLibraryGridNoDuplicateCaption, which must stay last.
static void testListRowPercentRightAligned() {
  libraryTap(400, 20);  // header toggle: grid -> list

  readingOpenBook(0);
  readingShow(true);
  uint32_t pct = readingProgressPercent(0);
  int guard = 0;
  while (pct < 10 && guard < 50) {
    readingTurnPage(1);
    pct = readingProgressPercent(0);
    guard++;
  }
  CHECK(pct >= 10);  // needed a 2+ digit label; the fixed book is long enough
  CHECK(pct <= 99);

  libraryShow(false);  // redraw the list view with book 0's updated progress

  GFXcanvas1 &gfx = appCanvas();
  // Book 0 is list row 0: y 50-167 (LIST_TOP=50, LIST_ROW_H=118). Scanning
  // x >= 400 stays clear of the progress bar (which ends at x = 392) and of
  // the row's bottom divider line (y = 167), so only the percentage label's
  // own pixels can be found here.
  int16_t maxInkX = -1;
  for (int16_t y = 50; y <= 165; y++) {
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

// Every current book has cover == nullptr, so drawCover's placeholder
// already states title and author once inside the frame. The grid's own
// caption band below the cover must stay blank in that case, or the text is
// stated twice. (Coordinates match library.cpp's private grid geometry: the
// top-left cell starts at (24, 54); it is the one cell that stays inside the
// unrotated test canvas without calling appBegin().)
static void testStorePacking() {
  uint32_t offset;
  bool italic;

  storeUnpack(storePack(0, false), offset, italic);
  CHECK_EQ(offset, 0);
  CHECK(!italic);

  storeUnpack(storePack(0, true), offset, italic);
  CHECK_EQ(offset, 0);
  CHECK(italic);

  storeUnpack(storePack(123456, true), offset, italic);
  CHECK_EQ(offset, 123456);
  CHECK(italic);

  // The largest offset that survives the round trip.
  storeUnpack(storePack(0x7FFFFFFF, false), offset, italic);
  CHECK_EQ(offset, 0x7FFFFFFF);
  CHECK(!italic);
}

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
  testTruncateToWidth();
  testLibraryGridHitTest();
  testLibraryListHitTest();
  testDrawCoverPixels();
  testDrawCoverPlaceholderLegibility();
  testReadingProgressPercent();
  testControlsVisibleSurvivesRejectedTurn();
  testResumeRebuildsPageHistory();
  testListRowPercentRightAligned();
  testLibraryGridNoDuplicateCaption();
  testStorePacking();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
