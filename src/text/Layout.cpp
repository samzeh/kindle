#include "text/Layout.h"

#include "config.h"

namespace {

constexpr uint8_t MAX_WORDS_PER_LINE = 32;
constexpr uint16_t LINE_TEXT_BYTES = 256;

struct LineBuf {
  char text[LINE_TEXT_BYTES];
  struct Word {
    uint16_t off;
    uint8_t len;
    FontStyle style;
    int16_t width;
    int16_t sep;  // gap rendered before this word
  } words[MAX_WORDS_PER_LINE];

  uint16_t textLen;
  uint8_t count;
  int16_t width;  // includes inter-word spaces
  BlockStyle block;
  uint8_t listDepth;
  bool hasContent;
};

// Kept out of the stack: the reader task runs with an 8KB stack and this is
// most of a kilobyte.
LineBuf gLine;

void resetLine(LineBuf& l) {
  l.textLen = 0;
  l.count = 0;
  l.width = 0;
  l.hasContent = false;
}

int16_t wordWidth(const char* text, size_t len, FontStyle style) {
  int16_t w = 0;
  size_t i = 0;
  while (i < len) {
    uint32_t cp = utf8Next(text, len, &i);
    if (cp == 0) break;
    w += gFonts.advance(style, cp);
  }
  return w;
}

// `sep` is the gap to insert before this word: a space normally, zero when
// the word is glued to the previous one.
bool addWord(LineBuf& l, const char* text, uint8_t len, FontStyle style,
             int16_t width, int16_t sep) {
  if (l.count >= MAX_WORDS_PER_LINE) return false;
  if (l.textLen + len > LINE_TEXT_BYTES) return false;

  if (l.hasContent) l.width += sep;

  l.words[l.count].off = l.textLen;
  l.words[l.count].len = len;
  l.words[l.count].style = style;
  l.words[l.count].width = width;
  l.words[l.count].sep = l.hasContent ? sep : 0;
  memcpy(l.text + l.textLen, text, len);

  l.textLen += len;
  l.count++;
  l.width += width;
  l.hasContent = true;
  return true;
}

// Removes the trailing word so it can be carried to the next line together
// with the fragment glued to it.
void detachLastWord(LineBuf& l, char* text, uint8_t& len, FontStyle& style,
                    int16_t& width) {
  const LineBuf::Word& w = l.words[l.count - 1];
  len = w.len;
  style = w.style;
  width = w.width;
  memcpy(text, l.text + w.off, len);

  l.width -= w.width + w.sep;
  l.textLen = w.off;
  l.count--;
  l.hasContent = l.count > 0;
}

void drawLine(const LineBuf& l, int16_t y, const PageMetrics& m, PageRenderer* r) {
  int16_t indent = Layout::indentForBlock(l.block, m.fontPx, l.listDepth);

  int16_t x = m.x + indent;
  if (Layout::centeredBlock(l.block)) {
    int16_t avail = m.w - indent;
    x += (avail - l.width) / 2;
    if (x < m.x) x = m.x;
  }

  int16_t baseline = y + gFonts.ascent();
  int16_t clipRight = m.x + m.w;

  for (uint8_t i = 0; i < l.count; i++) {
    x += l.words[i].sep;

    const char* t = l.text + l.words[i].off;
    uint8_t len = l.words[i].len;
    FontStyle style = l.words[i].style;

    size_t p = 0;
    while (p < len) {
      uint32_t cp = utf8Next(t, len, &p);
      if (cp == 0) break;

      const Glyph* g = gFonts.glyph(style, cp);
      if (g) {
        if (x < clipRight) r->glyph(x, baseline, g);
        x += g->advance;
      } else {
        x += gFonts.advance(style, cp);
      }
    }
  }
}

int16_t gapAfterBlock(BlockStyle block, uint16_t basePx) {
  int16_t gap = static_cast<int16_t>(basePx * 40 / 100);
  switch (block) {
    case BlockStyle::H1:
    case BlockStyle::H2:
    case BlockStyle::H3:
      gap += static_cast<int16_t>(basePx * 30 / 100);
      break;
    default:
      break;
  }
  return gap;
}

}  // namespace

uint16_t Layout::sizeForBlock(BlockStyle block, uint16_t basePx) {
  switch (block) {
    case BlockStyle::H1: return static_cast<uint16_t>(basePx * 150 / 100);
    case BlockStyle::H2: return static_cast<uint16_t>(basePx * 130 / 100);
    case BlockStyle::H3: return static_cast<uint16_t>(basePx * 115 / 100);
    default: return basePx;
  }
}

FontStyle Layout::styleForBlock(BlockStyle block, uint8_t emphasis) {
  switch (block) {
    case BlockStyle::H1:
    case BlockStyle::H2:
    case BlockStyle::H3:
    case BlockStyle::H4:
    case BlockStyle::H5:
    case BlockStyle::H6:
      return styleFromEmphasis(emphasis | EMPH_BOLD);
    case BlockStyle::Blockquote:
      return styleFromEmphasis(emphasis | EMPH_ITALIC);
    default:
      return styleFromEmphasis(emphasis);
  }
}

int16_t Layout::indentForBlock(BlockStyle block, uint16_t basePx, uint8_t listDepth) {
  switch (block) {
    case BlockStyle::Blockquote:
      return static_cast<int16_t>(basePx);
    case BlockStyle::ListItem:
      return static_cast<int16_t>(basePx * (listDepth ? listDepth : 1));
    default:
      return 0;
  }
}

