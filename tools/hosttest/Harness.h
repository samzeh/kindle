#pragma once
#include <string>
#include <vector>

#include "text/Content.h"
#include "text/Layout.h"

// A ContentSource backed by an in-memory buffer, so host tests can drive the
// layout engine without a filesystem.
class MemTextSource : public ContentSource {
 public:
  explicit MemTextSource(std::string data) : data_(std::move(data)) {}

  SourceState state() const override {
    SourceState s;
    s.offset = pos_;
    return s;
  }

  bool restore(const SourceState& s) override {
    pos_ = s.offset;
    return pos_ <= data_.size();
  }

  uint32_t sizeBytes() const override { return data_.size(); }

  bool next(Frag& out) override {
    if (pos_ >= data_.size()) return false;

    out.emphasis = 0;
    out.block = BlockStyle::Paragraph;
    out.listDepth = 0;
    out.len = 0;

    auto isSpace = [](char c) {
      return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };

    if (isSpace(data_[pos_])) {
      int newlines = 0;
      while (pos_ < data_.size() && isSpace(data_[pos_])) {
        if (data_[pos_] == '\n') newlines++;
        pos_++;
      }
      out.kind = newlines >= 2 ? FragKind::ParaBreak : FragKind::Space;
      return true;
    }

    while (pos_ < data_.size() && out.len < FRAG_TEXT_MAX && !isSpace(data_[pos_])) {
      out.text[out.len++] = data_[pos_++];
    }
    out.kind = FragKind::Word;
    return out.len > 0;
  }

 private:
  std::string data_;
  uint32_t pos_ = 0;
};

// Renders into an 8bpp greyscale buffer, mirroring exactly what
// Adafruit_GFX::drawBitmap does on device (MSB first, byteWidth = (w+7)/8,
// only set bits painted).
class BitmapRenderer : public PageRenderer {
 public:
  BitmapRenderer(int w, int h) : w_(w), h_(h), px_(static_cast<size_t>(w) * h, 255) {}

  void clear() { std::fill(px_.begin(), px_.end(), 255); }

  void glyph(int16_t penX, int16_t baseline, const Glyph* g) override {
    int x0 = penX + g->xoff;
    int y0 = baseline + g->yoff;
    for (int y = 0; y < g->h; y++) {
      for (int x = 0; x < g->w; x++) {
        if (g->bits[y * g->stride + (x >> 3)] & (0x80 >> (x & 7))) set(x0 + x, y0 + y);
      }
    }
    inked_ += g->w;
  }

  void rule(int16_t x, int16_t y, int16_t w) override {
    for (int i = 0; i < w; i++) set(x + i, y);
  }

  bool writePgm(const char* path) const {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "P5\n%d %d\n255\n", w_, h_);
    fwrite(px_.data(), 1, px_.size(), f);
    fclose(f);
    return true;
  }

  long inked() const { return inked_; }

 private:
  void set(int x, int y) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) {
      clipped_++;
      return;
    }
    px_[static_cast<size_t>(y) * w_ + x] = 0;
  }

  int w_, h_;
  std::vector<uint8_t> px_;
  long inked_ = 0;

 public:
  long clipped_ = 0;
};
