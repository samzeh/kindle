#include "app/Book.h"

#include "hal/Storage.h"

namespace {

struct ImportProgressCtx {
  Book::Progress cb;
  void* user;
};

void onExtract(void* user, uint16_t done, uint16_t total, const char* label) {
  (void)label;
  auto* ctx = static_cast<ImportProgressCtx*>(user);
  if (ctx->cb) ctx->cb(ctx->user, "Extracting", done, total);
}

void onIndex(void* user, uint16_t chapter, uint16_t chapters, uint32_t pages) {
  (void)pages;
  auto* ctx = static_cast<ImportProgressCtx*>(user);
  if (ctx->cb) ctx->cb(ctx->user, "Paginating", chapter, chapters);
}

}  // namespace

bool Book::open(const char* filename, const PageMetrics& m, Progress progress,
                void* user) {
  close();
  error_ = "";

  strncpy(filename_, filename, sizeof(filename_) - 1);
  filename_[sizeof(filename_) - 1] = '\0';

  bookId_ = Storage::bookId(filename_);
  Storage::cacheDir(bookId_, cacheDir_, sizeof(cacheDir_));
  metrics_ = m;

  if (!ensureCache(m, progress, user)) return false;

  if (!index_.open(SD, cacheDir_, m)) {
    error_ = "page index missing";
    return false;
  }

  tocCount_ = loadToc(SD, cacheDir_, toc_, MAX_TOC);
  page_ = 0;
  return true;
}

void Book::close() {
  index_.close();
  chapterFeed_.close();
  openChapter_ = -1;
  page_ = 0;
  tocCount_ = 0;
  filename_[0] = '\0';
}

bool Book::ensureCache(const PageMetrics& m, Progress progress, void* user) {
  char epubPath[160];
  snprintf(epubPath, sizeof(epubPath), "%s/%s", DIR_BOOKS, filename_);

  File probe = SD.open(epubPath, FILE_READ);
  if (!probe) {
    error_ = "book file not found";
    return false;
  }
  uint32_t sourceSize = probe.size();
  probe.close();

  bool cacheValid = loadBookMeta(SD, cacheDir_, meta_) &&
                    meta_.sourceSize == sourceSize && meta_.chapterCount > 0;

  ImportProgressCtx ctx{progress, user};

  if (!cacheValid) {
    // A stale cache is worse than none: a replaced file with the same name
    // would otherwise be read with the previous book's chapters.
    gStorage.removeTree(cacheDir_);
    gStorage.ensureDir(cacheDir_);

    EpubImporter importer;
    if (!importer.import(SD, epubPath, cacheDir_, onExtract, &ctx)) {
      error_ = importer.error();
      log_e("book: import failed: %s", error_);
      return false;
    }
    meta_ = importer.meta();
  }

  if (!PageIndex::exists(SD, cacheDir_, m)) {
    if (!PageIndex::build(SD, cacheDir_, meta_.chapterCount, m, onIndex, &ctx)) {
      error_ = "pagination failed";
      return false;
    }
  }
  return true;
}

bool Book::openChapter(uint16_t chapter) {
  if (openChapter_ == static_cast<int32_t>(chapter) && chapterFeed_.isOpen()) {
    return true;
  }

  chapterFeed_.close();

  char path[192];
  chapterCachePath(cacheDir_, chapter, path, sizeof(path));
  if (!chapterFeed_.open(SD, path)) {
    log_e("book: cannot open chapter %u", chapter);
    openChapter_ = -1;
    return false;
  }

  if (!source_.begin(&chapterFeed_)) {
    openChapter_ = -1;
    return false;
  }
  openChapter_ = chapter;
  return true;
}

bool Book::seek(uint32_t page) {
  if (page >= index_.pageCount()) return false;
  page_ = page;
  return true;
}

bool Book::nextPage() {
  if (page_ + 1 >= index_.pageCount()) return false;
  page_++;
  return true;
}

bool Book::prevPage() {
  if (page_ == 0) return false;
  page_--;
  return true;
}

bool Book::seekChapter(uint16_t chapter) {
  return seek(index_.firstPageOfChapter(chapter));
}

uint16_t Book::currentChapter() {
  return index_.chapterOfPage(page_);
}

bool Book::render(PageRenderer* renderer) {
  PageRef ref;
  if (!index_.get(page_, ref)) return false;
  if (!openChapter(ref.chapter)) return false;

  SourceState state;
  state.offset = ref.offset;
  state.sub = ref.sub;
  state.block = ref.block;
  state.emphasis = ref.emphasis;
  state.listDepth = ref.listDepth;

  if (!source_.restore(state)) return false;
  Layout::layoutPage(source_, metrics_, renderer);
  return true;
}

bool Book::relayout(const PageMetrics& m, Progress progress, void* user) {
  // Remember roughly where the reader was, as a fraction of the book, so the
  // position survives a pagination change.
  float fraction = 0.0f;
  if (index_.pageCount() > 1) {
    fraction = static_cast<float>(page_) / static_cast<float>(index_.pageCount() - 1);
  }

  index_.close();
  chapterFeed_.close();
  openChapter_ = -1;
  metrics_ = m;

  ImportProgressCtx ctx{progress, user};
  if (!PageIndex::exists(SD, cacheDir_, m)) {
    if (!PageIndex::build(SD, cacheDir_, meta_.chapterCount, m, onIndex, &ctx)) {
      error_ = "pagination failed";
      return false;
    }
  }

  if (!index_.open(SD, cacheDir_, m)) {
    error_ = "page index missing";
    return false;
  }

  uint32_t target = static_cast<uint32_t>(fraction * (index_.pageCount() - 1) + 0.5f);
  page_ = target < index_.pageCount() ? target : index_.pageCount() - 1;
  return true;
}
