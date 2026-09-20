#pragma once
#include <Arduino.h>

#include "app/Book.h"
#include "hal/Display.h"
#include "hal/Touch.h"

// Screen-by-screen interface state machine.
//
// Every screen redraws the whole framebuffer and presents once, because a
// partial e-paper refresh costs the same regardless of how much changed.
class Ui {
 public:
  void begin();
  void tick();

 private:
  enum class Screen : uint8_t {
    Library,
    Reader,
    Menu,
    Toc,
    Settings,
    Wifi,
  };

  void go(Screen screen, Display::Refresh refresh = Display::Refresh::Auto);
  void draw();
  void handle(const TouchEvent& ev);

  void drawLibrary();
  void drawReader();
  void drawMenu();
  void drawToc();
  void drawSettings();
  void drawWifi();

  void handleLibrary(const TouchEvent& ev);
  void handleReader(const TouchEvent& ev);
  void handleMenu(const TouchEvent& ev);
  void handleToc(const TouchEvent& ev);
  void handleSettings(const TouchEvent& ev);
  void handleWifi(const TouchEvent& ev);

  void scanLibrary();
  bool openBook(uint16_t index);
  void saveProgress();
  void applyLayoutChange();

  // Row hit-testing shared by every list screen.
  int16_t rowAt(int16_t y) const;
  uint16_t visibleRows() const;
  void drawListChrome(const char* title, uint16_t page, uint16_t pageCount);
  bool handleListNav(const TouchEvent& ev, uint16_t itemCount, uint16_t& scroll);

  static void progressTrampoline(void* user, const char* stage, uint16_t done,
                                 uint16_t total);
  void onProgress(const char* stage, uint16_t done, uint16_t total);

  static constexpr uint16_t MAX_BOOKS = 48;
  static constexpr int16_t HEADER_H = 64;
  static constexpr int16_t FOOTER_H = 56;
  static constexpr int16_t ROW_H = 56;

  Screen screen_ = Screen::Library;

  char books_[MAX_BOOKS][72];
  uint16_t bookCount_ = 0;
  uint16_t libraryScroll_ = 0;

  Book book_;
  uint16_t tocScroll_ = 0;
  uint16_t settingsScroll_ = 0;

  uint32_t lastUploadChanges_ = 0;
  uint32_t lastProgressDraw_ = 0;
  char progressTitle_[72] = {0};
};

extern Ui gUi;
