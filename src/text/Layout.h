#pragma once
#include <Arduino.h>

#include "config.h"
#include "text/Content.h"
#include "text/FontCache.h"

// Where a page's text goes and how it is spaced.
struct PageMetrics {
  int16_t x = 0;
  int16_t y = 0;
  int16_t w = 0;
  int16_t h = 0;
  uint16_t fontPx = 26;
  uint8_t lineSpacingPct = LINE_SPACING_DEFAULT;
};

// Receives positioned glyphs. Measuring passes supply nullptr instead.
class PageRenderer {
 public:
  virtual ~PageRenderer() {}
  virtual void glyph(int16_t x, int16_t baseline, const Glyph* g) = 0;
  virtual void rule(int16_t x, int16_t y, int16_t w) = 0;
};

struct PageResult {
  SourceState next;      // where the following page begins
  bool endOfContent = false;
  uint16_t lines = 0;
};

// Single-pass line breaker shared by the indexing and drawing paths.
//
// Running the identical code in both modes is what makes the page index
// trustworthy: a measured page break lands in exactly the same place the
// renderer would put it.
class Layout {
 public:
  static PageResult layoutPage(ContentSource& src, const PageMetrics& m,
                               PageRenderer* renderer);

  // Body size scaled up for headings.
  static uint16_t sizeForBlock(BlockStyle block, uint16_t basePx);
  static FontStyle styleForBlock(BlockStyle block, uint8_t emphasis);
  static int16_t indentForBlock(BlockStyle block, uint16_t basePx, uint8_t listDepth);
  static bool centeredBlock(BlockStyle block);
};
