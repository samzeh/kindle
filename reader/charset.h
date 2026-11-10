// The reader's charset, which is Windows-1252: ASCII up to 0x7E, typographic
// punctuation (curly quotes, en and em dash, ellipsis, bullet, dagger) in
// 0x80-0x9F, and Latin-1 from 0xA0 up. The fonts in reader/fonts cover
// 0x20-0xFF, so every byte in this range has a glyph.
//
// Book text is converted into this charset once, when a book is imported, and
// stored one byte per character. That keeps layout.cpp indexing text by byte,
// keeps page offsets from landing mid-character, and keeps multi-byte decoding
// out of pagination, which walks every character of a book to count its pages.
//
// A character the charset has no room for keeps its nearest spelling ("(c)",
// "1/2", "--"), so it degrades rather than vanishing.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Writes the charset spelling of a Unicode character into out (up to 4 bytes,
// not terminated) and returns its length: 0 means drop the character (soft
// hyphens, zero-width spaces). Control characters come back as a space.
uint8_t charsetFor(uint32_t codepoint, char out[4]);

// Converts a UTF-8 string to the charset with charsetFor, truncating to fit
// outSize (always terminated). Runs of whitespace collapse to one space and
// the ends are trimmed, which is what titles and chapter names want.
void charsetFromUtf8(const char *in, char *out, size_t outSize);

// Builds a short charset string (a title, author or chapter name) one Unicode
// character at a time: whitespace collapses, the ends are trimmed, and
// anything past the buffer's size is dropped.
struct CharsetBuilder {
  char *buf;
  size_t size;
  size_t len = 0;
  bool space = false;
  CharsetBuilder(char *b, size_t n) : buf(b), size(n) { clear(); }
  void clear() {
    len = 0;
    space = false;
    if (size) buf[0] = '\0';
  }
  void add(uint32_t codepoint);
};
