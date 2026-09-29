# Library UI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the e-reader a library home screen showing every book with its cover, in either a cover grid or a list with reading progress, with reading positions that survive a reboot.

**Architecture:** A `Screen` enum in `app.cpp` routes taps to one of several screen modules, each exposing exactly two functions (`xxxShow`, `xxxTap`). The reading screen moves out of `app.cpp` into its own module so the router is the only thing `app.cpp` does. All screens draw into one shared 48 KB `GFXcanvas1`. Hit-testing and layout arithmetic are pure functions, tested on the host.

**Tech Stack:** C++17, Adafruit GFX, ESP32 Arduino core (`Preferences` for NVS), SDL2 for the desktop simulator, Python 3 + Pillow for the cover tool.

**Spec:** `docs/superpowers/specs/2026-09-29-library-ui-design.md`

## Global Constraints

- All drawing happens in a **480 × 800 portrait** coordinate space (panel is 800 × 480 native with `SCREEN_ROTATION 3`).
- There is exactly **one** `GFXcanvas1`, owned by `app.cpp` and reached via `appCanvas()`. No screen allocates its own — 48 KB is too much of the ESP32's RAM to duplicate.
- Screen modules receive **screen coordinates**. `appTap` applies `TOUCH_FLIP_X` once, at the top.
- Book data is `const` so the ESP32 keeps it in flash, not RAM.
- Cover bit convention: **1 = ink (black)**, MSB first, rows padded to whole bytes — 26 bytes/row, 7,956 bytes per 204 × 306 cover.
- Refreshes: `epdShowFull` for whole-image changes (screen change, view toggle, library paging); `epdShowPartial` for page turns and the control bar.
- Existing horizontal text margin is `MARGIN_X = 24`; library columns align to it.
- Layout constants, used verbatim throughout: `HEADER_H 48`, `BODY_TOP 48`, `BODY_BOTTOM 760`, `FOOTER_H 40`, `COVER_W 204`, `COVER_H 306`, `THUMB_W 72`, `THUMB_H 108`.

---

### Task 1: Test harness, screen router, and reading-screen extraction

Behaviour is unchanged after this task: the device still boots into the book, left third goes back, the rest goes forward. What changes is the shape — `app.cpp` becomes a router, and there is somewhere to write tests.

**Files:**
- Create: `reader/screens.h`, `reader/reading.h`, `reader/reading.cpp`
- Modify: `reader/app.h`, `reader/app.cpp`, `simulator/Makefile`
- Test: `simulator/tests.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `enum Screen : uint8_t { SCREEN_LIBRARY, SCREEN_READING }` (`screens.h`)
  - `void appGoTo(Screen s)` and `GFXcanvas1 &appCanvas()` (`screens.h`)
  - `enum ReadingAction : uint8_t { READ_NONE, READ_PREV, READ_NEXT }` (`reading.h`)
  - `ReadingAction readingHitTest(int16_t x)` (`reading.h`) — **Task 7 changes this signature**
  - `void readingShow(bool fullRefresh)`, `void readingTap(int16_t x, int16_t y)`, `void readingTurnPage(int delta)` (`reading.h`)

- [ ] **Step 1: Add the test target to `simulator/Makefile`**

Append after the existing `run` target. The tests link the reader sources and Adafruit GFX but not SDL and not `main.cpp`, so `tests.cpp` supplies the stand-ins that `main.cpp` normally provides.

```make
# Host tests for the pure layout and hit-testing arithmetic. No SDL, no
# main.cpp, so tests.cpp stands in for the screen driver and Serial.
TESTFLAGS := -std=c++17 -O1 -DARDUINO=100 -Ishim -I$(READER) -I$(GFX)

reader-tests: tests.cpp $(READER_SOURCES) $(GFX)/Adafruit_GFX.cpp $(HEADERS)
	$(CXX) $(TESTFLAGS) tests.cpp $(READER_SOURCES) $(GFX)/Adafruit_GFX.cpp -o $@

test: reader-tests
	./reader-tests
```

Also add `test` to the `.PHONY` line and `reader-tests` to the `clean` rule:

```make
clean:
	rm -f reader-sim reader-tests

.PHONY: run test clean
```

- [ ] **Step 2: Write `simulator/tests.cpp` with the failing test**

```cpp
// Host tests for the reader's pure arithmetic: hit-testing, pagination and
// cover scaling. Run with `make test`.
#include <cstdio>
#include "Arduino.h"
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

