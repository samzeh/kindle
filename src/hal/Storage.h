#pragma once
#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>

#include "config.h"

// Book storage in internal flash.
//
// The reader needs a filesystem rather than an SD card specifically: with no
// PSRAM there is nowhere in RAM to hold a decompressed chapter, so storage is
// the working memory. LittleFS on the 2.4MB partition removes the card, and
// with it the unverified SD chip select, at the cost of holding one or two
// books at a time instead of a library.
//
// Everything above this layer takes an fs::FS&, so moving back to SD means
// changing begin() and fs() and nothing else.
class Storage {
 public:
  bool begin();
  bool ready() const { return ready_; }

  fs::FS& fs() { return LittleFS; }

  uint64_t totalBytes() const;
  uint64_t usedBytes() const;
  uint64_t freeBytes() const;

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
