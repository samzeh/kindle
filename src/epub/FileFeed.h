#pragma once
#include <FS.h>

#include "epub/ByteFeed.h"

// ByteFeed over a filesystem file, with a small read-ahead buffer so the
// tokeniser's byte-at-a-time access does not hit the SD card constantly.
class FileFeed : public ByteFeed {
 public:
  bool open(fs::FS& fs, const char* path) {
    close();
    file_ = fs.open(path, FILE_READ);
    if (!file_) return false;
    size_ = file_.size();
    pos_ = 0;
    return true;
  }

  void close() {
    if (file_) file_.close();
    size_ = 0;
    pos_ = 0;
  }

  bool isOpen() const { return static_cast<bool>(file_); }

  int read(uint8_t* dst, uint32_t len) override {
    if (!file_) return 0;
    int got = file_.read(dst, len);
    if (got > 0) pos_ += static_cast<uint32_t>(got);
    return got;
  }

  bool seek(uint32_t offset) override {
    if (!file_) return false;
    if (offset > size_) return false;
    if (!file_.seek(offset)) return false;
    pos_ = offset;
    return true;
  }

  uint32_t size() const override { return size_; }

 private:
  File file_;
  uint32_t size_ = 0;
  uint32_t pos_ = 0;
};
