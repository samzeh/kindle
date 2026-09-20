#pragma once
#include <Arduino.h>
#include <FS.h>

#include "epub/ZipReader.h"

struct BookMeta {
  char title[80] = {0};
  char author[56] = {0};
  uint16_t chapterCount = 0;
  uint32_t sourceSize = 0;  // EPUB byte size, used to detect a stale cache
};

struct TocItem {
  char title[64];
  uint16_t chapter;
};

// One-time import of an EPUB into the SD cache.
//
// Nothing here stays resident: chapters are decompressed straight to
// individual files so the reader can seek into them later without ever
// holding a chapter in RAM.
class EpubImporter {
 public:
  using Progress = void (*)(void* user, uint16_t done, uint16_t total, const char* label);

  // Extracts spine chapters to cacheDir/ch###.xht, writes book.meta and
  // toc.bin. Safe to re-run; it overwrites.
  bool import(fs::FS& fs, const char* epubPath, const char* cacheDir,
              Progress progress = nullptr, void* user = nullptr);

  const BookMeta& meta() const { return meta_; }
  const char* error() const { return error_; }

 private:
  bool importInner(fs::FS& fs, const char* epubPath, const char* cacheDir,
                   Progress progress, void* user);
  bool readContainer(char* opfPathOut, size_t outLen);
  bool loadManifest(fs::FS& fs, const char* opfCachePath);
  const char* hrefForId(const char* id) const;
  bool extractSpine(fs::FS& fs, const char* opfCachePath, const char* cacheDir,
                    Progress progress, void* user);
  bool writeMeta(fs::FS& fs, const char* cacheDir);
  bool buildToc(fs::FS& fs, const char* cacheDir, const char* opfCachePath);

  ZipReader zip_;
  BookMeta meta_;
  char opfDir_[96] = {0};  // directory of the OPF inside the ZIP, with trailing '/'
  const char* error_ = "";
};

// Cache file names, relative to a book's cache directory.
const char* const kCacheOpf = "package.opf";
const char* const kCacheMeta = "book.meta";
const char* const kCacheToc = "toc.bin";

void chapterCachePath(const char* cacheDir, uint16_t chapter, char* out, size_t outLen);
bool loadBookMeta(fs::FS& fs, const char* cacheDir, BookMeta& out);
uint16_t loadToc(fs::FS& fs, const char* cacheDir, TocItem* items, uint16_t maxItems);

// Resolves an OPF-relative href against the OPF's directory, dropping any
// fragment and decoding percent escapes.
void resolveZipPath(const char* baseDir, const char* href, char* out, size_t outLen);
