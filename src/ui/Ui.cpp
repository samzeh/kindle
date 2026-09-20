#include "ui/Ui.h"

#include <SD.h>

#include "app/Settings.h"
#include "config.h"
#include "hal/Display.h"
#include "hal/Storage.h"
#include "net/UploadServer.h"
#include "power/Power.h"
#include "text/EpdRenderer.h"
#include "ui/Draw.h"

namespace {

EpdRenderer gRenderer;

// The progress screen is redrawn at most this often: each e-paper refresh
// costs ~0.4s, so updating per chapter would dominate import time.
constexpr uint32_t PROGRESS_INTERVAL_MS = 2500;

constexpr uint16_t UI_TEXT_PX = 22;
constexpr uint16_t UI_TITLE_PX = 26;

bool hasBookExtension(const char* name) {
  const char* dot = strrchr(name, '.');
  if (!dot) return false;
  return strcasecmp(dot, ".epub") == 0 || strcasecmp(dot, ".txt") == 0;
}

enum SettingsRow : uint8_t {
  ROW_FONT_SIZE = 0,
  ROW_MARGIN,
  ROW_SPACING,
  ROW_WIFI,
  ROW_CLEAR_CACHE,
  ROW_SLEEP,
  SETTINGS_ROW_COUNT,
};

enum MenuRow : uint8_t {
  MENU_RESUME = 0,
  MENU_TOC,
  MENU_LIBRARY,
  MENU_SETTINGS,
  MENU_ROW_COUNT,
};

}  // namespace

Ui gUi;

void Ui::begin() {
  scanLibrary();
  lastUploadChanges_ = gUploadServer.changeCount();

  // Resume the last book if there was one, otherwise show the shelf.
  const Settings& s = gSettings.get();
  if (s.lastBook[0]) {
    for (uint16_t i = 0; i < bookCount_; i++) {
      if (strcmp(books_[i], s.lastBook) == 0) {
        if (openBook(i)) {
          go(Screen::Reader, Display::Refresh::Full);
          return;
        }
        break;
      }
    }
  }
  go(Screen::Library, Display::Refresh::Full);
}

void Ui::go(Screen screen, Display::Refresh refresh) {
  screen_ = screen;
  gDisplay.clearBuffer();
  draw();
  gDisplay.present(refresh);
}

void Ui::draw() {
  switch (screen_) {
    case Screen::Library: drawLibrary(); break;
    case Screen::Reader: drawReader(); break;
    case Screen::Menu: drawMenu(); break;
    case Screen::Toc: drawToc(); break;
    case Screen::Settings: drawSettings(); break;
    case Screen::Wifi: drawWifi(); break;
  }
}

void Ui::tick() {
  if (gUploadServer.running()) {
    gUploadServer.handle();

    // A finished upload changes the shelf; refresh it if that is what is on
    // screen.
    if (gUploadServer.changeCount() != lastUploadChanges_) {
      lastUploadChanges_ = gUploadServer.changeCount();
      scanLibrary();
      if (screen_ == Screen::Wifi || screen_ == Screen::Library) {
        gDisplay.clearBuffer();
        draw();
        gDisplay.present(Display::Refresh::Partial);
      }
    }
  }

  TouchEvent ev = gTouch.poll();
  if (ev.gesture != Gesture::None) {
    gPower.noteActivity();
    handle(ev);
    return;
  }

  // Never sleep while the upload server is serving.
  if (!gUploadServer.running() && gPower.shouldSleep()) {
    saveProgress();
    gSettings.save();
    gPower.deepSleep();
  }
}

void Ui::handle(const TouchEvent& ev) {
  switch (screen_) {
    case Screen::Library: handleLibrary(ev); break;
    case Screen::Reader: handleReader(ev); break;
    case Screen::Menu: handleMenu(ev); break;
    case Screen::Toc: handleToc(ev); break;
    case Screen::Settings: handleSettings(ev); break;
    case Screen::Wifi: handleWifi(ev); break;
  }
}

// ---------------------------------------------------------------------------
// Shared list chrome
// ---------------------------------------------------------------------------

uint16_t Ui::visibleRows() const {
  return static_cast<uint16_t>((SCREEN_H - HEADER_H - FOOTER_H) / ROW_H);
}

int16_t Ui::rowAt(int16_t y) const {
  if (y < HEADER_H || y >= SCREEN_H - FOOTER_H) return -1;
  return (y - HEADER_H) / ROW_H;
}

