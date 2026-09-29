// Host tests for the reader's pure arithmetic: hit-testing, pagination and
// cover scaling. Run with `make test`.
#include <cstdio>
#include "Arduino.h"
#include "books.h"
#include "cover.h"
#include "epd.h"
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

int main() {
  testReadingHitTest();
  testBookTable();
  testCoverScaling();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
