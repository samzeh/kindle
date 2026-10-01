// The books: every .epub file in /books, and the reader's cache of what has
// been worked out about each one.
//
// Each book gets a cache folder, /.reader/<id>/, where <id> is 8 hex digits
// identifying the file (from its name and size):
//   meta.bin   title, author, whether it has a cover, text length
//   cover.bin  the cover, already dithered, at grid and thumbnail size
//   text.txt   the book's text in the reader's format (textformat.h)
//   toc.bin    the chapter list
//   pages.bin  where each page starts (written by reading.cpp)
// Title, author and cover are read when a book is first seen; the text and
// chapters when it is first opened. Files are written under a .tmp name and
// renamed when complete, so an interrupted import just runs again.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "convert.h"

struct BookInfo {
  uint32_t id;
  char file[96];     // file name inside /books
  char title[64];
  char author[48];
  bool hasCover;
  uint32_t textLength;  // 0 until the book has been opened once
};

// Finds the books in /books and reads the title, author and cover of any
// not seen before (about a second each on the ESP32). `progress` (may be
// null) is called before each such book. Books are sorted by title.
// Returns how many books were imported.
typedef void (*CatalogProgress)(uint16_t done, uint16_t total, const char *name, void *ctx);
uint16_t catalogScan(CatalogProgress progress, void *ctx);

uint16_t catalogCount();
const BookInfo &catalogBook(uint16_t index);

// A file in the book's cache folder: catalogPath(book, "text.txt", ...).
void catalogPath(const BookInfo &book, const char *name, char *out, size_t outSize);

// Makes sure the book's text and chapter list exist in its cache, converting
// the EPUB if not (a few seconds for a novel). `progress` (may be null) gets
// 0-100. False if the EPUB could not be read.
typedef void (*ConvertProgress)(uint8_t percent, void *ctx);
bool catalogPrepareText(uint16_t index, ConvertProgress progress, void *ctx);

// Reads the chapter list written by catalogPrepareText.
bool catalogLoadChapters(const BookInfo &book, std::vector<Chapter> &chapters);