void Ui::drawListChrome(const char* title, uint16_t page, uint16_t pageCount) {
  ui::drawTextClipped(18, 40, SCREEN_W - 36, title, FontStyle::Bold, UI_TITLE_PX);
  ui::drawHLine(0, HEADER_H - 1, SCREEN_W);

  int16_t footerY = SCREEN_H - FOOTER_H;
  ui::drawHLine(0, footerY, SCREEN_W);

  ui::drawText(24, footerY + 36, "<", FontStyle::Bold, UI_TITLE_PX);
  ui::drawText(SCREEN_W - 36, footerY + 36, ">", FontStyle::Bold, UI_TITLE_PX);

  char label[24];
  snprintf(label, sizeof(label), "%u / %u", static_cast<unsigned>(page + 1),
           static_cast<unsigned>(pageCount ? pageCount : 1));
  ui::drawTextCentered(SCREEN_W / 2, footerY + 36, label, FontStyle::Regular,
                       UI_TEXT_PX);
}

// Handles the footer arrows and swipes. Returns true when it consumed the
// event and the caller should redraw.
bool Ui::handleListNav(const TouchEvent& ev, uint16_t itemCount, uint16_t& scroll) {
  uint16_t perPage = visibleRows();
  if (perPage == 0) return false;

  bool back = ev.gesture == Gesture::SwipeRight ||
              (ev.gesture == Gesture::Tap && ev.y >= SCREEN_H - FOOTER_H &&
               ev.x < SCREEN_W / 3);
  bool forward = ev.gesture == Gesture::SwipeLeft ||
                 (ev.gesture == Gesture::Tap && ev.y >= SCREEN_H - FOOTER_H &&
                  ev.x > SCREEN_W * 2 / 3);

  if (back && scroll >= perPage) {
    scroll -= perPage;
    return true;
  }
  if (forward && scroll + perPage < itemCount) {
    scroll += perPage;
    return true;
  }
  return back || forward;  // consumed even at the ends, to avoid stray taps
}

// ---------------------------------------------------------------------------
// Library
// ---------------------------------------------------------------------------

void Ui::scanLibrary() {
  bookCount_ = 0;
  if (!gStorage.ready()) return;

  File dir = SD.open(DIR_BOOKS);
  if (!dir) return;

  while (bookCount_ < MAX_BOOKS) {
    File entry = dir.openNextFile();
    if (!entry) break;

    if (!entry.isDirectory()) {
      const char* path = entry.name();
      const char* base = strrchr(path, '/');
      base = base ? base + 1 : path;

      if (hasBookExtension(base) && base[0] != '.') {
        strncpy(books_[bookCount_], base, sizeof(books_[0]) - 1);
        books_[bookCount_][sizeof(books_[0]) - 1] = '\0';
        bookCount_++;
      }
    }
    entry.close();
  }
  dir.close();

  if (libraryScroll_ >= bookCount_) libraryScroll_ = 0;
}

void Ui::drawLibrary() {
  uint16_t perPage = visibleRows();
  uint16_t pageCount = bookCount_ ? (bookCount_ + perPage - 1) / perPage : 1;
  drawListChrome("Library", perPage ? libraryScroll_ / perPage : 0, pageCount);

  if (bookCount_ == 0) {
    ui::drawTextCentered(SCREEN_W / 2, SCREEN_H / 2 - 12, "No books yet",
                         FontStyle::Bold, UI_TITLE_PX);
    ui::drawTextCentered(SCREEN_W / 2, SCREEN_H / 2 + 20,
                         "Menu > Settings > WiFi upload", FontStyle::Regular, 20);
    return;
  }

  for (uint16_t i = 0; i < perPage; i++) {
    uint16_t idx = libraryScroll_ + i;
    if (idx >= bookCount_) break;
    ui::drawListRow(HEADER_H + i * ROW_H, ROW_H, books_[idx], nullptr, false,
                    UI_TEXT_PX);
  }
}

void Ui::progressTrampoline(void* user, const char* stage, uint16_t done,
                            uint16_t total) {
  static_cast<Ui*>(user)->onProgress(stage, done, total);
}

void Ui::onProgress(const char* stage, uint16_t done, uint16_t total) {
  uint32_t now = millis();
  bool last = (total > 0 && done >= total);
  if (!last && now - lastProgressDraw_ < PROGRESS_INTERVAL_MS) return;
  lastProgressDraw_ = now;

  ui::drawProgress(progressTitle_, stage, done, total);
  gDisplay.present(Display::Refresh::Partial);
}

