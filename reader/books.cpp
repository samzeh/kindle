#include "books.h"

#include "sample_text.h"
#include "texts/dracula.h"
#include "texts/frankenstein.h"
#include "texts/moby_dick.h"

const Book BOOKS[] = {
  { "Pride and Prejudice", "Jane Austen", SAMPLE_TEXT, nullptr },
  { "Frankenstein", "Mary Shelley", FRANKENSTEIN_TEXT, nullptr },
  { "Dracula", "Bram Stoker", DRACULA_TEXT, nullptr },
  { "Moby-Dick", "Herman Melville", MOBY_DICK_TEXT, nullptr },
};

const uint8_t BOOK_COUNT = sizeof(BOOKS) / sizeof(BOOKS[0]);
