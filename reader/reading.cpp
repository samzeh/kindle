#include "reading.h"
#include <Adafruit_GFX.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerifItalic12pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifBoldItalic12pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <algorithm>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "busy.h"
#include "fonts/FreeSerif8pt7b.h"
#include "icons.h"
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

// The controls, shown only after a tap in the middle of the page: a bar
// across the top (back, chapter name, settings) in the 9pt font, a little
// smaller than the book's 12pt text, and the page number in a strip along
// the bottom in 8pt. They are drawn over the page, and each grows to hide
// whole lines of text rather than cut through one.
static const int16_t SCREEN_W = 480;
static const int16_t SCREEN_H = 800;
static const int16_t BAR_H = 44;        // top bar height
static const int16_t BAR_MID = BAR_H / 2;
static const int16_t ICON_TAP_W = 64;   // tap targets of the back and settings icons
static const int16_t PAGE_STRIP_H = 30; // bottom strip with the page number

// The progress line, in the bottom margin while reading: a thin track across
// the text's width, with a thicker part showing how far through the book.
static const int16_t PROGRESS_X = 24;   // matches the text margins
static const int16_t PROGRESS_W = SCREEN_W - 2 * PROGRESS_X;
static const int16_t PROGRESS_Y = SCREEN_H - 12;  // the track's row
static const uint32_t PAGES_MAGIC = 0x50475331;  // "PGS1"

static const PageFonts fonts = {
  &FreeSerif12pt7b,
  &FreeSerifItalic12pt7b,
  &FreeSerifBold12pt7b,
  &FreeSerifBoldItalic12pt7b,
};

static FileText text;
static PageLayout *layout = nullptr;  // made on first use: it needs the canvas
static uint16_t currentBook = UINT16_MAX;
// A book's pages: its cover (if it has one) as page 1, then the text pages.
static bool coverPage = false;
static std::vector<uint32_t> pages;  // where each text page starts (pagePack)
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

static uint32_t pageTotal() {
  return (uint32_t)pages.size() + coverPage;
}

static bool onCover() {
  return coverPage && currentPage == 0;
}

// Where a text page starts (page counted from the book's first page; not
// the cover).
static PagePos pageStart(uint32_t page) {
  return pageUnpack(pages[page - coverPage]);
}

// Where a page starts in the text; the cover counts as the very start.
static uint32_t pageOffset(uint32_t page) {
  return coverPage && page == 0 ? 0 : pageStart(page).offset;
}

// The last text page starting at or before `offset`.
static uint32_t pageContaining(uint32_t offset) {
  auto it = std::upper_bound(pages.begin(), pages.end(), offset,
                             [](uint32_t off, uint32_t packed) { return off < pageUnpack(packed).offset; });
  uint32_t textPage = it == pages.begin() ? 0 : (uint32_t)(it - pages.begin() - 1);
  return textPage + coverPage;
}

bool readingOpenBook(uint16_t index) {
  if (index >= catalogCount()) return false;
  if (!layout) layout = new PageLayout(appCanvas(), text, fonts);
  const BookInfo &book = catalogBook(index);
  controlsVisible = false;
  turnsSinceFullRefresh = 0;
  currentBook = UINT16_MAX;
  coverPage = false;
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
  // The first page is the cover, as on a Kindle. Made once per book; it
  // overwrites the canvas, which the page about to be shown redraws.
  coverPage = book.hasCover && catalogPrepareFullCover(index, appCanvas());
  currentBook = index;

  // A book never opened starts at its first page: the cover.
  uint32_t offset = 0;
  currentPage = storeLoadProgress(book.id, offset) ? pageContaining(offset) : 0;
  return true;
}

uint32_t readingCurrentPage() {
  return currentPage;
}

uint32_t readingPageCount() {
  return pageTotal();
}

uint32_t readingPageOffset(uint32_t page) {
  return page < pageTotal() ? pageOffset(page) : 0;
}

static void saveProgress() {
  if (currentBook == UINT16_MAX || pages.empty()) return;
  storeSaveProgress(catalogBook(currentBook).id, pageOffset(currentPage));
}

