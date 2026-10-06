#include "layout.h"

#include "textformat.h"

// On the panel a 0 bit is black and a 1 bit is white.
static const uint16_t INK = 0x0000;
static const uint16_t PAPER = 0xFFFF;

static const int16_t MARGIN_X = 24;
// The reading screen's controls are overlays (they hide the lines under them
// while shown), so the margins need no room for them.
static const int16_t MARGIN_TOP = 30;
static const int16_t MARGIN_BOTTOM = 16;
static const int MAX_WORDS_PER_LINE = 48;
static const int16_t MAX_JUSTIFY_STRETCH = 2;  // extra gap allowed, in spaces

PageLayout::PageLayout(Adafruit_GFX &gfx, TextSource &text, const PageFonts &fonts)
  : _gfx(gfx), _text(text), _fonts(fonts) {}

const GFXfont *PageLayout::font(bool bold, bool italic) const {
  if (bold) return italic ? _fonts.boldItalic : _fonts.bold;
  return italic ? _fonts.italic : _fonts.regular;
}

int16_t PageLayout::charAdvance(const GFXfont *f, char c) const {
  uint8_t ch = (uint8_t)c;
  if (ch < f->first || ch > f->last) return 0;
  return f->glyph[ch - f->first].xAdvance;
}

// Width of a word in pixels. TXT_ITALIC toggles italics and takes no space;
// `italic` is updated to the style in effect after the word.
int16_t PageLayout::measureWord(uint32_t start, uint16_t len, bool bold, bool &italic) {
  int16_t width = 0;
  for (uint16_t i = 0; i < len; i++) {
    char c = _text.at(start + i);
    if (c == TXT_ITALIC) {
      italic = !italic;
      continue;
    }
    width += charAdvance(font(bold, italic), c);
  }
  return width;
}

void PageLayout::drawWord(int16_t x, int16_t baseline, const Word &w, bool bold, bool &italic) {
  _gfx.setFont(font(bold, italic));
  _gfx.setCursor(x, baseline);
  for (uint16_t i = 0; i < w.len; i++) {
    char c = _text.at(w.start + i);
    if (c == TXT_ITALIC) {
      italic = !italic;
      _gfx.setFont(font(bold, italic));
      continue;
    }
    _gfx.write(c);
  }
}

bool PageLayout::atParagraphEnd(uint32_t pos) {
  return pos >= _text.length() || _text.at(pos) == '\n';
}

