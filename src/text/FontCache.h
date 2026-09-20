#pragma once
#include <Arduino.h>

enum class FontStyle : uint8_t {
  Regular = 0,
  Bold = 1,
  Italic = 2,
  BoldItalic = 3,
};
static constexpr uint8_t FONT_STYLE_COUNT = 4;

// A rasterised glyph, 1 bit per pixel, rows padded to whole bytes.
struct Glyph {
  const uint8_t* bits;
  int16_t advance;  // pen advance
  int16_t xoff;     // left edge relative to the pen
  int16_t yoff;     // top edge relative to the baseline (negative = above)
  uint8_t w;
  uint8_t h;
  uint8_t stride;  // bytes per row
};

// stb_truetype wrapper with a fixed-size raster cache.
//
// Advances are answered from a small lookup table so the measuring pass that
// builds the page index never rasterises anything. Only the draw pass touches
// the raster cache, which is a 4-way set-associative slab sized for the
// current font size and flushed whenever that size changes.
class FontCache {
 public:
  bool begin();

  // Metrics. Cheap enough to call per character during layout.
  int16_t advance(FontStyle style, uint32_t codepoint) const;
  int16_t lineHeight() const { return lineHeight_; }
  int16_t ascent() const { return ascent_; }
  int16_t descent() const { return descent_; }

  // Metrics for a size other than the current one, without disturbing state.
  int16_t lineHeightFor(uint16_t px) const;

  uint16_t size() const { return px_; }

  // Changing size flushes the raster cache and rebuilds the advance tables.
  void setSize(uint16_t px);

  // Rasterise (or fetch) a glyph at the current size. Returns nullptr for
  // codepoints the subset font does not contain.
  const Glyph* glyph(FontStyle style, uint32_t codepoint);

  void flush();

 private:
  struct Slot {
    uint32_t key;  // (style << 24) | codepoint, 0 = empty
    uint32_t stamp;
    Glyph glyph;
  };

  // Slot table lives at the front of the raster arena; pixel payloads follow.
  static Slot* slots();

  int16_t measure(FontStyle style, uint32_t codepoint) const;
  bool rasterise(FontStyle style, uint32_t codepoint, Slot& out, uint8_t* dest);

  uint16_t px_ = 0;
  float scale_[FONT_STYLE_COUNT] = {0};
  int16_t ascent_ = 0;
  int16_t descent_ = 0;
  int16_t lineHeight_ = 0;

  // Advance table for ASCII at the current size; anything else falls through
  // to stb_truetype, which is rare in practice.
  static constexpr uint32_t ASCII_LO = 0x20;
  static constexpr uint32_t ASCII_HI = 0x7F;
  int16_t asciiAdvance_[FONT_STYLE_COUNT][ASCII_HI - ASCII_LO] = {};

  uint32_t stamp_ = 0;
  uint16_t slotBytes_ = 0;
  uint16_t slotCount_ = 0;
  bool ready_ = false;
};

extern FontCache gFonts;
