#include "charset.h"

#include <string.h>

// The 27 characters Windows-1252 puts in 0x80-0x9F, where Latin-1 has
// nothing. Everything else in the charset is its own Unicode value, so this
// is the only table needed. (0x81, 0x8D, 0x8F, 0x90 and 0x9D are undefined in
// cp1252 and never produced.)
struct HighEntry {
  uint16_t codepoint;
  uint8_t byte;
};

static const HighEntry CP1252_HIGH[] = {
  { 0x20AC, 0x80 },  // euro
  { 0x201A, 0x82 },  // single low quote
  { 0x0192, 0x83 },  // florin
  { 0x201E, 0x84 },  // double low quote
  { 0x2026, 0x85 },  // ellipsis
  { 0x2020, 0x86 },  // dagger
  { 0x2021, 0x87 },  // double dagger
  { 0x02C6, 0x88 },  // circumflex
  { 0x2030, 0x89 },  // per mille
  { 0x0160, 0x8A },  // S caron
  { 0x2039, 0x8B },  // single left angle quote
  { 0x0152, 0x8C },  // OE
  { 0x017D, 0x8E },  // Z caron
  { 0x2018, 0x91 },  // left single quote
  { 0x2019, 0x92 },  // right single quote
  { 0x201C, 0x93 },  // left double quote
  { 0x201D, 0x94 },  // right double quote
  { 0x2022, 0x95 },  // bullet
  { 0x2013, 0x96 },  // en dash
  { 0x2014, 0x97 },  // em dash
  { 0x02DC, 0x98 },  // small tilde
  { 0x2122, 0x99 },  // trademark
  { 0x0161, 0x9A },  // s caron
  { 0x203A, 0x9B },  // single right angle quote
  { 0x0153, 0x9C },  // oe
  { 0x017E, 0x9E },  // z caron
  { 0x0178, 0x9F },  // Y diaeresis
};

static uint8_t put(char out[4], const char *s) {
  uint8_t n = (uint8_t)strlen(s);
  memcpy(out, s, n);
  return n;
}

static uint8_t one(char out[4], uint8_t byte) {
  out[0] = (char)byte;
  return 1;
}