PagePos PageLayout::layoutPage(PagePos start, bool draw) {
  const uint32_t len = _text.length();
  const int16_t maxWidth = _gfx.width() - 2 * MARGIN_X;
  const int16_t bottom = _gfx.height() - MARGIN_BOTTOM;
  const int16_t lineHeight = _fonts.regular->yAdvance;
  const int16_t ascent = -_fonts.regular->glyph['d' - _fonts.regular->first].yOffset;
  const int16_t spaceWidth = charAdvance(_fonts.regular, ' ');
  const int16_t indentWidth = 3 * spaceWidth;

  if (draw) {
    _gfx.fillScreen(PAPER);
    _gfx.setTextColor(INK);
    _gfx.setTextWrap(false);
  }

  uint32_t pos = start.offset;
  bool italic = start.italic;
  bool heading = start.inHeading;  // for a paragraph continued from the last page
  int16_t y = MARGIN_TOP;          // top of the next line
  bool pageEmpty = true;
  _lineCount = 0;

  while (pos < len) {
    char c = _text.at(pos);
    if (c == '\n') {  // between paragraphs
      pos++;
      continue;
    }
    const bool atParaStart = pos == 0 || _text.at(pos - 1) == '\n';
    if (atParaStart && c == TXT_PAGEBREAK) {
      if (!pageEmpty) break;  // this paragraph starts the next page
      if (++pos >= len) break;
      c = _text.at(pos);      // the paragraph's own markers follow
    }
    bool noIndent = false;
    if (atParaStart) {
      heading = c == TXT_HEADING;
      noIndent = c == TXT_NOINDENT;
    }

    if (heading && atParaStart) {
      int16_t spaceBefore = pageEmpty ? 0 : lineHeight;
      // Keep a heading together with at least one line of the text below it.
      if (y + spaceBefore + 2 * lineHeight > bottom) break;
      y += spaceBefore;
    }
    if (atParaStart && (heading || noIndent)) pos++;  // skip the marker
    const bool indentFirstLine = atParaStart && !heading && !noIndent;

    bool firstLine = true;
    while (!atParagraphEnd(pos)) {
      if (y + lineHeight > bottom) break;

      // Greedily collect the words that fit on this line.
      const int16_t indent = (firstLine && indentFirstLine) ? indentWidth : 0;
      const int16_t lineMax = maxWidth - indent;
      Word words[MAX_WORDS_PER_LINE];
      int count = 0;
      int16_t lineWidth = 0;
      uint32_t p = pos;
      bool lineItalic = italic;
      while (count < MAX_WORDS_PER_LINE) {
        while (!atParagraphEnd(p) && _text.at(p) == ' ') p++;
        if (atParagraphEnd(p)) break;
        uint32_t wordStart = p;
        while (!atParagraphEnd(p) && _text.at(p) != ' ') p++;
        bool wordItalic = lineItalic;
        int16_t w = measureWord(wordStart, p - wordStart, heading, wordItalic);
        int16_t needed = lineWidth + (count ? spaceWidth : 0) + w;
        if (count > 0 && needed > lineMax) {
          p = wordStart;
          break;
        }
        words[count++] = { wordStart, (uint16_t)(p - wordStart), w };
        lineWidth = needed;
        lineItalic = wordItalic;
      }
      while (!atParagraphEnd(p) && _text.at(p) == ' ') p++;
      const bool lastLine = atParagraphEnd(p);

      if (draw) {
        int16_t x = MARGIN_X + indent;
        int16_t gap = spaceWidth, extra = 0;
        if (heading) {
          x = MARGIN_X + (maxWidth - lineWidth) / 2;
        } else if (!lastLine && count > 1) {  // justify
          int16_t slack = lineMax - lineWidth;
          // A line with only a few long words would get huge gaps;
          // leave such lines left-aligned instead.
          if (slack / (count - 1) <= MAX_JUSTIFY_STRETCH * spaceWidth) {
            gap += slack / (count - 1);
            extra = slack % (count - 1);
          }
        }
        bool drawItalic = italic;
        for (int i = 0; i < count; i++) {
          drawWord(x, y + ascent, words[i], heading, drawItalic);
          x += words[i].width + gap + (i < extra ? 1 : 0);
        }
      }

      italic = lineItalic;
      pos = p;
      if (_lineCount < MAX_LINES) {
        _lineTop[_lineCount] = y;
        _lineBottom[_lineCount] = y + lineHeight;
        _lineCount++;
      }
      y += lineHeight;
      firstLine = false;
      pageEmpty = false;
    }

    if (!atParagraphEnd(pos)) break;  // page is full mid-paragraph
    if (pos < len) pos++;             // past the '\n'
    if (heading) y += lineHeight / 2;
  }

  return { pos, italic, heading && pos < len && !(pos == 0 || _text.at(pos - 1) == '\n') };
}

int16_t PageLayout::coverDownTo(int16_t y) const {
  int16_t bottom = y;
  for (uint8_t i = 0; i < _lineCount; i++)
    if (_lineTop[i] < y && _lineBottom[i] > bottom) bottom = _lineBottom[i];
  return bottom;
}

int16_t PageLayout::coverUpTo(int16_t y) const {
  int16_t top = y;
  for (uint8_t i = 0; i < _lineCount; i++)
    if (_lineBottom[i] > y && _lineTop[i] < top) top = _lineTop[i];
  return top;
}
