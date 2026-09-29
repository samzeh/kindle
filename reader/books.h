// The shelf. Everything here is const so the ESP32 keeps it in flash rather
// than RAM.
//
// Text format is the one layout.h documents: one paragraph per line, "# " for
// a heading, _underscores_ for italics.
#pragma once
#include <stdint.h>

struct Book {
  const char *title;
  const char *author;
  const char *text;
  const uint8_t *cover;  // 1bpp 204 x 306, 1 = ink; nullptr = placeholder
};

extern const Book BOOKS[];
extern const uint8_t BOOK_COUNT;
