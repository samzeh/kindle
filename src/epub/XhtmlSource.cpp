#include "epub/XhtmlSource.h"

namespace {

// Length of the whitespace run starting at `p`, or 0 if it is not whitespace.
//
// U+00A0 arrives here already decoded to UTF-8 (C2 A0), so it has to be
// matched as a pair. Testing the trailing A0 byte alone would split the
// sequence and leave an orphan C2 attached to the previous word.
inline uint8_t spaceRunAt(const char* s, uint16_t len, uint16_t p) {
  char c = s[p];
  if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
    return 1;
  }
  if (static_cast<uint8_t>(c) == 0xC2 && p + 1 < len &&
      static_cast<uint8_t>(s[p + 1]) == 0xA0) {
    // Treated as breakable: on a 2.19" column, honouring no-break would
    // overflow the line more often than it would help.
    return 2;
  }
  return 0;
}

bool eq(const char* a, const char* b) {
  return strcmp(a, b) == 0;
}

// Elements whose content is not body text.
bool isSkippedElement(const char* n) {
  return eq(n, "head") || eq(n, "script") || eq(n, "style") || eq(n, "svg") ||
         eq(n, "title");
}

// Returns true and sets `out` if the tag opens a block.
bool blockForTag(const char* n, BlockStyle& out) {
  if (eq(n, "p")) { out = BlockStyle::Paragraph; return true; }
  if (eq(n, "div")) { out = BlockStyle::Paragraph; return true; }
  if (eq(n, "h1")) { out = BlockStyle::H1; return true; }
  if (eq(n, "h2")) { out = BlockStyle::H2; return true; }
  if (eq(n, "h3")) { out = BlockStyle::H3; return true; }
  if (eq(n, "h4")) { out = BlockStyle::H4; return true; }
  if (eq(n, "h5")) { out = BlockStyle::H5; return true; }
  if (eq(n, "h6")) { out = BlockStyle::H6; return true; }
  if (eq(n, "blockquote")) { out = BlockStyle::Blockquote; return true; }
  if (eq(n, "li")) { out = BlockStyle::ListItem; return true; }
  if (eq(n, "pre")) { out = BlockStyle::Preformatted; return true; }
  return false;
}

bool isItalicTag(const char* n) {
  return eq(n, "em") || eq(n, "i") || eq(n, "cite") || eq(n, "dfn") ||
         eq(n, "var") || eq(n, "address");
}

bool isBoldTag(const char* n) {
  return eq(n, "strong") || eq(n, "b");
}

}  // namespace

bool XhtmlSource::begin(ByteFeed* feed) {
  feed_ = feed;
  if (!feed_) return false;
  xml_.begin(feed_, 0);
  resetState();
  return true;
}

void XhtmlSource::resetState() {
  runOffset_ = 0;
  runPos_ = 0;
  runLen_ = 0;
  runFrags_ = 0;
  block_ = BlockStyle::Paragraph;
  emphasis_ = 0;
  listDepth_ = 0;
  skipUntil_[0] = '\0';
  sawSpace_ = false;
  lastWasWord_ = false;
}

SourceState XhtmlSource::state() const {
  SourceState s;
  if (runPos_ < runLen_) {
    s.offset = runOffset_;
    s.sub = runFrags_;
  } else {
    s.offset = xml_.tell();
    s.sub = 0;
  }
  s.block = static_cast<uint8_t>(block_);
  s.emphasis = emphasis_;
  s.listDepth = listDepth_;
  return s;
}

bool XhtmlSource::restore(const SourceState& s) {
  if (!feed_) return false;

  runPos_ = 0;
  runLen_ = 0;
  runFrags_ = 0;
  skipUntil_[0] = '\0';
  // A resume point is always the start of a line, where nothing can be joined
  // to a preceding word. The layout guarantees it never breaks a page between
  // a word and a fragment joined to it.
  sawSpace_ = false;
  lastWasWord_ = false;

  block_ = static_cast<BlockStyle>(s.block);
  emphasis_ = s.emphasis;
  listDepth_ = s.listDepth;

  if (!xml_.seek(s.offset)) return false;

  if (s.sub > 0) {
    // The resume point is inside a text run: re-read it and drop the words
    // that belonged to the previous page.
    if (xml_.next() != XmlToken::Text) return false;
    runOffset_ = s.offset;
    loadRun();
    Frag scratch;
    for (uint16_t i = 0; i < s.sub; i++) {
      if (!wordFromRun(scratch)) break;
    }
  }
  return true;
}

