#pragma once
#include <Arduino.h>

#include "text/FontCache.h"

enum class BlockStyle : uint8_t {
  Paragraph = 0,
  H1,
  H2,
  H3,
  H4,
  H5,
  H6,
  Blockquote,
  ListItem,
  Preformatted,
};

enum class FragKind : uint8_t {
  Word,       // a run of non-space characters
  Space,      // one or more collapsed spaces
  LineBreak,  // <br>
  ParaBreak,  // end of a block
  Rule,       // <hr>
  End,
};

// Longest word carried in one fragment. Anything longer is split across
// fragments, which only affects pathological input like bare URLs.
static constexpr uint8_t FRAG_TEXT_MAX = 64;

struct Frag {
  FragKind kind = FragKind::End;
  BlockStyle block = BlockStyle::Paragraph;
  // Inline emphasis only. The layout combines this with the block to pick the
  // final face, so the mapping lives in exactly one place.
  uint8_t emphasis = 0;
  uint8_t listDepth = 0;  // nesting level, for list indentation
  uint8_t len = 0;
  // Set when no whitespace separated this word from the previous one, as in
  // "<strong>word</strong>." where the period is a separate text node. Without
  // this the layout would insert a space and render "word .".
  bool joinPrev = false;
  char text[FRAG_TEXT_MAX];
};

// Emphasis is tracked as a bitmask so nested <em><strong> unwinds correctly.
static constexpr uint8_t EMPH_BOLD = 1 << 0;
static constexpr uint8_t EMPH_ITALIC = 1 << 1;

inline FontStyle styleFromEmphasis(uint8_t mask) {
  switch (mask & (EMPH_BOLD | EMPH_ITALIC)) {
    case EMPH_BOLD: return FontStyle::Bold;
    case EMPH_ITALIC: return FontStyle::Italic;
    case EMPH_BOLD | EMPH_ITALIC: return FontStyle::BoldItalic;
    default: return FontStyle::Regular;
  }
}

// Everything needed to resume parsing at a page boundary. The page index
// stores one of these per page, so seeking never rewinds to the chapter start.
struct SourceState {
  uint32_t offset = 0;
  // Fragments already consumed from the token at `offset`. A single XHTML
  // text run yields many words, so a byte offset alone cannot address a
  // resume point in the middle of one.
  uint16_t sub = 0;
  uint8_t block = static_cast<uint8_t>(BlockStyle::Paragraph);
  uint8_t emphasis = 0;
  uint8_t listDepth = 0;
};

// A stream of styled fragments. Implemented over plain text and over the
// streaming XHTML tokeniser.
class ContentSource {
 public:
  virtual ~ContentSource() {}

  // Position and parser state *before* the next fragment is read.
  virtual SourceState state() const = 0;

  // Fetch the next fragment. Returns false at end of content.
  virtual bool next(Frag& out) = 0;

  // Resume from a previously captured state.
  virtual bool restore(const SourceState& s) = 0;

  virtual uint32_t sizeBytes() const = 0;
};

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

// Decodes one codepoint, advancing *i. Invalid bytes decode as U+FFFD so a
// malformed book degrades rather than desyncing the whole page.
inline uint32_t utf8Next(const char* s, size_t len, size_t* i) {
  if (*i >= len) return 0;

  uint8_t c = static_cast<uint8_t>(s[*i]);
  if (c < 0x80) {
    (*i)++;
    return c;
  }

  size_t extra;
  uint32_t cp;
  if ((c & 0xE0) == 0xC0) {
    extra = 1;
    cp = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    extra = 2;
    cp = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    extra = 3;
    cp = c & 0x07;
  } else {
    (*i)++;
    return 0xFFFD;
  }

  if (*i + extra >= len) {  // truncated sequence
    *i = len;
    return 0xFFFD;
  }

  (*i)++;
  for (size_t k = 0; k < extra; k++) {
    uint8_t cc = static_cast<uint8_t>(s[*i]);
    if ((cc & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | (cc & 0x3F);
    (*i)++;
  }
  return cp;
}

// Appends a codepoint as UTF-8. Returns bytes written, 0 if it would overflow.
inline uint8_t utf8Encode(uint32_t cp, char* out, uint8_t space) {
  if (cp < 0x80) {
    if (space < 1) return 0;
    out[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    if (space < 2) return 0;
    out[0] = static_cast<char>(0xC0 | (cp >> 6));
    out[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    if (space < 3) return 0;
    out[0] = static_cast<char>(0xE0 | (cp >> 12));
    out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  if (space < 4) return 0;
  out[0] = static_cast<char>(0xF0 | (cp >> 18));
  out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  out[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}
