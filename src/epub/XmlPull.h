#pragma once
#include <Arduino.h>

#include "epub/ByteFeed.h"

enum class XmlToken : uint8_t {
  None,
  StartTag,
  EndTag,
  Text,
  Eof,
};

// Streaming pull parser for the XML and XHTML inside an EPUB.
//
// Deliberately not a DOM parser: a single Gutenberg chapter can be several
// hundred KB of XHTML and the ESP-WROOM-32 has no PSRAM, so nothing larger
// than one text run is ever resident.
class XmlPull {
 public:
  // Not NAME_MAX: that is a POSIX macro from <sys/syslimits.h>.
  static constexpr uint8_t TAG_NAME_MAX = 32;
  static constexpr uint16_t TEXT_MAX = 256;
  static constexpr uint16_t ATTR_BUF = 256;

  void begin(ByteFeed* feed, uint32_t offset = 0);
  bool seek(uint32_t offset);

  // Offset of the next unread byte, i.e. where the following token starts.
  uint32_t tell() const { return bufStart_ + bufPos_; }

  XmlToken next();

  // Tag name, lowercased with any namespace prefix stripped.
  const char* name() const { return name_; }
  bool selfClosing() const { return selfClosing_; }

  // Attribute lookup by lowercased key. Returns nullptr when absent or when
  // the attribute did not fit in the fixed buffer.
  const char* attr(const char* key) const;

  const char* text() const { return text_; }
  uint16_t textLen() const { return textLen_; }

 private:
  int peekByte();
  int getByte();
  bool fill();

  void readName();
  void readAttrs();
  void readText();
  void skipUntilGt();
  bool matchAhead(const char* literal);
  uint32_t readEntity();
  void pushText(uint32_t codepoint);

  ByteFeed* feed_ = nullptr;
  uint8_t buf_[256];
  uint16_t bufPos_ = 0;
  uint16_t bufLen_ = 0;
  uint32_t bufStart_ = 0;

  char name_[TAG_NAME_MAX];
  bool selfClosing_ = false;

  char attrs_[ATTR_BUF];  // packed key\0value\0 pairs, terminated by an empty key
  uint16_t attrLen_ = 0;

  char text_[TEXT_MAX];
  uint16_t textLen_ = 0;
  bool inCdata_ = false;
};