void XhtmlSource::loadRun() {
  runLen_ = xml_.textLen();
  if (runLen_ > sizeof(run_)) runLen_ = sizeof(run_);
  memcpy(run_, xml_.text(), runLen_);
  runPos_ = 0;
  runFrags_ = 0;
}

bool XhtmlSource::wordFromRun(Frag& out) {
  while (runPos_ < runLen_) {
    uint8_t n = spaceRunAt(run_, runLen_, runPos_);
    if (n == 0) break;
    runPos_ += n;
    sawSpace_ = true;
  }
  if (runPos_ >= runLen_) return false;

  out.kind = FragKind::Word;
  out.block = block_;
  out.emphasis = emphasis_;
  out.listDepth = listDepth_;
  out.joinPrev = lastWasWord_ && !sawSpace_;
  out.len = 0;

  while (runPos_ < runLen_ && out.len < FRAG_TEXT_MAX &&
         spaceRunAt(run_, runLen_, runPos_) == 0) {
    out.text[out.len++] = run_[runPos_++];
  }

  sawSpace_ = false;
  lastWasWord_ = true;
  runFrags_++;
  return out.len > 0;
}

bool XhtmlSource::next(Frag& out) {
  if (runPos_ < runLen_ && wordFromRun(out)) return true;

  while (true) {
    uint32_t tokenStart = xml_.tell();
    XmlToken tok = xml_.next();

    if (tok == XmlToken::Eof) return false;

    if (tok == XmlToken::Text) {
      if (skipUntil_[0]) continue;
      runOffset_ = tokenStart;
      loadRun();
      if (wordFromRun(out)) return true;
      continue;  // whitespace-only run
    }

    const char* n = xml_.name();

    if (tok == XmlToken::StartTag) {
      if (skipUntil_[0]) continue;

      if (isSkippedElement(n) && !xml_.selfClosing()) {
        strncpy(skipUntil_, n, sizeof(skipUntil_) - 1);
        skipUntil_[sizeof(skipUntil_) - 1] = '\0';
        continue;
      }

      if (eq(n, "br")) {
        out.kind = FragKind::LineBreak;
        out.block = block_;
        out.emphasis = emphasis_;
        out.listDepth = listDepth_;
        out.len = 0;
        out.joinPrev = false;
        lastWasWord_ = false;
        sawSpace_ = false;
        return true;
      }

      if (eq(n, "hr")) {
        out.kind = FragKind::Rule;
        out.block = BlockStyle::Paragraph;
        out.emphasis = 0;
        out.listDepth = 0;
        out.len = 0;
        out.joinPrev = false;
        lastWasWord_ = false;
        sawSpace_ = false;
        return true;
      }

      if (eq(n, "ul") || eq(n, "ol")) {
        if (listDepth_ < 8) listDepth_++;
        continue;
      }

      if (isItalicTag(n)) {
        emphasis_ |= EMPH_ITALIC;
        continue;
      }
      if (isBoldTag(n)) {
        emphasis_ |= EMPH_BOLD;
        continue;
      }

      BlockStyle block;
      if (blockForTag(n, block)) {
        block_ = block;
        if (block == BlockStyle::ListItem) {
          // Emitting the bullet as a normal word keeps the layout engine free
          // of any "first line of a list item" state, which would otherwise
          // have to survive a page break.
          out.kind = FragKind::Word;
          out.block = block_;
          out.emphasis = emphasis_;
          out.listDepth = listDepth_;
          out.joinPrev = false;
          out.len = static_cast<uint8_t>(
              utf8Encode(0x2022, out.text, FRAG_TEXT_MAX));
          lastWasWord_ = true;
          sawSpace_ = true;  // the item's text must not glue to the bullet
          return true;
        }
      }
      continue;
    }

    // EndTag
    if (skipUntil_[0]) {
      if (eq(n, skipUntil_)) skipUntil_[0] = '\0';
      continue;
    }

    if (eq(n, "ul") || eq(n, "ol")) {
      if (listDepth_ > 0) listDepth_--;
      continue;
    }
    if (isItalicTag(n)) {
      emphasis_ &= ~EMPH_ITALIC;
      continue;
    }
    if (isBoldTag(n)) {
      emphasis_ &= ~EMPH_BOLD;
      continue;
    }

    BlockStyle ended;
    if (blockForTag(n, ended)) {
      out.kind = FragKind::ParaBreak;
      out.block = ended;
      out.emphasis = 0;
      out.listDepth = listDepth_;
      out.len = 0;
      out.joinPrev = false;
      block_ = BlockStyle::Paragraph;
      lastWasWord_ = false;
      sawSpace_ = false;
      return true;
    }
  }
}
