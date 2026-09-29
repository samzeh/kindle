# Tools

## make_cover.py

Turns a cover image into the 1-bit C array the reader draws.

    python3 -m pip install pillow
    python3 tools/make_cover.py path/to/cover.jpg pride

Writes `reader/covers/pride.h` defining `COVER_PRIDE`. To wire it up, add to
`reader/books.cpp` (where the book table is defined):

    #include "covers/pride.h"
    ...
    { "Pride and Prejudice", "Jane Austen", SAMPLE_TEXT, COVER_PRIDE },

Each cover is 204 x 306 and about 7.9 KB, which becomes roughly 40 KB of
source text. That is nothing against 4 MB of flash, but it is why the shelf
starts at four books rather than twenty.
