# Library UI — design

Date: 2026-09-29
Status: approved, ready for implementation planning

## Goal

Give the e-reader a library: a home screen showing every book with its cover,
from which you open a book to read. Two views of the same shelf — a cover grid
and a list with reading progress — with a toggle between them. Reading
position survives a reboot.

## Non-goals

These are explicitly out of scope and must not creep in:

- EPUB parsing. Books stay in the `sample_text.h` format (one paragraph per
  line, `# ` heading, `_italics_`).
- SD card or any filesystem book source. Books are compiled in.
- The book settings screen (font, frontlight). It is coming, and the screen
  router below is shaped so it costs one file and two lines — but it is not
  built here.
- Sorting, search, collections, or a book count beyond the 4–6 samples.

## Current state

`app.cpp` holds one hardcoded book and one `PageLayout`. `appTap` maps every
tap to a page turn: left third back, the rest forward. There is no concept of
a screen, and no persistence of any kind in the repo.

Two facts from the existing code that the design depends on:

- The panel is 800 × 480 native, drawn through a single `GFXcanvas1` with
  `SCREEN_ROTATION 3`, so all drawing happens in a **480 × 800 portrait**
  coordinate space. The existing text margin is `MARGIN_X = 24`.
- `appTap` receives *raw touch coordinates*. It applies `TOUCH_FLIP_X` inline
  and discards `y` entirely. `simulator/main.cpp` already feeds a true `y`
  through, so once `y` is honoured, grid hit-testing works in the simulator
  with no simulator-side change.

## Architecture

### Files

```
reader/
  screens.h          Screen enum, appGoTo(), appCanvas()
  app.cpp            router: owns the canvas, normalises taps, dispatches
  reading.h/.cpp     the reading screen (moved out of app.cpp)
  library.h/.cpp     the library screen: grid view, list view, toggle
  cover.h/.cpp       drawCover()
  store.h/.cpp       persistence (HARDWARE_ONLY — ESP32 Preferences/NVS)
  books.h            the book table
  covers/<slug>.h    generated 1-bit cover arrays
tools/
  make_cover.py      image -> covers/<slug>.h
```

`simulator/README.md` already states that any new `reader/*.cpp` is picked up
automatically by both the Arduino IDE and the simulator, and names
`library.cpp` as its example. Only `store.cpp` needs special handling.

### The screen router

```c
// screens.h
enum Screen : uint8_t {
  SCREEN_LIBRARY,
  SCREEN_READING,
  // SCREEN_BOOK_SETTINGS goes here
};

void appGoTo(Screen s);
GFXcanvas1 &appCanvas();   // the single shared 48 KB frame buffer
```

Each screen module exposes exactly two functions, e.g.:

```c
void libraryShow(bool fullRefresh);
void libraryTap(int16_t x, int16_t y);   // screen coordinates, 480 x 800
```

`app.cpp` switches on the current screen. Adding the settings screen later is:
one new file pair, one enum value, two lines in each switch. No base class and
no virtual dispatch — for a handful of screens on a microcontroller that is
ceremony without benefit.

`app.cpp` normalises coordinates **once**, so every screen hit-tests in plain
screen space:

```c
void appTap(uint16_t rawX, uint16_t rawY) {
  int16_t x = TOUCH_FLIP_X ? (TOUCH_WIDTH - 1 - (int16_t)rawX) : (int16_t)rawX;
  int16_t y = (int16_t)rawY;
  switch (current) {
    case SCREEN_LIBRARY: libraryTap(x, y); break;
    case SCREEN_READING: readingTap(x, y); break;
  }
}
```

`appTurnPage(int delta)` stays in `app.h` as a façade, so `reader.ino`'s serial
`n`/`p` and the simulator's arrow keys keep working unchanged. It forwards to
the reading screen, and pages the library when the library is showing.