bool Ui::openBook(uint16_t index) {
  if (index >= bookCount_) return false;

  strncpy(progressTitle_, books_[index], sizeof(progressTitle_) - 1);
  progressTitle_[sizeof(progressTitle_) - 1] = '\0';
  lastProgressDraw_ = 0;

  ui::drawProgress(progressTitle_, "Opening", 0, 1);
  gDisplay.present(Display::Refresh::Partial);

  PageMetrics m = metricsFor(gSettings.get());
  if (!book_.open(books_[index], m, progressTrampoline, this)) {
    ui::drawMessage("Could not open", book_.error());
    gDisplay.present(Display::Refresh::Full);
    delay(2500);
    return false;
  }

  uint32_t saved = gSettings.progressFor(book_.bookId());
  if (saved < book_.pageCount()) book_.seek(saved);

  Settings& s = gSettings.get();
  strncpy(s.lastBook, books_[index], sizeof(s.lastBook) - 1);
  s.lastBook[sizeof(s.lastBook) - 1] = '\0';
  gSettings.save();
  return true;
}

void Ui::handleLibrary(const TouchEvent& ev) {
  if (handleListNav(ev, bookCount_, libraryScroll_)) {
    go(Screen::Library, Display::Refresh::Partial);
    return;
  }

  if (ev.gesture == Gesture::Tap && ev.y < HEADER_H) {
    if (book_.isOpen()) go(Screen::Reader, Display::Refresh::Full);
    else go(Screen::Settings, Display::Refresh::Partial);
    return;
  }

  int16_t row = rowAt(ev.y);
  if (row < 0 || ev.gesture != Gesture::Tap) return;

  uint16_t idx = libraryScroll_ + static_cast<uint16_t>(row);
  if (idx >= bookCount_) return;

  if (openBook(idx)) go(Screen::Reader, Display::Refresh::Full);
  else go(Screen::Library, Display::Refresh::Full);
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

void Ui::drawReader() {
  if (!book_.isOpen()) {
    ui::drawMessage("No book open", "Tap to choose one");
    return;
  }

  book_.render(&gRenderer);

  // Status strip: title on the left, position on the right.
  int16_t y = SCREEN_H - STATUS_BAR_H;
  ui::drawHLine(20, y, SCREEN_W - 40);

  const char* title = book_.meta().title[0] ? book_.meta().title : book_.filename();

  char position[32];
  uint32_t total = book_.pageCount();
  uint32_t current = book_.currentPage() + 1;
  uint32_t percent = total ? (current * 100) / total : 0;
  snprintf(position, sizeof(position), "%lu/%lu  %lu%%",
           static_cast<unsigned long>(current), static_cast<unsigned long>(total),
           static_cast<unsigned long>(percent));

  int16_t posW = ui::textWidth(position, FontStyle::Regular, 16);
  ui::drawText(SCREEN_W - 20 - posW, SCREEN_H - 7, position, FontStyle::Regular, 16);
  ui::drawTextClipped(20, SCREEN_H - 7, SCREEN_W - 56 - posW, title,
                      FontStyle::Regular, 16);
}

void Ui::handleReader(const TouchEvent& ev) {
  if (!book_.isOpen()) {
    go(Screen::Library, Display::Refresh::Full);
    return;
  }

  // Top strip opens the menu; otherwise a narrow left column goes back and
  // everything else advances, which is the familiar e-reader arrangement.
  if (ev.gesture == Gesture::LongPress) {
    go(Screen::Menu, Display::Refresh::Partial);
    return;
  }

  bool forward = false, backward = false;

  if (ev.gesture == Gesture::Tap) {
    if (ev.y < 64) {
      go(Screen::Menu, Display::Refresh::Partial);
      return;
    }
    if (ev.x < 140) backward = true;
    else forward = true;
  } else if (ev.gesture == Gesture::SwipeLeft) {
    forward = true;
  } else if (ev.gesture == Gesture::SwipeRight) {
    backward = true;
  } else {
    return;
  }

  bool moved = forward ? book_.nextPage() : (backward ? book_.prevPage() : false);
  if (!moved) return;

  saveProgress();
  gDisplay.clearBuffer();
  drawReader();
  gDisplay.present(Display::Refresh::Auto);
}

void Ui::saveProgress() {
  if (book_.isOpen()) gSettings.saveProgress(book_.bookId(), book_.currentPage());
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------

void Ui::drawMenu() {
  drawListChrome("Menu", 0, 1);

  const char* labels[MENU_ROW_COUNT] = {"Resume reading", "Contents", "Library",
                                        "Settings"};
  for (uint8_t i = 0; i < MENU_ROW_COUNT; i++) {
    ui::drawListRow(HEADER_H + i * ROW_H, ROW_H, labels[i], nullptr, false,
                    UI_TEXT_PX);
  }
}

void Ui::handleMenu(const TouchEvent& ev) {
  if (ev.gesture != Gesture::Tap) return;

  int16_t row = rowAt(ev.y);
  if (row < 0) {
    go(Screen::Reader, Display::Refresh::Full);
    return;
  }

  switch (row) {
    case MENU_RESUME:
      go(Screen::Reader, Display::Refresh::Full);
      break;
    case MENU_TOC:
      tocScroll_ = 0;
      go(Screen::Toc, Display::Refresh::Partial);
      break;
    case MENU_LIBRARY:
      scanLibrary();
      go(Screen::Library, Display::Refresh::Partial);
      break;
    case MENU_SETTINGS:
      settingsScroll_ = 0;
      go(Screen::Settings, Display::Refresh::Partial);
      break;
    default:
      go(Screen::Reader, Display::Refresh::Full);
      break;
  }
}

// ---------------------------------------------------------------------------
// Table of contents
// ---------------------------------------------------------------------------

void Ui::drawToc() {
  uint16_t count = book_.tocCount();
  uint16_t perPage = visibleRows();
  uint16_t pageCount = count ? (count + perPage - 1) / perPage : 1;
  drawListChrome("Contents", perPage ? tocScroll_ / perPage : 0, pageCount);

  if (count == 0) {
    ui::drawTextCentered(SCREEN_W / 2, SCREEN_H / 2, "No contents",
                         FontStyle::Regular, UI_TEXT_PX);
    return;
  }

  uint16_t here = book_.currentChapter();
  for (uint16_t i = 0; i < perPage; i++) {
    uint16_t idx = tocScroll_ + i;
    if (idx >= count) break;
    const TocItem& item = book_.tocItem(idx);
    ui::drawListRow(HEADER_H + i * ROW_H, ROW_H, item.title, nullptr,
                    item.chapter == here, UI_TEXT_PX);
  }
}

void Ui::handleToc(const TouchEvent& ev) {
  if (handleListNav(ev, book_.tocCount(), tocScroll_)) {
    go(Screen::Toc, Display::Refresh::Partial);
    return;
  }
  if (ev.gesture == Gesture::Tap && ev.y < HEADER_H) {
    go(Screen::Menu, Display::Refresh::Partial);
    return;
  }

  int16_t row = rowAt(ev.y);
  if (row < 0 || ev.gesture != Gesture::Tap) return;

  uint16_t idx = tocScroll_ + static_cast<uint16_t>(row);
  if (idx >= book_.tocCount()) return;

  book_.seekChapter(book_.tocItem(idx).chapter);
  saveProgress();
  go(Screen::Reader, Display::Refresh::Full);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void Ui::drawSettings() {
  drawListChrome("Settings", 0, 1);

  Settings& s = gSettings.get();
  char value[24];

  snprintf(value, sizeof(value), "%u px", static_cast<unsigned>(s.fontPx()));
  ui::drawListRow(HEADER_H + ROW_FONT_SIZE * ROW_H, ROW_H, "Text size", value, false,
                  UI_TEXT_PX);

  snprintf(value, sizeof(value), "%u px", static_cast<unsigned>(s.margin()));
  ui::drawListRow(HEADER_H + ROW_MARGIN * ROW_H, ROW_H, "Margins", value, false,
                  UI_TEXT_PX);

  snprintf(value, sizeof(value), "%u%%", static_cast<unsigned>(s.lineSpacingPct));
  ui::drawListRow(HEADER_H + ROW_SPACING * ROW_H, ROW_H, "Line spacing", value, false,
                  UI_TEXT_PX);

  ui::drawListRow(HEADER_H + ROW_WIFI * ROW_H, ROW_H, "WiFi upload",
                  gUploadServer.running() ? "on" : "off", false, UI_TEXT_PX);

  ui::drawListRow(HEADER_H + ROW_CLEAR_CACHE * ROW_H, ROW_H, "Rebuild this book",
                  nullptr, false, UI_TEXT_PX);

  ui::drawListRow(HEADER_H + ROW_SLEEP * ROW_H, ROW_H, "Sleep now", nullptr, false,
                  UI_TEXT_PX);
}

void Ui::applyLayoutChange() {
  gSettings.save();
  if (!book_.isOpen()) return;

  strncpy(progressTitle_, book_.meta().title[0] ? book_.meta().title : book_.filename(),
          sizeof(progressTitle_) - 1);
  progressTitle_[sizeof(progressTitle_) - 1] = '\0';
  lastProgressDraw_ = 0;

  // A different font size or margin means different page boundaries, so the
  // index has to be rebuilt before anything can be drawn.
  book_.relayout(metricsFor(gSettings.get()), progressTrampoline, this);
  saveProgress();
}

void Ui::handleSettings(const TouchEvent& ev) {
  if (ev.gesture != Gesture::Tap) return;

  if (ev.y < HEADER_H) {
    go(book_.isOpen() ? Screen::Menu : Screen::Library, Display::Refresh::Partial);
    return;
  }

  int16_t row = rowAt(ev.y);
  if (row < 0) {
    go(book_.isOpen() ? Screen::Menu : Screen::Library, Display::Refresh::Partial);
    return;
  }

  Settings& s = gSettings.get();

  switch (row) {
    case ROW_FONT_SIZE:
      s.fontSizeIndex = (s.fontSizeIndex + 1) % FONT_SIZE_COUNT;
      applyLayoutChange();
      go(Screen::Settings, Display::Refresh::Partial);
      break;

    case ROW_MARGIN:
      s.marginIndex = (s.marginIndex + 1) % MARGIN_COUNT;
      applyLayoutChange();
      go(Screen::Settings, Display::Refresh::Partial);
      break;

    case ROW_SPACING:
      s.lineSpacingPct = s.lineSpacingPct >= 150
                             ? 105
                             : static_cast<uint8_t>(s.lineSpacingPct + 15);
      applyLayoutChange();
      go(Screen::Settings, Display::Refresh::Partial);
      break;

    case ROW_WIFI:
      if (gUploadServer.running()) {
        gUploadServer.stop();
        go(Screen::Settings, Display::Refresh::Partial);
      } else {
        ui::drawMessage("Connecting", "");
        gDisplay.present(Display::Refresh::Partial);
        gUploadServer.begin(s.wifiSsid, s.wifiPass);
        go(Screen::Wifi, Display::Refresh::Full);
      }
      break;

    case ROW_CLEAR_CACHE: {
      if (!book_.isOpen()) break;
      char cache[80];
      Storage::cacheDir(book_.bookId(), cache, sizeof(cache));

      char filename[72];
      strncpy(filename, book_.filename(), sizeof(filename) - 1);
      filename[sizeof(filename) - 1] = '\0';

      book_.close();
      gStorage.removeTree(cache);

      for (uint16_t i = 0; i < bookCount_; i++) {
        if (strcmp(books_[i], filename) == 0) {
          if (openBook(i)) go(Screen::Reader, Display::Refresh::Full);
          else go(Screen::Library, Display::Refresh::Full);
          return;
        }
      }
      go(Screen::Library, Display::Refresh::Full);
      break;
    }

    case ROW_SLEEP:
      saveProgress();
      gSettings.save();
      gPower.deepSleep();
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------

void Ui::drawWifi() {
  drawListChrome("WiFi upload", 0, 1);

  if (!gUploadServer.running()) {
    ui::drawTextCentered(SCREEN_W / 2, SCREEN_H / 2, "Not connected",
                         FontStyle::Regular, UI_TEXT_PX);
    return;
  }

  int16_t y = HEADER_H + 50;

  bool ap = gUploadServer.mode() == UploadServer::Mode::AccessPoint;
  ui::drawTextCentered(SCREEN_W / 2, y, ap ? "Join this network:" : "Connected to:",
                       FontStyle::Regular, 20);
  y += 44;
  ui::drawTextCentered(SCREEN_W / 2, y, gUploadServer.networkName(), FontStyle::Bold,
                       UI_TITLE_PX);

  if (ap) {
    y += 36;
    ui::drawTextCentered(SCREEN_W / 2, y, "password: readabook", FontStyle::Regular,
                         20);
  }

  y += 70;
  ui::drawTextCentered(SCREEN_W / 2, y, "Then open in a browser:", FontStyle::Regular,
                       20);
  y += 44;
  ui::drawTextCentered(SCREEN_W / 2, y, gUploadServer.url(), FontStyle::Bold,
                       UI_TITLE_PX);

  y += 80;
  ui::drawTextCentered(SCREEN_W / 2, y, "Sleep is paused while this is on",
                       FontStyle::Regular, 18);
  y += 32;
  ui::drawTextCentered(SCREEN_W / 2, y, "Tap the header to go back",
                       FontStyle::Regular, 18);
}

void Ui::handleWifi(const TouchEvent& ev) {
  if (ev.gesture != Gesture::Tap) return;
  scanLibrary();
  go(Screen::Settings, Display::Refresh::Partial);
}
