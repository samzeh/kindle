#pragma once
#include <Arduino.h>

#include "epub/Epub.h"
#include "epub/FileFeed.h"
#include "epub/PageIndex.h"
#include "epub/XhtmlSource.h"
#include "text/Layout.h"

// An open book: cached chapters on the SD card, a page index for the current
// layout settings, and a cursor.
//
// Only one chapter file is held open at a time, so memory use does not grow
// with book length.
class Book {
 public:
  // Reported during the one-time import and indexing passes.
  using Progress = void (*)(void* user, const char* stage, uint16_t done,
                            uint16_t total);

  // Imports and indexes if needed, then positions at `startPage`.
  bool open(const char* filename, const PageMetrics& m, Progress progress = nullptr,
            void* user = nullptr);
  void close();
  bool isOpen() const { return index_.isOpen(); }

  const BookMeta& meta() const { return meta_; }
  const char* filename() const { return filename_; }
  uint32_t bookId() const { return bookId_; }
  const char* error() const { return error_; }

  uint32_t pageCount() const { return index_.pageCount(); }
  uint32_t currentPage() const { return page_; }
  uint16_t currentChapter();

  bool seek(uint32_t page);
  bool nextPage();
  bool prevPage();
  bool seekChapter(uint16_t chapter);

  // Draws the current page. The caller clears the framebuffer first.
  bool render(PageRenderer* renderer);

  uint16_t tocCount() const { return tocCount_; }
  const TocItem& tocItem(uint16_t i) const { return toc_[i]; }

  // Rebuilds the index for new layout settings, preserving the reading
  // position as closely as the new pagination allows.
  bool relayout(const PageMetrics& m, Progress progress = nullptr,
                void* user = nullptr);

 private:
  bool ensureCache(const PageMetrics& m, Progress progress, void* user);
  bool openChapter(uint16_t chapter);

  static constexpr uint16_t MAX_TOC = 64;

  char filename_[72] = {0};
  char cacheDir_[64] = {0};
  uint32_t bookId_ = 0;

  BookMeta meta_;
  PageIndex index_;
  PageMetrics metrics_;

  FileFeed chapterFeed_;
  XhtmlSource source_;
  int32_t openChapter_ = -1;

  uint32_t page_ = 0;
  TocItem toc_[MAX_TOC];
  uint16_t tocCount_ = 0;
  const char* error_ = "";
};