`app.cpp` owns the one `GFXcanvas1(EPD_NATIVE_WIDTH, EPD_NATIVE_HEIGHT)` and
hands it out via `appCanvas()`. Screens must not allocate their own — 48 KB is
too much of the ESP32's RAM to duplicate.

### Book model

```c
// books.h
struct Book {
  const char *title;
  const char *author;
  const char *text;       // sample_text.h format
  const uint8_t *cover;   // 1bpp 204 x 306, 1 = ink; nullptr = placeholder
};

extern const Book BOOKS[];
extern const uint8_t BOOK_COUNT;
```

Everything is `const`, so the ESP32 keeps it in flash rather than RAM. Start
with 4–6 books: a cover is ~7.9 KB binary, which becomes roughly 40 KB of
source text per book as a C array. Harmless for 4 MB of flash, unpleasant to
have twenty of in a repo.

## Persistence

`store.h` is a tiny key/value façade over ESP32 NVS (`Preferences`, namespace
`"reader"`):

```c
void    storeBegin();
void    storeSaveProgress(uint8_t book, uint32_t offset, bool italic);
bool    storeLoadProgress(uint8_t book, uint32_t &offset, bool &italic);
void    storeSaveView(uint8_t view);   // 0 = grid, 1 = list
uint8_t storeLoadView();               // 0 (grid) if unset
```

Keys are `p<index>` / `i<index>` per book, plus `view`.

There is deliberately no "last book opened" key: the library is the home
screen, so nothing would read it.

**Write policy.** NVS has a finite erase-cycle budget, so progress is *not*
written on every page turn. It is written when leaving the reading screen, and
as a safety net against power loss, every 20 page turns. The view toggle is
written when it changes, which is rare.

`store.cpp` uses `Preferences.h`, which is ESP32-only, so it joins
`HARDWARE_ONLY` in `simulator/Makefile` and `simulator/main.cpp` provides a
stand-in backed by a small `reader-sim.state` file. That makes the simulator
behave like the device — progress persists between runs — and
`reader-sim.state` is added to `simulator/.gitignore`.

## Library screen

Geometry in the 480 × 800 space: header 48px, body y ∈ [48, 760) = 712px tall,
footer 40px. Horizontal margins are 24px, matching the reading page.

### Grid view (default)

Four books per page, covers 204 × 306.

- Columns at x = 24 and x = 252 (204 wide, 24px gutter; 252 + 204 + 24 = 480).
- Cell height 350: cover 306, 6px gap, title line 20, author line 18.
- Row tops at y = 54 and y = 408 (4px row gap; last row ends at 758).
- Title in `FreeSerifBold9pt7b`, author in `FreeSerif9pt7b`, both centred under
  the cover and truncated with `...` if wider than 204px.

`FreeSerifBold9pt7b` is a new font include, used only by `library.cpp`. The
four body fonts and `FreeSerif9pt7b` that `app.cpp` includes today move to
`reading.cpp` along with the `PageFonts` struct that consumes them; `app.cpp`
ends up including no fonts at all.

### List view

Six books per page, rows 118px tall, first row top y = 50.

- Thumbnail 72 × 108 at x = 24, downscaled from the full cover at draw time.
- Text column starts at x = 112: title, then author.
- Progress bar 280 × 8, with the percentage right-aligned at x = 456.
- A hairline separator between rows.

Progress is `offset * 100 / textLength` — the same calculation
`PageLayout::drawFooter` already uses for the reading footer.

### Tap regions

| Region | Action |
|---|---|
| Header, x ≥ 384 | Toggle grid/list view |
| Body, a cover cell or list row | Open that book |
| Footer, x < 160 | Previous library page |
| Footer, x ≥ 320 | Next library page |

The footer centre shows `1/2`. Taps that hit nothing do nothing — no refresh.

## Reading screen

Page turns keep today's behaviour but gain a centre band:

| Region (controls hidden) | Action |
|---|---|
| x < 160 | Previous page |
| 160 ≤ x < 320 | Show the control bar |
| x ≥ 320 | Next page |

