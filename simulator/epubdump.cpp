// Runs an EPUB through the reader's EPUB code and prints what it found:
// metadata, cover, chapters and the converted text. For checking books on
// the Mac.
//
//   make epub-dump && ./epub-dump path/to/book.epub [--text]
//
// Markers in the text are shown as [H] (heading), [N] (no indent) and
// [I] (italic toggle).
#include <libgen.h>
#include <stdio.h>
#include <string.h>

#include <string>

#include "Arduino.h"
#include "convert.h"
#include "epub.h"
#include "host_platform.h"
#include "storage.h"
#include "textformat.h"
#include "zip.h"

SerialPort Serial;

struct StringSink : TextSink {
  std::string s;
  bool write(const char *p, uint32_t n) override {
    s.append(p, n);
    return true;
  }
};

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: %s book.epub [--text]\n", argv[0]);
    return 2;
  }
  bool showText = argc > 2 && strcmp(argv[2], "--text") == 0;
  std::string path = argv[1];
  std::string dir = path.substr(0, path.find_last_of('/') + 1);
  std::string name = path.substr(path.find_last_of('/') + 1);
  hostStorageSetRoots(dir.empty() ? "." : dir.c_str(), "/tmp");

  std::string storagePath = "/books/" + name;
  StorageFile *f = storageOpen(storagePath.c_str(), false);
  if (!f) {
    fprintf(stderr, "cannot open %s\n", path.c_str());
    return 1;
  }
  Zip zip;
  if (!zip.open(f)) {
    fprintf(stderr, "not a ZIP file\n");
    return 1;
  }
  EpubInfo info;
  std::vector<SpineItem> spine;
  if (!epubReadPackage(zip, info, spine)) {
    fprintf(stderr, "not a usable EPUB\n");
    return 1;
  }
  std::vector<TocEntry> toc;
  bool tocOk = epubReadToc(zip, info, spine, toc);

  unsigned long t0 = millis();
  StringSink sink;
  std::vector<Chapter> chapters;
  bool ok = convertBook(zip, spine, toc, sink, chapters, nullptr, nullptr);
  unsigned long ms = millis() - t0;

  printf("title:      %s\nauthor:     %s\nidentifier: %s\n", info.title, info.author, info.identifier);
  printf("cover:      %s\ntoc:        %s (%s, %s)\n", info.coverPath[0] ? info.coverPath : "(none)",
         info.tocPath[0] ? info.tocPath : "(none)", info.tocIsNcx ? "NCX" : "nav",
         tocOk ? "ok" : "FAILED");
  printf("zip:        %u entries, spine %zu files, toc %zu entries\n", zip.count(), spine.size(),
         toc.size());
  printf("converted:  %s, %zu bytes in %lu ms\n", ok ? "ok" : "FAILED", sink.s.size(), ms);

  size_t high = 0;
  for (unsigned char c : sink.s)
    if (c >= 0x80) high++;
  printf("non-ASCII bytes left: %zu\n\nchapters (%zu):\n", high, chapters.size());
  for (const Chapter &c : chapters) {
    // Show the first words at the chapter's offset, to check it lands right.
    std::string at = c.offset < sink.s.size() ? sink.s.substr(c.offset, 40) : "(past end)";
    for (char &ch : at)
      if (ch == '\n') ch = '|';
      else if (ch < 0x20) ch = '^';
    printf("  %*s%-44s @%-8u \"%s\"\n", c.depth * 2, "", c.title, c.offset, at.c_str());
  }

  if (showText) {
    printf("\n---- text ----\n");
    for (char ch : sink.s) {
      if (ch == TXT_HEADING) fputs("[H]", stdout);
      else if (ch == TXT_NOINDENT) fputs("[N]", stdout);
      else if (ch == TXT_ITALIC) fputs("[I]", stdout);
      else putchar(ch);
    }
    putchar('\n');
  }
  storageClose(f);
  return ok ? 0 : 1;
}
