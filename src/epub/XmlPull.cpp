#include "epub/XmlPull.h"

#include "text/Content.h"

namespace {

struct NamedEntity {
  const char* name;
  uint32_t codepoint;
};

// The entities that actually show up in EPUBs. Anything else is emitted
// literally rather than swallowed, so a stray "&" cannot eat a paragraph.
constexpr NamedEntity kEntities[] = {
    {"amp", '&'},      {"lt", '<'},        {"gt", '>'},      {"quot", '"'},
    {"apos", '\''},    {"nbsp", 0x00A0},   {"mdash", 0x2014}, {"ndash", 0x2013},
    {"hellip", 0x2026}, {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"ldquo", 0x201C},
    {"rdquo", 0x201D}, {"bull", 0x2022},   {"copy", 0x00A9}, {"reg", 0x00AE},
    {"deg", 0x00B0},   {"laquo", 0x00AB},  {"raquo", 0x00BB}, {"eacute", 0x00E9},
    {"egrave", 0x00E8}, {"agrave", 0x00E0}, {"ccedil", 0x00E7}, {"uuml", 0x00FC},
    {"ouml", 0x00F6},  {"auml", 0x00E4},   {"szlig", 0x00DF}, {"dagger", 0x2020},
    {"permil", 0x2030}, {"prime", 0x2032}, {"shy", 0x00AD},   {"ensp", 0x2002},
    {"emsp", 0x2003},  {"thinsp", 0x2009},
};

inline bool isNameChar(int c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         c == '_' || c == '-' || c == ':' || c == '.';
}

inline bool isXmlSpace(int c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline char lower(char c) {
  return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

void XmlPull::begin(ByteFeed* feed, uint32_t offset) {
  feed_ = feed;
  seek(offset);
}

bool XmlPull::seek(uint32_t offset) {
  if (!feed_) return false;
  if (!feed_->seek(offset)) return false;
  bufStart_ = offset;
  bufPos_ = 0;
  bufLen_ = 0;
  inCdata_ = false;
  return true;
}

bool XmlPull::fill() {
  if (bufPos_ < bufLen_) return true;
  if (!feed_) return false;

  bufStart_ += bufLen_;
  bufPos_ = 0;
  int got = feed_->read(buf_, sizeof(buf_));
  bufLen_ = got > 0 ? static_cast<uint16_t>(got) : 0;
  return bufLen_ > 0;
}

int XmlPull::peekByte() {
  if (!fill()) return -1;
  return buf_[bufPos_];
}

int XmlPull::getByte() {
  if (!fill()) return -1;
  return buf_[bufPos_++];
}

// Looks ahead for a literal, consuming it only on a full match. The literal
// must be shorter than the buffer so a refill cannot split it.
bool XmlPull::matchAhead(const char* literal) {
  size_t n = strlen(literal);
  for (size_t i = 0; i < n; i++) {
    if (bufPos_ + i >= bufLen_) {
      // Compact the tail to the front so the literal becomes contiguous.
      uint16_t remain = bufLen_ - bufPos_;
      memmove(buf_, buf_ + bufPos_, remain);
      bufStart_ += bufPos_;
      bufPos_ = 0;
      bufLen_ = remain;
      int got = feed_ ? feed_->read(buf_ + remain, sizeof(buf_) - remain) : 0;
      if (got > 0) bufLen_ += static_cast<uint16_t>(got);
      if (bufPos_ + i >= bufLen_) return false;
    }
    if (buf_[bufPos_ + i] != static_cast<uint8_t>(literal[i])) return false;
  }
  bufPos_ += static_cast<uint16_t>(n);
  return true;
}

void XmlPull::skipUntilGt() {
  int c;
  while ((c = getByte()) >= 0) {
    if (c == '>') return;
  }
}

void XmlPull::readName() {
  uint8_t len = 0;
  int colonAt = -1;

  int c;
  while ((c = peekByte()) >= 0 && isNameChar(c)) {
    getByte();
    if (c == ':') {
      colonAt = len;  // namespace prefix, dropped below
    }
    if (len < NAME_MAX - 1) name_[len++] = lower(static_cast<char>(c));
  }
  name_[len] = '\0';

  if (colonAt >= 0 && colonAt + 1 < len) {
    memmove(name_, name_ + colonAt + 1, len - colonAt);
  } else if (colonAt >= 0) {
    name_[0] = '\0';
  }
}

uint32_t XmlPull::readEntity() {
  // The '&' has already been consumed.
  char buf[12];
  uint8_t len = 0;

  int c;
  while ((c = peekByte()) >= 0 && c != ';' && len < sizeof(buf) - 1) {
    if (isXmlSpace(c) || c == '<' || c == '&') break;  // bare '&', not an entity
    buf[len++] = static_cast<char>(getByte());
  }
  buf[len] = '\0';

  if (peekByte() != ';') {
    // Not a well-formed entity: emit it verbatim.
    pushText('&');
    for (uint8_t i = 0; i < len; i++) pushText(static_cast<uint8_t>(buf[i]));
    return 0;
  }
  getByte();  // consume ';'

  if (len == 0) return 0;

  if (buf[0] == '#') {
    uint32_t cp = 0;
    if (buf[1] == 'x' || buf[1] == 'X') {
      for (uint8_t i = 2; i < len; i++) {
        char d = lower(buf[i]);
        cp = cp * 16 + (d >= 'a' ? (d - 'a' + 10) : (d - '0'));
      }
    } else {
      for (uint8_t i = 1; i < len; i++) cp = cp * 10 + (buf[i] - '0');
    }
    return cp ? cp : 0;
  }

  for (const auto& e : kEntities) {
    if (strcmp(buf, e.name) == 0) return e.codepoint;
  }
  return 0xFFFD;
}

void XmlPull::pushText(uint32_t codepoint) {
  if (textLen_ + 4 >= TEXT_MAX) return;
  textLen_ += utf8Encode(codepoint, text_ + textLen_, TEXT_MAX - textLen_ - 1);
}

void XmlPull::readAttrs() {
  attrLen_ = 0;
  selfClosing_ = false;

  while (true) {
    int c = peekByte();
    while (c >= 0 && isXmlSpace(c)) {
      getByte();
      c = peekByte();
    }

    if (c < 0) return;
    if (c == '>') {
      getByte();
      return;
    }
    if (c == '/') {
      getByte();
      selfClosing_ = true;
      continue;
    }
    if (!isNameChar(c)) {
      getByte();  // junk, skip
      continue;
    }

    // Key
    while ((c = peekByte()) >= 0 && isNameChar(c)) {
      getByte();
      if (attrLen_ < ATTR_BUF - 2) attrs_[attrLen_++] = lower(static_cast<char>(c));
    }
    if (attrLen_ < ATTR_BUF - 1) attrs_[attrLen_++] = '\0';

    c = peekByte();
    while (c >= 0 && isXmlSpace(c)) {
      getByte();
      c = peekByte();
    }

    if (c != '=') {
      // Valueless attribute; store an empty value.
      if (attrLen_ < ATTR_BUF - 1) attrs_[attrLen_++] = '\0';
      continue;
    }
    getByte();  // '='

    c = peekByte();
    while (c >= 0 && isXmlSpace(c)) {
      getByte();
      c = peekByte();
    }

    char quote = 0;
    if (c == '"' || c == '\'') {
      quote = static_cast<char>(getByte());
    }

    while (true) {
      c = peekByte();
      if (c < 0) break;
      if (quote && c == quote) {
        getByte();
        break;
      }
      if (!quote && (isXmlSpace(c) || c == '>' || c == '/')) break;

      getByte();
      if (c == '&') {
        // Decode into the attribute buffer via the text buffer machinery.
        uint16_t savedLen = textLen_;
        textLen_ = 0;
        uint32_t cp = readEntity();
        if (cp) pushText(cp);
        for (uint16_t i = 0; i < textLen_; i++) {
          if (attrLen_ < ATTR_BUF - 2) attrs_[attrLen_++] = text_[i];
        }
        textLen_ = savedLen;
      } else if (attrLen_ < ATTR_BUF - 2) {
        attrs_[attrLen_++] = static_cast<char>(c);
      }
    }
    if (attrLen_ < ATTR_BUF - 1) attrs_[attrLen_++] = '\0';

    // Keep room for the terminating empty key.
    if (attrLen_ >= ATTR_BUF - 2) {
      skipUntilGt();
      break;
    }
  }
  attrs_[attrLen_] = '\0';
}

const char* XmlPull::attr(const char* key) const {
  uint16_t i = 0;
  while (i < attrLen_) {
    const char* k = attrs_ + i;
    if (*k == '\0') break;
    i += strlen(k) + 1;
    if (i > attrLen_) break;
    const char* v = attrs_ + i;
    i += strlen(v) + 1;
    if (strcmp(k, key) == 0) return v;
  }
  return nullptr;
}

void XmlPull::readText() {
  textLen_ = 0;

  // Break text runs at a whitespace boundary once the buffer is mostly full,
  // so a word is never split across two Text tokens.
  constexpr uint16_t SOFT_LIMIT = TEXT_MAX - 72;

  while (true) {
    int c = peekByte();
    if (c < 0) break;
    if (c == '<') break;
    if (textLen_ >= TEXT_MAX - 8) break;
    if (textLen_ >= SOFT_LIMIT && isXmlSpace(c)) break;

    getByte();
    if (c == '&') {
      uint32_t cp = readEntity();
      if (cp) pushText(cp);
    } else {
      pushText(static_cast<uint8_t>(c));
    }
  }
  text_[textLen_] = '\0';
}

XmlToken XmlPull::next() {
  while (true) {
    int c = peekByte();
    if (c < 0) return XmlToken::Eof;

    if (c != '<') {
      readText();
      if (textLen_ == 0) continue;
      return XmlToken::Text;
    }

    getByte();  // '<'
    c = peekByte();

    if (c == '/') {
      getByte();
      readName();
      skipUntilGt();
      selfClosing_ = false;
      attrLen_ = 0;
      return XmlToken::EndTag;
    }

    if (c == '!') {
      getByte();
      if (matchAhead("--")) {
        // Comment: scan for the closing marker.
        int a = -1, b = -1;
        while ((c = getByte()) >= 0) {
          if (a == '-' && b == '-' && c == '>') break;
          a = b;
          b = c;
        }
        continue;
      }
      if (matchAhead("[CDATA[")) {
        textLen_ = 0;
        int a = -1, b = -1;
        while ((c = getByte()) >= 0) {
          if (a == ']' && b == ']' && c == '>') {
            if (textLen_ >= 2) textLen_ -= 2;  // drop the trailing "]]"
            break;
          }
          if (textLen_ < TEXT_MAX - 4) text_[textLen_++] = static_cast<char>(c);
          a = b;
          b = c;
        }
        text_[textLen_] = '\0';
        if (textLen_ == 0) continue;
        return XmlToken::Text;
      }
      skipUntilGt();  // DOCTYPE or other declaration
      continue;
    }

    if (c == '?') {
      getByte();
      int prev = -1;
      while ((c = getByte()) >= 0) {
        if (prev == '?' && c == '>') break;
        prev = c;
      }
      continue;
    }

    readName();
    readAttrs();
    if (name_[0] == '\0') continue;
    return XmlToken::StartTag;
  }
}