bool Layout::centeredBlock(BlockStyle block) {
  return block == BlockStyle::H1 || block == BlockStyle::H2;
}

PageResult Layout::layoutPage(ContentSource& src, const PageMetrics& m,
                              PageRenderer* renderer) {
  PageResult res;
  gFonts.setSize(m.fontPx);
  const int16_t bottom = m.y + m.h;

  int16_t y = m.y;
  SourceState lineStart = src.state();
  // Resume point of the word before the one being processed, needed when a
  // glued pair has to move to the next line together.
  SourceState prevWordStart = lineStart;
  BlockStyle curBlock = BlockStyle::Paragraph;

  resetLine(gLine);
  gLine.block = BlockStyle::Paragraph;
  gLine.listDepth = 0;

  // Advance for the block currently being laid out. Assumes the caller has
  // already selected the matching font size.
  auto lineAdvance = [&]() -> int16_t {
    return static_cast<int16_t>(static_cast<int32_t>(gFonts.lineHeight()) *
                                m.lineSpacingPct / 100);
  };

  // Emits the buffered line if it fits vertically. Returns false when the
  // page is full, in which case nothing was drawn and y is unchanged.
  auto flushLine = [&]() -> bool {
    if (!gLine.hasContent) return true;

    gFonts.setSize(sizeForBlock(gLine.block, m.fontPx));
    int16_t adv = lineAdvance();
    if (y + adv > bottom) return false;

    if (renderer) drawLine(gLine, y, m, renderer);
    y += adv;
    res.lines++;
    resetLine(gLine);
    return true;
  };

  while (true) {
    SourceState before = src.state();

    Frag f;
    if (!src.next(f)) {
      if (gLine.hasContent && !flushLine()) {
        res.next = lineStart;
        return res;
      }
      res.endOfContent = true;
      res.next = src.state();
      return res;
    }

    curBlock = f.block;

    switch (f.kind) {
      case FragKind::Word: {
        gFonts.setSize(sizeForBlock(f.block, m.fontPx));
        FontStyle style = styleForBlock(f.block, f.emphasis);
        int16_t spaceW = gFonts.advance(FontStyle::Regular, ' ');
        int16_t ww = wordWidth(f.text, f.len, style);
        int16_t indent = indentForBlock(f.block, m.fontPx, f.listDepth);
        int16_t avail = m.w - indent;

        int16_t sep = (gLine.hasContent && !f.joinPrev) ? spaceW : 0;
        bool fits = !gLine.hasContent || (gLine.width + sep + ww) <= avail;
        bool buffered = gLine.count < MAX_WORDS_PER_LINE &&
                        gLine.textLen + f.len <= LINE_TEXT_BYTES;

        if (!fits && f.joinPrev && gLine.count >= 2) {
          // Trailing punctuation cannot start a line on its own, so the word
          // it is glued to comes down with it.
          char prevText[FRAG_TEXT_MAX];
          uint8_t prevLen;
          FontStyle prevStyle;
          int16_t prevWidth;
          detachLastWord(gLine, prevText, prevLen, prevStyle, prevWidth);

          if (!flushLine()) {
            res.next = lineStart;
            return res;
          }
          lineStart = prevWordStart;
          gLine.block = f.block;
          gLine.listDepth = f.listDepth;
          addWord(gLine, prevText, prevLen, prevStyle, prevWidth, 0);
          addWord(gLine, f.text, f.len, style, ww, 0);
        } else if (!fits || !buffered) {
          if (!flushLine()) {
            res.next = lineStart;
            return res;
          }
          // This word opens a line, so this is where the next page resumes if
          // the line turns out not to fit.
          lineStart = before;
          gLine.block = f.block;
          gLine.listDepth = f.listDepth;
          addWord(gLine, f.text, f.len, style, ww, 0);
        } else {
          if (!gLine.hasContent) {
            lineStart = before;
            gLine.block = f.block;
            gLine.listDepth = f.listDepth;
          }
          addWord(gLine, f.text, f.len, style, ww, sep);
        }

        prevWordStart = before;
        break;
      }

      case FragKind::Space:
        // Inter-word spacing is synthesised at draw time, so a Space fragment
        // only matters as a word boundary, which the source already gives us.
        break;

      case FragKind::LineBreak: {
        if (gLine.hasContent) {
          if (!flushLine()) {
            res.next = lineStart;
            return res;
          }
        } else {
          gFonts.setSize(sizeForBlock(curBlock, m.fontPx));
          int16_t adv = lineAdvance();
          if (y + adv > bottom) {
            res.next = lineStart;
            return res;
          }
          y += adv;
        }
        lineStart = src.state();
        break;
      }

      case FragKind::ParaBreak: {
        bool had = gLine.hasContent;
        if (!flushLine()) {
          res.next = lineStart;
          return res;
        }
        if (had && res.lines > 0) y += gapAfterBlock(f.block, m.fontPx);
        lineStart = src.state();
        break;
      }

      case FragKind::Rule: {
        if (!flushLine()) {
          res.next = lineStart;
          return res;
        }
        gFonts.setSize(m.fontPx);
        int16_t adv = lineAdvance();
        if (y + adv > bottom) {
          res.next = lineStart;
          return res;
        }
        if (renderer) renderer->rule(m.x + m.w / 6, y + adv / 2, m.w * 2 / 3);
        y += adv;
        lineStart = src.state();
        break;
      }

      case FragKind::End:
        if (gLine.hasContent && !flushLine()) {
          res.next = lineStart;
          return res;
        }
        res.endOfContent = true;
        res.next = src.state();
        return res;
    }
  }
}
