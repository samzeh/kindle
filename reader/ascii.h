// The reader's fonts only have plain ASCII characters (space to '~'). Book
// text is full of curly quotes, dashes and accented letters, so each one is
// replaced by its nearest ASCII spelling.
#pragma once
#include <stddef.h>
#include <stdint.h>

// Writes the ASCII spelling of a Unicode character into out (up to 4 chars,
// not terminated) and returns its length: 0 means drop the character
// (soft hyphens, zero-width spaces). Control characters come back as a space.
uint8_t asciiFor(uint32_t codepoint, char out[4]);

// Converts a UTF-8 string to ASCII with asciiFor, truncating to fit outSize
// (always terminated). Runs of whitespace collapse to one space and the ends
// are trimmed, which is what titles and chapter names want.
void asciiFromUtf8(const char *in, char *out, size_t outSize);

// Builds a short ASCII string (a title, author or chapter name) one Unicode
// character at a time: whitespace collapses, the ends are trimmed, and
// anything past the buffer's size is dropped.
struct AsciiBuilder {
  char *buf;
  size_t size;
  size_t len = 0;
  bool space = false;
  AsciiBuilder(char *b, size_t n) : buf(b), size(n) { clear(); }
  void clear() {
    len = 0;
    space = false;
    if (size) buf[0] = '\0';
  }
  void add(uint32_t codepoint);
};