int main() {
  testReadingHitTest();
  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("all tests passed\n");
  return 0;
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cd simulator && make test`
Expected: compile error — `reading.h` does not exist.

- [ ] **Step 4: Create `reader/screens.h`**

```c
// Which screen is showing, and the shared frame buffer they all draw into.
//
// Adding a screen: add an enum value, add a module with xxxShow/xxxTap, and
// add a case to each switch in app.cpp. A book settings screen (font,
// frontlight) is the next one planned.
#pragma once
#include <Adafruit_GFX.h>
#include <stdint.h>

enum Screen : uint8_t {
  SCREEN_LIBRARY,
  SCREEN_READING,
};

// Switches screen and draws it with a full (flashing) refresh, which is what
// a whole-image change needs on e-paper.
void appGoTo(Screen s);

// The one 48 KB buffer every screen draws into. Never allocate another.
GFXcanvas1 &appCanvas();
```

- [ ] **Step 5: Create `reader/reading.h`**

```c
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

// Draws the current page.
void readingShow(bool fullRefresh);

// A tap in screen coordinates.
void readingTap(int16_t x, int16_t y);

// +1 = next page, -1 = previous page.
void readingTurnPage(int delta);
```

- [ ] **Step 6: Create `reader/reading.cpp` by moving the logic out of `app.cpp`**

This is the existing `app.cpp` body with the canvas and tap routing removed. The fonts and `PageFonts` move here; `app.cpp` ends up including no fonts at all.

```cpp
#include "reading.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerifItalic12pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifBoldItalic12pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <vector>

#include "board_config.h"
#include "epd.h"
#include "layout.h"
#include "sample_text.h"
#include "screens.h"

// Page turns use the no-flash refresh, which leaves faint ghosting over time.
// Set this to N to do a full (flashing) refresh every N turns; 0 = never.
static const uint8_t FULL_REFRESH_EVERY = 0;

static const int16_t SCREEN_W = 480;

static const PageFonts fonts = {
  &FreeSerif12pt7b,
  &FreeSerifItalic12pt7b,
  &FreeSerifBold12pt7b,
  &FreeSerifBoldItalic12pt7b,
  &FreeSerif9pt7b,
};

static PageLayout *layout = nullptr;
static std::vector<PagePos> pageStarts;  // grows as pages are visited
static size_t currentPage = 0;
static uint8_t turnsSinceFullRefresh = 0;

ReadingAction readingHitTest(int16_t x) {
  return x < SCREEN_W / 3 ? READ_PREV : READ_NEXT;
}

void readingShow(bool fullRefresh) {
  if (!layout) {
    layout = new PageLayout(appCanvas(), SAMPLE_TEXT, fonts);
    pageStarts.push_back({ 0, false });
  }
  unsigned long t0 = millis();
  PagePos next = layout->layoutPage(pageStarts[currentPage], true);
  if (currentPage + 1 == pageStarts.size() && !layout->isEnd(next)) {
    pageStarts.push_back(next);
  }
  if (fullRefresh) epdShowFull(appCanvas().getBuffer());
  else epdShowPartial(appCanvas().getBuffer());
  Serial.printf("page %u shown in %lu ms (%s)\n", (unsigned)currentPage + 1,
                millis() - t0, fullRefresh ? "full" : "partial");
}

void readingTurnPage(int delta) {
  if (delta > 0 && currentPage + 1 >= pageStarts.size()) return;  // last page
  if (delta < 0 && currentPage == 0) return;
  currentPage += delta;
  bool full = FULL_REFRESH_EVERY > 0 && ++turnsSinceFullRefresh >= FULL_REFRESH_EVERY;
  if (full) turnsSinceFullRefresh = 0;
  readingShow(full);
}

void readingTap(int16_t x, int16_t y) {
  (void)y;
  switch (readingHitTest(x)) {
    case READ_PREV: readingTurnPage(-1); break;
    case READ_NEXT: readingTurnPage(1); break;
    default: break;
  }
}
```

- [ ] **Step 7: Rewrite `reader/app.cpp` as the router**

```cpp
#include "app.h"
#include <Adafruit_GFX.h>

#include "board_config.h"
#include "epd.h"
#include "reading.h"
#include "screens.h"

// Full-screen buffer (48 KB): each screen is drawn in RAM, then sent in one
// go. Its memory layout matches the panel's, so it is sent unchanged.
static GFXcanvas1 canvas(EPD_NATIVE_WIDTH, EPD_NATIVE_HEIGHT);
static Screen current = SCREEN_READING;

GFXcanvas1 &appCanvas() {
  return canvas;
}

static void showCurrent(bool fullRefresh) {
  switch (current) {
    case SCREEN_READING: readingShow(fullRefresh); break;
    default: break;
  }
}

void appGoTo(Screen s) {
  current = s;
  showCurrent(true);
}

void appBegin() {
  canvas.setRotation(SCREEN_ROTATION);
  appGoTo(SCREEN_READING);
}

void appTurnPage(int delta) {
  if (current == SCREEN_READING) readingTurnPage(delta);
}

// Taps arrive in raw touch coordinates. Normalise once here, so every screen
// hit-tests in plain 480 x 800 screen space.
void appTap(uint16_t rawX, uint16_t rawY) {
  int16_t x = TOUCH_FLIP_X ? (int16_t)(TOUCH_WIDTH - 1 - rawX) : (int16_t)rawX;
  int16_t y = (int16_t)rawY;
  Serial.printf("tap at %d,%d\n", x, y);
  switch (current) {
    case SCREEN_READING: readingTap(x, y); break;
    default: break;
  }
}
```

`reader/app.h` is unchanged — `appBegin`, `appTap` and `appTurnPage` keep their signatures, so `reader.ino` and `simulator/main.cpp` need no edits.

- [ ] **Step 8: Run the tests and the simulator**

Run: `cd simulator && make test`
Expected: `all tests passed`

Run: `make run`
Expected: the book opens exactly as before; clicking left third goes back, the rest forward; arrow keys still work.

- [ ] **Step 9: Commit**

```bash
git add reader/screens.h reader/reading.h reader/reading.cpp reader/app.cpp \
        simulator/tests.cpp simulator/Makefile
git commit -m "Add screen router and move the reading screen out of app.cpp"
```

---

### Task 2: The book table

**Files:**
- Create: `reader/books.h`, `reader/texts/frankenstein.h`, `reader/texts/dracula.h`, `reader/texts/moby_dick.h`
- Modify: `simulator/tests.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `struct Book { const char *title; const char *author; const char *text; const uint8_t *cover; }`, `extern const Book BOOKS[]`, `extern const uint8_t BOOK_COUNT` (`books.h`).

- [ ] **Step 1: Write the failing test**

Add to `simulator/tests.cpp`, with `#include "books.h"` at the top and the call added to `main()`:

```cpp
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
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd simulator && make test`
Expected: compile error — `books.h` does not exist.

- [ ] **Step 3: Create the three new text files**

Same format as `sample_text.h`: one paragraph per line, `# ` marks a heading, `_underscores_` mark italics. Each is a public-domain opening, long enough to fill two or three pages.

`reader/texts/frankenstein.h`:

```c
// Frankenstein, Mary Shelley (1818). Public domain. Opening letter.
#pragma once

static const char FRANKENSTEIN_TEXT[] =
  "# Letter I\n"
  "You will rejoice to hear that no disaster has accompanied the commencement of an enterprise which you have regarded with such evil forebodings. I arrived here yesterday, and my first task is to assure my dear sister of my welfare and increasing confidence in the success of my undertaking.\n"
  "I am already far north of London, and as I walk in the streets of Petersburgh, I feel a cold northern breeze play upon my cheeks, which braces my nerves and fills me with delight. Do you understand this feeling?\n"
  "This breeze, which has travelled from the regions towards which I am advancing, gives me a foretaste of those icy climes. Inspirited by this wind of promise, my daydreams become more fervent and vivid.\n"
  "I try in vain to be persuaded that the pole is the seat of frost and desolation; it ever presents itself to my imagination as the region of beauty and delight. There, Margaret, the sun is for ever visible, its broad disk just skirting the horizon and diffusing a perpetual splendour.\n"
  "There, for with your leave, my sister, I will put some trust in preceding navigators, there snow and frost are banished; and, sailing over a calm sea, we may be wafted to a land surpassing in wonders and in beauty every region hitherto discovered on the habitable globe.\n"
  "Its productions and features may be without example, as the phenomena of the heavenly bodies undoubtedly are in those undiscovered solitudes. What may not be expected in a country of eternal light?\n"
  "I may there discover the wondrous power which attracts the needle and may regulate a thousand celestial observations that require only this voyage to render their seeming eccentricities consistent for ever.\n"
  "I shall satiate my ardent curiosity with the sight of a part of the world never before visited, and may tread a land never before imprinted by the foot of man. These are my enticements, and they are sufficient to conquer all fear of danger or death.\n"
  "These visions fade when I look upon my little boat, and upon the great expanse of water that must be crossed before I am rewarded by the first sight of that unvisited shore.\n"
  "Yet I shall kill no albatross; therefore do not be alarmed for my safety or if I should come back to you as worn and woeful as the _Ancient Mariner_. You will smile at my allusion, but I will disclose a secret.\n"
  "I have often attributed my attachment to, my passionate enthusiasm for, the dangerous mysteries of ocean to that production of the most imaginative of modern poets. There is something at work in my soul which I do not understand.\n"
  "I am practically industrious, painstaking, a workman to execute with perseverance and labour, but besides this there is a love for the marvellous, a belief in the marvellous, intertwined in all my projects, which hurries me out of the common pathways of men.\n";
```

`reader/texts/dracula.h`:

```c
// Dracula, Bram Stoker (1897). Public domain. Opening of Jonathan Harker's
// journal.
#pragma once

static const char DRACULA_TEXT[] =
  "# Chapter I\n"
  "_3 May. Bistritz._ Left Munich at 8:35 P. M., on 1st May, arriving at Vienna early next morning; should have arrived at 6:46, but train was an hour late. Buda-Pesth seems a wonderful place, from the glimpse which I got of it from the train and the little I could walk through the streets.\n"
  "I feared to go very far from the station, as we had arrived late and would start as near the correct time as possible. The impression I had was that we were leaving the West and entering the East.\n"
  "The most western of splendid bridges over the Danube, which is here of noble width and depth, took us among the traditions of Turkish rule. We left in pretty good time, and came after nightfall to Klausenburgh.\n"
  "Here I stopped for the night at the Hotel Royale. I had for dinner, or rather supper, a chicken done up some way with red pepper, which was very good but thirsty. I got my dinner here and made a note of the recipe for Mina.\n"
  "I found my smattering of German very useful here; indeed, I don't know how I should be able to get on without it. Having had some time at my disposal when in London, I had visited the British Museum, and made search among the books and maps in the library regarding Transylvania.\n"
  "It had struck me that some foreknowledge of the country could hardly fail to have some importance in dealing with a nobleman of that country. I find that the district he named is in the extreme east of the country.\n"
  "It is just on the borders of three states, Transylvania, Moldavia and Bukovina, in the midst of the Carpathian mountains; one of the wildest and least known portions of Europe.\n"
  "I was not able to light on any map or work giving the exact locality of the Castle Dracula, as there are no maps of this country as yet to compare with our own Ordnance Survey maps.\n"
  "But I found that Bistritz, the post town named by Count Dracula, is a fairly well-known place. I shall enter here some of my notes, as they may refresh my memory when I talk over my travels with Mina.\n"
  "In the population of Transylvania there are four distinct nationalities: Saxons in the South, and mixed with them the Wallachs, who are the descendants of the Dacians; Magyars in the West, and Szekelys in the East and North.\n"
  "I am going among the latter, who claim to be descended from Attila and the Huns. This may be so, for when the Magyars conquered the country in the eleventh century they found the Huns settled in it.\n"
  "I read that every known superstition in the world is gathered into the horseshoe of the Carpathians, as if it were the centre of some sort of imaginative whirlpool; if so my stay may be very interesting.\n";
```

`reader/texts/moby_dick.h`:

```c
// Moby-Dick, Herman Melville (1851). Public domain. Opening of chapter 1.
#pragma once

static const char MOBY_DICK_TEXT[] =
  "# Loomings\n"
  "Call me Ishmael. Some years ago, never mind how long precisely, having little or no money in my purse, and nothing particular to interest me on shore, I thought I would sail about a little and see the watery part of the world.\n"
  "It is a way I have of driving off the spleen and regulating the circulation. Whenever I find myself growing grim about the mouth; whenever it is a damp, drizzly November in my soul; whenever I find myself involuntarily pausing before coffin warehouses, and bringing up the rear of every funeral I meet.\n"
  "And especially whenever my hypos get such an upper hand of me, that it requires a strong moral principle to prevent me from deliberately stepping into the street, and methodically knocking people's hats off, then, I account it high time to get to sea as soon as I can.\n"
  "This is my substitute for pistol and ball. With a philosophical flourish Cato throws himself upon his sword; I quietly take to the ship. There is nothing surprising in this.\n"
  "If they but knew it, almost all men in their degree, some time or other, cherish very nearly the same feelings towards the ocean with me.\n"
  "There now is your insular city of the Manhattoes, belted round by wharves as Indian isles by coral reefs, commerce surrounds it with her surf. Right and left, the streets take you waterward.\n"
  "Its extreme downtown is the battery, where that noble mole is washed by waves, and cooled by breezes, which a few hours previous were out of sight of land. Look at the crowds of water-gazers there.\n"
  "Circumambulate the city of a dreamy Sabbath afternoon. Go from Corlears Hook to Coenties Slip, and from thence, by Whitehall, northward. What do you see? Posted like silent sentinels all around the town, stand thousands upon thousands of mortal men fixed in ocean reveries.\n"
  "Some leaning against the spiles; some seated upon the pier-heads; some looking over the bulwarks of ships from China; some high aloft in the rigging, as if striving to get a still better seaward peep.\n"
  "But these are all landsmen; of week days pent up in lath and plaster, tied to counters, nailed to benches, clinched to desks. How then is this? Are the green fields gone? What do they here?\n"
  "But look! here come more crowds, pacing straight for the water, and seemingly bound for a dive. Strange! Nothing will content them but the extremest limit of the land.\n"
  "Yes, as every one knows, meditation and water are wedded for ever. Say you are in the country; in some high land of lakes. Take almost any path you please, and ten to one it carries you down in a dale, and leaves you there by a pool in the stream.\n";
```

- [ ] **Step 4: Create `reader/books.h`**

```c
// The shelf. Everything here is const so the ESP32 keeps it in flash rather
// than RAM.
//
// Text format is the one layout.h documents: one paragraph per line, "# " for
// a heading, _underscores_ for italics.
#pragma once
#include <stdint.h>

#include "sample_text.h"
#include "texts/dracula.h"
#include "texts/frankenstein.h"
#include "texts/moby_dick.h"

struct Book {
  const char *title;
  const char *author;
  const char *text;
  const uint8_t *cover;  // 1bpp 204 x 306, 1 = ink; nullptr = placeholder
};

static const Book BOOKS[] = {
  { "Pride and Prejudice", "Jane Austen", SAMPLE_TEXT, nullptr },
  { "Frankenstein", "Mary Shelley", FRANKENSTEIN_TEXT, nullptr },
  { "Dracula", "Bram Stoker", DRACULA_TEXT, nullptr },
  { "Moby-Dick", "Herman Melville", MOBY_DICK_TEXT, nullptr },
};

static const uint8_t BOOK_COUNT = sizeof(BOOKS) / sizeof(BOOKS[0]);
```

Covers stay `nullptr` until Task 4; Task 3 gives them a placeholder to draw.

- [ ] **Step 5: Run the tests**

Run: `cd simulator && make test`
Expected: `all tests passed`

- [ ] **Step 6: Commit**

```bash
git add reader/books.h reader/texts/ simulator/tests.cpp
git commit -m "Add the book table with four public-domain sample books"
```

---

### Task 3: The cover helper

**Files:**
- Create: `reader/cover.h`, `reader/cover.cpp`
- Modify: `simulator/tests.cpp`

**Interfaces:**
- Consumes: `struct Book` (Task 2).
- Produces:
  - `COVER_W = 204`, `COVER_H = 306`, `COVER_ROW_BYTES = 26` (`cover.h`)
  - `bool coverBit(const uint8_t *cover, int16_t sx, int16_t sy)` (`cover.h`)
  - `int16_t coverSrcIndex(int16_t dst, int16_t dstSize, int16_t srcSize)` (`cover.h`)
  - `void drawCover(Adafruit_GFX &gfx, const Book &book, int16_t x, int16_t y, int16_t w, int16_t h)` (`cover.h`)

- [ ] **Step 1: Write the failing test**

Add to `simulator/tests.cpp`, with `#include "cover.h"` and the call added to `main()`:

```cpp
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
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd simulator && make test`
Expected: compile error — `cover.h` does not exist.

- [ ] **Step 3: Create `reader/cover.h`**

```c
// Draws a book cover at any size, and a typographic stand-in when a book has
// no cover art.
//
// Cover bitmaps are 1 bit per pixel, 1 = ink (black), MSB first, each row
// padded to whole bytes: the layout Adafruit_GFX::drawBitmap expects.
#pragma once
#include <Adafruit_GFX.h>
#include <stdint.h>

#include "books.h"

static const int16_t COVER_W = 204;
static const int16_t COVER_H = 306;
static const int16_t COVER_ROW_BYTES = (COVER_W + 7) / 8;  // 26

// True if the source pixel is ink.
bool coverBit(const uint8_t *cover, int16_t sx, int16_t sy);

// Nearest-neighbour: which source pixel a destination pixel samples.
int16_t coverSrcIndex(int16_t dst, int16_t dstSize, int16_t srcSize);

// Draws `book`'s cover into the w x h box at (x, y), scaling as needed, with
// a 1px frame. Falls back to title and author centred in the frame when the
// book has no cover.
void drawCover(Adafruit_GFX &gfx, const Book &book, int16_t x, int16_t y,
               int16_t w, int16_t h);
```

- [ ] **Step 4: Create `reader/cover.cpp`**

```cpp
#include "cover.h"
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <string.h>

static const uint16_t INK = 0x0000;
static const uint16_t PAPER = 0xFFFF;

bool coverBit(const uint8_t *cover, int16_t sx, int16_t sy) {
  return cover[sy * COVER_ROW_BYTES + sx / 8] & (0x80 >> (sx % 8));
}

int16_t coverSrcIndex(int16_t dst, int16_t dstSize, int16_t srcSize) {
  return (int16_t)(((int32_t)dst * srcSize) / dstSize);
}

// Draws text centred in the box, truncating with "..." if it does not fit.
static void drawCentred(Adafruit_GFX &gfx, const char *text, const GFXfont *font,
                        int16_t boxX, int16_t boxW, int16_t baseline) {
  char buf[48];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  gfx.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  gfx.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  while (w > boxW && strlen(buf) > 4) {
    size_t n = strlen(buf);
    strcpy(buf + n - 4, "...");
    gfx.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  }
  gfx.setCursor(boxX + (boxW - (int16_t)w) / 2 - x1, baseline);
  gfx.print(buf);
}

void drawCover(Adafruit_GFX &gfx, const Book &book, int16_t x, int16_t y,
               int16_t w, int16_t h) {
  gfx.fillRect(x, y, w, h, PAPER);

  if (book.cover) {
    for (int16_t py = 0; py < h; py++) {
      int16_t sy = coverSrcIndex(py, h, COVER_H);
      for (int16_t px = 0; px < w; px++) {
        int16_t sx = coverSrcIndex(px, w, COVER_W);
        if (coverBit(book.cover, sx, sy)) gfx.drawPixel(x + px, y + py, INK);
      }
    }
  } else {
    // No cover art: set the title and author inside the frame instead.
    gfx.setTextColor(INK);
    gfx.setTextWrap(false);
    int16_t inset = 10;
    drawCentred(gfx, book.title, &FreeSerifBold9pt7b, x + inset, w - 2 * inset,
                y + h / 2 - 6);
    drawCentred(gfx, book.author, &FreeSerif9pt7b, x + inset, w - 2 * inset,
                y + h / 2 + 18);
  }

  gfx.drawRect(x, y, w, h, INK);
}
```

- [ ] **Step 5: Run the tests**

Run: `cd simulator && make test`
Expected: `all tests passed`

- [ ] **Step 6: Commit**

```bash
git add reader/cover.h reader/cover.cpp simulator/tests.cpp
git commit -m "Add drawCover, which scales a cover to any box and falls back to type"
```

---

### Task 4: The cover generation tool

**Files:**
- Create: `tools/make_cover.py`, `tools/README.md`
- Modify: `reader/books.h`
- Test: `tools/test_make_cover.py`

**Interfaces:**
- Consumes: the bit convention from Task 3.
- Produces: `reader/covers/<slug>.h`, each defining `static const uint8_t COVER_<SLUG>[COVER_BYTES]`.

- [ ] **Step 1: Write the failing test**

`tools/test_make_cover.py`:

```python
"""Checks that make_cover.py emits a header matching reader/cover.h's layout."""
import pathlib
import re
import subprocess
import sys
import tempfile

from PIL import Image

HERE = pathlib.Path(__file__).parent
COVER_W, COVER_H, ROW_BYTES = 204, 306, 26


def test_emits_correctly_sized_array():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        src = tmp / "in.png"
        # Left half black, right half white, so bit order is observable.
        img = Image.new("L", (100, 150), 255)
        for y in range(150):
            for x in range(50):
                img.putpixel((x, y), 0)
        img.save(src)

        subprocess.run(
            [sys.executable, str(HERE / "make_cover.py"), str(src), "testbook",
             "--out-dir", str(tmp)],
            check=True,
        )

        text = (tmp / "testbook.h").read_text()
        assert "COVER_TESTBOOK" in text
        values = re.findall(r"0x[0-9a-fA-F]{2}", text)
        assert len(values) == ROW_BYTES * COVER_H

        # First byte of a row is the left edge, which is black -> all ink bits.
        assert int(values[0], 16) == 0xFF
        # Last byte of a row is the right edge, which is white -> no ink bits.
        assert int(values[ROW_BYTES - 1], 16) == 0x00
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `python3 -m pytest tools/test_make_cover.py -v`
Expected: FAIL — `make_cover.py` does not exist. (If pytest is missing: `python3 -m pip install pytest pillow`.)

- [ ] **Step 3: Write `tools/make_cover.py`**

```python
#!/usr/bin/env python3
"""Turns a cover image into a 1-bit C array for the e-reader.

    python3 tools/make_cover.py cover.jpg pride

writes reader/covers/pride.h containing COVER_PRIDE.

The panel is 1 bit per pixel, so the image is Floyd-Steinberg dithered rather
than thresholded: on e-ink that choice is the difference between a cover that
reads as artwork and one that reads as a blob.

Bit convention, matching reader/cover.h: 1 = ink (black), MSB first, each row
padded to whole bytes.
"""
import argparse
import pathlib

from PIL import Image

COVER_W, COVER_H = 204, 306
ROW_BYTES = (COVER_W + 7) // 8


def pack(img: Image.Image) -> bytes:
    out = bytearray(ROW_BYTES * COVER_H)
    for y in range(COVER_H):
        for x in range(COVER_W):
            # Mode "1": 0 is black. Our arrays use 1 for ink.
            if img.getpixel((x, y)) == 0:
                out[y * ROW_BYTES + x // 8] |= 0x80 >> (x % 8)
    return bytes(out)


def emit(data: bytes, slug: str) -> str:
    name = f"COVER_{slug.upper()}"
    lines = [
        f"// Generated by tools/make_cover.py. Do not edit by hand.",
        f"// {COVER_W} x {COVER_H}, 1bpp, 1 = ink, MSB first.",
        "#pragma once",
        "#include <stdint.h>",
        "",
        f"static const uint8_t {name}[] = {{",
    ]
    for i in range(0, len(data), 16):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i:i + 16])
        lines.append(f"  {chunk},")
    lines += ["};", ""]
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image")
    parser.add_argument("slug", help="file and symbol name, e.g. 'pride'")
    parser.add_argument(
        "--out-dir",
        default=str(pathlib.Path(__file__).parent.parent / "reader" / "covers"),
    )
    args = parser.parse_args()

    img = Image.open(args.image).convert("L")
    img = img.resize((COVER_W, COVER_H), Image.LANCZOS)
    img = img.convert("1")  # Floyd-Steinberg by default

    out_dir = pathlib.Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    path = out_dir / f"{args.slug}.h"
    path.write_text(emit(pack(img), args.slug))
    print(f"wrote {path} ({ROW_BYTES * COVER_H} bytes)")


