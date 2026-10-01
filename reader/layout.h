// Paginates and renders styled text onto any Adafruit_GFX surface.
//
// The text is in the reader's format (textformat.h): one paragraph per line,
// with markers for headings, unindented paragraphs and italics. It is read
// through a TextSource, so a whole book never has to be in memory.
#pragma once
#include <Adafruit_GFX.h>

#include "textsource.h"

// Where a page begins in the text, plus the state in effect there: a page
// can start in the middle of an italic run, or in the middle of a heading
// that wrapped onto several lines.
struct PagePos {
  uint32_t offset;
  bool italic;
  bool inHeading;
};

// Packs a PagePos into 32 bits (30-bit offset, 1 GB of text) for storing.
inline uint32_t pagePack(PagePos p) {
  return (p.offset & 0x3FFFFFFF) | (p.italic ? 0x80000000u : 0) | (p.inHeading ? 0x40000000u : 0);
}
inline PagePos pageUnpack(uint32_t v) {
  return { v & 0x3FFFFFFF, (v & 0x80000000u) != 0, (v & 0x40000000u) != 0 };
}

struct PageFonts {
  const GFXfont *regular;
  const GFXfont *italic;
  const GFXfont *bold;
  const GFXfont *boldItalic;
  const GFXfont *footer;
};

class PageLayout {
public:
  PageLayout(Adafruit_GFX &gfx, TextSource &text, const PageFonts &fonts);

  // Lays out one page beginning at `start`, drawing it if `draw` is true
  // (the page area is cleared first; the footer is left to the caller).
  // Returns where the following page begins. With draw = false nothing is
  // touched on the canvas, so this is also how a book is paginated.
  PagePos layoutPage(PagePos start, bool draw);

  bool isEnd(const PagePos &pos) const {
    return pos.offset >= _text.length();
  }

  // Where the footer's baseline goes, below the text area.
  int16_t footerBaseline() const {
    return _gfx.height() - 20;
  }

private:
  struct Word {
    uint32_t start;
    uint16_t len;
    int16_t width;
  };

  const GFXfont *font(bool bold, bool italic) const;
  int16_t charAdvance(const GFXfont *f, char c) const;
  int16_t measureWord(uint32_t start, uint16_t len, bool bold, bool &italic);
  void drawWord(int16_t x, int16_t baseline, const Word &w, bool bold, bool &italic);
  bool atParagraphEnd(uint32_t pos);

  Adafruit_GFX &_gfx;
  TextSource &_text;
  PageFonts _fonts;
};
