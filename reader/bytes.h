// A stream of bytes to read from front to back. The ZIP, XML and JPEG code
// all read through this, so each works the same on a file, a ZIP entry being
// decompressed, or bytes already in memory (tests).
#pragma once
#include <stdint.h>
#include <string.h>

#include "storage.h"

struct ByteReader {
  // Reads up to n bytes into buf; returns how many (0 at the end, -1 on error).
  virtual int32_t read(uint8_t *buf, uint32_t n) = 0;
  virtual ~ByteReader() {}
};

struct MemReader : ByteReader {
  const uint8_t *data;
  uint32_t size, pos = 0;
  MemReader(const void *d, uint32_t n) : data((const uint8_t *)d), size(n) {}
  int32_t read(uint8_t *buf, uint32_t n) override {
    if (n > size - pos) n = size - pos;
    memcpy(buf, data + pos, n);
    pos += n;
    return (int32_t)n;
  }
};

struct FileReader : ByteReader {
  StorageFile *file;
  explicit FileReader(StorageFile *f) : file(f) {}
  int32_t read(uint8_t *buf, uint32_t n) override { return storageRead(file, buf, n); }
};
