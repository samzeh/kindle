#pragma once
#include <FS.h>

#include "text/Content.h"

// Reads a plain .txt file as a fragment stream.
//
// A single newline is treated as a space (hard-wrapped source text reflows),
// and a blank line starts a new paragraph, which is the convention Project
// Gutenberg plain-text files follow.
class PlainTextSource : public ContentSource {
 public:
  bool open(fs::FS& fs, const char* path);
  void close();

  SourceState state() const override;
  bool next(Frag& out) override;
  bool restore(const SourceState& s) override;
  uint32_t sizeBytes() const override { return size_; }

 private:
  bool fill();
  int peekByte();
  int getByte();

  File file_;
  uint32_t size_ = 0;
  uint32_t bufStart_ = 0;  // file offset of buf_[0]
  uint16_t bufPos_ = 0;
  uint16_t bufLen_ = 0;
  uint8_t buf_[512];
};
