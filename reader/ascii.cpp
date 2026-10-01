#include "ascii.h"

#include <string.h>

// Latin-1 letters U+00C0..U+00FF folded to their base letter.
static const char LATIN1[] =
  "AAAAAAACEEEEIIII"   // C0-CF (C6 AE handled below)
  "DNOOOOOxOUUUUYTs"   // D0-DF (D7 x, DE Th, DF ss handled below)
  "aaaaaaaceeeeiiii"   // E0-EF (E6 ae handled below)
  "dnooooo/ouuuuyty";  // F0-FF (FE th handled below)

static uint8_t put(char out[4], const char *s) {
  uint8_t n = (uint8_t)strlen(s);
  memcpy(out, s, n);
  return n;
}

uint8_t asciiFor(uint32_t cp, char out[4]) {
  if (cp >= 0x20 && cp < 0x7F) {
    out[0] = (char)cp;
    return 1;
  }
  if (cp < 0x20 || cp == 0x7F) return put(out, " ");

  switch (cp) {
    case 0xA0: case 0x2002: case 0x2003: case 0x2004: case 0x2005: case 0x2006:
    case 0x2007: case 0x2008: case 0x2009: case 0x200A: case 0x202F: case 0x3000:
      return put(out, " ");
    case 0xAD: case 0x200B: case 0x200C: case 0x200D: case 0x2060: case 0xFEFF:
      return 0;
    case 0x2018: case 0x2019: case 0x201A: case 0x201B: case 0x2032: case 0xB4:
      return put(out, "'");
    case 0x201C: case 0x201D: case 0x201E: case 0x201F: case 0x2033: case 0xAB: case 0xBB:
      return put(out, "\"");
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2212:
      return put(out, "-");
    case 0x2014: case 0x2015:
      return put(out, "--");
    case 0x2026: return put(out, "...");
    case 0x2022: case 0x2023: case 0x25CF: return put(out, "*");
    case 0xB7: return put(out, ".");
    case 0xA9: return put(out, "(c)");
    case 0xAE: return put(out, "(R)");
    case 0x2122: return put(out, "TM");
    case 0xB0: return put(out, "o");
    case 0xA3: return put(out, "L");
    case 0x20AC: return put(out, "EUR");
    case 0xA7: return put(out, "S");
    case 0xB6: return put(out, "P");
    case 0x2020: case 0x2021: return put(out, "+");
    case 0xBD: return put(out, "1/2");
    case 0xBC: return put(out, "1/4");
    case 0xBE: return put(out, "3/4");
    case 0xC6: return put(out, "AE");
    case 0xE6: return put(out, "ae");
    case 0xDF: return put(out, "ss");
    case 0xDE: return put(out, "Th");
    case 0xFE: return put(out, "th");
    case 0x152: return put(out, "OE");
    case 0x153: return put(out, "oe");
  }
  if (cp >= 0xC0 && cp <= 0xFF) {
    out[0] = LATIN1[cp - 0xC0];
    return 1;
  }
  return put(out, "?");
}

void asciiFromUtf8(const char *in, char *out, size_t outSize) {
  AsciiBuilder b(out, outSize);
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

void AsciiBuilder::add(uint32_t codepoint) {
  char tmp[4];
  uint8_t n = asciiFor(codepoint, tmp);
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
