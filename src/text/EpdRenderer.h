#pragma once
#include "hal/Display.h"
#include "text/Layout.h"

// Draws laid-out glyphs into the e-paper framebuffer.
class EpdRenderer : public PageRenderer {
 public:
  void glyph(int16_t penX, int16_t baseline, const Glyph* g) override {
    // Adafruit_GFX's 6-argument drawBitmap paints only the set bits, which
    // matches the 1bpp MSB-first packing the font cache produces.
    gDisplay.gfx().drawBitmap(penX + g->xoff, baseline + g->yoff,
                              const_cast<uint8_t*>(g->bits), g->w, g->h,
                              GxEPD_BLACK);
  }

  void rule(int16_t x, int16_t y, int16_t w) override {
    gDisplay.gfx().drawFastHLine(x, y, w, GxEPD_BLACK);
  }
};