if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `python3 -m pytest tools/test_make_cover.py -v`
Expected: PASS

- [ ] **Step 5: Write `tools/README.md`**

```markdown
# Tools

## make_cover.py

Turns a cover image into the 1-bit C array the reader draws.

    python3 -m pip install pillow
    python3 tools/make_cover.py path/to/cover.jpg pride

Writes `reader/covers/pride.h` defining `COVER_PRIDE`, then add it to
`reader/books.h`:

    #include "covers/pride.h"
    ...
    { "Pride and Prejudice", "Jane Austen", SAMPLE_TEXT, COVER_PRIDE },

Each cover is 204 x 306 and about 7.9 KB, which becomes roughly 40 KB of
source text. That is nothing against 4 MB of flash, but it is why the shelf
starts at four books rather than twenty.
```

- [ ] **Step 6: Generate covers and wire up one book**

Generate a cover for at least one book so the real path is exercised alongside the placeholder path, leaving the rest `nullptr` on purpose — both branches of `drawCover` then have a live example on screen.

```bash
python3 tools/make_cover.py <some image> pride
```

Then in `reader/books.h` add `#include "covers/pride.h"` and change the first row's cover from `nullptr` to `COVER_PRIDE`.

- [ ] **Step 7: Commit**

```bash
git add tools/ reader/covers/ reader/books.h
git commit -m "Add make_cover.py and generate the first book cover"
```

