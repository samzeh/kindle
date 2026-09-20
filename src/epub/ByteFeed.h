#pragma once
#include <Arduino.h>

// A seekable byte stream. Abstracting this is what lets the XHTML tokeniser
// run against an SD file on device and a memory buffer in host tests.
class ByteFeed {
 public:
  virtual ~ByteFeed() {}
  virtual int read(uint8_t* dst, uint32_t len) = 0;
  virtual bool seek(uint32_t offset) = 0;
  virtual uint32_t size() const = 0;
};

class MemoryFeed : public ByteFeed {
 public:
  MemoryFeed(const uint8_t* data, uint32_t len) : data_(data), len_(len) {}

  int read(uint8_t* dst, uint32_t len) override {
    uint32_t avail = pos_ < len_ ? len_ - pos_ : 0;
    uint32_t n = len < avail ? len : avail;
    memcpy(dst, data_ + pos_, n);
    pos_ += n;
    return static_cast<int>(n);
  }

  bool seek(uint32_t offset) override {
    if (offset > len_) return false;
    pos_ = offset;
    return true;
  }

  uint32_t size() const override { return len_; }

 private:
  const uint8_t* data_;
  uint32_t len_;
  uint32_t pos_ = 0;
};
