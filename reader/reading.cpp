#include "reading.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerifItalic12pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifBoldItalic12pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "busy.h"
#include "catalog.h"
#include "cover.h"
#include "epd.h"
#include "layout.h"
#include "screens.h"
#include "storage.h"
#include "store.h"
#include "textsource.h"

// Page turns use the no-flash refresh, which leaves faint ghosting over time.
// Set this to N to do a full (flashing) refresh every N turns; 0 = never.
static const uint8_t FULL_REFRESH_EVERY = 0;

static const int16_t SCREEN_W = 480;
static const int16_t BAR_H = 64;    // control bar height, drawn over the page top
static const int16_t BACK_W = 80;   // width of the back arrow's tap target
static const uint32_t PAGES_MAGIC = 0x50475331;  // "PGS1"

static const PageFonts fonts = {
  &FreeSerif12pt7b,
  &FreeSerifItalic12pt7b,
  &FreeSerifBold12pt7b,
  &FreeSerifBoldItalic12pt7b,
  &FreeSerif9pt7b,
};

static FileText text;
static PageLayout *layout = nullptr;  // made on first use: it needs the canvas
static uint16_t currentBook = UINT16_MAX;
static std::vector<uint32_t> pages;  // where each page starts (pagePack)
static std::vector<Chapter> chapters;
static uint32_t currentPage = 0;
static uint8_t turnsSinceFullRefresh = 0;
static bool controlsVisible = false;

// pages.bin: this header, then one pagePack'd PagePos per page.
struct PagesHeader {
  uint32_t magic;
  uint16_t layoutVersion;
  uint16_t width, height;  // of the page it was counted for
  uint32_t textLength;     // of the text it was counted from
  uint32_t count;
};

static bool loadPages(const BookInfo &book) {
  char path[48];
  catalogPath(book, "pages.bin", path, sizeof(path));
  StorageFile *f = storageOpen(path, false);
  if (!f) return false;
  PagesHeader h;
  bool ok = storageRead(f, &h, sizeof(h)) == (int32_t)sizeof(h) && h.magic == PAGES_MAGIC &&
            h.layoutVersion == LAYOUT_VERSION && h.width == appCanvas().width() &&
            h.height == appCanvas().height() && h.textLength == text.length() && h.count > 0;
  if (ok) {
    pages.assign(h.count, 0);
    ok = storageRead(f, pages.data(), h.count * 4) == (int32_t)(h.count * 4);
  }
  storageClose(f);
  if (!ok) pages.clear();
  return ok;
}

// Counts the book's pages by laying out every page without drawing, and
// saves where each one starts so this happens once per book.
static void paginate(const BookInfo &book) {
  unsigned long t0 = millis();
  pages.clear();
  PagePos pos = { 0, false, false };
  uint8_t shown = 0;
  while (true) {
    pages.push_back(pagePack(pos));
    PagePos next = layout->layoutPage(pos, false);
    if (layout->isEnd(next) || next.offset <= pos.offset) break;  // never loop forever
    pos = next;
    // A few progress updates, not one per page: each costs a refresh.
    uint8_t pct = (uint8_t)((uint64_t)pos.offset * 100 / text.length());
    if (pct >= shown + 34) {
      shown = pct;
      busyShow(book.title, "Counting pages...", pct);
    }
  }
  Serial.printf("reading: %u pages counted in %lu ms\n", (unsigned)pages.size(), millis() - t0);

  char path[48], tmp[52];
  catalogPath(book, "pages.bin", path, sizeof(path));
  snprintf(tmp, sizeof(tmp), "%s.tmp", path);
  PagesHeader h = { PAGES_MAGIC, LAYOUT_VERSION, (uint16_t)appCanvas().width(),
                    (uint16_t)appCanvas().height(), text.length(), (uint32_t)pages.size() };
  StorageFile *f = storageOpen(tmp, true);
  bool ok = f && storageWrite(f, &h, sizeof(h)) &&
            storageWrite(f, pages.data(), (uint32_t)(pages.size() * 4));
  storageClose(f);
  if (ok) {
    storageRemove(path);
    ok = storageRename(tmp, path);
  }
  if (!ok) Serial.printf("reading: could not save the page count for %s\n", book.title);
}

// Converting a book reports 0-100; show a few of those steps.
struct ConvertShown {
  const char *title;
  uint8_t shown;
};
static void onConvertProgress(uint8_t pct, void *ctx) {
  ConvertShown *c = (ConvertShown *)ctx;
  if (pct >= c->shown + 34 && pct < 100) {
    c->shown = pct;
    busyShow(c->title, "Preparing book...", pct);
  }
}

static PagePos pageStart(uint32_t page) {
  return pageUnpack(pages[page]);
}

// The last page starting at or before `offset`.
static uint32_t pageContaining(uint32_t offset) {
  auto it = std::upper_bound(pages.begin(), pages.end(), offset,
                             [](uint32_t off, uint32_t packed) { return off < pageUnpack(packed).offset; });
  return it == pages.begin() ? 0 : (uint32_t)(it - pages.begin() - 1);
}