---

### Task 5: Library grid view

**Files:**
- Create: `reader/library.h`, `reader/library.cpp`
- Modify: `reader/app.cpp`, `reader/reading.h`, `reader/reading.cpp`, `simulator/tests.cpp`
- Test: `simulator/tests.cpp`

**Interfaces:**
- Consumes: `BOOKS`/`BOOK_COUNT` (Task 2), `drawCover` (Task 3), `appCanvas`/`appGoTo` (Task 1).
- Produces:
  - `enum LibraryAction : uint8_t { LIB_NONE, LIB_OPEN_BOOK, LIB_PREV_PAGE, LIB_NEXT_PAGE, LIB_TOGGLE_VIEW }` (`library.h`)
  - `struct LibraryHit { LibraryAction action; uint8_t book; }` (`library.h`)
  - `LibraryHit libraryHitTest(int16_t x, int16_t y, uint8_t page, uint8_t view)` (`library.h`)
  - `uint8_t libraryPageCount(uint8_t view)` (`library.h`)
  - `void libraryShow(bool fullRefresh)`, `void libraryTap(int16_t x, int16_t y)`, `void libraryTurnPage(int delta)` (`library.h`)
  - `void readingOpenBook(uint8_t index)` (`reading.h`)

- [ ] **Step 1: Write the failing test**

