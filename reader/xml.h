// A small streaming XML parser, enough for EPUB files (package, table of
// contents and XHTML chapters). Bytes are pushed in as they are read, in
// chunks of any size, and the handler is called for each tag and character.
//
// Tag and attribute names are reported as lower-case local names, without
// any namespace prefix: "dc:title" arrives as "title", "navPoint" as
// "navpoint". Text arrives one Unicode character at a time, with entities
// (&amp; &#8217; &mdash; ...) and UTF-8 already decoded.
#pragma once
#include <stdint.h>

#include "bytes.h"

class XmlAttrs {
public:
  // Value of the attribute with this local name ("href", "type" for
  // epub:type, ...), entities decoded; nullptr if absent.
  const char *get(const char *name) const;

private:
  friend class XmlParser;
  static const uint8_t MAX = 16;
  const char *names_[MAX];
  const char *values_[MAX];
  uint8_t count_ = 0;
};

// Names and attribute values passed to a handler are only valid during that
// call: copy anything that is needed later.
struct XmlHandler {
  virtual void startTag(const char *name, const XmlAttrs &attrs, bool selfClosing) {}
  virtual void endTag(const char *name) {}
  virtual void text(uint32_t codepoint) {}
  virtual ~XmlHandler() {}
};

class XmlParser {
public:
  explicit XmlParser(XmlHandler &h) : handler_(h) {}
  void feed(const uint8_t *data, uint32_t n);

private:
  void byte(uint8_t c);
  void textByte(uint8_t c);
  void emitText(uint32_t codepoint);
  void finishEntity();
  void finishTag();

  enum State : uint8_t { TEXT, TAG, COMMENT, CDATA, SKIP_DECL, ENTITY };
  XmlHandler &handler_;
  State state_ = TEXT;

  // The tag being read, from after '<' to before '>'. Long tags are cut
  // short (attributes past the limit are lost; the name and early
  // attributes survive).
  static const uint16_t TAG_MAX = 512;
  char tag_[TAG_MAX];
  uint16_t tagLen_ = 0;
  char quote_ = 0;  // inside a quoted attribute value

  char entity_[12];
  uint8_t entityLen_ = 0;

  uint32_t utf8_ = 0;  // UTF-8 character being assembled
  uint8_t utf8Left_ = 0;

  uint8_t match_ = 0;  // progress through "-->" or "]]>"
};

// Parses a whole stream. False if reading failed.
bool xmlParse(ByteReader &in, XmlHandler &h);

// Decodes a named or numeric entity body ("amp", "#8217", "#x2019"); 0 if
// unknown.
uint32_t xmlEntity(const char *body);
