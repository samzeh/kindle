#include "textsource.h"

#include <stdlib.h>

FileText::FileText(uint16_t blockSize) : blockSize_(blockSize) {}

FileText::~FileText() {
  close();
}

bool FileText::open(const char *path) {
  close();
  for (Block &b : blocks_) {
    b.data = (uint8_t *)malloc(blockSize_);
    if (!b.data) {
      close();
      return false;
    }
  }
  file_ = storageOpen(path, false);
  if (!file_) {
    close();
    return false;
  }
  size_ = storageSize(file_);
  return true;
}

void FileText::close() {
  storageClose(file_);
  file_ = nullptr;
  size_ = 0;
  for (Block &b : blocks_) {
    free(b.data);
    b = Block();
  }
}

FileText::Block *FileText::load(uint32_t i) {
  uint8_t slot = lastUsed_ ^ 1;  // replace the one not used most recently
  Block &b = blocks_[slot];
  b.start = i - i % blockSize_;
  int32_t got = storageSeek(file_, b.start) ? storageRead(file_, b.data, blockSize_) : -1;
  b.len = got > 0 ? (uint32_t)got : 0;
  lastUsed_ = slot;
  return &b;
}

char FileText::at(uint32_t i) {
  for (uint8_t s = 0; s < 2; s++) {
    Block &b = blocks_[s];
    if (i >= b.start && i - b.start < b.len) {
      lastUsed_ = s;
      return (char)b.data[i - b.start];
    }
  }
  if (!file_ || i >= size_) return '\n';
  Block *b = load(i);
  return i - b->start < b->len ? (char)b->data[i - b->start] : '\n';
}