Add to `simulator/tests.cpp`, with `#include "library.h"` and the call in `main()`:

```cpp
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
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd simulator && make test`
Expected: compile error — `library.h` does not exist.

- [ ] **Step 3: Create `reader/library.h`**

```c
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
  uint8_t book;  // meaningful only when action is LIB_OPEN_BOOK
};

// What a tap at (x, y) means on the given page of the given view. Pure, so it
// is tested on the host.
LibraryHit libraryHitTest(int16_t x, int16_t y, uint8_t page, uint8_t view);

// How many pages the shelf needs in this view.
uint8_t libraryPageCount(uint8_t view);

void libraryShow(bool fullRefresh);
void libraryTap(int16_t x, int16_t y);
void libraryTurnPage(int delta);
```

- [ ] **Step 4: Create `reader/library.cpp` with the grid view**

The list view is added in Task 6; `libraryHitTest` and `drawBody` branch on view now so that task is a pure addition.

```cpp
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
static const int16_t BODY_TOP = 48;
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

// Draws text centred in a box, truncating with "..." if it does not fit.
static void drawCentred(Adafruit_GFX &gfx, const char *text, const GFXfont *font,
                        int16_t boxX, int16_t boxW, int16_t baseline) {
  char buf[48];
  strncpy(buf, text, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  gfx.setFont(font);
  int16_t x1, y1;
  uint16_t w, h;
  gfx.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  while (w > boxW && strlen(buf) > 4) {
    strcpy(buf + strlen(buf) - 4, "...");
    gfx.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  }
  gfx.setCursor(boxX + (boxW - (int16_t)w) / 2 - x1, baseline);
  gfx.print(buf);
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
  drawCentred(gfx, label, &FreeSerif9pt7b, 0, SCREEN_W, SCREEN_H - 14);
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
    drawCentred(gfx, BOOKS[index].title, &FreeSerifBold9pt7b, x, COVER_W,
                y + COVER_H + 20);
    drawCentred(gfx, BOOKS[index].author, &FreeSerif9pt7b, x, COVER_W,
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
```

- [ ] **Step 5: Add `readingOpenBook` to the reading screen**

In `reader/reading.h`, add:

```c
// Switches to book `index`, at the start (or its saved position once
// persistence exists).
void readingOpenBook(uint8_t index);
```

In `reader/reading.cpp`, replace the `SAMPLE_TEXT` hardcoding. Delete the `#include "sample_text.h"` line, add `#include "books.h"`, and replace the `layout`/`pageStarts` declarations and `readingShow`'s lazy init with:

```cpp
static PageLayout *layout = nullptr;
static uint8_t currentBook = 0;
static std::vector<PagePos> pageStarts;
static size_t currentPage = 0;
static uint8_t turnsSinceFullRefresh = 0;

void readingOpenBook(uint8_t index) {
  if (index >= BOOK_COUNT) return;
  currentBook = index;
  delete layout;
  layout = new PageLayout(appCanvas(), BOOKS[index].text, fonts);
  pageStarts.clear();
  pageStarts.push_back({ 0, false });
  currentPage = 0;
}
```

and change the top of `readingShow` to:

```cpp
void readingShow(bool fullRefresh) {
  if (!layout) readingOpenBook(currentBook);
  unsigned long t0 = millis();
  ...
```

- [ ] **Step 6: Make the library the home screen in `reader/app.cpp`**

Add `#include "library.h"`, change the initial screen, and add the library case to all three switches:

```cpp
static Screen current = SCREEN_LIBRARY;

static void showCurrent(bool fullRefresh) {
  switch (current) {
    case SCREEN_LIBRARY: libraryShow(fullRefresh); break;
    case SCREEN_READING: readingShow(fullRefresh); break;
  }
}

void appBegin() {
  canvas.setRotation(SCREEN_ROTATION);
  appGoTo(SCREEN_LIBRARY);
}

void appTurnPage(int delta) {
  switch (current) {
    case SCREEN_LIBRARY: libraryTurnPage(delta); break;
    case SCREEN_READING: readingTurnPage(delta); break;
  }
}
```

and in `appTap`:

```cpp
  switch (current) {
    case SCREEN_LIBRARY: libraryTap(x, y); break;
    case SCREEN_READING: readingTap(x, y); break;
  }
```

- [ ] **Step 7: Run the tests and the simulator**

Run: `cd simulator && make test`
Expected: `all tests passed`

Run: `make run`
Expected: the library opens with four covers — one real, three typographic placeholders. Clicking a cover opens that book. Arrow keys page the library when it is showing.

Note there is no way back to the library yet; that is Task 7. Quit and restart to check another book.

- [ ] **Step 8: Commit**

```bash
git add reader/library.h reader/library.cpp reader/app.cpp reader/reading.h \
        reader/reading.cpp simulator/tests.cpp
git commit -m "Add the library grid view and make it the home screen"
```

---

### Task 6: Library list view with progress

**Files:**
- Modify: `reader/library.cpp`, `reader/reading.h`, `reader/reading.cpp`, `simulator/tests.cpp`

**Interfaces:**
- Consumes: everything from Task 5.
- Produces: `uint32_t readingProgressPercent(uint8_t book)` (`reading.h`).

- [ ] **Step 1: Write the failing test**

Add to `simulator/tests.cpp` and call it from `main()`:

```cpp
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
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd simulator && make test`
Expected: FAIL — `libraryHitTest` returns `LIB_NONE` for list-view body taps.

- [ ] **Step 3: Add list hit-testing to `reader/library.cpp`**

Add the constants next to the grid ones:

```cpp
// List: six rows, thumbnail then title, author and a progress bar.
static const int16_t LIST_PER_PAGE = 6;
static const int16_t LIST_TOP = 50;
static const int16_t LIST_ROW_H = 118;
static const int16_t LIST_TEXT_X = 112;   // MARGIN_X + THUMB_W + 16
static const int16_t LIST_BAR_W = 280;
static const int16_t LIST_BAR_H = 8;
```

Replace the `6` in `libraryPageCount` with `LIST_PER_PAGE`, add:

```cpp
static LibraryHit listHitTest(int16_t x, int16_t y, uint8_t p) {
  (void)x;  // a row is hit anywhere across its width
  if (y < LIST_TOP) return { LIB_NONE, 0 };
  int16_t row = (y - LIST_TOP) / LIST_ROW_H;
  if (row < 0 || row >= LIST_PER_PAGE) return { LIB_NONE, 0 };
  uint8_t index = (uint8_t)(p * LIST_PER_PAGE + row);
  if (index >= BOOK_COUNT) return { LIB_NONE, 0 };
  return { LIB_OPEN_BOOK, index };
}
```