The control bar draws over the top 64px of the current page: a back arrow at
the left and the book title centred. Tapping the arrow (x < 80) returns to the
library; tapping anywhere else dismisses the bar. The settings screen will
later add a control to the right of this bar — nothing is drawn there now.

Opening a book restores its saved position from `storeLoadProgress`.

## The cover helper

```c
// cover.h
static const int16_t COVER_W = 204;
static const int16_t COVER_H = 306;

void drawCover(Adafruit_GFX &gfx, const Book &book,
               int16_t x, int16_t y, int16_t w, int16_t h);
```

Behaviour:

- At native size, a straight `drawBitmap(x, y, cover, COVER_W, COVER_H, INK,
  PAPER)` — the two-colour variant, so white pixels are painted rather than
  left transparent.
- At any other size, nearest-neighbour downscale: each destination pixel reads
  the source bit at `sx = px * COVER_W / w`, `sy = py * COVER_H / h`. This is
  what lets the list view derive its 72 × 108 thumbnail from the stored cover
  instead of costing a second ~1 KB array per book.
- A 1px frame around the cover, so a pale cover does not float on white paper.
- If `book.cover` is `nullptr`, draw a typographic placeholder instead: the
  frame with the title and author centred inside, scaled to the box.

Bit convention: **1 = ink (black)**, MSB first, rows padded to whole bytes —
`(204 + 7) / 8 = 26` bytes per row, 7,956 bytes per cover. This matches what
`Adafruit_GFX::drawBitmap` expects.

### tools/make_cover.py

`make_cover.py <image> <slug>` writes `reader/covers/<slug>.h` containing
`static const uint8_t COVER_<SLUG>[] = { ... };`. It resizes to 204 × 306,
converts with Pillow's Floyd–Steinberg dithering (`img.convert("1")`), and
packs bits MSB-first with `bit = (pixel == 0)`.

Dithering, not thresholding. On a 1-bit e-ink panel that choice is the
difference between a cover that reads as artwork and one that reads as a blob.

## Refresh strategy

| Change | Refresh |
|---|---|
| Library ↔ reading, view toggle, library paging | `epdShowFull` |
| Page turn | unchanged (`epdShowPartial`, honouring `FULL_REFRESH_EVERY`) |
| Control bar shown/hidden | `epdShowPartial` |

Whole-image changes use the flashing refresh because a partial update across
an entirely new image ghosts badly. The control bar is a small delta, so it
gets the fast (~0.8 s) path.

## Simulator support

- `store.cpp` added to `HARDWARE_ONLY` in `simulator/Makefile`, with the
  stand-in implementation in `main.cpp`.
- New keys: `l` returns to the library, `v` toggles the library view.
- `--screenshot <file> <sequence>` accepts `l` and `v` alongside `n` and `p`,
  so both views can be captured without clicking.
- `reader-sim.state` added to `simulator/.gitignore`.

## Verification

The repo has no test harness today. The layout and hit-testing logic is pure
arithmetic with no hardware dependency, which is worth making testable rather
than eyeballing:

- Add a `make test` target building `tests.cpp` (no SDL) that covers library
  pagination (book count → page count → which books on page N), grid and list
  hit-testing including the gutters and dead zones, reading-screen tap
  thirds, and `drawCover`'s downscale index arithmetic at both native and
  thumbnail size.
- Simulator screenshots for the visual result: grid view, list view, a book
  opened at a restored position, and the control bar shown.
- On-device check that progress survives a power cycle, since NVS is the one
  piece the simulator only imitates.

## Risks

- **Flash wear** if the write policy regresses to per-page-turn saves. The 20
  page-turn safety net is the only periodic writer; keep it there.
- **RAM.** One shared canvas is the rule. The list view's downscale must read
  source bits directly, never materialise a scaled buffer.
- **Cover bit convention** is easy to get backwards; a cover rendering as a
  photographic negative means `make_cover.py` and `drawCover` disagree about
  whether 1 is ink.