ReadingAction readingHitTest(int16_t x, int16_t y, bool barUp) {
  if (barUp) {
    if (y < BAR_H && x < ICON_TAP_W) return READ_BACK_TO_LIBRARY;
    if (y < BAR_H && x >= SCREEN_W - ICON_TAP_W) return READ_OPEN_SETTINGS;
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
    offset = pageOffset(currentPage);
  } else if (!storeLoadProgress(book.id, offset)) {
    return 0;
  }
  return (uint32_t)((uint64_t)offset * 100 / book.textLength);
}

// The chapter the current page belongs to: the last one that starts before
// the next page does, so a page where a new chapter begins counts as that
// chapter.
const char *readingChapterTitle() {
  if (pages.empty() || chapters.empty() || onCover()) return "";
  uint32_t end = currentPage + 1 < pageTotal() ? pageOffset(currentPage + 1) : UINT32_MAX;
  const char *title = "";
  for (const Chapter &c : chapters) {
    if (c.offset >= end) break;
    title = c.title;
  }
  return title;
}

static const uint16_t INK = 0x0000, PAPER = 0xFFFF;

// The top bar: back chevron, the chapter's title (or, before the first
// chapter, the book's) in the middle, and the settings gear. drawCentredText
// truncates the title, so a long one cannot run under the icons.
static void drawTopBar(Adafruit_GFX &gfx) {
  gfx.fillRect(0, 0, SCREEN_W, onCover() ? BAR_H : layout->coverDownTo(BAR_H), PAPER);
  iconChevronLeft(gfx, 30, BAR_MID);
  iconGear(gfx, SCREEN_W - 30, BAR_MID);

  const char *title = readingChapterTitle();
  if (!title[0]) title = catalogBook(currentBook).title;
  gfx.setTextColor(INK);
  // Baseline placed so a capital letter's middle is on BAR_MID, level with
  // the middles of the chevron and gear.
  const GFXglyph &cap = FreeSerif9pt7b.glyph['H' - FreeSerif9pt7b.first];
  int16_t baseline = BAR_MID - cap.yOffset - (cap.height - 1) / 2;
  drawCentredText(gfx, title, &FreeSerif9pt7b, ICON_TAP_W, SCREEN_W - 2 * ICON_TAP_W, baseline);
}

static void drawProgressLine(Adafruit_GFX &gfx) {
  gfx.drawFastHLine(PROGRESS_X, PROGRESS_Y, PROGRESS_W, INK);
  // Full on the last page.
  int16_t done = (int16_t)((uint32_t)PROGRESS_W * (currentPage + 1) / pageTotal());
  gfx.fillRect(PROGRESS_X, PROGRESS_Y - 1, done, 3, INK);
}

// The bottom strip: "Page 12 of 340".
static void drawPageStrip(Adafruit_GFX &gfx) {
  int16_t top = onCover() ? SCREEN_H - PAGE_STRIP_H : layout->coverUpTo(SCREEN_H - PAGE_STRIP_H);
  gfx.fillRect(0, top, SCREEN_W, SCREEN_H - top, PAPER);
  char label[32];
  snprintf(label, sizeof(label), "Page %u of %u", (unsigned)currentPage + 1, (unsigned)pageTotal());
  gfx.setTextColor(INK);
  drawCentredText(gfx, label, &FreeSerif8pt7b, 0, SCREEN_W, SCREEN_H - 18);
}

void readingShow(bool fullRefresh) {
  if (currentBook == UINT16_MAX || pages.empty()) return;
  unsigned long t0 = millis();
  if (onCover()) {
    if (!catalogLoadFullCover(catalogBook(currentBook), appCanvas())) appCanvas().fillScreen(PAPER);
  } else {
    layout->layoutPage(pageStart(currentPage), true);
    drawProgressLine(appCanvas());  // the page strip covers it when the controls are up
  }
  if (controlsVisible) {
    drawTopBar(appCanvas());
    drawPageStrip(appCanvas());
  }
  appRefresh(fullRefresh);
  Serial.printf("page %u of %u shown in %lu ms (%s)\n", (unsigned)currentPage + 1,
                (unsigned)pageTotal(), millis() - t0, fullRefresh ? "full" : "partial");
}

void readingTurnPage(int delta) {
  if (delta > 0 && currentPage + 1 >= pageTotal()) return;  // last page
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
    case READ_OPEN_SETTINGS:
      Serial.println("reading: settings are not built yet");  // the bar stays up
      break;
    default: break;
  }
}