and change the last line of `libraryHitTest`:

```cpp
  if (v == LIB_VIEW_GRID) return gridHitTest(x, y, p);
  return listHitTest(x, y, p);
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `cd simulator && make test`
Expected: `all tests passed`

- [ ] **Step 5: Add `readingProgressPercent`**

In `reader/reading.h`:

```c
// How far through `book` the reader has got, 0-100. Books never opened read 0.
uint32_t readingProgressPercent(uint8_t book);
```

In `reader/reading.cpp` — for now only the current book has a position, which
Task 8 replaces with stored positions for every book:

```cpp
uint32_t readingProgressPercent(uint8_t book) {
  if (book >= BOOK_COUNT) return 0;
  if (book != currentBook || pageStarts.empty()) return 0;
  uint32_t len = (uint32_t)strlen(BOOKS[book].text);
  if (len == 0) return 0;
  return (uint32_t)((uint64_t)pageStarts[currentPage].offset * 100 / len);
}
```

Add `#include <string.h>` to `reading.cpp` if it is not already there.

- [ ] **Step 6: Draw the list view in `reader/library.cpp`**

```cpp
static void drawList(Adafruit_GFX &gfx) {
  for (uint8_t row = 0; row < LIST_PER_PAGE; row++) {
    uint8_t index = (uint8_t)(page * LIST_PER_PAGE + row);
    if (index >= BOOK_COUNT) break;
    int16_t top = LIST_TOP + row * LIST_ROW_H;

    drawCover(gfx, BOOKS[index], MARGIN_X, top + 5, THUMB_W, THUMB_H);

    gfx.setTextColor(INK);
    gfx.setFont(&FreeSerifBold9pt7b);
    gfx.setCursor(LIST_TEXT_X, top + 30);
    gfx.print(BOOKS[index].title);

    gfx.setFont(&FreeSerif9pt7b);
    gfx.setCursor(LIST_TEXT_X, top + 54);
    gfx.print(BOOKS[index].author);

    uint32_t pct = readingProgressPercent(index);
    int16_t barY = top + 72;
    gfx.drawRect(LIST_TEXT_X, barY, LIST_BAR_W, LIST_BAR_H, INK);
    int16_t filled = (int16_t)((LIST_BAR_W - 2) * pct / 100);
    if (filled > 0) gfx.fillRect(LIST_TEXT_X + 1, barY + 1, filled, LIST_BAR_H - 2, INK);

    char label[8];
    snprintf(label, sizeof(label), "%u%%", (unsigned)pct);
    gfx.setCursor(SCREEN_W - MARGIN_X - 36, barY + LIST_BAR_H);
    gfx.print(label);

    if (row + 1 < LIST_PER_PAGE && (uint8_t)(index + 1) < BOOK_COUNT) {
      gfx.drawFastHLine(MARGIN_X, top + LIST_ROW_H - 1, SCREEN_W - 2 * MARGIN_X, INK);
    }
  }
}
```

Add `#include "cover.h"` usage of `THUMB_W`/`THUMB_H` by adding them to `cover.h`:

```c
static const int16_t THUMB_W = 72;
static const int16_t THUMB_H = 108;
```

And branch in `libraryShow`:

```cpp
  if (view == LIB_VIEW_GRID) drawGrid(gfx);
  else drawList(gfx);
```

- [ ] **Step 7: Add the simulator's view keys**

In `simulator/main.cpp`, in the `SDL_KEYDOWN` switch, add a `v` key that toggles the library view by synthesising a tap on the toggle region, and an `l` key for Task 7's back-to-library:

```cpp
        case SDLK_v: appTap(400, 20); break;
```

Update the comment block at the top of `main.cpp` to list the new key, and extend the `--screenshot` sequence loop so `v` works there too:

```cpp
    for (const char *c = screenshotTurns; *c; c++) {
      if (*c == 'v') appTap(400, 20);
      else appTurnPage(*c == 'p' ? -1 : 1);
    }
```

- [ ] **Step 8: Run the tests and look at both views**

Run: `cd simulator && make test`
Expected: `all tests passed`

Run: `make run`, then press `v`
Expected: the shelf switches to the list view — thumbnail, title, author and a progress bar per row — and `v` switches back.

- [ ] **Step 9: Commit**

```bash
git add reader/library.cpp reader/cover.h reader/reading.h reader/reading.cpp \
        simulator/main.cpp simulator/tests.cpp
git commit -m "Add the library list view with reading progress"
```

---

### Task 7: The reading control bar

**Files:**
- Modify: `reader/reading.h`, `reader/reading.cpp`, `simulator/main.cpp`, `simulator/tests.cpp`

**Interfaces:**
- Consumes: `appGoTo` (Task 1).
- Produces: `readingHitTest` **changes signature** to `ReadingAction readingHitTest(int16_t x, int16_t y, bool controlsVisible)`, and `ReadingAction` gains `READ_SHOW_CONTROLS`, `READ_HIDE_CONTROLS`, `READ_BACK_TO_LIBRARY`.

- [ ] **Step 1: Rewrite the reading hit-test test**

Replace `testReadingHitTest` in `simulator/tests.cpp` with:

```cpp
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

  // The back arrow only counts inside the bar, not down the left edge.
  CHECK_EQ(readingHitTest(10, 65, true), READ_HIDE_CONTROLS);
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd simulator && make test`
Expected: compile error — `readingHitTest` takes one argument, and `READ_SHOW_CONTROLS` is undeclared.

- [ ] **Step 3: Update `reader/reading.h`**

```c
enum ReadingAction : uint8_t {
  READ_NONE,
  READ_PREV,
  READ_NEXT,
  READ_SHOW_CONTROLS,
  READ_HIDE_CONTROLS,
  READ_BACK_TO_LIBRARY,
};

// What a tap means, given whether the control bar is up. Pure, so it is
// tested on the host.
ReadingAction readingHitTest(int16_t x, int16_t y, bool controlsVisible);
```

- [ ] **Step 4: Update `reader/reading.cpp`**

Add the constants and rewrite the hit-test and tap handler:

```cpp
static const int16_t SCREEN_W = 480;
static const int16_t BAR_H = 64;
static const int16_t BACK_W = 80;

static bool controlsVisible = false;

ReadingAction readingHitTest(int16_t x, int16_t y, bool barUp) {
  if (barUp) {
    if (y < BAR_H && x < BACK_W) return READ_BACK_TO_LIBRARY;
    return READ_HIDE_CONTROLS;
  }
  if (x < SCREEN_W / 3) return READ_PREV;
  if (x < 2 * SCREEN_W / 3) return READ_SHOW_CONTROLS;
  return READ_NEXT;
}

void readingTap(int16_t x, int16_t y) {
  switch (readingHitTest(x, y, controlsVisible)) {
    case READ_PREV: readingTurnPage(-1); break;
    case READ_NEXT: readingTurnPage(1); break;
    case READ_SHOW_CONTROLS:
      controlsVisible = true;
      readingShow(false);  // a small delta: the fast refresh
      break;
    case READ_HIDE_CONTROLS:
      controlsVisible = false;
      readingShow(false);
      break;
    case READ_BACK_TO_LIBRARY:
      controlsVisible = false;
      appGoTo(SCREEN_LIBRARY);
      break;
    default: break;
  }
}
```

Draw the bar at the end of `readingShow`, after `layoutPage` and before the
refresh call. Add `#include <Fonts/FreeSerifBold9pt7b.h>` and
`#include "books.h"`:

```cpp
static void drawControlBar(Adafruit_GFX &gfx) {
  const uint16_t INK = 0x0000, PAPER = 0xFFFF;
  gfx.fillRect(0, 0, SCREEN_W, BAR_H, PAPER);
  gfx.drawFastHLine(0, BAR_H - 1, SCREEN_W, INK);

  // Back arrow: a triangle with a shaft, pointing left.
  gfx.fillTriangle(24, 32, 38, 22, 38, 42, INK);
  gfx.drawFastHLine(38, 32, 22, INK);

  gfx.setTextColor(INK);
  gfx.setFont(&FreeSerifBold9pt7b);
  int16_t x1, y1;
  uint16_t w, h;
  gfx.getTextBounds(BOOKS[currentBook].title, 0, 0, &x1, &y1, &w, &h);
  gfx.setCursor((SCREEN_W - (int16_t)w) / 2 - x1, 38);
  gfx.print(BOOKS[currentBook].title);
}
```

and in `readingShow`, immediately before the `epdShow...` calls:

```cpp
  if (controlsVisible) drawControlBar(appCanvas());
```

`readingTurnPage` should hide the bar first, since the page underneath is
about to change:

```cpp
void readingTurnPage(int delta) {
  controlsVisible = false;
  if (delta > 0 && currentPage + 1 >= pageStarts.size()) return;  // last page
  ...
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `cd simulator && make test`
Expected: `all tests passed`

- [ ] **Step 6: Add the simulator's library key**

In `simulator/main.cpp`'s `SDL_KEYDOWN` switch:

```cpp
        case SDLK_l: appTap(240, 400); appTap(40, 30); break;  // controls, then back
```

Update the top-of-file comment block and `simulator/README.md`'s input table to list `v` and `l`.

- [ ] **Step 7: Check it by hand**

Run: `cd simulator && make run`
Expected: open a book, tap the middle of the page — a bar appears at the top with a back arrow and the book's title. Tap the arrow to return to the library. Tap the middle again to dismiss the bar without leaving.

- [ ] **Step 8: Commit**

```bash
git add reader/reading.h reader/reading.cpp simulator/main.cpp \
        simulator/tests.cpp simulator/README.md
git commit -m "Add the reading control bar with a way back to the library"
```

---

### Task 8: Persistence

**Files:**
- Create: `reader/store.h`, `reader/store.cpp`
- Modify: `reader/reading.cpp`, `reader/library.cpp`, `reader/app.cpp`, `simulator/Makefile`, `simulator/main.cpp`, `simulator/.gitignore`, `simulator/tests.cpp`

**Interfaces:**
- Consumes: everything above.
- Produces: `storeBegin`, `storeSaveProgress`, `storeLoadProgress`, `storeSaveView`, `storeLoadView`, and the pure helpers `storePack`/`storeUnpack` (`store.h`).

- [ ] **Step 1: Write the failing test**

Add to `simulator/tests.cpp`, with `#include "store.h"` and the call in `main()`:

```cpp
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
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `cd simulator && make test`
Expected: compile error — `store.h` does not exist.

- [ ] **Step 3: Create `reader/store.h`**

```c
// Saved reading positions and the chosen library view, kept in the ESP32's
// NVS flash so they survive a reboot.
//
// store.cpp uses the ESP32's Preferences library, so it is a hardware-only
// file: the simulator provides its own implementation in main.cpp, backed by
// a file, so it behaves the same way.
#pragma once
#include <stdint.h>

// Offset and italic travel as one uint32: offset in bits 0-30, italic in bit
// 31. One NVS entry per save rather than two, and one key per book. Text
// lengths are nowhere near 2^31. These two are pure, so they are tested.
uint32_t storePack(uint32_t offset, bool italic);
void storeUnpack(uint32_t packed, uint32_t &offset, bool &italic);

void storeBegin();
void storeSaveProgress(uint8_t book, uint32_t offset, bool italic);
bool storeLoadProgress(uint8_t book, uint32_t &offset, bool &italic);
void storeSaveView(uint8_t view);   // 0 = grid, 1 = list
uint8_t storeLoadView();            // 0 (grid) if never set
```

- [ ] **Step 4: Create `reader/store.cpp`**

```cpp
#include "store.h"
#include <Preferences.h>

static Preferences prefs;

uint32_t storePack(uint32_t offset, bool italic) {
  return (offset & 0x7FFFFFFFu) | (italic ? 0x80000000u : 0u);
}

void storeUnpack(uint32_t packed, uint32_t &offset, bool &italic) {
  offset = packed & 0x7FFFFFFFu;
  italic = (packed & 0x80000000u) != 0;
}

void storeBegin() {
  prefs.begin("reader", false);
}

static void progressKey(uint8_t book, char *out, size_t n) {
  snprintf(out, n, "p%u", (unsigned)book);
}

void storeSaveProgress(uint8_t book, uint32_t offset, bool italic) {
  char key[8];
  progressKey(book, key, sizeof(key));
  // NVS skips a write whose value is unchanged, so repeated saves at the same
  // position cost nothing.
  prefs.putUInt(key, storePack(offset, italic));
}

bool storeLoadProgress(uint8_t book, uint32_t &offset, bool &italic) {
  char key[8];
  progressKey(book, key, sizeof(key));
  if (!prefs.isKey(key)) return false;
  storeUnpack(prefs.getUInt(key, 0), offset, italic);
  return true;
}

void storeSaveView(uint8_t view) {
  prefs.putUChar("view", view);
}

uint8_t storeLoadView() {
  return prefs.getUChar("view", 0);
}
```

- [ ] **Step 5: Mark it hardware-only and write the simulator stand-in**

In `simulator/Makefile`:

```make
HARDWARE_ONLY := $(READER)/epd.cpp $(READER)/touch.cpp $(READER)/store.cpp
```

Note `storePack`/`storeUnpack` live in `store.cpp`, which the tests no longer
compile — so move those two functions into `store.h` as `inline`, and delete
them from `store.cpp`. That keeps them testable on the host while the NVS
calls stay hardware-only:

```c
inline uint32_t storePack(uint32_t offset, bool italic) {
  return (offset & 0x7FFFFFFFu) | (italic ? 0x80000000u : 0u);
}

inline void storeUnpack(uint32_t packed, uint32_t &offset, bool &italic) {
  offset = packed & 0x7FFFFFFFu;
  italic = (packed & 0x80000000u) != 0;
}
```

In `simulator/main.cpp`, next to the `epd` stand-ins, add a file-backed one so
the simulator keeps positions between runs like the device does:

```cpp
// ---- Stand-in for NVS (reader/store.h), backed by a file ----

static const char *STATE_FILE = "reader-sim.state";
static const int MAX_BOOKS = 32;

struct SimState {
  uint32_t progress[MAX_BOOKS];
  bool saved[MAX_BOOKS];
  uint8_t view;
};

static SimState simState = {};

static void simStateWrite() {
  FILE *f = fopen(STATE_FILE, "wb");
  if (!f) return;
  fwrite(&simState, sizeof(simState), 1, f);
  fclose(f);
}

void storeBegin() {
  FILE *f = fopen(STATE_FILE, "rb");
  if (!f) return;
  if (fread(&simState, sizeof(simState), 1, f) != 1) simState = SimState{};
  fclose(f);
}

void storeSaveProgress(uint8_t book, uint32_t offset, bool italic) {
  if (book >= MAX_BOOKS) return;
  simState.progress[book] = storePack(offset, italic);
  simState.saved[book] = true;
  simStateWrite();
}

bool storeLoadProgress(uint8_t book, uint32_t &offset, bool &italic) {
  if (book >= MAX_BOOKS || !simState.saved[book]) return false;
  storeUnpack(simState.progress[book], offset, italic);
  return true;
}

void storeSaveView(uint8_t view) {
  simState.view = view;
  simStateWrite();
}