bool readingOpenBook(uint16_t index) {
  if (index >= catalogCount()) return false;
  if (!layout) layout = new PageLayout(appCanvas(), text, fonts);
  const BookInfo &book = catalogBook(index);
  controlsVisible = false;
  turnsSinceFullRefresh = 0;
  currentBook = UINT16_MAX;
  pages.clear();

  if (book.textLength == 0) {
    busyShow(book.title, "Preparing book...", 0);
    ConvertShown shown = { book.title, 0 };
    if (!catalogPrepareText(index, onConvertProgress, &shown)) {
      Serial.printf("reading: %s could not be read\n", book.title);
      return false;
    }
  }

  char path[48];
  catalogPath(book, "text.txt", path, sizeof(path));
  if (!text.open(path) || text.length() == 0) return false;
  catalogLoadChapters(book, chapters);
  if (!loadPages(book)) {
    busyShow(book.title, "Counting pages...", 0);
    paginate(book);
  }
  currentBook = index;

  uint32_t offset = 0;
  storeLoadProgress(book.id, offset);
  currentPage = pageContaining(offset);
  return true;
}

uint32_t readingCurrentPage() {
  return currentPage;
}

uint32_t readingPageCount() {
  return (uint32_t)pages.size();
}

static void saveProgress() {
  if (currentBook == UINT16_MAX || pages.empty()) return;
  storeSaveProgress(catalogBook(currentBook).id, pageStart(currentPage).offset);
}

ReadingAction readingHitTest(int16_t x, int16_t y, bool barUp) {
  if (barUp) {
    if (y < BAR_H && x < BACK_W) return READ_BACK_TO_LIBRARY;
    return READ_HIDE_CONTROLS;
  }
  if (x < SCREEN_W / 3) return READ_PREV;
  if (x < 2 * SCREEN_W / 3) return READ_SHOW_CONTROLS;
  return READ_NEXT;
}

uint32_t readingProgressPercent(uint16_t index) {
  if (index >= catalogCount()) return 0;
  const BookInfo &book = catalogBook(index);
  if (book.textLength == 0) return 0;
  uint32_t offset = 0;
  if (index == currentBook && !pages.empty()) {
    offset = pageStart(currentPage).offset;
  } else if (!storeLoadProgress(book.id, offset)) {
    return 0;
  }
  return (uint32_t)((uint64_t)offset * 100 / book.textLength);
}

// The chapter the current page belongs to: the last one that starts before
// the next page does, so a page where a new chapter begins counts as that
// chapter.
const char *readingChapterTitle() {
  if (pages.empty() || chapters.empty()) return "";
  uint32_t end = currentPage + 1 < pages.size() ? pageStart(currentPage + 1).offset : UINT32_MAX;
  const char *title = "";
  for (const Chapter &c : chapters) {
    if (c.offset >= end) break;
    title = c.title;
  }
  return title;
}

// Draws the control bar over the top BAR_H pixels of the current page: a
// back arrow to the library, and the chapter's title (or, before the first
// chapter, the book's) centred in the rest of the bar (drawCentredText also
// truncates it, so a long title cannot run under the arrow or off the edge).
static void drawControlBar(Adafruit_GFX &gfx) {
  const uint16_t INK = 0x0000, PAPER = 0xFFFF;
  gfx.fillRect(0, 0, SCREEN_W, BAR_H, PAPER);
  gfx.drawFastHLine(0, BAR_H - 1, SCREEN_W, INK);

  // Back arrow: a triangle with a shaft, pointing left.
  gfx.fillTriangle(24, 32, 38, 22, 38, 42, INK);
  gfx.drawFastHLine(38, 32, 22, INK);

  const char *title = readingChapterTitle();
  if (!title[0]) title = catalogBook(currentBook).title;
  gfx.setTextColor(INK);
  drawCentredText(gfx, title, &FreeSerifBold9pt7b, BACK_W, SCREEN_W - BACK_W, 38);
}

// "Page 12 of 340", centred below the text.
static void drawFooter(Adafruit_GFX &gfx) {
  char label[32];
  snprintf(label, sizeof(label), "Page %u of %u", (unsigned)currentPage + 1, (unsigned)pages.size());
  gfx.setTextColor(0x0000);
  drawCentredText(gfx, label, fonts.footer, 0, gfx.width(), layout->footerBaseline());
}

void readingShow(bool fullRefresh) {
  if (currentBook == UINT16_MAX || pages.empty()) return;
  unsigned long t0 = millis();
  layout->layoutPage(pageStart(currentPage), true);
  drawFooter(appCanvas());
  if (controlsVisible) drawControlBar(appCanvas());
  appRefresh(fullRefresh);
  Serial.printf("page %u of %u shown in %lu ms (%s)\n", (unsigned)currentPage + 1,
                (unsigned)pages.size(), millis() - t0, fullRefresh ? "full" : "partial");
}

void readingTurnPage(int delta) {
  if (delta > 0 && currentPage + 1 >= pages.size()) return;  // last page
  if (delta < 0 && currentPage == 0) return;
  controlsVisible = false;  // after the guards: a rejected turn changes nothing
  currentPage += delta;
  bool full = FULL_REFRESH_EVERY > 0 && ++turnsSinceFullRefresh >= FULL_REFRESH_EVERY;
  if (full) turnsSinceFullRefresh = 0;
  readingShow(full);
  saveProgress();
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
      saveProgress();
      appGoTo(SCREEN_LIBRARY);
      break;
    default: break;
  }
}
