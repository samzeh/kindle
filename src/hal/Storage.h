#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SD.h>

#include "config.h"

// microSD on the FTS02, sharing VSPI with the e-paper.
class Storage {
 public:
  bool begin();
  bool ready() const { return ready_; }

  uint64_t totalBytes() const;
  uint64_t usedBytes() const;

  // Create a directory and any missing parents.
  bool ensureDir(const char* path);

  // Delete a directory and everything under it. Used to drop a book's cache.
  bool removeTree(const char* path);

  // Stable 32-bit id for a book, derived from its filename. Cache directories
  // are named after this so a book keeps its cache across reboots.
  static uint32_t bookId(const char* filename);
  static void cacheDir(uint32_t id, char* out, size_t outLen);

 private:
  bool ready_ = false;
};

extern Storage gStorage;
