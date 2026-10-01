// Where the page layout reads a book's text from (format: textformat.h).
//
// A converted book is far bigger than the ESP32's memory, so FileText keeps
// only two small blocks of the file in memory and reloads as the layout
// moves through it. The layout reads almost entirely forwards, within about
// a line of where it is, so this stays fast.
#pragma once
#include <stdint.h>

#include "storage.h"

class TextSource {
public:
  virtual uint32_t length() const = 0;
  // The character at i (i < length()).
  virtual char at(uint32_t i) = 0;
  virtual ~TextSource() {}
};

// Text already in memory.
class MemText : public TextSource {
public:
  MemText(const char *text, uint32_t len) : text_(text), len_(len) {}
  uint32_t length() const override { return len_; }
  char at(uint32_t i) override { return text_[i]; }

private:
  const char *text_;
  uint32_t len_;
};

// Text read from a file through two blocks of `blockSize` bytes (least
// recently used one is replaced).
class FileText : public TextSource {
public:
  explicit FileText(uint16_t blockSize = 4096);
  ~FileText() override;
  bool open(const char *path);  // false if it cannot be opened or no memory
  void close();
  uint32_t length() const override { return size_; }
  char at(uint32_t i) override;

private:
  struct Block {
    uint32_t start = UINT32_MAX;
    uint32_t len = 0;
    uint8_t *data = nullptr;
  };
  Block *load(uint32_t i);

  uint16_t blockSize_;
  StorageFile *file_ = nullptr;
  uint32_t size_ = 0;
  Block blocks_[2];
  uint8_t lastUsed_ = 0;
};
