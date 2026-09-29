// Paginates and renders styled text onto any Adafruit_GFX surface.
//
// Text format (a stand-in until EPUB parsing exists):
//   - one paragraph per line ('\n' separated)
//   - a paragraph beginning with "# " is a heading (bold, centered)
//   - _underscores_ toggle italics
#pragma once
#include <Adafruit_GFX.h>

// Where a page begins in the text, plus the style in effect at that point
// (a page can start in the middle of an italic run).
struct PagePos {
  uint32_t offset;
  bool italic;
};

struct PageFonts {
  const GFXfont *regular;
  const GFXfont *italic;
  const GFXfont *bold;
  const GFXfont *boldItalic;
  const GFXfont *footer;
};

class PageLayout {
public:
  PageLayout(Adafruit_GFX &gfx, const char *text, const PageFonts &fonts);

  // Lays out one page beginning at `start`, drawing it if `draw` is true.
  // Returns where the following page begins.
  PagePos layoutPage(PagePos start, bool draw);

  bool isEnd(const PagePos &pos) const {
    return pos.offset >= _len;
  }

private:
  struct Word {
    uint32_t start;
    uint16_t len;
    int16_t width;
  };

  const GFXfont *font(bool bold, bool italic) const;
  int16_t charAdvance(const GFXfont *f, char c) const;
  int16_t measureWord(uint32_t start, uint16_t len, bool bold, bool &italic) const;
  void drawWord(int16_t x, int16_t baseline, const Word &w, bool bold, bool &italic);
  uint32_t paragraphStart(uint32_t pos) const;
  uint32_t paragraphEnd(uint32_t pos) const;
  bool isHeading(uint32_t paraStart) const;
  void drawFooter(uint32_t offset);

  Adafruit_GFX &_gfx;
  const char *_text;
  uint32_t _len;
  PageFonts _fonts;
};
