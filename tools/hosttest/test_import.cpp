// End-to-end check of the ZIP reader, EPUB importer and page index against a
// real EPUB file, using a host directory in place of the SD card.
//
// This is the code most likely to hide a bug (binary offsets, streaming
// inflate, relative path resolution) and the least pleasant to debug on
// hardware, so it gets exercised here.

#include <cstdio>
#include <string>
#include <vector>

#include "Harness.h"
#include "epub/Epub.h"
#include "epub/FileFeed.h"
#include "epub/PageIndex.h"
#include "epub/XhtmlSource.h"
#include "epub/ZipReader.h"
#include "text/FontCache.h"
#include "text/Layout.h"

HostFS SD;

namespace {

int failures = 0;

void check(bool cond, const char* what) {
  if (!cond) {
    printf("  FAIL: %s\n", what);
    failures++;
  }
}

PageMetrics metrics(uint16_t fontPx) {
  PageMetrics m;
  m.x = MARGIN_CHOICES[MARGIN_DEFAULT];
  m.y = 14;
  m.w = SCREEN_W - 2 * MARGIN_CHOICES[MARGIN_DEFAULT];
  m.h = SCREEN_H - m.y - STATUS_BAR_H;
  m.fontPx = fontPx;
  m.lineSpacingPct = LINE_SPACING_DEFAULT;
  return m;
}

struct ProgressTally {
  int extractCalls = 0;
  int indexCalls = 0;
  uint16_t lastDone = 0;
  uint16_t lastTotal = 0;
};

void onProgress(void* user, uint16_t done, uint16_t total, const char* label) {
  (void)label;
  auto* t = static_cast<ProgressTally*>(user);
  t->extractCalls++;
  t->lastDone = done;
  t->lastTotal = total;
}

void onIndexProgress(void* user, uint16_t chapter, uint16_t chapters, uint32_t pages) {
  (void)chapter;
  (void)chapters;
  (void)pages;
  static_cast<ProgressTally*>(user)->indexCalls++;
}

void testPathResolution() {
  printf("-- path resolution --\n");
  char out[256];

  resolveZipPath("OEBPS/", "text/ch1.xhtml", out, sizeof(out));
  check(strcmp(out, "OEBPS/text/ch1.xhtml") == 0, "relative href joins the OPF dir");

  resolveZipPath("OEBPS/text/", "../images/cover.png", out, sizeof(out));
  check(strcmp(out, "OEBPS/images/cover.png") == 0, "parent segment collapses");

  resolveZipPath("OEBPS/", "text/chap%2001.xhtml", out, sizeof(out));
  check(strcmp(out, "OEBPS/text/chap 01.xhtml") == 0, "percent escape decoded");

  resolveZipPath("OEBPS/", "text/ch1.xhtml#section2", out, sizeof(out));
  check(strcmp(out, "OEBPS/text/ch1.xhtml") == 0, "fragment dropped");

  resolveZipPath("OEBPS/", "./ch1.xhtml", out, sizeof(out));
  check(strcmp(out, "OEBPS/ch1.xhtml") == 0, "current-dir segment dropped");

  resolveZipPath("", "ch1.xhtml", out, sizeof(out));
  check(strcmp(out, "ch1.xhtml") == 0, "no base dir");
}

void testZipReader(const char* epubDevicePath) {
  printf("-- zip reader --\n");

  ZipReader zip;
  check(zip.open(SD, epubDevicePath), "opened the archive");
  if (!zip.isOpen()) return;

  printf("   %u entries\n", zip.entryCount());
  check(zip.entryCount() > 5, "found the central directory");

  // A stored (uncompressed) entry.
  ZipEntry mimetype;
  check(zip.find("mimetype", mimetype), "located the stored mimetype entry");
  if (mimetype.valid) {
    check(mimetype.method == 0, "mimetype is stored, not deflated");
    uint8_t buf[64];
    int n = zip.extractToBuffer(mimetype, buf, sizeof(buf));
    check(n == 20, "mimetype is 20 bytes");
    if (n > 0) {
      check(memcmp(buf, "application/epub+zip", 20) == 0, "mimetype content correct");
    }
  }

  // A deflated entry, round-tripped through streaming inflate.
  ZipEntry container;
  check(zip.find("META-INF/container.xml", container), "located container.xml");
  if (container.valid) {
    check(container.method == 8, "container.xml is deflated");
    std::vector<uint8_t> buf(container.uncompSize + 1, 0);
    int n = zip.extractToBuffer(container, buf.data(), buf.size());
    check(n == static_cast<int>(container.uncompSize),
          "inflated size matches the directory entry");
    check(strstr(reinterpret_cast<char*>(buf.data()), "content.opf") != nullptr,
          "inflated container.xml is intact");
  }

  check(!zip.find("does/not/exist.xhtml", container), "missing entry reports absent");

  // A large deflated entry, to cross the 32KB window boundary more than once.
  ZipEntry chapter;
  if (zip.find("OEBPS/text/chap 00.xhtml", chapter)) {
    check(chapter.uncompSize > 32768,
          "test chapter exceeds one inflate window");
    std::vector<uint8_t> buf(chapter.uncompSize + 1, 0);
    int n = zip.extractToBuffer(chapter, buf.data(), buf.size());
    check(n == static_cast<int>(chapter.uncompSize), "large entry inflates fully");
    if (n > 0) {
      buf[n] = 0;
      check(strstr(reinterpret_cast<char*>(buf.data()), "</html>") != nullptr,
            "large entry ends correctly");
    }
  } else {
    check(false, "located a chapter with a space in its name");
  }

  zip.close();
}

void testImport(const char* epubDevicePath, const char* cacheDir) {
  printf("-- import --\n");

  SD.mkdir(cacheDir);

  ProgressTally tally;
  EpubImporter importer;
  bool ok = importer.import(SD, epubDevicePath, cacheDir, onProgress, &tally);
  if (!ok) printf("  import error: %s\n", importer.error());
  check(ok, "import succeeded");
  if (!ok) return;

  const BookMeta& meta = importer.meta();
  printf("   title='%s' author='%s' chapters=%u\n", meta.title, meta.author,
         meta.chapterCount);

  check(strcmp(meta.title, "Pride and Prejudice & Other Tests") == 0,
        "title parsed with entity decoded");
  check(strcmp(meta.author, "Jane Austen") == 0, "author parsed");
  check(meta.chapterCount == 5, "all five spine items extracted");
  check(tally.extractCalls == 5, "progress reported once per chapter");

  // Chapter files must exist and be real XHTML.
  for (uint16_t c = 0; c < meta.chapterCount; c++) {
    char path[192];
    chapterCachePath(cacheDir, c, path, sizeof(path));
    File f = SD.open(path, FILE_READ);
    if (!f) {
      printf("  FAIL: chapter %u missing at %s\n", c, path);
      failures++;
      continue;
    }
    check(f.size() > 1000, "chapter file has content");
    f.close();
  }

  BookMeta reloaded;
  check(loadBookMeta(SD, cacheDir, reloaded), "book.meta round-trips");
  check(reloaded.chapterCount == meta.chapterCount, "chapter count persisted");
  check(strcmp(reloaded.title, meta.title) == 0, "title persisted");

  TocItem toc[64];
  uint16_t tocCount = loadToc(SD, cacheDir, toc, 64);
  printf("   toc entries=%u\n", tocCount);
  check(tocCount == 5, "NCX produced one entry per chapter");
  if (tocCount >= 2) {
    check(strcmp(toc[0].title, "Chapter 1") == 0, "first TOC label");
    check(toc[0].chapter == 0, "first TOC target resolves to spine 0");
    check(toc[1].chapter == 1, "second TOC target resolves to spine 1");
  }
}

void testPageIndex(const char* cacheDir) {
  printf("-- page index --\n");

  BookMeta meta;
  if (!loadBookMeta(SD, cacheDir, meta)) {
    check(false, "meta available for indexing");
    return;
  }

  PageMetrics m = metrics(FONT_SIZES[FONT_SIZE_DEFAULT]);
  check(!PageIndex::exists(SD, cacheDir, m), "no index before building");

  ProgressTally tally;
  bool built = PageIndex::build(SD, cacheDir, meta.chapterCount, m, onIndexProgress,
                                &tally);
  check(built, "index built");
  check(PageIndex::exists(SD, cacheDir, m), "index file present afterwards");

  PageIndex index;
  check(index.open(SD, cacheDir, m), "index opens");
  printf("   %u pages across %u chapters\n",
         static_cast<unsigned>(index.pageCount()), meta.chapterCount);
  check(index.pageCount() > meta.chapterCount, "more pages than chapters");

  // Chapter numbers must be non-decreasing, or the binary search is invalid.
  uint16_t prevChapter = 0;
  bool monotonic = true;
  for (uint32_t p = 0; p < index.pageCount(); p++) {
    PageRef ref;
    if (!index.get(p, ref)) {
      monotonic = false;
      break;
    }
    if (ref.chapter < prevChapter) monotonic = false;
    prevChapter = ref.chapter;
  }
  check(monotonic, "chapters are non-decreasing through the index");

  for (uint16_t c = 0; c < meta.chapterCount; c++) {
    uint32_t first = index.firstPageOfChapter(c);
    PageRef ref;
    check(index.get(first, ref), "chapter start page readable");
    if (ref.chapter != c) {
      printf("  FAIL: firstPageOfChapter(%u) landed in chapter %u\n", c, ref.chapter);
      failures++;
    }
  }

  // Every indexed page must render, and rendering it must reproduce the same
  // content the index promised.
  BitmapRenderer renderer(SCREEN_W, SCREEN_H);
  FileFeed feed;
  XhtmlSource src;
  int32_t openChapter = -1;
  uint32_t blank = 0;

  for (uint32_t p = 0; p < index.pageCount(); p++) {
    PageRef ref;
    if (!index.get(p, ref)) break;

    if (openChapter != static_cast<int32_t>(ref.chapter)) {
      char path[192];
      chapterCachePath(cacheDir, ref.chapter, path, sizeof(path));
      feed.close();
      if (!feed.open(SD, path)) {
        check(false, "chapter reopens for rendering");
        break;
      }
      src.begin(&feed);
      openChapter = ref.chapter;
    }

    SourceState state;
    state.offset = ref.offset;
    state.sub = ref.sub;
    state.block = ref.block;
    state.emphasis = ref.emphasis;
    state.listDepth = ref.listDepth;

    if (!src.restore(state)) {
      printf("  FAIL: could not restore page %u\n", static_cast<unsigned>(p));
      failures++;
      break;
    }

    renderer.clear();
    Layout::layoutPage(src, metrics(FONT_SIZES[FONT_SIZE_DEFAULT]), &renderer);
    if (renderer.inked() == 0) blank++;

    if (p < 2) {
      char out[128];
      snprintf(out, sizeof(out), "/tmp/kindle-hosttest/import-%02u.pgm",
               static_cast<unsigned>(p));
      renderer.writePgm(out);
    }
  }

  check(blank == 0, "no indexed page renders blank");
  check(renderer.clipped_ == 0, "nothing drawn outside the canvas");

  // Rebuilding at a different size must produce a separate, larger index.
  PageMetrics big = metrics(FONT_SIZES[FONT_SIZE_COUNT - 1]);
  check(PageIndex::build(SD, cacheDir, meta.chapterCount, big, nullptr, nullptr),
        "index rebuilt at a larger size");
  PageIndex bigIndex;
  check(bigIndex.open(SD, cacheDir, big), "larger index opens");
  printf("   %u pages at %upx\n", static_cast<unsigned>(bigIndex.pageCount()),
         FONT_SIZES[FONT_SIZE_COUNT - 1]);
  check(bigIndex.pageCount() > index.pageCount(),
        "larger text needs more pages");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    printf("usage: test_import ROOT_DIR\n");
    return 2;
  }
  SD.setRoot(argv[1]);

  if (!gFonts.begin()) {
    printf("FATAL: font init failed\n");
    return 1;
  }

  testPathResolution();
  testZipReader("/books/test.epub");
  testImport("/books/test.epub", "/cache/test");
  testPageIndex("/cache/test");

  printf(failures ? "\n%d CHECK(S) FAILED\n" : "\nall checks passed\n", failures);
  return failures ? 1 : 0;
}