uint8_t charsetFor(uint32_t cp, char out[4]) {
  if (cp >= 0x20 && cp < 0x7F) return one(out, (uint8_t)cp);
  if (cp < 0x20 || cp == 0x7F) return put(out, " ");

  // Invisible characters first: these are in the charset's range but must not
  // reach the text. A no-break space has to become a real space or the words
  // either side would never be split for line breaking.
  switch (cp) {
    case 0xA0: case 0x2002: case 0x2003: case 0x2004: case 0x2005: case 0x2006:
    case 0x2007: case 0x2008: case 0x2009: case 0x200A: case 0x202F: case 0x3000:
      return put(out, " ");
    case 0xAD: case 0x200B: case 0x200C: case 0x200D: case 0x2060: case 0xFEFF:
      return 0;
  }

  // In the charset as itself: all of Latin-1 above the two handled above.
  if (cp >= 0xA1 && cp <= 0xFF) return one(out, (uint8_t)cp);

  // In the charset in the 0x80-0x9F block.
  for (size_t i = 0; i < sizeof(CP1252_HIGH) / sizeof(CP1252_HIGH[0]); i++)
    if (CP1252_HIGH[i].codepoint == cp) return one(out, CP1252_HIGH[i].byte);

  // Outside the charset: the nearest spelling, so it degrades rather than
  // vanishing. These are the characters books actually contain; anything else
  // becomes '?'.
  switch (cp) {
    case 0x201B: case 0x2032: return put(out, "'");
    case 0x201F: case 0x2033: return put(out, "\"");
    case 0x2010: case 0x2011: case 0x2012: case 0x2212: return put(out, "-");
    case 0x2015: return put(out, "--");
    case 0x2023: case 0x25CF: return put(out, "*");
    case 0x2044: return put(out, "/");
    case 0x0100: case 0x0102: case 0x0104: return put(out, "A");
    case 0x0101: case 0x0103: case 0x0105: return put(out, "a");
    case 0x0106: case 0x0108: case 0x010A: case 0x010C: return put(out, "C");
    case 0x0107: case 0x0109: case 0x010B: case 0x010D: return put(out, "c");
    case 0x010E: case 0x0110: return put(out, "D");
    case 0x010F: case 0x0111: return put(out, "d");
    case 0x0112: case 0x0114: case 0x0116: case 0x0118: case 0x011A: return put(out, "E");
    case 0x0113: case 0x0115: case 0x0117: case 0x0119: case 0x011B: return put(out, "e");
    case 0x011C: case 0x011E: case 0x0120: case 0x0122: return put(out, "G");
    case 0x011D: case 0x011F: case 0x0121: case 0x0123: return put(out, "g");
    case 0x0128: case 0x012A: case 0x012C: case 0x012E: case 0x0130: return put(out, "I");
    case 0x0129: case 0x012B: case 0x012D: case 0x012F: case 0x0131: return put(out, "i");
    case 0x0139: case 0x013B: case 0x013D: case 0x013F: case 0x0141: return put(out, "L");
    case 0x013A: case 0x013C: case 0x013E: case 0x0140: case 0x0142: return put(out, "l");
    case 0x0143: case 0x0145: case 0x0147: return put(out, "N");
    case 0x0144: case 0x0146: case 0x0148: return put(out, "n");
    case 0x014C: case 0x014E: case 0x0150: return put(out, "O");
    case 0x014D: case 0x014F: case 0x0151: return put(out, "o");
    case 0x0154: case 0x0156: case 0x0158: return put(out, "R");
    case 0x0155: case 0x0157: case 0x0159: return put(out, "r");
    case 0x015A: case 0x015C: case 0x015E: return put(out, "S");
    case 0x015B: case 0x015D: case 0x015F: return put(out, "s");
    case 0x0162: case 0x0164: case 0x0166: return put(out, "T");
    case 0x0163: case 0x0165: case 0x0167: return put(out, "t");
    case 0x0168: case 0x016A: case 0x016C: case 0x016E: case 0x0170: case 0x0172:
      return put(out, "U");
    case 0x0169: case 0x016B: case 0x016D: case 0x016F: case 0x0171: case 0x0173:
      return put(out, "u");
    case 0x0174: return put(out, "W");
    case 0x0175: return put(out, "w");
    case 0x0176: return put(out, "Y");
    case 0x0177: return put(out, "y");
    case 0x0179: case 0x017B: return put(out, "Z");
    case 0x017A: case 0x017C: return put(out, "z");
  }
  return put(out, "?");
}

void charsetFromUtf8(const char *in, char *out, size_t outSize) {
  CharsetBuilder b(out, outSize);
  const uint8_t *p = (const uint8_t *)in;
  while (*p) {
    uint32_t cp = *p++;
    uint8_t extra = cp >= 0xF0 ? 3 : cp >= 0xE0 ? 2 : cp >= 0xC0 ? 1 : 0;
    if (extra) {
      cp &= 0x3F >> extra;
      for (uint8_t i = 0; i < extra && (*p & 0xC0) == 0x80; i++) cp = cp << 6 | (*p++ & 0x3F);
    }
    b.add(cp);
  }
}

void CharsetBuilder::add(uint32_t codepoint) {
  char tmp[4];
  uint8_t n = charsetFor(codepoint, tmp);
  for (uint8_t i = 0; i < n; i++) {
    if (tmp[i] == ' ') {
      space = len > 0;
      continue;
    }
    if (space && len + 1 < size) buf[len++] = ' ';
    space = false;
    if (len + 1 < size) buf[len++] = tmp[i];
  }
  if (size) buf[len] = '\0';
}
