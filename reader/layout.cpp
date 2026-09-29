#include "layout.h"

// On the panel a 0 bit is black and a 1 bit is white.
static const uint16_t INK = 0x0000;
static const uint16_t PAPER = 0xFFFF;

static const int16_t MARGIN_X = 24;
static const int16_t MARGIN_TOP = 32;
static const int16_t MARGIN_BOTTOM = 52;  // leaves room for the footer
static const int MAX_WORDS_PER_LINE = 48;
static const int16_t MAX_JUSTIFY_STRETCH = 2;  // extra gap allowed, in spaces

PageLayout::PageLayout(Adafruit_GFX &gfx, const char *text, const PageFonts &fonts)
  : _gfx(gfx), _text(text), _len(strlen(text)), _fonts(fonts) {}

const GFXfont *PageLayout::font(bool bold, bool italic) const {
  if (bold) return italic ? _fonts.boldItalic : _fonts.bold;
  return italic ? _fonts.italic : _fonts.regular;
}

int16_t PageLayout::charAdvance(const GFXfont *f, char c) const {
  uint8_t ch = (uint8_t)c;
  if (ch < f->first || ch > f->last) return 0;
  return f->glyph[ch - f->first].xAdvance;
}

// Width of a word in pixels. '_' toggles italics and takes no space;
// `italic` is updated to the style in effect after the word.
int16_t PageLayout::measureWord(uint32_t start, uint16_t len, bool bold, bool &italic) const {
  int16_t width = 0;
  for (uint16_t i = 0; i < len; i++) {
    char c = _text[start + i];
    if (c == '_') {
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
    char c = _text[w.start + i];
    if (c == '_') {
      italic = !italic;
      _gfx.setFont(font(bold, italic));
      continue;
    }
    _gfx.write(c);
  }
}

uint32_t PageLayout::paragraphStart(uint32_t pos) const {
  while (pos > 0 && _text[pos - 1] != '\n') pos--;
  return pos;
}

uint32_t PageLayout::paragraphEnd(uint32_t pos) const {
  while (pos < _len && _text[pos] != '\n') pos++;
  return pos;
}

bool PageLayout::isHeading(uint32_t paraStart) const {
  return _text[paraStart] == '#' && _text[paraStart + 1] == ' ';
}

void PageLayout::drawFooter(uint32_t offset) {
  char label[8];
  snprintf(label, sizeof(label), "%u%%", (unsigned)((uint64_t)offset * 100 / _len));
  _gfx.setFont(_fonts.footer);
  int16_t x1, y1;
  uint16_t w, h;
  _gfx.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
  _gfx.setCursor((_gfx.width() - w) / 2 - x1, _gfx.height() - 20);
  _gfx.print(label);
}

PagePos PageLayout::layoutPage(PagePos start, bool draw) {
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
  int16_t y = MARGIN_TOP;  // top of the next line
  bool pageEmpty = true;

  while (pos < _len) {
    if (_text[pos] == '\n') {  // empty paragraph
      pos++;
      continue;
    }
    const uint32_t paraStart = paragraphStart(pos);
    const uint32_t paraEnd = paragraphEnd(pos);
    const bool heading = isHeading(paraStart);
    const bool atParaStart = (pos == paraStart);

    if (heading && atParaStart) {
      int16_t spaceBefore = pageEmpty ? 0 : lineHeight;
      // Keep a heading together with at least one line of the text below it.
      if (y + spaceBefore + 2 * lineHeight > bottom) break;
      y += spaceBefore;
      pos += 2;  // skip "# "
    }
    const bool indentFirstLine =
      !heading && atParaStart && paraStart > 0 && !isHeading(paragraphStart(paraStart - 1));

    bool firstLine = true;
    while (pos < paraEnd) {
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
        while (p < paraEnd && _text[p] == ' ') p++;
        if (p >= paraEnd) break;
        uint32_t wordStart = p;
        while (p < paraEnd && _text[p] != ' ') p++;
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
      while (p < paraEnd && _text[p] == ' ') p++;
      const bool lastLine = (p >= paraEnd);

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
      y += lineHeight;
      firstLine = false;
      pageEmpty = false;
    }

    if (pos < paraEnd) break;  // page is full mid-paragraph
    pos = paraEnd < _len ? paraEnd + 1 : _len;
    if (heading) y += lineHeight / 2;
  }

  if (draw) drawFooter(start.offset);
  return { pos, italic };
}