uint8_t storeLoadView() {
  return simState.view;
}
```

Add `#include "store.h"` to `main.cpp`, and `reader-sim.state` to
`simulator/.gitignore`.

Also add the same stand-in functions to `simulator/tests.cpp` — the tests link
the reader sources but not `main.cpp`, so `library.cpp` and `reading.cpp`'s
calls need somewhere to land. A trivial in-memory version is enough:

```cpp
// ---- Stand-ins for persistence (reader/store.h) ----
void storeBegin() {}
void storeSaveProgress(uint8_t, uint32_t, bool) {}
bool storeLoadProgress(uint8_t, uint32_t &, bool &) { return false; }
void storeSaveView(uint8_t) {}
uint8_t storeLoadView() { return 0; }
```

- [ ] **Step 6: Run the test to verify it passes**

Run: `cd simulator && make test`
Expected: `all tests passed`

- [ ] **Step 7: Wire persistence into the screens**

In `reader/app.cpp`, call `storeBegin()` at the top of `appBegin()` and add
`#include "store.h"`.

In `reader/reading.cpp`, add `#include "store.h"`, restore on open and save on
every turn:

```cpp
void readingOpenBook(uint8_t index) {
  if (index >= BOOK_COUNT) return;
  currentBook = index;
  delete layout;
  layout = new PageLayout(appCanvas(), BOOKS[index].text, fonts);
  pageStarts.clear();

  uint32_t offset = 0;
  bool italic = false;
  storeLoadProgress(index, offset, italic);
  pageStarts.push_back({ offset, italic });
  currentPage = 0;
}

static void saveProgress() {
  if (pageStarts.empty()) return;
  storeSaveProgress(currentBook, pageStarts[currentPage].offset,
                    pageStarts[currentPage].italic);
}
```

Call `saveProgress()` at the end of `readingTurnPage`, and in `readingTap`'s
`READ_BACK_TO_LIBRARY` case before `appGoTo`.

Replace `readingProgressPercent` so it reports every book, not only the open
one:

```cpp
uint32_t readingProgressPercent(uint8_t book) {
  if (book >= BOOK_COUNT) return 0;
  uint32_t len = (uint32_t)strlen(BOOKS[book].text);
  if (len == 0) return 0;

  uint32_t offset = 0;
  bool italic = false;
  if (book == currentBook && !pageStarts.empty()) {
    offset = pageStarts[currentPage].offset;
  } else if (!storeLoadProgress(book, offset, italic)) {
    return 0;
  }
  return (uint32_t)((uint64_t)offset * 100 / len);
}
```

In `reader/library.cpp`, add `#include "store.h"`, initialise `view` from
storage in `libraryShow`'s first call, and persist the toggle:

```cpp
static bool viewLoaded = false;

void libraryShow(bool fullRefresh) {
  if (!viewLoaded) {
    view = storeLoadView();
    viewLoaded = true;
  }
  ...
```

and in the `LIB_TOGGLE_VIEW` case, after flipping `view`:

```cpp
      storeSaveView(view);
```

- [ ] **Step 8: Verify it end to end**

Run: `cd simulator && make test`
Expected: `all tests passed`

Run: `rm -f reader-sim.state && make run`
Expected: library opens in grid view, all progress at 0%. Open a book, turn a
few pages, go back to the library, switch to the list view — that book shows a
non-zero percentage. Quit, run again: the list view is still selected and the
percentage is still there.

- [ ] **Step 9: Check it on the device**

NVS is the one piece the simulator only imitates, so this needs real hardware.
Upload, open a book, turn several pages, return to the library, then power
cycle the board. Expected: the library comes back in the same view, with that
book's progress intact, and reopening it lands on the same page.

- [ ] **Step 10: Commit**

```bash
git add reader/store.h reader/store.cpp reader/reading.cpp reader/library.cpp \
        reader/app.cpp simulator/Makefile simulator/main.cpp \
        simulator/.gitignore simulator/tests.cpp
git commit -m "Persist reading positions and the library view in NVS"
```

---

### Task 9: Documentation

**Files:**
- Modify: `reader/README.md`, `simulator/README.md`

- [ ] **Step 1: Update `reader/README.md`'s file table**

Add rows for the new files and correct the description of `app.cpp`:

```markdown
| `app.h` / `app.cpp` | The screen router: owns the frame buffer, normalises taps, and hands them to whichever screen is showing. |
| `screens.h` | The list of screens, plus `appGoTo()` and the shared canvas. Adding a screen starts here. |
| `reading.h` / `reading.cpp` | The reading screen: which page of which book is showing, the control bar, and what a tap does. |
| `library.h` / `library.cpp` | The library (home) screen: the cover grid, the list view with progress, and the toggle between them. |
| `cover.h` / `cover.cpp` | Draws a cover at any size, and sets the title and author in a frame when a book has no cover art. |
| `store.h` / `store.cpp` | Saved reading positions and library view, in NVS flash. Hardware-only; the simulator stands in for it. |
| `books.h` | The shelf: title, author, text and cover for each book. |
| `texts/` | The book texts, in the format `layout.h` documents. |
| `covers/` | Generated 1-bit cover arrays. See `tools/make_cover.py`. |
```

Also replace the opening paragraph's description of the tap behaviour, which
is now wrong:

```markdown
It opens on a library of books shown with their covers, in either a cover grid
or a list with reading progress. Tap a book to read it; in a book, tap the
left third for the previous page, the right third for the next, and the middle
for a bar with a way back to the library.
```

And add a settings row to the Settings table:

```markdown
| Which books are on the shelf | `books.h` |
```

- [ ] **Step 2: Update `simulator/README.md`'s input table**

```markdown
| Input | Does |
|---|---|
| Click | Tap: in a book, left third = previous page, middle = controls, right third = next page. In the library, a cover or row opens that book. |
| Right arrow, `n`, space | Next page (or next library page) |
| Left arrow, `p` | Previous page (or previous library page) |
| `v` | Toggle the library between grid and list |
| `l` | Back to the library from a book |
| `q` or Esc | Quit |
```

Add a note under "Running" about the host tests and the state file:

```markdown
`make test` runs the host tests for the layout and hit-testing arithmetic —
no window, no hardware. Run it after changing anything in `library.cpp`,
`reading.cpp` or `cover.cpp`.

Reading positions are saved to `reader-sim.state` beside the binary, so the
simulator remembers where you were between runs, as the device does. Delete
it to start fresh.
```

- [ ] **Step 3: Commit**

```bash
git add reader/README.md simulator/README.md
git commit -m "Document the library screens and the new simulator keys"
```

---

## Self-review notes

Checked against the spec:

- **Spec coverage.** Every section maps to a task: router and extraction → 1; book model → 2; cover helper → 3; `make_cover.py` → 4; grid view, geometry, tap regions, library as home → 5; list view and progress → 6; reading tap thirds and control bar → 7; persistence, write policy, packing, simulator stand-in → 8; the refresh strategy is applied in 5–7 (full for screen changes and paging, partial for the control bar) and the simulator keys in 6–7.
- **Deviation from the spec, deliberate:** the spec puts `storePack`/`storeUnpack` in `store.cpp`. Task 8 Step 5 moves them into `store.h` as `inline`, because `store.cpp` is hardware-only and so is not linked into the host tests — leaving them in the `.cpp` would make the packing untestable, which is the one piece of persistence worth testing.
- **Type consistency.** `readingHitTest` deliberately changes signature between Task 1 and Task 7; both are spelled out, and Task 7's Interfaces block flags it. `LibraryHit`/`LibraryAction`, `THUMB_W`/`THUMB_H` (added to `cover.h` in Task 6) and the `store*` signatures are consistent across tasks.
- **Known rough edge:** after Task 5 there is no way back to the library until Task 7. Called out in Task 5's verification step rather than hidden.
