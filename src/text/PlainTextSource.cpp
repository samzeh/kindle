#include "text/PlainTextSource.h"

namespace {
inline bool isSpaceByte(int c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
}  // namespace

bool PlainTextSource::open(fs::FS& fs, const char* path) {
  close();
  file_ = fs.open(path, FILE_READ);
  if (!file_) return false;

  size_ = file_.size();
  bufStart_ = 0;
  bufPos_ = 0;
  bufLen_ = 0;

  // Skip a UTF-8 BOM so it does not render as a stray glyph.
  uint8_t bom[3];
  if (file_.read(bom, 3) == 3 && bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF) {
    bufStart_ = 3;
  } else {
    bufStart_ = 0;
  }
  file_.seek(bufStart_);
  return true;
}

void PlainTextSource::close() {
  if (file_) file_.close();
  size_ = 0;
  bufStart_ = 0;
  bufPos_ = 0;
  bufLen_ = 0;
}

bool PlainTextSource::fill() {
  if (bufPos_ < bufLen_) return true;
  if (!file_) return false;

  bufStart_ += bufLen_;
  bufPos_ = 0;
  int got = file_.read(buf_, sizeof(buf_));
  bufLen_ = got > 0 ? static_cast<uint16_t>(got) : 0;
  return bufLen_ > 0;
}

int PlainTextSource::peekByte() {
  if (!fill()) return -1;
  return buf_[bufPos_];
}

int PlainTextSource::getByte() {
  if (!fill()) return -1;
  return buf_[bufPos_++];
}

SourceState PlainTextSource::state() const {
  SourceState s;
  s.offset = bufStart_ + bufPos_;
  s.block = static_cast<uint8_t>(BlockStyle::Paragraph);
  s.emphasis = 0;
  s.listDepth = 0;
  return s;
}

bool PlainTextSource::restore(const SourceState& s) {
  if (!file_) return false;
  if (!file_.seek(s.offset)) return false;
  bufStart_ = s.offset;
  bufPos_ = 0;
  bufLen_ = 0;
  return true;
}

bool PlainTextSource::next(Frag& out) {
  int c = peekByte();
  if (c < 0) return false;

  out.emphasis = 0;
  out.block = BlockStyle::Paragraph;
  out.listDepth = 0;
  out.len = 0;

  if (isSpaceByte(c)) {
    uint8_t newlines = 0;
    while (true) {
      c = peekByte();
      if (c < 0 || !isSpaceByte(c)) break;
      if (c == '\n') newlines++;
      getByte();
    }
    out.kind = newlines >= 2 ? FragKind::ParaBreak : FragKind::Space;
    return true;
  }

  while (out.len < FRAG_TEXT_MAX) {
    c = peekByte();
    if (c < 0 || isSpaceByte(c)) break;
    out.text[out.len++] = static_cast<char>(getByte());
  }

  out.kind = FragKind::Word;
  return out.len > 0;
}
