#pragma once
#include <Arduino.h>
#include <FS.h>

#include "text/Layout.h"

// Where one page starts. Twelve bytes, so even a thousand-page book indexes
// to 12KB on the SD card and a lookup is a single seek and read.
struct PageRef {
  uint16_t chapter;
  uint16_t sub;
  uint32_t offset;
  uint8_t block;
  uint8_t emphasis;
  uint8_t listDepth;
  uint8_t reserved;
};
static_assert(sizeof(PageRef) == 12, "PageRef must stay 12 bytes on disk");

// Page boundaries for one book at one set of layout settings.
//
// Built by running the real layout engine in measure mode, so an indexed
// boundary is exactly where the renderer will break the page. Changing font
// size, margin or line spacing produces a separate index file.
class PageIndex {
 public:
  using Progress = void (*)(void* user, uint16_t chapter, uint16_t chapters,
                            uint32_t pages);

  static void path(const char* cacheDir, const PageMetrics& m, char* out,
                   size_t outLen);
  static bool exists(fs::FS& fs, const char* cacheDir, const PageMetrics& m);

  // Paginates every chapter. Expensive (seconds to minutes for a long book),
  // so callers should show progress.
  static bool build(fs::FS& fs, const char* cacheDir, uint16_t chapterCount,
                    const PageMetrics& m, Progress progress = nullptr,
                    void* user = nullptr);

  bool open(fs::FS& fs, const char* cacheDir, const PageMetrics& m);
  void close();
  bool isOpen() const { return static_cast<bool>(file_); }

  uint32_t pageCount() const { return pageCount_; }
  bool get(uint32_t page, PageRef& out);

  // Index of the first page at or after the start of `chapter`.
  uint32_t firstPageOfChapter(uint16_t chapter);
  // Chapter containing `page`, or 0.
  uint16_t chapterOfPage(uint32_t page);

 private:
  File file_;
  uint32_t pageCount_ = 0;
};
