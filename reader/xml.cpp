#include "xml.h"

#include <stdlib.h>
#include <string.h>

// The named entities EPUBs actually use. EPUB 3 XHTML only allows the five
// XML ones, but EPUB 2 books often use HTML names.
static const struct {
  const char *name;
  uint16_t cp;
} ENTITIES[] = {
  { "amp", '&' },      { "lt", '<' },       { "gt", '>' },       { "quot", '"' },
  { "apos", '\'' },    { "nbsp", 0xA0 },    { "shy", 0xAD },     { "copy", 0xA9 },
  { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 }, { "lsquo", 0x2018 },
  { "rsquo", 0x2019 }, { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "laquo", 0xAB },
  { "raquo", 0xBB },   { "middot", 0xB7 },  { "deg", 0xB0 },     { "eacute", 0xE9 },
  { "egrave", 0xE8 },  { "agrave", 0xE0 },  { "aacute", 0xE1 },  { "ccedil", 0xE7 },
  { "uuml", 0xFC },    { "ouml", 0xF6 },    { "auml", 0xE4 },    { "iuml", 0xEF },
  { "ecirc", 0xEA },   { "acirc", 0xE2 },   { "ocirc", 0xF4 },   { "aelig", 0xE6 },
  { "oelig", 0x153 },  { "szlig", 0xDF },   { "pound", 0xA3 },   { "sect", 0xA7 },
  { "para", 0xB6 },    { "dagger", 0x2020 }, { "times", 0xD7 },  { "frac12", 0xBD },
};

uint32_t xmlEntity(const char *body) {
  if (body[0] == '#') {
    char *end;
    unsigned long v = (body[1] == 'x' || body[1] == 'X') ? strtoul(body + 2, &end, 16)
                                                         : strtoul(body + 1, &end, 10);
    return (*end == '\0' && v > 0 && v <= 0x10FFFF) ? (uint32_t)v : 0;
  }
  for (const auto &e : ENTITIES)
    if (strcmp(body, e.name) == 0) return e.cp;
  return 0;
}

const char *XmlAttrs::get(const char *name) const {
  for (uint8_t i = 0; i < count_; i++)
    if (strcmp(names_[i], name) == 0) return values_[i];
  return nullptr;
}

void XmlParser::emitText(uint32_t cp) {
  handler_.text(cp);
}

// Text outside tags: decode UTF-8, and hand '&' to the entity state.
void XmlParser::textByte(uint8_t c) {
  if (utf8Left_ > 0) {
    if ((c & 0xC0) == 0x80) {
      utf8_ = (utf8_ << 6) | (c & 0x3F);
      if (--utf8Left_ == 0) emitText(utf8_);
      return;
    }
    utf8Left_ = 0;  // broken sequence: drop it and treat this byte afresh
    emitText('?');
  }
  if (c < 0x80) emitText(c);
  else if ((c & 0xE0) == 0xC0) utf8_ = c & 0x1F, utf8Left_ = 1;
  else if ((c & 0xF0) == 0xE0) utf8_ = c & 0x0F, utf8Left_ = 2;
  else if ((c & 0xF8) == 0xF0) utf8_ = c & 0x07, utf8Left_ = 3;
  else emitText('?');
}

void XmlParser::finishEntity() {
  entity_[entityLen_] = '\0';
  uint32_t cp = xmlEntity(entity_);
  if (cp) {
    emitText(cp);
  } else {  // unknown: keep the text as written
    emitText('&');
    for (uint8_t i = 0; i < entityLen_; i++) emitText((uint8_t)entity_[i]);
    emitText(';');
  }
}

// Decodes entities in an attribute value, in place (the result is never
// longer). Non-ASCII characters are written back as UTF-8.
static void decodeValue(char *s) {
  char *out = s;
  while (*s) {
    char *semi;
    if (*s == '&' && (semi = strchr(s, ';')) && semi - s < 11) {
      *semi = '\0';
      uint32_t cp = xmlEntity(s + 1);
      *semi = ';';
      if (cp) {
        if (cp > 0xFFFF) *out++ = '?';  // beyond what 3 bytes (the entity's length) hold
        else if (cp < 0x80) *out++ = (char)cp;
        else if (cp < 0x800) *out++ = (char)(0xC0 | cp >> 6), *out++ = (char)(0x80 | (cp & 0x3F));
        else *out++ = (char)(0xE0 | cp >> 12), *out++ = (char)(0x80 | (cp >> 6 & 0x3F)),
             *out++ = (char)(0x80 | (cp & 0x3F));
        s = semi + 1;
        continue;
      }
    }
    *out++ = *s++;
  }
  *out = '\0';
}

// Lower-cases a name in place and returns its local part (after any ':').
static char *localName(char *name) {
  char *local = name;
  for (char *p = name; *p; p++) {
    if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    if (*p == ':') local = p + 1;
  }
  return local;
}

static bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// A complete tag is in tag_ (without '<' and '>'): split it into name and
// attributes, in place, and report it.
void XmlParser::finishTag() {
  tag_[tagLen_] = '\0';
  char *p = tag_;

  if (*p == '/') {  // end tag
    p++;
    char *end = p;
    while (*end && !isSpace(*end)) end++;
    *end = '\0';
    handler_.endTag(localName(p));
    return;
  }

  bool selfClosing = tagLen_ > 0 && tag_[tagLen_ - 1] == '/';
  if (selfClosing) tag_[--tagLen_] = '\0';

  char *name = p;
  while (*p && !isSpace(*p)) p++;
  if (*p) *p++ = '\0';

  XmlAttrs attrs;
  while (*p && attrs.count_ < XmlAttrs::MAX) {
    while (isSpace(*p)) p++;
    if (!*p) break;
    char *attrName = p;
    while (*p && *p != '=' && !isSpace(*p)) p++;
    char *nameEnd = p;
    while (isSpace(*p)) p++;
    if (*p != '=') {  // attribute without a value: ignore it
      *nameEnd = '\0';
      continue;
    }
    *nameEnd = '\0';
    p++;
    while (isSpace(*p)) p++;
    char q = *p;
    if (q != '"' && q != '\'') break;
    char *value = ++p;
    while (*p && *p != q) p++;
    if (*p) *p++ = '\0';
    decodeValue(value);
    attrs.names_[attrs.count_] = localName(attrName);
    attrs.values_[attrs.count_] = value;
    attrs.count_++;
  }
  handler_.startTag(localName(name), attrs, selfClosing);
}

void XmlParser::byte(uint8_t c) {
  switch (state_) {
    case TEXT:
      if (c == '<') {
        state_ = TAG;
        tagLen_ = 0;
        quote_ = 0;
      } else if (c == '&') {
        state_ = ENTITY;
        entityLen_ = 0;
      } else {
        textByte(c);
      }
      break;

    case ENTITY:
      if (c == ';') {
        finishEntity();
        state_ = TEXT;
      } else if (entityLen_ < sizeof(entity_) - 1 && c != '<' && c != '&' && !isSpace(c)) {
        entity_[entityLen_++] = (char)c;
      } else {  // not an entity after all: emit what was read, then this byte
        emitText('&');
        for (uint8_t i = 0; i < entityLen_; i++) emitText((uint8_t)entity_[i]);
        state_ = TEXT;
        byte(c);
      }
      break;

    case TAG:
      if (quote_) {
        if (c == quote_) quote_ = 0;
      } else if (c == '"' || c == '\'') {
        quote_ = (char)c;
      } else if (c == '>') {
        state_ = TEXT;
        if (tagLen_ > 0 && tag_[0] != '!' && tag_[0] != '?') finishTag();
        break;
      }
      if (tagLen_ < TAG_MAX - 1) tag_[tagLen_++] = (char)c;
      // Recognise comments, CDATA and declarations as soon as they start.
      if (tagLen_ == 3 && memcmp(tag_, "!--", 3) == 0) state_ = COMMENT, match_ = 0;
      else if (tagLen_ == 8 && memcmp(tag_, "![CDATA[", 8) == 0) state_ = CDATA, match_ = 0;
      else if (tagLen_ == 1 && (c == '?' || c == '!')) {
        // might still become a comment or CDATA; decided above as more arrives
      } else if (tagLen_ == 2 && tag_[0] == '!' && c != '-' && c != '[') state_ = SKIP_DECL;
      else if (tagLen_ == 2 && tag_[0] == '?') state_ = SKIP_DECL;
      break;

    case SKIP_DECL:  // <!DOCTYPE ...>, <?xml ...?>
      if (c == '>') state_ = TEXT;
      break;

    case COMMENT:  // until "-->"
      if (c == '-') match_ = match_ < 2 ? match_ + 1 : 2;
      else if (c == '>' && match_ == 2) state_ = TEXT;
      else match_ = 0;
      break;

    case CDATA:  // text until "]]>"
      if (c == ']') {
        if (match_ < 2) match_++;
        else textByte(']');  // "]]]": the first ']' is text
      } else if (c == '>' && match_ == 2) {
        state_ = TEXT;
      } else {
        for (; match_ > 0; match_--) textByte(']');
        textByte(c);
      }
      break;
  }
}

void XmlParser::feed(const uint8_t *data, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) byte(data[i]);
}

bool xmlParse(ByteReader &in, XmlHandler &h) {
  XmlParser parser(h);
  uint8_t buf[512];
  for (;;) {
    int32_t got = in.read(buf, sizeof(buf));
    if (got < 0) return false;
    if (got == 0) return true;
    parser.feed(buf, (uint32_t)got);
  }
}
