# Tools

## fetch_books.sh

Downloads four public-domain books (Pride and Prejudice, Frankenstein,
Dracula, Moby-Dick) from Project Gutenberg as EPUB files into
`simulator/books/`, for trying the reader in the simulator.

    sh tools/fetch_books.sh

## make_fixture_epubs.py

Builds the small test EPUBs in `simulator/fixtures/library/`, each written to
exercise different EPUB features (EPUB 2 and 3, covers, tables of contents,
folders, odd links). The output is committed; re-run this only after changing
it. Needs Pillow (`python3 -m pip install pillow`).

    python3 tools/make_fixture_epubs.py

## epub_cover.py (no longer used by the reader)

Extracts a book's cover from its EPUB file, byte for byte, as a C header.
The reader used to have covers compiled in this way; it now reads them from
the EPUB itself (`reader/epub.cpp`), whose lookup mirrors this script's.
Kept for reference.

    python3 tools/epub_cover.py path/to/book.epub NAME > cover.h

## make_cover.py (no longer used by the reader)

Turns a cover image into a pre-dithered 1-bit C array (`COVER_PRIDE`), from
when covers were compiled in. Covers now come from each EPUB and are
dithered on the device (`reader/cover.cpp`). Kept for reference.
