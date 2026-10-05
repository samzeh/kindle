// Reads an EPUB's structure: its title, author and cover, the order of its
// chapter files (the "spine"), and its table of contents.
//
// The package file (OPF) is found through META-INF/container.xml. The cover
// is the manifest item with properties="cover-image" (EPUB 3) or the one
// named by <meta name="cover"> (EPUB 2). The table of contents is the EPUB 3
// navigation document (<nav epub:type="toc">) if there is one, otherwise the
// EPUB 2 NCX file. Text is converted to ASCII (see ascii.h).
#pragma once
#include <stdint.h>
#include <vector>

#include "xml.h"
#include "zip.h"

static const uint16_t EPUB_PATH_MAX = 192;

struct EpubInfo {
  char title[96];
  char author[64];
  char identifier[64];
  char coverPath[EPUB_PATH_MAX];  // path inside the ZIP; "" if no cover
  char tocPath[EPUB_PATH_MAX];    // nav document or NCX; "" if none
  bool tocIsNcx;
  // The book's stylesheets (fnv1a of their paths), for page-break rules.
  static const uint8_t MAX_STYLESHEETS = 8;
  uint32_t styleSheets[MAX_STYLESHEETS];
  uint8_t styleSheetCount;
};

// A CSS rule about page breaks before matching elements, reduced to its
// simple selector: a tag, a class, or both (fnv1a; 0 = any). "h2",
// ".chapter" and "div.chapter" are kept; selectors with ids, attributes or
// more than one class only count their tag and first class.
//
// `breaks` is true for page-break-before / break-before: always, page, left
// or right, and false for avoid or auto -- a rule that cancels a less
// specific one, as in "h2 { page-break-before: always }" with
// ".no-break { page-break-before: avoid }".
struct BreakRule {
  uint32_t tag;
  uint32_t cls;
  bool breaks;
};

// One chapter file, in reading order. Only the hash of its path is kept; the
// ZIP can be searched by hash.
struct SpineItem {
  uint32_t pathHash;
  ZipEntry entry;
};

// A table-of-contents entry: which spine file it points into, and which
// element in that file (fnv1a of the id after '#'; 0 = the start of the file).
struct TocEntry {
  uint16_t spine;
  uint32_t fragmentHash;
  uint8_t depth;  // 0 = top level
  char title[48];
};

// Reads container.xml and the package file. False if this is not a usable
// EPUB (missing files, or a spine with no readable chapters).
bool epubReadPackage(const Zip &zip, EpubInfo &info, std::vector<SpineItem> &spine);

// Reads the table of contents, keeping entries that point into the spine.
// Leaves `toc` empty (and returns true) if the book has none.
bool epubReadToc(const Zip &zip, const EpubInfo &info, const std::vector<SpineItem> &spine,
                 std::vector<TocEntry> &toc);

// Reads the page-break rules from the book's stylesheets (see BreakRule).
bool epubReadBreakRules(const Zip &zip, const EpubInfo &info, std::vector<BreakRule> &rules);

// Parses CSS text into page-break rules; exposed for tests.
void epubParseBreakRules(ByteReader &css, std::vector<BreakRule> &rules);

// Resolves a link found in the file `from` (both paths inside the ZIP):
// joins it to from's folder, handles "../" and %-escapes, and splits off
// any "#fragment". Writes the path to out; returns fnv1a(fragment), or 0.
uint32_t epubResolve(const char *from, const char *href, char *out, size_t outSize);

// Parses one file from the ZIP with an XML handler.
bool epubParseFile(const Zip &zip, const char *path, XmlHandler &h);
